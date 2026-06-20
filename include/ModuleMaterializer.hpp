#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — ModuleMaterializer.hpp   (s23 — Layer 2)
//
// Turns a ScaffoldPlan into real BinderNodes in the manuscript: Part/Book groups
// (skipped when TopContainer::None), Chapter groups, and Scene leaves — each
// scene stamped with its Key Point (kp_id) and word target. Uses the model's own
// add_group/add_leaf so iids/ids are minted consistently. Everything it lays
// down is ordinary, fully-editable clay: combine/split/move/delete freely; only
// kp_id is meant to ride along through reshaping.
//
// Not pure (mutates DocumentModel, which transitively needs glibmm), so this is
// inspection-tier, compiled on Scott's box — not sandbox-verified like the
// planner. The dialog (Layer 3) will gather PlanInputs; for now a hamburger
// action drives it with the built-in defaults so the scaffold is eyes-on.
// ─────────────────────────────────────────────────────────────────────────────

#include "Module.hpp"
#include <string>

namespace Folio {

class DocumentModel;

namespace ModuleMaterializer {

// Append the plan into the manuscript root. Returns the iid of the first
// top-level node created (for selection), or "" if the plan was empty.
std::string materialize(DocumentModel& model, const ScaffoldPlan& plan);

} // namespace ModuleMaterializer
} // namespace Folio
