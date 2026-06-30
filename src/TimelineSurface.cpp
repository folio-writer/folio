// ─────────────────────────────────────────────────────────────────────────────
// TimelineSurface.cpp — the Relationship Timeline painter (s80, §9.9 step 2).
//
// THIN by contract: the ordering and the band spans come from project_spine();
// this file turns that projection into bands + cards + axis, and forwards a card
// click. The pixel constants below are the geometry locked by the s80 mock
// (MOCK_timeline_spine.svg) — band height, column width, card size.
//
// Also defines spine_input_from_manuscript() — the model → DTO adapter declared
// in TimelineSpine.hpp. It lives HERE (a gtkmm TU) rather than in the pure unit,
// the same separation StoryGraph::edges_from_backlinks uses: the pure projection
// stays sandbox-compilable, the model-touching read lives where DocumentModel.hpp
// is already pulled in.
// ─────────────────────────────────────────────────────────────────────────────

#include "TimelineSurface.hpp"

#include <algorithm>
#include <cmath>
#include <memory>

#include "DocumentModel.hpp"
#include "FolioPrefs.hpp"
#include "Iid.hpp"          // s86 — make_iid(IidKind::KeyPoint) for KP mint
#include "TimelineClock.hpp"  // s93 — world-clock: gap_phrase / apply_relative_gap
#include "TimelineChrono.hpp" // s93 — world-clock: chronological_order (the re-lay)
#include "KpPalette.hpp"    // s86 — palette_remap / apply_palette_remap (delete reconcile)
#include "StoryGraph.hpp"   // s80 step 3 — edges_from_backlinks (subject adapter)
#include "TimelineRelief.hpp"  // compute_relief — the per-track renderer input

