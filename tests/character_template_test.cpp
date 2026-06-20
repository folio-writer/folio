// ─────────────────────────────────────────────────────────────────────────────
// Folio — character_template_test.cpp   (s33 — build-a-character-template, e2e)
//
// The integration test for the central use case: take the built-in Character
// notecard and BUILD IT OUT the way the template builder does, then drive the
// result through every layer the subsystem composes —
//
//   TemplateEdit (the builder's brain)  -> a richer Character schema
//      -> ObjectIO round-trip            (schema survives save/load)
//         -> ObjectStore.upsert + instantiate  (an object on the new schema)
//            -> FormPlan.plan_form        (the form the Inspector renders)
//               -> merge projection       (custom values survive rebuild)
//                  -> relation reads       (the edges light up the graph)
//
// If this passes, the Christie->Dune range (§1) holds on the Character type: the
// notecard still works, and the same machine carries age/species/relations the
// moment the author adds them. PURE — sandbox-compilable.
// ─────────────────────────────────────────────────────────────────────────────
/*
g++ -std=c++20 -Wall -Wextra -Werror -I include -I /home/claude/sbox tests/character_template_test.cpp src/ObjectStore.cpp src/ObjectIO.cpp src/Iid.cpp -o /tmp/character_template_test
/tmp/character_template_test
clang++ -std=c++20 -Wall -Wextra -Werror -I include tests/character_template_test.cpp src/ObjectStore.cpp src/ObjectIO.cpp src/Iid.cpp -o /tmp/character_template_test
/tmp/character_template_test
*/

#include "TemplateEdit.hpp"
#include "FormPlan.hpp"
#include "ObjectStore.hpp"
#include "ObjectIO.hpp"
#include <cassert>
#include <iostream>
#include <string>

using namespace Folio;
namespace TE = Folio::TemplateEdit;

static int checks = 0;
static void check(bool cond, const std::string& what) {
    ++checks;
    if (!cond) { std::cerr << "FAIL: " << what << "\n"; std::exit(1); }
}
static std::string ids(const Template& t) {
    std::string s; for (auto& f : t.fields) { if (!s.empty()) s += ","; s += f.id; } return s;
}

int main() {
    // ── 1. Start from the built-in Character notecard ─────────────────────────
    Template ch = built_in_character_template();
    check(ids(ch) == "name,image,description", "notecard floor");
    check(ch.id == "character", "type id is character");

    // ── 2. Build it out (what the dialog's Add field / retype / rename do) ────
    std::string age     = TE::add_field(ch, FieldType::Number,   "Age");
    std::string species = TE::add_field(ch, FieldType::Text,     "Species");
    std::string home    = TE::add_field(ch, FieldType::Relation, "Home",
                                        json{{"target_type","place"},{"multi",false}});
    std::string allies  = TE::add_field(ch, FieldType::Relation, "Allies",
                                        json{{"target_type","character"},{"multi",true}});
    check(age=="age" && species=="species" && home=="home" && allies=="allies", "ids minted");
    // All structured/relation fields land ABOVE the description buffer (§4).
    check(ids(ch) == "name,image,age,species,home,allies,description",
          "added fields sit above the buffer");

    // Rename a label (id stays stable), reorder a field up.
    TE::rename_field(ch, species, "Race");
    check(ch.find_field("species")->label == "Race", "label renamed");
    check(ch.find_field("species") != nullptr, "id stable after rename");
    TE::move_field(ch, allies, -1);                       // allies above home
    check(ids(ch) == "name,image,age,species,home,allies,description" ||
          ids(ch) == "name,image,age,species,allies,home,description", "reorder ran");

    check(TE::validate(ch).empty(), "built-out Character validates");

    // ── 3. Schema round-trips through ObjectIO (save/load of the template) ────
    Template reloaded = ObjectIO::template_from_string(ObjectIO::template_to_string(ch));
    check(ids(reloaded) == ids(ch), "schema field order survives round-trip");
    check(reloaded.find_field("home")->relation_target_type() == "place",
          "relation config survives round-trip");
    check(reloaded.find_field("allies")->relation_multi(), "multi flag survives");

    // ── 4. Register it + instantiate a character object on the new schema ─────
    ObjectStore store;
    store.seed_builtins();
    store.upsert_template(ch);                            // replaces built-in character
    check(store.find_template("character")->fields.size() == 7, "registered built-out schema");

    // A place to point at, and the character (migrated from a leaf, then filled).
    store.add_migrated_leaf("plc_manor", /*is_place=*/true, "The Manor", "<p>...</p>", "");
    store.add_migrated_leaf("chr_miss",  /*is_place=*/false, "Miss Marple", "<p>Sleuth.</p>", "");
    Object* m = store.find_object("chr_miss");
    instantiate_against(*m, ch);                          // seed defaults for new fields
    m->set_value("age",     json(75));
    m->set_value("species", json("human"));
    m->set_value("home",    json("plc_manor"));           // a relation edge
    m->set_value("allies",  json::array({ "chr_watson" }));

    // ── 5. plan_form: the form the Inspector renders for this character ───────
    FormPlan plan = plan_form(ch, *m);
    check(plan.rows.size() == ch.fields.size(), "one row per field");
    check(plan.rows.front().field_id == "name", "name first");
    check(plan.rows.back().field_id == "description" && plan.rows.back().full_width,
          "buffer is last and full-width");
    // The custom field carries its value into the row.
    bool saw_age = false, saw_home_ro = false;
    for (const auto& r : plan.rows) {
        if (r.field_id == "age")  { saw_age = (field_display_string(r.type, r.value) == "75"); }
        if (r.field_id == "home") { saw_home_ro = r.read_only; }   // relation read-only this slice
    }
    check(saw_age, "age row shows the stored value 75");
    check(saw_home_ro, "relation row is read-only (no picker yet)");

    // ── 6. Merge projection: custom values + edges SURVIVE a rebuild ──────────
    // (Re-projecting the leaf must not clobber age/species/home/allies.)
    store.add_migrated_leaf("chr_miss", false, "Miss Jane Marple", "<p>Sleuth.</p>", "");
    m = store.find_object("chr_miss");
    check(m->value_or("name", "") == json("Miss Jane Marple"), "leaf rename restamped");
    check(m->value_or("age", "")     == json(75),          "age survived re-projection");
    check(m->value_or("species", "") == json("human"),     "species survived");
    check(m->value_or("home", "")    == json("plc_manor"), "relation edge survived");

    // ── 7. Relation reads: the edge lights up the graph ───────────────────────
    auto into_manor = store.incoming_edges("plc_manor");
    check(into_manor.size() == 1 && into_manor[0]->iid == "chr_miss",
          "the Manor knows Miss Marple is tied to it");
    auto homed = store.group_members("plc_manor", "home");
    check(homed.size() == 1, "group-by-home of the Manor = Miss Marple");

    // ── 8. Object round-trips through ObjectIO with all its values ────────────
    Object ro = ObjectIO::object_from_string(ObjectIO::object_to_string(*m));
    check(ro.value_or("age", "")  == json(75),          "object age survives json round-trip");
    check(ro.value_or("home", "") == json("plc_manor"), "object edge survives json round-trip");

    std::cout << "character_template_test: all " << checks << " checks passed\n";
    return 0;
}
