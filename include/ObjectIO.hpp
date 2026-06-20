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
// `is_place` selects the Place vs Character built-in default; `iid` carries the
// node's existing stable id forward unchanged (the object IS the same part).
Object migrate_legacy_leaf(const std::string& iid,
                           bool               is_place,
                           const std::string& title,
                           const std::string& buffer_html,
                           const std::string& image_path,
                           const std::string& legacy_tagline = "",
                           const std::string& legacy_role    = "");

}  // namespace ObjectIO
}  // namespace Folio
