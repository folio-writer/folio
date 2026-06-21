#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — TemplateEdit.hpp   (s33 — the template builder's BRAIN; pure, GTK-free)
//
// §7 of the objects design: the schema editor — "add field → pick type → set
// label + config → order them → name the type + icon → save." The only novel
// logic in that is the SCHEMA MUTATION + its invariants; it lives here, pure and
// tested, so the GTK TemplateBuilderDialog is a thin mechanical surface over it —
// the same brain/hands split as FormPlan / ObjectStore's reads.
//
// INVARIANTS this module guards (so the renderer, the round-trip, and orphan-and-
// keep can rely on them):
//   • Field ids are STABLE machine keys: minted once from the label, never shown,
//     never reused. Renaming a label never touches the id (and therefore never
//     touches a stored value). add_field mints a unique id; nothing else mints.
//   • Field ids are unique and non-empty within a template.
//   • The richtext BUFFER is the floor and stays at the BOTTOM (§4 — the
//     dissertation every template carries). A new structured field lands ABOVE a
//     trailing buffer; reordering can't push a field below it.
//   • A type_name is required to be a valid, savable template.
//
// PURE: Object.hpp + <string>/<vector>/<cctype>. g++-compilable / sandbox-tested.
// ─────────────────────────────────────────────────────────────────────────────

#include "Object.hpp"
#include <cctype>
#include <string>
#include <vector>

