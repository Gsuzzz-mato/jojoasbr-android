// jojo_shaders.h. The control section and code block layouts are those of docs/nvn/SHADERS.md; the BNVN header word
// that locates the code block was found on the player's own 2.3.3 dump (tools: games/jojoasbr/tools/probe3).
#include "jojo_shaders.h"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <map>
#include <stdexcept>

#if defined(_WIN32)
#include <direct.h>
#endif

namespace jojo {

namespace {

struct Fail : std::runtime_error {
    using std::runtime_error::runtime_error;
};

uint32_t le32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }
uint32_t be32(const uint8_t* p) { return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3]; }

// ---- SHA-1 (the harness names blocks by the first 16 hex digits of their SHA-1; as p5r_shaders.cpp) ----

std::string sha1_16(const uint8_t* data, size_t n) {
    uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    auto rol = [](uint32_t x, int s) { return (x << s) | (x >> (32 - s)); };
    auto block = [&](const uint8_t* p) {
        uint32_t w[80];
        for (int i = 0; i < 16; i++) w[i] = be32(p + 4 * i);
        for (int i = 16; i < 80; i++) w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
        for (int i = 0; i < 80; i++) {
            uint32_t f, k;
            if (i < 20) f = (b & c) | (~b & d), k = 0x5A827999;
            else if (i < 40) f = b ^ c ^ d, k = 0x6ED9EBA1;
            else if (i < 60) f = (b & c) | (b & d) | (c & d), k = 0x8F1BBCDC;
            else f = b ^ c ^ d, k = 0xCA62C1D6;
            uint32_t t = rol(a, 5) + f + e + k + w[i];
            e = d, d = c, c = rol(b, 30), b = a, a = t;
        }
        h[0] += a, h[1] += b, h[2] += c, h[3] += d, h[4] += e;
    };
    size_t i = 0;
    for (; i + 64 <= n; i += 64) block(data + i);
    uint8_t tail[128] = {};
    size_t rest = n - i;
    std::memcpy(tail, data + i, rest);
    tail[rest] = 0x80;
    size_t len = rest + 1 + 8 <= 64 ? 64 : 128;
    uint64_t bits = uint64_t(n) * 8;
    for (int j = 0; j < 8; j++) tail[len - 1 - j] = uint8_t(bits >> (8 * j));
    block(tail);
    if (len == 128) block(tail + 64);
    char out[41];
    for (int j = 0; j < 5; j++) std::snprintf(out + 8 * j, 9, "%08x", h[j]);
    return std::string(out, 16);
}

// ---- the blocks ----

constexpr uint32_t kCtrlMagic = 0x98761234, kCode = 0x12345678, kCompute = 0x12345679;
constexpr uint64_t kExit = 0xe30000000007000full;
constexpr uint64_t kHeader = 0x2E0;  // BNVN header: from the blob's magic to its control section
constexpr uint64_t kWordCode = 0xD4, kWordSize = 0xD8, kWordCode2 = 0xDC;  // header words 53, 54, 55

struct Data {
    const std::vector<uint8_t>& d;
    uint32_t u32(uint64_t o) const { return o + 4 <= d.size() ? le32(d.data() + o) : 0; }
    bool in(uint64_t o, uint64_t n) const { return o <= d.size() && n <= d.size() - o; }
    bool has_exit(uint64_t a, uint64_t b) const {  // the 8-byte EXIT anywhere in [a, b)
        uint8_t pat[8];
        for (int i = 0; i < 8; i++) pat[i] = uint8_t(kExit >> (8 * i));
        b = std::min<uint64_t>(b, d.size());
        if (a >= b) return false;
        return std::search(d.begin() + ptrdiff_t(a), d.begin() + ptrdiff_t(b), pat, pat + 8) != d.begin() + ptrdiff_t(b);
    }
    bool zero(uint64_t a, uint64_t b) const {
        b = std::min<uint64_t>(b, d.size());
        for (uint64_t i = a; i < b; i++)
            if (d[size_t(i)]) return false;
        return true;
    }
};

