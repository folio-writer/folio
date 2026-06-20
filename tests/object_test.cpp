// ─────────────────────────────────────────────────────────────────────────────
// Folio — object_test.cpp   (s31)   Pure unit check for the objects & templates
// model + its json seam. Proves the four load-bearing claims of the slice:
//   1. The built-in default templates are the floor case {name,image,description}.
//   2. Template + Object round-trip through json verbatim (config + values).
//   3. Relation values (single + multi) survive and resolve to graph edges.
//   4. Orphan-and-keep: a value whose field has left the schema survives the
//      round-trip and is restored when the field returns (§12).
//   5. Migration (§8) is reframe-not-replace: a legacy character/place leaf maps
//      onto the default template with NOTHING lost (role/tagline kept as orphans).
//
// Pure: no GTK/GLib. Sandbox uses vendored nlohmann at -I /home/claude/sbox;
// Fedora uses system nlohmann. Header survives the same -Werror flags as the code.
//
/*
g++ -std=c++20 -Wall -Wextra -Werror -I /home/claude/sbox -Iinclude tests/object_test.cpp src/ObjectIO.cpp src/Iid.cpp -o /tmp/object_test && /tmp/object_test
clang++ -std=c++20 -Wall -Wextra -Werror -Iinclude tests/object_test.cpp src/ObjectIO.cpp src/Iid.cpp -o /tmp/object_test && /tmp/object_test
*/

#include "Object.hpp"
#include "ObjectIO.hpp"
#include "Iid.hpp"

#include <cstdio>
#include <string>
#include <vector>

using namespace Folio;

// ── tiny assert harness ──────────────────────────────────────────────────────
static int g_checks = 0;
static int g_fails  = 0;

static void check(bool cond, const char* what) {
    ++g_checks;
    if (!cond) {
        ++g_fails;
        std::printf("  FAIL: %s\n", what);
    }
}

// ── 1. built-in default templates are the floor ──────────────────────────────
static void test_builtins() {
    std::printf("[builtins]\n");
    Template c = built_in_character_template();
    Template p = built_in_place_template();

    check(c.id == "character", "character template id");
    check(c.type_name == "Character", "character type_name");
    check(c.icon == "folio-character-symbolic", "character icon");
    check(c.fields.size() == 3, "character has 3 floor fields");

    check(p.id == "place", "place template id");
    check(p.type_name == "Place", "place type_name");
    check(p.id != c.id, "character and place are DISTINCT types");

    // floor shape: name=text, image=image, description=richtext
    check(c.fields[0].id == "name" && c.fields[0].type == FieldType::Text, "field0 = name/text");
    check(c.fields[1].id == "image" && c.fields[1].type == FieldType::Image, "field1 = image/image");
    check(c.fields[2].id == "description" && c.fields[2].type == FieldType::RichText,
          "field2 = description/richtext (the buffer floor)");

    const FieldSchema* f = c.find_field("description");
    check(f != nullptr && f->label == "Description", "find_field by stable id");
    check(c.find_field("nope") == nullptr, "find_field misses unknown id");
}

// ── 2. template round-trip preserves fields + config ─────────────────────────
static void test_template_roundtrip() {
    std::printf("[template round-trip]\n");
    Template t;
    t.id = "species"; t.type_name = "Species"; t.icon = "folio-tag-symbolic";
    t.fields = {
        { "common_name", FieldType::Text,     "Common Name", json::object() },
        { "homeworld",   FieldType::Relation, "Homeworld",
            json{ { "target_type", "place" }, { "multi", false } } },
        { "traits",      FieldType::List,     "Traits",      json::object() },
        { "menace",      FieldType::Slider,   "Menace",
            json{ { "min", 0 }, { "max", 10 }, { "step", 1 } } },
    };

    Template r = ObjectIO::template_from_string(ObjectIO::template_to_string(t));
    check(r.id == t.id && r.type_name == t.type_name && r.icon == t.icon, "template head survives");
    check(r.fields.size() == 4, "all fields survive");
    check(r.fields[1].type == FieldType::Relation, "relation type survives");
    check(r.fields[1].relation_target_type() == "place", "relation target_type survives in config");
    check(r.fields[1].relation_multi() == false, "relation multi flag survives");
    check(r.fields[3].config.value("max", -1) == 10, "slider config survives verbatim");

    // an unknown future type degrades to Unknown, not a crash
    Template u = ObjectIO::template_from_string(
        R"({"id":"x","fields":[{"id":"f","type":"hologram","label":"L"}]})");
    check(u.fields.size() == 1 && u.fields[0].type == FieldType::Unknown,
          "unknown field type → Unknown (forward-compat)");
}