namespace Folio {

namespace {

// ── Locked geometry (s80 mocks) ──────────────────────────────────────────────
constexpr int LEFT_PAD      = 16;   // breathing room before the gutter
constexpr int GUTTER        = 132;  // row-label column (band labels + track names)
constexpr int COL           = 72;   // px per scene column at base size (zoom 1.0).
                                    // s91 — zoom is a UNIFORM cr->scale(z) over the
                                    // whole surface, NOT a change to COL, so this is
                                    // a fixed base constant again; col_left/col_cx
                                    // compute base coords and the transform scales.
constexpr int kCardWBase    = 56;   // scene-card width at base size (card_w()).
constexpr int CARD_H        = 44;
constexpr int BAND_H        = 24;
constexpr int BAND_GAP      = 5;
constexpr int BAND_TO_SPINE = 10;
constexpr int TOP_PAD       = 12;
constexpr int BOT_PAD       = 16;
constexpr int BADGE_R       = 8;
// KP strip (s81 step 6) — a single row up against the spine, between the cards
// and the relief tracks. KPs partition the spine, so all lanes share this row.
constexpr int KP_PAD        = 12;   // gap from the spine cards down to the strip
constexpr int KP_H          = 26;   // strip height
constexpr int KP_DIAMOND_R  = 8;    // singleton diamond half-extent
constexpr int KP_BAR_H      = 14;   // KP bar thickness
// relief tracks (s80 step 3)
constexpr int TRACK_PAD     = 12;   // gap from the spine cards to the first track
constexpr int TRACK_H       = 26;   // track row height
constexpr int TRACK_GAP     = 6;    // vertical gap between tracks
constexpr int BAR_H         = 12;   // relief bar thickness
// s84 — the thread band (step 7): a display-only relief band below the subject
// tracks. Its own header row + one TRACK_H row per authored thread.
constexpr int THREAD_PAD     = 16;  // gap above the thread band
constexpr int THREAD_HEADER_H = 18; // height of the "STORY THREADS" header row
// s82 — the staging row: when a rail subject is armed, a single dashed lane in
// its hue appears above the relief tracks; sweeping it places the armed subject
// on the spine (the §3 build gesture). Reserves space only while armed.
constexpr int STAGE_PAD     = 12;   // gap above the staging row
constexpr int STAGE_H       = 28;   // staging row height
constexpr int RAIL_W        = 184;  // resource-rail panel width

// Category → HUE (§9.6), the fixed semantic hues — NOT theme chrome, so they are
// the design's exact literals rather than palette tokens. (Focus-brightness and
// group-badge are the later colour channels; this step uses hue only.)
const char* category_hue(TrackCategory c) {
  switch (c) {
    case TrackCategory::Character: return "#89b4fa";  // blue
    case TrackCategory::Place:     return "#a6e3a1";  // green
    case TrackCategory::Reference: return "#cba6f7";  // mauve
    case TrackCategory::Image:     return "#fab387";  // peach
  }
  return "#cdd6f4";
}

// Category → rail section header (§9.6 order). Plural — a roster heading.
const char* category_heading(TrackCategory c) {
  switch (c) {
    case TrackCategory::Character: return "Characters";
    case TrackCategory::Place:     return "Places";
    case TrackCategory::Reference: return "References";
    case TrackCategory::Image:     return "Images";
  }
  return "Resources";
}

inline int x0() { return LEFT_PAD + GUTTER; }            // left edge of column 0
// s91 — col_left / col_cx are TimelineSurface members; x0() stays a free helper
// (LEFT_PAD + GUTTER). All are BASE coords — the uniform cr->scale(m_zoom) in
// draw() does the zooming, so these don't depend on the zoom.

// Pull a palette colour with a sensible literal fallback (mirrors MindMapCanvas).
Gdk::RGBA themed(Gtk::Widget& w, const char* name, const char* fallback) {
  Gdk::RGBA c;
  if (w.get_style_context()->lookup_color(name, c)) return c;
  c.set(fallback);
  return c;
}

// Band fill darkens slightly with depth, so nesting reads as recession. Neutral
// on purpose — category hue is reserved for the relief tracks (§9.6), never the
// structure bands.
const char* band_token(int depth) {
  switch (depth) {
    case 0:  return "adw_overlay2";   // shallowest (Part) — lightest
    case 1:  return "adw_overlay";    // Chapter
    default: return "adw_surface2";   // deeper
  }
}

void rounded_rect(const Cairo::RefPtr<Cairo::Context>& cr,
                  double x, double y, double w, double h, double r) {
  cr->begin_new_sub_path();
  cr->arc(x + w - r, y + r,     r, -M_PI / 2, 0);
  cr->arc(x + w - r, y + h - r, r, 0, M_PI / 2);
  cr->arc(x + r,     y + h - r, r, M_PI / 2, M_PI);
  cr->arc(x + r,     y + r,     r, M_PI, 3 * M_PI / 2);
  cr->close_path();
}

void set_src(const Cairo::RefPtr<Cairo::Context>& cr, const Gdk::RGBA& c, double a = 1.0) {
  cr->set_source_rgba(c.get_red(), c.get_green(), c.get_blue(), a);
}

// s92 — namespaced focus keys. A subject iid (chr_/plc_/ref_/ast_), a thread key
// (a thr_ iid) and a KP id (a palette-swatch id) live in different worlds, but
// they share ONE focus set, so each is kind-prefixed before it enters — the
// s87/s91 specificity rule applied up front (a generic shared id would otherwise
// silently collide). The pure layer treats the result as an opaque string.
std::string fk_subject (const std::string& iid) { return "S:" + iid; }
std::string fk_thread  (const std::string& key) { return "T:" + key; }
std::string fk_keypoint(const std::string& id)  { return "K:" + id;  }

}  // namespace

// ── The model → DTO adapter (declared in TimelineSpine.hpp) ──────────────────
// A trivial recursive copy of (iid, title, group?, children) off the Manuscript
// binder tree. Keeps the pure unit free of DocumentModel.hpp (which pulls GTK).
namespace {
SpineInputNode to_input(const BinderNode& n) {
  SpineInputNode in;
  in.iid = n.iid;
  in.title = n.title;
  in.is_group = binder_kind_is_group(n.kind);
  if (in.is_group) {
    in.children.reserve(n.children.size());
    for (const auto& c : n.children) in.children.push_back(to_input(c));
  }
  return in;
}
}  // namespace

std::vector<SpineInputNode> spine_input_from_manuscript(const DocumentModel& model) {
  std::vector<SpineInputNode> roots;
  const auto& src = model.root(Section::Manuscript);
  roots.reserve(src.size());
  for (const auto& n : src) roots.push_back(to_input(n));
  return roots;
}

// ── Resource-rail candidate collector (s82) ──────────────────────────────────
// Walk a section tree, emitting every LEAF (a linkable subject) as a candidate
// in the given category; groups are descended, not listed. Mirrors the spine
// walk but keeps the leaves, not the order.
namespace {
void collect_resource_leaves(const std::vector<BinderNode>& nodes,
                             TrackCategory cat,
                             std::vector<ResourceCandidate>& out) {
  for (const BinderNode& n : nodes) {
    if (binder_kind_is_group(n.kind)) {
      collect_resource_leaves(n.children, cat, out);
    } else if (!n.iid.empty()) {
      out.push_back(ResourceCandidate{n.iid, n.title, cat});
    }
  }
}
}  // namespace

// ── Surface ──────────────────────────────────────────────────────────────────

TimelineSurface::TimelineSurface(DocumentModel& model, FolioPrefs& prefs)
    : Gtk::Box(Gtk::Orientation::HORIZONTAL, 0), m_model(model), m_prefs(prefs) {
  set_hexpand(true);
  set_vexpand(true);
  set_name("timeline-surface");

  // ── Resource rail (s82, §3) — the left-panel armer ─────────────────────────
  m_rail_box.set_name("timeline-rail-box");
  m_rail_box.add_css_class("timeline-rail");
  m_rail_empty.set_text("No resources yet \u2014 add characters, places or references in the binder.");
  m_rail_empty.set_name("timeline-rail-empty");
  m_rail_empty.add_css_class("dim-label");
  m_rail_empty.set_wrap(true);
  m_rail_empty.set_xalign(0.0f);
  m_rail_empty.set_margin(10);
  m_rail_empty.set_visible(false);
  m_rail_box.append(m_rail_empty);

  m_rail_scroll.set_name("timeline-rail-scroll");
  m_rail_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  m_rail_scroll.set_child(m_rail_box);
  m_rail_scroll.set_vexpand(true);

  m_area.set_name("timeline-area");
  m_area.set_focusable(true);   // s91 — so the bare +/- zoom keys land on the canvas
  m_area.set_draw_func(sigc::mem_fun(*this, &TimelineSurface::draw));

  m_scroll.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
  m_scroll.set_child(m_area);
  m_scroll.set_hexpand(true);
  m_scroll.set_vexpand(true);

  m_empty_hint.set_text("No scenes yet \u2014 the spine reads the Manuscript told order.");
  m_empty_hint.set_name("timeline-empty-hint");
  m_empty_hint.add_css_class("dim-label");
  m_empty_hint.set_halign(Gtk::Align::CENTER);
  m_empty_hint.set_valign(Gtk::Align::CENTER);
  m_empty_hint.set_visible(false);

  m_overlay.set_child(m_scroll);
  m_overlay.add_overlay(m_empty_hint);
  m_overlay.set_hexpand(true);
  m_overlay.set_vexpand(true);

  // s84 — the right side of the split is a vertical box: the canvas (vexpand) above,
  // the scene peek panel (a revealer) below. The lens toggle (Told Order / Chrono) is
  // painted on the canvas in the SPINE gutter, not a widget here. build_peek_panel()
  // fills the revealer's child once; select_scene reveals + populates it on a click.
  build_peek_panel();
  m_right_box.set_hexpand(true);
  m_right_box.set_vexpand(true);
  m_right_box.append(m_overlay);
  m_right_box.append(m_peek_revealer);

  // s82 — adjustable split: rail (start) | spine canvas (end). The rail keeps
  // its width when the window resizes (the canvas takes the slack) but the
  // author can drag the divider; it can be dragged narrow but not collapsed.
  m_paned.set_name("timeline-split");
  m_paned.set_start_child(m_rail_scroll);
  m_paned.set_end_child(m_right_box);
  m_paned.set_resize_start_child(false);
  m_paned.set_shrink_start_child(false);
  m_paned.set_resize_end_child(true);
  m_paned.set_shrink_end_child(true);
  m_paned.set_position(RAIL_W);
  m_paned.set_hexpand(true);
  m_paned.set_vexpand(true);
  append(m_paned);

  // Click a scene card. s84 — single click SELECTS the scene and reveals the
  // peek panel (synopsis + metadata + links) at the bottom; a DOUBLE click (or
  // the panel's Open button) navigates into the editor, the way a single click
  // used to. s91 — modifier routing: CTRL+click zooms IN, CTRL+SHIFT+click zooms
  // OUT (about the click point — the Map's spatial-zoom muscle memory); ALT+click
  // TOGGLES one scene's association on the row under the cursor (the precise
  // non-contiguous edit; the sweep remains the span gesture). s82 had Ctrl as a
  // second toggle binding for WMs that grab Alt+click; s91 reassigns Ctrl to zoom,
  // so the toggle is Alt-only (keyboard +/- and Ctrl+scroll also zoom).
  auto click = Gtk::GestureClick::create();
  click->set_button(GDK_BUTTON_PRIMARY);
  Gtk::GestureClick* clickp = click.get();   // raw: m_area owns the controller
  click->signal_released().connect([this, clickp](int n_press, double x, double y) {
    const Gdk::ModifierType st = clickp->get_current_event_state();
    const bool ctrl  = (st & Gdk::ModifierType::CONTROL_MASK) != Gdk::ModifierType{};
    const bool shift = (st & Gdk::ModifierType::SHIFT_MASK)   != Gdk::ModifierType{};
    const bool alt   = (st & Gdk::ModifierType::ALT_MASK)     != Gdk::ModifierType{};
    // s91 — Ctrl+click zooms IN, Ctrl+Shift+click zooms OUT, about the click
    // point — the Map's spatial-zoom muscle memory (a trackpad-friendly path,
    // since the Ctrl+scroll gesture isn't reliable on every device). This takes
    // Ctrl+click away from the cell-toggle, which is now ALT+click only.
    if (ctrl) {
      auto hadj = m_scroll.get_hadjustment();
      const double vx = x - (hadj ? hadj->get_value() : 0.0);   // click → viewport pixel
      zoom_at_viewport(vx, shift ? (1.0 / 1.5) : 1.5);
      return;
    }
    // s91 — event coords are in SCALED content space; divide by m_zoom back into
    // the base geometry the hit-tests use.
    const double bx = x / m_zoom, by = y / m_zoom;
    if (lens_toggle_click(bx, by)) return;   // s93 — Told Order / Chrono pills in the SPINE gutter
    if (alt) { toggle_cell(bx, by); return; }   // precise non-contiguous link edit
    const std::string iid = scene_at(bx, by);
    if (iid.empty()) {
      // s92 — a bare PLAIN/SHIFT click on a relief ROW toggles PERSISTENT focus
      // on that row (vs the transient hover-isolate, which springs back). Plain
      // click = single-focus the row (re-click clears); Shift+click = toggle it
      // in the spotlighted set (§9.12.5). Skipped when this release ended a sweep
      // DRAG (m_sweep_moved, set during drag_update before the release) so a
      // sweep never doubles as a focus toggle; Ctrl/Alt already returned above.
      if (!m_sweep_moved) {
        const std::string key = focus_key_at(bx, by);
        if (!key.empty()) {
          m_focus = shift ? focus_toggle_spotlight(m_focus, key)
                          : focus_toggle_primary(m_focus, key);
          recompute_focus_positions();
          m_area.queue_draw();
        }
      }
      return;
    }
    if (n_press >= 2) {                 // double-click → edit
      if (m_on_open) m_on_open(iid);
    } else {                            // single-click → peek
      select_scene(iid);
    }
  });
  m_area.add_controller(click);

  // s82 — secondary (right) click on a relief track row opens its context menu
  // (remove the subject from the timeline / unlink the scene under the cursor).
  auto sec = Gtk::GestureClick::create();
  sec->set_button(GDK_BUTTON_SECONDARY);
  Gtk::GestureClick* secp = sec.get();   // raw: m_area owns the controller (no cycle)
  sec->signal_pressed().connect([this, secp](int /*n*/, double x, double y) {
    // s86 — right-click a thread band row or a KP strip beat to manage it (the
    // same popovers as the rail rows). show_thread_menu/show_kp_menu parent to
    // *this, so translate the m_area coords into *this's space.
    // s91 — hit-test in BASE space (event coords are scaled by the zoom); the
    // popover anchor uses the RAW coords (it positions in widget space).
    const double bx = x / m_zoom, by = y / m_zoom;
    const int tlane = thread_lane_at(by);
    if (tlane >= 0 && tlane < static_cast<int>(m_thread_lanes.size())) {
      secp->set_state(Gtk::EventSequenceState::CLAIMED);
      double rx = 0.0, ry = 0.0;
      m_area.translate_coordinates(*this, x, y, rx, ry);
      show_thread_menu(m_thread_lanes[static_cast<std::size_t>(tlane)].thread_key, rx, ry);
      return;
    }
    if (over_kp_strip(by)) {
      const int klane = kp_lane_at_col(column_at(bx));
      if (klane >= 0 && klane < static_cast<int>(m_kp_lanes.size())) {
        secp->set_state(Gtk::EventSequenceState::CLAIMED);
        double rx = 0.0, ry = 0.0;
        m_area.translate_coordinates(*this, x, y, rx, ry);
        show_kp_menu(m_kp_lanes[static_cast<std::size_t>(klane)].kp_id, rx, ry);
        return;
      }
    }
    const int trk = track_row_at(by);
    if (trk < 0) return;
    secp->set_state(Gtk::EventSequenceState::CLAIMED);
    show_track_menu(trk, x, y);   // RAW coords: positions a popover in widget space
  });
  m_area.add_controller(sec);

  // s80 step 4 — hover focus. Motion sets the isolated track / lit column;
  // leaving clears both. Presentation only; a redraw reflects the new focus.
  auto motion = Gtk::EventControllerMotion::create();
  motion->signal_enter().connect([this](double, double) {
    // s91 — focus the canvas when the pointer enters, so the bare +/-/0 zoom
    // keys land here rather than on whatever the view-open focused first (the
    // Map uses the same trick). Guarded so it never steals focus mid-typing.
    if (auto* win = dynamic_cast<Gtk::Window*>(get_root())) {
      Gtk::Widget* foc = win->get_focus();
      if (foc && (dynamic_cast<Gtk::Editable*>(foc) || dynamic_cast<Gtk::TextView*>(foc)))
        return;
    }
    if (!m_area.has_focus()) m_area.grab_focus();
  });
  motion->signal_motion().connect([this](double x, double y) {
    // s91 — m_ptr_vx is the pointer's VIEWPORT pixel (raw event x minus scroll),
    // the Ctrl+scroll zoom anchor. Hover hit-tests need BASE coords, so divide
    // the event coords by m_zoom (events are in scaled content space).
    auto hadj = m_scroll.get_hadjustment();
    m_ptr_vx = x - (hadj ? hadj->get_value() : 0.0);
    const double bx = x / m_zoom, by = y / m_zoom;
    int trk = -1, col = -1;
    const int st = spine_top();
    if (by >= st && by <= st + CARD_H && !scene_at(bx, by).empty()) {
      col = column_at(bx);           // over a scene card → light its column
    } else {
      trk = track_row_at(by);        // over a track row → isolate it
    }
    if (trk != m_hover_track || col != m_hover_col) {
      m_hover_track = trk;
      m_hover_col = col;
      m_area.queue_draw();
    }
  });
  motion->signal_leave().connect([this]() {
    if (m_hover_track != -1 || m_hover_col != -1) {
      m_hover_track = -1;
      m_hover_col = -1;
      m_area.queue_draw();
    }
  });
  m_area.add_controller(motion);

  // s91 — Ctrl+scroll zoom (the COL knob). Matches the Map's spatial-zoom gesture
  // (CustomMindMapCanvas: Ctrl+scroll, factor 1.10/step) so the zoom muscle memory
  // is uniform across the spatial surfaces — the same uniformity principle that
  // drove the s90 collapse grammar. Ctrl gates it; a plain (un-Ctrl) scroll is
  // NOT handled here (return false) so it falls through to the ScrolledWindow as
  // ordinary pan. Zoom scales m_zoom (the uniform cr->scale factor) and resizes
  // the canvas; the geometry is drawn at base size under the transform, so a
  // relayout + redraw is "free" — no rebuild(), no model read.
  auto scroll = Gtk::EventControllerScroll::create();
  scroll->set_flags(Gtk::EventControllerScroll::Flags::BOTH_AXES);
  Gtk::EventControllerScroll* scrollp = scroll.get();   // raw: m_area owns it
  scroll->signal_scroll().connect(
      [this, scrollp](double /*dx*/, double dy) {
        const bool ctrl =
            (scrollp->get_current_event_state() & Gdk::ModifierType::CONTROL_MASK)
            != Gdk::ModifierType{};
        if (!ctrl) return false;                 // plain scroll → ScrolledWindow pans
        const double factor = (dy < 0.0) ? 1.10 : (dy > 0.0 ? 1.0 / 1.10 : 1.0);
        if (factor != 1.0) zoom_at_viewport(m_ptr_vx, factor);
        return true;                             // claimed: no pan while zooming
      }, false);
  m_area.add_controller(scroll);

  // s91 — keyboard zoom (the Map's bare +/-/0 idiom; trackpad-friendly, no wheel
  // needed). Attached to the surface BOX so it fires when focus is anywhere in
  // the timeline, but skips while the user is typing in a field (the peek
  // synopsis, a rail mint entry) so '+'/'-'/'0' type normally there. Bare keys
  // (no modifier) match the Map; main-row AND keypad variants are bound so a
  // laptop without a numpad still has them.
  auto key = Gtk::EventControllerKey::create();
  key->signal_key_pressed().connect(
      [this](guint kv, guint, Gdk::ModifierType) {
        if (auto* win = dynamic_cast<Gtk::Window*>(get_root())) {
          Gtk::Widget* foc = win->get_focus();
          if (foc && (dynamic_cast<Gtk::Editable*>(foc) ||
                      dynamic_cast<Gtk::TextView*>(foc)))
            return false;                       // typing — let the field have it
        }
        switch (kv) {
          case GDK_KEY_plus: case GDK_KEY_equal:
          case GDK_KEY_KP_Add:        zoom_in();    return true;
          case GDK_KEY_minus: case GDK_KEY_underscore:
          case GDK_KEY_KP_Subtract:   zoom_out();   return true;
          case GDK_KEY_0: case GDK_KEY_KP_0:
                                      reset_zoom(); return true;
          // s92 — Esc unpins persistent focus (the deliberate "back to whole
          // graph" key); a no-op when nothing is focused, so it doesn't swallow
          // Esc from anything else that wants it in that case.
          case GDK_KEY_Escape:
            if (focus_active(m_focus)) { clear_focus(); return true; }
            return false;
          default: return false;
        }
      }, false);
  add_controller(key);

  // s80 step 5c / s82 — the subject-first sweep. A primary drag that BEGINS on a
  // track row (or the armed staging row) sweeps a span of scene columns; on
  // release the verb is resolved by sweep_range (drag onto empty → add the span;
  // drag onto a linked cell → remove the cells swept). A bare press (no movement)
  // adds the single anchor cell only once a real drag is detected.
  auto drag = Gtk::GestureDrag::create();
  drag->set_button(GDK_BUTTON_PRIMARY);
  drag->signal_drag_begin().connect([this](double x, double y) {
    // s91 — events are in scaled content space; work in BASE coords. m_sweep_start_x
    // is stored in base so the base drag offset (ox/m_zoom) in drag_update lines up.
    const double bx = x / m_zoom, by = y / m_zoom;
    // s82 — a sweep that begins on the STAGING row arms the rail subject across
    // the swept span (the §3 build gesture); otherwise a sweep on an existing
    // track row edits that subject (s80). Staging takes priority when armed.
    if (staging_active() && over_staging(by)) {
      m_sweep_is_armed = true;
      m_sweep_track = -1;
      m_sweep_band_thread = -1;
      m_sweep_band_kp = -1;
      m_sweep_start_x = bx;
      m_sweep_from_col = clamped_col(bx);
      m_sweep_to_col = m_sweep_from_col;
      m_sweep_moved = false;
      m_area.queue_draw();
      return;
    }
    m_sweep_is_armed = false;
    // s86 — direct sweep on the thread band (row-keyed) or the KP strip
    // (column-keyed), parity with sweeping a subject track row. Checked before
    // the subject-track check; they live in disjoint y-bands so there is no clash.
    const int tlane = thread_lane_at(by);
    if (tlane >= 0) {
      m_sweep_band_thread = tlane;
      m_sweep_band_kp = -1;
      m_sweep_track = -1;
      m_sweep_start_x = bx;
      m_sweep_from_col = clamped_col(bx);
      m_sweep_to_col = m_sweep_from_col;
      m_sweep_moved = false;
      m_area.queue_draw();
      return;
    }
    if (over_kp_strip(by)) {
      const int klane = kp_lane_at_col(clamped_col(bx));
      if (klane >= 0) {   // press must land on an existing beat (no KP identity on an empty cell)
        m_sweep_band_kp = klane;
        m_sweep_band_thread = -1;
        m_sweep_track = -1;
        m_sweep_start_x = bx;
        m_sweep_from_col = clamped_col(bx);
        m_sweep_to_col = m_sweep_from_col;
        m_sweep_moved = false;
        m_area.queue_draw();
        return;
      }
    }
    m_sweep_band_thread = -1;
    m_sweep_band_kp = -1;
    const int trk = track_row_at(by);
    if (trk < 0) { m_sweep_track = -1; return; }   // only sweeps from a track row
    m_sweep_track = trk;
    m_sweep_start_x = bx;
    m_sweep_from_col = clamped_col(bx);
    m_sweep_to_col = m_sweep_from_col;
    m_sweep_moved = false;
    m_area.queue_draw();
  });
  drag->signal_drag_update().connect([this](double ox, double oy) {
    if (m_sweep_track < 0 && !m_sweep_is_armed
        && m_sweep_band_thread < 0 && m_sweep_band_kp < 0) return;
    if (std::abs(ox) > 2.0 || std::abs(oy) > 2.0) m_sweep_moved = true;  // raw px threshold
    // s91 — the drag offset is in scaled space; m_sweep_start_x is base, so use
    // the base offset (ox / m_zoom) for the base-space column hit-test.
    m_sweep_to_col = clamped_col(m_sweep_start_x + ox / m_zoom);
    m_area.queue_draw();
  });
  drag->signal_drag_end().connect([this](double /*ox*/, double /*oy*/) {
    const bool band = (m_sweep_band_thread >= 0 || m_sweep_band_kp >= 0);
    if ((m_sweep_track >= 0 || m_sweep_is_armed || band) && m_sweep_moved) commit_sweep();
    m_sweep_track = -1;
    m_sweep_is_armed = false;
    m_sweep_band_thread = -1;
    m_sweep_band_kp = -1;
    m_sweep_moved = false;
    m_area.queue_draw();
  });
  m_area.add_controller(drag);
}

// s91 — base column geometry. col_left/col_cx compute BASE coords off the fixed
// COL; the uniform cr->scale(m_zoom) in draw() turns these into on-screen pixels.
// x0() is the free helper (LEFT_PAD + GUTTER).
int TimelineSurface::col_left(int k) const { return x0() + k * COL; }
int TimelineSurface::col_cx(int k)   const { return col_left(k) + COL / 2; }

// s91 — base geometry: card width is fixed at the mock size; the uniform
// cr->scale(m_zoom) in draw() does ALL the resizing, so cards/lanes/gaps/labels
// scale together and stay proportional (an "actual zoom", not a per-element tweak).
int TimelineSurface::card_w() const { return kCardWBase; }

int TimelineSurface::column_at(double x) const {
  const int n = static_cast<int>(m_proj.spine.size());
  if (n == 0 || x < x0() || x >= x0() + n * COL) return 0;
  return 1 + static_cast<int>((x - x0()) / COL);
}

int TimelineSurface::clamped_col(double x) const {
  const int n = static_cast<int>(m_proj.spine.size());
  if (n == 0) return 0;
  if (x < x0()) return 1;
  if (x >= x0() + n * COL) return n;
  return 1 + static_cast<int>((x - x0()) / COL);
}

void TimelineSurface::commit_sweep() {
  // s86 — direct band/strip sweeps resolve their target from the lane under the
  // press (not the arm / not a track), then write the same single-valued field
  // as the armed path. Handled first; they return before the subject logic.
  if (m_sweep_band_thread >= 0) {
    if (m_sweep_band_thread >= static_cast<int>(m_thread_lanes.size())) return;
    const ThreadLane& ln = m_thread_lanes[static_cast<std::size_t>(m_sweep_band_thread)];
    const std::string tid = ln.thread_key;
    const SweepRange sr = sweep_range(&ln.claimed);
    if (!sr.valid) return;
    bool changed = false;
    for (int p = sr.lo; p <= sr.hi; ++p) {
      const std::string scene_iid = scene_iid_at_col(p);
      if (scene_iid.empty()) continue;
      BinderNode* sn = m_model.find_node_by_iid(scene_iid);
      if (!sn) continue;
      if (sr.remove) {
        if (sn->thread == tid) { sn->thread.clear(); changed = true; }
      } else if (sn->thread != tid) { sn->thread = tid; changed = true; }
    }
    if (changed) m_model.mark_modified();
    rebuild();
    return;
  }
  if (m_sweep_band_kp >= 0) {
    if (m_sweep_band_kp >= static_cast<int>(m_kp_lanes.size())) return;
    const KpLane& ln = m_kp_lanes[static_cast<std::size_t>(m_sweep_band_kp)];
    const std::string kid = ln.kp_id, klabel = ln.label;
    const int kidx = ln.color_idx;
    const SweepRange sr = sweep_range(&ln.claimed);
    if (!sr.valid) return;
    bool changed = false;
    for (int p = sr.lo; p <= sr.hi; ++p) {
      const std::string scene_iid = scene_iid_at_col(p);
      if (scene_iid.empty()) continue;
      BinderNode* sn = m_model.find_node_by_iid(scene_iid);
      if (!sn) continue;
      const bool holds_this = sn->is_key_point && sn->kp_id == kid;
      if (sr.remove) {
        if (holds_this) {
          sn->kp_id.clear(); sn->kp_label.clear();
          sn->color_idx = 0; sn->is_key_point = false; sn->pin = false;
          changed = true;
        }
      } else if (!holds_this) {
        sn->kp_id = kid; sn->color_idx = kidx; sn->kp_label = klabel;
        sn->is_key_point = true; changed = true;
      }
    }
    if (changed) m_model.mark_modified();
    rebuild();
    return;
  }

  // s82 — the subject + its current claimed set come from one of two places:
  // the armed rail subject (staging-row sweep — may have NO track yet, the
  // builder case), or an existing track row (s80 edit case).
  std::string subject;
  std::unordered_set<std::string> claimed;
  if (m_sweep_is_armed) {
    if (m_armed_iid.empty()) return;
    subject = m_armed_iid;
    if (const auto* c = armed_claimed()) claimed = *c;  // else stays empty (no track)
  } else {
    if (m_sweep_track < 0 || m_sweep_track >= static_cast<int>(m_tracks.size())) return;
    const TimelineTrack& tk = m_tracks[static_cast<std::size_t>(m_sweep_track)];
    subject = tk.iid;
    claimed = tk.claimed;
  }

  // s82 (Scott review) — the verb comes from the cell you drag ONTO (see
  // sweep_range): drag onto empty → ADD the span (anchor included); drag onto a
  // linked cell → REMOVE the cells you swept (anchor left intact). Not direction.
  const SweepRange sr = sweep_range(&claimed);
  if (!sr.valid) {
    if (m_sweep_is_armed) disarm();   // consume the arm (the gesture happened)
    return;
  }

  bool changed = false;
  for (int p = sr.lo; p <= sr.hi; ++p) {
    const std::string scene_iid = scene_iid_at_col(p);
    if (scene_iid.empty()) continue;
    BinderNode* sn = m_model.find_node_by_iid(scene_iid);
    if (!sn) continue;

    // s85 — a THREAD arm SETS a single-valued field, not a set membership edge.
    // ADD = set node.thread to the armed thread (OVERWRITING any other thread the
    // scene held — a thread is single-valued, §9.12). REMOVE = clear it, but only
    // if it currently IS this thread (so removing thread T never touches a scene
    // that holds a different thread U swept across). A SUBJECT arm keeps the s82
    // edge semantics (a claim is a SET; add/remove only this subject).
    if (m_sweep_is_armed && m_armed_kind == ArmedKind::Thread) {
      if (sr.remove) {
        if (sn->thread == subject) { sn->thread.clear(); changed = true; }
      } else {
        if (sn->thread != subject) { sn->thread = subject; changed = true; }
      }
      continue;
    }

    // s86 — a KEY POINT arm stamps the scene's KP fields (kp_id + color_idx +
    // kp_label + is_key_point). Like a thread, a KP is single-valued — a scene
    // carries one kp_id (KPs partition the spine) — so ADD overwrites any other
    // KP. REMOVE fully clears the beat (only if the cell currently holds THIS
    // KP), mirroring the thread clear.
    if (m_sweep_is_armed && m_armed_kind == ArmedKind::KeyPoint) {
      const bool holds_this = sn->is_key_point && sn->kp_id == subject;
      if (sr.remove) {
        if (holds_this) {
          sn->kp_id.clear(); sn->kp_label.clear();
          sn->color_idx = 0; sn->is_key_point = false; sn->pin = false;
          changed = true;
        }
      } else if (!holds_this) {
        sn->kp_id        = subject;
        sn->color_idx    = m_armed_color_idx;
        sn->kp_label     = m_armed_label;
        sn->is_key_point = true;
        changed = true;
      }
      continue;
    }

    auto& v = sn->subject_links;
    if (sr.remove) {
      const auto before = v.size();
      v.erase(std::remove(v.begin(), v.end(), subject), v.end());
      changed = changed || (v.size() != before);
    } else {
      // dedupe: a subject is linked to a scene at most once in the store.
      if (std::find(v.begin(), v.end(), subject) == v.end()) {
        v.push_back(subject);
        changed = true;
      }
    }
  }
  if (changed) m_model.mark_modified();
  // s86 — persistent arm for the BAND-only kinds. A SUBJECT clears after a sweep
  // because it gains an editable TRACK ROW to slide/extend afterward; a THREAD or
  // KEY POINT has only a display-only band, so its single edit surface is the
  // armed staging row — keep it (and the arm) alive after the sweep so the author
  // can extend, contract, and lay further non-contiguous blocks without
  // re-arming (the braid case). Re-click the armed row to disarm.
  if (m_sweep_is_armed && m_armed_kind == ArmedKind::Subject)
    m_armed_iid.clear();   // placed — clear the arm before rebuild
  rebuild();   // re-read edges → tracks recompute; the bar appears live
}

// s82 (Scott review) — resolve the swept columns + verb from the press/cursor
// columns and the subject's current claim. The cell first ENTERED past the
// anchor decides: already-linked → REMOVE the entered cells (anchor is the
// handle, kept); empty → ADD the whole span (anchor included). A press with no
// column change adds the anchor cell.
TimelineSurface::SweepRange
TimelineSurface::sweep_range(const std::unordered_set<std::string>* claimed) const {
  SweepRange r;
  const int from = m_sweep_from_col, to = m_sweep_to_col;
  if (from <= 0 || to <= 0) return r;          // invalid
  if (from == to) { r.remove = false; r.lo = from; r.hi = from; r.valid = true; return r; }

  const int first_entered = (to > from) ? from + 1 : from - 1;
  bool first_on = false;
  if (claimed) {
    const std::string sid = scene_iid_at_col(first_entered);
    first_on = !sid.empty() && claimed->count(sid) > 0;
  }
  if (first_on) {                              // REMOVE the cells dragged over
    r.remove = true;
    r.lo = (to > from) ? from + 1 : to;        // exclude the anchor (the handle)
    r.hi = (to > from) ? to       : from - 1;
  } else {                                     // ADD the whole span (incl. anchor)
    r.remove = false;
    r.lo = std::min(from, to);
    r.hi = std::max(from, to);
  }
  r.valid = (r.hi >= r.lo);
  return r;
}

// s82 — erase EVERY subject_links edge that targets this subject, across all
// nodes (on-spine and off). The inverse of a full sweep; "remove from timeline."
void TimelineSurface::remove_subject(const std::string& subject) {
  if (subject.empty()) return;
  // Collect the scene iids that link the subject (const scan), then erase via the
  // mutable handle (all_node_ptrs() is const-only).
  std::vector<std::string> scenes;
  for (const BinderNode* n : m_model.all_node_ptrs()) {
    if (!n) continue;
    if (std::find(n->subject_links.begin(), n->subject_links.end(), subject)
        != n->subject_links.end())
      scenes.push_back(n->iid);
  }
  bool changed = false;
  for (const std::string& scene_iid : scenes) {
    BinderNode* sn = m_model.find_node_by_iid(scene_iid);
    if (!sn) continue;
    auto& v = sn->subject_links;
    const auto before = v.size();
    v.erase(std::remove(v.begin(), v.end(), subject), v.end());
    changed = changed || (v.size() != before);
  }
  if (!changed) return;
  if (m_armed_iid == subject) m_armed_iid.clear();  // armed row may have vanished
  m_model.mark_modified();
  rebuild();
}

// s82 — erase a single subject↔scene edge (the scene under a right-click).
void TimelineSurface::unlink_subject_scene(const std::string& subject,
                                           const std::string& scene_iid) {
  if (subject.empty() || scene_iid.empty()) return;
  BinderNode* sn = m_model.find_node_by_iid(scene_iid);
  if (!sn) return;
  auto& v = sn->subject_links;
  const auto before = v.size();
  v.erase(std::remove(v.begin(), v.end(), subject), v.end());
  if (v.size() == before) return;   // edge was not present
  m_model.mark_modified();
  rebuild();
}

// s82 — the told-order scene iid at a 1-based column, or "" if out of range.
std::string TimelineSurface::scene_iid_at_col(int col) const {
  if (col <= 0) return {};
  for (const auto& s : m_proj.spine)
    if (s.position == col) return s.iid;
  return {};
}

// s82 (Scott review) — toggle ONE scene's association for the row under the
// cursor: a track row (its subject) or the armed staging row (the armed
// subject, the builder case — lets a non-contiguous claim be built a cell at a
// time before any track exists). Add if absent, remove if present.
void TimelineSurface::toggle_cell(double x, double y) {
  const int col = column_at(x);
  if (col <= 0) return;
  const std::string scene_iid = scene_iid_at_col(col);
  if (scene_iid.empty()) return;
  BinderNode* sn = m_model.find_node_by_iid(scene_iid);
  if (!sn) return;

  // Resolve the toggle target from whatever lane the click landed on: the
  // staging row (the arm), a subject track row, a thread band row, or a KP strip
  // beat. Threads/KPs are single-valued; subjects are a set edge. KP stamping
  // needs the colour + label, which come from the arm (staging) or the lane (band).
  std::string subject;
  bool thread_toggle = false;
  bool kp_toggle     = false;
  int  kp_cidx = 0;
  std::string kp_lbl;

  if (staging_active() && over_staging(y)) {
    subject = m_armed_iid;
    thread_toggle = (m_armed_kind == ArmedKind::Thread);
    kp_toggle     = (m_armed_kind == ArmedKind::KeyPoint);
    kp_cidx = m_armed_color_idx;
    kp_lbl  = m_armed_label;
  } else if (const int trk = track_row_at(y); trk >= 0) {
    subject = m_tracks[static_cast<std::size_t>(trk)].iid;
  } else if (const int tl = thread_lane_at(y); tl >= 0) {
    // s86 — modifier-click a thread band row toggles that thread on this scene.
    subject = m_thread_lanes[static_cast<std::size_t>(tl)].thread_key;
    thread_toggle = true;
  } else if (over_kp_strip(y)) {
    // s86 — modifier-click a KP strip beat toggles that key point on this scene.
    const int kl = kp_lane_at_col(col);
    if (kl < 0) return;
    const KpLane& kln = m_kp_lanes[static_cast<std::size_t>(kl)];
    subject   = kln.kp_id;
    kp_cidx   = kln.color_idx;
    kp_lbl    = kln.label;
    kp_toggle = true;
  } else {
    return;
  }
  if (subject.empty()) return;

  if (thread_toggle) {
    if (sn->thread == subject) sn->thread.clear();   // toggle off
    else                       sn->thread = subject;  // toggle on / overwrite
  } else if (kp_toggle) {
    if (sn->is_key_point && sn->kp_id == subject) {
      sn->kp_id.clear(); sn->kp_label.clear();
      sn->color_idx = 0; sn->is_key_point = false; sn->pin = false;
    } else {
      sn->kp_id        = subject;
      sn->color_idx    = kp_cidx;
      sn->kp_label     = kp_lbl;
      sn->is_key_point = true;
    }
  } else {
    auto& v = sn->subject_links;
    auto it = std::find(v.begin(), v.end(), subject);
    if (it != v.end()) v.erase(it);            // toggle off
    else               v.push_back(subject);   // toggle on
  }
  m_model.mark_modified();
  rebuild();
}

void TimelineSurface::show_track_menu(int track_idx, double x, double y) {
  if (track_idx < 0 || track_idx >= static_cast<int>(m_tracks.size())) return;
  const TimelineTrack& tk = m_tracks[static_cast<std::size_t>(track_idx)];
  const std::string subject = tk.iid;
  const std::string label   = tk.label.empty() ? tk.iid : tk.label;

  // The scene under the cursor (if any) — offered as a per-scene unlink when the
  // subject is actually claimed there. s91 — x,y arrive RAW (scaled content
  // space) so set_pointing_to lands correctly; divide by m_zoom for the hit-test.
  const int col = column_at(x / m_zoom);   // 1-based told-order column, or 0
  const std::string scene_iid = scene_iid_at_col(col);
  const int scene_pos = scene_iid.empty() ? 0 : col;
  const bool over_claim = !scene_iid.empty() && tk.claimed.count(scene_iid) > 0;

  auto menu = Gio::Menu::create();
  auto ag   = Gio::SimpleActionGroup::create();

  if (over_claim) {
    auto a = ag->add_action("unlink-scene", [this, subject, scene_iid]() {
      unlink_subject_scene(subject, scene_iid);
    });
    (void)a;
    menu->append(Glib::ustring::compose("Unlink from Scene %1", scene_pos),
                 "tlctx.unlink-scene");
  }
  ag->add_action("remove-track", [this, subject]() { remove_subject(subject); });
  menu->append(Glib::ustring::compose("Remove \u201c%1\u201d from timeline", label),
               "tlctx.remove-track");

  if (m_ctx_popover) { m_ctx_popover->unparent(); m_ctx_popover = nullptr; }
  m_ctx_popover = Gtk::make_managed<Gtk::PopoverMenu>(menu);
  m_ctx_popover->insert_action_group("tlctx", ag);
  m_ctx_popover->set_parent(m_area);
  m_ctx_popover->set_has_arrow(false);
  Gdk::Rectangle r;
  r.set_x(static_cast<int>(x));
  r.set_y(static_cast<int>(y));
  r.set_width(1);
  r.set_height(1);
  m_ctx_popover->set_pointing_to(r);
  m_ctx_popover->signal_closed().connect([this]() {
    Glib::signal_idle().connect_once([this]() {
      if (m_ctx_popover) { m_ctx_popover->unparent(); m_ctx_popover = nullptr; }
    });
  });
  m_ctx_popover->popup();
}

// ─────────────────────────────────────────────────────────────────────────────
// s89 — tell the host a palette/registry edit happened so it can live-refresh
// the surfaces that READ the palette but are not the timeline itself (the
// Inspector colour dropdowns + the sidebar swatches). Deferred to idle so it is
// safe to call from inside a rail/popover swatch handler (the s24 rule): the
// host's refresh rebuilds views, which must not run while the popover that
// triggered the edit is still on the stack.
void TimelineSurface::notify_palette_changed() {
  if (!m_on_palette_changed) return;
  Glib::signal_idle().connect_once([this]() {
    if (m_on_palette_changed) m_on_palette_changed();
  });
}

// s86 — thread management (rename / recolour / delete-unused)
//
// The edit surface for an existing thread. Mint already lives on two surfaces
// (Inspector + rail); this closes the last gap — there was no way to change a
// thread's label or colour after the fact, nor remove an unused one. The
// registry (DocumentModel::threads) is the rename-safe home: a scene references
// its thread by the stable thr_ iid (BinderNode.thread), so a rename/recolour is
// a one-field edit on the ThreadDef and never has to walk the manuscript, and a
// delete is a registry erase. Reached by a right-click on a rail Story-Threads
// row (the mirror of the subject track's right-click remove menu).
// ─────────────────────────────────────────────────────────────────────────────

// Count every node (on- AND off-spine) whose thread is this one. The TRUE usage
// (so an off-spine assignment still counts), unlike the rail's on-spine claim
// count — deleting a thread an off-spine scene still holds would orphan that
// reference (find_thread → nullptr → the lane silently drops, but the field
// dangles). 0 here means the erase is clean.
int TimelineSurface::thread_usage_count(const std::string& thread_iid) const {
  if (thread_iid.empty()) return 0;
  int n = 0;
  for (const BinderNode* node : m_model.all_node_ptrs())
    if (node && node->thread == thread_iid) ++n;
  return n;
}

// Rename: a one-field edit on the ThreadDef. The lane label is re-resolved from
// the registry on the next build (SceneThreadInfo pulls td->label), so a deferred
// rebuild refreshes the rail row + the band header live. Empty / unchanged names
// are ignored (the old label stands). Committed from the entry's activate, its
// focus-leave, and the popover close — all idempotent via the no-change guard.
void TimelineSurface::rename_thread(const std::string& thread_iid,
                                    const std::string& raw_label) {
  if (thread_iid.empty()) return;
  std::string label = raw_label;
  const auto b = label.find_first_not_of(" \t\n\r");
  const auto e = label.find_last_not_of(" \t\n\r");
  label = (b == std::string::npos) ? std::string() : label.substr(b, e - b + 1);
  if (label.empty()) return;   // a blank rename is a no-op; keep the old label
  for (ThreadDef& t : m_model.threads()) {
    if (t.iid != thread_iid) continue;
    if (t.label == label) return;               // no change
    t.label = label;
    m_model.mark_modified();
    notify_palette_changed();   // s89
    Glib::signal_idle().connect_once([this]() { rebuild(); });
    return;
  }
}

// Recolour: set the ThreadDef's palette index. The band lane + rail swatch
// re-resolve the hue from the registry on rebuild (deferred to idle so we never
// rebuild re-entrantly from inside the swatch's click handler).
void TimelineSurface::recolour_thread(const std::string& thread_iid, int color_idx) {
  if (thread_iid.empty()) return;
  for (ThreadDef& t : m_model.threads()) {
    if (t.iid != thread_iid) continue;
    if (t.color_idx == color_idx) return;       // no change
    t.color_idx = color_idx;
    m_model.mark_modified();
    notify_palette_changed();   // s89 — refresh Inspector/sidebar
    Glib::signal_idle().connect_once([this]() { rebuild(); });
    return;
  }
}

// Delete an UNUSED thread (the chosen scope — see the s86 fork in the handoff:
// the destructive force-delete-and-clear variant is a one-liner here). Refuses
// if any node still references it (the button is disabled in that case too, so
// this is a double-guard). Erase from the registry, drop a dangling arm, and
// rebuild — all on an idle tick, after the popover has popped down, so the row
// the popover anchored to is gone before the teardown (the s24 rule).
void TimelineSurface::delete_thread(const std::string& thread_iid) {
  if (thread_iid.empty()) return;
  if (thread_usage_count(thread_iid) > 0) return;   // delete-unused only
  if (m_thread_popover) m_thread_popover->popdown();
  Glib::signal_idle().connect_once([this, thread_iid]() {
    auto& v = m_model.threads();
    const auto it = std::find_if(v.begin(), v.end(),
        [&](const ThreadDef& t) { return t.iid == thread_iid; });
    if (it == v.end()) return;
    v.erase(it);
    if (m_armed_kind == ArmedKind::Thread && m_armed_iid == thread_iid) {
      m_armed_iid.clear();
      m_armed_kind = ArmedKind::Subject;
      m_armed_color_idx = 0;
    }
    m_model.mark_modified();
    notify_palette_changed();   // s89
    rebuild();
  });
}

// s86 — clear a SET thread off the timeline: clear BinderNode.thread on every
// scene that holds it, keeping the registry entry (the analog of remove_subject;
// the subject keeps existing in the binder, only its timeline relief goes). The
// thread row stays in the rail at 0 scenes, re-assignable; Delete then enables.
void TimelineSurface::remove_thread_assignments(const std::string& thread_iid) {
  if (thread_iid.empty()) return;
  if (m_thread_popover) m_thread_popover->popdown();
  Glib::signal_idle().connect_once([this, thread_iid]() {
    std::vector<std::string> scenes;
    for (const BinderNode* n : m_model.all_node_ptrs())
      if (n && n->thread == thread_iid) scenes.push_back(n->iid);
    bool changed = false;
    for (const std::string& iid : scenes) {
      BinderNode* mn = m_model.find_node_by_iid(iid);
      if (mn) { mn->thread.clear(); changed = true; }
    }
    if (changed) m_model.mark_modified();
    rebuild();
  });
}

// The thread manage popover — rename entry + recolour swatch grid + remove /
// delete. A widget popover (not a Gio::Menu) because the actions need controls.
void TimelineSurface::show_thread_menu(const std::string& thread_iid,
                                       double x, double y) {
  const ThreadDef* td = m_model.find_thread(thread_iid);
  if (!td) return;
  const std::string cur_label = td->label;
  const int         cur_idx   = td->color_idx;
  const int         usage     = thread_usage_count(thread_iid);

  if (m_thread_popover) {
    // Null FIRST, then unparent: unparenting a live popover can emit `closed`,
    // and the self-guard in that handler keys off m_thread_popover — clearing it
    // here makes the superseded popover's close a clean no-op.
    Gtk::Popover* old = m_thread_popover;
    m_thread_popover = nullptr;
    old->unparent();
  }
  m_thread_popover = Gtk::make_managed<Gtk::Popover>();
  m_thread_popover->set_name("timeline-thread-mgr");
  m_thread_popover->add_css_class("timeline-thread-mgr");
  m_thread_popover->set_parent(*this);

  auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
  box->set_margin(10);

  auto* hdr = Gtk::make_managed<Gtk::Label>("Story thread");
  hdr->set_xalign(0.0f);
  hdr->add_css_class("timeline-rail-header");
  box->append(*hdr);

  // ── Rename ──────────────────────────────────────────────────────────────
  auto* entry = Gtk::make_managed<Gtk::Entry>();
  entry->set_text(cur_label);
  entry->set_hexpand(true);
  entry->set_max_width_chars(20);
  entry->set_tooltip_text("Rename this thread");
  box->append(*entry);

  // ── Recolour — a grid of palette swatches (8 per row), the current one
  // ringed. Each swatch is a DrawingArea (fill + selection ring drawn in
  // Cairo, the rail-swatch idiom) wrapped in a flat button for the click;
  // a shared selected-index lets every swatch redraw its ring on a pick
  // without a rail rebuild.
  const auto& palette = m_prefs.tag_colors;
  if (!palette.empty()) {
    auto* clabel = Gtk::make_managed<Gtk::Label>("Colour");
    clabel->set_xalign(0.0f);
    clabel->add_css_class("timeline-rail-header");
    box->append(*clabel);

    auto* grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_row_spacing(4);
    grid->set_column_spacing(4);
    auto sel = std::make_shared<int>(cur_idx);
    std::vector<Gtk::DrawingArea*> areas;
    areas.reserve(palette.size());

    constexpr int PER_ROW = 8;
    for (std::size_t i = 0; i < palette.size(); ++i) {
      const int idx = static_cast<int>(i) + 1;
      Gdk::RGBA hue; hue.set(palette[i].hex);

      auto* sw = Gtk::make_managed<Gtk::DrawingArea>();
      sw->set_content_width(22);
      sw->set_content_height(22);
      sw->set_draw_func([hue, idx, sel](const Cairo::RefPtr<Cairo::Context>& cr,
                                        int w, int h) {
        const double pad = 2.0, r = 4.0;
        const double x0 = pad, y0 = pad, x1 = w - pad, y1 = h - pad;
        cr->begin_new_sub_path();
        cr->arc(x1 - r, y0 + r, r, -M_PI / 2, 0);
        cr->arc(x1 - r, y1 - r, r, 0, M_PI / 2);
        cr->arc(x0 + r, y1 - r, r, M_PI / 2, M_PI);
        cr->arc(x0 + r, y0 + r, r, M_PI, 3 * M_PI / 2);
        cr->close_path();
        cr->set_source_rgb(hue.get_red(), hue.get_green(), hue.get_blue());
        cr->fill_preserve();
        if (*sel == idx) {                       // selection ring
          cr->set_source_rgb(0.95, 0.95, 0.98);
          cr->set_line_width(2.0);
          cr->stroke();
        } else {
          cr->begin_new_path();
        }
      });
      areas.push_back(sw);

      auto* sbtn = Gtk::make_managed<Gtk::Button>();
      sbtn->set_has_frame(false);
      sbtn->add_css_class("timeline-thread-swatch");
      sbtn->set_child(*sw);
      sbtn->set_tooltip_text(palette[i].name);
      sbtn->signal_clicked().connect(
          [this, thread_iid, idx, sel, areas]() {
            *sel = idx;
            for (Gtk::DrawingArea* a : areas) if (a) a->queue_draw();
            recolour_thread(thread_iid, idx);
          });

      grid->attach(*sbtn, static_cast<int>(i) % PER_ROW,
                          static_cast<int>(i) / PER_ROW);
    }
    box->append(*grid);
  }

  auto* sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
  box->append(*sep);

  // ── Remove from timeline (clear all assignments) — when the thread is SET ──
  if (usage > 0) {
    auto* rm = Gtk::make_managed<Gtk::Button>("Remove from timeline");
    rm->set_tooltip_text("Clear this thread off every scene (keeps the thread)");
    rm->signal_clicked().connect(
        [this, thread_iid]() { remove_thread_assignments(thread_iid); });
    box->append(*rm);
  }

  // ── Delete (unused only) ────────────────────────────────────────────────
  auto* del = Gtk::make_managed<Gtk::Button>("Delete thread");
  del->add_css_class("destructive-action");
  del->set_sensitive(usage == 0);
  del->signal_clicked().connect(
      [this, thread_iid]() { delete_thread(thread_iid); });
  box->append(*del);
  if (usage > 0) {
    // A disabled button gets no hover events, so the reason rides in a visible
    // hint rather than a tooltip.
    auto* why = Gtk::make_managed<Gtk::Label>(
        Glib::ustring::compose(
            usage == 1 ? "On %1 scene \u2014 \u201cRemove from timeline\u201d to delete."
                       : "On %1 scenes \u2014 \u201cRemove from timeline\u201d to delete.",
            usage));
    why->set_xalign(0.0f);
    why->set_wrap(true);
    why->set_max_width_chars(24);
    why->add_css_class("dim-label");
    why->add_css_class("timeline-thread-hint");
    box->append(*why);
  }

  m_thread_popover->set_child(*box);

  // Rename commit paths: Enter, focus-leave (e.g. clicking a swatch), and a
  // close-time safety net. All funnel through rename_thread's no-change guard.
  entry->signal_activate().connect(
      [this, thread_iid, entry]() { rename_thread(thread_iid, entry->get_text().raw()); });
  auto foc = Gtk::EventControllerFocus::create();
  foc->signal_leave().connect(
      [this, thread_iid, entry]() { rename_thread(thread_iid, entry->get_text().raw()); });
  entry->add_controller(foc);

  Gtk::Popover* self = m_thread_popover;
  m_thread_popover->signal_closed().connect([this, self, thread_iid, entry]() {
    if (m_thread_popover != self) return;   // superseded by a newer manage popover
    rename_thread(thread_iid, entry->get_text().raw());   // catch an un-Enter'd edit
    Glib::signal_idle().connect_once([this, self]() {
      if (m_thread_popover == self) { m_thread_popover->unparent(); m_thread_popover = nullptr; }
    });
  });

  Gdk::Rectangle r;
  r.set_x(static_cast<int>(x));
  r.set_y(static_cast<int>(y));
  r.set_width(1);
  r.set_height(1);
  m_thread_popover->set_pointing_to(r);
  m_thread_popover->popup();
}

// s86 — the "what's a story thread?" teaching popover. Self-unparenting on close
// (a transient, no member needed). Anchored to whatever triggered it.
void TimelineSurface::show_thread_help(Gtk::Widget* anchor) {
  if (!anchor) return;
  auto* pop = Gtk::make_managed<Gtk::Popover>();
  pop->set_name("timeline-thread-help");
  pop->add_css_class("timeline-thread-help");
  pop->set_parent(*anchor);

  auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
  box->set_margin(12);

  auto* title = Gtk::make_managed<Gtk::Label>("Story threads");
  title->set_xalign(0.0f);
  title->add_css_class("timeline-thread-help-title");
  box->append(*title);

  auto add_para = [&](const Glib::ustring& t) {
    auto* l = Gtk::make_managed<Gtk::Label>(t);
    l->set_xalign(0.0f);
    l->set_wrap(true);
    l->set_max_width_chars(42);
    box->append(*l);
  };
  add_para("A thread is an independent story arc \u2014 Main, Protagonist, "
           "Antagonist, Backstory, Subplot \u2014 threaded through the manuscript. "
           "You assign scenes to it, and the timeline shows it as its own coloured "
           "lane.");
  add_para("Use it whenever a book runs more than one arc at once: a far-past / "
           "near-past / present structure, or a protagonist line cut against an "
           "antagonist line. Tag each scene with the arc it belongs to, and the "
           "band under the spine shows the arcs weaving through told order: where "
           "you linger in one, where another goes dark, and whether they\u2019re "
           "balanced (each lane shows its scene count).");
  add_para("A thread is an ASSIGNED arc, not a character. A character track is "
           "revealed automatically (every scene they appear in); a thread is the "
           "arc you declare \u2014 so a scene can be on the Antagonist thread even "
           "if the hero also appears in it. A thread is just a coloured label: it "
           "has no page of its own and isn\u2019t in the binder. The scenes hold "
           "the writing; the thread records which arc each belongs to. "
           "Single-arc books don\u2019t need threads.");
  add_para("To build one: type a name below and press +, then sweep it across the "
           "scenes on the spine (or set it per scene in the Inspector). "
           "Right-click a thread to rename, recolour, or delete it.");

  pop->set_child(*box);
  pop->signal_closed().connect([pop]() { pop->unparent(); });
  pop->popup();
}

// ─────────────────────────────────────────────────────────────────────────────
// s86 — Key Point management (mint / place / rename / recolour / delete)
//
// The one-place fix: building a KP after the fact used to need Preferences (make
// the swatch) AND the Inspector (flag the scene). A KP IS a palette swatch ("the
// palette is the arc"): identity + colour live in m_prefs.tag_colors keyed by a
// stable kp_ id; a scene becomes a beat by carrying that id + color_idx (swatch
// position) + kp_label + is_key_point. So these helpers edit the palette and the
// scene fields directly. Because a KP is a palette swatch, recolour/delete are
// palette-wide; delete runs the positional reconcile (KpPalette) over every
// coloured node, exactly as the Preferences palette editor does on close.
// ─────────────────────────────────────────────────────────────────────────────

// On-spine BEATS wearing this KP (read from m_kp_lanes — is_key_point scenes
// with this kp_id). Gates delete + feeds its hint, mirroring threads.
int TimelineSurface::kp_usage_count(const std::string& kp_id) const {
  if (kp_id.empty()) return 0;
  for (const KpLane& ln : m_kp_lanes)
    if (ln.kp_id == kp_id) return static_cast<int>(ln.claimed.size());
  return 0;
}

// Rename a KP = rename its palette swatch. kp_label on a beat is resolved from
// the swatch at the next build, so a deferred rebuild refreshes the strip + rail.
// (The stored kp_label on already-stamped scenes is a cache; the strip re-reads
// it from the node, so we also restamp live beats' kp_label on rename to keep
// the cache honest — cheap, and the reconcile path does the same for color_idx.)
void TimelineSurface::rename_kp(const std::string& kp_id,
                                const std::string& raw_label) {
  if (kp_id.empty()) return;
  std::string label = raw_label;
  const auto b = label.find_first_not_of(" \t\n\r");
  const auto e = label.find_last_not_of(" \t\n\r");
  label = (b == std::string::npos) ? std::string() : label.substr(b, e - b + 1);
  if (label.empty()) return;
  for (TagColor& tc : m_prefs.tag_colors) {
    if (tc.id != kp_id) continue;
    if (tc.name == label) return;             // no change
    tc.name = label;
    m_prefs.save();
    // Keep the kp_label cache on live beats in step with the swatch name.
    for (const BinderNode* n : m_model.all_node_ptrs()) {
      if (n && n->kp_id == kp_id) {
        BinderNode* mn = m_model.find_node_by_iid(n->iid);
        if (mn) mn->kp_label = label;
      }
    }
    m_model.mark_modified();
    notify_palette_changed();   // s89 — KP label feeds the Inspector dropdowns
    Glib::signal_idle().connect_once([this]() { rebuild(); });
    return;
  }
}

// Recolour a KP = set its swatch hex (its position/color_idx is unchanged, so no
// reconcile is needed — beats keep their color_idx and simply paint the new hex).
void TimelineSurface::recolour_kp(const std::string& kp_id, const std::string& hex) {
  if (kp_id.empty() || hex.empty()) return;
  for (TagColor& tc : m_prefs.tag_colors) {
    if (tc.id != kp_id) continue;
    if (tc.hex == hex) return;                // no change
    tc.hex = hex;
    m_prefs.save();
    m_model.mark_modified();                  // the project's colours moved
    notify_palette_changed();   // s89 — KP hue feeds dropdowns + sidebar swatches
    Glib::signal_idle().connect_once([this]() { rebuild(); });
    return;
  }
}

// Delete a KP = remove its palette swatch + reconcile. Gated on the swatch being
// UNUSED (no scene wears it — beat or plain colour), because removing it clears
// every scene that does. The reconcile still runs because removing a swatch
// shifts the positional color_idx of every LATER swatch, so EVERY coloured node
// must be remapped (the machinery the Preferences palette editor runs on close).
// Deferred to idle after the popover pops down.
void TimelineSurface::delete_kp(const std::string& kp_id) {
  if (kp_id.empty()) return;
  // Gate on TOTAL swatch usage (beats + plain-coloured scenes): deleting the
  // swatch clears every scene wearing it. Compute its position, count users.
  int position = 0;
  for (std::size_t i = 0; i < m_prefs.tag_colors.size(); ++i)
    if (m_prefs.tag_colors[i].id == kp_id) { position = static_cast<int>(i) + 1; break; }
  if (position == 0) return;
  for (const BinderNode* n : m_model.all_node_ptrs())
    if (n && n->color_idx == position) return;   // in use — refuse (button also disabled)
  if (m_kp_popover) m_kp_popover->popdown();
  Glib::signal_idle().connect_once([this, kp_id]() {
    // Snapshot the old swatch-id order, erase the swatch, build the new order.
    std::vector<std::string> old_ids;
    old_ids.reserve(m_prefs.tag_colors.size());
    for (const TagColor& tc : m_prefs.tag_colors) old_ids.push_back(tc.id);

    auto& pal = m_prefs.tag_colors;
    const auto it = std::find_if(pal.begin(), pal.end(),
        [&](const TagColor& tc) { return tc.id == kp_id; });
    if (it == pal.end()) return;
    pal.erase(it);

    std::vector<std::string> new_ids;
    new_ids.reserve(pal.size());
    for (const TagColor& tc : pal) new_ids.push_back(tc.id);

    // Positional remap → rewrite every coloured node's color_idx (survivors
    // follow their swatch; a deleted swatch's scenes clear — none here, since
    // we gate on zero beats, but other swatches still shift).
    const auto remap = palette_remap(old_ids, new_ids);
    std::vector<std::string> iids;
    std::vector<SceneKpRef>  refs;
    for (const BinderNode* n : m_model.all_node_ptrs()) {
      if (!n || n->color_idx <= 0 || n->iid.empty()) continue;
      iids.push_back(n->iid);
      refs.push_back({n->kp_id, n->color_idx, n->kp_label, n->is_key_point, n->pin});
    }
    if (apply_palette_remap(remap, refs) > 0) {
      for (std::size_t i = 0; i < iids.size(); ++i) {
        BinderNode* n = m_model.find_node_by_iid(iids[i]);
        if (!n) continue;
        n->kp_id        = refs[i].kp_id;
        n->color_idx    = refs[i].color_idx;
        n->kp_label     = refs[i].kp_label;
        n->is_key_point = refs[i].is_key_point;
        n->pin          = refs[i].pin;
      }
    }

    if (m_armed_kind == ArmedKind::KeyPoint && m_armed_iid == kp_id) {
      m_armed_iid.clear();
      m_armed_kind = ArmedKind::Subject;
      m_armed_color_idx = 0;
    }
    m_prefs.save();
    m_model.mark_modified();
    notify_palette_changed();   // s89 — delete remaps color_idx → dropdowns + swatches
    rebuild();
  });
}

// s86 — clear a KP off every beat: un-stamp the KP fields on each scene wearing
// it, keeping the palette swatch (the analog of remove_thread_assignments). Full
// clear (kp_id/color_idx/kp_label/is_key_point/pin), matching the band remove.
void TimelineSurface::remove_kp_assignments(const std::string& kp_id) {
  if (kp_id.empty()) return;
  if (m_kp_popover) m_kp_popover->popdown();
  Glib::signal_idle().connect_once([this, kp_id]() {
    std::vector<std::string> beats;
    for (const BinderNode* n : m_model.all_node_ptrs())
      if (n && n->is_key_point && n->kp_id == kp_id) beats.push_back(n->iid);
    bool changed = false;
    for (const std::string& iid : beats) {
      BinderNode* mn = m_model.find_node_by_iid(iid);
      if (!mn) continue;
      mn->kp_id.clear(); mn->kp_label.clear();
      mn->color_idx = 0; mn->is_key_point = false; mn->pin = false;
      changed = true;
    }
    if (changed) m_model.mark_modified();
    rebuild();
  });
}

// The KP manage popover — rename entry + recolour swatch grid + delete-unused.
// The recolour grid offers a fixed REFERENCE palette (the built-in tag colours)
// as hex choices, NOT the live project palette (which IS the arc — picking from
// it would be circular); a pick sets THIS swatch's hex. Parented to *this
// (stable across rebuild), pointed at the right-clicked row.
void TimelineSurface::show_kp_menu(const std::string& kp_id, double x, double y) {
  const TagColor* sw = nullptr;
  int position = 0;                       // 1-based palette position of this swatch
  for (std::size_t i = 0; i < m_prefs.tag_colors.size(); ++i)
    if (m_prefs.tag_colors[i].id == kp_id) {
      sw = &m_prefs.tag_colors[i];
      position = static_cast<int>(i) + 1;
      break;
    }
  if (!sw) return;
  const std::string cur_label = sw->name;
  const std::string cur_hex   = sw->hex;
  // Usage for the delete gate = ANY scene wearing this swatch (a beat OR a plain
  // colour), since deleting the swatch clears every such scene's colour via the
  // reconcile. Distinct from the rail row's BEAT count (kp_usage_count).
  int usage = 0;
  for (const BinderNode* n : m_model.all_node_ptrs())
    if (n && n->color_idx == position) ++usage;

  if (m_kp_popover) {
    Gtk::Popover* old = m_kp_popover;
    m_kp_popover = nullptr;
    old->unparent();
  }
  m_kp_popover = Gtk::make_managed<Gtk::Popover>();
  m_kp_popover->set_name("timeline-thread-mgr");
  m_kp_popover->add_css_class("timeline-thread-mgr");
  m_kp_popover->set_parent(*this);

  auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
  box->set_margin(10);

  auto* hdr = Gtk::make_managed<Gtk::Label>("Key point");
  hdr->set_xalign(0.0f);
  hdr->add_css_class("timeline-rail-header");
  box->append(*hdr);

  auto* entry = Gtk::make_managed<Gtk::Entry>();
  entry->set_text(cur_label);
  entry->set_hexpand(true);
  entry->set_max_width_chars(20);
  entry->set_tooltip_text("Rename this key point");
  box->append(*entry);

  // Recolour — reference palette (the built-in tag colours), the current hex
  // ringed. Drawn the same way as the thread recolour grid.
  const std::vector<TagColor> ref = FolioPrefs{}.tag_colors;
  if (!ref.empty()) {
    auto* clabel = Gtk::make_managed<Gtk::Label>("Colour");
    clabel->set_xalign(0.0f);
    clabel->add_css_class("timeline-rail-header");
    box->append(*clabel);

    auto* grid = Gtk::make_managed<Gtk::Grid>();
    grid->set_row_spacing(4);
    grid->set_column_spacing(4);
    auto sel_hex = std::make_shared<std::string>(cur_hex);
    std::vector<Gtk::DrawingArea*> areas;
    areas.reserve(ref.size());

    constexpr int PER_ROW = 8;
    for (std::size_t i = 0; i < ref.size(); ++i) {
      const std::string hex = ref[i].hex;
      Gdk::RGBA rgba; rgba.set(hex);

      auto* swa = Gtk::make_managed<Gtk::DrawingArea>();
      swa->set_content_width(22);
      swa->set_content_height(22);
      swa->set_draw_func([rgba, hex, sel_hex](const Cairo::RefPtr<Cairo::Context>& cr,
                                              int w, int h) {
        const double pad = 2.0, r = 4.0;
        const double x0 = pad, y0 = pad, x1 = w - pad, y1 = h - pad;
        cr->begin_new_sub_path();
        cr->arc(x1 - r, y0 + r, r, -M_PI / 2, 0);
        cr->arc(x1 - r, y1 - r, r, 0, M_PI / 2);
        cr->arc(x0 + r, y1 - r, r, M_PI / 2, M_PI);
        cr->arc(x0 + r, y0 + r, r, M_PI, 3 * M_PI / 2);
        cr->close_path();
        cr->set_source_rgb(rgba.get_red(), rgba.get_green(), rgba.get_blue());
        cr->fill_preserve();
        if (*sel_hex == hex) {
          cr->set_source_rgb(0.95, 0.95, 0.98);
          cr->set_line_width(2.0);
          cr->stroke();
        } else {
          cr->begin_new_path();
        }
      });
      areas.push_back(swa);

      auto* sbtn = Gtk::make_managed<Gtk::Button>();
      sbtn->set_has_frame(false);
      sbtn->add_css_class("timeline-thread-swatch");
      sbtn->set_child(*swa);
      sbtn->set_tooltip_text(ref[i].name);
      sbtn->signal_clicked().connect(
          [this, kp_id, hex, sel_hex, areas]() {
            *sel_hex = hex;
            for (Gtk::DrawingArea* a : areas) if (a) a->queue_draw();
            recolour_kp(kp_id, hex);
          });
      grid->attach(*sbtn, static_cast<int>(i) % PER_ROW,
                          static_cast<int>(i) / PER_ROW);
    }
    box->append(*grid);
  }

  auto* sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
  box->append(*sep);

  // ── Remove from timeline (clear every beat) — when this KP marks scenes ────
  const int beats = kp_usage_count(kp_id);
  if (beats > 0) {
    auto* rm = Gtk::make_managed<Gtk::Button>("Remove from timeline");
    rm->set_tooltip_text("Un-mark every scene wearing this key point (keeps the colour)");
    rm->signal_clicked().connect(
        [this, kp_id]() { remove_kp_assignments(kp_id); });
    box->append(*rm);
  }

  auto* del = Gtk::make_managed<Gtk::Button>("Delete key point");
  del->add_css_class("destructive-action");
  del->set_sensitive(usage == 0);
  del->signal_clicked().connect([this, kp_id]() { delete_kp(kp_id); });
  box->append(*del);
  if (usage > 0) {
    auto* why = Gtk::make_managed<Gtk::Label>(
        Glib::ustring::compose(
            usage == 1 ? "On %1 scene \u2014 clear this colour from it first."
                       : "On %1 scenes \u2014 clear this colour from them first.",
            usage));
    why->set_xalign(0.0f);
    why->set_wrap(true);
    why->set_max_width_chars(24);
    why->add_css_class("dim-label");
    why->add_css_class("timeline-thread-hint");
    box->append(*why);
  }

  m_kp_popover->set_child(*box);

  entry->signal_activate().connect(
      [this, kp_id, entry]() { rename_kp(kp_id, entry->get_text().raw()); });
  auto foc = Gtk::EventControllerFocus::create();
  foc->signal_leave().connect(
      [this, kp_id, entry]() { rename_kp(kp_id, entry->get_text().raw()); });
  entry->add_controller(foc);

  Gtk::Popover* self = m_kp_popover;
  m_kp_popover->signal_closed().connect([this, self, kp_id, entry]() {
    if (m_kp_popover != self) return;
    rename_kp(kp_id, entry->get_text().raw());
    Glib::signal_idle().connect_once([this, self]() {
      if (m_kp_popover == self) { m_kp_popover->unparent(); m_kp_popover = nullptr; }
    });
  });

  Gdk::Rectangle r;
  r.set_x(static_cast<int>(x));
  r.set_y(static_cast<int>(y));
  r.set_width(1);
  r.set_height(1);
  m_kp_popover->set_pointing_to(r);
  m_kp_popover->popup();
}

// s86 — the "what's a key point?" teaching popover (mirrors the thread one).
void TimelineSurface::show_kp_help(Gtk::Widget* anchor) {
  if (!anchor) return;
  auto* pop = Gtk::make_managed<Gtk::Popover>();
  pop->set_name("timeline-thread-help");
  pop->add_css_class("timeline-thread-help");
  pop->set_parent(*anchor);

  auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
  box->set_margin(12);
  auto* title = Gtk::make_managed<Gtk::Label>("Key points");
  title->set_xalign(0.0f);
  title->add_css_class("timeline-thread-help-title");
  box->append(*title);

  auto add_para = [&](const Glib::ustring& t) {
    auto* l = Gtk::make_managed<Gtk::Label>(t);
    l->set_xalign(0.0f);
    l->set_wrap(true);
    l->set_max_width_chars(42);
    box->append(*l);
  };
  add_para("A key point is a structural beat in your arc \u2014 Inciting "
           "Incident, Midpoint, Climax \u2014 marked on the scene that carries it. "
           "The strip just above the spine shows where your beats fall.");
  add_para("Key points partition the arc: a scene carries at most one. Each is a "
           "named, coloured swatch, and the colours run as a spectrum so the strip "
           "reads as a ramp from opening to climax.");
  add_para("This is the one place to build one after the fact: type a name and "
           "press +, then sweep it across the scene(s) that land that beat "
           "(right-click a key point to rename, recolour, or delete it). Before "
           "now you had to make the swatch in Preferences and flag the scene in "
           "the Inspector \u2014 two rooms for one beat.");
  add_para("A pattern (New from Pattern) lays down a whole arc of key points at "
           "once; this section is for adding or adjusting beats by hand.");

  pop->set_child(*box);
  pop->signal_closed().connect([pop]() { pop->unparent(); });
  pop->popup();
}

int TimelineSurface::track_row_at(double y) const {
  const int n = static_cast<int>(m_tracks.size());
  if (n == 0) return -1;
  const double rel = y - track_top();
  if (rel < 0) return -1;
  const int idx = static_cast<int>(rel / (TRACK_H + TRACK_GAP));
  if (idx < 0 || idx >= n) return -1;
  // reject the gap between rows (only the TRACK_H band counts as a hit)
  if (rel - idx * (TRACK_H + TRACK_GAP) > TRACK_H) return -1;
  return idx;
}

// s86 — the thread band lane under y (row-keyed, like track_row_at), or -1. The
// band rows start at thread_rows_top(), one TRACK_H row per lane (TRACK_GAP between).
int TimelineSurface::thread_lane_at(double y) const {
  const int n = static_cast<int>(m_thread_lanes.size());
  if (n == 0) return -1;
  const double rel = y - thread_rows_top();
  if (rel < 0) return -1;
  const int idx = static_cast<int>(rel / (TRACK_H + TRACK_GAP));
  if (idx < 0 || idx >= n) return -1;
  if (rel - idx * (TRACK_H + TRACK_GAP) > TRACK_H) return -1;   // gap, not a hit
  return idx;
}

// s86 — is y within the KP strip row (a single row of height KP_H at kp_top()).
bool TimelineSurface::over_kp_strip(double y) const {
  if (m_kp_lanes.empty()) return false;
  const int t = kp_top();
  return y >= t && y <= t + KP_H;
}

// s86 — the KP lane claiming the scene at a 1-based told-order column, or -1.
// KPs partition the spine, so at most one lane claims a given column.
int TimelineSurface::kp_lane_at_col(int col) const {
  if (col <= 0) return -1;
  const std::string scene = scene_iid_at_col(col);
  if (scene.empty()) return -1;
  for (std::size_t i = 0; i < m_kp_lanes.size(); ++i)
    if (m_kp_lanes[i].claimed.count(scene) > 0) return static_cast<int>(i);
  return -1;
}

// s92 — the relief row under a BASE-coord point → its kind-namespaced focus key,
// or "" if the point is not over a focusable row. The three y-bands are disjoint
// (subject tracks / thread band / KP strip), so the order of checks is immaterial
// to correctness; a KP needs the column too (its row is partitioned by column).
std::string TimelineSurface::focus_key_at(double x, double y) const {
  if (const int trk = track_row_at(y);
      trk >= 0 && trk < static_cast<int>(m_tracks.size()))
    return fk_subject(m_tracks[static_cast<std::size_t>(trk)].iid);
  if (const int tl = thread_lane_at(y);
      tl >= 0 && tl < static_cast<int>(m_thread_lanes.size()))
    return fk_thread(m_thread_lanes[static_cast<std::size_t>(tl)].thread_key);
  if (over_kp_strip(y)) {
    const int kl = kp_lane_at_col(column_at(x));
    if (kl >= 0 && kl < static_cast<int>(m_kp_lanes.size()))
      return fk_keypoint(m_kp_lanes[static_cast<std::size_t>(kl)].kp_id);
  }
  return {};
}

// s92 — the uniform (key, claimed) adapter over all three lane vectors, the input
// to the pure focus_positions. Built fresh from the current lanes; keys are
// namespaced to match what focus_key_at pins, so a focused key always resolves to
// the right lane (or to nothing, when its row was pruned).
std::vector<FocusLane> TimelineSurface::focus_lanes() const {
  std::vector<FocusLane> lanes;
  lanes.reserve(m_tracks.size() + m_thread_lanes.size() + m_kp_lanes.size());
  for (const TimelineTrack& t : m_tracks)
    lanes.push_back(FocusLane{fk_subject(t.iid), t.claimed});
  for (const ThreadLane& l : m_thread_lanes)
    lanes.push_back(FocusLane{fk_thread(l.thread_key), l.claimed});
  for (const KpLane& l : m_kp_lanes)
    lanes.push_back(FocusLane{fk_keypoint(l.kp_id), l.claimed});
  return lanes;
}

// s92 — refresh the spine-walk cache from the live focus set + lanes. Cheap, but
// only needs to run on a focus change or a rebuild (not per frame), so draw()
// reads m_focus_positions instead of recomputing.
void TimelineSurface::recompute_focus_positions() {
  m_focus_positions = focus_positions(m_focus, focus_lanes(), m_spine_iids);
}

// s92 — drop any focused key whose row no longer exists (a rebuild may have
// removed a subject / deleted a thread / cleared a KP). The mirror of the
// m_selected_iid peek-sync: stale presentation state is reconciled to the model
// on every rebuild rather than left dangling.
void TimelineSurface::prune_focus() {
  if (m_focus.empty()) return;
  std::set<std::string> live;
  for (const TimelineTrack& t : m_tracks)     live.insert(fk_subject(t.iid));
  for (const ThreadLane& l : m_thread_lanes)  live.insert(fk_thread(l.thread_key));
  for (const KpLane& l : m_kp_lanes)          live.insert(fk_keypoint(l.kp_id));
  for (auto it = m_focus.begin(); it != m_focus.end();) {
    if (live.find(*it) == live.end()) it = m_focus.erase(it);
    else ++it;
  }
}

// s92 — unpin everything (Esc / a future "clear focus" control). Redraws only
// when something actually cleared.
void TimelineSurface::clear_focus() {
  if (m_focus.empty()) return;
  m_focus.clear();
  m_focus_positions.clear();
  m_area.queue_draw();
}

int TimelineSurface::spine_top() const {
  return TOP_PAD + m_proj.band_rows * (BAND_H + BAND_GAP) + BAND_TO_SPINE;
}

// The KP strip sits KP_PAD below the spine cards. (Its own top; height is KP_H
// only when there are lanes — see track_top / content_height for the reserve.)
int TimelineSurface::kp_top() const {
  return spine_top() + CARD_H + KP_PAD;
}

// The staging row sits below the spine/KP strip when a rail subject is armed.
int TimelineSurface::staging_top() const {
  const int after_spine = spine_top() + CARD_H;
  const int after_kp = m_kp_lanes.empty() ? after_spine : kp_top() + KP_H;
  return after_kp + STAGE_PAD;
}

// s93 — the Told Order / Chrono lens toggle, two pills in the SPINE gutter just
// under the row label. BASE coords (drawn under the same cr->scale(m_zoom) as the
// rest; the click handler divides by m_zoom before calling lens_toggle_click). The
// gutter is GUTTER (132) wide, so the pair fits left of column 0 at x0().
TimelineSurface::LensToggleGeom TimelineSurface::lens_toggle_geom() const {
  const double axis_y = spine_top() + CARD_H / 2.0;
  LensToggleGeom g;
  g.y        = axis_y + 7.0;
  g.h        = 15.0;
  g.told_x   = LEFT_PAD;
  g.told_w   = 64.0;
  g.chrono_x = LEFT_PAD + g.told_w + 4.0;   // 84
  g.chrono_w = 46.0;                        // ends at 130, inside the 148px gutter
  return g;
}

void TimelineSurface::draw_lens_toggle(const Cairo::RefPtr<Cairo::Context>& cr) {
  const Gdk::RGBA accent = themed(m_area, "accent", "#89b4fa");
  const Gdk::RGBA muted  = themed(m_area, "tx3", "#9196b4");
  const LensToggleGeom g = lens_toggle_geom();
  struct Pill { double x, w; const char* label; bool active; };
  const Pill pills[2] = {
    { g.told_x,   g.told_w,   "Told Order", !m_story_axis },
    { g.chrono_x, g.chrono_w, "Chrono",      m_story_axis },
  };
  for (const Pill& p : pills) {
    rounded_rect(cr, p.x, g.y, p.w, g.h, g.h / 2.0);
    if (p.active) {
      set_src(cr, accent, 0.20); cr->fill_preserve();
      set_src(cr, accent, 0.95);
    } else {
      set_src(cr, muted, 0.35);
    }
    cr->set_line_width(1.0);
    cr->stroke();
    auto tl = m_area.create_pango_layout(p.label);
    tl->set_font_description(Pango::FontDescription("sans 8"));
    int tw = 0, th = 0; tl->get_pixel_size(tw, th);
    set_src(cr, p.active ? accent : muted, p.active ? 1.0 : 0.85);
    cr->move_to(p.x + (p.w - tw) / 2.0, g.y + (g.h - th) / 2.0);
    tl->show_in_cairo_context(cr);
  }
}

bool TimelineSurface::lens_toggle_click(double bx, double by) {
  const LensToggleGeom g = lens_toggle_geom();
  if (by < g.y || by > g.y + g.h) return false;
  bool want_chrono;
  if (bx >= g.told_x && bx <= g.told_x + g.told_w)             want_chrono = false;
  else if (bx >= g.chrono_x && bx <= g.chrono_x + g.chrono_w)  want_chrono = true;
  else return false;
  if (want_chrono != m_story_axis) {   // a no-op re-click of the active pill just consumes
    m_story_axis = want_chrono;
    recompute_chrono();
    m_selected_iid.clear();
    m_peek_revealer.set_reveal_child(false);
    m_area.queue_draw();
  }
  return true;
}

bool TimelineSurface::over_staging(double y) const {
  if (!staging_active()) return false;
  const int t = staging_top();
  return y >= t && y <= t + STAGE_H;
}

// The relief tracks start below the staging row (when armed) or the KP strip /
// cards otherwise. The staging row reserves space only while armed (§3).
int TimelineSurface::track_top() const {
  if (staging_active()) return staging_top() + STAGE_H + TRACK_PAD;
  const int after_spine = spine_top() + CARD_H;
  const int after_kp = m_kp_lanes.empty() ? after_spine
                                          : kp_top() + KP_H;
  return after_kp + TRACK_PAD;
}

// The armed subject's current claimed scene-set (from its track, if it has one).
// Returns nullptr when nothing is armed or the armed subject has no track yet —
// the builder case, where a sweep adds every column in the span.
// s85 — thread-aware: when a THREAD is armed, the "current presence" is its
// lane's claimed set (the on-spine scenes already assigned that thread), read
// from m_thread_lanes — the exact analog of reading a subject's track. So a
// thread sweep extends/edits the existing thread relief just as a subject sweep
// does, and the sweep verb (add vs remove) keys on whether the entered cell is
// already in THIS thread.
const std::unordered_set<std::string>* TimelineSurface::armed_claimed() const {
  if (m_armed_iid.empty()) return nullptr;
  if (m_armed_kind == ArmedKind::Thread) {
    for (const ThreadLane& ln : m_thread_lanes)
      if (ln.thread_key == m_armed_iid) return &ln.claimed;
    return nullptr;   // armed a thread with no on-spine scenes yet
  }
  if (m_armed_kind == ArmedKind::KeyPoint) {
    for (const KpLane& ln : m_kp_lanes)
      if (ln.kp_id == m_armed_iid) return &ln.claimed;
    return nullptr;   // armed a KP with no on-spine beats yet
  }
  for (const TimelineTrack& t : m_tracks)
    if (t.iid == m_armed_iid) return &t.claimed;
  return nullptr;
}

// s85 — the armed thing's hue. A subject arm draws in its §9.6 category hue; a
// thread arm draws in its palette colour (thread_hex, lavender fallback); a KP
// arm draws in its swatch colour (kp_hex, orange fallback) — so the staging row
// + sweep preview read in the same hue as the lane it targets.
std::string TimelineSurface::armed_hue() const {
  if (m_armed_kind == ArmedKind::Thread)   return thread_hex(m_armed_color_idx);
  if (m_armed_kind == ArmedKind::KeyPoint) return kp_hex(m_armed_color_idx);
  return subject_hex(m_armed_color_idx, m_armed_cat);
}

// A KP's hue: its stamped color_idx into the project palette (the spectrum the
// materializer copied in), or orange when unstamped (color_idx 0 / OOB) — the
// runtime-pinned beat that never went through materialization (§9.6, s81 ruling).
std::string TimelineSurface::kp_hex(int color_idx) const {
  const std::string hex = m_prefs.color_hex_for_idx(color_idx);  // "" if 0/OOB
  return hex.empty() ? "#fab387" : hex;                          // peach = orange
}

// s84 — a thread's hue: its color_idx into the project palette, lavender when
// unset (a distinct fallback from the KP orange so an uncoloured thread does not
// read as an unstamped KP).
std::string TimelineSurface::thread_hex(int color_idx) const {
  const std::string hex = m_prefs.color_hex_for_idx(color_idx);  // "" if 0/OOB
  return hex.empty() ? "#b4befe" : hex;                          // lavender
}

// s86 — a subject's hue: its assigned palette colour when set, else the §9.6
// category hue. So a user-coloured character/place/reference shows in its own
// colour on the timeline; an uncoloured one (and every image) keeps category.
std::string TimelineSurface::subject_hex(int color_idx, TrackCategory cat) const {
  const std::string hex = m_prefs.color_hex_for_idx(color_idx);  // "" if 0/OOB
  return hex.empty() ? std::string(category_hue(cat)) : hex;
}

int TimelineSurface::content_width() const {
  const int n = static_cast<int>(m_proj.spine.size());
  return x0() + n * COL + LEFT_PAD;
}

// s91 — the scrollable content size is the BASE geometry scaled by the zoom.
// content_width/height stay BASE (draw() works in base coords under the scale);
// only the DrawingArea's reported size — which lives in widget/event space — is
// the scaled size. Called wherever the zoom or the layout changes.
void TimelineSurface::sync_content_size() {
  m_area.set_content_width(
      std::max(static_cast<int>(std::lround(content_width()  * m_zoom)), 1));
  m_area.set_content_height(
      std::max(static_cast<int>(std::lround(content_height() * m_zoom)), 1));
}

// s91 — zoom is a UNIFORM scale of the whole surface: draw() applies one
// cr->scale(m_zoom), so every card / lane / gap / label scales together and
// stays proportional (an "actual zoom", not the per-element resize of earlier
// cuts). zoom_at_viewport multiplies m_zoom by `factor` (clamped by next_timeline_zoom)
// and keeps the content under viewport pixel `vx` fixed. Callers: Ctrl+scroll and
// Ctrl+click pass the pointer / click position; the keyboard passes the centre.
//
// The anchor does NOT accumulate: `vx` is a viewport pixel; the BASE content
// point under it is recomputed from the LIVE scroll each call
// (base = (vx + scroll) / old_zoom), so a burst can't drift the scroll to a rail.
// The new scroll lands that base point's new on-screen position (base * new_zoom)
// back under `vx`. Applied now (best effort) AND once more on the adjustment's
// `changed` (after the relayout grows the upper) — the canonical fix for set_value
// clamping against a not-yet-grown upper on zoom-in. One pending anchor connection
// at a time (m_zoom_anchor_conn), replaced each zoom.
void TimelineSurface::zoom_at_viewport(double vx, double factor) {
  const double old_zoom = m_zoom;
  const double new_zoom = next_timeline_zoom(old_zoom, factor);
  if (new_zoom == old_zoom) return;

  auto hadj = m_scroll.get_hadjustment();
  const double scroll0 = hadj ? hadj->get_value() : 0.0;
  const double base_x  = (vx + scroll0) / old_zoom;   // BASE point under the anchor (live)

  m_zoom = new_zoom;
  m_model.timeline_zoom = new_zoom;   // s91 — write through; the model persists it
  m_model.mark_modified();            // per-project state (mirrors rail-collapse toggle)
  sync_content_size();

  if (hadj) {
    const double target = base_x * new_zoom - vx;
    hadj->set_value(target);                       // best effort before relayout
    m_zoom_anchor_conn.disconnect();               // drop any prior pending anchor
    // Capture *this* (not the RefPtr) — the slot is owned by hadj, itself owned
    // by m_scroll (a member), so it dies with the surface and never fires stale;
    // capturing a RefPtr here would form a slot↔adjustment cycle and leak it.
    m_zoom_anchor_conn = hadj->signal_changed().connect([this, target]() {
      if (auto h = m_scroll.get_hadjustment()) h->set_value(target);
      m_zoom_anchor_conn.disconnect();
    });
  }
  m_area.queue_draw();
}

// s91 — keyboard zoom helpers (the Map's bare +/-/0 idiom). They anchor about the
// viewport centre (no pointer needed) and reuse zoom_at_viewport. reset_zoom uses
// the factor that returns to kTimelineZoomDefault exactly: next_timeline_zoom(z, def/z) == def.
double TimelineSurface::viewport_center_vx() const {
  auto hadj = m_scroll.get_hadjustment();
  const double page = hadj ? hadj->get_page_size() : static_cast<double>(m_area.get_width());
  return page / 2.0;
}
void TimelineSurface::zoom_in()  { zoom_at_viewport(viewport_center_vx(), 1.15); }
void TimelineSurface::zoom_out() { zoom_at_viewport(viewport_center_vx(), 1.0 / 1.15); }
void TimelineSurface::reset_zoom() {
  if (m_zoom == kTimelineZoomDefault) return;
  zoom_at_viewport(viewport_center_vx(), kTimelineZoomDefault / m_zoom);
}

// s84 — the bottom of the spine/KP/staging/subject-track region, before the
// thread band and the bottom pad. The old content_height inlined this; factored
// so the thread band and content_height share one floor.
int TimelineSurface::relief_floor() const {
  const int n_tracks = static_cast<int>(m_tracks.size());
  if (n_tracks == 0) {
    int floor_y = m_kp_lanes.empty() ? spine_top() + CARD_H : kp_top() + KP_H;
    if (staging_active()) floor_y = staging_top() + STAGE_H;
    return floor_y;
  }
  return track_top() + n_tracks * (TRACK_H + TRACK_GAP);
}

// s84 — the thread band (valid only when m_thread_lanes is non-empty).
int TimelineSurface::thread_top() const { return relief_floor() + THREAD_PAD; }
int TimelineSurface::thread_rows_top() const { return thread_top() + THREAD_HEADER_H; }

int TimelineSurface::content_height() const {
  int floor_y = relief_floor();
  if (!m_thread_lanes.empty())
    floor_y = thread_rows_top()
              + static_cast<int>(m_thread_lanes.size()) * (TRACK_H + TRACK_GAP);
  return floor_y + BOT_PAD;
}

void TimelineSurface::rebuild() {
  // s91 — the persisted zoom is the model's (per-project, written through by
  // zoom_at_viewport). Read it back on every rebuild so a project load / view re-entry
  // restores the saved zoom; clamped via next_timeline_zoom(.,1.0) so a stale
  // value can't paint a broken spine. The set_content_* below picks it up.
  // s91 — the persisted zoom is the model's (per-project, written through by
  // zoom_at_viewport). Read it back on every rebuild so a project load / view
  // re-entry restores the saved zoom; next_timeline_zoom(.,1.0) clamps a stale value.
  m_zoom = next_timeline_zoom(m_model.timeline_zoom, 1.0);

  m_proj = project_spine(spine_input_from_manuscript(m_model));
  m_spine_iids = m_proj.spine_iids();

  // s80 step 3 — subject tracks from the one unified edge projection. Labels map
  // every binder node + image fragment iid → its title/caption so the pure
  // assembler can name each track; the renderer then colours by category (§9.6).
  std::unordered_map<std::string, std::string> labels;
  for (const BinderNode* n : m_model.all_node_ptrs())
    if (n && !n->iid.empty()) labels.emplace(n->iid, n->title);
  for (const ImageFragment& f : m_model.image_pool().all())
    if (!f.deleted && !f.iid.empty()) labels.emplace(f.iid, f.caption);

  const auto edges = StoryGraph::edges_from_backlinks(m_model);
  m_tracks = assemble_tracks(m_spine_iids, edges, labels);

  // s86 — stamp each track with its subject's assigned colour (a character /
  // place / reference can carry a palette colour the user chose). Images are not
  // binder nodes, so they resolve to 0 and keep the category hue. The relief,
  // rail swatch, staging row, and sweep preview all read this (subject_hex):
  // assigned colour when set, the §9.6 category hue otherwise.
  for (TimelineTrack& tk : m_tracks) {
    const BinderNode* sn = m_model.find_node_by_iid(tk.iid);
    tk.color_idx = sn ? sn->color_idx : 0;
  }

  // s81 step 6 — the KP lane. The per-scene KP fact lives on the scene node
  // (kp_id / kp_label / color_idx, stamped by the materializer), so the adapter
  // is a plain walk of the binder — built inline here exactly like `labels`,
  // mirroring assemble_tracks' edge/label seam. assemble_kp_lanes then groups
  // the on-spine tagged scenes into arc-ordered lanes (off-spine tags drop).
  std::unordered_map<std::string, SceneKpInfo> scene_kp;
  for (const BinderNode* nptr : m_model.all_node_ptrs()) {
    if (!nptr || nptr->iid.empty() || nptr->kp_id.empty()) continue;
    // pin implies a beat (a Key Point target IS a beat), so a legacy scene
    // pinned before is_key_point existed still lands on the lane (s81).
    const bool beat = nptr->is_key_point || nptr->pin;
    scene_kp.emplace(nptr->iid,
                     SceneKpInfo{nptr->kp_id, nptr->kp_label, nptr->color_idx,
                                 beat});
  }
  m_kp_lanes = assemble_kp_lanes(m_spine_iids, scene_kp);

  // s84 step 7 — the thread lanes. A scene's assigned thread is a fact on the
  // node (BinderNode.thread, a thr_ iid), resolved to label/colour via the
  // project thread registry. Mirror of the KP build: a plain binder walk →
  // SceneThreadInfo per assigned on-spine scene; assemble_thread_lanes groups
  // them into braid-ordered lanes (off-spine / unassigned scenes drop).
  std::unordered_map<std::string, SceneThreadInfo> scene_thread;
  for (const BinderNode* nptr : m_model.all_node_ptrs()) {
    if (!nptr || nptr->iid.empty() || nptr->thread.empty()) continue;
    const ThreadDef* td = m_model.find_thread(nptr->thread);
    scene_thread.emplace(nptr->iid,
                         SceneThreadInfo{nptr->thread,
                                         td ? td->label : std::string{},
                                         td ? td->color_idx : 0});
  }
  m_thread_lanes = assemble_thread_lanes(m_spine_iids, scene_thread);

  recompute_chrono();   // s93 — refresh the world-clock order caches off the new spine + coords

  // s82 — the resource rail roster: every linkable subject (Characters / Places
  // / References binder leaves + the live image pool), claimed or not, so the
  // author can arm one with no track yet and place it on the spine (§3). Claim
  // counts come from the tracks just assembled (no claim-rule duplication).
  std::vector<ResourceCandidate> candidates;
  collect_resource_leaves(m_model.root(Section::Characters),
                          TrackCategory::Character, candidates);
  collect_resource_leaves(m_model.root(Section::Places),
                          TrackCategory::Place, candidates);
  collect_resource_leaves(m_model.root(Section::References),
                          TrackCategory::Reference, candidates);
  for (const ImageFragment& f : m_model.image_pool().all())
    if (!f.deleted && !f.iid.empty())
      candidates.push_back(ResourceCandidate{f.iid, f.caption, TrackCategory::Image});

  // If the armed thing was deleted since arming, drop the (now dangling) arm.
  // s85 — a THREAD arm is validated against the registry (its iid is never in the
  // subject candidate list, so the old subject-only scan would wrongly clear it
  // on every rebuild — i.e. right after each commit_sweep).
  if (!m_armed_iid.empty()) {
    bool still = false;
    if (m_armed_kind == ArmedKind::Thread) {
      still = (m_model.find_thread(m_armed_iid) != nullptr);
    } else if (m_armed_kind == ArmedKind::KeyPoint) {
      // s86 — a KP arm validates against the palette (its kp_ id is a swatch id,
      // never in the subject candidate list — same reason the thread guard exists).
      for (const auto& tc : m_prefs.tag_colors)
        if (tc.id == m_armed_iid) { still = true; break; }
    } else {
      for (const auto& c : candidates)
        if (c.iid == m_armed_iid) { still = true; break; }
    }
    if (!still) m_armed_iid.clear();
  }
  build_rail(assemble_resources(candidates, m_tracks));

  // s84 — keep the peek panel in sync across a rebuild: if the selected scene is
  // still on the spine, re-populate it (metadata/links may have changed); if it
  // vanished (deleted / moved off-spine), clear the selection and hide the panel.
  if (!m_selected_iid.empty()) {
    bool on_spine = false;
    for (const auto& id : m_spine_iids)
      if (id == m_selected_iid) { on_spine = true; break; }
    if (on_spine) {
      populate_peek(m_selected_iid);
    } else {
      m_selected_iid.clear();
      m_peek_revealer.set_reveal_child(false);
    }
  }

  m_empty_hint.set_visible(m_proj.spine.empty());
  // s92 — reconcile persistent focus to the rebuilt lanes: drop any pinned row
  // that vanished, then refresh the spine-walk cache from what survives (the
  // m_selected_iid peek-sync discipline, one row up).
  prune_focus();
  recompute_focus_positions();
  sync_content_size();
  m_area.queue_draw();
}

// ── Scene peek panel (s84) ───────────────────────────────────────────────────
// A single click on a scene card reveals this panel at the bottom of the lens
// with the scene's synopsis (editable — the one WRITE surface), its metadata
// (status / Key Point / thread — SHOWN), and a links-at-a-glance readout (the
// subjects that claim this scene, read off the already-computed tracks). §5.

void TimelineSurface::build_peek_panel() {
  auto* outer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
  outer->set_name("timeline-peek");
  outer->add_css_class("timeline-peek");

  // header: SCENE n  ·  title  ·············  [Open in editor]
  auto* hdr = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
  m_peek_scene_no.add_css_class("timeline-peek-scene");
  m_peek_scene_no.set_valign(Gtk::Align::CENTER);
  m_peek_title.add_css_class("timeline-peek-title");
  m_peek_title.set_halign(Gtk::Align::START);
  m_peek_title.set_hexpand(true);
  m_peek_title.set_ellipsize(Pango::EllipsizeMode::END);
  m_peek_open.set_label("Open in editor");
  m_peek_open.set_valign(Gtk::Align::CENTER);
  m_peek_open.signal_clicked().connect([this]() {
    if (!m_selected_iid.empty() && m_on_open) m_on_open(m_selected_iid);
  });
  // s84 — close: hide the panel and clear the selection (drops the card ring).
  auto* close_btn = Gtk::make_managed<Gtk::Button>();
  close_btn->set_icon_name("window-close-symbolic");
  close_btn->set_tooltip_text("Close");
  close_btn->add_css_class("flat");
  close_btn->set_valign(Gtk::Align::CENTER);
  close_btn->signal_clicked().connect([this]() {
    m_peek_revealer.set_reveal_child(false);
    m_selected_iid.clear();
    m_area.queue_draw();   // remove the selection ring
  });
  hdr->append(m_peek_scene_no);
  hdr->append(m_peek_title);
  hdr->append(m_peek_open);
  hdr->append(*close_btn);
  outer->append(*hdr);

  // body: synopsis (editable, left, expands) | metadata + links (right)
  auto* body = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 16);