std::string stage_of(const Data& x, uint64_t p, uint64_t c) {
    const uint32_t start = x.u32(c + 0x708);
    if (start == 0 || x.u32(p) == kCompute) return "compute";
    uint32_t t = (x.u32(p + start) >> 10) & 0xF;  // the SPH's shader type
    return t == 1 ? "vertex" : t == 5 ? "pixel" : "other";
}

void note(Report& rep, const std::string& group, uint64_t blob, const char* why) {
    rep.invalid++;
    if (rep.notes.size() < 12) {
        char b[160];
        std::snprintf(b, sizeof b, "%s: blob at 0x%llx: %s", group.c_str(), (unsigned long long)blob, why);
        rep.notes.push_back(b);
    }
}

// The code blocks (code offset, control offset) of the BNVN blobs of one buffer.
std::vector<std::pair<uint64_t, uint64_t>> bnvn_blocks(const Data& x, const std::string& group, Report& rep) {
    std::vector<std::pair<uint64_t, uint64_t>> found;
    static const char kMagic[4] = {'B', 'N', 'V', 'N'};
    auto it = x.d.begin();
    while (true) {
        it = std::search(it, x.d.end(), kMagic, kMagic + 4);
        if (it == x.d.end()) break;
        const uint64_t p = uint64_t(it - x.d.begin());
        ++it;
        const uint64_t c = p + kHeader;
        if (x.u32(c) != kCtrlMagic) continue;  // the string "BNVN" somewhere else, not a blob
        rep.blobs++;
        const uint64_t code = c + x.u32(p + kWordCode);  // header word 53: offset from the end of the header
        const uint64_t size = x.u32(c + 0x704), start = x.u32(c + 0x708), plen = x.u32(c + 0x6f8);
        if (x.u32(p + kWordCode2) != x.u32(p + kWordCode)) { note(rep, group, p, "header words 53 and 55 differ"); continue; }
        if (x.u32(p + kWordSize) != size) { note(rep, group, p, "header word 54 is not the control's code size"); continue; }
        if (size == 0 || size % 0x100 || !x.in(code, size)) { note(rep, group, p, "code block outside the data"); continue; }
        if (start + plen > size) { note(rep, group, p, "program longer than its code block"); continue; }
        // the block's own header: graphics 0x12345678 at 0x30, compute 0x12345679 at 0x100, or none (start 0)
        const uint32_t magic = x.u32(code);
        if (start == 0x30 ? magic != kCode : start == 0x100 ? magic != kCompute : start != 0) {
            note(rep, group, p, "unexpected code block header");
            continue;
        }
        if (!x.has_exit(code + start, code + start + plen)) { note(rep, group, p, "no EXIT inside the program"); continue; }
        if (start == 0x30) {
            uint32_t t = (x.u32(code + start) >> 10) & 0xF;
            if (t != 1 && t != 5) { note(rep, group, p, "graphics stage that is neither vertex nor pixel"); continue; }
        }
        found.push_back({code, c});
    }
    return found;
}

// ---- main's built-in blocks: tools/maxwell/exefs_shaders.py find(), as games/p5r/import/p5r_shaders.cpp ----

uint64_t up(uint64_t x) { return (x + 0xff) & ~uint64_t(0xff); }

