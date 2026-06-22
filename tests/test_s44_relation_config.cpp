// ─────────────────────────────────────────────────────────────────────────────
// Folio — test_s44_relation_config.cpp   (s44 brick 1 — "The Reference", cont.)
//
// Proves the LAST config-key family in templates: relation { target_type, multi }.
// The write helpers (TemplateEdit::set_relation_target_type / set_relation_multi)
// were the missing authoring half — the read accessors (FieldSchema::relation_*)
// and the instance picker (FormPlan / ObjectForm, s37) already shipped. This test
// closes the loop end to end, all pure (no GTK):
//
//   1. helpers write the right keys, tolerate a missing/garbage config, and are
//      INDEPENDENT (writing target_type never disturbs multi, and vice-versa);
//   2. the FieldSchema read accessors see what the helpers wrote;
//   3. the config survives an ObjectIO round-trip to/from disk verbatim;
//   4. THE RELIEF (§4): once a relation field is authored and an instance fills it,
//      the backlink is COMPUTED, not stored — ObjectStore::incoming_edges lifts the
//      reverse impression off the target node. This is the chicken-story acceptance
//      in miniature: author the edge as a plain field, read it backward for free.
// ─────────────────────────────────────────────────────────────────────────────
/*
g++  -std=c++20 -I../include -I/home/claude/sbox -o /tmp/t_s44 test_s44_relation_config.cpp ../src/ObjectIO.cpp ../src/ObjectStore.cpp ../src/Iid.cpp && /tmp/t_s44
clang++ -std=c++20 -I../include -I/usr/include -o /tmp/t_s44 test_s44_relation_config.cpp ../src/ObjectIO.cpp ../src/ObjectStore.cpp ../src/Iid.cpp && /tmp/t_s44
*/

#include "Object.hpp"
#include "ObjectIO.hpp"
#include "ObjectStore.hpp"
#include "TemplateEdit.hpp"
#include "FormPlan.hpp"

#include <iostream>
#include <string>

using namespace Folio;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const std::string& what) {
    if (ok) { ++g_pass; }
    else    { ++g_fail; std::cerr << "  FAIL: " << what << "\n"; }
}