  auto* syn_col = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 3);
  syn_col->set_hexpand(true);
  auto* syn_cap = Gtk::make_managed<Gtk::Label>("SYNOPSIS");
  syn_cap->add_css_class("timeline-peek-cap");
  syn_cap->set_halign(Gtk::Align::START);
  m_peek_synopsis.add_css_class("timeline-peek-synopsis");
  m_peek_synopsis.set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
  m_peek_synopsis.set_top_margin(6);
  m_peek_synopsis.set_bottom_margin(6);
  m_peek_synopsis.set_left_margin(8);
  m_peek_synopsis.set_right_margin(8);
  m_peek_synopsis_buf = m_peek_synopsis.get_buffer();
  auto* syn_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
  syn_scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  syn_scroll->set_min_content_height(64);
  syn_scroll->set_child(m_peek_synopsis);
  // edits write straight to the node (the binder card / editor read the same
  // synopsis). Guarded so populate_peek's set_text does not write back.
  m_peek_synopsis_buf->signal_changed().connect([this]() {
    if (m_peek_loading || m_selected_iid.empty()) return;
    BinderNode* n = m_model.find_node_by_iid(m_selected_iid);
    if (!n) return;
    n->synopsis = m_peek_synopsis_buf->get_text(
        m_peek_synopsis_buf->begin(), m_peek_synopsis_buf->end(), false).raw();
    m_model.mark_modified();
  });
  syn_col->append(*syn_cap);
  syn_col->append(*syn_scroll);

  auto* meta_col = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 3);
  meta_col->set_valign(Gtk::Align::START);
  auto* meta_cap = Gtk::make_managed<Gtk::Label>("METADATA");
  meta_cap->add_css_class("timeline-peek-cap");
  meta_cap->set_halign(Gtk::Align::START);
  m_peek_meta.add_css_class("timeline-peek-meta");
  m_peek_meta.set_halign(Gtk::Align::START);
  m_peek_meta.set_xalign(0.0f);
  auto* links_cap = Gtk::make_managed<Gtk::Label>("LINKED");
  links_cap->add_css_class("timeline-peek-cap");
  links_cap->set_halign(Gtk::Align::START);
  links_cap->set_margin_top(4);
  m_peek_links.add_css_class("timeline-peek-links");
  m_peek_links.set_halign(Gtk::Align::START);
  m_peek_links.set_xalign(0.0f);
  m_peek_links.set_wrap(true);
  m_peek_links.set_max_width_chars(40);
  meta_col->append(*meta_cap);
  meta_col->append(m_peek_meta);
  meta_col->append(*links_cap);
  meta_col->append(m_peek_links);

  // s93 — STORY-TIME (world-clock authoring; DESIGN_timeline.md §9.14.2). The
  // readout is the DERIVED gap to the previous dated scene; the row authors a
  // relative gap ("3 weeks later") that writes an ABSOLUTE coordinate via
  // apply_relative_gap (Option B) — reorder-safe. The on-spine gap pills are the
  // focused follow pass; this is the set/clear surface and the at-a-glance phrase.
  auto* st_cap = Gtk::make_managed<Gtk::Label>("STORY-TIME");
  st_cap->add_css_class("timeline-peek-cap");
  st_cap->set_halign(Gtk::Align::START);
  st_cap->set_margin_top(4);
  m_peek_storytime.add_css_class("timeline-peek-meta");
  m_peek_storytime.set_halign(Gtk::Align::START);
  m_peek_storytime.set_xalign(0.0f);
  m_peek_storytime.set_wrap(true);
  m_peek_storytime.set_max_width_chars(40);

  auto* st_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
  st_row->set_margin_top(2);
  m_peek_st_count.set_adjustment(Gtk::Adjustment::create(1.0, 1.0, 9999.0, 1.0, 10.0));
  m_peek_st_count.set_digits(0);
  m_peek_st_count.set_numeric(true);
  m_peek_st_count.set_width_chars(4);
  m_peek_st_unit.set_model(Gtk::StringList::create(std::vector<Glib::ustring>{
      "years", "months", "weeks", "days", "hours", "minutes", "seconds"}));
  m_peek_st_unit.set_selected(2);   // weeks
  m_peek_st_dir.set_model(Gtk::StringList::create(std::vector<Glib::ustring>{
      "later", "earlier"}));
  m_peek_st_dir.set_selected(0);    // later
  auto* st_set = Gtk::make_managed<Gtk::Button>("Set");
  st_set->add_css_class("flat");
  st_set->signal_clicked().connect([this]() { apply_story_time(); });
  auto* st_clear = Gtk::make_managed<Gtk::Button>("Clear");
  st_clear->add_css_class("flat");
  st_clear->signal_clicked().connect([this]() { clear_story_time(); });
  st_row->append(m_peek_st_count);
  st_row->append(m_peek_st_unit);
  st_row->append(m_peek_st_dir);
  st_row->append(*st_set);
  st_row->append(*st_clear);

  meta_col->append(*st_cap);
  meta_col->append(m_peek_storytime);
  meta_col->append(*st_row);

  body->append(*syn_col);
  body->append(*meta_col);
  outer->append(*body);

  m_peek_revealer.set_transition_type(Gtk::RevealerTransitionType::SLIDE_UP);
  m_peek_revealer.set_child(*outer);
  m_peek_revealer.set_reveal_child(false);
}

