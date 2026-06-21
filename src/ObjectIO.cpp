// ─────────────────────────────────────────────────────────────────────────────
// Folio — ObjectIO.cpp   (s31)   GTK-free / GLib-free. See ObjectIO.hpp.
//
// Pure round-trip + legacy migration for the objects & templates subsystem.
// Same shape as ModuleIO.cpp: small json⇄struct helpers, tolerant readers
// (missing fields take struct defaults; unknown fields ignored — except the
// Object value-map, which is preserved VERBATIM so orphan values survive).
// ─────────────────────────────────────────────────────────────────────────────
#include "ObjectIO.hpp"

namespace Folio {
namespace ObjectIO {

// ── Template (schema) ─────────────────────────────────────────────────────────
static json field_to_json(const FieldSchema& f) {
    json j{
        { "id",    f.id },
        { "type",  field_type_to_str(f.type) },
        { "label", f.label },
    };
    // Only emit config when it carries something — keeps the floor template's
    // JSON clean (the notecard's three fields have empty configs).
    if (f.config.is_object() && !f.config.empty())
        j["config"] = f.config;
    return j;
}

static FieldSchema field_from_json(const json& j) {
    FieldSchema f;
    f.id    = j.value("id", "");
    f.type  = field_type_from_str(j.value("type", "text"));
    f.label = j.value("label", "");
    if (j.contains("config") && j["config"].is_object())
        f.config = j["config"];
    else
        f.config = json::object();
    return f;
}

json template_to_json(const Template& t) {
    json fields = json::array();
    for (const auto& f : t.fields) fields.push_back(field_to_json(f));
    json j{
        { "schema",    1 },
        { "id",        t.id },
        { "type_name", t.type_name },
        { "icon",      t.icon },
        { "fields",    fields },
    };
    if (t.builtin) j["builtin"] = true;   // omit when false to keep user types clean
    if (!t.category.empty()) j["category"] = t.category;   // s38 — omit when unset
    return j;
}

Template template_from_json(const json& j) {
    Template t;
    t.id        = j.value("id", "");
    t.type_name = j.value("type_name", "");
    t.icon      = j.value("icon", "");
    t.builtin   = j.value("builtin", false);
    t.category  = j.value("category", "");   // s38 — "" on legacy templates
    if (j.contains("fields") && j["fields"].is_array())
        for (const auto& fj : j["fields"])
            t.fields.push_back(field_from_json(fj));
    return t;
}

std::string template_to_string(const Template& t, bool pretty) {
    return template_to_json(t).dump(pretty ? 2 : -1);
}
Template template_from_string(const std::string& text) {
    return template_from_json(json::parse(text));
}

// ── Object (instance) ─────────────────────────────────────────────────────────
json object_to_json(const Object& o) {
    // `values` is stored verbatim (orphan keys included) — see header.
    json j{
        { "schema", 1 },
        { "iid",    o.iid },
        { "type",   o.type },
        { "values", o.values.is_object() ? o.values : json::object() },
    };
    if (o.projected) j["projected"] = true;   // s35: omit when false (store-owned)
    return j;
}

Object object_from_json(const json& j) {
    Object o;
    o.iid       = j.value("iid", "");
    o.type      = j.value("type", "");
    o.projected = j.value("projected", false);   // s35: leaf-backed marker
    if (j.contains("values") && j["values"].is_object())
        o.values = j["values"];
    else
        o.values = json::object();
    return o;
}

std::string object_to_string(const Object& o, bool pretty) {
    return object_to_json(o).dump(pretty ? 2 : -1);
}
Object object_from_string(const std::string& text) {
    return object_from_json(json::parse(text));
}

// ── Migration (§8) ────────────────────────────────────────────────────────────
Object migrate_legacy_leaf(const std::string& iid,
                           const std::string& floor_type,
                           const std::string& title,
                           const std::string& buffer_html,
                           const std::string& image_path,
                           const std::string& legacy_tagline,
                           const std::string& legacy_role) {
    const Template tmpl = floor_type == "place"     ? built_in_place_template()
                        : floor_type == "reference" ? built_in_reference_template()
                                                    : built_in_character_template();
    Object o;
    o.iid  = iid;                       // same part — the iid travels forward
    instantiate_against(o, tmpl);       // seeds name/image/description defaults

    // §8 mapping: title → name, buffer → description, image_path → image.
    o.set_value("name",        title);
    o.set_value("description", buffer_html);
    o.set_value("image",       image_path);

    // Nothing lost: preserve the legacy one-liner and role as ORPHAN values so a
    // template that later surfaces them restores them (orphan-and-keep, §12).
    if (!legacy_tagline.empty()) o.set_value("tagline", legacy_tagline);
    if (!legacy_role.empty())    o.set_value("role",    legacy_role);

    return o;
}

}  // namespace ObjectIO
}  // namespace Folio
