// ─────────────────────────────────────────────────────────────────────────────
// test_bundle_roundtrip.cpp — does save→load preserve each scene's content?
//
// Reproduces the reported bug: after import + save + reopen, every scene in a
// chapter shows that chapter's FIRST scene's text. This builds a manifest blob
// shaped exactly like an imported manuscript (2 chapters/groups, each with 3
// scenes carrying DISTINCT content and unique iids), runs the REAL
// explode() → implode(), and asserts every scene's content
// survives intact and distinct.
// ─────────────────────────────────────────────────────────────────────────────
/*
  ── Sandbox (g++) ──
    cd test
    g++ -std=c++20 -I inc -I ../include test_bundle_roundtrip.cpp \
        ../src/ProjectBundle.cpp ../src/Iid.cpp -o test_bundle && ./test_bundle
*/
#include "ProjectBundle.hpp"
#include "Iid.hpp"

#include <filesystem>
#include <iostream>
#include <string>

namespace fs = std::filesystem;
using namespace Folio;
using json = nlohmann::json;

static int g_pass = 0, g_fail = 0;
static void eq(const std::string& got, const std::string& exp, const std::string& what) {
    if (got == exp) { ++g_pass; std::cout << "  ok   " << what << "\n"; }
    else { ++g_fail; std::cout << "  FAIL " << what
                               << "\n        got: [" << got << "]\n   expected: [" << exp << "]\n"; }
}

// Build a scene node with a unique iid and given content.
static json scene(const std::string& title, const std::string& content) {
    json n;
    n["kind"]    = "scene";
    n["iid"]     = make_iid(IidKind::Scene);
    n["title"]   = title;
    n["content"] = content;
    n["children"] = json::array();
    return n;
}
static json group(const std::string& title, std::vector<json> kids) {
    json g;
    g["kind"]  = "group";
    g["iid"]   = make_iid(IidKind::Group);
    g["title"] = title;
    g["children"] = json::array();
    for (auto& k : kids) g["children"].push_back(k);
    return g;
}

// Pull a scene's content out of the imploded blob by chapter index + scene index.
static std::string scene_content(const json& blob, int chap, int sc) {
    return blob["manuscript"][chap]["children"][sc].value("content", "<MISSING>");
}

int main() {
    fs::path root = fs::temp_directory_path() / "folio_bundle_rt";
    fs::remove_all(root);
    fs::path bundle = root / "Jasper";

    // Manifest shaped like an imported book: 2 chapters × 3 scenes, distinct text.
    json blob;
    blob["folio_version"] = 5;
    blob["manuscript"] = json::array();
    blob["manuscript"].push_back(group("Chapter 1", {
        scene("Scene 1", "C1: the opening scene."),
        scene("Scene 2", "C2: the second scene."),
        scene("Scene 3", "C3: the third scene."),
    }));
    blob["manuscript"].push_back(group("Chapter 2", {
        scene("Scene 10", "C10: chapter two begins."),
        scene("Scene 11", "C11: more of chapter two."),
        scene("Scene 12", "C12: end of chapter two."),
    }));
    // Other trees present but empty (explode/implode iterate all kTreeKeys).
    for (const char* k : {"characters","places","references","templates","trash"})
        blob[k] = json::array();

    // Record the iids we minted, to verify uniqueness.
    std::cout << "── minted iids ──\n";
    for (int c = 0; c < 2; ++c)
        for (int s = 0; s < 3; ++s)
            std::cout << "  ch" << c << " sc" << s << " = "
                      << blob["manuscript"][c]["children"][s].value("iid","") << "\n";

    // ── ROUND TRIP through the real bundle code ──────────────────────────────
    explode(blob, bundle);
    ReconcileReport rep;
    json back = implode(bundle, rep);

    std::cout << "\n── reconcile: " << rep.summary() << " ──\n";
    std::cout << "── content after save→load ──\n";

    eq(scene_content(back, 0, 0), "C1: the opening scene.",   "ch1 scene1");
    eq(scene_content(back, 0, 1), "C2: the second scene.",    "ch1 scene2");
    eq(scene_content(back, 0, 2), "C3: the third scene.",     "ch1 scene3");
    eq(scene_content(back, 1, 0), "C10: chapter two begins.", "ch2 scene1");
    eq(scene_content(back, 1, 1), "C11: more of chapter two.","ch2 scene2");
    eq(scene_content(back, 1, 2), "C12: end of chapter two.", "ch2 scene3");

    // Also list what files actually landed on disk.
    std::cout << "\n── content/ files on disk ──\n";
    fs::path cdir = bundle / "content";
    if (fs::is_directory(cdir))
        for (auto& e : fs::directory_iterator(cdir))
            std::cout << "  " << e.path().filename().string() << "\n";

    std::cout << "\n══════ PASS " << g_pass << "  FAIL " << g_fail << " ══════\n";
    fs::remove_all(root);
    return g_fail == 0 ? 0 : 1;
}
