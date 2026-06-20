// ─────────────────────────────────────────────────────────────────────────────
// Folio — relation_test.cpp   (s32 — the relation graph reads; the picker's brain)
//
// The relation field is first-class from day one (§3/§11): its value is an iid
// pointer, and a pointer IS a graph edge. This test pins the PURE reads the
// picker UI and the group/timeline reads will ride on, built before any picker
// widget exists (brain first, hands thin):
//   • objects_of_type   — the picker's candidate list for a target type.
//   • incoming_edges     — reverse edges ("Arrakis lists every character tied to it").
//   • group_members      — field-scoped reverse edge ("everyone whose house = Harkonnen").
// Single AND multi-target relations are exercised, plus the Dune proof from the
// design (§1/§9): characters -> Houses, so a House is a group by reverse edge.
//
// PURE: ObjectStore.hpp / Object.hpp / nlohmann::json only — sandbox-compilable;
// the reads are inline, so no .cpp link is needed.
// ─────────────────────────────────────────────────────────────────────────────
/*
g++ -std=c++20 -Wall -Wextra -Werror -I include -I /home/claude/sbox tests/relation_test.cpp -o /tmp/relation_test
/tmp/relation_test
clang++ -std=c++20 -Wall -Wextra -Werror -I include tests/relation_test.cpp -o /tmp/relation_test
/tmp/relation_test
*/

#include "ObjectStore.hpp"
#include <cassert>
#include <algorithm>
#include <iostream>
#include <string>

using namespace Folio;

static int checks = 0;
static void check(bool cond, const std::string& what) {
    ++checks;
    if (!cond) { std::cerr << "FAIL: " << what << "\n"; std::exit(1); }
}

// Does `v` contain an object with the given iid?
static bool has_iid(const std::vector<const Object*>& v, const std::string& iid) {
    return std::any_of(v.begin(), v.end(),
                       [&](const Object* o){ return o && o->iid == iid; });
}

int main() {
    ObjectStore store;

    // ── A Dune-shaped schema: House type + Character type with two relations ──
    Template house;
    house.id = "house"; house.type_name = "House"; house.icon = "folio-group-symbolic";
    house.fields = { { "name", FieldType::Text, "Name", json::object() } };

    Template character;
    character.id = "character"; character.type_name = "Character";
    character.icon = "folio-character-symbolic";
    FieldSchema f_name { "name", FieldType::Text, "Name", json::object() };
    FieldSchema f_house{ "house", FieldType::Relation, "House",
                         json{{"target_type","house"},{"multi",false}} };
    FieldSchema f_allies{ "allies", FieldType::Relation, "Allies",
                          json{{"target_type","character"},{"multi",true}} };
    character.fields = { f_name, f_house, f_allies };

    store.templates = { house, character };

    // ── Instances: two Houses, three characters pointing at them ──────────────
    auto mk = [&](const std::string& iid, const std::string& type, const json& vals){
        Object o; o.iid = iid; o.type = type; o.values = vals; store.objects.push_back(o);
    };
    mk("hse_atr", "house", json{{"name","Atreides"}});
    mk("hse_hark","house", json{{"name","Harkonnen"}});
    mk("chr_leto","character", json{{"name","Leto"},   {"house","hse_atr"},  {"allies", json::array()}});
    mk("chr_paul","character", json{{"name","Paul"},   {"house","hse_atr"},  {"allies", json{"chr_leto"}}});
    mk("chr_vlad","character", json{{"name","Vladimir"},{"house","hse_hark"},{"allies", json{"chr_paul","chr_leto"}}});

    // ── objects_of_type: the picker candidate list ────────────────────────────
    auto houses = store.objects_of_type("house");
    check(houses.size() == 2, "two house candidates");
    check(has_iid(houses, "hse_atr") && has_iid(houses, "hse_hark"), "both houses listed");

    auto chars = store.objects_of_type("character");
    check(chars.size() == 3, "three character candidates");

    auto all = store.objects_of_type("");          // empty target_type => any
    check(all.size() == 5, "empty type => all objects");

    check(store.objects_of_type("sector").empty(), "no candidates for absent type");

    // ── incoming_edges: reverse edges across ALL relation fields ──────────────
    // Atreides is pointed at by Leto.house and Paul.house (single relations).
    auto into_atr = store.incoming_edges("hse_atr");
    check(into_atr.size() == 2, "Atreides has two incoming (Leto, Paul)");
    check(has_iid(into_atr, "chr_leto") && has_iid(into_atr, "chr_paul"), "Atreides members correct");

    // Leto is pointed at by Paul.allies and Vlad.allies (MULTI relations) — and
    // by nobody's house. Reverse read must cross the multi-target array.
    auto into_leto = store.incoming_edges("chr_leto");
    check(into_leto.size() == 2, "Leto has two incoming via multi 'allies'");
    check(has_iid(into_leto, "chr_paul") && has_iid(into_leto, "chr_vlad"), "Leto ally-sources correct");

    check(store.incoming_edges("hse_hark").size() == 1, "Harkonnen has one incoming (Vlad)");
    check(store.incoming_edges("nobody").empty(), "no incoming for unknown target");
    check(store.incoming_edges("").empty(), "empty target => no incoming");

    // ── group_members: field-scoped reverse edge (the timeline's lane read) ───
    // "Everyone whose HOUSE is Atreides" — must NOT include allies edges.
    auto house_of_atr = store.group_members("hse_atr", "house");
    check(house_of_atr.size() == 2, "house-group Atreides = Leto + Paul");
    check(has_iid(house_of_atr, "chr_leto") && has_iid(house_of_atr, "chr_paul"), "house-group correct");

    // Scoping Leto by the 'house' field finds nobody (allies point at Leto, but
    // not via 'house') — proving the field scope actually narrows.
    check(store.group_members("chr_leto", "house").empty(), "no one houses Leto");

    // Scoping Leto by 'allies' finds Paul + Vlad (the multi array members).
    auto allies_of_leto = store.group_members("chr_leto", "allies");
    check(allies_of_leto.size() == 2, "allies-group of Leto = Paul + Vlad");

    // Empty via_field_id falls back to incoming_edges (any relation field).
    check(store.group_members("hse_atr", "").size() == store.incoming_edges("hse_atr").size(),
          "empty via == incoming_edges");

    // ── A character with a missing template contributes no edges ──────────────
    mk("orphan", "no_such_type", json{{"house","hse_atr"}});
    check(store.incoming_edges("hse_atr").size() == 2,
          "object with missing template is skipped in reverse read");

    std::cout << "relation_test: all " << checks << " checks passed\n";
    return 0;
}
