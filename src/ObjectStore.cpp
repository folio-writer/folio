// ─────────────────────────────────────────────────────────────────────────────
// Folio — ObjectStore.cpp   (s31)   Pure. GTK/GLib-free. See ObjectStore.hpp.
// ─────────────────────────────────────────────────────────────────────────────
#include "ObjectStore.hpp"
#include "ObjectIO.hpp"
#include "Iid.hpp"   // s34 — make_iid(IidKind::Template) for cloned type ids

namespace Folio {

// s34 — clone-to-edit: built-ins are locked, so authoring a custom type means
// copying an existing template into a fresh, editable user type. Mints a unique
// tpl_ id (loops on the astronomically-unlikely collision), copies schema + icon,
// names it "Copy of <name>", and clears the builtin flag.
std::string ObjectStore::clone_template(const std::string& src_id) {
    const Template* src = find_template(src_id);
    if (!src) return "";

    Template copy = *src;
    do { copy.id = make_iid(IidKind::Template); } while (has_template(copy.id));
    copy.type_name = "Copy of " + src->type_name;
    copy.builtin   = false;
    templates.push_back(copy);
    return copy.id;
}

std::string ObjectStore::add_migrated_leaf(const std::string& iid,
                                           bool               is_place,
                                           const std::string& title,
                                           const std::string& buffer_html,
                                           const std::string& image_path,
                                           const std::string& legacy_tagline,
                                           const std::string& legacy_role) {
    if (Object* existing = find_object(iid)) {
        // MERGE-PRESERVING: restamp ONLY the leaf-owned fields from the leaf;
        // every other value (custom fields, relation iids) is left untouched, so
        // it survives the projection rebuild. Floor + orphan fields are restamped
        // unconditionally (incl. clears) so the object tracks the leaf's truth.
        existing->type = is_place ? "place" : "character";
        if (!existing->values.is_object()) existing->values = json::object();
        existing->set_value("name",        title);
        existing->set_value("description", buffer_html);
        existing->set_value("image",       image_path);
        existing->set_value("tagline",     legacy_tagline);
        existing->set_value("role",        legacy_role);
        return iid;
    }
    // First sighting of this iid — create the object fresh from the leaf.
    Object o = ObjectIO::migrate_legacy_leaf(iid, is_place, title, buffer_html,
                                             image_path, legacy_tagline, legacy_role);
    objects.push_back(std::move(o));
    return iid;
}

json ObjectStore::to_json() const {
    json tarr = json::array();
    for (const auto& t : templates) tarr.push_back(ObjectIO::template_to_json(t));
    json oarr = json::array();
    for (const auto& o : objects)   oarr.push_back(ObjectIO::object_to_json(o));
    return json{
        { "schema",    1 },
        { "templates", tarr },
        { "objects",   oarr },
    };
}

void ObjectStore::from_json(const json& j) {
    templates.clear();
    objects.clear();
    if (j.contains("templates") && j["templates"].is_array())
        for (const auto& tj : j["templates"])
            templates.push_back(ObjectIO::template_from_json(tj));
    if (j.contains("objects") && j["objects"].is_array())
        for (const auto& oj : j["objects"])
            objects.push_back(ObjectIO::object_from_json(oj));
}

}  // namespace Folio
