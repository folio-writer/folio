// ─────────────────────────────────────────────────────────────────────────────
// Folio — formplan_test.cpp   (s31)   Pure unit check for the form-render BRAIN.
// The GTK ObjectForm is a thin renderer over plan_form()/apply_field(); this
// proves the logic it leans on:
//   1. plan_form walks the template's fields IN ORDER, one row each.
//   2. Each row carries the object's current value, defaulted to the field zero
//      when absent (the renderer can assume a present, shaped value).
//   3. Orphan values (in the object, not in the schema) are NOT planned —
//      retained-but-hidden (orphan-and-keep, §12).
//   4. Layout/read-only hints: richtext/list full-width; relation read-only.
//   5. apply_field coerces raw widget input to the field's json shape on write.
//   6. The floor template (Character) plans exactly the notecard: name/image/
//      description.
//
// Pure: no GTK/GLib. Sandbox nlohmann at -I /home/claude/sbox; Fedora system.
//
/*
g++ -std=c++20 -Wall -Wextra -Werror -I /home/claude/sbox -Iinclude tests/formplan_test.cpp src/ObjectIO.cpp src/Iid.cpp -o /tmp/formplan_test && /tmp/formplan_test
clang++ -std=c++20 -Wall -Wextra -Werror -Iinclude tests/formplan_test.cpp src/ObjectIO.cpp src/Iid.cpp -o /tmp/formplan_test && /tmp/formplan_test
*/

#include "FormPlan.hpp"
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

// A rich template covering every layout/coercion branch.
static Template species_template() {
    Template t;
    t.id = "species"; t.type_name = "Species"; t.icon = "folio-tag-symbolic";
    t.fields = {
        { "name",      FieldType::Text,     "Name",      json::object() },
        { "homeworld", FieldType::Relation, "Homeworld", json{ { "target_type", "place" } } },
        { "menace",    FieldType::Slider,   "Menace",    json{ { "min", 0 }, { "max", 10 } } },
        { "sentient",  FieldType::Toggle,   "Sentient",  json::object() },
        { "traits",    FieldType::List,     "Traits",    json::object() },
        { "lore",      FieldType::RichText, "Lore",      json::object() },
    };
    return t;
}

static void test_plan_order_and_defaults() {
    std::printf("[plan: order + defaults]\n");
    Template t = species_template();
    Object o; o.iid = "obj_x"; instantiate_against(o, t);

    FormPlan p = plan_form(t, o);
    check(p.type_name == "Species" && p.icon == "folio-tag-symbolic", "plan carries type heading + icon");
    check(p.rows.size() == 6, "one row per schema field");
    // order preserved
    const char* ids[] = { "name", "homeworld", "menace", "sentient", "traits", "lore" };
    bool ordered = true;
    for (size_t i = 0; i < p.rows.size(); ++i) ordered = ordered && (p.rows[i].field_id == ids[i]);
    check(ordered, "rows follow the template's field order");

    // defaults present + shaped
    check(p.rows[0].value == "", "text default empty");
    check(p.rows[2].value == 0, "slider default 0");
    check(p.rows[3].value == false, "toggle default false");
    check(p.rows[4].value.is_array(), "list default array");
}

static void test_layout_and_readonly_hints() {
    std::printf("[layout + read-only hints]\n");
    Template t = species_template();
    Object o; o.iid = "obj_x"; instantiate_against(o, t);
    FormPlan p = plan_form(t, o);

    auto row = [&](const std::string& id) -> const FormRow& {
        for (const auto& r : p.rows) if (r.field_id == id) return r;
        static FormRow none; return none;
    };
    check(!row("name").full_width, "text is compact row");
    check(row("lore").full_width,  "richtext is full-width block");
    check(row("traits").full_width,"list is full-width block");
    check(row("homeworld").read_only, "relation is read-only (no picker yet)");
    check(!row("name").read_only, "non-relation is editable");
}

