#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — FormPlan.hpp   (s31 — the form renderer's BRAIN; pure, GTK-free)
//
// §6 of the objects design: "walk the template's ordered fields, switch on type,
// emit the matching widget, bind it to the object's value." That walk + the
// value coercion is the only novel logic; it lives here, pure and tested, so the
// GTK ObjectForm is a thin mechanical renderer over a FormPlan — the same
// brain/hands split as Iid / ModuleIO / StoryGraph.
//
// A FormPlan is an ordered list of FormRow — one per SCHEMA field (orphan values
// in the object, whose field has left the schema, are deliberately NOT planned:
// orphan-and-keep means retained-but-hidden, §12). Each row carries everything
// the renderer needs: the stable id (for write-back), the label, the type (which
// widget), the config (widget options), the current value (already defaulted to
// the field's zero if the object lacks it), and a layout hint (full-width fields
// like richtext/list get their own block; the rest are compact label/value rows).
//
// Write-back is the other half: apply_field() coerces a raw widget input to the
// field's json shape and stores it — so the GTK handler just hands over the raw
// value and never has to know number-vs-string-vs-array shaping.
//
// PURE: Object.hpp + nlohmann::json. g++-compilable / sandbox-testable.
// ─────────────────────────────────────────────────────────────────────────────

#include "Object.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Folio {

using json = nlohmann::json;

// One planned field: schema + the object's current value, ready to render.
struct FormRow {
    std::string field_id;             // stable key — write-back target
    std::string label;                // display
    FieldType   type = FieldType::Text;
    json        config = json::object();
    json        value;                // current value (defaulted if absent)
    bool        full_width = false;   // richtext/list → own block; else label/row
    bool        read_only  = false;   // (relation became editable in s37 — picker lives in the form)
};

struct FormPlan {
    std::string type_name;            // the template's display type (panel heading)
    std::string icon;                 // folio-*-symbolic
    std::vector<FormRow> rows;        // ordered as the template orders its fields
};

// Layout hint: the field types that want a full-width block rather than a compact
// label/value row. The richtext buffer (the dissertation floor) and list/
// multiselect chips need room; everything else is a tidy right-aligned row.
inline bool field_is_full_width(FieldType t) {
    return t == FieldType::RichText
        || t == FieldType::List
        || t == FieldType::MultiSelect
        || t == FieldType::Heading;   // s39 — a section header spans the row
}

// Build the plan: walk the template's ORDERED fields, resolve each against the
// object's value-map (defaulting to the field's zero when absent), tag layout +
// read-only. The object's `type` need not match `t.id` (the caller pairs them);
// orphan values are not planned. Pure — no mutation of the object.
inline FormPlan plan_form(const Template& t, const Object& o) {
    FormPlan p;
    p.type_name = t.type_name;
    p.icon      = t.icon;
    for (const auto& f : t.fields) {
        FormRow r;
        r.field_id   = f.id;
        r.label      = f.label;
        r.type       = f.type;
        r.config     = f.config;
        r.value      = o.value_or(f.id, field_default_value(f));
        r.full_width = field_is_full_width(f.type);
        // Relation is editable from s37 (the form renders a picker over the
        // store's candidates); it carries no special read-only flag now.
        r.read_only  = false;
        p.rows.push_back(std::move(r));
    }
    return p;
}

// ── Write-back: coerce a raw widget input to the field's json shape ───────────
// The GTK handler hands over what the widget produced; this stores it correctly-
// typed so a round-trip stays stable. Number/Slider/Color → number; Toggle →
// bool; List/MultiSelect → array<string>; everything else → string. Relation is
// read-only this slice, but the coercion is defined (single → string iid, multi
// → array) so wiring the picker later is a one-line change here.
inline json coerce_field_value(const FieldSchema& f, const json& raw) {
    switch (f.type) {
        case FieldType::Number:
        case FieldType::Slider:
        case FieldType::Color:
            if (raw.is_number()) return raw;
            if (raw.is_string()) { try { return json(std::stod(raw.get<std::string>())); } catch (...) { return json(0); } }
            return json(0);
        case FieldType::Toggle:
            return json(raw.is_boolean() ? raw.get<bool>() : false);
        case FieldType::List:
        case FieldType::MultiSelect:
            return raw.is_array() ? raw : json::array();
        case FieldType::Relation:
            if (f.relation_multi()) return raw.is_array() ? raw : json::array();
            return json(raw.is_string() ? raw.get<std::string>() : std::string{});
        case FieldType::Text:
        case FieldType::RichText:
        case FieldType::Dropdown:
        case FieldType::Image:
        case FieldType::Date:
        case FieldType::Unknown:
            return json(raw.is_string() ? raw.get<std::string>() : std::string{});
        case FieldType::Heading:
            return json(nullptr);   // s39 — a marker is never written
    }
    return json(raw.is_string() ? raw.get<std::string>() : std::string{});
}

// Coerce + store in one step. The renderer's change handler calls this with the
// field schema and the raw widget value; the object is updated in place.
inline void apply_field(Object& o, const FieldSchema& f, const json& raw) {
    o.set_value(f.id, coerce_field_value(f, raw));
}

// ── Config accessors (s36) — the renderer's read side of the config-key ───────
// contract that TemplateEdit's editors write (min/max/step, options, presets).
// Tolerant of a missing/wrong-typed config (return the supplied fallback / an
// empty list), so the renderer can route on them without re-checking shapes.
struct FieldChoice { std::string id; std::string label; };

