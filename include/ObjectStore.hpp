#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — ObjectStore.hpp   (s31 — the object registry + instances; pure)
//
// The canonical home for the objects & templates subsystem inside a project:
//   • templates — the TYPE REGISTRY (Character, Place, and any user-defined
//     type). Seeded with the two built-in floor templates. This is §5's
//     "user-extensible registry of object kinds" made real — Character and Place
//     are no longer hardcoded arrays but the first two entries of an open set.
//   • objects   — the INSTANCES, each keyed to a template by Object.type.
//
// It serialises to a single json sub-tree that travels INSIDE the project blob
// (top-level key "object_store"), so it rides the v5 bundle for free: explode()
// copies the whole blob and only rewrites the tree keys, so a non-tree key like
// this passes through untouched, and implode() hands it back verbatim.
//
// THE STAGING CONTRACT (reframe-not-replace, this slice):
//   Today the live Sidebar/Inspector still edit the character/place BinderNode
//   trees — the object FORM RENDERER does not exist yet, so objects cannot be
//   the editable surface. So for now the store is a PROJECTION rebuilt from the
//   binder leaves at save time (one source of truth — the BinderNodes — never
//   two drifting copies), serialised, and read back / migrated at load. It
//   proves the round-trip end to end. When the renderer lands, editing flips to
//   objects and the binder leaves retire; the store is already the durable form.
//
// PURE: Object.hpp + ObjectIO.hpp + nlohmann::json. No GTK, no GLib, no
// DocumentModel — so it is g++-compilable and unit-checkable in the sandbox. The
// binder-leaf WALK (which needs BinderNode) lives in DocumentModel, feeds this
// store its plain leaf data through add_migrated_leaf(); the store stays pure
// (UI/model reaches; the seam is fed — DESIGN §4.7a).
// ─────────────────────────────────────────────────────────────────────────────

#include "Object.hpp"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace Folio {

using json = nlohmann::json;

struct ObjectStore {
    std::vector<Template> templates;   // the type registry
    std::vector<Object>   objects;     // the instances

    // ── Registry lookups ──────────────────────────────────────────────────────
    const Template* find_template(const std::string& id) const {
        for (const auto& t : templates)
            if (t.id == id) return &t;
        return nullptr;
    }
    bool has_template(const std::string& id) const { return find_template(id) != nullptr; }

    Object* find_object(const std::string& iid) {
        for (auto& o : objects)
            if (o.iid == iid) return &o;
        return nullptr;
    }
    const Object* find_object(const std::string& iid) const {
        for (const auto& o : objects)
            if (o.iid == iid) return &o;
        return nullptr;
    }

    // ── Seeding ───────────────────────────────────────────────────────────────
    // Ensure the two built-in floor templates (Character, Place) are present.
    // Idempotent: only adds a template id the registry lacks, so a project that
    // already carries user-edited Character/Place templates is left untouched.
    void seed_builtins() {
        if (!has_template("character")) templates.push_back(built_in_character_template());
        if (!has_template("place"))     templates.push_back(built_in_place_template());
    }

    // ── Migration intake (the pure seam the binder walk feeds) ────────────────
    // Produce one Object from a legacy character/place leaf's plain fields and
    // add it (replacing any existing object with the same iid — re-projection is
    // idempotent). Returns the iid added. See ObjectIO::migrate_legacy_leaf.
    std::string add_migrated_leaf(const std::string& iid,
                                  bool               is_place,
                                  const std::string& title,
                                  const std::string& buffer_html,
                                  const std::string& image_path,
                                  const std::string& legacy_tagline = "",
                                  const std::string& legacy_role     = "");

    void clear_objects() { objects.clear(); }

    // ── Serialisation (the "object_store" blob sub-tree) ──────────────────────
    json to_json() const;
    void from_json(const json& j);

    bool empty() const { return templates.empty() && objects.empty(); }
};

}  // namespace Folio
