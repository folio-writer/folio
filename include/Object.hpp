#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — Object.hpp   (s31 — objects & templates: the atoms; pure, GTK-free)
//
// The third instance of the principle the whole tool already runs on — module-
// templates for STRUCTURE, pattern-library for GENRE, and now form-templates for
// OBJECTS (characters, places, and whatever else a book needs). The object model
// is generic; the TEMPLATE is the variable. We are not inventing a subsystem; we
// are re-pointing one we built twice. See DESIGN_objects_and_templates.md.
//
// THE ATOMS (§2):
//   FieldType  — selects the widget + the value shape.
//   FieldSchema— {id, type, label, config}  (no value)  → lives in the Template.
//   Template   — an ORDERED list of FieldSchema + a type name + an icon.
//   Object     — {iid, type, values:map<field_id → value>} → the field-map is
//                where variability lives; the struct stays tiny.
//   (Edge      — a relation field's value IS a graph edge; no separate struct.)
//   (Group     — emergent: the set of objects whose relation points at one
//                target; a read over edges, never stored. See §2.5 / §5.)
//
// LOCKED DECISIONS honoured here (§11):
//   • One object model, many templates — never a fixed struct per type. The
//     field-map is generic JSON; the struct stays small.
//   • Relation is a first-class field type FROM DAY ONE — its value (iid or
//     [iid]) is just JSON, so the data layer carries it now even though the
//     picker UI lands later.
//   • Containment is a relation (config.target_type) — gives the timeline scale.
//   • The text buffer is the default FLOOR field on every template (richtext).
//   • Object types are an open, user-defined registry — Character/Place are the
//     first two of an open set, seeded as type-flavoured built-in defaults.
//   • Migration is reframe-not-replace: today's buffer = the simplest template
//     (§8). Orphan-and-keep (§12): a value whose field left the schema stays in
//     the map (a json object gives this for free) so a re-add restores it.
//
// PURE: <string>, <vector>, nlohmann::json only. No GTK, no GLib — so the model
// + its IO are g++-compilable and unit-checkable in the Claude sandbox, exactly
// like the Module/ModuleIO layer it mirrors.
// ─────────────────────────────────────────────────────────────────────────────

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Folio {

using json = nlohmann::json;

// ─────────────────────────────────────────────────────────────────────────────
// FieldType — each type is just a WIDGET + a VALUE SHAPE (§3). Every widget
// already exists in Folio; this is assembly, not invention. The value column is
// what the json carries:
//
//   type        widget (already shipped)        value (json)
//   ─────────── ─────────────────────────────── ───────────────
//   Text        Entry                           string
//   RichText    text buffer (the dissertation)  string (html)
//   Number      SpinButton                      number
//   Slider      Scale (the energy faders)       number
//   Toggle      class-driven toggle (the pin)   bool
//   Dropdown    DropDown (config.options)       string (option id)
//   MultiSelect DropDown / chips                array<string>
//   List        the KP-row pattern              array<string>
//   Image       existing image handler          string (path/blob ref)
//   Color       palette swatch                  number (palette index)
//   Date        entry / number                  string
//   Relation    object picker (config.target_type)  string OR array<string>  (iid[s])
//
// Relation is THE one that changes what this is: its value is a POINTER (iid),
// not a string — reverse edges fall out, the timeline lanes by reading edges,
// and renaming a target updates everywhere instead of drifting. Built first-
// class for that reason (§3, §11).
// ─────────────────────────────────────────────────────────────────────────────

enum class FieldType {
    Text,
    RichText,
    Number,
    Slider,
    Toggle,
    Dropdown,
    MultiSelect,
    List,
    Image,
    Color,
    Date,
    Relation,
    Heading,    // s39 — layout marker (a section divider); carries no value
    Unknown,    // fallback for forward-compat / malformed schemas
};

inline const char* field_type_to_str(FieldType t) {
    switch (t) {
        case FieldType::Text:        return "text";
        case FieldType::RichText:    return "richtext";
        case FieldType::Number:      return "number";
        case FieldType::Slider:      return "slider";
        case FieldType::Toggle:      return "toggle";
        case FieldType::Dropdown:    return "dropdown";
        case FieldType::MultiSelect: return "multiselect";
        case FieldType::List:        return "list";
        case FieldType::Image:       return "image";
        case FieldType::Color:       return "color";
        case FieldType::Date:        return "date";
        case FieldType::Relation:    return "relation";
        case FieldType::Heading:     return "heading";
        case FieldType::Unknown:     return "unknown";
    }
    return "unknown";
}

inline FieldType field_type_from_str(const std::string& s) {
    if (s == "text")        return FieldType::Text;
    if (s == "richtext")    return FieldType::RichText;
    if (s == "number")      return FieldType::Number;
    if (s == "slider")      return FieldType::Slider;
    if (s == "toggle")      return FieldType::Toggle;
    if (s == "dropdown")    return FieldType::Dropdown;
    if (s == "multiselect") return FieldType::MultiSelect;
    if (s == "list")        return FieldType::List;
    if (s == "image")       return FieldType::Image;
    if (s == "color")       return FieldType::Color;
    if (s == "date")        return FieldType::Date;
    if (s == "relation")    return FieldType::Relation;
    if (s == "heading")     return FieldType::Heading;
    return FieldType::Unknown;
}

// True for the relation family — the field types whose value is an iid (or list
// of iids) and therefore draws a graph edge (§2.4). Containment is a relation
// too, so this is the test a graph read uses to find edges.
inline bool field_type_is_relation(FieldType t) {
    return t == FieldType::Relation;
}

// The "zero" value a freshly-instantiated field carries, by type. Keeps the
// renderer simple (it can assume a value is present and well-shaped) and makes
// round-trips deterministic. Relation defaults to a single empty pointer; a
// many-target relation (config.multi == true) is rendered as a list but still
// defaults sensibly (empty string → "no target yet").
inline json field_default_value(FieldType t) {
    switch (t) {
        case FieldType::Number:
        case FieldType::Slider:
        case FieldType::Color:       return json(0);
        case FieldType::Toggle:      return json(false);
        case FieldType::MultiSelect:
        case FieldType::List:        return json::array();
        case FieldType::Text:
        case FieldType::RichText:
        case FieldType::Dropdown:
        case FieldType::Image:
        case FieldType::Date:
        case FieldType::Relation:
        case FieldType::Unknown:     return json("");
        case FieldType::Heading:     return json(nullptr);   // s39 — a marker, no value
    }
    return json("");
}

// ─────────────────────────────────────────────────────────────────────────────
// FieldSchema (the atom, §2.1) — {id, type, label, config}, NO value.
// Schema lives in the Template; the value lives in the Object, keyed by `id`.
// `id` is a stable machine key: never shown, never reused (renaming the LABEL
// leaves the id — and therefore every stored value — untouched).
// `config` is type-specific json: slider {min,max,step}, dropdown {options:[
// {id,label}]}, relation {target_type, multi}, etc. Empty object when unused.
// ─────────────────────────────────────────────────────────────────────────────
struct FieldSchema {
    std::string id;                     // stable machine key (never shown/reused)
    FieldType   type = FieldType::Text; // selects widget + value shape
    std::string label;                  // display name (free to rename)
    json        config = json::object();// type-specific options

    // Config conveniences (read-only; tolerant of missing keys). These keep the
    // renderer and the graph read from re-deriving config access everywhere.
    std::string relation_target_type() const {
        return config.is_object() ? config.value("target_type", std::string{})
                                   : std::string{};
    }
    bool relation_multi() const {
        return config.is_object() ? config.value("multi", false) : false;
    }
};

// Schema-aware default value: identical to the type-only default EXCEPT a multi-
// target relation (config.multi == true) defaults to an empty array, so the
// renderer (chips) and outgoing_edges read a uniformly-shaped value. The multi-
// ness lives in config, not the type, which is why this overload is needed.
inline json field_default_value(const FieldSchema& f) {
    if (f.type == FieldType::Relation && f.relation_multi())
        return json::array();
    return field_default_value(f.type);
}

// ─────────────────────────────────────────────────────────────────────────────
// Template (the molecule, §2.2) — a SCHEMA: ordered fields without values, plus
// a type name and an icon. The user builds this in the field-builder; it is
// saved to a library (the stamp) and copied into the bundle (the instance) —
// the same stamp/instance split proven on ModuleLibrary.
//
// `id` is the stable type key that Object.type references and that a relation
// field's config.target_type points at; built-ins use readable ids ("character",
// "place"), user-defined types mint a tpl_ iid. `type_name` is the display name
// (free to rename without breaking references). THE TEMPLATE IS THE NEED,
// captured as a schema — complexity opt-in, never imposed (§1, §11).
// ─────────────────────────────────────────────────────────────────────────────
struct Template {
    std::string              id;        // stable type key (Object.type / target_type)
    std::string              type_name; // "Character", "Species", "House", "Sector"…
    std::string              icon;      // folio-*-symbolic
    std::vector<FieldSchema> fields;    // ORDERED
    bool                     builtin = false;  // locked floor/seed type — clone to edit (s34)
    // s38 — which binder section this template stamps instances into: "character",
    // "place", or "reference". The create-from-template submenu filters by it; the
    // built-in floors set it. Empty on a legacy/untyped template (treated as
    // "character" by the floor fallback). Stored now; the filtered picker is a
    // later slice.
    std::string              category;

    const FieldSchema* find_field(const std::string& field_id) const {
        for (const auto& f : fields)
            if (f.id == field_id) return &f;
        return nullptr;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Object (the organism, §2.3) — a type plus values keyed to its template's
// schema. The struct stays tiny; the flexibility lives in `values`. ONE object
// model for character, planet, species, House — distinguished only by template.
//
// `values` is a json OBJECT (field_id → value). This is deliberate: it gives
// orphan-and-keep for free (§12) — a value whose field has left the schema simply
// stays in the map, unread by the renderer, restored the moment the field is
// re-added. A relation field's value here (an iid, or list of iids) IS a graph
// edge (§2.4); fill the form, draw the graph.
// ─────────────────────────────────────────────────────────────────────────────
struct Object {
    std::string iid;                    // stable id (graph edges resolve to this)
    std::string type;                   // which Template (its id) this instantiates
    json        values = json::object();// field_id → value (the field-map)

    // s35 — leaf-backed marker. True for objects PROJECTED from a Characters/
    // Places binder leaf (set by ObjectStore::add_migrated_leaf); false for store-
    // owned objects (a user-defined type with no leaf, future). The reconcile
    // prunes a projected object whose leaf has vanished but never touches a store-
    // owned one. Was inferred from o.type ∈ {character,place} until s35 — but a
    // leaf can now adopt a CLONE type (tpl_…), so the floor-type proxy broke and
    // the fact has to be carried explicitly. Serialised omit-when-false so it
    // survives a reload (a ghost whose leaf was deleted is still pruned next pass).
    bool        projected = false;

    // Read a value with a fallback (tolerant; the renderer can assume presence
    // only after instantiate() below, but reads stay safe regardless).
    json value_or(const std::string& field_id, const json& fallback) const {
        if (values.is_object()) {
            auto it = values.find(field_id);
            if (it != values.end()) return *it;
        }
        return fallback;
    }
    void set_value(const std::string& field_id, json v) {
        if (!values.is_object()) values = json::object();
        values[field_id] = std::move(v);
    }
    bool has_value(const std::string& field_id) const {
        return values.is_object() && values.contains(field_id);
    }

    // The set of (resolved) iid targets across ALL relation fields of `tmpl` —
    // the object's outgoing graph edges. Pure read over the field-map; the graph
    // layer (StoryGraph) and the timeline's group reads consume this. Skips
    // empty pointers. Works for both single (string) and multi (array) relations.
    std::vector<std::string> outgoing_edges(const Template& tmpl) const {
        std::vector<std::string> out;
        for (const auto& f : tmpl.fields) {
            if (!field_type_is_relation(f.type)) continue;
            if (!has_value(f.id)) continue;
            const json& v = values.at(f.id);
            if (v.is_string()) {
                if (!v.get<std::string>().empty()) out.push_back(v.get<std::string>());
            } else if (v.is_array()) {
                for (const auto& e : v)
                    if (e.is_string() && !e.get<std::string>().empty())
                        out.push_back(e.get<std::string>());
            }
        }
        return out;
    }
};

// The object's human label — the floor `name` field (every template carries it;
// migration maps the leaf title into it). Falls back to a parenthesised
// placeholder when unnamed, so a relation picker never shows a blank row and a
// freshly-created character still reads as something. Pure read over the value
// map; the relation candidate list and any read-only edge summary resolve through
// this so an iid always renders as a name, never a raw key. (s37)
inline std::string object_display_name(const Object& o) {
    json v = o.value_or("name", json(std::string{}));
    if (v.is_string() && !v.get<std::string>().empty()) return v.get<std::string>();
    return "(unnamed)";
}

// ─────────────────────────────────────────────────────────────────────────────
// Instantiate an Object against a Template: mint nothing here (caller owns the
// iid), but seed every schema field with its default value so the form renderer
// can assume a present, well-shaped value for each field. Values already in the
// object are preserved (orphan-and-keep, and re-instantiation is idempotent).
// ─────────────────────────────────────────────────────────────────────────────
inline void instantiate_against(Object& obj, const Template& tmpl) {
    if (!obj.values.is_object()) obj.values = json::object();
    obj.type = tmpl.id;
    for (const auto& f : tmpl.fields) {
        if (f.type == FieldType::Heading) continue;   // s39 — layout marker, no value
        if (!obj.has_value(f.id))
            obj.values[f.id] = field_default_value(f);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Built-in default templates (§8) — the FLOOR case. The notecard IS the three
// fields {name, image, description(richtext)}; description is the dissertation,
// the default long-form buffer every template carries at the bottom (§4).
//
// Character and Place share this simplest shape but are DISTINCT types (§5 — the
// first two of an open set), so they seed as two type-flavoured built-ins rather
// than collapsing into one anonymous type. (This resolves §12's first open fork
// — lean generic engine, seeded with type-flavoured defaults — because the
// binder sections and the timeline's lane-by-type both rely on the distinction.
// A genuine fork the doc flagged "decide at build"; overridable.)
// ─────────────────────────────────────────────────────────────────────────────

// The three floor fields, shared by both built-in defaults. `name`=text,
// `image`=image, `description`=richtext (the buffer). Ids are stable keys.
inline std::vector<FieldSchema> default_floor_fields() {
    return {
        { "name",        FieldType::Text,     "Name",        json::object() },
        { "image",       FieldType::Image,    "Image",       json::object() },
        { "description", FieldType::RichText, "Description", json::object() },
    };
}

inline Template built_in_character_template() {
    Template t;
    t.id        = "character";
    t.type_name = "Character";
    t.icon      = "folio-character-symbolic";
    t.fields    = default_floor_fields();
    t.builtin   = true;     // locked floor type — clone to customize (s34)
    t.category  = "character";
    return t;
}

inline Template built_in_place_template() {
    Template t;
    t.id        = "place";
    t.type_name = "Place";
    t.icon      = "folio-place-symbolic";
    t.fields    = default_floor_fields();
    t.builtin   = true;     // locked floor type — clone to customize (s34)
    t.category  = "place";
    return t;
}

}  // namespace Folio
