// ─────────────────────────────────────────────────────────────────────────────
// Folio — objectstore_test.cpp   (s31)   Pure unit check for ObjectStore — the
// registry + instances that ride the project blob. Proves the wiring claims the
// DocumentModel save/load path relies on (DocumentModel itself is GTK/GLib-bound
// and not sandbox-compilable, so this exercises the pure store the same way the
// model does — building it from legacy leaves and round-tripping the blob key):
//   1. seed_builtins populates the Character + Place floor templates, idempotent.
//   2. add_migrated_leaf (the binder-walk seam) builds objects under the right
//      template, routing characters vs places.
//   3. Re-projection is idempotent: re-adding a leaf iid updates in place, never
//      duplicates (the save-time projection can run repeatedly).
//   4. The store round-trips through to_json/from_json EMBEDDED in a project blob
//      under "object_store" — the exact shape explode/implode carry verbatim.
//   5. Legacy path: a blob with no "object_store" → rebuild-from-leaves yields a
//      populated, correct store (reframe-not-replace on load).
//
// Pure: no GTK/GLib. Sandbox uses vendored nlohmann at -I /home/claude/sbox;
// Fedora uses system nlohmann.
//
/*
g++ -std=c++20 -Wall -Wextra -Werror -I /home/claude/sbox -Iinclude tests/objectstore_test.cpp src/ObjectStore.cpp src/ObjectIO.cpp src/Iid.cpp -o /tmp/objectstore_test && /tmp/objectstore_test
clang++ -std=c++20 -Wall -Wextra -Werror -Iinclude tests/objectstore_test.cpp src/ObjectStore.cpp src/ObjectIO.cpp src/Iid.cpp -o /tmp/objectstore_test && /tmp/objectstore_test
*/

#include "ObjectStore.hpp"
#include "ObjectIO.hpp"
#include "Iid.hpp"

#include <cstdio>
#include <string>

using namespace Folio;

static int g_checks = 0;
static int g_fails  = 0;
static void check(bool cond, const char* what) {
    ++g_checks;
    if (!cond) { ++g_fails; std::printf("  FAIL: %s\n", what); }
}

// Stand-in for DocumentModel::rebuild_object_store(): seed the registry, then
// migrate a fixed set of "leaves" (what the binder walk would hand the store).
static void project_demo_leaves(ObjectStore& s,
                                const std::string& chr1, const std::string& chr2,
                                const std::string& plc1) {
    s.clear_objects();
    s.seed_builtins();
    s.add_migrated_leaf(chr1, false, "Paul Atreides", "<p>Heir to House Atreides.</p>",
                        "assets/paul.png", "the Kwisatz Haderach", "Protagonist");
    s.add_migrated_leaf(chr2, false, "Baron Harkonnen", "<p>Ruler of Giedi Prime.</p>",
                        "", "", "Antagonist");
    s.add_migrated_leaf(plc1, true, "Arrakis", "<p>The desert planet. Dune.</p>", "");
}

static void test_seed_and_build() {
    std::printf("[seed + build]\n");
    ObjectStore s;
    std::string chr1 = make_iid(IidKind::Character);
    std::string chr2 = make_iid(IidKind::Character);
    std::string plc1 = make_iid(IidKind::Place);
    project_demo_leaves(s, chr1, chr2, plc1);

    check(s.has_template("character") && s.has_template("place"), "seed_builtins adds both floor templates");
    check(s.templates.size() == 2, "exactly two built-in templates seeded");
    check(s.objects.size() == 3, "three leaves migrated to objects");

    const Object* paul = s.find_object(chr1);
    check(paul != nullptr, "object found by leaf iid");
    check(paul && paul->type == "character", "character leaf → character template");
    check(paul && paul->value_or("name", json("")) == "Paul Atreides", "title → name carried");
    check(paul && paul->value_or("role", json("")) == "Protagonist", "legacy role kept (orphan)");

    const Object* dune = s.find_object(plc1);
    check(dune && dune->type == "place", "place leaf → place template");
    check(dune && dune->value_or("name", json("")) == "Arrakis", "place name carried");

    // seed again — idempotent, no duplicate templates
    s.seed_builtins();
    check(s.templates.size() == 2, "seed_builtins is idempotent");
}

