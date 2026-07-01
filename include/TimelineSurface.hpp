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
#include <unordered_map>
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
#include "TimelineFocus.hpp"    // s92 — persistent focus (FocusSet + the pure toggles)
#include "TimelineChrono.hpp"   // s93/s94 — world-clock re-lay + the drag-reorder write path
#include "TimelineCluster.hpp"  // s95 — temporal clusters (ClusterLayout) derived from the chrono chain

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
  // s93 — the two-lens control is drawn ON the canvas, in the gutter under the
  // "SPINE" label (the row it reorders), as two pills: Told Order / Chrono. A
  // mutually-exclusive reveal — clicking redraws the single canvas to lay the scenes
  // and the shared relief against told order or story-time. lens_toggle_geom() gives
  // the BASE-coord hit rects shared by the painter and the click handler.
  struct LensToggleGeom { double told_x, chrono_x, y, told_w, chrono_w, h; };
  LensToggleGeom lens_toggle_geom() const;
  void           draw_lens_toggle(const Cairo::RefPtr<Cairo::Context>& cr);
  bool           lens_toggle_click(double bx, double by);  // true if the click hit a pill
  // s97 — set / toggle the lens from the pill click OR the keyboard (the 't' hotkey).
  // set_lens recomputes the order and drops the per-lens selections; toggle_lens flips.
  void           set_lens(bool chrono);
  void           toggle_lens();

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
  // s93 — STORY-TIME authoring (world-clock, DESIGN_timeline.md §9.14.2). The
  // readout shows the DERIVED gap to the previous dated scene; the row authors a
  // relative gap that writes an ABSOLUTE coordinate (Option B) via
  // apply_relative_gap, so a told-order reorder never invalidates it.
  Gtk::Label          m_peek_storytime;  // derived gap to previous dated scene (shown)
  Gtk::SpinButton     m_peek_st_count;   // relative gap count
  Gtk::DropDown       m_peek_st_unit;    // unit (years … seconds)
  Gtk::DropDown       m_peek_st_dir;     // later / earlier
  // s97 — gap-mode peek (§9.14.10). The revealer body switches between the SCENE box
  // (all of the above) and the GAP box on whether a scene or a gap seam is selected.
  // The gap box authors the seam's DURATION (count + unit, written via cascade_shift
  // so the broken-axis ruler relabels) and offers Remove (clears the visual room).
  // The duration row is disabled unless both ends of the seam are dated.
  Gtk::Box            m_peek_scene_box{Gtk::Orientation::VERTICAL, 8};
  Gtk::Box            m_peek_gap_box{Gtk::Orientation::VERTICAL, 8};
  Gtk::Label          m_peek_gap_desc;   // "Between 'A' and 'B'"
  Gtk::Label          m_peek_gap_readout; // current duration / visual-room readout
  Gtk::SpinButton     m_peek_gap_count;  // duration count
  Gtk::DropDown       m_peek_gap_unit;   // unit (years … seconds)
  Gtk::Button         m_peek_gap_set{"Set"};
  Gtk::Button         m_peek_gap_remove{"Remove gap"};
  std::string         m_selected_iid;    // the peeked scene ("" = none) — the multi-sel anchor
  // s96 — multi-selection on the Chrono lens. Shift+click toggles a scene card into
  // this set (plain click selects one, clearing it); every member draws the selection
  // ring, and a drag that begins on a member drags the WHOLE set onto a cluster bar /
  // row. m_selected_iid is the peek anchor and is always present here when non-empty.
  std::unordered_set<std::string> m_multi_sel;
  bool                m_peek_loading = false;  // guard: populate must not write back

  // ── Lens state (presentation only; the model stays the truth) ──────────────
  SpineProjection m_proj;   // last projection (drives both draw and hit-test)

  // s91 — the ZOOM. An "actual zoom": draw() applies one uniform cr->scale(m_zoom)
  // over the whole surface, so cards / lanes / gaps / labels scale together and
  // stay proportional. All geometry is computed at BASE size (COL, CARD_*, lane
  // heights); only this factor changes. Clamped [kTimelineZoomMin,kTimelineZoomMax] by next_timeline_zoom.
  // PERSISTED per-project: zoom_at_viewport writes through to
  // DocumentModel::timeline_zoom (+ mark_modified) and rebuild() reads it back,
  // so the model is the single source of truth — the rail-collapse shape.
  double m_zoom = kTimelineZoomDefault;
  double m_ptr_vx = 0.0;          // s91 — pointer x RELATIVE TO THE VIEWPORT, kept
                                  // current on motion; the Ctrl+scroll zoom anchor.
                                  // (Ctrl+click passes the click's viewport x and
                                  // the keyboard the viewport centre.) Viewport-
                                  // relative so it stays valid across a wheel burst
                                  // that scrolls content under a still pointer.
  sigc::connection m_zoom_anchor_conn;  // one pending zoom-anchor scroll fix

  std::vector<std::string> m_spine_iids;   // cached told-order iids (relief input)
  // s93 — world-clock axis (DESIGN_timeline.md §9.14, step 3). m_story_axis flips
  // the spine from told order to story-time (presentation-only, like focus — not
  // serialised). m_chrono_order is ALL scenes in chronological order (undated ones
  // carry forward and hold their slot, s93); m_chrono_undated is retained but empty;
  // m_title_of / m_told_pos map an iid to its title and stable told/binder number so
  // the Chrono lens labels cards identically to Told Order (the scene looks the same
  // in both lenses). Recomputed on rebuild, on toggle, and after any story-time edit.
  bool                     m_story_axis = false;
  std::vector<std::string> m_chrono_order;
  std::vector<std::string> m_chrono_undated;
  std::unordered_map<std::string, std::string> m_title_of;
  std::unordered_map<std::string, int>         m_told_pos;   // iid -> told/binder position (1-based)

  // s95 — temporal CLUSTERS over the dated chrono prefix (DESIGN_timeline.md
  // §9.14.9). Derived in recompute_chrono: the dated scenes of m_chrono_order are
  // exactly its first D columns (chronological_order lays dated first, ascending),
  // so the chain is neighbour-subtraction over their coordinates and cluster.first/
  // .last index straight into those columns. cluster_chain emits the ⊓ brackets
  // (contiguous runs cohesive in story-time) and the signed seam gaps; the renderer
  // draws a named bar above each cluster's cards, peach for a flashback (its
  // incoming gap is negative), and words each SEAM gap's magnitude on the bracket
  // band. Presentation cache only — never serialised; recomputed with the order.
  ClusterLayout m_chrono_clusters;

  // s94 — drag-to-reorder a DATED card within the Chrono lens (DESIGN §9.14.8,
  // build slice 1). A primary drag that BEGINS on a dated scene card in the Chrono
  // lens LIFTS it and lets the author slide it to a new chronological slot; on drop
  // the moved card's coordinate is rewritten from its new neighbours (chrono_reorder
  // → the model), so the re-laid order is AUTHORED by drag rather than only derived
  // from a numeric setter. Presentation-only during the drag; the commit is the one
  // model write. Slice 1 reorders only the SET (dated) cards — the unset row +
  // drag-to-place is slice 2, the right-click-a-gap duration is slice 3. The numeric
  // peek setter (apply_story_time) is kept alongside this slice so dated scenes can
  // still be created to reorder; it is removed once slice 2 lands.
  std::string m_card_drag_iid;             // "" = no card drag in progress
  int         m_card_drag_from   = -1;     // grabbed card's dated rank (0-based); -1 if undated
  int         m_card_drag_to     = -1;     // current target rank/column; -1 when aiming at a cluster bar
  double      m_card_drag_press_x = 0.0;   // BASE-coord press x (the drag origin)
  double      m_card_drag_cur_x   = 0.0;   // BASE-coord current cursor x (ghost follows)
  double      m_card_drag_press_y = 0.0;   // s96 — BASE-coord press y
  double      m_card_drag_cur_y   = 0.0;   // s96 — BASE-coord current cursor y (for the bracket-band hit)
  bool        m_card_drag_moved   = false; // a real drag occurred (vs a bare press)
  // s96 — DnD assign: a card (dated OR undated) can be dragged onto a ⊓ cluster bar to
  // be ADDED to that cluster; an undated card dropped on the dated row is placed there
  // directly (drag-to-place). m_card_drag_is_dated records whether the grabbed card was
  // already dated (reorder) vs undated (place/append); m_card_drag_cluster is the
  // cluster bracket under the cursor mid-drag (-1 = over the row, the column target).
  bool        m_card_drag_is_dated = false;
  int         m_card_drag_cluster  = -1;
  bool        m_card_drag_unschedule = false;   // s96 — cursor over the "out of cluster" strip
  // s96 — the iids being dragged, in chrono order. {anchor} for a single-card drag;
  // the whole multi-selection when the press lands on a selected card. Routes the drop
  // to the batch assign / place when size > 1.
  std::vector<std::string> m_card_drag_set;

  // s97 — gap authoring (DESIGN_timeline.md §9.14.10). m_chrono_leads is the per-rank
  // extra visual room before each m_chrono_order column (BinderNode.chrono_gap of the
  // node at that rank), rebuilt in recompute_chrono; col_cx_view / the lens-aware
  // column_at read it so a kept-open gap pushes its downstream cards over and the draw
  // and hit-tests stay in lockstep. m_gap_sel is the selected seam (1..N-1, the gap
  // before that rank) or -1; dragging the gap bar adjusts the right-hand scene's
  // chrono_gap live. The peek's gap-mode time editor (the duration label via
  // cascade_shift) is the close-out slice.
  std::vector<double> m_chrono_leads;
  int    m_gap_sel            = -1;      // selected seam (right-rank index 1..N-1), -1 none
  bool   m_gap_drag_active    = false;   // a gap-bar drag is in progress
  double m_gap_drag_base_lead = 0.0;     // the seam's chrono_gap at drag start
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
  // s98 — the hovered SCENE and the hovered resource lane (thread / kp), so a
  // hover can cross-highlight the scenes a resource touches in its own colour. At
  // most one of {card, subject, thread, kp} is active at a time (set in the motion
  // handler). The cache below is recomputed per frame from whichever is live.
  std::string m_hover_iid;              // the hovered scene card iid, or empty
  int m_hover_thread = -1;              // index into m_thread_lanes, or -1
  int m_hover_kp     = -1;              // index into m_kp_lanes, or -1
  std::unordered_set<std::string> m_hi_iids;  // scenes to outline this frame
  Gdk::RGBA                       m_hi_color; // the outline colour (resource / accent)
  bool                            m_hi_on = false;
  void compute_hover_hi();              // fill the cache from the live hover

  // s92 — PERSISTENT focus (§4 / §9.8 #1 / §9.12.5). Unlike the transient hover
  // above, a focused set STICKS until the author unpins it: pick a relief row
  // (subject track / thread lane / KP) and walk the spine with everything else
  // dimmed, to SEE the gaps and draw the missing links. Focus keys on ANY row
  // uniformly — all three are (label, colour, scene-set) — via a kind-namespaced
  // key (fk_subject/fk_thread/fk_keypoint in the .cpp), so the three id
  // namespaces never collide in one set. Plain click = single-focus that row
  // (re-click clears); Shift+click = toggle it in the spotlighted set; Esc
  // clears. While focus is active it OVERRIDES the hover-isolate (the pin wins);
  // hovering a scene card to light its column still works. Presentation-only
  // this slice (NOT serialised — the cross-session question is an open call,
  // s92 handoff); pruned to live rows on every rebuild, the m_selected_iid
  // peek-sync shape. m_focus_positions caches focus_positions() so draw() reads
  // it instead of recomputing per frame — refreshed on a focus change / rebuild.
  FocusSet      m_focus;            // namespaced keys currently focused ({} = off)
  std::set<int> m_focus_positions;  // told-order cols any focused row claims (cache)

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
  // s93 — world-clock chronological view (§9.14.3). draw_story_axis renders the
  // story-time re-lay (isolated so the told-order draw is untouched); scene_at_story
  // hit-tests its cards + undated tray; recompute_chrono rebuilds the order caches.
  void draw_story_axis(const Cairo::RefPtr<Cairo::Context>& cr);
  // s95 — the ⊓ cluster brackets band above the Chrono cards (§9.14.9). Reads
  // m_chrono_clusters; draws a bar+legs over each cluster's column span with the
  // opener's cluster_label on a tab, peach-tinted for flashback clusters. `top` is
  // the card row top (spine_top()); the band lives in the headroom above it.
  void draw_cluster_brackets(const Cairo::RefPtr<Cairo::Context>& cr, int top);
  std::string scene_at_story(double x, double y) const;
  void recompute_chrono();
  // s94 — drag-to-reorder helpers (DESIGN §9.14.8 slice 1). dated_scenes() pulls the
  // dated prefix of m_chrono_order as (iid, coordinate) pairs in ascending order (the
  // input chrono_reorder expects); dated_rank_of resolves an iid to its 0-based rank
  // among the dated cards (-1 if undated/absent); commit_card_reorder applies the
  // pure chrono_reorder writes for the in-flight drag back onto the model.
  std::vector<ChronoDated> dated_scenes() const;
  int  dated_rank_of(const std::string& iid) const;
  void commit_card_reorder();
  // s93 — subject-track relief shared by both axes (positions from compute_relief
  // over the passed order); apply_focus gates told-order focus/hover dimming.
  void draw_subject_tracks(const Cairo::RefPtr<Cairo::Context>& cr,
                           const std::vector<std::string>& order,
                           double ttop, bool apply_focus);
  void draw_kp_strip(const Cairo::RefPtr<Cairo::Context>& cr,
                     const std::vector<std::string>& order,
                     int kt, bool apply_focus, bool draw_pins);
  void draw_thread_band(const Cairo::RefPtr<Cairo::Context>& cr,
                        const std::vector<std::string>& order,
                        int hdr_y, int rtop, bool apply_focus);
  // s97 — the reusable "active drag extent": one dashed-bordered rounded ghost span
  // over columns lo..hi at row-centre cy, in `hue` (or the error red when `remove`).
  // Factored from the four s80/s82/s86 sweep-preview blocks so the relationship sweep
  // can draw it in BOTH lenses, and so the gap drag (slice 2) wears the same border.
  // Columns map through col_cx, so it lands right under whichever order is laid out.
  void draw_sweep_extent(const Cairo::RefPtr<Cairo::Context>& cr,
                         int lo, int hi, double cy,
                         bool remove, const Gdk::RGBA& hue, double fill_a = 0.30);
  // s97 — draw whichever live sweep preview is active (track row / staging / thread
  // band / KP strip) using the current lens's relief tops. Called from the told draw
  // AND draw_story_axis, so a relationship edit shows its extent box in Chrono too.
  void draw_active_sweep_preview(const Cairo::RefPtr<Cairo::Context>& cr);
  // s97 — the armed staging row (the add-a-resource-line lane). Factored from the told
  // draw so the Chrono draw can show it too; positions through col_cx_view + the passed
  // order so it lands under the chrono columns. Drawn only while a rail subject is armed.
  void draw_staging_row(const Cairo::RefPtr<Cairo::Context>& cr,
                        const std::vector<std::string>& order);
  // s93 — ONE scene-card painter (square, border, selection ring, position badges,
  // title) shared by the Told Order and Chrono draws, so a scene looks identical in
  // either lens. `order_badge` is the position in the CURRENT view (upper-left);
  // `told_badge` is the stable told/manuscript number (upper-right, both lenses).
  // The caller draws the axis dot / ruler stem around it.
  void draw_scene_card(const Cairo::RefPtr<Cairo::Context>& cr,
                       const std::string& iid, const std::string& title,
                       int order_badge, int told_badge, double cardx, int top);
  // s84 — the scene peek panel. build_peek_panel wires the bottom widget once;
  // select_scene sets the selection + reveals + populates; populate_peek fills the
  // synopsis/metadata/links readout from the model for the given scene.
  void build_peek_panel();
  void select_scene(const std::string& iid);
  void populate_peek(const std::string& iid);
  // s93 — STORY-TIME authoring (§9.14.2): Set writes an absolute coordinate from
  // the relative gap typed against the previous dated scene; Clear marks undated.
  void apply_story_time();
  void clear_story_time();
  int column_at(double x) const;        // 1-based told-order column under x, or 0
  int clamped_col(double x) const;      // column under x, clamped into [1, n] for sweep
  // s97 — lens-aware horizontal geometry. col_cx_view is the card centre for view
  // column k: ordinal col_cx in Told, variable-spaced (col_cx + accumulated chrono
  // leads) in Chrono so an open gap pushes downstream cards over. lead_through is the
  // cumulative lead up to and including rank k (0 in Told). The relief / ruler / card
  // draws and the inverse column_at all read these so an open gap can't desync them.
  double col_cx_view(int k) const;
  double lead_through(int k) const;
  // s97 — gap authoring on the Chrono time bar. ruler_y_band: is base-coord y on the
  // ruler row (the seam-click band, Chrono only). gap_seam_at: the seam (1..N-1)
  // nearest base-coord x on that band, or -1. select_gap reveals the gap editor;
  // clear_gap_selection drops it. set_gap_lead writes the right-rank scene's
  // chrono_gap (mark_modified only on change) and rebuilds the leads.
  bool ruler_y_band(double y) const;
  int  gap_seam_at(double x) const;
  void select_gap(int seam);
  void clear_gap_selection();
  void set_gap_lead(int seam, double lead_px);
  // s97 — gap-mode peek. populate_gap fills the gap editor for the selected seam (and
  // gates the duration row on both ends being dated); apply_gap_time writes the typed
  // duration via cascade_shift (the ruler relabels); remove_gap clears the seam's
  // visual room (chrono_gap -> 0) and deselects.
  void populate_gap(int seam);
  void apply_gap_time();
  void remove_gap();
  int track_row_at(double y) const;     // m_tracks index under y, or -1
  // s86 — band/strip hit-tests for direct sweep (parity with track_row_at).
  // thread_lane_at: m_thread_lanes index under y (the band rows), or -1.
  // over_kp_strip: is y within the KP strip row. kp_lane_at_col: the m_kp_lanes
  // index whose beat occupies a 1-based told-order column, or -1.
  int  thread_lane_at(double y) const;
  bool over_kp_strip(double y) const;
  int  kp_lane_at_col(int col) const;
  void commit_sweep();                  // write plan_sweep's adds into subject_links

  // s92 — persistent-focus helpers. focus_key_at resolves the relief row under a
  // BASE-coord point to its kind-namespaced focus key ("" if not a focusable
  // row); recompute_focus_positions refreshes the m_focus_positions cache from
  // m_focus + the current lanes; prune_focus drops keys whose row vanished (a
  // rebuild may delete a subject/thread/KP), the m_selected_iid peek-sync shape;
  // clear_focus unpins everything (Esc / programmatic). focus_lanes builds the
  // uniform (key, claimed) adapter over all three lane vectors for the pure read.
  std::string focus_key_at(double x, double y) const;
  std::vector<FocusLane> focus_lanes() const;
  void recompute_focus_positions();
  void prune_focus();
  void clear_focus();

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

  // s96 — CLUSTER AUTHORING (DESIGN_timeline.md §9.14.9). Two gestures on the Chrono
  // lens: (1) click a ⊓ bracket to NAME its cluster — writes cluster_label on the
  // cluster's opener via a small entry popover; (2) right-click a scene card to ADD
  // it to a cluster — since clusters are DERIVED from story-time, "assign" means
  // place the scene's coordinate INSIDE that cluster's span (one step at the
  // cluster's own pace, which by construction can't reach the next seam), so the
  // next recompute_chrono folds it into that run. cluster_bracket_at hit-tests the
  // bracket band; show_cluster_name_editor / show_cluster_assign_menu raise the
  // popovers; assign_scene_to_cluster / start_new_cluster_with do the model write.
  Gtk::Popover* m_cluster_name_popover = nullptr;   // managed; unparented on close
  int  cluster_bracket_at(double bx, double by) const;   // cluster index under a bracket-band point, or -1
  void show_cluster_name_editor(std::size_t cluster_index, double x, double y);
  void show_cluster_assign_menu(const std::string& scene_iid, double x, double y);
  void assign_scene_to_cluster(const std::string& scene_iid, std::size_t cluster_index);
  void start_new_cluster_with(const std::string& scene_iid);
  // s96 — drag-to-place: drop an undated card on the dated row at chronological rank
  // `to` (0..N) → date it there (chrono_insert_coord midpoint). The "added directly" path.
  void place_undated_at(const std::string& scene_iid, std::size_t to);
  // s96 — multi-select + batch DnD. toggle_multi_select flips a card's membership
  // (Shift+click); assign_scenes_to_cluster / place_scenes_at are the group writes
  // (append the set into a cluster, or place it contiguously on the row at rank `to`).
  void toggle_multi_select(const std::string& iid);
  void assign_scenes_to_cluster(const std::vector<std::string>& scene_iids, std::size_t cluster_index);
  void place_scenes_at(const std::vector<std::string>& scene_iids, std::size_t to);
  // s96 — drag a card out of its cluster: clears story_time on each, so they leave
  // the dated run and return to the undated tray (the inverse of a cluster assign).
  void unschedule_scenes(const std::vector<std::string>& scene_iids);

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

  int content_width() const;   // BASE width  (X0 + n*COL + pad) — scaled at use
  int content_height() const;  // BASE height (bands+spine+strip+staging+tracks)
  // s91 — base column geometry (fixed COL / card width); the uniform cr->scale in
  // draw() does the zooming. col_left(k)=x0+k*COL; col_cx=its centre; card_w is
  // the fixed base card width. The zoom helpers: zoom_at_viewport scales m_zoom
  // (clamped by next_timeline_zoom) keeping viewport pixel `vx` fixed; zoom_in/out/reset
  // are the bare +/-/0 keyboard idiom (Map parity); sync_content_size pushes the
  // BASE size × m_zoom to the DrawingArea (which lives in widget/event space).
  int  col_left(int k) const;
  int  col_cx(int k) const;
  int  card_w() const;
  void   zoom_at_viewport(double vx, double factor);
  double viewport_center_vx() const;
  void   zoom_in();
  void   zoom_out();
  void   reset_zoom();
  void   sync_content_size();
  int spine_top() const;       // y of the card row's top, given band_rows
  // s96 — the y just below the card row where the relief stack (KP / tracks /
  // thread) begins. Told order stacks directly under the cards; the Chrono lens
  // inserts the broken-axis ruler (the "time row") and the undated tray between
  // the cards and the relief, so the origin drops by their height. EVERY relief
  // geometry helper anchors here, so the draw and the row hit-tests agree in both
  // lenses (a told-order origin in Chrono put every row one off — s96 fix).
  int relief_origin() const;
  int kp_top() const;          // y of the KP strip's top (0 height when no KPs)
  bool staging_active() const { return !m_armed_iid.empty(); }
  int staging_top() const;     // y of the staging row top (valid when armed)
  bool over_staging(double y) const;  // is y within the staging row band
  int track_top() const;       // y of the first relief track row (staging-independent now)
  // s97 — bottom of the KP + subject-track stack, NOT counting the staging row or the
  // thread band. The staging lane sits just BELOW this (so a newly-armed line previews
  // where the committed track will land — below the last resource, both lenses), and
  // relief_floor adds the staging height when armed.
  int tracks_floor() const;
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
