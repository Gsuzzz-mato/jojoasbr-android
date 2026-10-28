#include "game_image.h"

#include <fcntl.h>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

#include <cstdlib>
#include <memory>
#include <mutex>
#include <vector>

#include <nx/runtime.h>

namespace nx {

namespace {

std::unique_ptr<nxi::GameImage> g_image;
std::once_flag g_once;

int open_read(const char* path) {
#if defined(_WIN32)
    return _open(path, _O_RDONLY | _O_BINARY | _O_NOINHERIT);
#else
    return ::open(path, O_RDONLY | O_CLOEXEC);
#endif
}

void close_fd(int fd) {
#if defined(_WIN32)
    _close(fd);
#else
    ::close(fd);
#endif
}

std::string base_name(const std::string& p) {
    size_t s = p.find_last_of("/\\");
    return s == std::string::npos ? p : p.substr(s + 1);
}

void open_image() {
    const char* fd = std::getenv("NX_GAME_FD");
    const char* path = std::getenv("NX_GAME_NSP");
    if (!(fd && *fd) && !(path && *path)) return;
    // Several files (base + update) are separated by '|' in every variable: NX_GAME_FD="12|13",
    // NX_GAME_NAME="base.nsp|update.nsp", NX_GAME_NSP="/a/base.nsp|/a/update.nsp".
    auto split = [](const char* s) {
        std::vector<std::string> v;
        std::string cur;
        for (const char* c = s ? s : ""; *c; c++) {
            if (*c == '|') v.push_back(cur), cur.clear();
            else cur += *c;
        }
        if (!cur.empty()) v.push_back(cur);
        return v;
    };
    std::vector<nxi::InputFile> in;
    std::string shown;
    auto add_shown = [&](const std::string& n) { shown += (shown.empty() ? "" : " + ") + n; };
    if (fd && *fd) {
        auto fds = split(fd), names = split(std::getenv("NX_GAME_NAME"));
        for (size_t i = 0; i < fds.size(); i++) {
            std::string n = i < names.size() && !names[i].empty() ? names[i] : (i ? "update.nsp" : "game.nsp");
            add_shown(n);
            in.push_back({n, std::atoi(fds[i].c_str())});
        }
    } else {
        for (auto& p : split(path)) {
            add_shown(base_name(p));
            int f = open_read(p.c_str());
            if (f < 0) fatal("game: cannot open %s", p.c_str());
            in.push_back({base_name(p), f});
        }
    }
    std::vector<int> keys;  // read while opening, then closed
    const char* k = std::getenv("NX_KEYS");
    if (!k || !*k) fatal("game: NX_KEYS (the prod.keys file) is not set");
    if ((keys.emplace_back(open_read(k))) < 0) fatal("game: cannot open the keys file");
    in.push_back({"prod.keys", keys.back()});
    if (const char* t = std::getenv("NX_TITLE_KEYS"); t && *t) {
        int f = open_read(t);
        if (f >= 0) keys.push_back(f), in.push_back({"title.keys", f});
    }
    std::string err;
    g_image = nxi::GameImage::open(in, 0, err);
    for (int f : keys) close_fd(f);
    if (!g_image) fatal("game: %s could not be read: %s", shown.c_str(), err.c_str());
    log("game: %s read in place (title %016llx): %zu RomFS files, ExeFS %zu files", shown.c_str(),
        (unsigned long long)g_image->title_id(), g_image->file_count(), g_image->exefs().size());
}

}  // namespace

const nxi::GameImage* game_image() {
    std::call_once(g_once, open_image);
    return g_image.get();
}

std::string game_image_path(const std::string& path) {
    std::string out, part;
    auto flush = [&] {
        if (!part.empty() && part != ".") out += (out.empty() ? "" : "/") + part;
        part.clear();
    };
    for (char ch : path) {
        if (ch == '/' || ch == '\\') flush();
        else part += ch;
    }
    flush();
    return out;
}

}  // namespace nx
