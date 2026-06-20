// ─────────────────────────────────────────────────────────────────────────────
// Folio — projection_test.cpp   (s32 — the merge-preserving projection reconcile)
//
// The object store is still a PROJECTION of the binder leaves this slice, but the
// reconcile is now MERGE-PRESERVING: the leaf owns its floor + orphan fields
// (name/description/image/tagline/role), restamped every pass, while the object
// owns everything else (custom template fields, relation iids), which must SURVIVE
// the rebuild. Without this, a relation edge or a builder-added field would be
// clobbered back to a bare leaf shape on the next selection/save. This test pins:
//   • custom / relation values survive a re-projection (the whole point);
//   • leaf-owned fields are restamped from the leaf (rename, clear);
//   • a vanished leaf prunes its projected object, but store-owned objects stay.
//
// PURE: ObjectStore.hpp / ObjectIO / Object / nlohmann::json — sandbox-compilable.
// ─────────────────────────────────────────────────────────────────────────────
/*
g++ -std=c++20 -Wall -Wextra -Werror -I include -I /home/claude/sbox tests/projection_test.cpp src/ObjectStore.cpp src/ObjectIO.cpp src/Iid.cpp -o /tmp/projection_test
/tmp/projection_test
clang++ -std=c++20 -Wall -Wextra -Werror -I include tests/projection_test.cpp src/ObjectStore.cpp src/ObjectIO.cpp src/Iid.cpp -o /tmp/projection_test
/tmp/projection_test
*/

#include "ObjectStore.hpp"
#include <cassert>
#include <iostream>
#include <string>

using namespace Folio;

static int checks = 0;
static void check(bool cond, const std::string& what) {
    ++checks;
    if (!cond) { std::cerr << "FAIL: " << what << "\n"; std::exit(1); }
}

int main() {
    ObjectStore store;
    store.seed_builtins();

    // ── First projection: a character leaf becomes an object ──────────────────
    store.add_migrated_leaf("chr_ana", /*is_place=*/false, "Ana", "<p>buffer</p>",
                            "/img/ana.png", "Protagonist", "lead");
    Object* ana = store.find_object("chr_ana");
    check(ana != nullptr, "object created on first projection");
    check(ana->value_or("name", "") == json("Ana"), "floor name stamped");
    check(ana->value_or("description", "") == json("<p>buffer</p>"), "floor buffer stamped");

    // ── Author edits the OBJECT: a relation iid + a custom field ──────────────
    // (As a relation picker / template-builder field would write.)
    ana->set_value("home_place", "plc_institute");      // a relation edge
    ana->set_value("species", "human");                  // a custom text field

    // ── Re-project (simulating rebuild_object_store on the next selection) ────
    store.add_migrated_leaf("chr_ana", false, "Ana", "<p>buffer</p>",
                            "/img/ana.png", "Protagonist", "lead");
    ana = store.find_object("chr_ana");
    check(ana->value_or("home_place", "") == json("plc_institute"),
          "relation edge SURVIVES re-projection");
    check(ana->value_or("species", "") == json("human"),
          "custom field SURVIVES re-projection");

    // ── Leaf rename: floor field restamps from the leaf, custom still preserved ─
    store.add_migrated_leaf("chr_ana", false, "Ana Reyes", "<p>new buffer</p>",
                            "/img/ana2.png", "Antagonist", "");
    ana = store.find_object("chr_ana");
    check(ana->value_or("name", "") == json("Ana Reyes"), "rename restamps name");
    check(ana->value_or("description", "") == json("<p>new buffer</p>"), "buffer restamps");
    check(ana->value_or("role", "") == json(""), "cleared role restamps to empty");
    check(ana->value_or("home_place", "") == json("plc_institute"),
          "relation edge still preserved after a leaf rename");
    check(ana->value_or("species", "") == json("human"),
          "custom field still preserved after a leaf rename");

    // ── A store-owned object (user-defined type, no leaf) ─────────────────────
    Object house; house.iid = "hse_x"; house.type = "house";
    house.set_value("name", "House X");
    store.objects.push_back(house);

    // ── Prune: only the live character leaf survives; chr_gone is dropped ─────
    store.add_migrated_leaf("chr_gone", false, "Ghost", "", "", "", "");
    check(store.find_object("chr_gone") != nullptr, "second character projected");

    std::vector<std::string> live = { "chr_ana" };   // chr_gone's leaf vanished
    store.prune_projected_except(live);

    check(store.find_object("chr_ana")  != nullptr, "live character kept");
    check(store.find_object("chr_gone") == nullptr, "vanished character pruned");
    check(store.find_object("hse_x")    != nullptr, "store-owned object NEVER pruned");

    // ── Prune doesn't touch store-owned objects even if absent from live set ──
    store.prune_projected_except({});   // no live leaves at all
    check(store.find_object("chr_ana") == nullptr, "empty-live prunes all projected");
    check(store.find_object("hse_x")   != nullptr, "store-owned still safe with empty live");

    // ── upsert_template: replace-in-place by id, or append (the builder commit) ─
    {
        ObjectStore s2;
        s2.seed_builtins();
        const std::size_t base = s2.templates.size();   // character + place
        // Edit the existing character template in place (same id) — count steady.
        Template ch = *s2.find_template("character");
        ch.type_name = "Hero";
        s2.upsert_template(ch);
        check(s2.templates.size() == base, "upsert existing id replaces in place");
        check(s2.find_template("character")->type_name == "Hero", "upsert applied the edit");
        // A new id appends.
        Template sp; sp.id = "species"; sp.type_name = "Species";
        s2.upsert_template(sp);
        check(s2.templates.size() == base + 1, "upsert new id appends");
        check(s2.find_template("species") != nullptr, "new template registered");
    }

    std::cout << "projection_test: all " << checks << " checks passed\n";
    return 0;
}
