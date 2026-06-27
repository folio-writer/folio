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

// ── Surface ──────────────────────────────────────────────────────────────────

TimelineSurface::TimelineSurface(DocumentModel& model, FolioPrefs& prefs)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 0), m_model(model), m_prefs(prefs) {
  set_hexpand(true);
  set_vexpand(true);
  set_name("timeline-surface");

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
  append(m_overlay);

  // Click a scene card → open it (read-only navigation, like the Map lens).
  auto click = Gtk::GestureClick::create();
  click->set_button(GDK_BUTTON_PRIMARY);
  click->signal_released().connect([this](int /*n*/, double x, double y) {
    const std::string iid = scene_at(x, y);
    if (!iid.empty() && m_on_open) m_on_open(iid);
  });
  m_area.add_controller(click);

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

  // s80 step 5c — the subject-first sweep. A primary drag that BEGINS on a track
  // row arms that row's subject and sweeps a span of scene columns; on release a
  // real drag links the subject across the swept span (plan_sweep → subject_links).
  // A bare press (no movement) commits nothing — a click on a track is a no-op.
  auto drag = Gtk::GestureDrag::create();
  drag->set_button(GDK_BUTTON_PRIMARY);
  drag->signal_drag_begin().connect([this](double x, double y) {
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
    if (m_sweep_track < 0) return;
    if (std::abs(ox) > 2.0 || std::abs(oy) > 2.0) m_sweep_moved = true;
    m_sweep_to_col = clamped_col(m_sweep_start_x + ox);
    m_area.queue_draw();
  });
  drag->signal_drag_end().connect([this](double /*ox*/, double /*oy*/) {
    if (m_sweep_track >= 0 && m_sweep_moved) commit_sweep();
    m_sweep_track = -1;
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
  if (m_sweep_track < 0 || m_sweep_track >= static_cast<int>(m_tracks.size())) return;
  const TimelineTrack& tk = m_tracks[static_cast<std::size_t>(m_sweep_track)];
  const std::string subject = tk.iid;
  const SweepPlan plan =
      plan_sweep(m_spine_iids, m_sweep_from_col, m_sweep_to_col, tk.claimed);
  if (plan.add.empty()) return;   // span fully covered already — nothing to write

  for (const std::string& scene_iid : plan.add) {
    BinderNode* sn = m_model.find_node_by_iid(scene_iid);
    if (!sn) continue;
    // dedupe: a subject is linked to a scene at most once in the store.
    if (std::find(sn->subject_links.begin(), sn->subject_links.end(), subject)
        == sn->subject_links.end())
      sn->subject_links.push_back(subject);
  }
  m_model.mark_modified();
  rebuild();   // re-read edges → tracks recompute; the new bar appears live
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

// The relief tracks start below the KP strip when the project has KPs, else
// directly below the cards (the strip reserves no space when empty, §9.4).
int TimelineSurface::track_top() const {
  const int after_spine = spine_top() + CARD_H;
  const int after_kp = m_kp_lanes.empty() ? after_spine
                                          : kp_top() + KP_H;
  return after_kp + TRACK_PAD;
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
    // No tracks: bottom is the KP strip if present, else the spine cards.
    const int floor_y = m_kp_lanes.empty() ? spine_top() + CARD_H
                                           : kp_top() + KP_H;
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

  m_empty_hint.set_visible(m_proj.spine.empty());
  m_area.set_content_width(std::max(content_width(), 1));
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

  // ── Relief tracks (s80 step 3) ─────────────────────────────────────────────
  // One row per subject: its compute_relief drawn as bars (runs), dots
  // (singletons), and faint dashed connectors over interior gaps. Hue = category.
  const int ttop = track_top();
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
        set_src(cr, hue, a_dot);
        cr->arc(col_cx(seg.start_pos - 1), cy, DOT_R, 0, 2 * M_PI);
        cr->fill();
      }
    }
  }

  // s80 step 5c — live sweep preview: a ghost span on the armed track row across
  // the swept columns, in the subject's hue (drawn on top of the tracks).
  if (m_sweep_track >= 0 && m_sweep_track < static_cast<int>(m_tracks.size())) {
    const int lo = std::min(m_sweep_from_col, m_sweep_to_col);
    const int hi = std::max(m_sweep_from_col, m_sweep_to_col);
    if (lo >= 1 && hi >= lo) {
      Gdk::RGBA hue; hue.set(category_hue(m_tracks[static_cast<std::size_t>(m_sweep_track)].category));
      const double cy = ttop + static_cast<double>(m_sweep_track) * (TRACK_H + TRACK_GAP)
                        + TRACK_H / 2.0;
      const double px = col_cx(lo - 1) - DOT_R;
      const double pw = (col_cx(hi - 1) + DOT_R) - px;
      const double ph = BAR_H + 6.0;
      rounded_rect(cr, px, cy - ph / 2.0, pw, ph, ph / 2.0);
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

}  // namespace Folio
