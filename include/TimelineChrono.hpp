#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TimelineChrono.hpp — re-lay the told-order spine by story-time (the world-clock
// axis, DESIGN_timeline.md §9.14, build-order step 3). Pure, GTK-free,
// header-only. The chronological REARRANGEMENT Scott asked for ("it helps set the
// mind to the period better"): flashbacks spring back to where they happen,
// parallel storylines line up.
//
// ONE input fact per scene: the OPTIONAL absolute story-time coordinate (the
// Option-B BinderNode field). The ordinal is DERIVED here — sort by coordinate,
// rank falls out (§9.14.1). This step is ORDINAL/even-spacing only; proportional
// spacing + the broken-axis ruler are step 4.
//
// UNDATED scenes (the §9.14.4 fork, RE-DECIDED s93b): a scene stays ON the spine,
// but DATED scenes lay out first in time order and UNDATED ("not yet placed") scenes
// trail in told order. The point is that setting a date is VISIBLE: a dated scene
// moves into the timed sequence the moment you place it, instead of sitting still
// (the carry-forward rule made a single date invisible, since an ordinal position is
// a rank, and rank only changes once two dated scenes conflict). With nothing dated
// the order is still identical to told; as you date scenes they join the timed run
// up front; the relationship relief stays attached because every scene is on spine.
//
// TIES (two scenes at the SAME coordinate — simultaneous, "meanwhile" — and the
// whole undated trail) keep their told order via a STABLE sort, so a deliberate
// ordering of concurrent or unplaced scenes survives the flip.
//
// Header-only inline (the TimelineFocus / TimelineClock precedent): pure ordering,
// no DocumentModel, no CMakeLists entry. The GTK side builds the input vector from
// the spine + BinderNode coordinates and feeds `chrono` to the same relief engine
// the told-order spine uses (the §9.14.5 substrate reuse).
//
// s94 — this file now also carries the WRITE path (§9.14.8, the drag authoring
// model, slice 1): chronological_order DERIVES order from coordinates; chrono_reorder
// (below) does the inverse — the author drags a dated card to a new slot and the
// card's coordinate is rewritten from its new neighbours so the next
// chronological_order() lays it there. Order is authored by drag; the coordinate
// falls out. Still pure and GTK-free; the surface owns the gesture and the model write.
// ─────────────────────────────────────────────────────────────────────────────

#include <algorithm>
#include <climits>
#include <cstddef>
#include <string>
#include <vector>

