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
#include <algorithm>
#include <string>
#include <vector>

namespace Folio {

using json = nlohmann::json;

// The two built-in types that are PROJECTED from binder leaves (character/place
// trees). An object of one of these is backed by a leaf; an object of any other
// type is store-owned (a user-defined type, no leaf). The projection reconcile
// only ever touches projected-type objects — store-owned objects pass through.
inline bool is_projected_type(const std::string& type_id) {
    return type_id == "character" || type_id == "place";
}

// A template is editable iff it is NOT a locked built-in. The builder opens only
// on editable types; a built-in must be cloned first (s34).
inline bool template_is_editable(const Template& t) { return !t.builtin; }

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

    // ── Relation graph reads (s32 — the on-ramp for the picker & groups) ──────
    // These are the pure brain the relation picker (the thin GTK hands) renders
    // over, and the foundation §2.5/§9 name: a group is "the set of objects whose
    // relation points at the same target," and that is exactly a reverse-edge
    // read. Built here, pure and tested, before any picker UI exists — so the
    // widget is mechanical and the graph logic can be verified in the sandbox.

    // Candidates for a relation field whose config.target_type is `type_id`:
    // every object of that type, in store order (the picker's option list; the
    // stored value is each object's iid). An empty `type_id` means "any type" —
    // a relation that can point at anything — so all objects are candidates.
    std::vector<const Object*> objects_of_type(const std::string& type_id) const {
        std::vector<const Object*> out;
        for (const auto& o : objects)
            if (type_id.empty() || o.type == type_id)
                out.push_back(&o);
        return out;
    }

    // Reverse edges: every object that points AT `target_iid` through ANY of its
    // relation fields. "Arrakis lists every character tied to it." Resolves each
    // object's template (to know which fields are relations), then tests its
    // outgoing_edges — so it honours both single and multi-target relations and
    // skips objects whose template is missing (can't read edges without a schema).
    std::vector<const Object*> incoming_edges(const std::string& target_iid) const {
        std::vector<const Object*> out;
        if (target_iid.empty()) return out;
        for (const auto& o : objects) {
            const Template* t = find_template(o.type);
            if (!t) continue;
            for (const auto& e : o.outgoing_edges(*t))
                if (e == target_iid) { out.push_back(&o); break; }
        }
        return out;
    }

    // A group read (§2.5/§5): the objects grouped BY pointing at `target_iid`
    // through a relation field of a SPECIFIC `via_field_id` (e.g. every character
    // whose `house` field → the Harkonnen object). This is the narrower,
    // field-scoped sibling of incoming_edges — the timeline lanes by it. Empty
    // via_field_id falls back to "any relation field" (== incoming_edges).
    std::vector<const Object*> group_members(const std::string& target_iid,
                                             const std::string& via_field_id) const {
        if (via_field_id.empty()) return incoming_edges(target_iid);
        std::vector<const Object*> out;
        if (target_iid.empty()) return out;
        for (const auto& o : objects) {
            if (!o.has_value(via_field_id)) continue;
            const json& v = o.values.at(via_field_id);
            bool hit = false;
            if (v.is_string()) {
                hit = (v.get<std::string>() == target_iid);
            } else if (v.is_array()) {
                for (const auto& e : v)
                    if (e.is_string() && e.get<std::string>() == target_iid) { hit = true; break; }
            }
            if (hit) out.push_back(&o);
        }
        return out;
    }

    // Ensure the two built-in floor templates (Character, Place) are present AND
    // pristine. RESEEDS (overwrites) them from the code definition every pass, so
    // a built-in is always the locked, current floor shape — built-ins are
    // immutable (clone to edit, s34), so there is never user data in them to lose,
    // and a future floor-schema change migrates for free. User types (tpl_ ids)
    // are never touched.
    void seed_builtins() {
        upsert_template(built_in_character_template());
        upsert_template(built_in_place_template());
        upsert_template(built_in_reference_template());   // s42
    }

    // Install or update a template by id (the template builder's commit, s33).
    // Replaces an existing template with the same id in place (preserving its
    // registry position) or appends a new one. Pure; the GTK dialog edits a copy
    // and hands the finished schema here. Objects of this type keep their values
    // (orphan-and-keep handles any field the edit removed).
    void upsert_template(const Template& t) {
        for (auto& existing : templates)
            if (existing.id == t.id) { existing = t; return; }
        templates.push_back(t);
    }

