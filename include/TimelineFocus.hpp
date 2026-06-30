#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TimelineFocus.hpp — PERSISTENT focus for the Relationship Timeline (s92,
// DESIGN_timeline.md §4 / §9.8 #1 / §9.12.5). Pure, GTK-free, header-only.
//
// The timeline has always had a TRANSIENT hover-isolate (hover a relief row →
// siblings dim; leave → it springs back). PERSISTENT focus makes that STICK:
// pick a relief row (or a few) and walk the spine with everything else dimmed,
// so the gaps are visible and you can draw the missing link right there — the §4
// "builder, not viewer" instinct. Nothing about the model changes; this is a
// presentation channel (§9.6 focus→brightness), the same channel the hover
// isolate uses, just pinned.
//
// FOCUS KEYS ON ANY RELIEF ROW (§9.12.5). A subject track, a thread lane and a
// KP are the same (label, colour, scene-set) shape, so focus treats them
// uniformly: each is reduced to an opaque KEY + its claimed told-order scene
// set. The GTK layer NAMESPACES the key by row kind ("S:"/"T:"/"K:" + id) before
// it reaches this unit, so the three id namespaces (subject iid / thread key /
// kp id) can never collide inside one focus set — the s87/s91 specificity
// lesson, applied up front. This unit stays kind-blind on purpose, exactly the
// way compute_relief stays Dot/Diamond-blind: it only ever compares keys for
// equality and unions scene-sets.
//
// THE MODEL — single primary by default, optional spotlighted set (the §9.12.5
// lean, here made concrete):
//   • PRIMARY toggle (a plain click): focus_toggle_primary — single-focus the
//     row; re-clicking the row that is ALREADY the sole focus clears focus.
//   • SPOTLIGHT toggle (a shift click): focus_toggle_spotlight — add/remove the
//     row from the focused set, so a handful of rows can be spotlighted at once.
// An empty set means "focus off" (everything full brightness, hover-isolate
// resumes). The two toggles are tiny set ops, but they ARE the fork being
// settled (§9.8 #1), so a test pinning their truth table is documentation, not
// theatre.
//
// THE SPINE-WALK READ — focus_positions: the told-order positions claimed by ANY
// focused row, so the painter can recede the columns the focused set does NOT
// touch (the spine itself then shows where the focused thread is present vs.
// dark — the §4 gap). An off-spine claim is dropped against `spine`, the same
// discipline the relief functions use.
//
// Presentation-only THIS slice: the focus set is NOT serialised (it descends
// from the hover-isolate, which persisted nothing). Whether focus should also
// stick ACROSS sessions — the way timeline zoom and rail-collapse do — is an
// open call left for Scott to make after using it (see the s92 handoff).
//
// Header-only (inline): the helpers are small enough that a .cpp + a CMakeLists
// entry would be ceremony; the sandbox test includes this header directly and
// the one consumer (TimelineSurface.cpp) gets them via TimelineSurface.hpp.
// ─────────────────────────────────────────────────────────────────────────────

#include <set>
#include <string>
#include <unordered_set>
#include <vector>

namespace Folio {

// One focusable relief row, reduced to the only two facts focus needs: its
// opaque (already kind-namespaced) KEY and its claimed told-order scene set.
struct FocusLane {
  std::string key;
  std::unordered_set<std::string> claimed;
};

// The focus set: the row keys currently pinned. std::set so iteration / equality
// are deterministic (the painter walks it, a test compares it).
using FocusSet = std::set<std::string>;

// PRIMARY toggle (plain click). Clicking the row that is ALREADY the sole focus
// clears focus; any other click replaces the whole set with {key}. This is the
// "single primary by default" half of §9.12.5 — a plain click never grows the
// set, it always lands on exactly one row (or none).
inline FocusSet focus_toggle_primary(const FocusSet& cur, const std::string& key) {
  if (cur.size() == 1 && *cur.begin() == key) return FocusSet{};
  return FocusSet{key};
}

// SPOTLIGHT toggle (shift click). Add `key` if absent, remove it if present —
// the "optional spotlighted set" half. Removing the last member lands on the
// empty set (focus off), the same end state focus_toggle_primary reaches.
inline FocusSet focus_toggle_spotlight(FocusSet cur, const std::string& key) {
  const auto it = cur.find(key);
  if (it != cur.end()) cur.erase(it);
  else cur.insert(key);
  return cur;
}

inline bool focus_active(const FocusSet& f) { return !f.empty(); }

inline bool focus_contains(const FocusSet& f, const std::string& key) {
  return f.find(key) != f.end();
}

// The told-order positions (1-based, matching SpineScene::position) claimed by
// ANY focused lane. `lanes` is every focusable row's (key, claimed); `spine` is
// the told-order iid vector. A focused lane's claim on an iid NOT on the spine
// is dropped (the relief discipline), so the result always lines up with the
// drawn spine. Empty focus → empty (the painter then recedes nothing).
inline std::set<int> focus_positions(const FocusSet& focus,
                                     const std::vector<FocusLane>& lanes,
                                     const std::vector<std::string>& spine) {
  std::set<int> out;
  if (focus.empty()) return out;
  std::unordered_set<std::string> claimed_union;
  for (const FocusLane& ln : lanes)
    if (focus_contains(focus, ln.key))
      for (const std::string& id : ln.claimed) claimed_union.insert(id);
  if (claimed_union.empty()) return out;
  for (std::size_t i = 0; i < spine.size(); ++i)
    if (claimed_union.count(spine[i]) > 0)
      out.insert(static_cast<int>(i) + 1);
  return out;
}

}  // namespace Folio