namespace Folio {
namespace TemplateEdit {

// A trailing richtext field is treated as the floor buffer (§4). "Is the buffer
// at index i" == it is a RichText field AND it is the last field.
inline bool is_trailing_buffer(const Template& t, std::size_t i) {
    return i + 1 == t.fields.size() && t.fields[i].type == FieldType::RichText;
}
inline bool has_trailing_buffer(const Template& t) {
    return !t.fields.empty() && t.fields.back().type == FieldType::RichText;
}

// Slugify a label into a stable machine id: lowercase, non-alphanumeric → '_',
// collapsed and trimmed; empty → "field". This is the RAW id; mint_field_id
// dedupes it against the template.
inline std::string slugify(const std::string& label) {
    std::string s;
    bool last_us = false;
    for (unsigned char c : label) {
        if (std::isalnum(c)) { s += static_cast<char>(std::tolower(c)); last_us = false; }
        else if (!last_us && !s.empty()) { s += '_'; last_us = true; }
    }
    while (!s.empty() && s.back() == '_') s.pop_back();   // trim trailing
    if (s.empty()) s = "field";
    return s;
}

// Mint a unique field id from a label, not colliding with any existing field id
// in `fields`. Appends _2, _3, … on collision. Pure; the only place ids are born.
inline std::string mint_field_id(const std::vector<FieldSchema>& fields,
                                  const std::string& label) {
    auto taken = [&](const std::string& id) {
        for (const auto& f : fields) if (f.id == id) return true;
        return false;
    };
    std::string base = slugify(label);
    if (!taken(base)) return base;
    for (int n = 2; ; ++n) {
        std::string cand = base + "_" + std::to_string(n);
        if (!taken(cand)) return cand;
    }
}

// Add a field: mint its id from `label`, then insert keeping the floor buffer at
// the bottom (§4) — a non-richtext field lands ABOVE a trailing buffer; a
// richtext field appends (becoming/extending the buffer region). Returns the
// new field's stable id.
inline std::string add_field(Template& t, FieldType type,
                             const std::string& label, json config = json::object()) {
    FieldSchema f;
    f.id     = mint_field_id(t.fields, label);
    f.type   = type;
    f.label  = label;
    f.config = config.is_object() ? config : json::object();

    const std::string new_id = f.id;   // capture before the move
    if (type != FieldType::RichText && has_trailing_buffer(t))
        t.fields.insert(t.fields.end() - 1, std::move(f));   // above the buffer
    else
        t.fields.push_back(std::move(f));
    return new_id;
}

inline int field_index(const Template& t, const std::string& field_id) {
    for (std::size_t i = 0; i < t.fields.size(); ++i)
        if (t.fields[i].id == field_id) return static_cast<int>(i);
    return -1;
}

inline bool remove_field(Template& t, const std::string& field_id) {
    int i = field_index(t, field_id);
    if (i < 0) return false;
    t.fields.erase(t.fields.begin() + i);
    return true;
}

// Move a field up (delta<0) or down (delta>0) by |delta| slots, clamped to the
// valid range. A field can NOT be moved below a trailing floor buffer, and the
// buffer itself can NOT be moved up out of the last slot (§4). Returns true if
// the field moved.
inline bool move_field(Template& t, const std::string& field_id, int delta) {
    int i = field_index(t, field_id);
    if (i < 0 || delta == 0) return false;
    const int n = static_cast<int>(t.fields.size());

    // The trailing buffer is pinned last.
    if (is_trailing_buffer(t, static_cast<std::size_t>(i))) return false;

    int j = i + delta;
    int lo = 0;
    int hi = n - 1;
    if (has_trailing_buffer(t) && t.fields[i].type != FieldType::RichText)
        hi = n - 2;                                  // can't pass below the buffer
    if (j < lo) j = lo;
    if (j > hi) j = hi;
    if (j == i) return false;

    FieldSchema moved = t.fields[i];
    t.fields.erase(t.fields.begin() + i);
    t.fields.insert(t.fields.begin() + j, std::move(moved));
    return true;
}

// Rename a field's LABEL only — the id (and every stored value keyed by it) is
// untouched. Returns true if the field exists.
inline bool rename_field(Template& t, const std::string& field_id,
                         const std::string& new_label) {
    int i = field_index(t, field_id);
    if (i < 0) return false;
    t.fields[static_cast<std::size_t>(i)].label = new_label;
    return true;
}

// Change a field's TYPE (id/label kept). Config is reset to empty, since a
// config shaped for the old type rarely fits the new one; the builder re-sets it.
inline bool retype_field(Template& t, const std::string& field_id, FieldType new_type) {
    int i = field_index(t, field_id);
    if (i < 0) return false;
    t.fields[static_cast<std::size_t>(i)].type   = new_type;
    t.fields[static_cast<std::size_t>(i)].config = json::object();
    return true;
}

inline bool set_field_config(Template& t, const std::string& field_id, json config) {
    int i = field_index(t, field_id);
    if (i < 0) return false;
    t.fields[static_cast<std::size_t>(i)].config = config.is_object() ? config : json::object();
    return true;
}

// ── Per-type CONFIG editing (s36) ────────────────────────────────────────────
// The builder edits a field's `config` through these; FormPlan's accessors read
// the SAME keys on the renderer side. THE CONFIG-KEY CONTRACT (round-trips to
// disk verbatim via ObjectIO, so the shape is load-bearing — both halves agree
// here, and a key rename is a migration):
//
//   number / slider   { "min": num, "max": num, "step": num }
//   dropdown / multiselect  { "options": [ { "id": str, "label": str }, … ] }
//   list              { "presets": [ str, … ] }                  (quick-add seeds)
//   relation          { "target_type": str, "multi": bool }      (edited at step 4)
//
// An OPTION id is a stable machine key minted from its label (deduped within the
// field's options), exactly like a FIELD id — renaming a choice's label never
// moves a stored value that points at it. Helpers are tolerant of a missing /
// wrong-typed config (they (re)create the object/array as needed) and pure, so
// the sandbox test can prove the shape before any GTK is cut.

inline FieldSchema* find_field_mut(Template& t, const std::string& field_id) {
    int i = field_index(t, field_id);
    return (i < 0) ? nullptr : &t.fields[static_cast<std::size_t>(i)];
}

// number / slider RANGE — writes all three keys in one settle.
inline bool set_number_range(Template& t, const std::string& field_id,
                             double min, double max, double step) {
    FieldSchema* f = find_field_mut(t, field_id);
    if (!f) return false;
    if (!f->config.is_object()) f->config = json::object();
    f->config["min"]  = min;
    f->config["max"]  = max;
    f->config["step"] = step;
    return true;
}

// dropdown / multiselect OPTIONS. add_option mints + appends, returning the new
// stable id; rename touches the label only (id stable); remove drops by id.
inline std::string add_option(Template& t, const std::string& field_id,
                              const std::string& label) {
    FieldSchema* f = find_field_mut(t, field_id);
    if (!f) return {};
    if (!f->config.is_object()) f->config = json::object();
    if (!f->config.contains("options") || !f->config["options"].is_array())
        f->config["options"] = json::array();
    json& opts = f->config["options"];
    auto taken = [&](const std::string& id) {
        for (const auto& o : opts)
            if (o.is_object() && o.value("id", std::string{}) == id) return true;
        return false;
    };
    std::string base = slugify(label.empty() ? std::string("option") : label);
    std::string id = base;
    for (int n = 2; taken(id); ++n) id = base + "_" + std::to_string(n);
    opts.push_back(json{ { "id", id }, { "label", label } });
    return id;
}
inline bool rename_option(Template& t, const std::string& field_id,
                          const std::string& opt_id, const std::string& new_label) {
    FieldSchema* f = find_field_mut(t, field_id);
    if (!f || !f->config.is_object() || !f->config.contains("options")
          || !f->config["options"].is_array()) return false;
    for (auto& o : f->config["options"])
        if (o.is_object() && o.value("id", std::string{}) == opt_id) {
            o["label"] = new_label;
            return true;
        }
    return false;
}
inline bool remove_option(Template& t, const std::string& field_id,
                          const std::string& opt_id) {
    FieldSchema* f = find_field_mut(t, field_id);
    if (!f || !f->config.is_object() || !f->config.contains("options")
          || !f->config["options"].is_array()) return false;
    json& opts = f->config["options"];
    for (std::size_t k = 0; k < opts.size(); ++k)
        if (opts[k].is_object() && opts[k].value("id", std::string{}) == opt_id) {
            opts.erase(opts.begin() + k);
            return true;
        }
    return false;
}

// list PRESETS — bare strings. add appends, set replaces by index, remove drops.
inline bool add_preset(Template& t, const std::string& field_id,
                       const std::string& value) {
    FieldSchema* f = find_field_mut(t, field_id);
    if (!f) return false;
    if (!f->config.is_object()) f->config = json::object();
    if (!f->config.contains("presets") || !f->config["presets"].is_array())
        f->config["presets"] = json::array();
    f->config["presets"].push_back(value);
    return true;
}
inline bool set_preset(Template& t, const std::string& field_id,
                       std::size_t index, const std::string& value) {
    FieldSchema* f = find_field_mut(t, field_id);
    if (!f || !f->config.is_object() || !f->config.contains("presets")
          || !f->config["presets"].is_array()
          || index >= f->config["presets"].size()) return false;
    f->config["presets"][index] = value;
    return true;
}
inline bool remove_preset(Template& t, const std::string& field_id,
                          std::size_t index) {
    FieldSchema* f = find_field_mut(t, field_id);
    if (!f || !f->config.is_object() || !f->config.contains("presets")
          || !f->config["presets"].is_array()
          || index >= f->config["presets"].size()) return false;
    f->config["presets"].erase(f->config["presets"].begin() + index);
    return true;
}

// Guarantee the floor buffer (§4): a trailing richtext "description" field.
// Idempotent — only appends one if the template has no trailing buffer. Uses the
// stable id "description" when free (matches the built-in floor + migration), so
// a migrated object's buffer value lands in it; else mints a unique id.
inline void ensure_floor_buffer(Template& t) {
    if (has_trailing_buffer(t)) return;
    FieldSchema f;
    f.id    = (field_index(t, "description") < 0) ? "description"
                                                   : mint_field_id(t.fields, "Description");
    f.type  = FieldType::RichText;
    f.label = "Description";
    t.fields.push_back(std::move(f));
}

// Trim helper for validation.
inline std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

// Validate a template for saving. Returns "" if valid, else a human message
// describing the FIRST problem found. Checks: a non-empty type_name; every field
// id non-empty and unique. (The buffer is ensured by ensure_floor_buffer, not
// required here — a structured-only type is legal, just unusual.)
inline std::string validate(const Template& t) {
    if (trim(t.type_name).empty())
        return "Type name is required.";
    std::vector<std::string> seen;
    for (const auto& f : t.fields) {
        if (trim(f.id).empty())
            return "A field is missing its id.";
        for (const auto& s : seen)
            if (s == f.id)
                return "Duplicate field id: " + f.id;
        seen.push_back(f.id);
    }
    return "";
}

}  // namespace TemplateEdit
}  // namespace Folio
