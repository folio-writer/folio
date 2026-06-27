#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// KpPalette.hpp — the Key-Point palette reconcile (s81 slice 2). Pure, GTK-free,
// JSON-free, sandbox-testable.
//
// THE IDEA (Scott's): a KP has no stored ordinal — its ordinal is IMPLIED BY ITS
// POSITION IN THE PALETTE. The palette is an ordered list of named, coloured
// swatches; a scene names its KP by the swatch's STABLE id (kp_id), never by
// position. So:
//
//   • the swatch's 1-based position IS the beat's arc ordinal (and its color_idx);
//   • reordering the palette re-ordinals AND re-colours every beat for free,
//     because each scene resolves its current ordinal/label/colour from its
//     stable id at read time — nothing positional is stored on the scene;
//   • a swatch carries {id, name, hex}; the spectrum ramp is baked into `hex` at
//     creation (ModuleIO::keypoint_palette), so colour reads straight off the
//     swatch — this layer never recomputes colour, it just reports the hex.
//
// This is the same discipline as the rest of the timeline: identity is a stable
// id, ORDER is derived from a list at read time, never duplicated onto the leaf
// (cf. the told-order spine deriving a scene's number from its position).
//
// TWO PURE OPERATIONS:
//
//   resolve_kp(palette, kp_id) — the read every consumer needs: given the CURRENT
//     ordered palette and a scene's kp_id, report the swatch's 1-based ordinal
//     (== color_idx), its label, and its hex. `found == false` when the swatch
//     was deleted (the scene's kp_id is now dangling — the caller decides whether
//     to clear the tag or leave it as a tombstone). First match wins on the
//     (degenerate) duplicate-id case, for determinism.
//
//   backfill_swatch_ids(palette, gen) — the migration: a pre-s81 palette has
//     swatches with no id (TagColor was {name, hex}). On load, mint a stable id
//     for each id-less swatch via the injected generator (the GTK side passes
//     make_iid(IidKind::KeyPoint); the sandbox passes a deterministic stub).
//     Returns the count changed so the caller knows whether to persist. Existing
//     ids are left untouched — a back-fill is idempotent across loads.
//
// Pure: <string>, <vector>, <functional>. No GTK, no GLib, no FolioPrefs, no
// Iid — the id generator is INJECTED so this unit is deterministic under test
// and free of the Iid TU. Sandbox-proven (TEST_kp_palette.cpp) before wiring.
// ─────────────────────────────────────────────────────────────────────────────

#include <functional>
#include <string>
#include <vector>

namespace Folio {

// A minimal mirror of a palette swatch — the GTK side maps TagColor↔KpSwatch.
// (TagColor lives in FolioPrefs.hpp, which pulls GLib; this stays pure.)
struct KpSwatch {
  std::string id;    // stable swatch id (kp_… ); "" before the s81 back-fill.
  std::string name;  // display label (the tag the scene wears).
  std::string hex;   // baked spectrum colour ("#rrggbb").
};

// The resolution of a scene's kp_id against the current ordered palette.
struct KpResolution {
  bool        found     = false;  // false → the swatch was deleted (dangling id).
  int         color_idx = 0;      // 1-based palette position (== arc ordinal); 0 if !found.
  std::string label;              // swatch name; empty if !found.
  std::string hex;                // swatch colour; empty if !found.
};

// Resolve a scene's KP by its stable id. Ordinal = palette position + 1. O(n).
KpResolution resolve_kp(const std::vector<KpSwatch>& palette,
                        const std::string& kp_id);

// Mint stable ids for any id-less swatches (the pre-s81 migration). Idempotent;
// returns how many swatches were changed. `gen` yields a fresh unique id per call.
int backfill_swatch_ids(std::vector<KpSwatch>& palette,
                        const std::function<std::string()>& gen);

// ── Reconcile on palette edit (s81 slice 2c) ─────────────────────────────────
// color_idx is a POSITIONAL index into the palette, so ANY edit (reorder OR
// delete) shifts which swatch a stored color_idx points at — and that hits EVERY
// coloured scene, not only KP-tagged ones. The fix is a positional REMAP: figure
// out where each OLD swatch position lands after the edit, then rewrite every
// scene's color_idx by that map. Universal — covers scenes coloured anywhere,
// with or without a kp_id.
//
// A mutable view of a scene's KP-bearing fields. The GTK side builds these from
// every colour-bearing BinderNode in a parallel vector and writes results back
// by index. Pure so the remap is deterministic and edge-tested before any model
// mutation.
struct SceneKpRef {
  std::string kp_id;
  int         color_idx    = 0;
  std::string kp_label;
  bool        is_key_point = false;
  bool        pin          = false;
};

// Build the old→new index map from the swatch IDENTITY order before/after an
// edit. Result is indexed by 1-based OLD color_idx: remap[old] = the swatch's new
// 1-based position, or 0 if it was deleted. remap[0] = 0 (None stays None). Both
// reorder and delete reduce to this one map (a reorder deletes nothing; a delete
// drops one id). Sizes to old_ids.size()+1.
std::vector<int> palette_remap(const std::vector<std::string>& old_ids,
                               const std::vector<std::string>& new_ids);

// Apply a remap to scenes. A scene whose swatch survived has its color_idx
// rewritten to the new position (kp_id / kp_label are identity, unchanged by a
// move). A scene whose swatch was DELETED (remap → 0) is fully CLEARED — kp_id,
// color_idx → 0, kp_label, is_key_point, pin (option b: deleted colour → None,
// and a beat can't outlive its colour). color_idx 0 (None) is left alone.
// Returns # of scenes changed.
int apply_palette_remap(const std::vector<int>& remap,
                        std::vector<SceneKpRef>& scenes);

}  // namespace Folio
