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
#include "KpPalette.hpp"    // s86 — palette_remap / apply_palette_remap (delete reconcile)
#include "StoryGraph.hpp"   // s80 step 3 — edges_from_backlinks (subject adapter)
#include "TimelineRelief.hpp"  // compute_relief — the per-track renderer input

namespace Folio {

namespace {

// ── Locked geometry (s80 mocks) ──────────────────────────────────────────────
constexpr int LEFT_PAD      = 16;   // breathing room before the gutter
constexpr int GUTTER        = 132;  // row-label column (band labels + track names)
constexpr int COL           = 72;   // COL_W — px per scene column (zoom unit)
constexpr int CARD_W        = 56;
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
inline int col_left(int k) { return x0() + k * COL; }    // 0-based column k
inline int col_cx(int k)   { return col_left(k) + COL / 2; }

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

  // s84 — the right side of the split is a vertical box: the canvas (vexpand)
  // above, the scene peek panel (a revealer) below. build_peek_panel() fills the
  // revealer's child once; select_scene reveals + populates it on a single click.
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
  // used to. s82 — with Alt or Ctrl held, a click instead TOGGLES one scene's
  // association on the row under the cursor (precise non-contiguous edit; the
  // sweep remains the span gesture). Either modifier works so a WM that grabs
  // Alt+click (some GNOME setups) still leaves Ctrl+click free.
  auto click = Gtk::GestureClick::create();
  click->set_button(GDK_BUTTON_PRIMARY);
  Gtk::GestureClick* clickp = click.get();   // raw: m_area owns the controller
  click->signal_released().connect([this, clickp](int n_press, double x, double y) {
    const Gdk::ModifierType st = clickp->get_current_event_state();
    const bool mod = (st & Gdk::ModifierType::ALT_MASK)     != Gdk::ModifierType{} ||
                     (st & Gdk::ModifierType::CONTROL_MASK) != Gdk::ModifierType{};
    if (mod) { toggle_cell(x, y); return; }
    const std::string iid = scene_at(x, y);
    if (iid.empty()) return;
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
    const int tlane = thread_lane_at(y);
    if (tlane >= 0 && tlane < static_cast<int>(m_thread_lanes.size())) {
      secp->set_state(Gtk::EventSequenceState::CLAIMED);
      double rx = 0.0, ry = 0.0;
      m_area.translate_coordinates(*this, x, y, rx, ry);
      show_thread_menu(m_thread_lanes[static_cast<std::size_t>(tlane)].thread_key, rx, ry);
      return;
    }
    if (over_kp_strip(y)) {
      const int klane = kp_lane_at_col(column_at(x));
      if (klane >= 0 && klane < static_cast<int>(m_kp_lanes.size())) {
        secp->set_state(Gtk::EventSequenceState::CLAIMED);
        double rx = 0.0, ry = 0.0;
        m_area.translate_coordinates(*this, x, y, rx, ry);
        show_kp_menu(m_kp_lanes[static_cast<std::size_t>(klane)].kp_id, rx, ry);
        return;
      }
    }
    const int trk = track_row_at(y);
    if (trk < 0) return;
    secp->set_state(Gtk::EventSequenceState::CLAIMED);
    show_track_menu(trk, x, y);
  });
  m_area.add_controller(sec);

