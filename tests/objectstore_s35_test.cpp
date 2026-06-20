// ─────────────────────────────────────────────────────────────────────────────
// Folio — objectstore_s35_test.cpp
//
// s35 — clone-to-customize, the PURE half: the leaf `template_id` → object.type
// resolution (the truth table), the `projected` leaf-backed marker driving the
// prune (so a clone-typed character whose leaf vanished is still dropped), the
// custom-field value-write surviving the merge reconcile, and the projected-flag
// round-trip. The GTK half (the "Customize fields…" affordance + the on_change
// None-branch wiring) is mechanical over exactly these checks.
//
// Mirrors add_migrated_leaf's call shape from DocumentModel::collect_object_leaves
// (iid, is_place, title, buffer, image, tagline, role, template_id) so the test
// drives the same seam the binder walk feeds.
// ─────────────────────────────────────────────────────────────────────────────
/*
g++ -std=c++20 -I ../include -I /home/claude/sbox -Wall -Wextra -Werror objectstore_s35_test.cpp ../src/ObjectStore.cpp ../src/ObjectIO.cpp ../src/Iid.cpp -o /tmp/objectstore_s35_test
/tmp/objectstore_s35_test
clang++ -std=c++20 -I ../include -Wall -Wextra -Werror objectstore_s35_test.cpp ../src/ObjectStore.cpp ../src/ObjectIO.cpp ../src/Iid.cpp -o /tmp/objectstore_s35_test
/tmp/objectstore_s35_test
*/

#include "ObjectStore.hpp"
#include "ObjectIO.hpp"
#include "FormPlan.hpp"

#include <cassert>
#include <iostream>
#include <string>

using namespace Folio;

static int g_pass = 0;
static void ok(bool cond, const std::string& what) {
    if (!cond) { std::cerr << "FAIL: " << what << "\n"; std::abort(); }
    ++g_pass;
}

// Convenience: a fresh store seeded with the two locked built-ins, as the model
// would have it before the first reconcile.
static ObjectStore seeded() {
    ObjectStore s;
    s.seed_builtins();
    return s;
}

// ── 1. Truth table: empty template_id resolves to the section floor ───────────
static void test_floor_resolution() {
    ObjectStore s = seeded();
    ok(s.resolve_leaf_type(/*is_place=*/false, "") == "character", "empty -> character floor");
    ok(s.resolve_leaf_type(/*is_place=*/true,  "") == "place",     "empty -> place floor");

    s.add_migrated_leaf("chr_a", false, "Paul", "<p>heir</p>", "", "", "", "");
    const Object* o = s.find_object("chr_a");
    ok(o && o->type == "character",          "floor character object typed");
    ok(o && o->projected,                    "floor object is projected (leaf-backed)");
    ok(o && o->value_or("name", "") == "Paul", "floor name written through");
}

// ── 2. Truth table: an editable clone is adopted as the object's type ─────────
static void test_clone_adoption() {
    ObjectStore s = seeded();
    const std::string clone = s.clone_template("character");   // tpl_… , unlocked
    ok(!clone.empty(),                       "clone minted");
    ok(s.resolve_leaf_type(false, clone) == clone, "editable clone -> clone id");

    s.add_migrated_leaf("chr_b", false, "Leto", "<p>duke</p>", "", "", "", clone);
    const Object* o = s.find_object("chr_b");
    ok(o && o->type == clone,                "object adopts the clone type");
    ok(o && o->projected,                    "clone-typed object still projected");
    // The clone shares the floor schema, so the floor fields are still seeded +
    // written through the projection.
    ok(o && o->value_or("name", "") == "Leto", "floor name on a clone-typed object");
}

// ── 3. Truth table rows 3 & 4: missing clone and a built-in id fall to floor ──
static void test_fallbacks() {
    ObjectStore s = seeded();
    // Row 3 — a template_id pointing at a deleted/absent clone falls back.
    ok(s.resolve_leaf_type(false, "tpl_ghost") == "character", "missing clone -> floor");
    s.add_migrated_leaf("chr_c", false, "Gurney", "", "", "", "", "tpl_ghost");
    const Object* o = s.find_object("chr_c");
    ok(o && o->type == "character",          "orphaned clone id projects to floor");
    ok(o && o->projected,                    "fallback object still projected");

    // Row 4 — naming a BUILT-IN id is never an adopted type; floor wins, and the
    // is_place argument decides which floor (so a cross-section id still lands right).
    ok(s.resolve_leaf_type(false, "character") == "character", "builtin id -> floor (char)");
    ok(s.resolve_leaf_type(true,  "character") == "place",     "builtin id, place leaf -> place floor");
}