void TimelineSurface::select_scene(const std::string& iid) {
  m_selected_iid = iid;
  populate_peek(iid);
  m_peek_revealer.set_reveal_child(true);
  m_area.queue_draw();   // paint the selection ring on the card
}

void TimelineSurface::populate_peek(const std::string& iid) {
  const BinderNode* n = m_model.find_node_by_iid(iid);
  if (!n) { m_peek_revealer.set_reveal_child(false); return; }

  // told-order position (1-based) for the "SCENE n" badge
  int pos = 0;
  for (std::size_t i = 0; i < m_spine_iids.size(); ++i)
    if (m_spine_iids[i] == iid) { pos = static_cast<int>(i) + 1; break; }
  m_peek_scene_no.set_text(pos > 0 ? "SCENE " + std::to_string(pos) : "SCENE");
  m_peek_title.set_text(n->title.empty() ? "(untitled scene)" : n->title);

  // synopsis (guarded so set_text does not fire the write-back handler)
  m_peek_loading = true;
  m_peek_synopsis_buf->set_text(n->synopsis);
  m_peek_loading = false;

  // metadata (shown, set elsewhere)
  auto status_str = [](NodeStatus s) -> std::string {
    switch (s) {
      case NodeStatus::RoughDraft: return "Rough Draft";
      case NodeStatus::InProgress: return "In Progress";
      case NodeStatus::Polished:   return "Polished";
      case NodeStatus::Skip:       return "Skip";
      default:                     return "Untitled";
    }
  };
  std::string meta = "Status:  " + status_str(n->status);
  meta += "\nKey Point:  " + (n->kp_label.empty() ? std::string("\u2014") : n->kp_label);
  std::string thr = "\u2014";
  if (!n->thread.empty()) {
    const ThreadDef* td = m_model.find_thread(n->thread);
    thr = (td && !td->label.empty()) ? td->label : n->thread;
  }
  meta += "\nThread:  " + thr;
  if (n->word_target > 0)
    meta += "\nWord target:  " + std::to_string(n->word_target);
  m_peek_meta.set_text(meta);

  // links-at-a-glance: the subjects that claim this scene, read off the tracks
  // already assembled (one source of truth for "what a subject claims"). Each
  // category gets a verb so the readout reads in words (§5).
  auto verb = [](TrackCategory c) -> const char* {
    switch (c) {
      case TrackCategory::Place:     return "Set in";
      case TrackCategory::Reference: return "Ref";
      case TrackCategory::Image:     return "Image";
      default:                       return "Involves";
    }
  };
  std::string links;
  for (const TimelineTrack& tk : m_tracks) {
    if (tk.claimed.count(iid) == 0) continue;
    if (!links.empty()) links += "\n";
    links += std::string(verb(tk.category)) + ":  "
             + (tk.label.empty() ? std::string("(untitled)") : tk.label);
  }
  m_peek_links.set_text(links.empty() ? "Nothing linked yet." : links);

  // s93 — STORY-TIME readout: the derived gap to the previous dated told-order
  // scene (recomputed live; never persisted — §9.14.2). Undated => ordinal only.
  std::string st;
  if (!n->has_story_time) {
    st = "Undated \u2014 ordinal only.";
  } else {
    const BinderNode* prev = nullptr;
    int prev_pos = 0;
    for (int i = pos - 2; i >= 0; --i) {
      const BinderNode* p =
          m_model.find_node_by_iid(m_spine_iids[static_cast<std::size_t>(i)]);
      if (p && p->has_story_time) { prev = p; prev_pos = i + 1; break; }
    }
    if (prev)
      st = gap_phrase(n->story_time - prev->story_time)
           + " than scene " + std::to_string(prev_pos) + ".";
    else
      st = "Sets the story-time start.";
  }
  m_peek_storytime.set_text(st);
}

