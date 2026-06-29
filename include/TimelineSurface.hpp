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
#include "TimelineThreads.hpp"  // s84 step 7 — the thread lane (relief of BinderNode.thread)
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

  // s89 — fired (deferred to idle) after a rail/band/KP or thread palette edit,
  // so the host can live-refresh the surfaces that read the palette but aren't
  // the timeline itself: the Inspector colour dropdowns and the sidebar swatches.
  using PaletteChangedCallback = std::function<void()>;
  void set_palette_changed_callback(PaletteChangedCallback cb) {
    m_on_palette_changed = std::move(cb);
  }

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
  // s90 — rail disclosure fold state now persists on the model
  // (DocumentModel::timeline_rail_collapsed, keyed by TrackCategory enum value;
  // -1 Story Threads, -2 Key Points) so it survives save/load as well as
  // in-session rebuilds. add_rail_disclosure reads/writes it via the model.
  // The live widgets of the disclosures built this pass, so Ctrl+Alt+click on any
  // header can expand/collapse them ALL in place (set_all_rail_disclosures);
  // repopulated each build_rail. Raw pointers — the widgets are make_managed and
  // owned by m_rail_box, valid until the next rebuild clears the vector.
  struct RailDisclosure { int key; Gtk::Revealer* rev; Gtk::Label* arrow; };
  std::vector<RailDisclosure> m_rail_disclosures;
  void set_all_rail_disclosures(bool expand);

  Gtk::Overlay        m_overlay;
  Gtk::ScrolledWindow m_scroll;
  Gtk::DrawingArea    m_area;
  Gtk::Label          m_empty_hint;   // shown when the manuscript has no scenes

  // s84 — the scene PEEK panel. The right side of the split is a vertical box:
  // [ canvas (vexpand) | peek revealer (bottom) ]. A single-click on a scene card
  // SELECTS it and reveals this panel (synopsis + metadata + links-at-a-glance)
  // rather than jumping into the editor; a double-click (or the Open button) edits.
  // §5 "scene cards on the spine": synopsis is the one WRITE surface, the rest is
  // SHOWN. The synopsis is a real Gtk::TextView (hence a widget panel, not Cairo).
  Gtk::Box            m_right_box{Gtk::Orientation::VERTICAL, 0};
  Gtk::Revealer       m_peek_revealer;
  Gtk::Label          m_peek_scene_no;   // "SCENE 5"
  Gtk::Label          m_peek_title;      // the scene title
  Gtk::Button         m_peek_open;       // "Open in editor"
  Gtk::TextView       m_peek_synopsis;   // editable — writes node->synopsis
  Glib::RefPtr<Gtk::TextBuffer> m_peek_synopsis_buf;
  Gtk::Label          m_peek_meta;       // status / KP / thread (shown)
  Gtk::Label          m_peek_links;      // links-at-a-glance readout (shown)
  std::string         m_selected_iid;    // the peeked scene ("" = none)
  bool                m_peek_loading = false;  // guard: populate must not write back

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

  // s84 step 7 — the thread band: on-spine scenes grouped by BinderNode.thread
  // (the "assigned arc", §9.12). One row per authored thread, drawn BELOW the
  // subject tracks as a DISPLAY-ONLY relief band (the KP-strip nature: not a
  // swept/hit-tested track). Empty when no scene is assigned a thread; the band
  // then reserves no space. Built in rebuild() via assemble_thread_lanes, the
  // exact mirror of the m_kp_lanes build.
  std::vector<ThreadLane> m_thread_lanes;

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
  // s86 — direct sweep ON the band/strip (parity with subject track rows, which
  // are swept in place). A drag that BEGINS on a thread band row sweeps THAT
  // thread (row-keyed); a drag on a KP strip beat sweeps THAT key point
  // (column-keyed, since KPs partition the spine into one row). -1 = not a band
  // sweep. These reuse the same m_sweep_from_col/to_col span + sweep_range verb;
  // commit_sweep resolves the target from the stored lane index. They do NOT use
  // the staging row or the arm — the band IS the surface.
  int    m_sweep_band_thread = -1;   // index into m_thread_lanes, or -1
  int    m_sweep_band_kp     = -1;   // index into m_kp_lanes, or -1

  // s82 — the armed rail subject (the §3 "pick Place A" half of the gesture).
  // Empty = nothing armed (no staging row). Armed from a rail row click; the
  // staging row then accepts a sweep that writes this subject across the span.
  // Cleared after a commit, on disarm (re-click), or when the subject vanishes.
  std::string   m_armed_iid;
  std::string   m_armed_label;
  TrackCategory m_armed_cat = TrackCategory::Character;
  // s85 — what KIND of thing is armed. A SUBJECT arm writes a subject_links edge
  // across the swept span (the s82 path); a THREAD arm SETS BinderNode.thread on
  // each swept scene (the §9.12 batch assign — single-valued, so a sweep onto a
  // cell that already holds a DIFFERENT thread overwrites it; sweeping onto cells
  // that already hold THIS thread clears them). The one gesture, two commit
  // targets — keyed here, resolved in commit_sweep/toggle_cell. The pure
  // TrackCategory stays subject-only (a thread has no §9.6 category); a thread's
  // hue comes from its palette color_idx, carried below.
  enum class ArmedKind { Subject, Thread, KeyPoint };
  ArmedKind     m_armed_kind = ArmedKind::Subject;
  int           m_armed_color_idx = 0;   // thread arm: its 1-based palette idx (hue)

  // s82 — rail rows by subject iid, so a click can re-mark the armed row's CSS
  // without a full rail rebuild. Repopulated every rebuild() from the roster.
  std::vector<std::pair<std::string, Gtk::Widget*>> m_rail_rows;

  OpenCallback m_on_open;
  PaletteChangedCallback m_on_palette_changed;            // s89
  void notify_palette_changed();                          // s89 — idle-deferred host refresh

  // ── Internals ──────────────────────────────────────────────────────────────
  void draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
  std::string scene_at(double x, double y) const;  // card hit-test → iid ("" none)
  // s84 — the scene peek panel. build_peek_panel wires the bottom widget once;
  // select_scene sets the selection + reveals + populates; populate_peek fills the
  // synopsis/metadata/links readout from the model for the given scene.
  void build_peek_panel();
  void select_scene(const std::string& iid);
  void populate_peek(const std::string& iid);
  int column_at(double x) const;        // 1-based told-order column under x, or 0
  int clamped_col(double x) const;      // column under x, clamped into [1, n] for sweep
  int track_row_at(double y) const;     // m_tracks index under y, or -1
  // s86 — band/strip hit-tests for direct sweep (parity with track_row_at).
  // thread_lane_at: m_thread_lanes index under y (the band rows), or -1.
  // over_kp_strip: is y within the KP strip row. kp_lane_at_col: the m_kp_lanes
  // index whose beat occupies a 1-based told-order column, or -1.
  int  thread_lane_at(double y) const;
  bool over_kp_strip(double y) const;
  int  kp_lane_at_col(int col) const;
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

  // s86 — the THREAD-management surface. A secondary (right) click on a rail
  // Story-Threads row opens a small "manage thread" popover (the mirror of the
  // subject track's right-click menu, but a thread carries an editable label +
  // a recolour swatch grid + a delete, so it is a widget popover, not a
  // Gio::Menu): RENAME (the entry), RECOLOUR (the palette swatches), and
  // DELETE-UNUSED (enabled only when no node references the thread; the registry
  // is the rename-safe home, so a rename/recolour is a one-field edit on the
  // ThreadDef and a delete is an erase). Parented to *this (stable across a
  // rebuild — unlike the rail rows, which are torn down), so an edit can refresh
  // the rail + band live without destroying the popover's anchor. Model edits
  // mutate the registry synchronously and defer the surface rebuild to a
  // signal_idle tick (the s24/s85 discipline — never rebuild re-entrantly from
  // inside a live widget's focus/click handler).
  Gtk::Popover* m_thread_popover = nullptr;   // managed; unparented on close
  Gtk::Popover* m_kp_popover     = nullptr;   // s86 — KP manage popover (same idiom)
  void show_thread_menu(const std::string& thread_iid, double x, double y);
  // s86 — the teaching popover for the Story Threads section (the feature is
  // not self-evident): what a thread is, the braided-book example, that it is a
  // label (not a binder object), and how to build one. Anchored to its trigger.
  void show_thread_help(Gtk::Widget* anchor);
  void rename_thread(const std::string& thread_iid, const std::string& raw_label);
  void recolour_thread(const std::string& thread_iid, int color_idx);
  void delete_thread(const std::string& thread_iid);
  // s86 — clear a SET thread off the whole timeline (the analog of remove_subject
  // / the subject track's "Remove from timeline"): clear BinderNode.thread on
  // every scene holding it, keeping the ThreadDef so it stays re-assignable. The
  // thread then reads as unused, so Delete enables.
  void remove_thread_assignments(const std::string& thread_iid);
  // s86 — how many nodes (on- AND off-spine) reference this thread. 0 = unused,
  // i.e. safe to delete with nothing orphaned; >0 gates the delete control and
  // feeds its "in use by N" hint. A model walk (the true usage), not the rail's
  // on-spine claim count, so an off-spine assignment still counts.
  int thread_usage_count(const std::string& thread_iid) const;

  // s82 — the resource rail. build_rail repopulates the left panel from the
  // roster (assemble_resources); arm_subject toggles which subject is armed (and
  // re-marks the rows' CSS); the armed subject's CURRENT claimed set feeds
  // plan_sweep when the staging row is swept.
  void build_rail(const std::vector<ResourceGroup>& groups);
  void arm_subject(const std::string& iid, const std::string& label,
                   TrackCategory cat);
  // s85 — arm a THREAD from the rail's Story Threads section (the batch-assign
  // half of §9.12.6 #4). The exact analog of arm_subject: a sweep on the staging
  // row then SETS BinderNode.thread across the span. cat is irrelevant for a
  // thread; the hue comes from color_idx (thread palette).
  void arm_thread(const std::string& iid, const std::string& label,
                  int color_idx);
  // s86 — arm a KEY POINT from the rail's Key Points section. A KP is a palette
  // SWATCH (kp_ id into m_prefs.tag_colors; "the palette IS the arc"), so a
  // sweep stamps kp_id + color_idx (swatch position) + kp_label + is_key_point
  // onto each swept scene (the never-built KP attach gesture). color_idx is the
  // swatch's 1-based palette position; the hue is kp_hex(color_idx).
  void arm_keypoint(const std::string& kp_id, const std::string& label,
                    int color_idx);
  void disarm();
  const std::unordered_set<std::string>* armed_claimed() const;  // its track/lane set, or nullptr
  // s85 — the armed thing's hue as a hex string: the §9.6 category hue for a
  // subject arm, or the thread palette colour (thread_hex) for a thread arm.
  // Used by the staging row + sweep preview so a thread sweep reads in its hue.
  std::string armed_hue() const;
  // s90 — build a binder-style collapsible disclosure for the rail (header row
  // with heading + chevron, wrapped in a Revealer) and append it to m_rail_box.
  // The WHOLE header row toggles (one GestureClick), so the arrow and the name
  // are both live — replacing the Gtk::Expander whose internal arrow node did
  // not share the title's click. Open/closed persists on the model
  // (DocumentModel::timeline_rail_collapsed) under `cat_key`, surviving save/load
  // and rebuilds. Returns the body Box for the caller to fill with rows.
  Gtk::Box* add_rail_disclosure(const std::string& heading, int cat_key);
  // s85 — append the rail's Story Threads section (registry-sourced) + the inline
  // "new thread" mint row. Called at the end of build_rail (after subject groups).
  void build_thread_rail_section();
  // s86 — the rail's Key Points section: the one place to MINT, PLACE, and
  // MANAGE a Key Point (today minting needs Preferences + the Inspector, two
  // rooms). A KP is a palette swatch, so this section is registry-sourced from
  // m_prefs.tag_colors; a mint appends a swatch (stable kp_ id, auto-coloured),
  // arming it for a spine sweep; a right-click manages it (rename / recolour /
  // delete). Because a KP IS a palette swatch, recolour/delete are palette-wide
  // and delete runs the positional reconcile (KpPalette) over every coloured
  // node — appropriate since "the palette is the arc".
  void build_kp_rail_section();
  void show_kp_menu(const std::string& kp_id, double x, double y);
  void show_kp_help(Gtk::Widget* anchor);
  void rename_kp(const std::string& kp_id, const std::string& raw_label);
  void recolour_kp(const std::string& kp_id, const std::string& hex);
  void delete_kp(const std::string& kp_id);
  // s86 — clear a KP off every BEAT (the analog of remove_thread_assignments):
  // un-stamp kp_id/color_idx/kp_label/is_key_point on each scene wearing it,
  // keeping the palette swatch so it stays available.
  void remove_kp_assignments(const std::string& kp_id);
  // s86 — how many on-spine BEATS wear this KP (is_key_point scenes with this
  // kp_id), read from m_kp_lanes. Gates the delete control + its "in use" hint.
  int kp_usage_count(const std::string& kp_id) const;

  int content_width() const;   // X0 + n*COL + pad
  int content_height() const;  // bands + spine + KP strip + staging + relief tracks
  int spine_top() const;       // y of the card row's top, given band_rows
  int kp_top() const;          // y of the KP strip's top (0 height when no KPs)
  bool staging_active() const { return !m_armed_iid.empty(); }
  int staging_top() const;     // y of the staging row top (valid when armed)
  bool over_staging(double y) const;  // is y within the staging row band
  int track_top() const;       // y of the first relief track row (below staging/KP)
  // s84 — the bottom of the spine/KP/staging/subject-track region (before the
  // thread band + bottom pad). Factored so content_height and the thread band
  // share one floor computation.
  int relief_floor() const;
  // s84 — the thread band (below the subject tracks). thread_top = the band
  // header; thread_rows_top = the first thread lane row. Valid only when
  // m_thread_lanes is non-empty (the band reserves no space otherwise).
  int thread_top() const;
  int thread_rows_top() const;
  // s81 — a KP's colour: its color_idx → the project spectrum hex (the palette
  // the materializer stamped), falling back to orange for an unstamped KP (§9.6).
  std::string kp_hex(int color_idx) const;
  // s84 — a thread's hue: its color_idx into the project palette, or a lavender
  // fallback (distinct from the KP orange fallback) when unset (color_idx 0/OOB).
  std::string thread_hex(int color_idx) const;
  // s86 — a subject's hue: its assigned palette colour when set (color_idx > 0),
  // else the §9.6 category hue. Honours a user-chosen object colour on the
  // timeline relief / rail / staging / preview.
  std::string subject_hex(int color_idx, TrackCategory cat) const;
};

}  // namespace Folio