static void test_values_carried() {
    std::printf("[values carried into plan]\n");
    Template t = species_template();
    Object o; o.iid = "obj_x"; instantiate_against(o, t);
    o.set_value("name", "Sandworm");
    o.set_value("menace", 9);
    o.set_value("sentient", true);

    FormPlan p = plan_form(t, o);
    for (const auto& r : p.rows) {
        if (r.field_id == "name")     check(r.value == "Sandworm", "text value carried");
        if (r.field_id == "menace")   check(r.value == 9, "number value carried");
        if (r.field_id == "sentient") check(r.value == true, "toggle value carried");
    }
}

static void test_orphans_not_planned() {
    std::printf("[orphans not planned]\n");
    // Floor character template, but the object also has an orphan "role" value
    // (as a migrated character does) that the floor schema does not surface.
    Template c = built_in_character_template();
    Object o = ObjectIO::migrate_legacy_leaf("chr_1", false, "Duncan", "<p>x</p>",
                                             "", "loyal", "Supporting");
    check(o.has_value("role"), "object carries the orphan role value");

    FormPlan p = plan_form(c, o);
    check(p.rows.size() == 3, "floor plans exactly the notecard (name/image/description)");
    bool has_role_row = false;
    for (const auto& r : p.rows) if (r.field_id == "role" || r.field_id == "tagline") has_role_row = true;
    check(!has_role_row, "orphan values are NOT rendered (retained-but-hidden)");
    // but they survive in the object (round-trip already proven elsewhere)
    check(o.value_or("role", json("")) == "Supporting", "orphan still retained in the object");
}

static void test_apply_field_coercion() {
    std::printf("[apply_field coercion]\n");
    Template t = species_template();
    Object o; o.iid = "obj_x"; instantiate_against(o, t);

    // number from a string widget → stored as number
    apply_field(o, t.fields[2] /*menace slider*/, json("7"));
    check(o.value_or("menace", json("")) == 7, "string '7' coerced to number");

    // toggle
    apply_field(o, t.fields[3] /*sentient*/, json(true));
    check(o.value_or("sentient", json(false)) == true, "toggle stored as bool");

    // list from array
    apply_field(o, t.fields[4] /*traits*/, json::array({ "armored", "burrowing" }));
    check(o.value_or("traits", json::array()).size() == 2, "list stored as array");

    // text
    apply_field(o, t.fields[0] /*name*/, json("Shai-Hulud"));
    check(o.value_or("name", json("")) == "Shai-Hulud", "text stored as string");

    // garbage into a number → safe 0, never throws
    apply_field(o, t.fields[2], json("not-a-number"));
    check(o.value_or("menace", json("")) == 0, "non-numeric string → 0 (no throw)");
}

static void test_display_string() {
    std::printf("[display strings]\n");
    check(field_display_string(FieldType::Text, json("Paul")) == "Paul", "text → itself");
    check(field_display_string(FieldType::Text, json("")) == "", "empty text → empty");
    check(field_display_string(FieldType::Toggle, json(true)) == "Yes", "toggle true → Yes");
    check(field_display_string(FieldType::Toggle, json(false)) == "No", "toggle false → No");
    check(field_display_string(FieldType::Number, json(7)) == "7", "integer number → 7");
    check(field_display_string(FieldType::Slider, json(3.5)) == "3.5", "fractional slider → 3.5");
    check(field_display_string(FieldType::Color, json(0)) == "0", "color index → 0");
    check(field_display_string(FieldType::List, json::array({ "a", "b", "c" })) == "a · b · c",
          "list joined with separator");
    check(field_display_string(FieldType::List, json::array()) == "", "empty list → empty");
    check(field_display_string(FieldType::Relation, json("plc_abc")) == "plc_abc", "relation → iid text");
}

int main() {
    std::printf("=== Folio FormPlan — s31 ===\n");
    test_plan_order_and_defaults();
    test_layout_and_readonly_hints();
    test_values_carried();
    test_orphans_not_planned();
    test_apply_field_coercion();
    test_display_string();

    std::printf("\n%d checks, %d failures\n", g_checks, g_fails);
    if (g_fails == 0) std::printf("ALL PASS\n");
    return g_fails == 0 ? 0 : 1;
}