// s93 — Set: write THIS scene's absolute story-time from the relative gap typed
// against the nearest EARLIER dated told-order scene (the anchor). With no earlier
// dated scene this scene anchors the clock at 0 (the start); the typed offset is
// ignored in that one case. Absolute storage (Option B) keeps it reorder-safe.
// NOTE (v1 limit, §9.14.4): anchoring is "previous dated in told order"; dating
// scenes badly out of order, or anchoring to an arbitrary scene, is a follow-on.
void TimelineSurface::apply_story_time() {
  if (m_selected_iid.empty()) return;
  BinderNode* n = m_model.find_node_by_iid(m_selected_iid);
  if (!n) return;

  int pos = 0;
  for (std::size_t i = 0; i < m_spine_iids.size(); ++i)
    if (m_spine_iids[i] == m_selected_iid) { pos = static_cast<int>(i) + 1; break; }

  const BinderNode* prev = nullptr;
  for (int i = pos - 2; i >= 0; --i) {
    const BinderNode* p =
        m_model.find_node_by_iid(m_spine_iids[static_cast<std::size_t>(i)]);
    if (p && p->has_story_time) { prev = p; break; }
  }

  static const DurationUnit kUnits[] = {
      DurationUnit::Year, DurationUnit::Month, DurationUnit::Week,
      DurationUnit::Day,  DurationUnit::Hour,  DurationUnit::Minute,
      DurationUnit::Second};
  const guint ui = m_peek_st_unit.get_selected();
  const DurationUnit unit = kUnits[ui < 7u ? ui : 2u];
  const int dir = (m_peek_st_dir.get_selected() == 0u) ? +1 : -1;
  const long long count = static_cast<long long>(m_peek_st_count.get_value_as_int());

  if (prev)
    n->story_time = apply_relative_gap(prev->story_time, count, unit, dir);
  else
    n->story_time = 0;   // no earlier dated scene → this anchors the clock start
  n->has_story_time = true;

  m_model.mark_modified();
  populate_peek(m_selected_iid);   // refresh the readout
  recompute_chrono();              // s93 — story order reflects the new coordinate
  m_area.queue_draw();             // (on-spine gap pills land in the follow pass)
}

void TimelineSurface::clear_story_time() {
  if (m_selected_iid.empty()) return;
  BinderNode* n = m_model.find_node_by_iid(m_selected_iid);
  if (!n) return;
  n->has_story_time = false;
  n->story_time = 0;
  m_model.mark_modified();
  populate_peek(m_selected_iid);
  recompute_chrono();              // s93 — drop it from the story order / into the tray
  m_area.queue_draw();
}

// s93 — rebuild the world-clock order caches (§9.14.3). Pure ordering lives in
// chronological_order; this is the GTK adapter: read each told-order scene's
// coordinate off the model, sort, split into the dated axis + the undated tray.
void TimelineSurface::recompute_chrono() {
  m_title_of.clear();
  m_told_pos.clear();
  for (const auto& s : m_proj.spine) {
    m_title_of[s.iid] = s.title;
    m_told_pos[s.iid] = s.position;   // stable told/binder number — the card badge in both lenses
  }

  std::vector<ChronoScene> told;
  told.reserve(m_spine_iids.size());
  for (const auto& iid : m_spine_iids) {
    ChronoScene cs;
    cs.iid = iid;
    const BinderNode* n = m_model.find_node_by_iid(iid);
    if (n && n->has_story_time) { cs.dated = true; cs.time = n->story_time; }
    told.push_back(cs);
  }
  ChronoOrder o = chronological_order(told);
  m_chrono_order   = o.chrono;
  m_chrono_undated = o.undated;
}

// s93 — ONE scene-card painter shared by the Told Order and Chrono draws, so a scene
// is visually identical in either lens: flipping the lens only re-slots cards and
// reveals the ruler — it never restyles a card or changes its number. `badge` is the
// stable told/binder position in both lenses. The caller owns the axis dot / stem.
void TimelineSurface::draw_scene_card(const Cairo::RefPtr<Cairo::Context>& cr,
                                      const std::string& iid, const std::string& title,
                                      int order_badge, int told_badge, double cardx, int top) {
  const Gdk::RGBA c_card     = themed(m_area, "adw_surface", "#242436");
  const Gdk::RGBA c_card_tx  = themed(m_area, "tx3", "#9196b4");
  const Gdk::RGBA c_badge    = themed(m_area, "adw_overlay2", "#3a3a54");
  const Gdk::RGBA c_badge_tx = themed(m_area, "tx1", "#cdd6f4");
  const Gdk::RGBA c_border   = themed(m_area, "border_subtle", "rgba(255,255,255,0.06)");

  rounded_rect(cr, cardx, top, card_w(), CARD_H, 7);
  set_src(cr, c_card, 0.98);
  cr->fill_preserve();
  set_src(cr, c_border);
  cr->set_line_width(1.0);
  cr->stroke();

  if (!m_selected_iid.empty() && iid == m_selected_iid) {   // peeked scene reads as selected
    Gdk::RGBA sel = themed(m_area, "accent", "#89b4fa");
    rounded_rect(cr, cardx - 2.5, top - 2.5, card_w() + 5, CARD_H + 5, 9);
    set_src(cr, sel, 0.95);
    cr->set_line_width(2.0);
    cr->stroke();
  }

  const double badge_cx = cardx + BADGE_R + 3;   // upper-left: order in the current view
  const double badge_cy = top + BADGE_R + 3;
  set_src(cr, c_badge);
  cr->arc(badge_cx, badge_cy, BADGE_R, 0, 2 * M_PI);
  cr->fill();
  auto pn = m_area.create_pango_layout(std::to_string(order_badge));
  pn->set_font_description(Pango::FontDescription("monospace 8"));
  int pw = 0, ph = 0; pn->get_pixel_size(pw, ph);
  set_src(cr, c_badge_tx);
  cr->move_to(badge_cx - pw / 2.0, badge_cy - ph / 2.0);
  pn->show_in_cairo_context(cr);

  const double tr_cx = cardx + card_w() - BADGE_R - 3;   // upper-right: told/manuscript number
  const double tr_cy = top + BADGE_R + 3;
  set_src(cr, c_badge, 0.65);
  cr->arc(tr_cx, tr_cy, BADGE_R - 1.0, 0, 2 * M_PI);
  cr->fill();
  auto tn = m_area.create_pango_layout(std::to_string(told_badge));
  tn->set_font_description(Pango::FontDescription("monospace 7"));
  int tnw = 0, tnh = 0; tn->get_pixel_size(tnw, tnh);
  set_src(cr, c_badge_tx, 0.7);
  cr->move_to(tr_cx - tnw / 2.0, tr_cy - tnh / 2.0);
  tn->show_in_cairo_context(cr);

  auto tl = m_area.create_pango_layout(title.empty() ? "(untitled)" : title);
  tl->set_font_description(Pango::FontDescription("sans 8"));
  tl->set_ellipsize(Pango::EllipsizeMode::END);
  tl->set_width(static_cast<int>((card_w() - 8) * Pango::SCALE));
  tl->set_alignment(Pango::Alignment::CENTER);
  int tw = 0, th = 0; tl->get_pixel_size(tw, th);
  set_src(cr, c_card_tx);
  cr->move_to(cardx + 4, top + CARD_H - th - 6);
  tl->show_in_cairo_context(cr);
}

