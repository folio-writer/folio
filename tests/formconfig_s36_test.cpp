// ─────────────────────────────────────────────────────────────────────────────
// Folio — formconfig_s36_test.cpp   (s36 — field config + configured-widget brain)
//
// Pure convergent-evidence test for the s36 config-key CONTRACT: the editors in
// TemplateEdit (set_number_range / add|rename|remove_option / add|set|remove_
// preset) and the read accessors in FormPlan (config_num / config_options /
// config_presets / option_label_for) write and read the SAME shape, and that
// shape round-trips verbatim through ObjectIO::template_to_json/from_json (it
// lands on disk, so the shape is load-bearing). GTK rendering is NOT exercised
// here — that ships uncompiled; this proves the data half before any widget.
//
// Sandbox (g++, vendored nlohmann):
/*
g++ -std=c++20 -Wall -Wextra -Werror -I ../include -I /home/claude/sbox formconfig_s36_test.cpp ../src/ObjectIO.cpp -o /tmp/formconfig_s36_test
/tmp/formconfig_s36_test
*/
// Fedora target (clang, system nlohmann):
/*
clang++ -std=c++20 -Wall -Wextra -Werror -I ../include formconfig_s36_test.cpp ../src/ObjectIO.cpp -o /tmp/formconfig_s36_test
/tmp/formconfig_s36_test
*/
// ─────────────────────────────────────────────────────────────────────────────
#include "Object.hpp"
#include "FormPlan.hpp"
#include "TemplateEdit.hpp"
#include "ObjectIO.hpp"

#include <cstdio>
#include <string>

using namespace Folio;

static int g_checks = 0;
static int g_fail   = 0;
static void check(bool ok, const char* what) {
    ++g_checks;
    if (!ok) { ++g_fail; std::printf("  FAIL: %s\n", what); }
}

// A small editable template with one of each config-bearing type, plus the
// floor buffer, mirroring what the builder produces after a clone.
static Template make_template() {
    Template t;
    t.id = "tpl_test"; t.type_name = "Test"; t.icon = "folio-character-symbolic";
    TemplateEdit::add_field(t, FieldType::Number,      "Age");
    TemplateEdit::add_field(t, FieldType::Slider,      "Tension");
    TemplateEdit::add_field(t, FieldType::Dropdown,    "Role");
    TemplateEdit::add_field(t, FieldType::MultiSelect, "Traits");
    TemplateEdit::add_field(t, FieldType::List,        "Aliases");
    TemplateEdit::ensure_floor_buffer(t);
    return t;
}