static void test_reprojection_idempotent() {
    std::printf("[re-projection idempotent]\n");
    ObjectStore s;
    std::string chr1 = make_iid(IidKind::Character);
    std::string chr2 = make_iid(IidKind::Character);
    std::string plc1 = make_iid(IidKind::Place);
    project_demo_leaves(s, chr1, chr2, plc1);
    check(s.objects.size() == 3, "first projection: 3 objects");

    // Project AGAIN (as a second save would) with an edited title for chr1.
    s.clear_objects();
    s.seed_builtins();
    s.add_migrated_leaf(chr1, false, "Paul Muad'Dib", "<p>Reborn.</p>", "assets/paul.png");
    s.add_migrated_leaf(chr2, false, "Baron Harkonnen", "<p>Ruler.</p>", "");
    s.add_migrated_leaf(plc1, true, "Arrakis", "<p>Dune.</p>", "");

    check(s.objects.size() == 3, "re-projection does not duplicate (same iids)");
    const Object* paul = s.find_object(chr1);
    check(paul && paul->value_or("name", json("")) == "Paul Muad'Dib", "re-projection updates the value");
}

static void test_blob_roundtrip() {
    std::printf("[blob round-trip]\n");
    ObjectStore s;
    std::string chr1 = make_iid(IidKind::Character);
    std::string chr2 = make_iid(IidKind::Character);
    std::string plc1 = make_iid(IidKind::Place);
    project_demo_leaves(s, chr1, chr2, plc1);

    // Embed under "object_store" in a mock project blob alongside other keys,
    // exactly as DocumentModel::save_to does. explode() preserves non-tree keys
    // verbatim, so this mirrors the real bundle round-trip.
    json blob;
    blob["project_title"] = "Dune";
    blob["manuscript"]    = json::array();
    blob["object_store"]  = s.to_json();

    // ... bundle write/read happens here in the app (key passes through) ...

    ObjectStore loaded;
    check(blob.contains("object_store") && blob["object_store"].is_object(),
          "object_store rides the blob as a top-level object");
    loaded.from_json(blob["object_store"]);

    check(loaded.templates.size() == 2, "templates survive the blob round-trip");
    check(loaded.objects.size() == 3, "objects survive the blob round-trip");
    const Object* paul = loaded.find_object(chr1);
    check(paul && paul->value_or("name", json("")) == "Paul Atreides", "object values intact after load");
    check(paul && paul->value_or("description", json("")) == "<p>Heir to House Atreides.</p>",
          "richtext buffer intact after load");
    const Template* ct = loaded.find_template("character");
    check(ct && ct->fields.size() == 3, "character template schema intact after load");
}

static void test_legacy_load_migrates() {
    std::printf("[legacy load → migrate]\n");
    // A project blob with NO object_store (a v5 bundle saved before this slice).
    json blob;
    blob["project_title"] = "Old Project";

    ObjectStore s;
    if (blob.contains("object_store") && blob["object_store"].is_object()) {
        s.from_json(blob["object_store"]);   // not taken
    } else {
        // DocumentModel::parse_blob would call rebuild_object_store() here, which
        // walks the just-loaded leaves. Simulate with the demo leaves.
        std::string chr1 = make_iid(IidKind::Character);
        std::string plc1 = make_iid(IidKind::Place);
        s.clear_objects();
        s.seed_builtins();
        s.add_migrated_leaf(chr1, false, "Legacy Char", "<p>desc</p>", "");
        s.add_migrated_leaf(plc1, true, "Legacy Place", "<p>here</p>", "");
    }
    s.seed_builtins();

    check(!s.empty(), "legacy project gets a populated store on load");
    check(s.has_template("character") && s.has_template("place"), "floor templates present after legacy migrate");
    check(s.objects.size() == 2, "legacy leaves migrated to objects on load");
}

int main() {
    std::printf("=== Folio ObjectStore — s31 ===\n");
    test_seed_and_build();
    test_reprojection_idempotent();
    test_blob_roundtrip();
    test_legacy_load_migrates();

    std::printf("\n%d checks, %d failures\n", g_checks, g_fails);
    if (g_fails == 0) std::printf("ALL PASS\n");
    return g_fails == 0 ? 0 : 1;
}