std::vector<std::pair<uint64_t, uint64_t>> exefs_blocks(const Data& x) {
    const auto& d = x.d;
    std::vector<uint64_t> ctrls, codes;
    for (uint64_t o = 0; o + 0x880 <= d.size(); o += 4)
        if (x.u32(o) == kCtrlMagic && x.u32(o + 0x704) > 0 && x.u32(o + 0x704) < (1u << 24)) ctrls.push_back(o);
    for (uint64_t o = 0; o + 0x30 <= d.size();) {
        bool m = (d[o] == 0x78 || d[o] == 0x79) && d[o + 1] == 0x56 && d[o + 2] == 0x34 && d[o + 3] == 0x12 && x.zero(o + 4, o + 0x30);
        if (m) codes.push_back(o), o += 0x30;
        else o++;
    }
    auto fits = [&](uint64_t p, uint64_t c) {
        uint64_t s = x.u32(c + 0x704), start = x.u32(c + 0x708), plen = x.u32(c + 0x6f8), csize = x.u32(c + 0x6fc),
                 coff = x.u32(c + 0x700);
        bool compute = x.u32(p) == kCompute;
        if (p + s > d.size() || start != (compute ? 0x100u : 0x30u) || start + plen > s) return false;
        if (!x.has_exit(p + start, p + start + plen) || x.zero(p + start + plen - 8, p + start + plen)) return false;
        if (csize) {
            if (coff != up(start + plen) || s != up(coff + csize) || !x.zero(p + coff + csize, p + s)) return false;
        } else if (s != up(start + plen)) {
            return false;
        }
        if (!x.zero(p + start + plen, p + up(start + plen))) return false;
        uint32_t t = (x.u32(p + start) >> 10) & 0xF;
        return compute || t == 1 || t == 5;
    };
    auto key = [&](uint64_t c) {
        return std::string(reinterpret_cast<const char*>(d.data() + c + 0x6f8), 0x14) +
               std::string(reinterpret_cast<const char*>(d.data() + c + 0x748), 0x10);
    };
    std::vector<std::pair<uint64_t, uint64_t>> found;
    for (size_t i = 0; i < codes.size(); i++) {
        uint64_t p = codes[i], room = (i + 1 < codes.size() ? codes[i + 1] : d.size()) - p;
        std::vector<uint64_t> cands;
        for (uint64_t c : ctrls)
            if (x.u32(c + 0x704) <= room && fits(p, c)) cands.push_back(c);
        std::vector<uint64_t> adjacent;
        for (uint64_t c : cands)
            if (p >= c && p - c == 0x850) adjacent.push_back(c);
        if (adjacent.size() == 1) cands = adjacent;
        if (!cands.empty()) {
            uint32_t size = 0;
            for (uint64_t c : cands) size = std::max(size, x.u32(c + 0x704));
            cands.erase(std::remove_if(cands.begin(), cands.end(), [&](uint64_t c) { return x.u32(c + 0x704) != size; }),
                        cands.end());
        }
        std::set<std::string> keys;
        for (uint64_t c : cands) keys.insert(key(c));
        if (keys.size() == 1) found.push_back({p, cands[0]});
    }
    return found;
}

// ---- output ----

void make_dir(const std::string& p) {
#if defined(_WIN32)
    _mkdir(p.c_str());
#else
    mkdir(p.c_str(), 0755);
#endif
}

void write_file(const std::string& path, const uint8_t* p, size_t n) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f || std::fwrite(p, 1, n, f) != n) {
        if (f) std::fclose(f);
        throw Fail("cannot write " + path + " (" + std::strerror(errno) + ")");
    }
    if (std::fclose(f) != 0) throw Fail("cannot write " + path);
}

std::string group_name(const std::string& path_without_ext) {
    std::string group;
    for (char ch : path_without_ext) {
        bool ok = std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
        if (ok) group += char(std::tolower(static_cast<unsigned char>(ch)));
        else if (group.empty() || group.back() != '_') group += '_';
    }
    while (!group.empty() && group.front() == '_') group.erase(group.begin());
    while (!group.empty() && group.back() == '_') group.pop_back();
    return group.empty() ? "shaders" : group;
}

bool ends_with(const std::string& s, const char* e) {
    size_t n = std::strlen(e);
    return s.size() >= n && s.compare(s.size() - n, n, e) == 0;
}

}  // namespace