// s93 — the world-clock chronological view (§9.14.3). Every scene stays on the spine
// (undated ones carry forward, s93); cards are painted by the shared draw_scene_card
// so they match Told Order exactly, and the broken-axis ruler is drawn beneath.
// Relief (tracks/KP/threads) re-read under m_chrono_order is the follow pass — one
// compute_relief(m_chrono_order, …) away (the §9.14.5 substrate reuse).
void TimelineSurface::draw_story_axis(const Cairo::RefPtr<Cairo::Context>& cr) {
  const Gdk::RGBA c_card     = themed(m_area, "adw_surface", "#242436");
  const Gdk::RGBA c_card_tx  = themed(m_area, "tx3", "#9196b4");
  const Gdk::RGBA c_axis     = themed(m_area, "tx4", "#5a5d75");
  const Gdk::RGBA c_badge    = themed(m_area, "adw_overlay2", "#3a3a54");
  const Gdk::RGBA c_border   = themed(m_area, "border_subtle", "rgba(255,255,255,0.06)");
  const Gdk::RGBA c_gutter   = themed(m_area, "tx3", "#9196b4");

  const int m = static_cast<int>(m_chrono_order.size());
  const int top = spine_top();
  const double axis_y = top + CARD_H / 2.0;
  const double ruler_y = top + CARD_H + 20.0;   // the broken-axis ruler sits below the cards

  {   // gutter caption (left, beside the spine row)
    auto gl = m_area.create_pango_layout("STORY-TIME");
    gl->set_font_description(Pango::FontDescription("sans bold 9"));
    int tw = 0, th = 0; gl->get_pixel_size(tw, th);
    set_src(cr, c_gutter, 0.9);
    cr->move_to(LEFT_PAD, axis_y - th / 2.0);
    gl->show_in_cairo_context(cr);
  }
  draw_lens_toggle(cr);   // s93 — Told Order / Chrono pills (Chrono active here)

  for (int k = 0; k < m; ++k) {   // cards, chronological order — same painter as Told Order
    const std::string& iid = m_chrono_order[static_cast<std::size_t>(k)];
    const double cardx = col_cx(k) - card_w() / 2.0;
    auto it  = m_title_of.find(iid);
    auto pit = m_told_pos.find(iid);
    const std::string title = (it != m_title_of.end()) ? it->second : std::string();
    const int told = (pit != m_told_pos.end()) ? pit->second : (k + 1);
    draw_scene_card(cr, iid, title, k + 1, told, cardx, top);   // left = chrono rank, right = told #
  }

  // ── Broken-axis ruler — ALWAYS drawn so Chrono reads as a timeline (§9.14.3) ──
  // start square —— ticked line —— end square. With dated scenes it carries the
  // worded gaps, per-unit ticks, card stems and collapse-blocks; with none dated yet
  // it shows an empty ticked axis, so the surface still reads as a clock waiting for
  // times rather than looking broken. Width scales to the scene count so the empty
  // axis has a believable length.
  {
    Gdk::RGBA amber;    amber.set("#e8a13a");
    const int total = m + static_cast<int>(m_chrono_undated.size());
    const double end_x = (total >= 1) ? col_cx(total - 1) + 28.0
                                      : x0() + 6.0 * COL;

    // start handle + label at the origin
    rounded_rect(cr, x0() - 16, ruler_y - 7, 12, 14, 3);
    set_src(cr, c_badge, 0.95);
    cr->fill();
    {
      auto sl = m_area.create_pango_layout("start");
      sl->set_font_description(Pango::FontDescription("sans 8"));
      int sw = 0, sh = 0; sl->get_pixel_size(sw, sh);
      set_src(cr, c_gutter, 0.8);
      cr->move_to(x0() - 22, ruler_y + 9);
      sl->show_in_cairo_context(cr);
    }

    if (m >= 1) {
      set_src(cr, c_axis, 0.8);   // stub from handle to the first card column
      cr->set_line_width(2.0);
      cr->move_to(x0() - 4, ruler_y);
      cr->line_to(col_cx(0), ruler_y);
      cr->stroke();

      for (int k = 0; k + 1 < m; ++k) {   // one segment per gap
        const BinderNode* a =
            m_model.find_node_by_iid(m_chrono_order[static_cast<std::size_t>(k)]);
        const BinderNode* b =
            m_model.find_node_by_iid(m_chrono_order[static_cast<std::size_t>(k + 1)]);
        const long long delta = (a && b && a->has_story_time && b->has_story_time)
                                    ? (b->story_time - a->story_time) : 0;
        const UnitCount uc = coarsest_unit(delta);
        const double xL = col_cx(k), xR = col_cx(k + 1);
        const double mx = (xL + xR) / 2.0;

        if (delta != 0) {   // worded gap label above the cards
          auto gl = m_area.create_pango_layout(duration_label(delta));
          gl->set_font_description(Pango::FontDescription("sans 9"));
          int gw = 0, gh = 0; gl->get_pixel_size(gw, gh);
          set_src(cr, c_gutter, 0.75);
          cr->move_to(mx - gw / 2.0, top - gh - 4);
          gl->show_in_cairo_context(cr);
        }

        if (uc.count > 12) {   // COLLAPSE: a labelled amber block with broken ruler
          auto bl = m_area.create_pango_layout(duration_label(delta));
          bl->set_font_description(Pango::FontDescription("sans bold 8"));
          int bw = 0, bh = 0; bl->get_pixel_size(bw, bh);
          const double bwid = bw + 16.0;
          const double bhei = bh + 8.0;
          set_src(cr, c_axis, 0.8);   // stubs up to the break on each side
          cr->set_line_width(2.0);
          cr->move_to(xL, ruler_y); cr->line_to(mx - bwid / 2.0 - 8, ruler_y); cr->stroke();
          cr->move_to(mx + bwid / 2.0 + 8, ruler_y); cr->line_to(xR, ruler_y); cr->stroke();
          set_src(cr, amber, 0.9);   // // break slashes on each side
          cr->set_line_width(1.5);
          for (double sgn : {-1.0, 1.0}) {
            const double bxr = mx + sgn * (bwid / 2.0 + 6.0);
            cr->move_to(bxr - 2, ruler_y + 5); cr->line_to(bxr + 2, ruler_y - 5); cr->stroke();
            cr->move_to(bxr + 1, ruler_y + 5); cr->line_to(bxr + 5, ruler_y - 5); cr->stroke();
          }
          rounded_rect(cr, mx - bwid / 2.0, ruler_y - bhei / 2.0, bwid, bhei, bhei / 2.0);
          set_src(cr, amber, 0.18); cr->fill_preserve();
          set_src(cr, amber, 0.95); cr->set_line_width(1.0); cr->stroke();
          set_src(cr, amber, 1.0);
          cr->move_to(mx - bw / 2.0, ruler_y - bh / 2.0);
          bl->show_in_cairo_context(cr);
        } else {   // normal: segment + one tick per unit in the gap
          set_src(cr, c_axis, 0.8);
          cr->set_line_width(2.0);
          cr->move_to(xL, ruler_y); cr->line_to(xR, ruler_y); cr->stroke();
          const int nt = static_cast<int>(uc.count);
          if (nt >= 1) {
            set_src(cr, c_axis, 0.65);
            cr->set_line_width(1.0);
            for (int t = 1; t <= nt; ++t) {
              const double tx = xL + (xR - xL) * (static_cast<double>(t)
                                                  / static_cast<double>(nt + 1));
              cr->move_to(tx, ruler_y - 4); cr->line_to(tx, ruler_y + 4); cr->stroke();
            }
          }
        }
      }

      for (int k = 0; k < m; ++k) {   // stem from each card down to its ruler dot
        const double cx = col_cx(k);
        set_src(cr, c_axis, 0.5);
        cr->set_line_width(1.0);
        cr->move_to(cx, top + CARD_H); cr->line_to(cx, ruler_y); cr->stroke();
        set_src(cr, c_axis);
        cr->arc(cx, ruler_y, 2.5, 0, 2 * M_PI);
        cr->fill();
      }

      set_src(cr, c_axis, 0.8);   // extension from the last card out to the end square
      cr->set_line_width(2.0);
      cr->move_to(col_cx(m - 1), ruler_y);
      cr->line_to(end_x, ruler_y);
      cr->stroke();
    } else {
      // nothing dated yet — an empty ticked axis so it still reads as a clock
      set_src(cr, c_axis, 0.8);
      cr->set_line_width(2.0);
      cr->move_to(x0() - 4, ruler_y);
      cr->line_to(end_x, ruler_y);
      cr->stroke();
      const int ticks = std::clamp(total, 6, 16);
      const double t0 = x0() - 4;
      set_src(cr, c_axis, 0.55);
      cr->set_line_width(1.0);
      for (int t = 1; t < ticks; ++t) {
        const double tx = t0 + (end_x - t0) * (static_cast<double>(t)
                                               / static_cast<double>(ticks));
        cr->move_to(tx, ruler_y - 4); cr->line_to(tx, ruler_y + 4); cr->stroke();
      }
    }

    // end square + label (always — the far edge of the clock)
    rounded_rect(cr, end_x, ruler_y - 7, 12, 14, 3);
    set_src(cr, c_badge, 0.95);
    cr->fill();
    {
      auto el = m_area.create_pango_layout("end");
      el->set_font_description(Pango::FontDescription("sans 8"));
      int ew = 0, eh = 0; el->get_pixel_size(ew, eh);
      set_src(cr, c_gutter, 0.8);
      cr->move_to(end_x + 6 - ew / 2.0, ruler_y + 9);
      el->show_in_cairo_context(cr);
    }
  }

  if (!m_chrono_undated.empty()) {   // the undated tray (off the axis)
    const double ty = top + CARD_H + 44.0;
    const double chipW = 72.0, gap = 8.0, chipH = 26.0;
    auto cap = m_area.create_pango_layout("UNDATED");
    cap->set_font_description(Pango::FontDescription("sans bold 9"));
    int cpw = 0, cph = 0; cap->get_pixel_size(cpw, cph);
    set_src(cr, c_gutter, 0.9);
    cr->move_to(LEFT_PAD, ty - cph - 4);
    cap->show_in_cairo_context(cr);

    for (std::size_t i = 0; i < m_chrono_undated.size(); ++i) {
      const std::string& iid = m_chrono_undated[i];
      const double cxx = x0() + static_cast<double>(i) * (chipW + gap);
      rounded_rect(cr, cxx, ty, chipW, chipH, 6);
      set_src(cr, c_card, 0.98);
      cr->fill_preserve();
      set_src(cr, c_border);
      cr->set_line_width(1.0);
      cr->stroke();
      if (!m_selected_iid.empty() && iid == m_selected_iid) {
        Gdk::RGBA sel = themed(m_area, "accent", "#89b4fa");
        rounded_rect(cr, cxx - 2.0, ty - 2.0, chipW + 4, chipH + 4, 8);
        set_src(cr, sel, 0.95);
        cr->set_line_width(2.0);
        cr->stroke();
      }
      auto it = m_title_of.find(iid);
      const std::string title = (it != m_title_of.end() && !it->second.empty())
                                    ? it->second : std::string("(untitled)");
      auto tl = m_area.create_pango_layout(title);
      tl->set_font_description(Pango::FontDescription("sans 8"));
      tl->set_ellipsize(Pango::EllipsizeMode::END);
      tl->set_width(static_cast<int>((chipW - 8) * Pango::SCALE));
      tl->set_alignment(Pango::Alignment::CENTER);
      int tw = 0, th = 0; tl->get_pixel_size(tw, th);
      set_src(cr, c_card_tx);
      cr->move_to(cxx + 4, ty + (chipH - th) / 2.0);
      tl->show_in_cairo_context(cr);
    }
  }

  // s93 — relief under the chronological order (§9.14.5 substrate reuse): the SAME
  // KP / subject / thread lanes, positions recomputed from m_chrono_order. Stacked
  // below the spine and the undated tray; no focus dimming and no KP pins here.
  double y = top + CARD_H + 44.0;                      // below the broken-axis ruler
  if (!m_chrono_undated.empty()) y += 40.0;            // and below the undated tray
  if (!m_kp_lanes.empty()) {
    draw_kp_strip(cr, m_chrono_order, static_cast<int>(y), false, false);
    y += KP_H + 14.0;
  }
  if (!m_tracks.empty()) {
    draw_subject_tracks(cr, m_chrono_order, y, false);
    y += static_cast<double>(m_tracks.size()) * (TRACK_H + TRACK_GAP) + 14.0;
  }
  if (!m_thread_lanes.empty()) {
    const double hdr_y = y;
    draw_thread_band(cr, m_chrono_order, static_cast<int>(hdr_y),
                     static_cast<int>(hdr_y + THREAD_HEADER_H), false);
  }
}

// s93 — hit-test the chronological layout: dated cards on the axis, then the
// undated tray below. Geometry mirrors draw_story_axis exactly.
std::string TimelineSurface::scene_at_story(double x, double y) const {
  const int top = spine_top();
  const int m = static_cast<int>(m_chrono_order.size());
  if (y >= top && y <= top + CARD_H) {
    for (int k = 0; k < m; ++k) {
      const double cx = col_cx(k);
      if (x >= cx - card_w() / 2.0 && x <= cx + card_w() / 2.0)
        return m_chrono_order[static_cast<std::size_t>(k)];
    }
    return {};
  }
  if (!m_chrono_undated.empty()) {
    const double ty = top + CARD_H + 44.0;
    const double chipW = 72.0, gap = 8.0, chipH = 26.0;
    if (y >= ty && y <= ty + chipH) {
      for (std::size_t i = 0; i < m_chrono_undated.size(); ++i) {
        const double cxx = x0() + static_cast<double>(i) * (chipW + gap);
        if (x >= cxx && x <= cxx + chipW) return m_chrono_undated[i];
      }
    }
  }
  return {};
}

// s93 — subject-track relief, shared by the told-order draw and the story-time
// view (§9.14.5 substrate reuse). Positions come from compute_relief(order, …),
// so the SAME lanes re-lay under whichever order is handed in. apply_focus gates
// the told-order focus/hover dimming — the story view passes false (no focus
// there yet), so every row draws at full strength. ttop is the row-region top,
// computed by each caller for its own vertical stack.
void TimelineSurface::draw_subject_tracks(const Cairo::RefPtr<Cairo::Context>& cr,
                                          const std::vector<std::string>& order,
                                          double ttop, bool apply_focus) {
  const bool fo = apply_focus && focus_active(m_focus);

  // s93 — vertical card→track drop-lines. Formerly a told-only block in draw();
  // now drawn here off the passed ORDER, so the Chrono lens shows the same
  // associations (a scene's relationship lines re-aim to its chrono column). A thin
  // hue-coded dashed line from each card down to the cell of every track that claims
  // it; co-claimants in a column fan symmetrically. Drawn first so the bars sit over
  // them. Dimming follows focus/hover only when apply_focus (told); off in Chrono.
  if (!m_tracks.empty() && !order.empty()) {
    const double y_top = spine_top() + CARD_H;   // bottom edge of the scene cards
    constexpr double CONN_STEP = 4.0;             // per-claimant x stagger
    std::vector<double> cdash{2.0, 3.0};
    cr->set_line_width(2.2);
    cr->set_dash(cdash, 0.0);
    for (std::size_t col = 0; col < order.size(); ++col) {
      const std::string& iid = order[col];
      std::vector<std::size_t> claimers;
      for (std::size_t t = 0; t < m_tracks.size(); ++t)
        if (m_tracks[t].claimed.count(iid)) claimers.push_back(t);
      const std::size_t mc = claimers.size();
      for (std::size_t k = 0; k < mc; ++k) {
        const std::size_t t = claimers[k];
        const bool dimc = fo
            ? !focus_contains(m_focus, fk_subject(m_tracks[t].iid))
            : (apply_focus && m_hover_track >= 0 && static_cast<int>(t) != m_hover_track);
        Gdk::RGBA chue; chue.set(subject_hex(m_tracks[t].color_idx, m_tracks[t].category));
        const double dx = (static_cast<double>(k) - (static_cast<double>(mc) - 1.0) / 2.0)
                          * CONN_STEP;
        const double lx = col_cx(static_cast<int>(col)) + dx;
        const double ly = ttop + static_cast<double>(t) * (TRACK_H + TRACK_GAP)
                          + TRACK_H / 2.0;
        set_src(cr, chue, dimc ? 0.10 : 0.42);
        cr->move_to(lx, y_top);
        cr->line_to(lx, ly);
        cr->stroke();
      }
    }
    cr->unset_dash();
  }

  for (std::size_t i = 0; i < m_tracks.size(); ++i) {
    const TimelineTrack& tk = m_tracks[i];
    const Relief rel = compute_relief(order, tk.claimed);
    Gdk::RGBA hue; hue.set(subject_hex(tk.color_idx, tk.category));
    const double row_y = ttop + static_cast<double>(i) * (TRACK_H + TRACK_GAP);
    const double cy = row_y + TRACK_H / 2.0;

    const bool focused = fo && focus_contains(m_focus, fk_subject(tk.iid));
    const bool dim = fo
        ? !focused
        : (apply_focus && m_hover_track >= 0 && static_cast<int>(i) != m_hover_track);
    const double a_bar = dim ? 0.16 : 0.90;
    const double a_dot = dim ? 0.16 : 0.95;
    const double a_gap = dim ? 0.08 : 0.22;
    const double a_lab = dim ? 0.40 : 1.00;

    rounded_rect(cr, LEFT_PAD, cy - 5, 10, 10, 2);
    set_src(cr, hue, dim ? 0.30 : 1.0);
    cr->fill();
    if (focused) {
      Gdk::RGBA ring = themed(m_area, "accent", "#89b4fa");
      rounded_rect(cr, LEFT_PAD - 2, cy - 7, 14, 14, 3);
      set_src(cr, ring, 0.95);
      cr->set_line_width(1.5);
      cr->stroke();
    }
    auto gl = m_area.create_pango_layout(tk.label.empty() ? "(untitled)" : tk.label);
    gl->set_font_description(Pango::FontDescription("sans 10"));
    gl->set_ellipsize(Pango::EllipsizeMode::END);
    gl->set_width(static_cast<int>((GUTTER - 26) * Pango::SCALE));
    int lw = 0, lh = 0; gl->get_pixel_size(lw, lh);
    set_src(cr, themed(m_area, "tx2", "#b8bfdd"), a_lab);
    cr->move_to(LEFT_PAD + 16, cy - lh / 2.0);
    gl->show_in_cairo_context(cr);

    std::vector<double> dashes{3.0, 3.0};
    cr->set_dash(dashes, 0.0);
    set_src(cr, hue, a_gap);
    cr->set_line_width(1.0);
    for (const auto& g : rel.gaps) {
      cr->move_to(col_cx(g.start_pos - 1), cy);
      cr->line_to(col_cx(g.end_pos), cy);
      cr->stroke();
    }
    cr->unset_dash();

    for (const auto& seg : rel.segments) {
      if (seg.kind == ReliefSegment::Kind::Bar) {
        const double bx = col_cx(seg.start_pos - 1) - card_w() / 2.0;
        const double bw = (col_cx(seg.end_pos - 1) + card_w() / 2.0) - bx;
        rounded_rect(cr, bx, cy - BAR_H / 2.0, bw, BAR_H, BAR_H / 2.0);
        set_src(cr, hue, a_bar);
        cr->fill();
      } else {
        const double sx = col_cx(seg.start_pos - 1) - card_w() / 2.0;
        rounded_rect(cr, sx, cy - BAR_H / 2.0, card_w(), BAR_H, BAR_H / 2.0);
        set_src(cr, hue, a_dot);
        cr->fill();
      }
    }
  }
}

// s93 — KP-strip relief, shared by both axes (§9.14.5). Positions from
// compute_relief(order, …); apply_focus gates told-order focus dimming; draw_pins
// draws the persistent card→strip connectors (told only — they assume the strip
// sits directly under the spine row). kt is the strip-row top.
void TimelineSurface::draw_kp_strip(const Cairo::RefPtr<Cairo::Context>& cr,
                                    const std::vector<std::string>& order,
                                    int kt, bool apply_focus, bool draw_pins) {
  if (m_kp_lanes.empty()) return;
  const bool fo = apply_focus && focus_active(m_focus);
  const int top = spine_top();
  const double kcy = kt + KP_H / 2.0;
  const Gdk::RGBA c_gutter = themed(m_area, "tx3", "#9196b4");

  auto gl = m_area.create_pango_layout("KEY POINTS");
  gl->set_font_description(Pango::FontDescription("sans bold 9"));
  int gw = 0, gh = 0; gl->get_pixel_size(gw, gh);
  set_src(cr, c_gutter, 0.9);
  cr->move_to(LEFT_PAD, kcy - gh / 2.0);
  gl->show_in_cairo_context(cr);

  Gdk::RGBA kp_dark; kp_dark.set("#11111b");

  for (const auto& lane : m_kp_lanes) {
    const Relief rel = compute_relief(order, lane.claimed);
    Gdk::RGBA hue; hue.set(kp_hex(lane.color_idx));
    const bool kp_focused = fo && focus_contains(m_focus, fk_keypoint(lane.kp_id));
    const double km = (fo && !kp_focused) ? 0.18 : 1.0;

    if (draw_pins) {
      set_src(cr, hue, 0.85 * km);
      cr->set_line_width(1.4);
      for (const auto& seg : rel.segments)
        for (int p = seg.start_pos; p <= seg.end_pos; ++p) {
          const double px = col_cx(p - 1);
          cr->move_to(px, top + CARD_H);
          cr->line_to(px, kt);
          cr->stroke();
          cr->arc(px, top + CARD_H, 2.2, 0, 2 * M_PI);
          cr->fill();
        }
    }

    auto seg_is_target = [this](const ReliefSegment& s) {
      for (const std::string& id : s.iids) {
        const BinderNode* n = m_model.find_node_by_iid(id);
        if (n && n->pin) return true;
      }
      return false;
    };
    Gdk::RGBA halo; halo.set("#ffffff");
    for (const auto& seg : rel.segments) {
      const bool target = seg_is_target(seg);
      if (seg.kind == ReliefSegment::Kind::Bar) {
        const double bx = col_cx(seg.start_pos - 1) - card_w() / 2.0;
        const double bw = (col_cx(seg.end_pos - 1) + card_w() / 2.0) - bx;
        rounded_rect(cr, bx, kcy - KP_BAR_H / 2.0, bw, KP_BAR_H, KP_BAR_H / 2.0);
        set_src(cr, hue, 0.95 * km);
        cr->fill_preserve();
        if (target) {
          set_src(cr, halo, 0.85 * km);
          cr->set_line_width(1.5);
          cr->stroke();
        } else {
          cr->begin_new_path();
        }
        if (!lane.label.empty()) {
          auto bl = m_area.create_pango_layout(lane.label);
          bl->set_font_description(Pango::FontDescription("sans 8"));
          bl->set_ellipsize(Pango::EllipsizeMode::END);
          bl->set_width(static_cast<int>((bw - 8) * Pango::SCALE));
          bl->set_alignment(Pango::Alignment::CENTER);
          int bw2 = 0, bh2 = 0; bl->get_pixel_size(bw2, bh2);
          set_src(cr, kp_dark, 0.92 * km);
          cr->move_to(bx + 4, kcy - bh2 / 2.0);
          bl->show_in_cairo_context(cr);
        }
      } else {
        const double dx = col_cx(seg.start_pos - 1);
        const double r  = target ? KP_DIAMOND_R + 2.0 : KP_DIAMOND_R;
        cr->begin_new_sub_path();
        cr->move_to(dx, kcy - r);
        cr->line_to(dx + r, kcy);
        cr->line_to(dx, kcy + r);
        cr->line_to(dx - r, kcy);
        cr->close_path();
        set_src(cr, hue, 0.95 * km);
        cr->fill_preserve();
        set_src(cr, target ? halo : kp_dark, (target ? 0.9 : 0.5) * km);
        cr->set_line_width(target ? 1.5 : 0.75);
        cr->stroke();
        if (!lane.label.empty()) {
          auto dl = m_area.create_pango_layout(lane.label);
          dl->set_font_description(Pango::FontDescription("sans 8"));
          int dw = 0, dh = 0; dl->get_pixel_size(dw, dh);
          set_src(cr, themed(m_area, "tx2", "#b8bfdd"), 0.95 * km);
          cr->move_to(dx - dw / 2.0, kcy - r - dh - 1);
          dl->show_in_cairo_context(cr);
        }
      }
    }
  }
}

// s93 — thread-band relief, shared by both axes (§9.14.5). hdr_y = header row top,
// rtop = first lane row top; apply_focus gates told-order focus dimming.
void TimelineSurface::draw_thread_band(const Cairo::RefPtr<Cairo::Context>& cr,
                                       const std::vector<std::string>& order,
                                       int hdr_y, int rtop, bool apply_focus) {
  if (m_thread_lanes.empty()) return;
  const bool fo = apply_focus && focus_active(m_focus);
  const Gdk::RGBA c_gutter = themed(m_area, "tx3", "#9196b4");

  auto hl = m_area.create_pango_layout("STORY THREADS");
  hl->set_font_description(Pango::FontDescription("sans bold 9"));
  int hlw = 0, hlh = 0; hl->get_pixel_size(hlw, hlh);
  set_src(cr, c_gutter, 0.9);
  cr->move_to(LEFT_PAD, hdr_y + (THREAD_HEADER_H - hlh) / 2.0);
  hl->show_in_cairo_context(cr);

  for (std::size_t i = 0; i < m_thread_lanes.size(); ++i) {
    const ThreadLane& ln = m_thread_lanes[i];
    const Relief rel = compute_relief(order, ln.claimed);
    Gdk::RGBA hue; hue.set(thread_hex(ln.color_idx));
    const double row_y = rtop + static_cast<double>(i) * (TRACK_H + TRACK_GAP);
    const double cy = row_y + TRACK_H / 2.0;

    const bool focused = fo && focus_contains(m_focus, fk_thread(ln.thread_key));
    const bool dim = fo && !focused;
    const double a_bar = dim ? 0.16 : 0.90;
    const double a_dot = dim ? 0.16 : 0.95;
    const double a_gap = dim ? 0.08 : 0.22;
    const double a_lab = dim ? 0.40 : 1.00;

    rounded_rect(cr, LEFT_PAD, cy - 5, 10, 10, 2);
    set_src(cr, hue, dim ? 0.30 : 1.0);
    cr->fill();
    if (focused) {
      Gdk::RGBA ring = themed(m_area, "accent", "#89b4fa");
      rounded_rect(cr, LEFT_PAD - 2, cy - 7, 14, 14, 3);
      set_src(cr, ring, 0.95);
      cr->set_line_width(1.5);
      cr->stroke();
    }
    auto gl = m_area.create_pango_layout(ln.label.empty() ? std::string("(thread)") : ln.label);
    gl->set_font_description(Pango::FontDescription("sans 10"));
    gl->set_ellipsize(Pango::EllipsizeMode::END);
    gl->set_width(static_cast<int>((GUTTER - 26) * Pango::SCALE));
    int lw = 0, lh = 0; gl->get_pixel_size(lw, lh);
    set_src(cr, themed(m_area, "tx2", "#b8bfdd"), a_lab);
    cr->move_to(LEFT_PAD + 16, cy - lh / 2.0);
    gl->show_in_cairo_context(cr);

    std::vector<double> dashes{3.0, 3.0};
    cr->set_dash(dashes, 0.0);
    set_src(cr, hue, a_gap);
    cr->set_line_width(1.0);
    for (const auto& g : rel.gaps) {
      cr->move_to(col_cx(g.start_pos - 1), cy);
      cr->line_to(col_cx(g.end_pos), cy);
      cr->stroke();
    }
    cr->unset_dash();

    for (const auto& seg : rel.segments) {
      if (seg.kind == ReliefSegment::Kind::Bar) {
        const double bx = col_cx(seg.start_pos - 1) - card_w() / 2.0;
        const double bw = (col_cx(seg.end_pos - 1) + card_w() / 2.0) - bx;
        rounded_rect(cr, bx, cy - BAR_H / 2.0, bw, BAR_H, BAR_H / 2.0);
        set_src(cr, hue, a_bar);
        cr->fill();
      } else {
        const double sx = col_cx(seg.start_pos - 1) - card_w() / 2.0;
        rounded_rect(cr, sx, cy - BAR_H / 2.0, card_w(), BAR_H, BAR_H / 2.0);
        set_src(cr, hue, a_dot);
        cr->fill();
      }
    }
  }
}