namespace Folio {

// One told-order on-spine scene as the chronological sort sees it. `time` is valid
// only when `dated` (the BinderNode has_story_time / story_time pair).
struct ChronoScene {
  std::string iid;
  bool        dated = false;
  long long   time  = 0;
};

struct ChronoOrder {
  std::vector<std::string> chrono;    // ALL scenes: dated ascending by coordinate, then undated in told order
  std::vector<std::string> undated;   // empty since s93 (undated scenes hold their spine slot); kept for the host API
};

// Re-lay `told` (index = told position) by story-time. Every scene stays on the
// spine: dated scenes ascend by coordinate; undated scenes trail in told order.
// Stable, so equal coordinates — and the undated trail — preserve told order.
inline ChronoOrder chronological_order(const std::vector<ChronoScene>& told) {
  ChronoOrder out;
  const std::size_t N = told.size();
  std::vector<long long> eff(N);
  for (std::size_t i = 0; i < N; ++i)
    eff[i] = told[i].dated ? told[i].time : LLONG_MAX;   // undated sort after every dated scene
  std::vector<std::size_t> idx(N);
  for (std::size_t i = 0; i < N; ++i) idx[i] = i;
  std::stable_sort(idx.begin(), idx.end(),
                   [&eff](std::size_t a, std::size_t b) { return eff[a] < eff[b]; });
  out.chrono.reserve(N);
  for (std::size_t i : idx) out.chrono.push_back(told[i].iid);
  return out;   // out.undated stays empty — undated scenes trail on the spine, not in a tray
}

// ─── Authoring: drag-to-reorder within the SET row (§9.14.8, the drag model, ────
// build slice 1). The READ path above derives order FROM coordinates; this is the
// WRITE path — the author drags a dated card to a new chronological slot and the
// card's coordinate is rewritten so the next chronological_order() sorts it there.
// Order becomes authored by drag; the coordinate falls out of the new neighbours.
//
// "writing the moved card's coordinate from its new neighbours" (the handoff): set
// it to the integer MIDPOINT of the two scenes it now sits between, so ONLY the
// moved card changes and the rest of the spacing is left alone. At the ends it steps
// out by `step`. When the neighbours are too close for an integer to fit between
// them (delta < 2, or they tie), the whole dated run is re-spaced to even `step`
// multiples in the new order so the drop can never land on a tie — the one case that
// rewrites every coordinate (`renormalized`).

// One dated scene as the reorder sees it: iid + its absolute coordinate.
struct ChronoDated {
  std::string iid;
  long long   coord = 0;
};

// A coordinate to store back on a node (the moved card alone, or every dated card
// when the run is renormalized).
struct ChronoMove {
  std::string iid;
  long long   coord = 0;
};

struct ChronoReorder {
  std::vector<std::string> order;       // dated iids in the NEW chronological order
  std::vector<ChronoMove>  writes;      // coordinates to persist (moved card, or all)
  bool                     renormalized = false;
};

// Move dated[from] to chronological rank `to` (0-based in the final arrangement of
// the same scenes). `dated` MUST be in ascending-coordinate order (the order the
// author sees). Returns the new iid order and the coordinate write(s). `from == to`
// (or an out-of-range index that resolves to it) yields the order with NO writes.
inline ChronoReorder chrono_reorder(const std::vector<ChronoDated>& dated,
                                    std::size_t from, std::size_t to,
                                    long long step = 86400 /* one nominal day */) {
  ChronoReorder r;
  const std::size_t N = dated.size();
  if (N == 0) return r;
  if (from >= N) from = N - 1;
  if (to   >= N) to   = N - 1;

  // New order: pull `from` out, splice it back in at `to`.
  std::vector<std::size_t> idx;
  idx.reserve(N);
  for (std::size_t i = 0; i < N; ++i)
    if (i != from) idx.push_back(i);
  idx.insert(idx.begin() + static_cast<std::ptrdiff_t>(to), from);

  r.order.reserve(N);
  for (std::size_t i : idx) r.order.push_back(dated[i].iid);

  if (from == to) return r;   // nothing moved → no coordinate to rewrite

  const bool has_left  = (to > 0);
  const bool has_right = (to + 1 < N);
  const long long left  = has_left  ? dated[idx[to - 1]].coord : 0;
  const long long right = has_right ? dated[idx[to + 1]].coord : 0;

  bool      renorm = false;
  long long coord  = 0;
  if (has_left && has_right) {
    if (right - left >= 2) coord = left + (right - left) / 2;   // integer midpoint fits
    else                   renorm = true;                       // too close / tie → re-space all
  } else if (has_left) {
    coord = left + step;     // dropped at the end — step past the last
  } else if (has_right) {
    coord = right - step;    // dropped at the front — step before the first
  } else {
    coord = 0;               // the only dated scene — anchor the clock start
  }

  if (renorm) {
    r.renormalized = true;
    r.writes.reserve(N);
    for (std::size_t p = 0; p < N; ++p)
      r.writes.push_back(ChronoMove{dated[idx[p]].iid, static_cast<long long>(p) * step});
  } else {
    r.writes.push_back(ChronoMove{dated[from].iid, coord});
  }
  return r;
}

// s96 — drag-to-place a NEW (previously undated) scene at chronological rank `to`
// (0..N) among `dated` (ascending by coordinate). Returns the coordinate to store.
// The insertion mirror of chrono_reorder's neighbour rule: midpoint between the two
// scenes it lands between, stepping out by `step` at either end; with nothing dated
// it anchors the clock at 0. Pure, GTK-free — the surface owns the model write.
inline long long chrono_insert_coord(const std::vector<ChronoDated>& dated,
                                     std::size_t to, long long step = 86400 /* one nominal day */) {
  const std::size_t N = dated.size();
  if (N == 0)  return 0;                          // first dated scene anchors the clock
  if (to == 0) return dated[0].coord - step;      // before the first
  if (to >= N) return dated[N - 1].coord + step;  // after the last
  const long long left = dated[to - 1].coord, right = dated[to].coord;
  if (right - left >= 2) return left + (right - left) / 2;   // integer midpoint fits
  return left + step;                              // neighbours too tight → step out (rare)
}

// s96 — drag-to-place a GROUP of `count` scenes at chronological rank `to` among
// `dated` (ascending), returning `count` coordinates IN ORDER, contiguous between the
// neighbours (evenly spread when they fit, else stepped out past the left neighbour).
// The multi-select batch mirror of chrono_insert_coord. Pure, GTK-free.
inline std::vector<long long> chrono_insert_span(const std::vector<ChronoDated>& dated,
                                                 std::size_t to, std::size_t count,
                                                 long long step = 86400 /* one nominal day */) {
  std::vector<long long> out;
  if (count == 0) return out;
  out.reserve(count);
  const std::size_t N = dated.size();
  const long long K = static_cast<long long>(count);
  if (N == 0) {                                   // nothing dated — anchor the run at 0, step apart
    for (long long j = 0; j < K; ++j) out.push_back(j * step);
    return out;
  }
  if (to == 0) {                                  // before the first: end just under it
    const long long base = dated[0].coord - K * step;
    for (long long j = 0; j < K; ++j) out.push_back(base + j * step);
    return out;
  }
  if (to >= N) {                                  // after the last: step out past it
    const long long base = dated[N - 1].coord;
    for (long long j = 0; j < K; ++j) out.push_back(base + (j + 1) * step);
    return out;
  }
  const long long left = dated[to - 1].coord, right = dated[to].coord;
  const long long span = right - left;
  if (span >= K + 1) {                            // evenly spread between the neighbours
    for (long long j = 0; j < K; ++j) out.push_back(left + span * (j + 1) / (K + 1));
  } else {                                        // too tight — step out past the left (rare)
    for (long long j = 0; j < K; ++j) out.push_back(left + (j + 1) * step);
  }
  return out;
}

// s97 — gap-duration authoring (slice 2). Set the gap at a seam to an exact
// duration and shift the whole downstream block to honour it, preserving the
// block's internal spacing (the locked s95 cascade rule). `dated` is ascending by
// coordinate; `seam_k` is the RIGHT-hand index of the seam (1..N-1) — the gap sits
// between dated[seam_k-1] and dated[seam_k]. Returns the coordinate to persist for
// dated[seam_k .. N-1]; every one moves by the same delta so interior gaps are
// untouched. The first scene (rank 0) has no left neighbour, so seam_k==0 (or any
// out-of-range seam) yields no writes — you can't set a gap before the clock start.
// `new_gap_seconds` is taken literally; a value that would push dated[seam_k] at or
// before its left neighbour is the caller's policy to refuse (the surface clamps to
// a >0 gap). Pure, GTK-free — the surface owns the model write + rebuild.
inline std::vector<ChronoMove> cascade_shift(const std::vector<ChronoDated>& dated,
                                             std::size_t seam_k,
                                             long long new_gap_seconds) {
  std::vector<ChronoMove> writes;
  const std::size_t N = dated.size();
  if (seam_k == 0 || seam_k >= N) return writes;   // no left anchor / out of range
  const long long target = dated[seam_k - 1].coord + new_gap_seconds;
  const long long delta  = target - dated[seam_k].coord;
  if (delta == 0) return writes;                   // already that gap — no-op
  writes.reserve(N - seam_k);
  for (std::size_t i = seam_k; i < N; ++i)
    writes.push_back(ChronoMove{dated[i].iid, dated[i].coord + delta});
  return writes;
}

// s97 — variable-spacing chrono geometry (gap authoring, §9.14.10). The Chrono axis
// is ordinally spaced (each card `col` apart) PLUS an optional per-rank LEAD gap
// drawn before that card; a lead pushes that card and everything downstream over,
// so a gap stays open persistently. These helpers give the card-centre x for a rank
// and invert a pixel x back to a column or the nearest seam, so the draw and the
// hit-tests share one geometry. `leads[k]` is the extra space before rank k (0 if
// none); ranks past the array end carry no lead. All GTK-free, sandbox-tested.

// Cumulative lead added through rank k inclusive (rank k's own lead shifts it too).
inline double chrono_lead_through(const std::vector<double>& leads, std::size_t k) {
  double s = 0.0;
  const std::size_t lim = (k + 1 < leads.size()) ? (k + 1) : leads.size();
  for (std::size_t i = 0; i < lim; ++i) s += leads[i];
  return s;
}

// Left edge of rank k's column.
inline double chrono_col_left(const std::vector<double>& leads, double x0,
                              double col, std::size_t k) {
  return x0 + static_cast<double>(k) * col + chrono_lead_through(leads, k);
}

// Centre x of rank k's card.
inline double chrono_col_cx(const std::vector<double>& leads, double x0,
                            double col, std::size_t k) {
  return chrono_col_left(leads, x0, col, k) + col / 2.0;
}

// Invert with clamp: nearest column 1..n for a pixel x (x in a lead/seam region
// clamps to the adjacent card). 0 only when n == 0. Used by the relationship sweep
// so a chrono column resolves to the card actually under the cursor.
inline int chrono_clamped_col(const std::vector<double>& leads, double x0,
                              double col, std::size_t n, double x) {
  if (n == 0) return 0;
  if (x < chrono_col_left(leads, x0, col, 0)) return 1;
  for (std::size_t k = 0; k < n; ++k)
    if (x < chrono_col_left(leads, x0, col, k) + col)
      return static_cast<int>(k) + 1;
  return static_cast<int>(n);
}

// Strict invert: the 1-based column whose COL-wide cell contains x, else 0 (x sits
// in a lead/seam region or off the ends).
inline int chrono_col_at(const std::vector<double>& leads, double x0,
                         double col, std::size_t n, double x) {
  for (std::size_t k = 0; k < n; ++k) {
    const double left = chrono_col_left(leads, x0, col, k);
    if (x >= left && x < left + col) return static_cast<int>(k) + 1;
  }
  return 0;
}

// The seam (1..n-1, between rank s-1 and s) nearest a pixel x — the one whose
// boundary midpoint is closest. 0 if fewer than two ranks. Used on the time-bar
// y-band where any click picks a gap to author.
inline int chrono_seam_nearest(const std::vector<double>& leads, double x0,
                               double col, std::size_t n, double x) {
  if (n < 2) return 0;
  int best = 1;
  double best_d = -1.0;
  for (std::size_t s = 1; s < n; ++s) {
    const double mid = (chrono_col_cx(leads, x0, col, s - 1)
                        + chrono_col_cx(leads, x0, col, s)) / 2.0;
    const double d = (x > mid) ? (x - mid) : (mid - x);
    if (best_d < 0.0 || d < best_d) { best_d = d; best = static_cast<int>(s); }
  }
  return best;
}

}  // namespace Folio
