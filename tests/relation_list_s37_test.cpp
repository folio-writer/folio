// ─────────────────────────────────────────────────────────────────────────────
// Folio — relation_list_s37_test.cpp   (s37 — relation picker + list value layer)
//
// The PURE brain the s37 GTK widgets render over: object display names, relation
// candidate assembly (objects_of_type + object_display_name → FieldChoice), the
// iid→label resolution the picker's selection line and the read-only summary use
// (orphan-safe), and the value coercion + round-trip for relation (single iid /
// multi array) and list (array<string>) values. The GTK relation picker and list
// editor are thin hands over exactly this; verifying it here means the widgets
// carry no novel logic to test on the device.
// ─────────────────────────────────────────────────────────────────────────────
/*
g++ -std=c++20 -Wall -Wextra -Werror -I ../include -I /home/claude/sbox relation_list_s37_test.cpp ../src/ObjectIO.cpp ../src/ObjectStore.cpp ../src/Iid.cpp -o /tmp/relation_list_s37_test
/tmp/relation_list_s37_test
clang++ -std=c++20 -Wall -Wextra -Werror -I ../include relation_list_s37_test.cpp ../src/ObjectIO.cpp ../src/ObjectStore.cpp ../src/Iid.cpp -o /tmp/relation_list_s37_test
/tmp/relation_list_s37_test
*/
#include "Object.hpp"
#include "FormPlan.hpp"
#include "ObjectStore.hpp"
#include "ObjectIO.hpp"

#include <iostream>
#include <string>
#include <vector>

using namespace Folio;

static int g_checks = 0, g_failed = 0;
static void check(bool ok, const std::string& what) {
    ++g_checks;
    if (!ok) { ++g_failed; std::cout << "  FAIL: " << what << "\n"; }
}

// Mirror the Inspector's provider: candidates for a relation target_type are the
// store's objects of that type, each as {iid, display-name}.
static std::vector<FieldChoice> candidates_for(const ObjectStore& s,
                                               const std::string& target_type) {
    std::vector<FieldChoice> out;
    for (const Object* o : s.objects_of_type(target_type))
        out.push_back({ o->iid, object_display_name(*o) });
    return out;
}

int main() {
    std::cout << "relation_list_s37_test\n";

    // ── A small store: two places, two characters; one char points at a place ──
    ObjectStore store;
    store.seed_builtins();

    Object arrakis; arrakis.iid = "obj_arrakis"; arrakis.type = "place";
    arrakis.set_value("name", "Arrakis");
    Object caladan; caladan.iid = "obj_caladan"; caladan.type = "place";
    caladan.set_value("name", "Caladan");
    Object paul; paul.iid = "obj_paul"; paul.type = "character";
    paul.set_value("name", "Paul");
    Object nameless; nameless.iid = "obj_x"; nameless.type = "character";
    // no name set — exercises the placeholder
    store.objects = { arrakis, caladan, paul, nameless };

    // ── object_display_name ───────────────────────────────────────────────────
    check(object_display_name(arrakis) == "Arrakis", "display name reads name field");
    check(object_display_name(nameless) == "(unnamed)", "unnamed object → placeholder");

    // ── candidate assembly (the provider's job) ───────────────────────────────
    auto places = candidates_for(store, "place");
    check(places.size() == 2, "two place candidates");
    check(places[0].id == "obj_arrakis" && places[0].label == "Arrakis", "candidate 0 iid+label");
    check(places[1].label == "Caladan", "candidate 1 label");

    auto chars = candidates_for(store, "character");
    check(chars.size() == 2, "two character candidates");
    check(chars[1].label == "(unnamed)", "unnamed candidate carries placeholder");

    auto anything = candidates_for(store, "");   // empty target_type = any
    check(anything.size() == 4, "empty target_type yields all objects");

    // ── relation resolution: single, multi, orphan, empty ─────────────────────
    check(relation_label_for(places, "obj_caladan") == "Caladan", "single iid → label");
    check(relation_label_for(places, "obj_gone") == "obj_gone", "orphan iid → raw key");
    check(relation_label_for(places, "").empty(), "empty iid → empty");

    check(relation_summary(places, json("obj_arrakis")) == "Arrakis", "summary single");
    json multi = json::array({ "obj_arrakis", "obj_caladan" });
    check(relation_summary(places, multi) == "Arrakis · Caladan", "summary multi joined");
    json multi_orphan = json::array({ "obj_arrakis", "obj_gone" });
    check(relation_summary(places, multi_orphan) == "Arrakis · obj_gone", "summary multi keeps orphan");
    check(relation_summary(places, json::array()).empty(), "summary empty array → empty");

    // ── coercion: relation single/multi/clear + list ──────────────────────────
    FieldSchema rel_single; rel_single.id = "homeworld"; rel_single.type = FieldType::Relation;
    rel_single.config = json{ {"target_type","place"}, {"multi",false} };
    FieldSchema rel_multi; rel_multi.id = "allies"; rel_multi.type = FieldType::Relation;
    rel_multi.config = json{ {"target_type","character"}, {"multi",true} };
    FieldSchema lst; lst.id = "aliases"; lst.type = FieldType::List;

    check(coerce_field_value(rel_single, json("obj_arrakis")) == json("obj_arrakis"),
          "single relation coerces to iid string");
    check(coerce_field_value(rel_single, json("")) == json(std::string{}),
          "single relation clear → empty string");
    check(coerce_field_value(rel_single, json(42)) == json(std::string{}),
          "single relation rejects non-string");
    check(coerce_field_value(rel_multi, multi) == multi,
          "multi relation coerces to array");
    check(coerce_field_value(rel_multi, json("oops")).is_array(),
          "multi relation non-array → empty array");

    json aliases = json::array({ "Muad'Dib", "Usul" });
    check(coerce_field_value(lst, aliases) == aliases, "list coerces array through");
    check(coerce_field_value(lst, json("nope")).is_array(), "list non-array → empty array");

    // ── apply + Object round-trip through the store ───────────────────────────
    Object& mp = *store.find_object("obj_paul");
    apply_field(mp, rel_single, json("obj_caladan"));   // Paul's homeworld → Caladan
    apply_field(mp, rel_multi, json::array({ "obj_x" })); // Paul's allies → [nameless]
    apply_field(mp, lst, aliases);                       // Paul's aliases

    json blob = store.to_json();
    ObjectStore reloaded;
    reloaded.from_json(blob);
    const Object* rp = reloaded.find_object("obj_paul");
    check(rp != nullptr, "paul survives round-trip");
    check(rp && rp->value_or("homeworld", json()) == json("obj_caladan"),
          "single relation iid round-trips");
    check(rp && rp->value_or("allies", json()) == json::array({ "obj_x" }),
          "multi relation array round-trips");
    check(rp && rp->value_or("aliases", json()) == aliases,
          "list value round-trips verbatim");

    // outgoing_edges sees both relation fields once Paul's template has them
    Template ct = built_in_character_template();
    ct.fields.push_back(rel_single);
    ct.fields.push_back(rel_multi);
    auto edges = rp->outgoing_edges(ct);
    check(edges.size() == 2, "outgoing_edges picks up single + multi targets");

    // ── FormPlan: relation row is no longer read-only (picker exists) ──────────
    Template t; t.id = "character"; t.type_name = "Character";
    t.fields = { rel_single };
    Object o; o.type = "character";
    FormPlan p = plan_form(t, o);
    check(p.rows.size() == 1 && !p.rows[0].read_only, "relation row is editable in s37");

    std::cout << g_checks << " checks, " << g_failed << " failed\n";
    return g_failed == 0 ? 0 : 1;
}
