// ─────────────────────────────────────────────────────────────────────────────
// Folio — ObjectStore.cpp   (s31)   Pure. GTK/GLib-free. See ObjectStore.hpp.
// ─────────────────────────────────────────────────────────────────────────────
#include "ObjectStore.hpp"
#include "ObjectIO.hpp"

namespace Folio {

std::string ObjectStore::add_migrated_leaf(const std::string& iid,
                                           bool               is_place,
                                           const std::string& title,
                                           const std::string& buffer_html,
                                           const std::string& image_path,
                                           const std::string& legacy_tagline,
                                           const std::string& legacy_role) {
    Object o = ObjectIO::migrate_legacy_leaf(iid, is_place, title, buffer_html,
                                             image_path, legacy_tagline, legacy_role);
    // Idempotent re-projection: replace an existing object with the same iid.
    if (Object* existing = find_object(iid))
        *existing = std::move(o);
    else
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
