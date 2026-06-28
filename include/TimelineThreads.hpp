#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TimelineThreads.hpp — the THREAD-lane assembler (s83, DESIGN_timeline.md §9.12
// / §9.9 step 7 — the arc model). Pure, GTK-free, JSON-free, sandbox-testable.
//
// THE ARC-MODEL FORK, SETTLED (§9.8 #2). The fork was "entity arcs *revealed*
// (pure relief) vs. thread arcs *assigned*." The settlement keeps BOTH because
// they answer two different questions over the one graph, and they do NOT
// compete — they are two independent axes that can both light the same scene:
//
//   • ENTITY arc  (REVEALED, already shipped) = the relief of a SUBJECT track
//       (assemble_tracks): "where does Boromir appear?" Falls straight out of
//       the edges the author drew; no substrate, no marks. Death/exit needs NO
//       hand-bounding — the relief already expresses it: a contiguous Bar is the
//       living run, the interior gap is the absence, and a detached Dot after
//       the gap IS the remembered/posthumous beat (§0's Faramir-remembers-Boromir
//       case, drawn for free). Hand-bounding a death was REJECTED: it asserts a
//       boundary the links don't, risks the relief lying if a later flashback is
//       written, and any "this beat is a memory" nuance belongs on the EDGE
//       (StoryEdge.label), never as a new scene-level verdict mark.
//
//   • THREAD arc  (ASSIGNED, this slice) = the relief of an authored THREAD a
//       scene is placed into: "which storyline does this scene belong to?" This
//       is the LOTR case (Frodo's line ∥ Aragorn's line, told in blocks) and the
//       Tapper case (one thread across braided timeframes). It is NOT derivable
//       from links — it is an authorial assignment, so it needs a mark on the
//       scene. That mark is the ONE piece of new substrate step 7 introduces.
//
// THE SUBSTRATE (mirrors the KP precedent exactly — the proven house shape):
//   A thread is, like every other timeline row (§9.1), "a named colour-coded
//   ordered scene-set." A scene carries its thread on the node (the GTK/model
//   slice adds `BinderNode.thread`, the exact analog of `kp_id`); a small
//   project registry resolves a thread KEY → label + colour. This PURE layer is
//   AGNOSTIC to whether that key is a free string (the SceneMark block-in's
//   `std::string thread`) or an iid into a registry (`thr_…`, rename-safe, the
//   KP-style call) — it groups on whatever key the model stamps. So shipping
//   this layer commits NOTHING about the registry: that decision rides into the
//   model slice. (Recommendation lives in §9.12: iid + registry, for rename
//   safety and a stable colour, consistent with kp_id.)
//
// SHAPE — the exact analog of assemble_kp_lanes (TimelineKp.hpp), one row per
// THREAD instead of one per KP:
//
//   input:  spine         — told-order scene iids (project_spine().spine_iids()).
//                           Authoritative for ordering AND membership: only
//                           on-spine scenes are claimed; an assignment on an
//                           off-spine / deleted scene is silently dropped, so the
//                           lane always lines up under the spine.
//           scene_thread  — per-scene thread fact, keyed by scene iid. The model
//                           fills it from BinderNode in the GTK TU (a walk of
//                           all_node_ptrs, like the KP map); the sandbox fills it
//                           by hand. A scene absent from the map, or present with
//                           an empty thread_key, is UNASSIGNED and joins no lane.
//                           NOTE the deliberate difference from the KP layer:
//                           there is no is_key_point-style gate — the assignment
//                           IS the opt-in. A scene with a non-empty thread_key is
//                           in that thread, period.
//   output: lanes         — one ThreadLane per distinct thread_key that claims
//                           ≥1 on-spine scene, ordered by FIRST APPEARANCE
//                           (earliest claimed told-order position), then
//                           thread_key for determinism. (First appearance is the
//                           natural braid order — Frodo's block, then Aragorn's
//                           block, etc.) No category grouping: a thread has no
//                           §9.6 category; like a KP it carries a colour.
//
// ONE RENDERER, FOUR ADAPTERS NOW. §9.3 named three sources feeding the single
// relief renderer (object-subjects, groups, key-points). Threads are the fourth,
// and slot in identically: each lane's `claimed` set is handed to the shipped
// compute_relief (§9.2) to produce its bars/dots/gaps. The renderer never learns
// which adapter produced the set — thread lanes draw with the same trunk as
// subject tracks and the KP strip.
//
// SCOPE — TOLD-ORDER lanes only (the braid rhythm: blocks of one thread
// alternating with another along the reader's sequence). The WORLD-CLOCK layout
// (threads as concurrent lanes on a shared story-time axis, StoryGraph.hpp's
// third timeline layout) reuses THIS SAME substrate later; it is not this slice.
//
// KEYED ON IID, NOT POSITION (§9.7, shared with compute_relief / assemble_tracks
// / assemble_kp_lanes): positions derive from the spine at call time, so a binder
// reorder renumbers and recomputes the lane for free. The timeline stores no
// geometry; a thread assignment is a fact ON the scene, read here, never owned.
//
// Pure: <string>, <unordered_map>, <unordered_set>, <vector>. No GTK, no GLib,
// no JSON, no DocumentModel — sandbox-proven (TEST_timeline_threads.cpp) before
// any geometry or model substrate is wired, like every other timeline layer.
// ─────────────────────────────────────────────────────────────────────────────

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Folio {

// ── The per-scene thread fact (a mirror of the scene node's stamped field) ────
// The model fills these from BinderNode (the GTK/model slice adds `thread` +
// resolves its label/colour from the registry); the sandbox fills them by hand.
// Unassigned scenes are simply absent from the map (or carry an empty
// thread_key). Self-contained — no DocumentModel dependency. The `thread_key` is
// opaque to this layer: free string OR thr_… iid, it only groups on it.
struct SceneThreadInfo {
  std::string thread_key;     // the thread this scene is placed in; "" = unassigned.
  std::string thread_label;   // human-readable thread label (display); may be empty.
  int         color_idx = 0;  // thread's colour index (0 = unset → renderer fallback).
};

// ── One thread lane: a named, coloured, ordered scene-set ─────────────────────
// Its claimed set is handed to compute_relief at render to produce the lane's
// bars (a block where the thread runs) and singletons (a lone scene of that
// thread between blocks of others). Carries color_idx unread — the renderer
// resolves it to a hue, exactly as the KP strip does.
struct ThreadLane {
  std::string thread_key;     // the stable key this lane groups on (string or iid).
  std::string label;          // display label (thread_label of the first claimed
                              //  scene in told order; "" if that scene had none).
  int         color_idx = 0;  // colour index (first claimed scene; 0 = unset).
  std::unordered_set<std::string> claimed;  // claimed scene iids, all ON the spine.
  int         first_pos = 0;  // earliest claimed told-order position (sort key).
};

// Assemble the thread lanes from the told-order spine + the per-scene thread map
// (§9.12). Deterministic; first-appearance (braid) order, then thread_key. The
// label/color_idx of each lane are taken from its FIRST claimed scene in told
// order, so a lane is self-describing even if a later scene carries a stale or
// blank label. See header for the claim rule (on-spine only, no is_key_point
// gate) and the substrate-agnostic key rationale.
std::vector<ThreadLane>
assemble_thread_lanes(const std::vector<std::string>& spine,
                      const std::unordered_map<std::string, SceneThreadInfo>& scene_thread);

}  // namespace Folio
