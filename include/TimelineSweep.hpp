#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TimelineSweep.hpp — the subject-first sweep planner (s80 step 5, §9.9 step 5).
// Pure, GTK-free, sandbox-testable: the diff the batch-linker commits.
//
// THE GESTURE (write-side, mirror of the read-side relief): the author arms a
// SUBJECT (a track), sweeps a SPAN of told-order scene columns [from..to], and
// releases to assert "this subject is present across these scenes". This unit
// computes WHAT THAT WRITE IS — additively: span scenes the subject does not yet
// claim get the link ADDED; span scenes it already claims (from ANY source —
// prose link or a prior timeline edit) are left alone (idempotent; no double-
// write, and edges_from_backlinks would dedupe anyway).
//
// ADDITIVE ONLY. A sweep asserts presence; it never removes links outside the
// span, and within the span it only adds. Detach is a separate, later gesture —
// but `already` is reported so a future toggle/detach is one branch away.
//
// KEYED ON IID. The span is given as told-order POSITIONS (what the cursor sweeps
// over), resolved to scene iids here against the spine — so the write targets are
// stable iids, and the result lines up with everything else keyed on iid.
//
// The planner OWNS NOTHING and writes NOTHING: it returns the iids to link; the
// model layer appends the subject to each scene's subject_links store and re-
// reads. "The timeline never keeps its own copy of a link."
// ─────────────────────────────────────────────────────────────────────────────

#include <string>
#include <unordered_set>
#include <vector>

namespace Folio {

// The diff a sweep commits, in told order.
struct SweepPlan {
  std::vector<std::string> add;      // span scenes NOT yet claimed → link the subject
  std::vector<std::string> already;  // span scenes already claimed (any source)
};

// Plan an additive sweep of the subject over the told-order span [from_pos..to_pos]
// (1-based, inclusive; order-insensitive — swept either direction). `claimed` is
// the subject's CURRENT claimed scene-set (from the full edge union), so already-
// linked scenes are excluded from `add`. Positions are clamped to the spine;
// an empty spine or a fully out-of-range span yields an empty plan.
SweepPlan plan_sweep(const std::vector<std::string>& spine,
                     int from_pos, int to_pos,
                     const std::unordered_set<std::string>& claimed);

}  // namespace Folio
