// ─────────────────────────────────────────────────────────────────────────────
// Folio — objectedit_test.cpp   (s32 — the editable object form's write-through)
//
// The editable object form (s32) writes a floor-field edit THROUGH to the backing
// binder leaf, because the object store is still a projection of the leaves this
// slice. That write-through MUST be the exact inverse of ObjectIO::migrate_legacy_leaf
// — if the two ever drift, an edit lands in the wrong leaf field and the next
// rebuild silently restores the old value. This test pins the inverse so the
// contract can't rot: it asserts floor_field_to_leaf() targets exactly the leaf
// field migrate_legacy_leaf() sourced each floor field from, and round-trips a
// migrate -> edit -> re-migrate cycle to prove an edit survives re-projection.
//
// PURE: ObjectIO.hpp / Object.hpp / nlohmann::json only — sandbox-compilable.
// ─────────────────────────────────────────────────────────────────────────────
/*
g++ -std=c++20 -Wall -Wextra -Werror -I include -I /home/claude/sbox tests/objectedit_test.cpp src/ObjectIO.cpp -o /tmp/objectedit_test
/tmp/objectedit_test
clang++ -std=c++20 -Wall -Wextra -Werror -I include tests/objectedit_test.cpp src/ObjectIO.cpp -o /tmp/objectedit_test
/tmp/objectedit_test
*/

#include "ObjectIO.hpp"
#include <cassert>
#include <iostream>
#include <string>

using namespace Folio;
using ObjectIO::LeafField;
using ObjectIO::floor_field_to_leaf;

static int checks = 0;
static void check(bool cond, const std::string& what) {
    ++checks;
    if (!cond) { std::cerr << "FAIL: " << what << "\n"; std::exit(1); }
}

int main() {
    // ── 1. The mapping IS the inverse of migration ────────────────────────────
    // migrate_legacy_leaf seeds: name<-title, description<-content(buffer),
    // image<-image_path, tagline<-node.description(one-liner), role<-node.role.
    check(floor_field_to_leaf("name")        == LeafField::Title,       "name -> Title");
    check(floor_field_to_leaf("description") == LeafField::Content,     "description -> Content");
    check(floor_field_to_leaf("image")       == LeafField::ImagePath,   "image -> ImagePath");
    check(floor_field_to_leaf("tagline")     == LeafField::Description, "tagline -> Description");
    check(floor_field_to_leaf("role")        == LeafField::Role,        "role -> Role");

    // ── 2. Unknown / object-only fields map to None (leaf untouched) ──────────
    check(floor_field_to_leaf("")              == LeafField::None, "empty -> None");
    check(floor_field_to_leaf("homeworld")     == LeafField::None, "custom field -> None");
    check(floor_field_to_leaf("Name")          == LeafField::None, "case-sensitive: Name -> None");
    check(floor_field_to_leaf("relation_house")== LeafField::None, "relation field -> None");

    // ── 3. Every floor field a migration sources is reachable by the inverse ──
    // (Guards against adding a floor field to migration but forgetting the inverse.)
    Object o = ObjectIO::migrate_legacy_leaf(
        "chr_x", /*is_place=*/false,
        "Ana Reyes",            // -> name
        "<p>A long buffer.</p>",// -> description
        "/img/ana.png",         // -> image
        "Protagonist · Analyst",// -> tagline (orphan)
        "lead");                // -> role   (orphan)
    for (const auto& key : {"name", "description", "image", "tagline", "role"}) {
        check(o.has_value(key), std::string("migrated object has floor key ") + key);
        check(floor_field_to_leaf(key) != LeafField::None,
              std::string("inverse covers migrated floor key ") + key);
    }

    // ── 4. migrate -> edit -> re-migrate survives (the projection round-trip) ─
    // Simulate the leaf the form would write to, edit a field through the inverse,
    // then re-migrate from the (edited) leaf and confirm the new value is what the
    // form would render — i.e. the edit is NOT clobbered by re-projection.
    std::string leaf_title = "Ana Reyes";
    std::string leaf_content = "<p>old</p>";
    std::string leaf_image = "/img/ana.png";
    std::string leaf_desc = "Protagonist";
    std::string leaf_role = "lead";

    auto write_through = [&](const std::string& field_id, const std::string& v) {
        switch (floor_field_to_leaf(field_id)) {
            case LeafField::Title:       leaf_title   = v; break;
            case LeafField::Content:     leaf_content = v; break;
            case LeafField::ImagePath:   leaf_image   = v; break;
            case LeafField::Description: leaf_desc    = v; break;
            case LeafField::Role:        leaf_role    = v; break;
            case LeafField::None:        /* leaf untouched */ break;
        }
    };

    write_through("name", "Ana R. Reyes");      // rename
    write_through("description", "<p>new buffer</p>");
    write_through("homeworld", "Caladan");        // None — must NOT touch any leaf field

    Object reproj = ObjectIO::migrate_legacy_leaf(
        "chr_x", false, leaf_title, leaf_content, leaf_image, leaf_desc, leaf_role);

    check(reproj.value_or("name", "")        == json("Ana R. Reyes"),   "edited name survives re-projection");
    check(reproj.value_or("description", "") == json("<p>new buffer</p>"), "edited buffer survives re-projection");
    check(leaf_image == "/img/ana.png", "None field did not corrupt an unrelated leaf field");
    check(reproj.value_or("tagline", "")     == json("Protagonist"),    "untouched orphan tagline preserved");
    check(reproj.value_or("role", "")        == json("lead"),           "untouched orphan role preserved");

    std::cout << "objectedit_test: all " << checks << " checks passed\n";
    return 0;
}
