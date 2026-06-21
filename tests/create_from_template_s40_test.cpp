// ─────────────────────────────────────────────────────────────────────────────
// Folio — create_from_template_s40_test.cpp   (slice 3 — create from template)
//
// The pure heart of "stamp a Character from a template": a binder leaf whose
// template_id is a template NODE's iid must project to an object typed to that
// template, with the template's fields seeded — and category metadata must be
// readable from the node's schema so the picker can filter. The GTK stamp only
// sets leaf.template_id = node.iid and the picker filters by category; this proves
// the projection turns that into a correct instance, and that a schema-less
// (boilerplate) template stays the buffer-only copy case.
// ─────────────────────────────────────────────────────────────────────────────
/*
g++ -std=c++20 -Wall -Wextra -Werror -I ../include -I /home/claude/sbox create_from_template_s40_test.cpp ../src/ObjectIO.cpp ../src/ObjectStore.cpp ../src/Iid.cpp -o /tmp/create_from_template_s40_test
/tmp/create_from_template_s40_test
clang++ -std=c++20 -Wall -Wextra -Werror -I ../include create_from_template_s40_test.cpp ../src/ObjectIO.cpp ../src/ObjectStore.cpp ../src/Iid.cpp -o /tmp/create_from_template_s40_test
/tmp/create_from_template_s40_test
*/
#include "Object.hpp"
#include "ObjectStore.hpp"
#include "ObjectIO.hpp"

#include <iostream>
#include <string>

using namespace Folio;

static int g_checks = 0, g_failed = 0;
static void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) { ++g_failed; std::cout << "  FAIL: " << what << "\n"; }
}

// A node-backed schema, as the builder authors it (cloned floor + a custom field +
// a heading + a category), serialized the way a Template node stores form_schema.
static json villain_schema() {
    Template t = built_in_character_template();
    t.builtin   = false;
    t.type_name = "Villain";
    t.category  = "character";
    FieldSchema sec; sec.id = "sec_combat"; sec.type = FieldType::Heading; sec.label = "Combat";
    FieldSchema menace; menace.id = "menace"; menace.type = FieldType::Number; menace.label = "Menace";
    t.fields.push_back(sec);
    t.fields.push_back(menace);
    return ObjectIO::template_to_json(t);
}

// Mirror the picker's category read (Sidebar reads form_schema.value("category")).
static std::string node_category(const json& form_schema) {
    return form_schema.is_object() ? form_schema.value("category", std::string{})
                                   : std::string{};
}

int main() {
    std::cout << "create_from_template_s40_test\n";

    ObjectStore store;
    store.seed_builtins();

    // The template node is projected into the registry under its node iid (s38).
    json vs = villain_schema();
    check(store.adopt_template_node("iid_villain", vs), "villain template node adopted");

    // ── the picker's category filter reads the node's schema ──────────────────
    check(node_category(vs) == "character", "category readable from form_schema");
    check(node_category(json::object()) == "", "schema-less node → no category (boilerplate)");

    // ── STAMP: a leaf adopts the template by node iid → instance ───────────────
    // (the GTK side just sets leaf.template_id = tpl.iid; the projection does this)
    store.add_migrated_leaf("char_bob", /*is_place=*/false, "Bob", "<p>bio</p>",
                            /*image*/"", /*tagline*/"", /*role*/"",
                            /*template_id=*/"iid_villain");
    const Object* bob = store.find_object("char_bob");
    check(bob != nullptr, "stamped character exists");
    check(bob && bob->type == "iid_villain", "instance is typed to the chosen template");
    check(bob && bob->value_or("name", json()) == json("Bob"), "leaf title → name");
    check(bob && bob->has_value("menace"), "template's custom field seeded on the instance");
    check(bob && bob->value_or("menace", json()) == json(0), "custom field seeded to its default");
    check(bob && !bob->has_value("sec_combat"), "heading marker seeds no value on the instance");
    check(bob && bob->has_value("description"), "floor buffer field present");

    // ── a plain character (no template_id) stays the floor type ───────────────
    store.add_migrated_leaf("char_plain", false, "Anon", "", "", "", "", /*template_id=*/"");
    const Object* anon = store.find_object("char_plain");
    check(anon && anon->type == "character", "no template_id → floor character type");
    check(anon && !anon->has_value("menace"), "floor instance has no villain field");

    // ── editing the template re-projects onto its instances ───────────────────
    // (re-adopt with a new field, re-run the leaf projection: the instance gains it)
    {
        Template t = ObjectIO::template_from_json(vs);
        FieldSchema lair; lair.id = "lair"; lair.type = FieldType::Text; lair.label = "Lair";
        t.fields.push_back(lair);
        store.adopt_template_node("iid_villain", ObjectIO::template_to_json(t));
        store.add_migrated_leaf("char_bob", false, "Bob", "<p>bio</p>", "", "", "", "iid_villain");
        const Object* b2 = store.find_object("char_bob");
        check(b2 && b2->has_value("lair"), "new template field appears on the existing instance");
        check(b2 && b2->has_value("menace"), "and the prior fields are preserved");
    }

    std::cout << g_checks << " checks, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