int main() {
    // ── 1. Authoring a relation field via the pure brain ────────────────────────
    Template t;
    t.id        = "tpl_fragment";
    t.type_name = "Fragment";
    const std::string rel = TemplateEdit::add_field(t, FieldType::Relation, "Informs");
    check(rel == "informs", "relation field id minted from label");

    const FieldSchema* f0 = t.find_field(rel);
    check(f0 && f0->relation_target_type().empty(), "fresh relation has empty target_type");
    check(f0 && f0->relation_multi() == false,       "fresh relation defaults single");

    // ── 2. The two write helpers, on a still-empty config (tolerance) ───────────
    check(TemplateEdit::set_relation_target_type(t, rel, "character"),
          "set_relation_target_type returns true");
    check(t.find_field(rel)->relation_target_type() == "character",
          "target_type read-back == character");
    // multi untouched by the target write
    check(t.find_field(rel)->relation_multi() == false,
          "writing target_type left multi alone");

    check(TemplateEdit::set_relation_multi(t, rel, true), "set_relation_multi returns true");
    check(t.find_field(rel)->relation_multi() == true, "multi read-back == true");
    // target untouched by the multi write — INDEPENDENCE both directions
    check(t.find_field(rel)->relation_target_type() == "character",
          "writing multi left target_type alone");

    // Re-pointing the target keeps multi; flipping multi keeps target.
    TemplateEdit::set_relation_target_type(t, rel, "place");
    check(t.find_field(rel)->relation_target_type() == "place"
          && t.find_field(rel)->relation_multi() == true,
          "retarget preserves multi");
    TemplateEdit::set_relation_multi(t, rel, false);
    check(t.find_field(rel)->relation_target_type() == "place"
          && t.find_field(rel)->relation_multi() == false,
          "flip multi preserves target");

    // Unknown field id → false, no throw.
    check(!TemplateEdit::set_relation_target_type(t, "nope", "x"),
          "set on missing field returns false");
    check(!TemplateEdit::set_relation_multi(t, "nope", true),
          "multi on missing field returns false");

    // Garbage (non-object) config is rebuilt, not crashed into.
    {
        Template g;
        std::string gid = TemplateEdit::add_field(g, FieldType::Relation, "Link");
        TemplateEdit::find_field_mut(g, gid)->config = json("garbage-string");  // wrong-typed
        check(TemplateEdit::set_relation_target_type(g, gid, "character"),
              "tolerates non-object config (target)");
        check(g.find_field(gid)->relation_target_type() == "character",
              "non-object config rebuilt to a clean object");
    }

    // ── 3. Round-trips through ObjectIO verbatim (the config-key contract) ──────
    {
        TemplateEdit::set_relation_target_type(t, rel, "character");
        TemplateEdit::set_relation_multi(t, rel, true);
        json j = ObjectIO::template_to_json(t);
        Template back = ObjectIO::template_from_json(j);
        const FieldSchema* fb = back.find_field(rel);
        check(fb != nullptr, "relation field survives round-trip");
        check(fb && fb->type == FieldType::Relation, "type survives round-trip");
        check(fb && fb->relation_target_type() == "character", "target_type survives disk");
        check(fb && fb->relation_multi() == true,              "multi survives disk");
    }

    // ── 4. THE RELIEF — authored field → instance edge → computed backlink ──────
    // A "House" type with a single-relation "Seat" pointing at a Place, and a
    // Place type. Two House instances seated at one Place: the backlink off the
    // Place must list both Houses — derived by contact, never stored on the Place.
    {
        ObjectStore store;

        Template place;
        place.id = "place"; place.type_name = "Place";
        TemplateEdit::add_field(place, FieldType::Text, "Name");
        store.upsert_template(place);

        Template house;
        house.id = "house"; house.type_name = "House";
        TemplateEdit::add_field(house, FieldType::Text, "Name");
        const std::string seat = TemplateEdit::add_field(house, FieldType::Relation, "Seat");
        TemplateEdit::set_relation_target_type(house, seat, "place");
        TemplateEdit::set_relation_multi(house, seat, false);
        store.upsert_template(house);

        Object arrakis; arrakis.iid = "obj_arrakis"; arrakis.type = "place";
        arrakis.values["name"] = "Arrakis";
        store.objects.push_back(arrakis);

        Object atreides; atreides.iid = "obj_atreides"; atreides.type = "house";
        atreides.values["name"] = "Atreides";
        atreides.values[seat]   = "obj_arrakis";       // the authored edge, as a field value
        store.objects.push_back(atreides);

        Object harkonnen; harkonnen.iid = "obj_harkonnen"; harkonnen.type = "house";
        harkonnen.values["name"] = "Harkonnen";
        harkonnen.values[seat]   = "obj_arrakis";
        store.objects.push_back(harkonnen);

        // Forward (press): the House's outgoing edge resolves to the Place.
        auto out = atreides.outgoing_edges(house);
        check(out.size() == 1 && out[0] == "obj_arrakis",
              "forward edge: House.Seat -> Arrakis");

        // Backward (relief): stand on the Place, see every House attached to it —
        // computed, not stored.
        auto back = store.incoming_edges("obj_arrakis");
        check(back.size() == 2, "relief: Arrakis has two incoming Houses");
        bool saw_a = false, saw_h = false;
        for (const Object* o : back) {
            if (o->iid == "obj_atreides")  saw_a = true;
            if (o->iid == "obj_harkonnen") saw_h = true;
        }
        check(saw_a && saw_h, "relief lists both seated Houses");

        // Field-scoped group read (the timeline lane): grouped via the Seat field.
        auto grp = store.group_members("obj_arrakis", seat);
        check(grp.size() == 2, "group_members via Seat == two Houses");

        // ── The backlink provider's resolution (s44 relief surface): for each
        // incoming source, identify WHICH relation field points here (the "via"
        // label). This mirrors Editor's set_backlink_provider, proven pure here.
        struct BL { std::string src_iid, src_label, via_label; };
        std::vector<BL> rows;
        for (const Object* src : store.incoming_edges("obj_arrakis")) {
            const Template* st = store.find_template(src->type);
            if (!st) continue;
            const std::string label = object_display_name(*src);
            for (const auto& f : st->fields) {
                if (!field_type_is_relation(f.type)) continue;
                if (!src->has_value(f.id)) continue;
                const json& v = src->values.at(f.id);
                bool hit = false;
                if (v.is_string()) hit = (v.get<std::string>() == "obj_arrakis");
                else if (v.is_array())
                    for (const auto& e : v)
                        if (e.is_string() && e.get<std::string>() == "obj_arrakis") { hit = true; break; }
                if (hit) rows.push_back({ src->iid, label, f.label });
            }
        }
        check(rows.size() == 2, "relief provider: two backlink rows for Arrakis");
        bool via_ok = true;
        for (const auto& r : rows) if (r.via_label != "Seat") via_ok = false;
        check(via_ok, "relief provider: each row resolves via 'Seat'");
    }

    // ── 5. The category default (s44 §11) — is_default flag + resolution ────────
    {
        // is_default survives the disk round-trip.
        Template ut;
        ut.id = "tpl_henchman"; ut.type_name = "Henchman"; ut.category = "character";
        ut.is_default = true;
        json j = ObjectIO::template_to_json(ut);
        check(j.value("is_default", false) == true, "is_default written when true");
        check(ObjectIO::template_from_json(j).is_default == true, "is_default survives disk");
        // omitted when false (clean user types).
        Template plain; plain.id = "tpl_plain"; plain.type_name = "Plain";
        check(ObjectIO::template_to_json(plain).contains("is_default") == false,
              "is_default omitted when false");

        ObjectStore store;
        store.seed_builtins();   // character/place/reference floors, none flagged
        // No explicit default yet → the floor is the implicit category default.
        check(store.category_default_id("character") == "character",
              "implicit default == floor when none marked");
        check(store.category_default_id("nonesuch").empty(),
              "unknown category default == empty");

        // Mark a user Character template default → it wins over the floor.
        store.upsert_template(ut);   // is_default = true, category character
        check(store.category_default_id("character") == "tpl_henchman",
              "explicit user default wins over floor");
        // Other categories unaffected.
        check(store.category_default_id("place") == "place",
              "place still on its floor");

        // A second user default in the same category, then clear-siblings keeps one.
        Template ut2;
        ut2.id = "tpl_boss"; ut2.type_name = "Boss"; ut2.category = "character";
        ut2.is_default = true;
        store.upsert_template(ut2);
        int cleared = store.clear_category_defaults_except("character", "tpl_boss");
        check(cleared == 1, "clear_category_defaults_except cleared the other one");
        check(store.category_default_id("character") == "tpl_boss",
              "the kept template is the sole default");
        check(store.find_template("tpl_henchman")->is_default == false,
              "the sibling's flag was cleared");
    }

    std::cout << "test_s44_relation_config: " << g_pass << " passed, "
              << g_fail << " failed\n";
    return g_fail == 0 ? 0 : 1;
}