  // s80 step 4 — hover focus. Motion sets the isolated track / lit column;
  // leaving clears both. Presentation only; a redraw reflects the new focus.
  auto motion = Gtk::EventControllerMotion::create();
  motion->signal_motion().connect([this](double x, double y) {
    int trk = -1, col = -1;
    const int st = spine_top();
    if (y >= st && y <= st + CARD_H && !scene_at(x, y).empty()) {
      col = column_at(x);            // over a scene card → light its column
    } else {
      trk = track_row_at(y);         // over a track row → isolate it
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

  // s80 step 5c / s82 — the subject-first sweep. A primary drag that BEGINS on a
  // track row (or the armed staging row) sweeps a span of scene columns; on
  // release the verb is resolved by sweep_range (drag onto empty → add the span;
  // drag onto a linked cell → remove the cells swept). A bare press (no movement)
  // adds the single anchor cell only once a real drag is detected.
  auto drag = Gtk::GestureDrag::create();
  drag->set_button(GDK_BUTTON_PRIMARY);
  drag->signal_drag_begin().connect([this](double x, double y) {
    // s82 — a sweep that begins on the STAGING row arms the rail subject across
    // the swept span (the §3 build gesture); otherwise a sweep on an existing
    // track row edits that subject (s80). Staging takes priority when armed.
    if (staging_active() && over_staging(y)) {
      m_sweep_is_armed = true;
      m_sweep_track = -1;
      m_sweep_band_thread = -1;
      m_sweep_band_kp = -1;
      m_sweep_start_x = x;
      m_sweep_from_col = clamped_col(x);
      m_sweep_to_col = m_sweep_from_col;
      m_sweep_moved = false;
      m_area.queue_draw();
      return;
    }
    m_sweep_is_armed = false;
    // s86 — direct sweep on the thread band (row-keyed) or the KP strip
    // (column-keyed), parity with sweeping a subject track row. Checked before
    // the subject-track check; they live in disjoint y-bands so there is no clash.
    const int tlane = thread_lane_at(y);
    if (tlane >= 0) {
      m_sweep_band_thread = tlane;
      m_sweep_band_kp = -1;
      m_sweep_track = -1;
      m_sweep_start_x = x;
      m_sweep_from_col = clamped_col(x);
      m_sweep_to_col = m_sweep_from_col;
      m_sweep_moved = false;
      m_area.queue_draw();
      return;
    }
    if (over_kp_strip(y)) {
      const int klane = kp_lane_at_col(clamped_col(x));
      if (klane >= 0) {   // press must land on an existing beat (no KP identity on an empty cell)
        m_sweep_band_kp = klane;
        m_sweep_band_thread = -1;
        m_sweep_track = -1;
        m_sweep_start_x = x;
        m_sweep_from_col = clamped_col(x);
        m_sweep_to_col = m_sweep_from_col;
        m_sweep_moved = false;
        m_area.queue_draw();
        return;
      }
    }
    m_sweep_band_thread = -1;
    m_sweep_band_kp = -1;
    const int trk = track_row_at(y);
    if (trk < 0) { m_sweep_track = -1; return; }   // only sweeps from a track row
    m_sweep_track = trk;
    m_sweep_start_x = x;
    m_sweep_from_col = clamped_col(x);
    m_sweep_to_col = m_sweep_from_col;
    m_sweep_moved = false;
    m_area.queue_draw();
  });
  drag->signal_drag_update().connect([this](double ox, double oy) {
    if (m_sweep_track < 0 && !m_sweep_is_armed
        && m_sweep_band_thread < 0 && m_sweep_band_kp < 0) return;
    if (std::abs(ox) > 2.0 || std::abs(oy) > 2.0) m_sweep_moved = true;
    m_sweep_to_col = clamped_col(m_sweep_start_x + ox);
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
  // subject is actually claimed there.
  const int col = column_at(x);     // 1-based told-order column, or 0
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
  m_area.set_content_width(std::max(content_width(), 1));
  m_area.set_content_height(std::max(content_height(), 1));
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
}


void TimelineSurface::build_rail(const std::vector<ResourceGroup>& groups) {
  // Tear down the previous rows (managed children destroyed on remove); the
  // member m_rail_empty is merely unparented and re-appended below.
  m_rail_rows.clear();
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

    // Category disclosure (binder-style): an Expander titled with the §9.6
    // heading, its body the subject rows. The author can collapse a category;
    // the choice persists across rebuilds (view-entry, commit_sweep) via
    // m_rail_collapsed, keyed by the TrackCategory enum value.
    const int cat_key = static_cast<int>(g.category);
    auto* exp = Gtk::make_managed<Gtk::Expander>();
    exp->set_name("timeline-rail-group");
    exp->add_css_class("timeline-rail-group");

    auto* hdr = Gtk::make_managed<Gtk::Label>(category_heading(g.category));
    hdr->set_xalign(0.0f);
    hdr->add_css_class("timeline-rail-header");
    hdr->set_name("timeline-rail-header");
    exp->set_label_widget(*hdr);
    exp->set_expanded(m_rail_collapsed.find(cat_key) == m_rail_collapsed.end());
    exp->property_expanded().signal_changed().connect([this, cat_key, exp]() {
      if (exp->get_expanded()) m_rail_collapsed.erase(cat_key);
      else                     m_rail_collapsed.insert(cat_key);
    });

    auto* body = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    body->add_css_class("timeline-rail-group-body");
    exp->set_child(*body);

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

    m_rail_box.append(*exp);
  }

  // s85 — the Story Threads section (the §9.12.6 #4 batch-assign entry), below the
  // subject groups. Registry-sourced, with an inline mint row; always rendered.
  build_thread_rail_section();
  build_kp_rail_section();
}

// s85 — the rail's "Story Threads" disclosure: one row per registered thread
// (DocumentModel::threads()) plus an inline "new thread" mint row at the bottom.
// Mirrors a subject group exactly (Expander + .timeline-rail-row buttons, the
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

  auto* exp = Gtk::make_managed<Gtk::Expander>();
  exp->set_name("timeline-rail-group");
  exp->add_css_class("timeline-rail-group");
  auto* hdr = Gtk::make_managed<Gtk::Label>("Story Threads");
  hdr->set_xalign(0.0f);
  hdr->add_css_class("timeline-rail-header");
  hdr->set_name("timeline-rail-header");
  exp->set_label_widget(*hdr);
  exp->set_expanded(m_rail_collapsed.find(THREAD_CAT_KEY) == m_rail_collapsed.end());
  exp->property_expanded().signal_changed().connect([this, exp, THREAD_CAT_KEY]() {
    if (exp->get_expanded()) m_rail_collapsed.erase(THREAD_CAT_KEY);
    else                     m_rail_collapsed.insert(THREAD_CAT_KEY);
  });

  auto* body = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  body->add_css_class("timeline-rail-group-body");
  exp->set_child(*body);

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

  m_rail_box.append(*exp);
}

// s86 — the rail's Key Points section: mint / place / manage a KP in one place.
// A KP is a palette swatch ("the palette is the arc"), so the roster is
// m_prefs.tag_colors; each row arms a KP (arm_keypoint) and a sweep stamps the
// beat. Mirrors build_thread_rail_section (Expander + .timeline-rail-row buttons,
// collapse-memory on a distinct sentinel key, the help link + empty nudge + an
// inline mint row + a per-row right-click manage popover).
void TimelineSurface::build_kp_rail_section() {
  const auto& palette = m_prefs.tag_colors;

  // A sentinel collapse key distinct from the TrackCategory values (0..3) and
  // the threads section (-1); -2 is the Key Points disclosure's own state.
  constexpr int KP_CAT_KEY = -2;

  auto* exp = Gtk::make_managed<Gtk::Expander>();
  exp->set_name("timeline-rail-group");
  exp->add_css_class("timeline-rail-group");
  auto* hdr = Gtk::make_managed<Gtk::Label>("Key Points");
  hdr->set_xalign(0.0f);
  hdr->add_css_class("timeline-rail-header");
  hdr->set_name("timeline-rail-header");
  exp->set_label_widget(*hdr);
  exp->set_expanded(m_rail_collapsed.find(KP_CAT_KEY) == m_rail_collapsed.end());
  exp->property_expanded().signal_changed().connect([this, exp, KP_CAT_KEY]() {
    if (exp->get_expanded()) m_rail_collapsed.erase(KP_CAT_KEY);
    else                     m_rail_collapsed.insert(KP_CAT_KEY);
  });

  auto* body = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  body->add_css_class("timeline-rail-group-body");
  exp->set_child(*body);

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

  m_rail_box.append(*exp);
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
  m_area.set_content_height(std::max(content_height(), 1));  // staging row reserves space
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
  m_area.set_content_height(std::max(content_height(), 1));
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
  m_area.set_content_height(std::max(content_height(), 1));
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
  m_area.set_content_height(std::max(content_height(), 1));
  m_area.queue_draw();
}

std::string TimelineSurface::scene_at(double x, double y) const {
  const int top = spine_top();
  if (y < top || y > top + CARD_H) return {};
  for (const auto& s : m_proj.spine) {
    const int cx = col_cx(s.position - 1);
    if (x >= cx - CARD_W / 2.0 && x <= cx + CARD_W / 2.0) return s.iid;
  }
  return {};
}

void TimelineSurface::draw(const Cairo::RefPtr<Cairo::Context>& cr, int /*w*/, int /*h*/) {
  if (m_proj.spine.empty()) return;

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

  const Gdk::RGBA c_border = themed(m_area, "border_subtle", "rgba(255,255,255,0.06)");
  const Gdk::RGBA c_band_brd = themed(m_area, "tx4", "#5a5d75");
  const Gdk::RGBA c_band_tx  = themed(m_area, "tx2", "#b8bfdd");
  const Gdk::RGBA c_card     = themed(m_area, "adw_surface", "#242436");
  const Gdk::RGBA c_card_tx  = themed(m_area, "tx3", "#9196b4");
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

  // ── Scene cards ────────────────────────────────────────────────────────────
  for (const auto& s : m_proj.spine) {
    const int k = s.position - 1;
    const double cx = col_cx(k);
    const double cardx = cx - CARD_W / 2.0;

    // axis dot under the card
    set_src(cr, c_axis);
    cr->arc(cx, axis_y, 2.5, 0, 2 * M_PI);
    cr->fill();

    rounded_rect(cr, cardx, top, CARD_W, CARD_H, 7);
    set_src(cr, c_card, 0.98);
    cr->fill_preserve();
    set_src(cr, c_border);
    cr->set_line_width(1.0);
    cr->stroke();

    // s84 — the peeked scene reads as selected: an accent ring around the card.
    if (!m_selected_iid.empty() && s.iid == m_selected_iid) {
      Gdk::RGBA sel = themed(m_area, "accent", "#89b4fa");
      rounded_rect(cr, cardx - 2.5, top - 2.5, CARD_W + 5, CARD_H + 5, 9);
      set_src(cr, sel, 0.95);
      cr->set_line_width(2.0);
      cr->stroke();
    }

    // position badge
    const double badge_cx = cardx + BADGE_R + 3;
    const double badge_cy = top + BADGE_R + 3;
    set_src(cr, c_badge);
    cr->arc(badge_cx, badge_cy, BADGE_R, 0, 2 * M_PI);
    cr->fill();
    auto pn = m_area.create_pango_layout(std::to_string(s.position));
    pn->set_font_description(Pango::FontDescription("monospace 8"));
    int pw = 0, ph = 0; pn->get_pixel_size(pw, ph);
    set_src(cr, c_badge_tx);
    cr->move_to(badge_cx - pw / 2.0, badge_cy - ph / 2.0);
    pn->show_in_cairo_context(cr);

    // title (clipped to card width)
    auto tl = m_area.create_pango_layout(s.title.empty() ? "(untitled)" : s.title);
    tl->set_font_description(Pango::FontDescription("sans 8"));
    tl->set_ellipsize(Pango::EllipsizeMode::END);
    tl->set_width(static_cast<int>((CARD_W - 8) * Pango::SCALE));
    tl->set_alignment(Pango::Alignment::CENTER);
    int tw = 0, th = 0; tl->get_pixel_size(tw, th);
    set_src(cr, c_card_tx);
    cr->move_to(cardx + 4, top + CARD_H - th - 6);
    tl->show_in_cairo_context(cr);
  }

  // ── KP strip (s81 step 6) ──────────────────────────────────────────────────
  // A SINGLE row up against the spine: the relief of kp_id (§9.4). KPs partition
  // the spine, so every lane tiles this one row without collision; a recurring
  // beat reads as bar + detached diamond in its own colour. Connections are
  // PERSISTENT (§9.5): a short tick pins each claimed scene down to its beat.
  // Singletons draw as DIAMONDS (vs. the subject Dot); colour is per-KP spectrum
  // with the orange fallback (kp_hex).
  if (!m_kp_lanes.empty()) {
    const int kt = kp_top();
    const double kcy = kt + KP_H / 2.0;

    auto gl = m_area.create_pango_layout("KEY POINTS");
    gl->set_font_description(Pango::FontDescription("sans bold 9"));
    int gw = 0, gh = 0; gl->get_pixel_size(gw, gh);
    set_src(cr, c_gutter, 0.9);
    cr->move_to(LEFT_PAD, kcy - gh / 2.0);
    gl->show_in_cairo_context(cr);

    Gdk::RGBA kp_dark; kp_dark.set("#11111b");   // dark on-bar / on-diamond text

    for (const auto& lane : m_kp_lanes) {
      const Relief rel = compute_relief(m_spine_iids, lane.claimed);
      Gdk::RGBA hue; hue.set(kp_hex(lane.color_idx));

      // persistent connectors: a short pin from each claimed card down to the
      // strip, with a port dot at the card bottom (§9.5).
      set_src(cr, hue, 0.85);
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

      // beats: bars for runs, diamonds for singletons. A KP TARGET (a pinned
      // hinge, §s29) reads as a brighter "milestone" — a white outline halo and
      // a larger gem — vs an ordinary pattern beat. Tier is per-scene (pin), so
      // we look it up on the segment's claimed scenes via the model.
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
          const double bx = col_cx(seg.start_pos - 1) - CARD_W / 2.0;            // s87 edge-to-edge
          const double bw = (col_cx(seg.end_pos - 1) + CARD_W / 2.0) - bx;
          rounded_rect(cr, bx, kcy - KP_BAR_H / 2.0, bw, KP_BAR_H, KP_BAR_H / 2.0);
          set_src(cr, hue, 0.95);
          cr->fill_preserve();
          if (target) {                 // KP-target run → bright halo border
            set_src(cr, halo, 0.85);
            cr->set_line_width(1.5);
            cr->stroke();
          } else {
            cr->begin_new_path();
          }
          // label rides on the bar (centred, dark for contrast).
          if (!lane.label.empty()) {
            auto bl = m_area.create_pango_layout(lane.label);
            bl->set_font_description(Pango::FontDescription("sans 8"));
            bl->set_ellipsize(Pango::EllipsizeMode::END);
            bl->set_width(static_cast<int>((bw - 8) * Pango::SCALE));
            bl->set_alignment(Pango::Alignment::CENTER);
            int bw2 = 0, bh2 = 0; bl->get_pixel_size(bw2, bh2);
            set_src(cr, kp_dark, 0.92);
            cr->move_to(bx + 4, kcy - bh2 / 2.0);
            bl->show_in_cairo_context(cr);
          }
        } else {
          // singleton → diamond; a KP target draws as a larger haloed gem.
          const double dx = col_cx(seg.start_pos - 1);
          const double r  = target ? KP_DIAMOND_R + 2.0 : KP_DIAMOND_R;
          cr->begin_new_sub_path();
          cr->move_to(dx, kcy - r);
          cr->line_to(dx + r, kcy);
          cr->line_to(dx, kcy + r);
          cr->line_to(dx - r, kcy);
          cr->close_path();
          set_src(cr, hue, 0.95);
          cr->fill_preserve();
          set_src(cr, target ? halo : kp_dark, target ? 0.9 : 0.5);
          cr->set_line_width(target ? 1.5 : 0.75);
          cr->stroke();
          // caption above the diamond (subtle).
          if (!lane.label.empty()) {
            auto dl = m_area.create_pango_layout(lane.label);
            dl->set_font_description(Pango::FontDescription("sans 8"));
            int dw = 0, dh = 0; dl->get_pixel_size(dw, dh);
            set_src(cr, themed(m_area, "tx2", "#b8bfdd"), 0.95);
            cr->move_to(dx - dw / 2.0, kcy - r - dh - 1);
            dl->show_in_cairo_context(cr);
          }
        }
      }
    }
  }

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
          const double bx = col_cx(seg.start_pos - 1) - CARD_W / 2.0;           // s87 edge-to-edge
          const double bw = (col_cx(seg.end_pos - 1) + CARD_W / 2.0) - bx;
          rounded_rect(cr, bx, scy - BAR_H / 2.0, bw, BAR_H, BAR_H / 2.0);
          set_src(cr, hue, 0.45);
          cr->fill();
        } else {
          const double sx = col_cx(seg.start_pos - 1) - CARD_W / 2.0;
          rounded_rect(cr, sx, scy - BAR_H / 2.0, CARD_W, BAR_H, BAR_H / 2.0);
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
  const int ttop = track_top();

  // s82 (Scott review) — vertical connection drop-lines: a thin, hue-coded dashed
  // line from each scene card down to the cell of every track that claims it.
  // Staggered horizontally WITHIN a column so co-claimed scenes don't hide each
  // other (a lone claimant stays centred on its dot; collisions fan symmetrically).
  // Kept deliberately thin and faint so it reads as a guide, not chrome.
  if (!m_tracks.empty() && !m_proj.spine.empty()) {
    const double y_top = spine_top() + CARD_H;   // bottom edge of the scene cards
    constexpr double CONN_STEP = 4.0;            // per-claimant x stagger
    std::vector<double> cdash{2.0, 3.0};
    cr->set_line_width(1.4);
    cr->set_dash(cdash, 0.0);
    for (const auto& s : m_proj.spine) {
      // claimants of this column, in track order (top → bottom)
      std::vector<std::size_t> claimers;
      for (std::size_t t = 0; t < m_tracks.size(); ++t)
        if (m_tracks[t].claimed.count(s.iid)) claimers.push_back(t);
      const std::size_t m = claimers.size();
      for (std::size_t k = 0; k < m; ++k) {
        const std::size_t t = claimers[k];
        const bool dimc = (m_hover_track >= 0 && static_cast<int>(t) != m_hover_track);
        Gdk::RGBA chue; chue.set(subject_hex(m_tracks[t].color_idx, m_tracks[t].category));
        const double dx = (static_cast<double>(k) - (static_cast<double>(m) - 1.0) / 2.0)
                          * CONN_STEP;
        const double lx = col_cx(s.position - 1) + dx;
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
    const Relief rel = compute_relief(m_spine_iids, tk.claimed);
    Gdk::RGBA hue; hue.set(subject_hex(tk.color_idx, tk.category));
    const double row_y = ttop + static_cast<double>(i) * (TRACK_H + TRACK_GAP);
    const double cy = row_y + TRACK_H / 2.0;

    // s80 step 4 — isolate: when a track is hovered, siblings dim to ~0.16
    // (§9.6 focus→brightness). Structure (bands + spine) stays lit (§9.5).
    const bool dim = (m_hover_track >= 0 && static_cast<int>(i) != m_hover_track);
    const double a_bar = dim ? 0.16 : 0.90;
    const double a_dot = dim ? 0.16 : 0.95;
    const double a_gap = dim ? 0.08 : 0.22;
    const double a_lab = dim ? 0.40 : 1.00;

    // gutter: category swatch + subject label
    rounded_rect(cr, LEFT_PAD, cy - 5, 10, 10, 2);
    set_src(cr, hue, dim ? 0.30 : 1.0);
    cr->fill();
    auto gl = m_area.create_pango_layout(tk.label.empty() ? "(untitled)" : tk.label);
    gl->set_font_description(Pango::FontDescription("sans 10"));
    gl->set_ellipsize(Pango::EllipsizeMode::END);
    gl->set_width(static_cast<int>((GUTTER - 26) * Pango::SCALE));
    int lw = 0, lh = 0; gl->get_pixel_size(lw, lh);
    set_src(cr, themed(m_area, "tx2", "#b8bfdd"), a_lab);
    cr->move_to(LEFT_PAD + 16, cy - lh / 2.0);
    gl->show_in_cairo_context(cr);

    // faint dashed connectors over interior gaps ("where did they go", §9.2)
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

    // bars (contiguous runs) and dots (singletons)
    for (const auto& seg : rel.segments) {
      if (seg.kind == ReliefSegment::Kind::Bar) {
        const double bx = col_cx(seg.start_pos - 1) - CARD_W / 2.0;             // s87 edge-to-edge
        const double bw = (col_cx(seg.end_pos - 1) + CARD_W / 2.0) - bx;
        rounded_rect(cr, bx, cy - BAR_H / 2.0, bw, BAR_H, BAR_H / 2.0);
        set_src(cr, hue, a_bar);
        cr->fill();
      } else {
        // single-scene link: a pill the WIDTH OF THE SCENE (not a dot), so one
        // link reads as "this whole scene", consistent with a one-cell bar.
        const double sx = col_cx(seg.start_pos - 1) - CARD_W / 2.0;
        rounded_rect(cr, sx, cy - BAR_H / 2.0, CARD_W, BAR_H, BAR_H / 2.0);
        set_src(cr, hue, a_dot);
        cr->fill();
      }
    }
  }

  // ── Thread band (s84 step 7) ───────────────────────────────────────────────
  // The "assigned arc": one row per authored thread, the relief of
  // BinderNode.thread. DISPLAY-ONLY (the KP-strip nature — not swept/hit-tested);
  // assignment is the Inspector's Thread control. Drawn below the subject tracks
  // with its own header so "revealed" (subject tracks) vs "assigned" (threads)
  // reads at a glance. Hue = the thread's palette colour (lavender fallback).
  if (!m_thread_lanes.empty()) {
    const int hdr_y = thread_top();
    auto hl = m_area.create_pango_layout("STORY THREADS");
    hl->set_font_description(Pango::FontDescription("sans bold 9"));
    int hlw = 0, hlh = 0; hl->get_pixel_size(hlw, hlh);
    set_src(cr, c_gutter, 0.9);
    cr->move_to(LEFT_PAD, hdr_y + (THREAD_HEADER_H - hlh) / 2.0);
    hl->show_in_cairo_context(cr);

    const int rtop = thread_rows_top();
    for (std::size_t i = 0; i < m_thread_lanes.size(); ++i) {
      const ThreadLane& ln = m_thread_lanes[i];
      const Relief rel = compute_relief(m_spine_iids, ln.claimed);
      Gdk::RGBA hue; hue.set(thread_hex(ln.color_idx));
      const double row_y = rtop + static_cast<double>(i) * (TRACK_H + TRACK_GAP);
      const double cy = row_y + TRACK_H / 2.0;

      // gutter: thread swatch + label
      rounded_rect(cr, LEFT_PAD, cy - 5, 10, 10, 2);
      set_src(cr, hue, 1.0);
      cr->fill();
      auto gl = m_area.create_pango_layout(
          ln.label.empty() ? std::string("(thread)") : ln.label);
      gl->set_font_description(Pango::FontDescription("sans 10"));
      gl->set_ellipsize(Pango::EllipsizeMode::END);
      gl->set_width(static_cast<int>((GUTTER - 26) * Pango::SCALE));
      int lw = 0, lh = 0; gl->get_pixel_size(lw, lh);
      set_src(cr, themed(m_area, "tx2", "#b8bfdd"), 1.0);
      cr->move_to(LEFT_PAD + 16, cy - lh / 2.0);
      gl->show_in_cairo_context(cr);

      // faint dashed connectors over interior gaps (the braid silence)
      std::vector<double> dashes{3.0, 3.0};
      cr->set_dash(dashes, 0.0);
      set_src(cr, hue, 0.22);
      cr->set_line_width(1.0);
      for (const auto& g : rel.gaps) {
        cr->move_to(col_cx(g.start_pos - 1), cy);
        cr->line_to(col_cx(g.end_pos), cy);
        cr->stroke();
      }
      cr->unset_dash();

      // bars (contiguous blocks) and single-scene pills (a lone beat of a thread)
      for (const auto& seg : rel.segments) {
        if (seg.kind == ReliefSegment::Kind::Bar) {
          const double bx = col_cx(seg.start_pos - 1) - CARD_W / 2.0;           // s87 edge-to-edge
          const double bw = (col_cx(seg.end_pos - 1) + CARD_W / 2.0) - bx;
          rounded_rect(cr, bx, cy - BAR_H / 2.0, bw, BAR_H, BAR_H / 2.0);
          set_src(cr, hue, 0.90);
          cr->fill();
        } else {
          const double sx = col_cx(seg.start_pos - 1) - CARD_W / 2.0;
          rounded_rect(cr, sx, cy - BAR_H / 2.0, CARD_W, BAR_H, BAR_H / 2.0);
          set_src(cr, hue, 0.95);
          cr->fill();
        }
      }
    }
  }

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
      const double px = col_cx(lo - 1) - CARD_W / 2.0;            // s87 edge-to-edge
      const double pw = (col_cx(hi - 1) + CARD_W / 2.0) - px;
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
      const double px = col_cx(lo - 1) - CARD_W / 2.0;            // s87 edge-to-edge
      const double pw = (col_cx(hi - 1) + CARD_W / 2.0) - px;
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
      const double px = col_cx(sr.lo - 1) - CARD_W / 2.0;         // s87 edge-to-edge
      const double pw = (col_cx(sr.hi - 1) + CARD_W / 2.0) - px;
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
      const double px = col_cx(sr.lo - 1) - CARD_W / 2.0;         // s87 edge-to-edge
      const double pw = (col_cx(sr.hi - 1) + CARD_W / 2.0) - px;
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
}

}  // namespace Folio