// ── 3. object instantiate + round-trip + relation edges ──────────────────────
static void test_object_and_edges() {
    std::printf("[object + edges]\n");
    Template t;
    t.id = "house"; t.type_name = "House"; t.icon = "folio-tag-symbolic";
    t.fields = {
        { "name",     FieldType::Text,     "Name",     json::object() },
        { "homeworld",FieldType::Relation, "Homeworld",
            json{ { "target_type", "place" } } },
        { "members",  FieldType::Relation, "Members",
            json{ { "target_type", "character" }, { "multi", true } } },
        { "active",   FieldType::Toggle,   "Active",   json::object() },
        { "rank",     FieldType::Number,   "Rank",     json::object() },
    };

    Object o;
    o.iid = make_iid(IidKind::Unknown);  // a stand-in id for a user-defined type
    instantiate_against(o, t);

    // defaults are seeded and well-shaped
    check(o.type == "house", "instantiate sets type");
    check(o.value_or("name", json("x")) == "", "text default empty string");
    check(o.value_or("active", json("x")) == false, "toggle default false");
    check(o.value_or("rank", json("x")) == 0, "number default 0");
    check(o.value_or("members", json("x")).is_array(), "multi relation default array");

    // fill: a single homeworld pointer and two members
    std::string plc = make_iid(IidKind::Place);
    std::string m1  = make_iid(IidKind::Character);
    std::string m2  = make_iid(IidKind::Character);
    o.set_value("name", "Atreides");
    o.set_value("homeworld", plc);
    o.set_value("members", json::array({ m1, m2 }));
    o.set_value("active", true);
    o.set_value("rank", 1);

    // edges: one + two = three outgoing, all well-formed iids
    auto edges = o.outgoing_edges(t);
    check(edges.size() == 3, "outgoing_edges resolves single + multi relations");
    bool all_iids = true;
    for (const auto& e : edges) all_iids = all_iids && is_iid(e);
    check(all_iids, "every edge target is a well-formed iid");

    // round-trip preserves the whole field-map
    Object r = ObjectIO::object_from_string(ObjectIO::object_to_string(o));
    check(r.iid == o.iid && r.type == o.type, "object head survives");
    check(r.value_or("name", json("")) == "Atreides", "text value survives");
    check(r.value_or("homeworld", json("")) == plc, "single relation survives");
    check(r.value_or("members", json::array()).size() == 2, "multi relation survives");
    check(r.outgoing_edges(t).size() == 3, "edges still resolve after round-trip");

    // an EMPTY relation pointer draws no edge
    Object empty;
    empty.iid = "obj_blank"; instantiate_against(empty, t);
    check(empty.outgoing_edges(t).empty(), "empty relation pointers draw no edges");
}

// ── 4. orphan-and-keep (§12) ─────────────────────────────────────────────────
static void test_orphan_and_keep() {
    std::printf("[orphan-and-keep]\n");
    // A "full" template, an object filled against it.
    Template full;
    full.id = "char2"; full.type_name = "Character";
    full.fields = {
        { "name", FieldType::Text, "Name", json::object() },
        { "secret_origin", FieldType::RichText, "Secret Origin", json::object() },
    };
    Object o; o.iid = "obj_orphan"; instantiate_against(o, full);
    o.set_value("name", "Paul");
    o.set_value("secret_origin", "<p>raised on Caladan</p>");

    // The template shrinks: "secret_origin" is removed from the schema.
    Template shrunk;
    shrunk.id = "char2"; shrunk.type_name = "Character";
    shrunk.fields = { { "name", FieldType::Text, "Name", json::object() } };

    // The renderer would walk only `shrunk` → secret_origin is not displayed,
    // but its VALUE must remain in the map (we never delete on schema change).
    Object r = ObjectIO::object_from_string(ObjectIO::object_to_string(o));
    check(r.has_value("secret_origin"), "orphan value survives round-trip");
    check(r.value_or("secret_origin", json("")) == "<p>raised on Caladan</p>",
          "orphan value is intact");

    // Re-adding the field restores it for display — nothing was lost.
    check(r.has_value("name") && r.value_or("name", json("")) == "Paul",
          "live field unaffected");
    // instantiate against the FULL template again: the orphan is already present,
    // so it is NOT overwritten with a default (idempotent restore).
    instantiate_against(r, full);
    check(r.value_or("secret_origin", json("")) == "<p>raised on Caladan</p>",
          "re-add restores orphan (not clobbered by default)");
}

// ── 5. migration is reframe-not-replace (§8) ─────────────────────────────────
static void test_migration() {
    std::printf("[migration]\n");
    std::string chr = make_iid(IidKind::Character);

    Object o = ObjectIO::migrate_legacy_leaf(
        /*iid*/        chr,
        /*is_place*/   false,
        /*title*/      "Duncan Idaho",
        /*buffer_html*/"<p>Swordmaster of the Ginaz.</p>",
        /*image_path*/ "assets/duncan.png",
        /*tagline*/    "loyal to the last",
        /*role*/       "Supporting");

    check(o.iid == chr, "migrated object keeps the SAME iid (same part)");
    check(o.type == "character", "migrated to the character default template");
    check(o.value_or("name", json("")) == "Duncan Idaho", "title → name");
    check(o.value_or("description", json("")) == "<p>Swordmaster of the Ginaz.</p>",
          "text buffer → description (richtext)");
    check(o.value_or("image", json("")) == "assets/duncan.png", "image_path → image");
    // nothing lost: legacy one-liner + role preserved as orphans
    check(o.value_or("tagline", json("")) == "loyal to the last", "legacy tagline kept (orphan)");
    check(o.value_or("role", json("")) == "Supporting", "legacy role kept (orphan)");

    // round-trip the migrated object cleanly
    Object r = ObjectIO::object_from_string(ObjectIO::object_to_string(o));
    check(r.value_or("name", json("")) == "Duncan Idaho", "migrated object round-trips");

    // place leaf selects the place template
    std::string plc = make_iid(IidKind::Place);
    Object pl = ObjectIO::migrate_legacy_leaf(plc, /*is_place*/true,
                                              "Arrakis", "<p>Dune.</p>", "");
    check(pl.type == "place", "place leaf → place default template");
    check(pl.value_or("name", json("")) == "Arrakis", "place title → name");
    check(!pl.has_value("role"), "no spurious orphan when legacy fields empty");
}

int main() {
    std::printf("=== Folio object & template model — s31 ===\n");
    test_builtins();
    test_template_roundtrip();
    test_object_and_edges();
    test_orphan_and_keep();
    test_migration();

    std::printf("\n%d checks, %d failures\n", g_checks, g_fails);
    if (g_fails == 0) std::printf("ALL PASS\n");
    return g_fails == 0 ? 0 : 1;
}
