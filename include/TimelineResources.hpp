#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TimelineResources.hpp — the resource-rail roster (s82, DESIGN_timeline.md §3 /
// §9.11). Pure, GTK-free, sandbox-testable.
//
// THE ENTRY POINT that makes the timeline a BUILDER, not a viewer. The relief
// tracks (assemble_tracks) only exist for subjects that ALREADY claim a scene —
// so a fresh "Place A" with no links has no track and no way onto the spine. The
// §3 primary gesture ("pick Place A → sweep these scenes → linked") needs a
// roster of EVERY linkable subject, claimed or not, to arm from. That roster is
// this unit's output; the rail paints it; arming one + sweeping the spine writes
// the edges (the existing sweep path, TimelineSweep.hpp) and the track appears.
//
// THE ROSTER = the union of linkable subjects (§2 "resources on the left"):
//   • binder objects — characters / places / references (the section leaves);
//   • the gallery image pool — each live fragment (§9.3 source-agnostic: an image
//     is just another resource you link to a scene). LEAN aggregation (the s76
//     map-view fork, Option A): a fragment IS a resource row, no heavy gallery
//     object wrapper. (HANDOFF s82: "lean Option A".)
// The GTK TU fills the candidate list off the model (Characters/Places/References
// roots + image_pool); the sandbox fills it by hand. Category is authoritative
// from the candidate (it is the section / pool the resource came from), so this
// layer never re-derives it from the iid — it only groups and counts.
//
// CLAIM COUNT without re-deriving the claim rule. The "how many on-spine scenes
// does this subject claim" number is EXACTLY assemble_tracks' claimed-set size.
// Rather than re-implement that direction-agnostic scene↔subject rule (and risk
// it drifting), this unit READS the shipped tracks: a candidate's count is the
// size of its track's claimed set, or 0 when it has no track. One source of
// truth for "what a subject claims", two readers (the relief rows and the rail).
//
// SHAPE:
//   input:  candidates — every linkable subject (iid, label, category).
//           tracks     — assemble_tracks() output (the claim sets, already
//                        spine-filtered). Used only for claim counts.
//   output: groups     — one ResourceGroup per NON-EMPTY category, in the §9.6
//                        hue order (character ▸ place ▸ reference ▸ image — the
//                        same order the relief tracks group by). Within a group,
//                        items are ordered by LABEL (case-insensitive), then iid
//                        for determinism — a roster is a directory you SCAN to
//                        pick from, so alphabetical beats first-appearance here
//                        (unlike the relief tracks, which are arc-ordered).
//
// Pure: <string>, <vector>. Depends only on TimelineTracks.hpp (itself pure).
// GTK-free, JSON-free — sandbox-proven (TEST_timeline_resources.cpp) before the
// rail is painted, like every other timeline layer.
// ─────────────────────────────────────────────────────────────────────────────

#include <string>
#include <vector>

#include "TimelineTracks.hpp"  // TrackCategory, TimelineTrack (pure)

namespace Folio {

// One linkable subject offered by the rail (pre-count). The model fills these off
// the Characters/Places/References roots + the image pool; the sandbox by hand.
struct ResourceCandidate {
  std::string   iid;       // chr_/plc_/ref_/ast_
  std::string   label;     // display title / caption (iid fallback handled by rail)
  TrackCategory category = TrackCategory::Character;
};

// One row in the rail: a candidate plus how many on-spine scenes it claims now.
struct ResourceItem {
  std::string   iid;
  std::string   label;
  TrackCategory category = TrackCategory::Character;
  int           claim_count = 0;  // size of this subject's track claimed set (0 = none)
};

// One category section of the rail (a header + its rows). Only non-empty groups
// are emitted, in the §9.6 hue order.
struct ResourceGroup {
  TrackCategory            category = TrackCategory::Character;
  std::vector<ResourceItem> items;   // label-then-iid order
};

// Assemble the rail roster (§3). Deterministic; see header for ordering and the
// claim-count-via-tracks rationale. A candidate with a blank label keeps it
// blank (the rail falls back to the iid for display, like the relief gutter).
std::vector<ResourceGroup>
assemble_resources(const std::vector<ResourceCandidate>& candidates,
                   const std::vector<TimelineTrack>& tracks);

}  // namespace Folio
