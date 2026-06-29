// ─────────────────────────────────────────────────────────────────────────────
// test_lo_convert.cpp — LibreOffice-conversion pure helpers (s89)
//
// WHAT THIS PROVES
//   1. split_search_path: a PATH string splits into directories, dropping empty
//      entries (leading/trailing/doubled colons) without losing real ones — the
//      logic libreoffice_program() walks to find soffice/libreoffice.
//   2. The expected output name derivation: LibreOffice writes <outdir>/<stem>.odt,
//      so the stem of the input drives where we look for the result.
//
// The subprocess (posix_spawn) + filesystem (mkdtemp/access) parts can't be
// sandbox-tested; they're exercised in Scott's Fedora environment. Only the pure
// string logic is mirrored here — keep it in sync with Importer.cpp.
// ─────────────────────────────────────────────────────────────────────────────
/*
  BUILD + RUN
  Sandbox (g++):
    g++ -std=c++20 -Wall -Wextra -o /tmp/test_lo_convert \
        tests/test_lo_convert.cpp && /tmp/test_lo_convert
  Fedora (clang++ 21, project flags):
    clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow \
        -o /tmp/test_lo_convert tests/test_lo_convert.cpp && /tmp/test_lo_convert
*/

#include <cstdio>
#include <string>
#include <vector>

// ── Mirror of Importer.cpp :: split_search_path ──────────────────────────────
static std::vector<std::string> split_search_path(const std::string& path) {
    std::vector<std::string> dirs;
    size_t start = 0;
    while (start <= path.size()) {
        size_t colon = path.find(':', start);
        size_t end   = (colon == std::string::npos) ? path.size() : colon;
        if (end > start) dirs.push_back(path.substr(start, end - start));
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    return dirs;
}

// ── Mirror of Importer.cpp :: stem (filename without dir/extension) ───────────
static std::string stem(const std::string& path) {
    size_t slash = path.find_last_of('/');
    std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
    size_t dot = base.find_last_of('.');
    return (dot == std::string::npos) ? base : base.substr(0, dot);
}

static int g_pass = 0, g_fail = 0;
static void eq(const char* what, const std::string& got, const std::string& want) {
    if (got == want) { ++g_pass; }
    else { ++g_fail; std::printf("  FAIL %s: got [%s] want [%s]\n",
                                 what, got.c_str(), want.c_str()); }
}
static void veq(const char* what, const std::vector<std::string>& got,
                const std::vector<std::string>& want) {
    bool ok = got.size() == want.size();
    for (size_t i = 0; ok && i < got.size(); ++i) ok = got[i] == want[i];
    if (ok) { ++g_pass; }
    else {
        ++g_fail;
        std::printf("  FAIL %s: got {", what);
        for (auto& d : got) std::printf("[%s]", d.c_str());
        std::printf("} want {");
        for (auto& d : want) std::printf("[%s]", d.c_str());
        std::printf("}\n");
    }
}

int main() {
    veq("simple", split_search_path("/usr/bin:/bin"), {"/usr/bin", "/bin"});
    veq("single", split_search_path("/usr/bin"), {"/usr/bin"});
    veq("trailing colon", split_search_path("/usr/bin:"), {"/usr/bin"});
    veq("leading colon", split_search_path(":/bin"), {"/bin"});
    veq("doubled colon", split_search_path("/a::/b"), {"/a", "/b"});
    veq("all empty", split_search_path(":::"), {});
    veq("empty string", split_search_path(""), {});
    veq("realistic",
        split_search_path("/usr/local/bin:/usr/bin:/bin:/opt/libreoffice/program"),
        {"/usr/local/bin", "/usr/bin", "/bin", "/opt/libreoffice/program"});

    // Output-name derivation: <outdir>/<stem(input)>.odt
    eq("odt name docx", stem("/home/me/My Novel.docx") + ".odt", "My Novel.odt");
    eq("odt name rtf",  stem("draft.rtf") + ".odt", "draft.odt");
    eq("odt name dotted", stem("/x/a.b.c.docx") + ".odt", "a.b.c.odt");

    std::printf("\n%s: %d passed, %d failed\n",
                g_fail == 0 ? "PASS" : "FAIL", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