int main() {
    std::printf("formconfig_s36_test\n");

    // ── number / slider range ────────────────────────────────────────────────
    {
        Template t = make_template();
        check(TemplateEdit::set_number_range(t, "age", 0, 120, 1), "set_number_range age");
        check(TemplateEdit::set_number_range(t, "tension", 1, 10, 0.5), "set_number_range tension");
        const FieldSchema* age = t.find_field("age");
        const FieldSchema* ten = t.find_field("tension");
        check(age && config_num(age->config, "max", -1) == 120, "config_num max=120");
        check(age && config_num(age->config, "min", -1) == 0,   "config_num min=0");
        check(age && config_num(age->config, "step", -1) == 1,  "config_num step=1");
        check(ten && config_num(ten->config, "step", -1) == 0.5, "config_num step=0.5");
        // missing key → fallback
        check(config_num(json::object(), "max", 99) == 99, "config_num fallback");
        check(config_num(json::array(), "max", 7) == 7,    "config_num non-object fallback");
    }

    // ── dropdown options: mint stable ids, dedupe, rename label, remove ──────
    {
        Template t = make_template();
        std::string a = TemplateEdit::add_option(t, "role", "Protagonist");
        std::string b = TemplateEdit::add_option(t, "role", "Antagonist");
        std::string c = TemplateEdit::add_option(t, "role", "Protagonist"); // dup label
        check(a == "protagonist", "option id slugified");
        check(b == "antagonist",  "second option id");
        check(c == "protagonist_2", "dup label -> deduped id");

        const FieldSchema* role = t.find_field("role");
        auto opts = config_options(role->config);
        check(opts.size() == 3, "3 options present");
        check(option_label_for(role->config, "antagonist") == "Antagonist", "label_for id");

        // rename label keeps id stable (the value-stability guarantee)
        check(TemplateEdit::rename_option(t, "role", "antagonist", "Villain"), "rename_option");
        check(option_label_for(t.find_field("role")->config, "antagonist") == "Villain", "renamed label");

        // remove by id
        check(TemplateEdit::remove_option(t, "role", "protagonist_2"), "remove_option");
        check(config_options(t.find_field("role")->config).size() == 2, "2 options after remove");
        // removed/unknown id falls back to the raw id (orphan-and-keep)
        check(option_label_for(t.find_field("role")->config, "ghost") == "ghost", "unknown id -> raw");
    }

    // ── list presets ─────────────────────────────────────────────────────────
    {
        Template t = make_template();
        check(TemplateEdit::add_preset(t, "aliases", "The Shadow"), "add_preset 1");
        check(TemplateEdit::add_preset(t, "aliases", "Nightwalker"), "add_preset 2");
        auto pr = config_presets(t.find_field("aliases")->config);
        check(pr.size() == 2 && pr[0] == "The Shadow", "presets read back");
        check(TemplateEdit::set_preset(t, "aliases", 1, "The Ghost"), "set_preset");
        check(config_presets(t.find_field("aliases")->config)[1] == "The Ghost", "preset replaced");
        check(TemplateEdit::remove_preset(t, "aliases", 0), "remove_preset");
        check(config_presets(t.find_field("aliases")->config).size() == 1, "1 preset after remove");
        check(!TemplateEdit::remove_preset(t, "aliases", 9), "remove_preset oob -> false");
    }

    // ── retype clears config (a config shaped for the old type won't fit) ────
    {
        Template t = make_template();
        TemplateEdit::set_number_range(t, "age", 0, 9, 1);
        check(config_num(t.find_field("age")->config, "max", -1) == 9, "range set before retype");
        check(TemplateEdit::retype_field(t, "age", FieldType::Text), "retype age->text");
        check(t.find_field("age")->config.empty(), "config cleared on retype");
    }

    // ── ROUND-TRIP: configured template survives template_to_json/from_json ──
    {
        Template t = make_template();
        TemplateEdit::set_number_range(t, "age", 0, 120, 1);
        TemplateEdit::add_option(t, "role", "Protagonist");
        TemplateEdit::add_option(t, "role", "Antagonist");
        TemplateEdit::add_preset(t, "aliases", "The Shadow");

        json j = ObjectIO::template_to_json(t);
        Template back = ObjectIO::template_from_json(j);

        const FieldSchema* age  = back.find_field("age");
        const FieldSchema* role = back.find_field("role");
        const FieldSchema* al   = back.find_field("aliases");
        check(age && config_num(age->config, "max", -1) == 120, "round-trip number config");
        check(role && config_options(role->config).size() == 2, "round-trip options");
        check(role && option_label_for(role->config, "antagonist") == "Antagonist", "round-trip option label");
        check(al && config_presets(al->config).size() == 1, "round-trip presets");
        // verbatim: the two configs serialise identically
        check(ObjectIO::template_to_json(back) == j, "round-trip is verbatim");
    }

    // ── display string resolves dropdown/multiselect ids to labels ──────────
    {
        Template t = make_template();
        TemplateEdit::add_option(t, "role", "Protagonist");
        TemplateEdit::add_option(t, "role", "Antagonist");
        const FieldSchema* role = t.find_field("role");
        check(field_display_string(FieldType::Dropdown, json("antagonist"), role->config)
                  == "Antagonist", "display dropdown -> label");
        json multi = json::array({ "protagonist", "antagonist" });
        check(field_display_string(FieldType::MultiSelect, multi, role->config)
                  == "Protagonist · Antagonist", "display multiselect -> labels");
        // a non-config type ignores the config arg (defers to 2-arg)
        check(field_display_string(FieldType::Toggle, json(true), role->config) == "Yes",
              "display toggle unaffected by config arg");
    }

    // ── coercion of the new widget shapes (the write-back the form will emit) ─
    {
        Template t = make_template();
        const FieldSchema* age = t.find_field("age");
        const FieldSchema* tr  = t.find_field("traits");
        Object o; o.iid = "x"; instantiate_against(o, t);
        apply_field(o, *age, json(42.0));
        check(o.value_or("age", json(0)).get<double>() == 42.0, "number coerced+stored");
        apply_field(o, *tr, json::array({ "brave", "loyal" }));
        check(o.value_or("traits", json::array()).size() == 2, "multiselect array stored");
        // a number handed a string still lands as a number (tolerant coercion)
        apply_field(o, *age, json("7"));
        check(o.value_or("age", json(0)).get<double>() == 7.0, "number from string");
    }

    std::printf("%d checks, %d failed\n", g_checks, g_fail);
    return g_fail == 0 ? 0 : 1;
}
