// ─────────────────────────────────────────────────────────────────────────────
// TEST_absorbed_archive.cpp — s108 §21.4: archive the spent received copy.
//
// Exercises Folio::archive_absorbed_copy / absorbed_dir_for against REAL temp
// dirs under /tmp: the happy move (source gone, lands in absorbed/ beside the
// project, correct name), same-day collision suffixing, the never-throw failure
// modes (unsaved project, vanished source), and idempotence (re-archiving a copy
// that is already in absorbed/ is a no-op, not a self-truncate). The cross-device
// copy+remove fallback can't be forced on a single-mount sandbox; the rename path
// it shares is covered, and the fallback is a straight copy_file+remove.
//
// This is the §21.6 "can't crash the project" evidence for the archive step: the
// helper returns {ok=false, reason} on every bad input and leaves the source
// untouched -- proven here, not just asserted in prose.
// ─────────────────────────────────────────────────────────────────────────────
/*
g++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I /home/claude/sbox -I ../include ../src/ProjectBundle.cpp ../src/Iid.cpp TEST_absorbed_archive.cpp -o /tmp/test_absorbed_archive && /tmp/test_absorbed_archive
clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I ../include ../src/ProjectBundle.cpp ../src/Iid.cpp TEST_absorbed_archive.cpp -o /tmp/test_absorbed_archive && /tmp/test_absorbed_archive
*/
#include "ProjectBundle.hpp"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using Folio::archive_absorbed_copy;
using Folio::absorbed_dir_for;

static int g_pass = 0, g_fail = 0;
static void ok(bool cond, const std::string& what) {
    if (cond) { ++g_pass; }
    else      { ++g_fail; std::cout << "  FAIL: " << what << "\n"; }
}

static fs::path make_temp_root() {
    fs::path base = fs::temp_directory_path() /
                    ("folio_absorb_" + std::to_string(::getpid()) + "_" +
                     std::to_string(g_pass + g_fail));
    fs::create_directories(base);
    return base;
}

static void write_text(const fs::path& p, const std::string& s) {
    std::ofstream f(p, std::ios::binary | std::ios::trunc);
    f << s;
}
static std::string read_text(const fs::path& p) {
    std::ifstream f(p, std::ios::binary);
    std::ostringstream ss; ss << f.rdbuf(); return ss.str();
}

int main() {
    const std::string date = "2026-07-03";

    // ── 1. Happy path: move a Downloads copy into absorbed/ beside the project ──
    {
        fs::path root = make_temp_root();
        fs::path proj_parent = root / "Documents";
        fs::path downloads    = root / "Downloads";
        fs::create_directories(proj_parent);
        fs::create_directories(downloads);

        fs::path project = proj_parent / "MyNovel.folio";
        fs::create_directories(project);                 // a v5 bundle dir
        write_text(project / "project.json", "{}");

        fs::path recv = downloads / "editor-return-final-v3.folioedit";
        write_text(recv, "SEALED-BYTES");

        auto r = archive_absorbed_copy(project, recv, date);
        ok(r.ok, "happy: ok==true");
        ok(!fs::exists(recv), "happy: source removed from Downloads");
        ok(fs::exists(r.dest), "happy: destination exists");
        ok(r.dest.parent_path() == proj_parent / "absorbed",
           "happy: lands in absorbed/ BESIDE the bundle (parent dir)");
        ok(r.dest.filename().string() == "MyNovel-returned-2026-07-03.folioedit",
           "happy: renamed <stem>-returned-<date>.folioedit");
        ok(read_text(r.dest) == "SEALED-BYTES", "happy: bytes preserved");
        fs::remove_all(root);
    }

    // ── 2. Same-day collision -> -2 suffix, first copy untouched ───────────────
    {
        fs::path root = make_temp_root();
        fs::path parent = root / "proj";
        fs::create_directories(parent);
        fs::path project = parent / "Book.folio";
        fs::create_directories(project);

        fs::path a = root / "a.folioedit"; write_text(a, "FIRST");
        fs::path b = root / "b.folioedit"; write_text(b, "SECOND");

        auto r1 = archive_absorbed_copy(project, a, date);
        auto r2 = archive_absorbed_copy(project, b, date);
        ok(r1.ok && r2.ok, "collision: both archived");
        ok(r1.dest.filename().string() == "Book-returned-2026-07-03.folioedit",
           "collision: first has plain name");
        ok(r2.dest.filename().string() == "Book-returned-2026-07-03-2.folioedit",
           "collision: second gets -2 suffix");
        ok(read_text(r1.dest) == "FIRST" && read_text(r2.dest) == "SECOND",
           "collision: neither clobbered the other");
        fs::remove_all(root);
    }

    // ── 3. Unsaved project (empty path) -> refuses, source untouched ───────────
    {
        fs::path root = make_temp_root();
        fs::path recv = root / "x.folioedit"; write_text(recv, "KEEP");
        auto r = archive_absorbed_copy(fs::path{}, recv, date);
        ok(!r.ok, "unsaved: ok==false");
        ok(!r.reason.empty(), "unsaved: gives a reason");
        ok(fs::exists(recv) && read_text(recv) == "KEEP",
           "unsaved: source left exactly as it was");
        ok(absorbed_dir_for(fs::path{}).empty(),
           "unsaved: absorbed_dir_for(empty) == empty");
        fs::remove_all(root);
    }

    // ── 4. Vanished source -> refuses, never throws ────────────────────────────
    {
        fs::path root = make_temp_root();
        fs::path project = root / "P.folio";
        fs::create_directories(project);
        auto r = archive_absorbed_copy(project, root / "nope.folioedit", date);
        ok(!r.ok && !r.reason.empty(), "vanished: refuses with reason");
        fs::remove_all(root);
    }

    // ── 5. Idempotent: a copy ALREADY in absorbed/ is a no-op, file intact ─────
    {
        fs::path root = make_temp_root();
        fs::path parent = root / "p";
        fs::create_directories(parent);
        fs::path project = parent / "N.folio";
        fs::create_directories(project);

        fs::path recv = root / "r.folioedit"; write_text(recv, "PAYLOAD");
        auto r1 = archive_absorbed_copy(project, recv, date);   // move it in
        ok(r1.ok, "idempotent: first archive ok");

        // Now try to archive the ALREADY-archived file again.
        auto r2 = archive_absorbed_copy(project, r1.dest, date);
        ok(r2.ok, "idempotent: re-archive reports ok");
        ok(r2.dest == r1.dest, "idempotent: dest == existing location");
        ok(fs::exists(r1.dest) && read_text(r1.dest) == "PAYLOAD",
           "idempotent: file still there, not self-truncated");
        fs::remove_all(root);
    }

    // ── 6. absorbed_dir_for names the right place ──────────────────────────────
    {
        ok(absorbed_dir_for("/home/me/Books/MyNovel.folio") ==
               fs::path("/home/me/Books/absorbed"),
           "dir_for: beside the bundle");
    }

    std::cout << "\nabsorbed-archive: " << g_pass << " passed, "
              << g_fail << " failed\n";
    return g_fail ? 1 : 0;
}
