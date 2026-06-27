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
#include <unordered_set>
#include <utility>
#include <vector>

#include <gtkmm.h>

#include "TimelineSpine.hpp"
#include "TimelineTracks.hpp"   // s80 step 3 — subject tracks (the relief rows)
#include "TimelineSweep.hpp"    // s80 step 5 — the sweep planner (write-side diff)
#include "TimelineKp.hpp"       // s81 step 6 — the KP lane (relief of kp_id)
#include "TimelineResources.hpp"  // s82 — the resource-rail roster (the §3 armer)

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
  // The surface is a HORIZONTAL split (s82): the resource RAIL on the left (the
  // §3 entry point — the roster of linkable subjects you arm), the spine canvas
  // on the right. The rail makes the lens a BUILDER: arm a subject here (even one
  // with no track yet), then sweep the staging row to place it on the spine.
  // s82 — the rail lives in the start pane of a draggable split so the author
  // can size it (long image captions ellipsize within whatever width is set).
  Gtk::Paned          m_paned{Gtk::Orientation::HORIZONTAL};
  Gtk::ScrolledWindow m_rail_scroll;
  Gtk::Box            m_rail_box{Gtk::Orientation::VERTICAL, 0};
  Gtk::Label          m_rail_empty;   // shown when the project has no resources
  // s82 — categories are disclosures (Gtk::Expander) like the binder; remember
  // which the author collapsed (keyed by TrackCategory enum value) so a rebuild
  // (view-entry, commit_sweep) does not reset their open/closed state.
  std::unordered_set<int> m_rail_collapsed;

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
  // s82 (Scott review) — the sweep is DIRECTIONAL: drag RIGHT of the press column
  // links the run [press..cursor]; drag LEFT unlinks the run [cursor..press].
  // Crossing back over the press column flips the operation live. Derived from
  // m_sweep_from_col vs m_sweep_to_col — no stored mode.
  // s82 — when true, the in-progress sweep targets the ARMED rail subject on the
  // staging row (vs. an existing track row). Set at drag-begin over the staging
  // row; routes commit_sweep to m_armed_iid instead of a m_tracks[] subject.
  bool   m_sweep_is_armed = false;

  // s82 — the armed rail subject (the §3 "pick Place A" half of the gesture).
  // Empty = nothing armed (no staging row). Armed from a rail row click; the
  // staging row then accepts a sweep that writes this subject across the span.
  // Cleared after a commit, on disarm (re-click), or when the subject vanishes.
  std::string   m_armed_iid;
  std::string   m_armed_label;
  TrackCategory m_armed_cat = TrackCategory::Character;

  // s82 — rail rows by subject iid, so a click can re-mark the armed row's CSS
  // without a full rail rebuild. Repopulated every rebuild() from the roster.
  std::vector<std::pair<std::string, Gtk::Widget*>> m_rail_rows;

  OpenCallback m_on_open;

  // ── Internals ──────────────────────────────────────────────────────────────
  void draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
  std::string scene_at(double x, double y) const;  // card hit-test → iid ("" none)
  int column_at(double x) const;        // 1-based told-order column under x, or 0
  int clamped_col(double x) const;      // column under x, clamped into [1, n] for sweep
  int track_row_at(double y) const;     // m_tracks index under y, or -1
  void commit_sweep();                  // write plan_sweep's adds into subject_links

  // s82 — removing a placed subject (the inverse of the sweep). A secondary
  // (right) click on a relief track row opens a context menu: remove the whole
  // subject from the timeline, or unlink it from just the scene under the cursor.
  void show_track_menu(int track_idx, double x, double y);
  void remove_subject(const std::string& subject);  // erase ALL its subject_links edges
  void unlink_subject_scene(const std::string& subject,
                            const std::string& scene_iid);  // erase one edge
  // s82 (Scott review) — modifier+click toggles ONE scene's association on the
  // row under the cursor (a track row, or the armed staging row). A claim is a
  // SET, so this is how non-contiguous membership is added/removed a cell at a
  // time; the sweep stays the span gesture.
  void toggle_cell(double x, double y);
  std::string scene_iid_at_col(int col) const;  // told-order scene iid at 1-based col ("" none)

  // s82 (Scott review) — the sweep's verb comes from the cell you drag ONTO, not
  // the direction: first-entered cell empty → ADD the span (anchor included);
  // first-entered cell already linked → REMOVE the cells you drag over (anchor
  // is the handle, left intact). One function feeds both commit and preview so
  // they never disagree. Computed from m_sweep_from_col / m_sweep_to_col.
  struct SweepRange { bool remove = false; int lo = 0; int hi = 0; bool valid = false; };
  SweepRange sweep_range(const std::unordered_set<std::string>* claimed) const;
  Gtk::PopoverMenu* m_ctx_popover = nullptr;  // managed; unparented on close

  // s82 — the resource rail. build_rail repopulates the left panel from the
  // roster (assemble_resources); arm_subject toggles which subject is armed (and
  // re-marks the rows' CSS); the armed subject's CURRENT claimed set feeds
  // plan_sweep when the staging row is swept.
  void build_rail(const std::vector<ResourceGroup>& groups);
  void arm_subject(const std::string& iid, const std::string& label,
                   TrackCategory cat);
  void disarm();
  const std::unordered_set<std::string>* armed_claimed() const;  // its track set, or nullptr

  int content_width() const;   // X0 + n*COL + pad
  int content_height() const;  // bands + spine + KP strip + staging + relief tracks
  int spine_top() const;       // y of the card row's top, given band_rows
  int kp_top() const;          // y of the KP strip's top (0 height when no KPs)
  bool staging_active() const { return !m_armed_iid.empty(); }
  int staging_top() const;     // y of the staging row top (valid when armed)
  bool over_staging(double y) const;  // is y within the staging row band
  int track_top() const;       // y of the first relief track row (below staging/KP)
  // s81 — a KP's colour: its color_idx → the project spectrum hex (the palette
  // the materializer stamped), falling back to orange for an unstamped KP (§9.6).
  std::string kp_hex(int color_idx) const;
};

}  // namespace Folio
