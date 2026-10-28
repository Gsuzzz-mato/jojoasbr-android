// PC test of jojo_shaders.h.
//   jojo_shaders <prod.keys> [title.keys] <base.nsp> [update.nsp] <out dir>     the whole game, as on the phone
//   jojo_shaders --file <a .nvn/.nsh file> <out dir>                              one file
#include <fcntl.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <cstring>

#include "jojo_shaders.h"

static void print_report(const jojo::Report& r) {
    printf("blobs %ld: %ld distinct code blocks written, %ld duplicates, %ld rejected\n", r.blobs, r.written, r.duplicates, r.invalid);
    for (const std::string& n : r.notes) printf("  rejected: %s\n", n.c_str());
}

int main(int argc, char** argv) {
    if (argc == 4 && std::strcmp(argv[1], "--file") == 0) {
        std::vector<uint8_t> data;
        if (FILE* f = fopen(argv[2], "rb")) {
            fseek(f, 0, SEEK_END);
            long n = ftell(f);
            fseek(f, 0, SEEK_SET);
            data.resize(size_t(n > 0 ? n : 0));
            if (fread(data.data(), 1, data.size(), f) != data.size()) data.clear();
            fclose(f);
        }
        if (data.empty()) return fprintf(stderr, "cannot read %s\n", argv[2]), 2;
        jojo::Report rep;
        std::set<std::string> seen;
        jojo::extract_buffer("file", data, false, argv[3], seen, rep);
        print_report(rep);
        return rep.invalid ? 1 : 0;
    }
    if (argc < 4) return fprintf(stderr, "usage: jojo_shaders <prod.keys> [title.keys] <base.nsp> [update.nsp] <out dir>\n       jojo_shaders --file <file> <out dir>\n"), 2;
    std::vector<nxi::InputFile> in;
    for (int i = 1; i < argc - 1; i++) {
        int fd = open(argv[i], O_RDONLY);
        if (fd < 0) return fprintf(stderr, "cannot open %s\n", argv[i]), 2;
        const char* s = strrchr(argv[i], '\\');
        const char* f = strrchr(argv[i], '/');
        if (f && (!s || f > s)) s = f;
        in.push_back({s ? s + 1 : argv[i], fd});
    }
    std::string err;
    auto g = nxi::GameImage::open(in, 0, err);
    if (!g) return fprintf(stderr, "open: %s\n", err.c_str()), 1;
    printf("title %016llx, update %s\n", (unsigned long long)g->title_id(), g->has_update() ? "APPLIED" : "not applied");
    auto t0 = std::chrono::steady_clock::now();
    jojo::Report rep;
    long n = jojo::extract_shaders(*g, argv[argc - 1], [](int d, int t) { fprintf(stderr, "  %d/%d\r", d, t); }, rep, err);
    double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    if (n < 0) return fprintf(stderr, "\nextract: %s\n", err.c_str()), 1;
    printf("\n%ld distinct code blocks in %.1f s\n", n, sec);
    print_report(rep);
    return 0;
}