    // Clone a template into a new, EDITABLE user type (s34). Built-ins are locked;
    // the only authoring path is clone-then-edit. Mints a fresh tpl_ id, copies
    // the schema + icon, names it "Copy of <name>", clears the builtin flag, and
    // registers it. Returns the new id, or "" if src_id is unknown.
    std::string clone_template(const std::string& src_id);

    // s38 — adopt a Template BINDER NODE's schema into the registry (the merge's
    // projection seam). The node is the truth; its `iid` becomes the template's
    // stable id, so a leaf's `template_id` and a relation `target_type` resolve to
    // it. Deserializes `form_schema`, stamps id = node_iid + builtin = false, and
    // upserts. A null/empty schema is ignored (no junk type). Returns true if a
    // template was adopted. Defined in ObjectStore.cpp (needs ObjectIO).
    bool adopt_template_node(const std::string& node_iid, const json& form_schema);

    // ── Type resolution (s35 — the leaf's template_id → object.type) ──────────
    // A character/place leaf carries an optional `template_id` naming a CLONE it
    // has adopted (empty = the section's built-in floor). Resolve it to the type
    // the projected object should carry, per the s35 truth table:
    //   empty                  -> floor (character / place)
    //   set, editable clone     -> the clone id
    //   set, missing (deleted)  -> floor   (orphan-and-keep: object retains its
    //                                        hidden custom values; falls back)
    //   set, a built-in id      -> floor   (built-ins are never a leaf's own type)
    //   set, a built-in id      -> floor   (built-ins are never a leaf's own type)
    // The floor is chosen by `floor_type` ("character"/"place"/"reference"), so a
    // stale cross-section id still lands on the right floor. seed_builtins() runs
    // before the reconcile, so the floor template always resolves.
    std::string resolve_leaf_type(const std::string& floor_type,
                                  const std::string& template_id) const {
        const std::string floor = floor_type.empty() ? "character" : floor_type;
        if (template_id.empty()) return floor;
        const Template* t = find_template(template_id);
        if (!t)          return floor;   // deleted clone -> fall back to floor
        if (t->builtin)  return floor;   // a built-in id is never an adopted type
        return t->id;                    // the editable clone
    }

    // ── Migration intake (the pure seam the binder walk feeds) ────────────────
    // Reconcile a legacy character/place leaf into the store. MERGE-PRESERVING
    // (s32): the leaf OWNS the floor fields (name<-title, description<-buffer,
    // image<-image_path) and the orphan leaf fields (tagline<-node.description,
    // role<-node.role), which are (re)stamped from the leaf every pass — but the
    // object owns EVERYTHING ELSE (custom template fields, relation iids), and
    // those survive the reconcile untouched. This is orphan-and-keep applied to
    // the projection itself: it is what lets the relation picker's stored edges
    // and a user-added field persist across the rebuild instead of being clobbered
    // back to a bare leaf shape. A first-time iid is created fresh. Returns the iid.
    //
    // s35 — `template_id` is the leaf's adopted clone (empty = floor); the object
    // carries the type resolve_leaf_type() produces, and is (idempotently)
    // instantiated against that template so a clone's custom fields are seeded.
    // The object is marked `projected` either way.
    std::string add_migrated_leaf(const std::string& iid,
                                  const std::string& floor_type,
                                  const std::string& title,
                                  const std::string& buffer_html,
                                  const std::string& image_path,
                                  const std::string& legacy_tagline = "",
                                  const std::string& legacy_role     = "",
                                  const std::string& template_id     = "");

    void clear_objects() { objects.clear(); }

    // Prune leaf-backed (projected) objects whose backing leaf is gone (iid not in
    // `live_iids`). Store-owned objects (projected == false) are never pruned —
    // they have no leaf to vanish. Called after a reconcile pass with the current
    // set of leaf iids, so a deleted character drops its object too.
    //
    // s35 — keyed on the explicit `projected` flag, not o.type: a leaf can now
    // adopt a CLONE type (tpl_…), so the old is_projected_type(o.type) proxy
    // would have wrongly spared a clone-typed character whose leaf was deleted.
    void prune_projected_except(const std::vector<std::string>& live_iids) {
        objects.erase(
            std::remove_if(objects.begin(), objects.end(),
                [&](const Object& o) {
                    if (!o.projected) return false;                        // store-owned: keep
                    return std::find(live_iids.begin(), live_iids.end(), o.iid)
                           == live_iids.end();                             // leaf gone: drop
                }),
            objects.end());
    }

    // ── Serialisation (the "object_store" blob sub-tree) ──────────────────────
    json to_json() const;
    void from_json(const json& j);

    bool empty() const { return templates.empty() && objects.empty(); }
};

}  // namespace Folio
