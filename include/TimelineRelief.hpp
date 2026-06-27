#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TimelineRelief.hpp — the pure relief function (DESIGN_timeline.md §9.2).
//
// THE first slice of the Relationship Timeline, and the heart of the whole
// surface. Given the told-order spine and a subject's claimed SET of scenes, it
// reports where that subject's presence draws as solid (bars), as isolated beats
// (dots), and where it is interrupted (the interior gaps focus surfaces).
//
//   input:  spine    — told-order scene iids; a scene's position is index+1.
//           claimed  — the subject's claimed scene iids, as a SET. Order is
//                      derived from the SPINE, never from claimed's iteration.
//           label    — presentation only (rides through unread).
//           colour   — presentation only (rides through unread).
//   output: segments — maximal contiguous claimed runs, told order:
//                        length ≥ 2 → Bar    (a span of presence)
//                        length 1   → Dot    (an isolated claimed scene)
//           gaps     — INTERIOR absences only: unclaimed positions strictly
//                      between two claimed segments. Leading scenes (before the
//                      subject first appears) and trailing scenes (after it is
//                      last present) are NOT gaps — they are absence-of-
//                      presence, not the "where did they go" focus surfaces.
//           unplaced — claimed iids that are not on the spine (dropped from the
//                      relief; reported for diagnostics), sorted for determinism.
//
// KEYED ON IID, NOT POSITION. Positions are derived from binder told-order at
// call time, so a binder reorder renumbers and recomputes the relief for free —
// an arc fragments or heals live with no stored state. This mirrors the app
// thesis "edges are read, never owned": the timeline stores no bars.
//
// SOURCE-AGNOSTIC (§9.3). The renderer never learns which adapter produced the
// claimed set — object subjects (edges_from_backlinks), groups (union of member
// sets), or key points (the tagline→scenes index) all reduce to the same
// `(label, colour, scene-set)` triple. The Dot/Diamond visual split is a
// RENDERER concern (subjects draw dots, KPs draw diamonds); this layer only
// knows "singleton", so it stays adapter-blind.
//
// Pure: <string>, <vector>, <unordered_set>. GTK-free, JSON-free — sandbox-
// proven (TEST_timeline_relief.cpp) before any geometry is wired.
// ─────────────────────────────────────────────────────────────────────────────

#include <string>
#include <unordered_set>
#include <vector>

namespace Folio {

// A maximal contiguous run of claimed positions on the told-order spine.
struct ReliefSegment {
  enum class Kind { Bar, Dot };  // Bar = length ≥ 2; Dot = singleton.

  Kind kind = Kind::Dot;
  int start_pos = 0;  // 1-based told-order position, inclusive.
  int end_pos = 0;    // inclusive; == start_pos for a Dot.
  std::vector<std::string> iids;  // claimed scene iids in this run, told order.

  int length() const { return end_pos - start_pos + 1; }
};

// An interior absence: a span of unclaimed positions strictly between two
// claimed segments (never leading/trailing — see header).
struct ReliefGap {
  int start_pos = 0;  // 1-based, inclusive.
  int end_pos = 0;    // inclusive.

  int length() const { return end_pos - start_pos + 1; }
};

// The relief of ONE subject over the told-order spine. Carries presentation
// (label, colour) but stores no edges — it is recomputed each render.
struct Relief {
  std::string label;
  std::string colour;
  std::vector<ReliefSegment> segments;  // told order.
  std::vector<ReliefGap> gaps;          // interior only, told order.
  std::vector<std::string> unplaced;    // claimed iids not on the spine, sorted.
};

// THE pure relief function (§9.2). Deterministic; spine order is authoritative.
Relief compute_relief(const std::vector<std::string>& spine,
                      const std::unordered_set<std::string>& claimed,
                      std::string label = {}, std::string colour = {});

}  // namespace Folio
