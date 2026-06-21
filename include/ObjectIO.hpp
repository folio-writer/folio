#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — ObjectIO.hpp   (s31 — pure json ⇄ Object/Template seam; GTK-free)
//
// Mirrors ModuleIO's role exactly: the only novel logic (the round-trip + the
// legacy migration) lives here as pure functions over nlohmann::json, with ZERO
// GTK/GLib dependency, so it is g++-compilable and unit-checkable in the sandbox.
// The app reads/writes the JSON and pumps the text through these; directory
// enumeration (the future ObjectLibrary, mirroring ModuleLibrary) is thin app
// glue, not part of this seam (UI reaches; seams are fed — DESIGN §4.7a).
//
// Objects & templates are JSON-native, like modules — so this seam is json-
// native too. A FieldSchema's `config` and an Object's `values` are stored
// verbatim as json sub-trees: the round-trip never has to know a field type's
// inner shape, which is what keeps the system open to new field types and to
// orphan values (§12 — a value whose field left the schema is just an extra key
// the writer preserves and the reader hands back untouched).
// ─────────────────────────────────────────────────────────────────────────────

#include "Object.hpp"
#include <nlohmann/json.hpp>
#include <string>

namespace Folio {

using json = nlohmann::json;

namespace ObjectIO {

// ── Template (schema) round-trip ─────────────────────────────────────────────
json     template_to_json(const Template& t);
Template template_from_json(const json& j);

std::string template_to_string(const Template& t, bool pretty = true);
Template    template_from_string(const std::string& text);

// ── Object (instance) round-trip ─────────────────────────────────────────────
// `values` is carried verbatim, including orphan keys (values whose field has
// left the schema) — they survive the round-trip so a re-added field restores
// them (§12 orphan-and-keep).
json   object_to_json(const Object& o);
Object object_from_json(const json& j);

std::string object_to_string(const Object& o, bool pretty = true);
Object      object_from_string(const std::string& text);

// ── Migration: today → this (§8 — reframe, not replace) ──────────────────────
// The current character/place model is a text-buffer leaf (BinderNode). Rather
// than depend on the heavy DocumentModel header, this seam takes the leaf's
// PLAIN fields and produces an Object under the matching built-in default
// template — keeping ObjectIO pure and sandbox-testable. The thin BinderNode
// adapter (reading node->title etc. and calling this) lives where DocumentModel
// is already in scope.
//
// Mapping (§8): title → name, the text buffer (BinderNode.content) → the
// richtext description field, image_path → image. NOTHING is lost: the legacy
// one-liner `description` and `role` (which the floor template does not surface)
// are preserved as ORPHAN values under keys "tagline"/"role", so a template that
// later adds those fields restores them — orphan-and-keep applied to migration.
//
// `floor_type` ("character"/"place"/"reference") selects the built-in default; `iid` carries the
// node's existing stable id forward unchanged (the object IS the same part).
Object migrate_legacy_leaf(const std::string& iid,
                           const std::string& floor_type,
                           const std::string& title,
                           const std::string& buffer_html,
                           const std::string& image_path,
                           const std::string& legacy_tagline = "",
                           const std::string& legacy_role    = "");

// ── Write-back: the INVERSE of the §8 floor mapping (s32) ────────────────────
// The editable object form (s32) flips editing authority toward objects, but the
// store is still a PROJECTION of the binder leaves this slice — so an edit to a
// floor field must be written THROUGH to the backing leaf, or rebuild_object_store
// would overwrite it on the next selection/save. That write-through must be the
// EXACT inverse of migrate_legacy_leaf's mapping, or an edit lands in the wrong
// leaf field. Encoding the inverse ONCE here, pure and tested, makes that drift
// structurally impossible (the same instinct as KP-as-tag-as-colour): the form's
// on_change resolves a floor field id to the leaf field it owns, and nowhere does
// the GTK code re-derive the mapping by hand.
//
// migrate (leaf -> object):  title->name, content->description, image_path->image,
//                            node.description->tagline(orphan), node.role->role(orphan)
// inverse (object field id -> leaf field), exactly reversed below.
//
// LeafField::None marks a field with no floor->leaf home this slice — a value
// that lives only in the object store (a future custom-template field), not a
// projected leaf field. The caller leaves the leaf untouched for None.
enum class LeafField { None, Title, Content, ImagePath, Description, Role };

inline LeafField floor_field_to_leaf(const std::string& field_id) {
    if (field_id == "name")        return LeafField::Title;        // <- title
    if (field_id == "description") return LeafField::Content;      // <- the buffer
    if (field_id == "image")       return LeafField::ImagePath;    // <- image_path
    if (field_id == "tagline")     return LeafField::Description;  // <- one-liner (orphan)
    if (field_id == "role")        return LeafField::Role;         // <- role (orphan)
    return LeafField::None;                                        // object-only field
}

}  // namespace ObjectIO
}  // namespace Folio
