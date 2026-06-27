#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// TimelineSurface.hpp — the Relationship Timeline, GTK side (s80, §9.9 step 2).
//
// The visual debut of the timeline: the told-order SPINE and the structure BANDS
// above it. A thin painter over the pure projection (TimelineSpine.{hpp,cpp}) —
// it owns NO geometry of its own beyond pixel constants; the spine ordering and
// the band spans are computed in the pure unit and rendered here, exactly as
// MindMapCanvas paints over the pure MindMap layer. The same discipline:
// pure logic, thin GTK; the model is the truth, this is a projection rebuilt on
// entry (never cached across a mutation).
//
// PRESENT (built across s80–s81):
//   • structure bands ABOVE the spine — group spans from the tree, one row per
//     depth (Book ▸ Part ▸ Chapter), contiguous by construction;
//   • the told-order spine — scene cards with their told-order position badge,
//     joined by the spine axis; click a card → open that scene;
//   • relief tracks below the spine (step 3) + hover-isolate / column light (4);
//   • the subject-first sweep batch-linker (step 5);
//   • the KP strip up against the spine (step 6) — the relief of kp_id, a single
//     row (KPs partition the spine), per-KP spectrum colour, diamonds for
//     singletons, persistent connectors. THIS SLICE adds the render; the attach
//     gesture + the selection glance-list are the following step-6 sub-slices.
// NOT YET: the bi-directional KP attach gesture, the glance-list, the arc model (7).
//
// Hosted in the Editor's view-stack as the "timeline-lens" child and entered as
// a ViewMode like Map/Board — a whole-manuscript lens, not a per-node surface.
// ─────────────────────────────────────────────────────────────────────────────

#include <functional>
#include <string>

#include <gtkmm.h>

#include "TimelineSpine.hpp"
#include "TimelineTracks.hpp"   // s80 step 3 — subject tracks (the relief rows)
#include "TimelineSweep.hpp"    // s80 step 5 — the sweep planner (write-side diff)
#include "TimelineKp.hpp"       // s81 step 6 — the KP lane (relief of kp_id)

namespace Folio {

class DocumentModel;
class FolioPrefs;

class TimelineSurface : public Gtk::Box {
public:
  TimelineSurface(DocumentModel& model, FolioPrefs& prefs);

  // Re-project from the model and redraw. Cheap; called every time the Editor
  // switches INTO Timeline view (truth → projection, never cached).
  void rebuild();

  // Fired when the author activates a scene card. The Editor forwards it to the
  // app-wide navigate path (switch to Write + select the node), exactly as the
  // Map lens does — opening a scene from the timeline behaves like the sidebar.
  using OpenCallback = std::function<void(const std::string& iid)>;
  void set_open_callback(OpenCallback cb) { m_on_open = std::move(cb); }

private:
  DocumentModel& m_model;
  FolioPrefs&    m_prefs;   // s81 — resolves a KP's color_idx → spectrum hex (§9.6
                           // per-KP colour, orange fallback). Wired in at step 6;
                           // earlier slices took it for ctor parity without storing.

  // ── Widget tree ────────────────────────────────────────────────────────────
  Gtk::Overlay        m_overlay;
  Gtk::ScrolledWindow m_scroll;
  Gtk::DrawingArea    m_area;
  Gtk::Label          m_empty_hint;   // shown when the manuscript has no scenes

  // ── Lens state (presentation only; the model stays the truth) ──────────────
  SpineProjection m_proj;   // last projection (drives both draw and hit-test)
  std::vector<std::string> m_spine_iids;   // cached told-order iids (relief input)
  std::vector<TimelineTrack> m_tracks;     // s80 step 3 — subject relief rows
  // s81 step 6 — the KP lane: on-spine scenes grouped by kp_id (relief of kp_id,
  // §9.4). A SINGLE strip up against the spine — KPs partition the spine (a scene
  // carries one kp_id), so all lanes tile one row with no collision. Empty when
  // the project has no KP-tagged scenes; the strip then reserves no space (§9.4
  // "appears only when the project has KPs").
  std::vector<KpLane> m_kp_lanes;

  // s80 step 4 — transient hover focus (presentation only; nothing persisted).
  // Hover a track → isolate it (siblings dim, structure stays lit, §9.5). Hover a
  // scene card → light its whole vertical column (§9.6). Single-target by nature,
  // so it does not commit the still-open §9.8 #1 persistent focus-key model.
  int m_hover_track = -1;   // index into m_tracks, or -1
  int m_hover_col   = -1;   // 1-based lit told-order column, or -1

  // s80 step 5c — live subject-first sweep. Press-drag along a track row links
  // that row's subject across the swept span of scene columns (additive,
  // plan_sweep). Presentation state during the drag; the commit writes to the
  // model's subject_links store and re-reads.
  int    m_sweep_track    = -1;    // armed track index (-1 = no sweep in progress)
  double m_sweep_start_x  = 0.0;   // press x (drag deltas arrive relative)
  int    m_sweep_from_col = 0;     // 1-based start column
  int    m_sweep_to_col   = 0;     // 1-based current column
  bool   m_sweep_moved    = false; // a real drag occurred (vs a bare press)

  OpenCallback m_on_open;

  // ── Internals ──────────────────────────────────────────────────────────────
  void draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
  std::string scene_at(double x, double y) const;  // card hit-test → iid ("" none)
  int column_at(double x) const;        // 1-based told-order column under x, or 0
  int clamped_col(double x) const;      // column under x, clamped into [1, n] for sweep
  int track_row_at(double y) const;     // m_tracks index under y, or -1
  void commit_sweep();                  // write plan_sweep's adds into subject_links

  int content_width() const;   // X0 + n*COL + pad
  int content_height() const;  // bands + spine + KP strip + relief tracks
  int spine_top() const;       // y of the card row's top, given band_rows
  int kp_top() const;          // y of the KP strip's top (0 height when no KPs)
  int track_top() const;       // y of the first relief track row, below the KP strip
  // s81 — a KP's colour: its color_idx → the project spectrum hex (the palette
  // the materializer stamped), falling back to orange for an unstamped KP (§9.6).
  std::string kp_hex(int color_idx) const;
};

}  // namespace Folio
