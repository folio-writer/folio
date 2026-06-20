#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — ModulePlanner.hpp   (s23 — pure scaffold planner; GTK-free)
//
// Turns a Module (anatomy) + PlanInputs (target words, words/scene, chapters,
// top container, scene pattern) into a ScaffoldPlan: ordered Parts → Chapters →
// Scenes, each scene tagged to its Key Point with a per-scene frenetic guide.
// Layer 2 materialises the plan into BinderNodes; Layer 3 (the dialog) gathers
// the inputs. Pure arithmetic — no GTK, no model, sandbox-testable.
//
// The pipeline:
//   1. total = round(target_words / avg_scene_words), floored at #KPs (each KP
//      gets ≥1 scene).
//   2. distribute `total` across KPs by `weight` (largest-remainder, min 1).
//   3. acts → Parts; each Part's scene count = Σ its KPs' scenes.
//   4. distribute `chapters` across acts by act scene count (largest-remainder,
//      min 1); chunk each act's contiguous scene run into its chapters evenly.
//   5. per-scene frenetic: Flat = KP frenetic; BuildToSpike = mean-preserving
//      build→spike contour within each KP cluster.
//   TopContainer::None collapses to one synthetic Part (label "") spanning the
//   whole arc; chapters then chunk the whole book.
// ─────────────────────────────────────────────────────────────────────────────

#include "Module.hpp"

namespace Folio {
namespace ModulePlanner {

ScaffoldPlan plan(const Module& m, const PlanInputs& in);

} // namespace ModulePlanner
} // namespace Folio
