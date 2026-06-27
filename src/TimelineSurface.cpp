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

#include "DocumentModel.hpp"
#include "FolioPrefs.hpp"
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
constexpr int DOT_R         = 6;    // relief dot / bar-cap radius
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

  // s82 — adjustable split: rail (start) | spine canvas (end). The rail keeps
  // its width when the window resizes (the canvas takes the slack) but the
  // author can drag the divider; it can be dragged narrow but not collapsed.
  m_paned.set_name("timeline-split");
  m_paned.set_start_child(m_rail_scroll);
  m_paned.set_end_child(m_overlay);
  m_paned.set_resize_start_child(false);
  m_paned.set_shrink_start_child(false);
  m_paned.set_resize_end_child(true);
  m_paned.set_shrink_end_child(true);
  m_paned.set_position(RAIL_W);
  m_paned.set_hexpand(true);
  m_paned.set_vexpand(true);
  append(m_paned);

  // Click a scene card → open it (read-only navigation, like the Map lens).
  // s82 (Scott review) — with Alt or Ctrl held, a click instead TOGGLES one
  // scene's association on the row under the cursor (precise non-contiguous
  // edit; the sweep remains the span gesture). Either modifier works so a WM
  // that grabs Alt+click (some GNOME setups) still leaves Ctrl+click free.
  auto click = Gtk::GestureClick::create();
  click->set_button(GDK_BUTTON_PRIMARY);
  Gtk::GestureClick* clickp = click.get();   // raw: m_area owns the controller
  click->signal_released().connect([this, clickp](int /*n*/, double x, double y) {
    const Gdk::ModifierType st = clickp->get_current_event_state();
    const bool mod = (st & Gdk::ModifierType::ALT_MASK)     != Gdk::ModifierType{} ||
                     (st & Gdk::ModifierType::CONTROL_MASK) != Gdk::ModifierType{};
    if (mod) { toggle_cell(x, y); return; }
    const std::string iid = scene_at(x, y);
    if (!iid.empty() && m_on_open) m_on_open(iid);
  });
  m_area.add_controller(click);

  // s82 — secondary (right) click on a relief track row opens its context menu
  // (remove the subject from the timeline / unlink the scene under the cursor).
  auto sec = Gtk::GestureClick::create();
  sec->set_button(GDK_BUTTON_SECONDARY);
  Gtk::GestureClick* secp = sec.get();   // raw: m_area owns the controller (no cycle)
  sec->signal_pressed().connect([this, secp](int /*n*/, double x, double y) {
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
      m_sweep_start_x = x;
      m_sweep_from_col = clamped_col(x);
      m_sweep_to_col = m_sweep_from_col;
      m_sweep_moved = false;
      m_area.queue_draw();
      return;
    }
    m_sweep_is_armed = false;
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
    if (m_sweep_track < 0 && !m_sweep_is_armed) return;
    if (std::abs(ox) > 2.0 || std::abs(oy) > 2.0) m_sweep_moved = true;
    m_sweep_to_col = clamped_col(m_sweep_start_x + ox);
    m_area.queue_draw();
  });
  drag->signal_drag_end().connect([this](double /*ox*/, double /*oy*/) {
    if ((m_sweep_track >= 0 || m_sweep_is_armed) && m_sweep_moved) commit_sweep();
    m_sweep_track = -1;
    m_sweep_is_armed = false;
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
  if (m_sweep_is_armed) m_armed_iid.clear();  // placed — clear the arm before rebuild
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
  std::string subject;
  if (staging_active() && over_staging(y)) {
    subject = m_armed_iid;
  } else {
    const int trk = track_row_at(y);
    if (trk < 0) return;
    subject = m_tracks[static_cast<std::size_t>(trk)].iid;
  }
  if (subject.empty()) return;

  const int col = column_at(x);
  if (col <= 0) return;
  const std::string scene_iid = scene_iid_at_col(col);
  if (scene_iid.empty()) return;

  BinderNode* sn = m_model.find_node_by_iid(scene_iid);
  if (!sn) return;
  auto& v = sn->subject_links;
  auto it = std::find(v.begin(), v.end(), subject);
  if (it != v.end()) v.erase(it);            // toggle off
  else               v.push_back(subject);   // toggle on
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
const std::unordered_set<std::string>* TimelineSurface::armed_claimed() const {
  if (m_armed_iid.empty()) return nullptr;
  for (const TimelineTrack& t : m_tracks)
    if (t.iid == m_armed_iid) return &t.claimed;
  return nullptr;
}

// A KP's hue: its stamped color_idx into the project palette (the spectrum the
// materializer copied in), or orange when unstamped (color_idx 0 / OOB) — the
// runtime-pinned beat that never went through materialization (§9.6, s81 ruling).
std::string TimelineSurface::kp_hex(int color_idx) const {
  const std::string hex = m_prefs.color_hex_for_idx(color_idx);  // "" if 0/OOB
  return hex.empty() ? "#fab387" : hex;                          // peach = orange
}

int TimelineSurface::content_width() const {
  const int n = static_cast<int>(m_proj.spine.size());
  return x0() + n * COL + LEFT_PAD;
}

int TimelineSurface::content_height() const {
  const int n_tracks = static_cast<int>(m_tracks.size());
  if (n_tracks == 0) {
    // No tracks: the floor is the staging row (if armed), else the KP strip (if
    // present), else the spine cards.
    int floor_y = m_kp_lanes.empty() ? spine_top() + CARD_H : kp_top() + KP_H;
    if (staging_active()) floor_y = staging_top() + STAGE_H;
    return floor_y + BOT_PAD;
  }
  return track_top() + n_tracks * (TRACK_H + TRACK_GAP) + BOT_PAD;
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

  // If the armed subject was deleted since arming, drop the (now dangling) arm.
  if (!m_armed_iid.empty()) {
    bool still = false;
    for (const auto& c : candidates)
      if (c.iid == m_armed_iid) { still = true; break; }
    if (!still) m_armed_iid.clear();
  }
  build_rail(assemble_resources(candidates, m_tracks));

  m_empty_hint.set_visible(m_proj.spine.empty());
  m_area.set_content_width(std::max(content_width(), 1));
  m_area.set_content_height(std::max(content_height(), 1));
  m_area.queue_draw();
}

// ── Resource rail (s82) ──────────────────────────────────────────────────────

void TimelineSurface::build_rail(const std::vector<ResourceGroup>& groups) {
  // Tear down the previous rows (managed children destroyed on remove); the
  // member m_rail_empty is merely unparented and re-appended below.
  m_rail_rows.clear();
  while (Gtk::Widget* c = m_rail_box.get_first_child()) m_rail_box.remove(*c);

  bool any = false;
  for (const ResourceGroup& g : groups) any = any || !g.items.empty();

  if (!any) {
    m_rail_empty.set_visible(true);
    m_rail_box.append(m_rail_empty);
    return;
  }
  m_rail_empty.set_visible(false);

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

    Gdk::RGBA hue; hue.set(category_hue(g.category));
    for (const ResourceItem& it : g.items) {
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
}

void TimelineSurface::arm_subject(const std::string& iid, const std::string& label,
                                  TrackCategory cat) {
  if (m_armed_iid == iid) { disarm(); return; }   // re-click the armed row → off
  m_armed_iid = iid;
  m_armed_label = label;
  m_armed_cat = cat;
  for (auto& [riid, w] : m_rail_rows) {
    if (!w) continue;
    if (riid == iid) w->add_css_class("armed");
    else             w->remove_css_class("armed");
  }
  m_area.set_content_height(std::max(content_height(), 1));  // staging row reserves space
  m_area.queue_draw();
}

void TimelineSurface::disarm() {
  m_armed_iid.clear();
  m_armed_label.clear();
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
          const double bx = col_cx(seg.start_pos - 1) - KP_DIAMOND_R;
          const double bw = (col_cx(seg.end_pos - 1) + KP_DIAMOND_R) - bx;
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
    Gdk::RGBA hue; hue.set(category_hue(m_armed_cat));

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
          const double bx = col_cx(seg.start_pos - 1) - DOT_R;
          const double bw = (col_cx(seg.end_pos - 1) + DOT_R) - bx;
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
        Gdk::RGBA chue; chue.set(category_hue(m_tracks[t].category));
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
    Gdk::RGBA hue; hue.set(category_hue(tk.category));
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
        const double bx = col_cx(seg.start_pos - 1) - DOT_R;
        const double bw = (col_cx(seg.end_pos - 1) + DOT_R) - bx;
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

  // s80 step 5c — live sweep preview: a ghost span on the armed track row across
  // the swept columns, in the subject's hue (drawn on top of the tracks).
  if (m_sweep_track >= 0 && m_sweep_track < static_cast<int>(m_tracks.size())) {
    const SweepRange sr =
        sweep_range(&m_tracks[static_cast<std::size_t>(m_sweep_track)].claimed);
    const bool unlink = sr.remove;
    const int lo = sr.lo;
    const int hi = sr.hi;
    if (sr.valid) {
      Gdk::RGBA hue; hue.set(category_hue(m_tracks[static_cast<std::size_t>(m_sweep_track)].category));
      const double cy = ttop + static_cast<double>(m_sweep_track) * (TRACK_H + TRACK_GAP)
                        + TRACK_H / 2.0;
      const double px = col_cx(lo - 1) - DOT_R;
      const double pw = (col_cx(hi - 1) + DOT_R) - px;
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
      Gdk::RGBA hue; hue.set(category_hue(m_armed_cat));
      const double cy = staging_top() + STAGE_H / 2.0;
      const double px = col_cx(lo - 1) - DOT_R;
      const double pw = (col_cx(hi - 1) + DOT_R) - px;
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
}

}  // namespace Folio