long extract_buffer(const std::string& group, const std::vector<uint8_t>& data, bool exefs, const std::string& out,
                    std::set<std::string>& seen, Report& rep) {
    Data x{data};
    auto found = exefs ? exefs_blocks(x) : bnvn_blocks(x, group, rep);
    std::string gdir = out + "/" + group;
    make_dir(out);
    make_dir(gdir);
    // The loose .nvn files of one folder share a group: this call adds its rows to the manifest the earlier files
    // wrote (it was overwritten, so only the last file's shaders were translated).
    std::string manifest;
    if (FILE* f = std::fopen((gdir + "/manifest.tsv").c_str(), "rb")) {
        char buf[65536];
        size_t got;
        while ((got = std::fread(buf, 1, sizeof buf, f)) > 0) manifest.append(buf, got);
        std::fclose(f);
    }
    if (manifest.empty())
        manifest =
            "id\tstage\tcode_off\tcode_size\tctrl_off\tctrl_size\tprogram_start\tprogram_bytes\tconst_off\tconst_size\t"
            "code_sha1_16\tcode_file\tctrl_file\n";
    long n = 0;
    char line[512];
    for (auto [p, c] : found) {
        uint64_t s = x.u32(c + 0x704);
        if (!x.in(p, s)) continue;
        std::string h = sha1_16(data.data() + p, size_t(s));
        if (!seen.insert(h).second) {
            rep.duplicates++;
            continue;
        }
        size_t clen = size_t(std::min<uint64_t>(0x880, data.size() - c));
        std::string cf = "ctrl_" + sha1_16(data.data() + c, clen) + ".bin";
        std::string stage = stage_of(x, p, c);
        write_file(gdir + "/code_" + h + ".bin", data.data() + p, size_t(s));
        write_file(gdir + "/" + cf, data.data() + c, clen);
        std::snprintf(line, sizeof line, "%s/0x%llx/%s\t%s\t0x%llx\t0x%llx\t0x%llx\t0x%x\t0x%x\t0x%x\t0x%x\t0x%x\t%s\tcode_%s.bin\t%s\n",
                      group.c_str(), (unsigned long long)p, stage.c_str(), stage.c_str(), (unsigned long long)p,
                      (unsigned long long)s, (unsigned long long)c, 0x880, x.u32(c + 0x708), x.u32(c + 0x6f8),
                      x.u32(c + 0x700), x.u32(c + 0x6fc), h.c_str(), h.c_str(), cf.c_str());
        manifest += line;
        n++;
    }
    write_file(gdir + "/manifest.tsv", reinterpret_cast<const uint8_t*>(manifest.data()), manifest.size());
    rep.written += n;
    return n;
}

long extract_shaders(const nxi::GameImage& game, const std::string& out_dir,
                     const std::function<void(int, int)>& progress, Report& rep, std::string& error) {
    try {
        make_dir(out_dir);
        // every .nvn / .nsh of the RomFS, as (path, entry); the .cpk archives are not opened
        std::vector<std::pair<std::string, nxi::GameImage::Entry>> files;
        std::function<void(const std::string&)> walk = [&](const std::string& dir) {
            for (auto& [name, e] : game.list(dir)) {
                std::string path = dir.empty() ? name : dir + "/" + name;
                if (e.dir) walk(path);
                else if (ends_with(name, ".nvn") || ends_with(name, ".nsh")) files.push_back({path, e});
            }
        };
        walk("");
        if (files.empty()) throw Fail("the game file has no shader files (*.nvn, *.nsh): is it JoJo's Bizarre Adventure: All-Star Battle R?");
        auto main_it = game.exefs().find("main");
        if (main_it == game.exefs().end()) throw Fail("the game file's ExeFS has no main");
        std::set<std::string> seen;
        long total = 0;
        const int steps = int(files.size()) + 1;
        for (size_t i = 0; i < files.size(); i++) {
            if (progress) progress(int(i), steps);
            const auto& [path, e] = files[i];
            std::vector<uint8_t> data(size_t(e.size));
            if (game.read(e, 0, data.data(), data.size()) != data.size()) throw Fail("cannot read " + path);
            // the loose .nvn files of one folder share a group; every .nsh pack has its own
            std::string group;
            if (ends_with(path, ".nvn")) group = "nvn_" + group_name(path.substr(0, path.rfind('/') == std::string::npos ? 0 : path.rfind('/')));
            else {
                const size_t slash = path.rfind('/'), from = slash == std::string::npos ? 0 : slash + 1, dot = path.rfind('.');
                group = group_name(path.substr(from, dot > from ? dot - from : std::string::npos));
            }
            total += extract_buffer(group, data, false, out_dir, seen, rep);
        }
        if (progress) progress(steps - 1, steps);
        std::vector<uint8_t> flat;
        if (!nxi::nso_to_flat_image(main_it->second, flat, error)) return -1;
        total += extract_buffer("exefs", flat, true, out_dir, seen, rep);
        if (progress) progress(steps, steps);
        return total;
    } catch (const Fail& e) {
        error = e.what();
    } catch (const std::bad_alloc&) {
        error = "out of memory";
    }
    return -1;
}

}  // namespace jojo
