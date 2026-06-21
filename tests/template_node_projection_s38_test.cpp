// ─────────────────────────────────────────────────────────────────────────────
// Folio — template_node_projection_s38_test.cpp   (s38 — template merge, slice 1)
//
// The PURE heart of the merge's slice 1: a Template binder node's schema projects
// into the ObjectStore registry, the node's iid serving as the template's stable
// id. The GTK tree-walk (rebuild_object_store over Section::Templates) is thin hands
// over ObjectStore::adopt_template_node — this verifies that helper, the new
// `category` round-trip, and the seed+project ordering, so the binder side carries
// no novel logic to test on the device.
// ─────────────────────────────────────────────────────────────────────────────
/*
g++ -std=c++20 -Wall -Wextra -Werror -I ../include -I /home/claude/sbox template_node_projection_s38_test.cpp ../src/ObjectIO.cpp ../src/ObjectStore.cpp ../src/Iid.cpp -o /tmp/template_node_projection_s38_test
/tmp/template_node_projection_s38_test
clang++ -std=c++20 -Wall -Wextra -Werror -I ../include template_node_projection_s38_test.cpp ../src/ObjectIO.cpp ../src/ObjectStore.cpp ../src/Iid.cpp -o /tmp/template_node_projection_s38_test
/tmp/template_node_projection_s38_test
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

// A node-backed template, as the builder would author it: a cloned floor + a field,
// tagged with a category. We serialize it the way a Template binder node stores its
// `form_schema`, then project it the way rebuild_object_store will.
static json authored_schema(const std::string& type_name,
                            const std::string& category,
                            const std::string& extra_field_id) {
    Template t = built_in_character_template();   // start from a floor (cloned shape)
    t.builtin   = false;
    t.id        = "ignored-overwritten-by-node-iid";
    t.type_name = type_name;
    t.category  = category;
    FieldSchema extra; extra.id = extra_field_id; extra.label = "Extra";
    extra.type = FieldType::Number;
    t.fields.push_back(extra);
    return ObjectIO::template_to_json(t);
}

int main() {
    std::cout << "template_node_projection_s38_test\n";

    // ── category round-trips through Template (de)serialization ────────────────
    {
        Template t = built_in_place_template();
        json j = ObjectIO::template_to_json(t);
        check(j.value("category", "") == "place", "category serializes");
        Template back = ObjectIO::template_from_json(j);
        check(back.category == "place", "category round-trips");
        // built-in floor with no category override still omits cleanly when empty
        Template u; u.id = "u"; u.type_name = "U";
        check(!ObjectIO::template_to_json(u).contains("category"),
              "empty category omitted from json");
        check(ObjectIO::template_from_json(json{{"id","x"}}).category.empty(),
              "missing category → empty on load");
    }

    // ── the projection: two template nodes adopt into the registry ─────────────
    ObjectStore store;
    store.seed_builtins();                       // built-ins first (code presets)
    const std::size_t n_builtin = store.templates.size();
    check(n_builtin == 2, "two built-ins seeded");

    json villain_schema = authored_schema("Villain",  "character", "menace");
    json house_schema   = authored_schema("Great House","place",    "seat");

    check(store.adopt_template_node("iid_villain", villain_schema), "adopt villain node");
    check(store.adopt_template_node("iid_house",   house_schema),   "adopt house node");

    const Template* v = store.find_template("iid_villain");
    check(v != nullptr, "villain template registered under node iid");
    check(v && v->id == "iid_villain", "template id IS the node iid (not the schema's)");
    check(v && v->builtin == false, "node template is editable (builtin false)");
    check(v && v->type_name == "Villain", "type_name carried");
    check(v && v->category == "character", "category carried");
    check(v && v->find_field("menace") != nullptr, "authored field survives projection");
    check(v && v->find_field("name") != nullptr, "floor fields survive projection");

    const Template* h = store.find_template("iid_house");
    check(h && h->category == "place", "second node's category carried");

    // ── built-ins remain present + pristine after projection ───────────────────
    const Template* ch = store.find_template("character");
    check(ch && ch->builtin, "built-in character still present + locked");
    check(store.find_template("place") != nullptr, "built-in place still present");

    // ── empty / malformed schema is ignored (no junk type) ─────────────────────
    check(!store.adopt_template_node("iid_empty", json()),        "null schema ignored");
    check(!store.adopt_template_node("iid_empty", json::array()), "non-object schema ignored");
    check(!store.adopt_template_node("",          villain_schema),"empty node iid ignored");
    check(store.find_template("iid_empty") == nullptr, "no template from ignored adopts");

    // ── re-adopt is idempotent in place (edit + re-project) ────────────────────
    json villain_v2 = authored_schema("Villain v2", "character", "menace");
    store.adopt_template_node("iid_villain", villain_v2);
    const Template* v2 = store.find_template("iid_villain");
    check(v2 && v2->type_name == "Villain v2", "re-adopt updates in place");
    std::size_t villain_count = 0;
    for (const auto& t : store.templates) if (t.id == "iid_villain") ++villain_count;
    check(villain_count == 1, "re-adopt does not duplicate the registry entry");

    // ── coexistence: a blob clone (tpl_ id) and a node template (iid) both live ─
    std::string clone_id = store.clone_template("character");   // tpl_ id, blob-era
    check(!clone_id.empty(), "blob clone still mints a tpl_ id");
    check(store.find_template(clone_id) != nullptr, "blob clone resolvable");
    check(store.find_template("iid_villain") != nullptr, "node template resolvable alongside");

    std::cout << g_checks << " checks, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