void TimelineSurface::build_rail(const std::vector<ResourceGroup>& groups) {
  // Tear down the previous rows (managed children destroyed on remove); the
  // member m_rail_empty is merely unparented and re-appended below.
  m_rail_rows.clear();
  m_rail_disclosures.clear();   // s90 — repopulated as disclosures are built below
  while (Gtk::Widget* c = m_rail_box.get_first_child()) m_rail_box.remove(*c);

  bool any_subjects = false;
  for (const ResourceGroup& g : groups) any_subjects = any_subjects || !g.items.empty();

  // s85 — the subject-roster hint shows when there are no linkable subjects, but
  // the Story Threads section (below) renders ALWAYS — it hosts the inline mint
  // row, so a thread can be created even in an otherwise-empty project. The hint
  // and the thread builder coexist rather than the hint replacing the whole rail.
  if (!any_subjects) {
    m_rail_empty.set_visible(true);
    m_rail_box.append(m_rail_empty);
  } else {
    m_rail_empty.set_visible(false);
  }

  for (const ResourceGroup& g : groups) {
    if (g.items.empty()) continue;

    // Category disclosure (binder-style, s90): a clickable header row titled with
    // the §9.6 heading, its Revealer body the subject rows. The author can collapse
    // a category; the choice persists across rebuilds AND save/load (s90) via
    // DocumentModel::timeline_rail_collapsed, keyed by the TrackCategory enum value.
    const int cat_key = static_cast<int>(g.category);
    Gtk::Box* body = add_rail_disclosure(category_heading(g.category), cat_key);

    for (const ResourceItem& it : g.items) {
      // s86 — the row swatch is the subject's own colour when set, else the
      // category hue (images have none → category). Per-item, not per-group.
      const BinderNode* itn = m_model.find_node_by_iid(it.iid);
      Gdk::RGBA hue; hue.set(subject_hex(itn ? itn->color_idx : 0, g.category));
      auto* btn = Gtk::make_managed<Gtk::Button>();
      btn->set_has_frame(false);
      btn->add_css_class("timeline-rail-row");
      btn->set_name(widget_name("timeline-rail-row", it.iid));
      if (it.iid == m_armed_iid) btn->add_css_class("armed");

      auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);

      // category swatch (a small rounded square in the §9.6 hue)
      auto* sw = Gtk::make_managed<Gtk::DrawingArea>();
      sw->set_content_width(10);
      sw->set_content_height(10);
      sw->set_valign(Gtk::Align::CENTER);
      sw->set_draw_func([hue](const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
        const double r = 2.0;
        cr->begin_new_sub_path();
        cr->arc(w - r, r,     r, -M_PI / 2, 0);
        cr->arc(w - r, h - r, r, 0, M_PI / 2);
        cr->arc(r,     h - r, r, M_PI / 2, M_PI);
        cr->arc(r,     r,     r, M_PI, 3 * M_PI / 2);
        cr->close_path();
        cr->set_source_rgb(hue.get_red(), hue.get_green(), hue.get_blue());
        cr->fill();
      });
      row->append(*sw);

      const std::string text = it.label.empty() ? it.iid : it.label;
      auto* name = Gtk::make_managed<Gtk::Label>(text);
      name->set_xalign(0.0f);
      name->set_ellipsize(Pango::EllipsizeMode::END);
      name->set_hexpand(true);
      // Cap the natural width so a long caption (e.g. a 3840px image filename)
      // cannot force the rail wide; it ellipsizes within whatever the divider
      // gives. The full text stays available as the row tooltip.
      name->set_max_width_chars(24);
      name->set_tooltip_text(text);
      name->add_css_class("timeline-rail-name");
      row->append(*name);

      if (it.claim_count > 0) {
        auto* cnt = Gtk::make_managed<Gtk::Label>(std::to_string(it.claim_count));
        cnt->add_css_class("timeline-rail-count");
        cnt->set_valign(Gtk::Align::CENTER);
        row->append(*cnt);
      }

      btn->set_child(*row);
      const std::string iid = it.iid, label = text;
      const TrackCategory cat = g.category;
      btn->signal_clicked().connect(
          [this, iid, label, cat]() { arm_subject(iid, label, cat); });

      body->append(*btn);
      m_rail_rows.emplace_back(it.iid, btn);
    }
  }

  // s85 — the Story Threads section (the §9.12.6 #4 batch-assign entry), below the
  // subject groups. Registry-sourced, with an inline mint row; always rendered.
  build_thread_rail_section();
  build_kp_rail_section();
}

// s90 — binder-style rail disclosure (mirrors the Sidebar pomodoro tile pattern).
// A header row [heading | chevron] wrapped in a GestureClick, above a Revealer
// holding the body. The whole header row is the click target, so clicking the
// chevron OR the name toggles — fixing the Gtk::Expander behaviour where only the
// label area responded and the disclosure arrow was dead. Open/closed persists in
// DocumentModel::timeline_rail_collapsed under `cat_key` (key present = collapsed),
// surviving save/load as well as rebuilds. Returns the body Box for the caller to fill.
Gtk::Box* TimelineSurface::add_rail_disclosure(const std::string& heading, int cat_key) {
  const bool expanded = !m_model.is_rail_collapsed(cat_key);

  auto* card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  card->add_css_class("timeline-rail-group");

  auto* hdr_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
  hdr_row->add_css_class("timeline-rail-group-hdr");
  hdr_row->set_cursor(Gdk::Cursor::create("pointer"));

  auto* hdr = Gtk::make_managed<Gtk::Label>(heading);
  hdr->set_xalign(0.0f);
  hdr->set_hexpand(true);
  hdr->add_css_class("timeline-rail-header");
  hdr->set_name("timeline-rail-header");
  hdr_row->append(*hdr);

  auto* arrow = Gtk::make_managed<Gtk::Label>(expanded ? "\u25be" : "\u25b8");  // ▾ / ▸
  arrow->add_css_class("timeline-rail-arrow");
  arrow->set_valign(Gtk::Align::CENTER);
  hdr_row->append(*arrow);
  card->append(*hdr_row);

  auto* rev = Gtk::make_managed<Gtk::Revealer>();
  rev->set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);
  rev->set_transition_duration(160);
  rev->set_reveal_child(expanded);

  auto* body = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  body->add_css_class("timeline-rail-group-body");
  rev->set_child(*body);
  card->append(*rev);

  // The whole header row toggles. State lives on the model
  // (DocumentModel::timeline_rail_collapsed) as the single source of truth, so it
  // persists across save/load and rebuilds — no surface-side cache to drift.
  // set_rail_collapsed marks the project modified. No idle defer (the s24 rule):
  // this flips a flag and animates the Revealer in place; it triggers no model
  // rebuild and frees no tree, so an immediate toggle is safe in the handler.
  // Ctrl+Alt+click applies to ALL categories at once — the clicked category's
  // current state picks the verb (it's collapsed → expand all; else collapse all).
  hdr_row->set_tooltip_text("Click to toggle \u00b7 Ctrl+Alt+click to expand/collapse all");
  auto gc = Gtk::GestureClick::create();
  gc->set_button(1);
  Gtk::GestureClick* gcp = gc.get();   // raw: hdr_row owns the controller (no cycle)
  gc->signal_pressed().connect([this, cat_key, rev, arrow, gcp](int, double, double) {
    const Gdk::ModifierType st = gcp->get_current_event_state();
    const bool ctrl = (st & Gdk::ModifierType::CONTROL_MASK) != Gdk::ModifierType{};
    const bool alt  = (st & Gdk::ModifierType::ALT_MASK)     != Gdk::ModifierType{};
    if (ctrl && alt) {
      // Apply to all; direction from THIS category's current state.
      set_all_rail_disclosures(/*expand=*/m_model.is_rail_collapsed(cat_key));
      return;
    }
    const bool now_expanded = m_model.is_rail_collapsed(cat_key);  // collapsed → will expand
    m_model.set_rail_collapsed(cat_key, !now_expanded);
    rev->set_reveal_child(now_expanded);
    arrow->set_text(now_expanded ? "\u25be" : "\u25b8");
  });
  hdr_row->add_controller(gc);

  m_rail_disclosures.push_back({cat_key, rev, arrow});

  m_rail_box.append(*card);
  return body;
}

// s90 — expand or collapse EVERY rail disclosure built this pass, in place (no
// rebuild). Drives the model (single source of truth, marks modified on change)
// then the live Revealer + chevron of each registered disclosure. Invoked by a
// Ctrl+Alt+click on any category header.
void TimelineSurface::set_all_rail_disclosures(bool expand) {
  for (const RailDisclosure& d : m_rail_disclosures) {
    m_model.set_rail_collapsed(d.key, !expand);
    if (d.rev)   d.rev->set_reveal_child(expand);
    if (d.arrow) d.arrow->set_text(expand ? "\u25be" : "\u25b8");
  }
}


// (DocumentModel::threads()) plus an inline "new thread" mint row at the bottom.
// Mirrors a subject group exactly (disclosure + .timeline-rail-row buttons, the
// same collapse-memory keyed off a sentinel int, the same armed CSS + m_rail_rows
// bookkeeping), but rows arm a THREAD (arm_thread) and carry the thread palette
// hue. Claim counts come from the already-assembled m_thread_lanes — the lane's
// claimed-set size — so the count rule is read, never re-derived (the same
// discipline as subject claim counts reading from m_tracks).
void TimelineSurface::build_thread_rail_section() {
  const auto& threads = m_model.threads();

  // A sentinel collapse key distinct from any TrackCategory enum value (those
  // are 0..3); -1 is the Threads disclosure's own remembered open/closed state.
  constexpr int THREAD_CAT_KEY = -1;

  Gtk::Box* body = add_rail_disclosure("Story Threads", THREAD_CAT_KEY);

  // s86 — an always-available "what's a story thread?" link opening the teaching
  // popover (the concept is not self-evident — it's a label, not a binder node).
  auto* help = Gtk::make_managed<Gtk::Button>("\u24d8  What\u2019s a story thread?");
  help->set_has_frame(false);
  help->add_css_class("timeline-thread-help-link");
  help->set_halign(Gtk::Align::START);
  help->signal_clicked().connect([this, help]() { show_thread_help(help); });
  body->append(*help);

  for (const ThreadDef& td : threads) {
    Gdk::RGBA hue; hue.set(thread_hex(td.color_idx));

    auto* btn = Gtk::make_managed<Gtk::Button>();
    btn->set_has_frame(false);
    btn->add_css_class("timeline-rail-row");
    btn->set_name(widget_name("timeline-rail-row", td.iid));
    if (td.iid == m_armed_iid && m_armed_kind == ArmedKind::Thread)
      btn->add_css_class("armed");

    auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);

    auto* sw = Gtk::make_managed<Gtk::DrawingArea>();
    sw->set_content_width(10);
    sw->set_content_height(10);
    sw->set_valign(Gtk::Align::CENTER);
    sw->set_draw_func([hue](const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
      const double r = 2.0;
      cr->begin_new_sub_path();
      cr->arc(w - r, r,     r, -M_PI / 2, 0);
      cr->arc(w - r, h - r, r, 0, M_PI / 2);
      cr->arc(r,     h - r, r, M_PI / 2, M_PI);
      cr->arc(r,     r,     r, M_PI, 3 * M_PI / 2);
      cr->close_path();
      cr->set_source_rgb(hue.get_red(), hue.get_green(), hue.get_blue());
      cr->fill();
    });
    row->append(*sw);

    const std::string text = td.label.empty() ? std::string("(thread)") : td.label;
    auto* name = Gtk::make_managed<Gtk::Label>(text);
    name->set_xalign(0.0f);
    name->set_ellipsize(Pango::EllipsizeMode::END);
    name->set_hexpand(true);
    name->set_max_width_chars(24);
    name->set_tooltip_text(text);
    name->add_css_class("timeline-rail-name");
    row->append(*name);

    // claim count = the thread lane's claimed-set size (on-spine scenes assigned
    // this thread). Read from m_thread_lanes, not re-derived.
    int claim_count = 0;
    for (const ThreadLane& ln : m_thread_lanes)
      if (ln.thread_key == td.iid) { claim_count = static_cast<int>(ln.claimed.size()); break; }
    if (claim_count > 0) {
      auto* cnt = Gtk::make_managed<Gtk::Label>(std::to_string(claim_count));
      cnt->add_css_class("timeline-rail-count");
      cnt->set_valign(Gtk::Align::CENTER);
      row->append(*cnt);
    }

    btn->set_child(*row);
    const std::string iid = td.iid, label = text;
    const int cidx = td.color_idx;
    btn->signal_clicked().connect(
        [this, iid, label, cidx]() { arm_thread(iid, label, cidx); });
    btn->set_tooltip_text("Click to arm \u00b7 right-click to rename, recolour or delete");

    // s86 — secondary (right) click opens the manage popover (rename / recolour
    // / delete-unused), the mirror of the subject track's right-click menu. The
    // gesture's coords are in the row button's space; translate them into *this
    // (the popover's stable parent) before pointing.
    auto sec = Gtk::GestureClick::create();
    sec->set_button(GDK_BUTTON_SECONDARY);
    Gtk::GestureClick* secp = sec.get();   // raw: btn owns the controller (no cycle)
    sec->signal_pressed().connect(
        [this, secp, btn, iid](int /*n*/, double bx, double by) {
          secp->set_state(Gtk::EventSequenceState::CLAIMED);
          double rx = 0.0, ry = 0.0;
          btn->translate_coordinates(*this, bx, by, rx, ry);
          show_thread_menu(iid, rx, ry);
        });
    btn->add_controller(sec);

    body->append(*btn);
    m_rail_rows.emplace_back(td.iid, btn);
  }

  // s86 — empty-state nudge: when the project has no threads yet, a one-line
  // prompt above the mint row (the help link carries the full explanation).
  if (threads.empty()) {
    auto* nudge = Gtk::make_managed<Gtk::Label>(
        "No threads yet \u2014 mark independent story arcs (Main, Protagonist, "
        "Antagonist, Backstory\u2026) as coloured lanes under the spine. Name one "
        "below to start.");
    nudge->set_wrap(true);
    nudge->set_xalign(0.0f);
    nudge->set_max_width_chars(24);
    nudge->add_css_class("dim-label");
    nudge->add_css_class("timeline-thread-empty");
    body->append(*nudge);
  }

  // Inline mint row — "New thread…" + a "+" that creates an auto-coloured thread
  // (cycling the palette, mirroring the Inspector mint) and immediately ARMS it,
  // so the author can sweep it onto the spine without leaving the lens. Rename /
  // recolour / delete stay out of this slice (the deferred thread-management UI).
  auto* mint = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
  mint->add_css_class("timeline-rail-mint");
  mint->set_margin_top(2);
  auto* entry = Gtk::make_managed<Gtk::Entry>();
  entry->set_placeholder_text("New thread\u2026");
  entry->set_hexpand(true);
  entry->set_max_width_chars(14);
  auto* add = Gtk::make_managed<Gtk::Button>("+");
  add->set_tooltip_text("Create a thread and arm it for a spine sweep");
  add->add_css_class("flat");

  auto do_mint = [this, entry]() {
    std::string nm = entry->get_text().raw();
    const auto b = nm.find_first_not_of(" \t\n\r");
    const auto e = nm.find_last_not_of(" \t\n\r");
    nm = (b == std::string::npos) ? std::string() : nm.substr(b, e - b + 1);
    if (nm.empty()) return;
    int cidx = 0;
    if (!m_prefs.tag_colors.empty())
      cidx = static_cast<int>(m_model.threads().size() % m_prefs.tag_colors.size()) + 1;
    ThreadDef& td = m_model.add_thread(nm, cidx);
    m_model.mark_modified();
    const std::string iid = td.iid, label = nm;
    entry->set_text("");
    // s85 — defer the rail rebuild (which destroys THIS entry) to an idle tick so
    // we never tear down the widget that is mid-`activate` emission (the s24 rule).
    // On the tick: rebuild lists the new thread, then arm it ready to sweep.
    Glib::signal_idle().connect_once([this, iid, label, cidx]() {
      rebuild();
      arm_thread(iid, label, cidx);
    });
  };
  add->signal_clicked().connect(do_mint);
  entry->signal_activate().connect(do_mint);
  mint->append(*entry);
  mint->append(*add);
  body->append(*mint);
}

// s86 — the rail's Key Points section: mint / place / manage a KP in one place.
// A KP is a palette swatch ("the palette is the arc"), so the roster is
// m_prefs.tag_colors; each row arms a KP (arm_keypoint) and a sweep stamps the
// beat. Mirrors build_thread_rail_section (disclosure + .timeline-rail-row buttons,
// collapse-memory on a distinct sentinel key, the help link + empty nudge + an
// inline mint row + a per-row right-click manage popover).
void TimelineSurface::build_kp_rail_section() {
  const auto& palette = m_prefs.tag_colors;

  // A sentinel collapse key distinct from the TrackCategory values (0..3) and
  // the threads section (-1); -2 is the Key Points disclosure's own state.
  constexpr int KP_CAT_KEY = -2;

  Gtk::Box* body = add_rail_disclosure("Key Points", KP_CAT_KEY);

  auto* help = Gtk::make_managed<Gtk::Button>("\u24d8  What\u2019s a key point?");
  help->set_has_frame(false);
  help->add_css_class("timeline-thread-help-link");
  help->set_halign(Gtk::Align::START);
  help->signal_clicked().connect([this, help]() { show_kp_help(help); });
  body->append(*help);

  for (std::size_t i = 0; i < palette.size(); ++i) {
    const TagColor& tc = palette[i];
    const int color_idx = static_cast<int>(i) + 1;   // 1-based palette position
    Gdk::RGBA hue; hue.set(tc.hex.empty() ? std::string("#fab387") : tc.hex);

    auto* btn = Gtk::make_managed<Gtk::Button>();
    btn->set_has_frame(false);
    btn->add_css_class("timeline-rail-row");
    btn->set_name(widget_name("timeline-rail-row", tc.id));
    if (tc.id == m_armed_iid && m_armed_kind == ArmedKind::KeyPoint)
      btn->add_css_class("armed");

    auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);

    auto* sw = Gtk::make_managed<Gtk::DrawingArea>();
    sw->set_content_width(10);
    sw->set_content_height(10);
    sw->set_valign(Gtk::Align::CENTER);
    sw->set_draw_func([hue](const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
      const double r = 2.0;
      cr->begin_new_sub_path();
      cr->arc(w - r, r,     r, -M_PI / 2, 0);
      cr->arc(w - r, h - r, r, 0, M_PI / 2);
      cr->arc(r,     h - r, r, M_PI / 2, M_PI);
      cr->arc(r,     r,     r, M_PI, 3 * M_PI / 2);
      cr->close_path();
      cr->set_source_rgb(hue.get_red(), hue.get_green(), hue.get_blue());
      cr->fill();
    });
    row->append(*sw);

    const std::string text = tc.name.empty() ? std::string("(key point)") : tc.name;
    auto* name = Gtk::make_managed<Gtk::Label>(text);
    name->set_xalign(0.0f);
    name->set_ellipsize(Pango::EllipsizeMode::END);
    name->set_hexpand(true);
    name->set_max_width_chars(24);
    name->set_tooltip_text(text);
    name->add_css_class("timeline-rail-name");
    row->append(*name);

    const int claim_count = kp_usage_count(tc.id);   // on-spine beats; read, not re-derived
    if (claim_count > 0) {
      auto* cnt = Gtk::make_managed<Gtk::Label>(std::to_string(claim_count));
      cnt->add_css_class("timeline-rail-count");
      cnt->set_valign(Gtk::Align::CENTER);
      row->append(*cnt);
    }

    btn->set_child(*row);
    const std::string kp_id = tc.id, label = text;
    btn->signal_clicked().connect(
        [this, kp_id, label, color_idx]() { arm_keypoint(kp_id, label, color_idx); });
    btn->set_tooltip_text("Click to arm \u00b7 right-click to rename, recolour or delete");

    auto sec = Gtk::GestureClick::create();
    sec->set_button(GDK_BUTTON_SECONDARY);
    Gtk::GestureClick* secp = sec.get();
    sec->signal_pressed().connect(
        [this, secp, btn, kp_id](int /*n*/, double bx, double by) {
          secp->set_state(Gtk::EventSequenceState::CLAIMED);
          double rx = 0.0, ry = 0.0;
          btn->translate_coordinates(*this, bx, by, rx, ry);
          show_kp_menu(kp_id, rx, ry);
        });
    btn->add_controller(sec);

    body->append(*btn);
    m_rail_rows.emplace_back(tc.id, btn);
  }

  if (palette.empty()) {
    auto* nudge = Gtk::make_managed<Gtk::Label>(
        "No key points yet \u2014 mark structural beats (Inciting Incident, "
        "Midpoint, Climax\u2026) on the scenes that land them. Name one below to "
        "start, or use New from Pattern for a whole arc.");
    nudge->set_wrap(true);
    nudge->set_xalign(0.0f);
    nudge->set_max_width_chars(24);
    nudge->add_css_class("dim-label");
    nudge->add_css_class("timeline-thread-empty");
    body->append(*nudge);
  }

  // Inline mint row — "New key point…" + a "+" that appends a palette swatch
  // (stable kp_ id, auto-coloured from the reference ramp) and arms it for a
  // spine sweep. The whole create+place flow in one place (the s86 point).
  auto* mint = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
  mint->add_css_class("timeline-rail-mint");
  mint->set_margin_top(2);
  auto* entry = Gtk::make_managed<Gtk::Entry>();
  entry->set_placeholder_text("New key point\u2026");
  entry->set_hexpand(true);
  entry->set_max_width_chars(14);
  auto* add = Gtk::make_managed<Gtk::Button>("+");
  add->set_tooltip_text("Create a key point and arm it for a spine sweep");
  add->add_css_class("flat");

  auto do_mint = [this, entry]() {
    std::string nm = entry->get_text().raw();
    const auto b = nm.find_first_not_of(" \t\n\r");
    const auto e = nm.find_last_not_of(" \t\n\r");
    nm = (b == std::string::npos) ? std::string() : nm.substr(b, e - b + 1);
    if (nm.empty()) return;
    const std::vector<TagColor> ref = FolioPrefs{}.tag_colors;
    std::string hex = ref.empty()
        ? std::string("#fab387")
        : ref[m_prefs.tag_colors.size() % ref.size()].hex;
    TagColor tc;
    tc.name = nm;
    tc.hex  = hex;
    tc.id   = make_iid(IidKind::KeyPoint);
    m_prefs.tag_colors.push_back(tc);
    m_prefs.save();
    notify_palette_changed();   // s89 — new swatch appears in the dropdowns
    const std::string kp_id = tc.id, label = nm;
    const int color_idx = static_cast<int>(m_prefs.tag_colors.size());  // new last position
    entry->set_text("");
    // Defer the rail rebuild (which destroys THIS entry) to idle (the s24 rule);
    // on the tick, rebuild lists the new KP, then arm it ready to sweep.
    Glib::signal_idle().connect_once([this, kp_id, label, color_idx]() {
      rebuild();
      arm_keypoint(kp_id, label, color_idx);
    });
  };
  add->signal_clicked().connect(do_mint);
  entry->signal_activate().connect(do_mint);
  mint->append(*entry);
  mint->append(*add);
  body->append(*mint);
}

