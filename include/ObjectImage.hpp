#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ObjectImage.hpp — pure helpers for the s44-unified object PORTRAIT field.
//
// DESIGN_gallery §2a / §9. An object's portrait is no longer an external file
// PATH; it is the iid of a linked POOL FRAGMENT (field-names-the-iid: the field
// VALUE is the primary marker — there is no separate flag). This unit is the
// pure seam two GTK surfaces lean on:
//
//   • image_display_path — the DUAL-READ resolver. A value that is an ast_ iid
//     resolves to assets/<iid>.<ext> (ext looked up in the pool); anything else
//     is treated as a literal external path and returned as-is. So legacy
//     projects keep rendering their old external paths with NO forced migration
//     — only a fresh Set-image lands an iid. Every reader of an object's image
//     value (the ObjectForm portrait preview AND the avatar strip) funnels
//     through this, so the two stay in lock-step.
//
// Orientation-independent display sizing (§9) is NOT reimplemented here — the
// portrait keys off the SAME ImageNormalize::fit_long_edge(w, h, cap) the import
// pipeline already uses (the rule §9 names: long edge = max(w,h), short follows
// aspect, downscale-only). The GTK preview passes the sizer value as the cap.
//
// Pure: <filesystem>, <string>, ImagePool (pure), ProjectBundle::asset_path
// (pure fs), Iid (pure). GTK-free — sandbox-proven before any pixbuf wiring.
// ─────────────────────────────────────────────────────────────────────────────

#include <filesystem>
#include <string>

#include "ImagePool.hpp"

namespace Folio {

namespace fs = std::filesystem;

// True iff the stored image-field VALUE is a pool fragment handle (a well-formed
// ast_ iid), as opposed to a legacy external filesystem path. The single
// predicate the resolver and the field's Set/Clear logic share.
bool image_value_is_asset(const std::string& value);

// The dual-read resolver (the unification seam). Returns the filesystem path to
// LOAD for preview, or "" when there is nothing to show:
//   • ast_ iid  → assets/<iid>.<ext> via the pool's recorded ext; "" if the
//                 fragment is unknown or soft-deleted (a dangling / hidden
//                 portrait shows the field's "could not load" notice, not a
//                 stale image);
//   • otherwise → the literal value (a legacy external path), returned verbatim
//                 (incl. "" for an unset field).
// `root` is the live .folio bundle dir (DocumentModel::current_path).
std::string image_display_path(const std::string& value,
                               const ImagePool& pool, const fs::path& root);

}  // namespace Folio
