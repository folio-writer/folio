// ─────────────────────────────────────────────────────────────────────────────
// Folio — heading_sections_s39_test.cpp   (s39 — heading-as-marker sections)
//
// A "section" is a Heading FieldType dropped into the flat fields array: a layout
// marker with NO value. This verifies the marker's pure contract — it round-trips
// as a type, defaults to null, is skipped by instantiation (no junk value on the
// object), coerces to null (never written), and is flagged full-width so the form
// renders it as a header. The GTK form renderer (grouping fields under headings)
// is thin hands over exactly these facts.
// ─────────────────────────────────────────────────────────────────────────────
/*
g++ -std=c++20 -Wall -Wextra -Werror -I ../include -I /home/claude/sbox heading_sections_s39_test.cpp ../src/ObjectIO.cpp -o /tmp/heading_sections_s39_test
/tmp/heading_sections_s39_test
clang++ -std=c++20 -Wall -Wextra -Werror -I ../include heading_sections_s39_test.cpp ../src/ObjectIO.cpp -o /tmp/heading_sections_s39_test
/tmp/heading_sections_s39_test
*/
#include "Object.hpp"
#include "FormPlan.hpp"
#include "TemplateEdit.hpp"
#include "ObjectIO.hpp"

#include <iostream>
#include <string>

using namespace Folio;

static int g_checks = 0, g_failed = 0;
static void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) { ++g_failed; std::cout << "  FAIL: " << what << "\n"; }
}

int main() {
    std::cout << "heading_sections_s39_test\n";

    // ── type round-trip ───────────────────────────────────────────────────────
    check(field_type_to_str(FieldType::Heading) == std::string("heading"), "Heading → \"heading\"");
    check(field_type_from_str("heading") == FieldType::Heading, "\"heading\" → Heading");
    check(field_type_from_str("nonsense") == FieldType::Unknown, "unknown str still → Unknown");

    // ── marker has no value ───────────────────────────────────────────────────
    check(field_default_value(FieldType::Heading).is_null(), "Heading default is null");
    check(field_is_full_width(FieldType::Heading), "Heading is full-width (a header row)");
    check(!field_type_is_relation(FieldType::Heading), "Heading is not a relation (no edge)");

    FieldSchema h; h.id = "sec_physical"; h.type = FieldType::Heading; h.label = "Physical";
    check(coerce_field_value(h, json("anything")).is_null(), "Heading coerces to null");

    // ── a template with headings: instantiate skips them ──────────────────────
    Template t; t.id = "tpl_char"; t.type_name = "Character"; t.category = "character";
    FieldSchema name; name.id = "name"; name.type = FieldType::Text; name.label = "Name";
    FieldSchema physical; physical.id = "sec_physical"; physical.type = FieldType::Heading;
    physical.label = "Physical";
    FieldSchema height; height.id = "height"; height.type = FieldType::Number; height.label = "Height";
    FieldSchema back; back.id = "sec_back"; back.type = FieldType::Heading; back.label = "Background";
    FieldSchema origin; origin.id = "origin"; origin.type = FieldType::Text; origin.label = "Origin";
    t.fields = { name, physical, height, back, origin };

    Object o; o.type = "tpl_char";
    instantiate_against(o, t);
    check(o.has_value("name"),   "data field seeded");
    check(o.has_value("height"), "field under a heading seeded");
    check(o.has_value("origin"), "field under second heading seeded");
    check(!o.has_value("sec_physical"), "heading marker carries NO value on the object");
    check(!o.has_value("sec_back"),     "second heading marker carries no value");
    check(o.values.size() == 3, "exactly the 3 data fields stored, no heading junk");

    // ── plan_form keeps headings as ordered full-width rows (render = header) ──
    FormPlan p = plan_form(t, o);
    check(p.rows.size() == 5, "all 5 schema rows planned (headings included, in order)");
    check(p.rows[1].type == FieldType::Heading && p.rows[1].full_width,
          "heading row is full-width in plan order");
    check(p.rows[1].label == "Physical", "heading row carries its section label");
    // the render walks these in order: a Heading row opens a section, the rows after
    // it group under it until the next Heading — purely a function of order, so no
    // nested container is stored (round-trips flat).
    check(p.rows[0].type == FieldType::Text &&
          p.rows[2].type == FieldType::Number &&
          p.rows[3].type == FieldType::Heading,
          "order preserved: name, [Physical], height, [Background], origin");

    // ── headings survive a Template round-trip through ObjectIO ───────────────
    json tj = ObjectIO::template_to_json(t);
    Template back2 = ObjectIO::template_from_json(tj);
    check(back2.fields.size() == 5, "all fields incl. headings round-trip");
    check(back2.fields[1].type == FieldType::Heading, "heading type survives json");
    check(back2.fields[1].label == "Physical", "heading label survives json");
    check(back2.category == "character", "category survives alongside");

    // ── the builder's pure path: add_field(Heading) lands above the floor buffer
    // and validates, and instantiation still skips it ─────────────────────────
    {
        Template bt = built_in_character_template();   // has the trailing RichText buffer
        const std::size_t before = bt.fields.size();
        std::string sid = TemplateEdit::add_field(bt, FieldType::Heading, "Combat");
        check(!sid.empty(), "add_field(Heading) mints an id");
        check(bt.fields.size() == before + 1, "heading added");
        // inserted ABOVE the trailing buffer (last field stays the RichText buffer)
        check(bt.fields.back().type == FieldType::RichText, "floor buffer stays last");
        check(bt.fields[bt.fields.size() - 2].type == FieldType::Heading,
              "heading sits just above the buffer");
        check(TemplateEdit::validate(bt).empty(), "a template with a heading validates");

        Object bo; bo.type = bt.id;
        instantiate_against(bo, bt);
        check(!bo.has_value(sid), "instantiate still skips the builder-added heading");
    }

    std::cout << g_checks << " checks, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