inline double config_num(const json& c, const char* key, double fallback) {
    if (c.is_object()) {
        auto it = c.find(key);
        if (it != c.end() && it->is_number()) return it->get<double>();
    }
    return fallback;
}

// dropdown / multiselect choices. {id,label} objects; a bare string is tolerated
// (id == label). Options with an empty id are skipped (no stable key to store).
inline std::vector<FieldChoice> config_options(const json& c) {
    std::vector<FieldChoice> out;
    if (c.is_object()) {
        auto it = c.find("options");
        if (it != c.end() && it->is_array()) {
            for (const auto& o : *it) {
                if (o.is_object()) {
                    FieldChoice fc;
                    fc.id    = o.value("id", std::string{});
                    fc.label = o.value("label", fc.id);
                    if (!fc.id.empty()) out.push_back(std::move(fc));
                } else if (o.is_string()) {
                    out.push_back({ o.get<std::string>(), o.get<std::string>() });
                }
            }
        }
    }
    return out;
}

inline std::vector<std::string> config_presets(const json& c) {
    std::vector<std::string> out;
    if (c.is_object()) {
        auto it = c.find("presets");
        if (it != c.end() && it->is_array())
            for (const auto& p : *it)
                if (p.is_string()) out.push_back(p.get<std::string>());
    }
    return out;
}

// Resolve an option id to its label (falls back to the raw id if the option was
// removed — orphan-and-keep at the option level, so a stored value still reads).
inline std::string option_label_for(const json& config, const std::string& id) {
    for (const auto& o : config_options(config))
        if (o.id == id) return o.label;
    return id;
}

// ── Relation resolution (s37) — iid(s) → human label(s) over a candidate set ──
// The picker's candidates are FieldChoice{iid,label}; this turns a stored
// relation value (a single iid string or an array of iids) into the display text
// for the read-only summary and the picker's current-selection line. A target
// whose object was deleted is not in the candidate set, so it falls back to its
// raw iid (orphan-and-keep at the edge level — the edge still reads, repairs when
// the object returns). Empty value → empty string (the renderer shows a
// placeholder). Pure; tested alongside the candidate assembly.
inline std::string relation_label_for(const std::vector<FieldChoice>& candidates,
                                      const std::string& iid) {
    if (iid.empty()) return {};
    for (const auto& c : candidates)
        if (c.id == iid) return c.label;
    return iid;   // orphan: object gone, show the raw key rather than nothing
}

inline std::string relation_summary(const std::vector<FieldChoice>& candidates,
                                    const json& value) {
    if (value.is_string()) return relation_label_for(candidates, value.get<std::string>());
    if (value.is_array()) {
        std::string s;
        for (const auto& e : value) {
            if (!e.is_string()) continue;
            std::string lbl = relation_label_for(candidates, e.get<std::string>());
            if (lbl.empty()) continue;
            if (!s.empty()) s += " · ";
            s += lbl;
        }
        return s;
    }
    return {};
}

// ── Display string: a value → human text (read-only render + previews) ────────
// Pushes the last bit of shaping into the tested pure layer so the GTK renderer
// is near-trivial label-setting. Number/Slider/Color → trimmed number; Toggle →
// Yes/No; List/MultiSelect → " · "-joined; everything else → the string (empty
// when unset, so the renderer can show a placeholder).
inline std::string field_display_string(FieldType t, const json& v) {
    switch (t) {
        case FieldType::Toggle:
            return (v.is_boolean() && v.get<bool>()) ? "Yes" : "No";
        case FieldType::Number:
        case FieldType::Slider:
        case FieldType::Color: {
            if (!v.is_number()) return "0";
            double d = v.get<double>();
            if (d == static_cast<double>(static_cast<long long>(d)))
                return std::to_string(static_cast<long long>(d));
            std::string s = std::to_string(d);
            // trim trailing zeros / dot for a clean readout
            while (!s.empty() && s.back() == '0') s.pop_back();
            if (!s.empty() && s.back() == '.') s.pop_back();
            return s;
        }
        case FieldType::List:
        case FieldType::MultiSelect: {
            if (!v.is_array()) return "";
            std::string s;
            for (const auto& e : v) {
                if (!e.is_string()) continue;
                if (!s.empty()) s += " · ";
                s += e.get<std::string>();
            }
            return s;
        }
        case FieldType::Text:
        case FieldType::RichText:
        case FieldType::Dropdown:
        case FieldType::Image:
        case FieldType::Date:
        case FieldType::Relation:
        case FieldType::Unknown:
            return v.is_string() ? v.get<std::string>() : std::string{};
        case FieldType::Heading:
            return std::string{};   // s39 — no value text; the label renders as a header
    }
    return v.is_string() ? v.get<std::string>() : std::string{};
}

// Config-aware overload (s36): dropdown / multiselect resolve their stored id(s)
// to the option LABEL(s) via config; every other type defers to the 2-arg form.
// Used for the read-only render of a configured field so a dropdown shows
// "Antagonist", not "antagonist" (and a removed option still shows its raw id).
inline std::string field_display_string(FieldType t, const json& v, const json& config) {
    if (t == FieldType::Dropdown)
        return v.is_string() ? option_label_for(config, v.get<std::string>())
                             : std::string{};
    if (t == FieldType::MultiSelect) {
        if (!v.is_array()) return {};
        std::string s;
        for (const auto& e : v) {
            if (!e.is_string()) continue;
            if (!s.empty()) s += " · ";
            s += option_label_for(config, e.get<std::string>());
        }
        return s;
    }
    return field_display_string(t, v);
}

}  // namespace Folio