// ── 4. The prune keys on `projected`, not on type (the clone-type bug fix) ────
static void test_prune_clone_typed() {
    ObjectStore s = seeded();
    const std::string clone = s.clone_template("character");

    // Two clone-typed characters projected from leaves.
    s.add_migrated_leaf("chr_keep", false, "Stilgar", "", "", "", "", clone);
    s.add_migrated_leaf("chr_drop", false, "Jamis",   "", "", "", "", clone);
    ok(s.find_object("chr_keep") && s.find_object("chr_drop"), "both clone-typed objects present");

    // Next reconcile: only chr_keep's leaf survives. The old is_projected_type
    // proxy would have spared chr_drop (its type is tpl_…, not character); the
    // projected flag drops it correctly.
    s.add_migrated_leaf("chr_keep", false, "Stilgar", "", "", "", "", clone);
    s.prune_projected_except({ "chr_keep" });
    ok(s.find_object("chr_keep"),  "live clone-typed leaf kept");
    ok(!s.find_object("chr_drop"), "vanished clone-typed leaf pruned");

    // A store-owned object (projected == false, a future user type with no leaf)
    // is never pruned even when absent from live_iids.
    Object owned; owned.iid = "obj_owned"; owned.type = clone; owned.projected = false;
    s.objects.push_back(owned);
    s.prune_projected_except({ "chr_keep" });
    ok(s.find_object("obj_owned"), "store-owned object survives prune");
}

// ── 5. A custom (non-floor) field value survives the merge reconcile ──────────
static void test_custom_value_survives_merge() {
    ObjectStore s = seeded();
    const std::string clone = s.clone_template("character");
    // Give the clone a custom Text field (what the builder would add).
    Template* ct = nullptr;
    for (auto& t : s.templates) if (t.id == clone) ct = &t;
    ok(ct != nullptr, "clone template found for editing");
    ct->fields.push_back(FieldSchema{ "fld_occ", FieldType::Text, "Occupation", json::object() });

    // Project the leaf, then write the custom field as the form's on_change would
    // (apply_field coerces against the schema and stores on the object).
    s.add_migrated_leaf("chr_d", false, "Duncan", "<p>swordmaster</p>", "", "", "", clone);
    Object* o = s.find_object("chr_d");
    ok(o != nullptr, "projected clone-typed object present");
    const FieldSchema* fs = ct->find_field("fld_occ");
    ok(fs != nullptr, "custom field schema present");
    apply_field(*o, *fs, json("Swordmaster of Ginaz"));
    ok(o->value_or("fld_occ", "") == "Swordmaster of Ginaz", "custom value written");

    // The merge reconcile (a save pass) restamps ONLY leaf-owned floor fields and
    // preserves everything else — the custom value must survive, and a floor edit
    // on the leaf must still land.
    s.add_migrated_leaf("chr_d", false, "Duncan Idaho", "<p>swordmaster</p>", "", "", "", clone);
    Object* o2 = s.find_object("chr_d");
    ok(o2 && o2->value_or("fld_occ", "") == "Swordmaster of Ginaz", "custom value survived merge");
    ok(o2 && o2->value_or("name", "")    == "Duncan Idaho",         "floor name restamped from leaf");
}

// ── 6. The projected flag round-trips through serialisation ───────────────────
static void test_projected_round_trip() {
    ObjectStore s = seeded();
    const std::string clone = s.clone_template("character");
    s.add_migrated_leaf("chr_e", false, "Chani", "", "", "", "", clone);     // projected
    Object owned; owned.iid = "obj_owned"; owned.type = clone; owned.projected = false;
    s.objects.push_back(owned);                                              // store-owned

    json blob = s.to_json();
    ObjectStore r;
    r.from_json(blob);
    const Object* a = r.find_object("chr_e");
    const Object* b = r.find_object("obj_owned");
    ok(a && a->projected,  "projected==true round-trips");
    ok(b && !b->projected, "projected==false round-trips (omitted -> default false)");

    // After reload, a reconcile where chr_e's leaf is GONE prunes the ghost but
    // keeps the store-owned object — the cross-session prune the flag enables.
    r.prune_projected_except({});   // no live leaves
    ok(!r.find_object("chr_e"),  "loaded ghost (leaf deleted) pruned next pass");
    ok(r.find_object("obj_owned"), "loaded store-owned object retained");

    // JSON cleanliness: a store-owned object omits the projected key entirely.
    bool found_owned = false;
    for (const auto& oj : blob["objects"])
        if (oj.value("iid", "") == "obj_owned") {
            found_owned = true;
            ok(!oj.contains("projected"), "store-owned object omits projected key");
        }
    ok(found_owned, "store-owned object present in blob");
}

// ── 7. The floor path is unchanged when no clone is in play (regression) ──────
static void test_floor_regression() {
    ObjectStore s = seeded();
    // Character + place leaves, no template_id — exactly the s32/s34 behaviour.
    s.add_migrated_leaf("chr_f", false, "Jessica", "<p>bene gesserit</p>", "img.png",
                        "the lady", "Supporting", "");
    s.add_migrated_leaf("plc_a", true,  "Arrakis", "<p>desert</p>", "", "", "", "");
    const Object* c = s.find_object("chr_f");
    const Object* p = s.find_object("plc_a");
    ok(c && c->type == "character" && c->projected, "char floor unchanged");
    ok(p && p->type == "place"     && p->projected, "place floor unchanged");
    // Orphan legacy fields still preserved (tagline/role).
    ok(c && c->value_or("tagline", "") == "the lady",  "legacy tagline orphan kept");
    ok(c && c->value_or("role", "")    == "Supporting","legacy role orphan kept");
}

int main() {
    test_floor_resolution();
    test_clone_adoption();
    test_fallbacks();
    test_prune_clone_typed();
    test_custom_value_survives_merge();
    test_projected_round_trip();
    test_floor_regression();
    std::cout << "objectstore_s35_test: all " << g_pass << " checks passed\n";
    return 0;
}