void TimelineSurface::arm_subject(const std::string& iid, const std::string& label,
                                  TrackCategory cat) {
  if (m_armed_iid == iid && m_armed_kind == ArmedKind::Subject) { disarm(); return; }
  m_armed_kind = ArmedKind::Subject;
  m_armed_iid = iid;
  m_armed_label = label;
  m_armed_cat = cat;
  // s86 — carry the subject's assigned colour so the staging row + sweep preview
  // read in its own hue (subject_hex falls back to the category hue when 0).
  const BinderNode* sn = m_model.find_node_by_iid(iid);
  m_armed_color_idx = sn ? sn->color_idx : 0;
  for (auto& [riid, w] : m_rail_rows) {
    if (!w) continue;
    if (riid == iid) w->add_css_class("armed");
    else             w->remove_css_class("armed");
  }
  sync_content_size();  // staging row reserves space
  m_area.queue_draw();
}

// s85 — arm a THREAD (the rail's Story Threads section). Same shape as
// arm_subject: re-clicking the armed thread disarms; otherwise this becomes the
// staging-row target and a sweep SETS BinderNode.thread across the span. The hue
// comes from the thread's palette color_idx (not a §9.6 category).
void TimelineSurface::arm_thread(const std::string& iid, const std::string& label,
                                 int color_idx) {
  if (m_armed_iid == iid && m_armed_kind == ArmedKind::Thread) { disarm(); return; }
  m_armed_kind = ArmedKind::Thread;
  m_armed_iid = iid;
  m_armed_label = label;
  m_armed_color_idx = color_idx;
  for (auto& [riid, w] : m_rail_rows) {
    if (!w) continue;
    if (riid == iid) w->add_css_class("armed");
    else             w->remove_css_class("armed");
  }
  sync_content_size();
  m_area.queue_draw();
}

// s86 — arm a KEY POINT from the rail. The analog of arm_thread: re-clicking the
// armed KP disarms; otherwise this becomes the staging-row target and a sweep
// stamps kp_id + color_idx + kp_label + is_key_point across the span. The hue is
// the swatch colour (kp_hex via color_idx).
void TimelineSurface::arm_keypoint(const std::string& kp_id,
                                   const std::string& label, int color_idx) {
  if (m_armed_iid == kp_id && m_armed_kind == ArmedKind::KeyPoint) { disarm(); return; }
  m_armed_kind = ArmedKind::KeyPoint;
  m_armed_iid = kp_id;
  m_armed_label = label;
  m_armed_color_idx = color_idx;
  for (auto& [riid, w] : m_rail_rows) {
    if (!w) continue;
    if (riid == kp_id) w->add_css_class("armed");
    else               w->remove_css_class("armed");
  }
  sync_content_size();
  m_area.queue_draw();
}

void TimelineSurface::disarm() {
  m_armed_iid.clear();
  m_armed_label.clear();
  m_armed_kind = ArmedKind::Subject;
  m_armed_color_idx = 0;
  for (auto& [riid, w] : m_rail_rows) {
    (void)riid;
    if (w) w->remove_css_class("armed");
  }
  sync_content_size();
  m_area.queue_draw();
}

std::string TimelineSurface::scene_at(double x, double y) const {
  if (m_story_axis) return scene_at_story(x, y);   // s93 — chronological layout
  const int top = spine_top();
  if (y < top || y > top + CARD_H) return {};
  for (const auto& s : m_proj.spine) {
    const int cx = col_cx(s.position - 1);
    if (x >= cx - card_w() / 2.0 && x <= cx + card_w() / 2.0) return s.iid;
  }
  return {};
}

void TimelineSurface::draw(const Cairo::RefPtr<Cairo::Context>& cr, int /*w*/, int /*h*/) {
  if (m_proj.spine.empty()) return;

  // s91 — the ACTUAL zoom: one uniform scale over the whole surface. Everything
  // below draws in BASE coordinates (COL, CARD_*, lane heights, paddings, fonts);
  // this single transform scales it all together, proportionally. Event coords
  // are divided by m_zoom back into base space for hit-testing (the controllers).
  cr->scale(m_zoom, m_zoom);

  // s93 — world-clock: the chronological view is a separate, self-contained draw
  // (the told-order apparatus below stays untouched). Zoom transform applies to
  // both; story view draws in the same base coords.
  if (m_story_axis) { draw_story_axis(cr); return; }

  // s92 — is persistent focus pinned? When true it OVERRIDES the transient
  // hover-isolate (the pin wins): focused rows stay full, every other row dims,
  // and the columns the focused set doesn't touch recede (the spine-walk scrim,
  // drawn last). focus_contains(.., fk_*) per row decides; m_focus_positions is
  // the precomputed "which columns are in focus" the scrim reads.
  const bool focus_on = focus_active(m_focus);

  // s80 step 4 — scene-column light (§9.6): a faint full-height highlight under
  // the hovered card's column, so the subjects present in that scene read down
  // the bars/dots that cross it. Drawn first so everything sits on top.
  if (m_hover_col >= 1) {
    Gdk::RGBA hl = themed(m_area, "accent", "#89b4fa");
    set_src(cr, hl, 0.10);
    cr->rectangle(col_left(m_hover_col - 1), TOP_PAD - 4,
                  COL, content_height() - (TOP_PAD - 4) - BOT_PAD + 4);
    cr->fill();
  }

  const Gdk::RGBA c_band_brd = themed(m_area, "tx4", "#5a5d75");
  const Gdk::RGBA c_band_tx  = themed(m_area, "tx2", "#b8bfdd");
  const Gdk::RGBA c_axis     = themed(m_area, "tx4", "#5a5d75");
  const Gdk::RGBA c_badge    = themed(m_area, "adw_overlay2", "#3a3a54");
  const Gdk::RGBA c_badge_tx = themed(m_area, "tx1", "#cdd6f4");
  const Gdk::RGBA c_gutter   = themed(m_area, "tx3", "#9196b4");

  const int n = static_cast<int>(m_proj.spine.size());
  const int top = spine_top();
  const double axis_y = top + CARD_H / 2.0;

  // ── Gutter row labels (anchor for the step-3 track names) ──────────────────
  if (m_proj.band_rows > 0) {
    auto gl = m_area.create_pango_layout("STRUCTURE");
    gl->set_font_description(Pango::FontDescription("sans bold 9"));
    int tw = 0, th = 0; gl->get_pixel_size(tw, th);
    set_src(cr, c_gutter, 0.9);
    cr->move_to(LEFT_PAD, TOP_PAD + (BAND_H - th) / 2.0);
    gl->show_in_cairo_context(cr);
  }
  {
    auto gl = m_area.create_pango_layout("SPINE");
    gl->set_font_description(Pango::FontDescription("sans bold 9"));
    int tw = 0, th = 0; gl->get_pixel_size(tw, th);
    set_src(cr, c_gutter, 0.9);
    cr->move_to(LEFT_PAD, axis_y - th / 2.0);
    gl->show_in_cairo_context(cr);
  }
  draw_lens_toggle(cr);   // s93 — Told Order / Chrono pills under the SPINE label

  // ── Structure bands ────────────────────────────────────────────────────────
  for (const auto& b : m_proj.bands) {
    const Gdk::RGBA fill = themed(m_area, band_token(b.depth), "#3a3a54");
    const double by = TOP_PAD + b.depth * (BAND_H + BAND_GAP);
    const double bx = col_left(b.start_pos - 1) + 3;
    const double bw = static_cast<double>(b.span()) * COL - 6;
    rounded_rect(cr, bx, by, bw, BAND_H, 6);
    set_src(cr, fill, 0.95);
    cr->fill_preserve();
    set_src(cr, c_band_brd, 0.5);
    cr->set_line_width(1.0);
    cr->stroke();

    auto lab = m_area.create_pango_layout(b.label.empty() ? "(untitled)" : b.label);
    lab->set_font_description(Pango::FontDescription("sans 10"));
    lab->set_ellipsize(Pango::EllipsizeMode::END);
    lab->set_width(static_cast<int>((bw - 12) * Pango::SCALE));
    lab->set_alignment(Pango::Alignment::CENTER);
    int tw = 0, th = 0; lab->get_pixel_size(tw, th);
    set_src(cr, c_band_tx);
    cr->move_to(bx + 6, by + (BAND_H - th) / 2.0);
    lab->show_in_cairo_context(cr);
  }

  // ── Spine axis ─────────────────────────────────────────────────────────────
  set_src(cr, c_axis, 0.8);
  cr->set_line_width(2.0);
  cr->move_to(col_cx(0), axis_y);
  cr->line_to(col_cx(n - 1), axis_y);
  cr->stroke();

  // ── Scene cards (shared painter — told order: left badge == right == position) ──
  for (const auto& s : m_proj.spine) {
    const int k = s.position - 1;
    const double cx = col_cx(k);
    const double cardx = cx - card_w() / 2.0;

    set_src(cr, c_axis);   // axis dot under the card
    cr->arc(cx, axis_y, 2.5, 0, 2 * M_PI);
    cr->fill();

    draw_scene_card(cr, s.iid, s.title, s.position, s.position, cardx, top);
  }

  // ── Story-time gap chips (s93, world-clock §9.14.2) ─────────────────────────
  // Between two consecutive DATED told-order scenes, a compact chip on the axis
  // names the elapsed gap (abbreviated — the peek panel carries the worded
  // phrase). Forward = the neutral badge chip; backward (a flashback boundary) =
  // amber. Undated on either side, or a zero gap, draws nothing. Per-frame model
  // lookup mirrors the peek path; a rebuild-time cache is the optimisation if a
  // large manuscript ever shows it.
  {
    Gdk::RGBA c_gap_back;     c_gap_back.set("#e8a13a");      // flashback amber
    Gdk::RGBA c_gap_back_tx;  c_gap_back_tx.set("#241a08");   // dark text on amber
    for (int k = 0; k + 1 < n; ++k) {
      const BinderNode* a =
          m_model.find_node_by_iid(m_spine_iids[static_cast<std::size_t>(k)]);
      const BinderNode* b =
          m_model.find_node_by_iid(m_spine_iids[static_cast<std::size_t>(k + 1)]);
      if (!a || !b || !a->has_story_time || !b->has_story_time) continue;
      const long long delta = b->story_time - a->story_time;
      if (delta == 0) continue;
      const bool back = delta < 0;

      auto gl = m_area.create_pango_layout(duration_abbrev(delta));
      gl->set_font_description(Pango::FontDescription("sans bold 7"));
      int gw = 0, gh = 0; gl->get_pixel_size(gw, gh);
      const double mx = (col_cx(k) + col_cx(k + 1)) / 2.0;
      const double cw = gw + 7.0;
      const double ch = gh + 3.0;
      rounded_rect(cr, mx - cw / 2.0, axis_y - ch / 2.0, cw, ch, ch / 2.0);
      set_src(cr, back ? c_gap_back : c_badge, 0.95);
      cr->fill();
      set_src(cr, back ? c_gap_back_tx : c_badge_tx);
      cr->move_to(mx - gw / 2.0, axis_y - gh / 2.0);
      gl->show_in_cairo_context(cr);
    }
  }

  // ── KP strip (s81 step 6) ──────────────────────────────────────────────────
  // A SINGLE row up against the spine: the relief of kp_id (§9.4). KPs partition
  // the spine, so every lane tiles this one row without collision; a recurring
  // beat reads as bar + detached diamond in its own colour. Connections are
  // PERSISTENT (§9.5): a short tick pins each claimed scene down to its beat.
  // Singletons draw as DIAMONDS (vs. the subject Dot); colour is per-KP spectrum
  // with the orange fallback (kp_hex).
  draw_kp_strip(cr, m_spine_iids, kp_top(), true, true);   // s93 — shared KP relief, told order

  // ── Staging row (s82) ──────────────────────────────────────────────────────
  // When a rail subject is armed, a single dashed lane in its hue invites a
  // sweep: drag across the scene columns to assert the subject's presence (§3).
  // The subject's CURRENT relief draws faintly so a sweep reads as "add to this".
  if (staging_active()) {
    const int sy = staging_top();
    const double scy = sy + STAGE_H / 2.0;
    Gdk::RGBA hue; hue.set(armed_hue());

    const double fx = col_left(0);
    const double fw = static_cast<double>(n) * COL;
    rounded_rect(cr, fx, sy, fw, STAGE_H, 7);
    set_src(cr, hue, 0.08);
    cr->fill_preserve();
    std::vector<double> sd{5.0, 4.0};
    cr->set_dash(sd, 0.0);
    set_src(cr, hue, 0.70);
    cr->set_line_width(1.5);
    cr->stroke();
    cr->unset_dash();

    // gutter: swatch + armed label (mirrors the track-name gutter)
    rounded_rect(cr, LEFT_PAD, scy - 5, 10, 10, 2);
    set_src(cr, hue, 1.0);
    cr->fill();
    auto gl = m_area.create_pango_layout(
        m_armed_label.empty() ? "(armed)" : m_armed_label);
    gl->set_font_description(Pango::FontDescription("sans bold 10"));
    gl->set_ellipsize(Pango::EllipsizeMode::END);
    gl->set_width(static_cast<int>((GUTTER - 26) * Pango::SCALE));
    int glw = 0, glh = 0; gl->get_pixel_size(glw, glh);
    set_src(cr, themed(m_area, "tx1", "#cdd6f4"), 1.0);
    cr->move_to(LEFT_PAD + 16, scy - glh / 2.0);
    gl->show_in_cairo_context(cr);

    // faint existing relief of the armed subject (so the sweep extends it)
    if (const auto* cl = armed_claimed()) {
      const Relief rel = compute_relief(m_spine_iids, *cl);
      for (const auto& seg : rel.segments) {
        if (seg.kind == ReliefSegment::Kind::Bar) {
          const double bx = col_cx(seg.start_pos - 1) - card_w() / 2.0;           // s87 edge-to-edge
          const double bw = (col_cx(seg.end_pos - 1) + card_w() / 2.0) - bx;
          rounded_rect(cr, bx, scy - BAR_H / 2.0, bw, BAR_H, BAR_H / 2.0);
          set_src(cr, hue, 0.45);
          cr->fill();
        } else {
          const double sx = col_cx(seg.start_pos - 1) - card_w() / 2.0;
          rounded_rect(cr, sx, scy - BAR_H / 2.0, card_w(), BAR_H, BAR_H / 2.0);
          set_src(cr, hue, 0.50);
          cr->fill();
        }
      }
    } else {
      // no current presence — a centred hint
      auto hl = m_area.create_pango_layout("drag across scenes to place");
      hl->set_font_description(Pango::FontDescription("sans italic 9"));
      int hlw = 0, hlh = 0; hl->get_pixel_size(hlw, hlh);
      set_src(cr, hue, 0.55);
      cr->move_to(fx + (fw - hlw) / 2.0, scy - hlh / 2.0);
      hl->show_in_cairo_context(cr);
    }
  }

  // ── Relief tracks (s80 step 3) ─────────────────────────────────────────────
  // One row per subject: its compute_relief drawn as bars (runs), dots
  // (singletons), and faint dashed connectors over interior gaps. Hue = category.
  // The vertical card→track drop-lines (s82) now live inside draw_subject_tracks so
  // both lenses draw them off their own order (s93).
  const int ttop = track_top();

  draw_subject_tracks(cr, m_spine_iids, ttop, true);   // s93 — shared relief, told order

  // ── Thread band (s84 step 7) ───────────────────────────────────────────────
  // The "assigned arc": one row per authored thread, the relief of
  // BinderNode.thread. DISPLAY-ONLY (the KP-strip nature — not swept/hit-tested);
  // assignment is the Inspector's Thread control. Drawn below the subject tracks
  // with its own header so "revealed" (subject tracks) vs "assigned" (threads)
  // reads at a glance. Hue = the thread's palette colour (lavender fallback).
  draw_thread_band(cr, m_spine_iids, thread_top(), thread_rows_top(), true);   // s93 — shared thread relief, told order

  // s80 step 5c — live sweep preview: a ghost span on the armed track row across
  // the swept columns, in the subject's hue (drawn on top of the tracks).
  if (m_sweep_track >= 0 && m_sweep_track < static_cast<int>(m_tracks.size())) {
    const SweepRange sr =
        sweep_range(&m_tracks[static_cast<std::size_t>(m_sweep_track)].claimed);
    const bool unlink = sr.remove;
    const int lo = sr.lo;
    const int hi = sr.hi;
    if (sr.valid) {
      Gdk::RGBA hue; hue.set(subject_hex(m_tracks[static_cast<std::size_t>(m_sweep_track)].color_idx, m_tracks[static_cast<std::size_t>(m_sweep_track)].category));
      const double cy = ttop + static_cast<double>(m_sweep_track) * (TRACK_H + TRACK_GAP)
                        + TRACK_H / 2.0;
      const double px = col_cx(lo - 1) - card_w() / 2.0;            // s87 edge-to-edge
      const double pw = (col_cx(hi - 1) + card_w() / 2.0) - px;
      const double ph = BAR_H + 6.0;
      rounded_rect(cr, px, cy - ph / 2.0, pw, ph, ph / 2.0);
      if (unlink) {
        const Gdk::RGBA del = themed(m_area, "error", "#f38ba8");
        set_src(cr, del, 0.14);
        cr->fill_preserve();
        std::vector<double> pd{4.0, 3.0};
        cr->set_dash(pd, 0.0);
        set_src(cr, del, 0.95);
        cr->set_line_width(1.5);
        cr->stroke();
        cr->unset_dash();
      } else {
        set_src(cr, hue, 0.28);
        cr->fill_preserve();
        std::vector<double> pd{4.0, 3.0};
        cr->set_dash(pd, 0.0);
        set_src(cr, hue, 0.9);
        cr->set_line_width(1.5);
        cr->stroke();
        cr->unset_dash();
      }
    }
  }

  // s82 — live sweep preview on the STAGING row (armed rail subject). Same ghost
  // span, in the armed subject's hue, on the staging lane.
  if (m_sweep_is_armed && staging_active()) {
    const SweepRange sr = sweep_range(armed_claimed());
    const bool unlink = sr.remove;
    const int lo = sr.lo;
    const int hi = sr.hi;
    if (sr.valid) {
      Gdk::RGBA hue; hue.set(armed_hue());
      const double cy = staging_top() + STAGE_H / 2.0;
      const double px = col_cx(lo - 1) - card_w() / 2.0;            // s87 edge-to-edge
      const double pw = (col_cx(hi - 1) + card_w() / 2.0) - px;
      const double ph = BAR_H + 6.0;
      rounded_rect(cr, px, cy - ph / 2.0, pw, ph, ph / 2.0);
      if (unlink) {
        const Gdk::RGBA del = themed(m_area, "error", "#f38ba8");
        set_src(cr, del, 0.16);
        cr->fill_preserve();
        std::vector<double> pd{4.0, 3.0};
        cr->set_dash(pd, 0.0);
        set_src(cr, del, 0.95);
        cr->set_line_width(1.5);
        cr->stroke();
        cr->unset_dash();
      } else {
        set_src(cr, hue, 0.32);
        cr->fill_preserve();
        std::vector<double> pd{4.0, 3.0};
        cr->set_dash(pd, 0.0);
        set_src(cr, hue, 0.95);
        cr->set_line_width(1.5);
        cr->stroke();
        cr->unset_dash();
      }
    }
  }

  // s86 — live preview for a direct THREAD band sweep: a ghost span on the band
  // row under the cursor, in the thread's hue (same ghost idiom as staging).
  if (m_sweep_band_thread >= 0
      && m_sweep_band_thread < static_cast<int>(m_thread_lanes.size())) {
    const ThreadLane& ln = m_thread_lanes[static_cast<std::size_t>(m_sweep_band_thread)];
    const SweepRange sr = sweep_range(&ln.claimed);
    if (sr.valid) {
      Gdk::RGBA hue; hue.set(thread_hex(ln.color_idx));
      const double cy = thread_rows_top()
                      + static_cast<double>(m_sweep_band_thread) * (TRACK_H + TRACK_GAP)
                      + TRACK_H / 2.0;
      const double px = col_cx(sr.lo - 1) - card_w() / 2.0;         // s87 edge-to-edge
      const double pw = (col_cx(sr.hi - 1) + card_w() / 2.0) - px;
      const double ph = BAR_H + 6.0;
      rounded_rect(cr, px, cy - ph / 2.0, pw, ph, ph / 2.0);
      if (sr.remove) {
        const Gdk::RGBA del = themed(m_area, "error", "#f38ba8");
        set_src(cr, del, 0.16); cr->fill_preserve();
        std::vector<double> pd{4.0, 3.0}; cr->set_dash(pd, 0.0);
        set_src(cr, del, 0.95); cr->set_line_width(1.5); cr->stroke(); cr->unset_dash();
      } else {
        set_src(cr, hue, 0.32); cr->fill_preserve();
        std::vector<double> pd{4.0, 3.0}; cr->set_dash(pd, 0.0);
        set_src(cr, hue, 0.95); cr->set_line_width(1.5); cr->stroke(); cr->unset_dash();
      }
    }
  }

  // s86 — live preview for a direct KP strip sweep: a ghost span on the strip
  // row, in the KP's hue.
  if (m_sweep_band_kp >= 0
      && m_sweep_band_kp < static_cast<int>(m_kp_lanes.size())) {
    const KpLane& ln = m_kp_lanes[static_cast<std::size_t>(m_sweep_band_kp)];
    const SweepRange sr = sweep_range(&ln.claimed);
    if (sr.valid) {
      Gdk::RGBA hue; hue.set(kp_hex(ln.color_idx));
      const double cy = kp_top() + KP_H / 2.0;
      const double px = col_cx(sr.lo - 1) - card_w() / 2.0;         // s87 edge-to-edge
      const double pw = (col_cx(sr.hi - 1) + card_w() / 2.0) - px;
      const double ph = BAR_H + 6.0;
      rounded_rect(cr, px, cy - ph / 2.0, pw, ph, ph / 2.0);
      if (sr.remove) {
        const Gdk::RGBA del = themed(m_area, "error", "#f38ba8");
        set_src(cr, del, 0.16); cr->fill_preserve();
        std::vector<double> pd{4.0, 3.0}; cr->set_dash(pd, 0.0);
        set_src(cr, del, 0.95); cr->set_line_width(1.5); cr->stroke(); cr->unset_dash();
      } else {
        set_src(cr, hue, 0.32); cr->fill_preserve();
        std::vector<double> pd{4.0, 3.0}; cr->set_dash(pd, 0.0);
        set_src(cr, hue, 0.95); cr->set_line_width(1.5); cr->stroke(); cr->unset_dash();
      }
    }
  }

  // s92 — the spine-walk scrim (drawn LAST, on top, so it RECEDES content). When
  // a focus is pinned, every told-order column the focused set does NOT touch is
  // dimmed by a translucent window-bg veil: the spine itself then lights up where
  // the focused thread is PRESENT and goes dark in its gaps — the §4 "walk the
  // spine, see the gap, draw the link" affordance, made visible on the cards too
  // (not only in the focused row's own dashed gaps). A focused column (in
  // m_focus_positions) is left untouched. Gentle by design and one constant to
  // tune or drop (kFocusScrimA) if it reads heavy in real use.
  if (focus_on && !m_focus_positions.empty()) {
    constexpr double kFocusScrimA = 0.45;
    const Gdk::RGBA veil = themed(m_area, "adw_window_bg", "#1e1e2e");
    const double y_scrim = TOP_PAD - 4;
    const double h_scrim = content_height() - (TOP_PAD - 4) - BOT_PAD + 4;
    set_src(cr, veil, kFocusScrimA);
    for (const auto& s : m_proj.spine)
      if (m_focus_positions.find(s.position) == m_focus_positions.end()) {
        cr->rectangle(col_left(s.position - 1), y_scrim, COL, h_scrim);
        cr->fill();
      }
  }
}

}  // namespace Folio
