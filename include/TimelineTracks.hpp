#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TimelineTracks.hpp — subject-track assembly for the relief tracks (s80 step 3,
// DESIGN_timeline.md §9.1 / §9.3). Pure, GTK-free, sandbox-testable.
//
// The relief tracks below the spine are one row per SUBJECT: a named, category-
// coloured, ordered scene-set (§9.1). This unit turns the typed edge list
// (StoryGraph::edges_from_backlinks) into those subjects, ready to hand each
// one's claimed scene-set to the shipped compute_relief (§9.2). One renderer,
// many adapters — this is the "object subjects" adapter (§9.3 source 1).
//
// WHICH SCENES A SUBJECT CLAIMS. A scene S is claimed by subject U when an edge
// connects them and exactly one endpoint is a scene (the other being U) —
// DIRECTION-AGNOSTIC: a scene that folio-links a character, AND a character page
// that folio-links a scene, both count. Scene→scene (foreshadow) and subject→
// subject (object relations, image→object) edges produce no track claim. Only
// scenes that are ON the told-order spine are kept (a claim on an off-spine /
// deleted scene is dropped), so the relief always lines up with the spine.
//
// ORDER (the s80 design call, confirmed): grouped by CATEGORY in the §9.6 hue
// order (character ▸ place ▸ reference ▸ image), then by FIRST APPEARANCE
// (earliest claimed told-order position), then iid for determinism. Subjects
// with no on-spine claim get no track.
//
// CATEGORY is derived from the subject iid prefix (Iid.hpp), never stored — the
// renderer maps category → hue (§9.6); this layer stays colour-blind, reporting
// only the category enum, the same way compute_relief stays Dot/Diamond-blind.
// ─────────────────────────────────────────────────────────────────────────────

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "StoryGraph.hpp"   // StoryEdge (pure; header-only struct)

namespace Folio {

// Category, in the §9.6 hue order (also the track group order).
enum class TrackCategory { Character, Place, Reference, Image };

// One subject track: a named, categorised, ordered scene-set. The claimed set is
// handed to compute_relief at render to produce its bars/dots/gaps.
struct TimelineTrack {
  std::string iid;        // subject iid (chr_/plc_/ref_/ast_)
  std::string label;      // display title (resolved from the model; iid if absent)
  TrackCategory category = TrackCategory::Character;
  std::unordered_set<std::string> claimed;  // claimed scene iids, all ON the spine
  int first_pos = 0;      // earliest claimed told-order position (sort key)
  // s86 — the subject's own assigned colour (1-based into the project palette),
  // 0 = none. The pure layer leaves this 0; the GTK layer fills it from the
  // model so the timeline can honour a per-object colour, falling back to the
  // §9.6 category hue when unset.
  int color_idx = 0;
};

// Assemble subject tracks from the edge list (§9.3 source 1). `spine` is the
// told-order scene iids (project_spine().spine_iids()); `labels` resolves a
// subject iid → display title (the model fills it; a missing entry falls back to
// the iid). Deterministic; see header for the claim rule and ordering.
std::vector<TimelineTrack>
assemble_tracks(const std::vector<std::string>& spine,
                const std::vector<StoryEdge>& edges,
                const std::unordered_map<std::string, std::string>& labels);

}  // namespace Folio
