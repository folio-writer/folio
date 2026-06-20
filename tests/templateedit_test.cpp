// ─────────────────────────────────────────────────────────────────────────────
// Folio — templateedit_test.cpp   (s33 — the template builder's brain)
//
// Pins the schema-mutation invariants the GTK TemplateBuilderDialog relies on:
// stable minted ids (never reused, label-rename never touches them), uniqueness,
// the §4 floor buffer staying at the bottom through adds and reorders, and the
// save-time validation. Pure — sandbox-compilable, header-only logic.
// ─────────────────────────────────────────────────────────────────────────────
/*
g++ -std=c++20 -Wall -Wextra -Werror -I include -I /home/claude/sbox tests/templateedit_test.cpp -o /tmp/templateedit_test
/tmp/templateedit_test
clang++ -std=c++20 -Wall -Wextra -Werror -I include tests/templateedit_test.cpp -o /tmp/templateedit_test
/tmp/templateedit_test
*/

#include "TemplateEdit.hpp"
#include "Object.hpp"
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
    std::string s;
    for (const auto& f : t.fields) { if (!s.empty()) s += ","; s += f.id; }
    return s;
}

int main() {
    // ── slugify / mint ────────────────────────────────────────────────────────
    check(TE::slugify("Home World") == "home_world", "slugify spaces");
    check(TE::slugify("  Sigil!! ") == "sigil",       "slugify trims + punct");
    check(TE::slugify("---") == "field",              "slugify empty fallback");
    check(TE::slugify("Étude 2") == "tude_2",         "slugify drops non-ascii alnum");

    std::vector<FieldSchema> existing = {
        { "house", FieldType::Relation, "House", json::object() },
    };
    check(TE::mint_field_id(existing, "Allies") == "allies", "mint fresh id");
    check(TE::mint_field_id(existing, "House")  == "house_2", "mint dedupes");
    existing.push_back({ "house_2", FieldType::Text, "x", json::object() });
    check(TE::mint_field_id(existing, "House")  == "house_3", "mint dedupes again");

    // ── Start from the built-in character floor: name, image, description(buffer)
    Template t = built_in_character_template();
    check(ids(t) == "name,image,description", "floor order");
    check(TE::has_trailing_buffer(t), "floor has trailing buffer");

    // ── add_field: a structured field lands ABOVE the trailing buffer ─────────
    std::string sp = TE::add_field(t, FieldType::Text, "Species");
    check(sp == "species", "add returns minted id");
    check(ids(t) == "name,image,species,description", "structured field above buffer");

    // A relation field, same rule.
    std::string hs = TE::add_field(t, FieldType::Relation, "House",
                                   json{{"target_type","house"},{"multi",false}});
    check(hs == "house", "relation id minted");
    check(ids(t) == "name,image,species,house,description", "relation above buffer");
    check(t.find_field("house")->relation_target_type() == "house", "relation config kept");

    // A second richtext appends (extends the buffer region, stays at/after bottom).
    std::string notes = TE::add_field(t, FieldType::RichText, "GM Notes");
    check(ids(t) == "name,image,species,house,description,gm_notes", "richtext appends to end");

    // ── rename never touches the id ───────────────────────────────────────────
    check(TE::rename_field(t, "species", "Race"), "rename ok");
    check(t.find_field("species") != nullptr, "id stable after rename");
    check(t.find_field("species")->label == "Race", "label changed");

    // ── retype resets config ──────────────────────────────────────────────────
    check(TE::retype_field(t, "house", FieldType::Text), "retype ok");
    check(t.find_field("house")->type == FieldType::Text, "type changed");
    check(t.find_field("house")->config.empty(), "config reset on retype");

    // ── reorder: can't push a structured field below the buffer ───────────────
    // Current: name,image,species,house,description,gm_notes
    // Move 'name' down a lot — it must stop above the FIRST trailing buffer slot.
    // (Two richtext at the end; only the LAST is the pinned buffer, so the cap is
    //  n-2 = index 4 = 'description'.)
    TE::move_field(t, "name", +99);
    check(ids(t) == "image,species,house,description,name,gm_notes", "down-move capped above last buffer");

    // The trailing buffer itself can't move up.
    check(!TE::move_field(t, "gm_notes", -1), "trailing buffer pinned last");
    check(ids(t).rfind("gm_notes") == ids(t).size() - std::string("gm_notes").size(),
          "buffer still last");

    // Move up works and clamps at 0.
    TE::move_field(t, "house", -99);
    check(ids(t) == "house,image,species,description,name,gm_notes", "up-move clamps at 0");

    // ── remove ────────────────────────────────────────────────────────────────
    check(TE::remove_field(t, "species"), "remove ok");
    check(t.find_field("species") == nullptr, "field gone");
    check(!TE::remove_field(t, "nope"), "remove missing -> false");

    // ── ensure_floor_buffer: idempotent; appends when missing ─────────────────
    Template bare; bare.id = "species"; bare.type_name = "Species";
    bare.fields = { { "latin", FieldType::Text, "Latin name", json::object() } };
    check(!TE::has_trailing_buffer(bare), "bare has no buffer");
    TE::ensure_floor_buffer(bare);
    check(ids(bare) == "latin,description", "buffer appended with id 'description'");
    TE::ensure_floor_buffer(bare);
    check(ids(bare) == "latin,description", "ensure is idempotent");

    // If 'description' id is taken by a non-buffer field, mint a unique buffer id.
    Template clash; clash.type_name = "Thing";
    clash.fields = { { "description", FieldType::Text, "Short desc", json::object() } };
    TE::ensure_floor_buffer(clash);
    check(clash.fields.size() == 2, "buffer added despite id clash");
    check(clash.fields.back().type == FieldType::RichText, "trailing is the buffer");
    check(clash.fields.back().id != "description", "buffer minted a non-clashing id");

    // ── validate ──────────────────────────────────────────────────────────────
    check(TE::validate(t).empty(), "valid template passes");
    Template noname = t; noname.type_name = "   ";
    check(!TE::validate(noname).empty(), "blank type_name rejected");
    Template dup = built_in_character_template();
    dup.fields.push_back({ "name", FieldType::Text, "Dup", json::object() });
    check(TE::validate(dup).rfind("Duplicate field id", 0) == 0, "duplicate id rejected");

    std::cout << "templateedit_test: all " << checks << " checks passed\n";
    return 0;
}
