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
    bool        read_only  = false;   // relation is read-only THIS slice (no picker yet)
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
        || t == FieldType::MultiSelect;
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
        r.read_only  = field_type_is_relation(f.type);   // picker is a later slice
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
    }
    return json(raw.is_string() ? raw.get<std::string>() : std::string{});
}

// Coerce + store in one step. The renderer's change handler calls this with the
// field schema and the raw widget value; the object is updated in place.
inline void apply_field(Object& o, const FieldSchema& f, const json& raw) {
    o.set_value(f.id, coerce_field_value(f, raw));
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
    }
    return v.is_string() ? v.get<std::string>() : std::string{};
}

}  // namespace Folio
