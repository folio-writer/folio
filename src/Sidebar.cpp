// ─────────────────────────────────────────────────────────────────────────────
// Folio — Sidebar.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "Sidebar.hpp"
#include "color_utils.hpp"
#include "FolioLog.hpp"
#include "Iid.hpp"
#include <algorithm>
#include <cairomm/context.h>
#include <cmath>
#include <gdkmm/contentprovider.h>
#include <gdkmm/enums.h>
#include <giomm/appinfo.h>
#include <graphene.h>
#include <gtk/gtk.h>
#include <gtkmm/dragsource.h>
#include <gtkmm/droptarget.h>
#include <gtkmm/eventcontrollerkey.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/popovermenu.h>
#include <iomanip>
#include <memory>
#include <sstream>

namespace Folio {

// ─────────────────────────────────────────────────────────────────────────────
// Static helpers
// ─────────────────────────────────────────────────────────────────────────────

// s19: GTK-naming discipline — the widget role label for a binder row, by kind.
// Composed with the node's iid via Folio::widget_name() so every model-bound
// row is named "<role>-<iid>" (e.g. "scene-row-scn_k3f9a2b7"): one string ties
// the widget (inspector/CSS), the model node, its files, and the log together.
static std::string sidebar_row_role(BinderKind k) {
    switch (k) {
        case BinderKind::Group:     return "group-row";
        case BinderKind::Character: return "character-row";
        case BinderKind::Place:     return "place-row";
        case BinderKind::Reference: return "reference-row";
        case BinderKind::Template:  return "template-row";
        default:                    return "scene-row";
    }
}

// Recursively collect a group node + all its descendants as positional SelPaths.
static void collect_group_and_descendants(BinderNode *node, Section section,
                                          const std::vector<int> &path,
                                          std::set<SelPath> &out) {
  if (!node)
    return;
  out.insert(SelPath::make(section, path));
  for (int i = 0; i < (int)node->children.size(); ++i) {
    auto child_path = path;
    child_path.push_back(i);
    collect_group_and_descendants(&node->children[i], section, child_path, out);
  }
}

// Strip HTML tags to plain text (for body-content searching).
static std::string strip_html(const std::string &html) {
  std::string out;
  out.reserve(html.size());
  bool in_tag = false;
  for (size_t i = 0; i < html.size(); ++i) {
    if (html[i] == '<') {
      // Emit a space for block-level tags so words don't merge.
      size_t j = i + 1;
      if (j < html.size() && html[j] == '/')
        ++j;
      // peek tag name
      std::string tn;
      while (j < html.size() && html[j] != '>' && html[j] != ' ')
        tn += ::tolower((unsigned char)html[j++]);
      if (tn == "p" || tn == "br" || tn == "div") {
        if (!out.empty() && out.back() != ' ')
          out += ' ';
      }
      in_tag = true;
    } else if (html[i] == '>') {
      in_tag = false;
    } else if (!in_tag) {
      out += html[i];
    }
  }
  // decode a handful of common entities
  std::string decoded;
  decoded.reserve(out.size());
  for (size_t i = 0; i < out.size();) {
    if (out[i] == '&') {
      if (out.substr(i, 5) == "&amp;") {
        decoded += '&';
        i += 5;
      } else if (out.substr(i, 4) == "&lt;") {
        decoded += '<';
        i += 4;
      } else if (out.substr(i, 4) == "&gt;") {
        decoded += '>';
        i += 4;
      } else if (out.substr(i, 6) == "&quot;") {
        decoded += '"';
        i += 6;
      } else {
        decoded += out[i++];
      }
    } else {
      decoded += out[i++];
    }
  }
  return decoded;
}

static bool matches_filter(const std::string &filter, const std::string &text) {
  if (filter.empty())
    return true;
  std::string lower = text;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  return lower.find(filter) != std::string::npos;
}

// Match filter against content HTML (strips tags first).
static bool matches_filter_html(const std::string &filter,
                                const std::string &html) {
  if (filter.empty())
    return true;
  std::string plain = strip_html(html);
  std::transform(plain.begin(), plain.end(), plain.begin(), ::tolower);
  return plain.find(filter) != std::string::npos;
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

Sidebar::Sidebar(DocumentModel &model, FolioPrefs &prefs)
    : Gtk::Box(Gtk::Orientation::VERTICAL),
      m_scroll_content(Gtk::Orientation::VERTICAL), m_model(model),
      m_prefs(prefs) {
  add_css_class("folio-sidebar");
  set_size_request(220, -1);
  set_overflow(Gtk::Overflow::HIDDEN);

  // Pre-seed tile state from prefs so the tile shows correct values
  // before the first push_pomodoro_to_sidebar() call from MainWindow.
  m_pomo_sessions_before_long = m_prefs.pomodoro.sessions_before_long;

  build_ui();
}

// ─────────────────────────────────────────────────────────────────────────────
// build_ui
// ─────────────────────────────────────────────────────────────────────────────

void Sidebar::build_ui() {
  // Search entry row — filter + escalate to global search
  auto *search_row =
      Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
  search_row->set_margin_top(10);
  search_row->set_margin_bottom(6);
  search_row->set_margin_start(12);
  search_row->set_margin_end(8);

  m_search_entry.set_placeholder_text("Filter project…");
  m_search_entry.set_tooltip_text(
      "Filter titles — press Enter to search & replace globally");
  m_search_entry.set_hexpand(true);
  m_search_entry.signal_search_changed().connect([this]() {
    std::string raw = std::string(m_search_entry.get_text());
    std::transform(raw.begin(), raw.end(), raw.begin(), ::tolower);
    m_search_filter = raw;
    rebuild_section(Section::Manuscript);
    rebuild_section(Section::Characters);
    rebuild_section(Section::Places);
    rebuild_section(Section::References);
    rebuild_section(Section::Templates);
  });
  // Enter → escalate to global search dialog
  m_search_entry.signal_activate().connect([this]() {
    if (m_on_global_search)
      m_on_global_search(std::string(m_search_entry.get_text()));
  });

  auto *global_btn = Gtk::make_managed<Gtk::Button>();
  global_btn->set_icon_name("edit-find-replace-symbolic");
  global_btn->add_css_class("flat");
  global_btn->set_tooltip_text("Open Search & Replace (Ctrl+Shift+G)");
  global_btn->set_valign(Gtk::Align::CENTER);
  global_btn->signal_clicked().connect([this]() {
    if (m_on_global_search)
      m_on_global_search(std::string(m_search_entry.get_text()));
  });

  search_row->append(m_search_entry);
  search_row->append(*global_btn);
  append(*search_row);

  auto *sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
  append(*sep);

  m_scroll_content.set_margin_top(4);
  m_scroll.set_child(m_scroll_content);
  m_scroll.set_vexpand(true);
  m_scroll.set_propagate_natural_height(false);
  m_scroll.set_min_content_height(1);
  m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  append(m_scroll);

  set_focusable(true);
  auto key_ctrl = Gtk::EventControllerKey::create();
  key_ctrl->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
  key_ctrl->signal_key_pressed().connect(
      [this](guint k, guint, Gdk::ModifierType s) -> bool {
        if (k == GDK_KEY_Escape && !m_board_selection.empty()) {
          m_board_selection.clear();
          refresh_all_highlights();
          fire_board_selection();
          return true;
        }

        // Ctrl+A: select all nodes in the same category as the current selection.
        // No-op if nothing is selected.
        bool ctrl_held = (s & Gdk::ModifierType::CONTROL_MASK) != Gdk::ModifierType{};
        if (ctrl_held && (k == GDK_KEY_a || k == GDK_KEY_A)) {
          if (m_board_selection.empty())
            return true; // no-op
          Section sec = m_board_selection.begin()->section;
          // Collect every node in the section recursively
          std::function<void(const std::vector<BinderNode>&, std::vector<int>)> collect_all;
          collect_all = [&](const std::vector<BinderNode> &nodes, std::vector<int> parent_path) {
            for (int i = 0; i < (int)nodes.size(); ++i) {
              auto p = parent_path;
              p.push_back(i);
              m_board_selection.insert(SelPath::make(sec, p));
              if (!nodes[i].children.empty())
                collect_all(nodes[i].children, p);
            }
          };
          m_board_selection.clear();
          collect_all(m_model.root(sec), {});
          refresh_all_highlights();
          fire_board_selection();
          return true;
        }

        bool shift = (s & Gdk::ModifierType::SHIFT_MASK) !=
                     (Gdk::ModifierType)0;
        bool is_up = (k == GDK_KEY_Up || k == GDK_KEY_KP_Up);
        bool is_down = (k == GDK_KEY_Down || k == GDK_KEY_KP_Down);
        bool is_left = (k == GDK_KEY_Left || k == GDK_KEY_KP_Left);
        bool is_right = (k == GDK_KEY_Right || k == GDK_KEY_KP_Right);

        if (!is_up && !is_down && !is_left && !is_right)
          return false;
        if (m_board_selection.empty())
          return false;

        // Use the first selected item as the cursor
        const auto &cur = *m_board_selection.begin();
        Section section = cur.section;
        auto path = cur.path;

        auto navigate_to = [&](Section sec, std::vector<int> new_path) {
          m_model.set_active(sec, new_path);
          m_board_selection = {SelPath::make(sec, new_path)};
          refresh_all_highlights();
          fire_board_selection();
          if (m_on_selected)
            m_on_selected(sec, new_path);
        };

        // Helper: find collapse entry for a path
        auto find_collapse = [&](Section sec,
                                 const std::vector<int> &p) -> CollapseEntry * {
          for (auto &e : m_collapse_entries)
            if (e.section == sec && e.path == p)
              return &e;
          return nullptr;
        };

        if (shift) {
          if (is_right) {
            // Shift+Right: expand group if collapsed, then enter first child
            const BinderNode *n = m_model.node_at(section, path);
            if (n && binder_kind_is_group(n->kind) && !n->children.empty()) {
              auto *ce = find_collapse(section, path);
              if (ce && !ce->expanded) {
                // Expand first
                for (int i = 0; i < (int)m_collapse_entries.size(); ++i)
                  if (&m_collapse_entries[i] == ce) {
                    toggle_node(i);
                    break;
                  }
              }
              auto child_path = path;
              child_path.push_back(0);
              navigate_to(section, child_path);
            }
            return true;
          }
          if (is_left) {
            // If current node is an expanded group, collapse it first.
            // If already collapsed (or a leaf), step out to parent.
            auto *ce = find_collapse(section, path);
            if (ce && ce->expanded) {
              for (int i = 0; i < (int)m_collapse_entries.size(); ++i)
                if (&m_collapse_entries[i] == ce) {
                  toggle_node(i);
                  break;
                }
            } else if (path.size() > 1) {
              auto parent_path = std::vector<int>(path.begin(), path.end() - 1);
              navigate_to(section, parent_path);
            }
            return true;
          }
          return false;
        }

        // Plain Up/Down: prev/next sibling
        std::vector<int> parent_path(path.begin(), path.end() - 1);
        int idx = path.back();

        const std::vector<BinderNode> *siblings = nullptr;
        if (parent_path.empty()) {
          siblings = &m_model.root(section);
        } else {
          const BinderNode *parent = m_model.node_at(section, parent_path);
          if (parent)
            siblings = &parent->children;
        }
        if (!siblings)
          return false;

        int next_idx = idx + (is_down ? 1 : -1);

        if (next_idx >= 0 && next_idx < (int)siblings->size()) {
          // Normal: next sibling exists
          auto new_path = parent_path;
          new_path.push_back(next_idx);
          navigate_to(section, new_path);
          return true;
        }

        if (is_down) {
          // Past last sibling — walk up the ancestor chain looking for a
          // parent that has a next sibling after itself.
          std::vector<int> walk = path; // start at current node
          while (!walk.empty()) {
            int my_idx = walk.back();
            walk.pop_back(); // walk is now my parent's path

            // Find my parent's siblings
            const std::vector<BinderNode> *pool = nullptr;
            if (walk.empty()) {
              pool = &m_model.root(section);
            } else {
              auto grandparent = std::vector<int>(walk.begin(), walk.end() - 1);
              const BinderNode *gp = m_model.node_at(section, grandparent);
              if (gp)
                pool = &gp->children;
            }
            if (!pool)
              break;

            if (my_idx + 1 < (int)pool->size()) {
              // Found a next sibling at this ancestor level
              auto new_path = walk;
              new_path.push_back(my_idx + 1);
              navigate_to(section, new_path);
              return true;
            }
            // my_idx was already the last — keep walking up
          }
          return true; // already at the very last node in the section
        }

        // Up at first sibling — stop
        return true;
      },
      false);
  add_controller(key_ctrl);

  build_section_header(Section::Manuscript);
  m_manuscript_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
  {
    auto *rev = Gtk::make_managed<Gtk::Revealer>();
    rev->set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);
    rev->set_transition_duration(180);
    rev->set_reveal_child(true);
    rev->set_child(*m_manuscript_box);
    m_scroll_content.append(*rev);
    m_sec_manuscript.revealer = rev;
  }
  rebuild_section(Section::Manuscript);

  {
    auto *sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
    sep->add_css_class("binder-section-sep");
    m_scroll_content.append(*sep);
  }
  build_section_header(Section::Characters);
  m_characters_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
  {
    auto *rev = Gtk::make_managed<Gtk::Revealer>();
    rev->set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);
    rev->set_transition_duration(180);
    rev->set_reveal_child(true);
    rev->set_child(*m_characters_box);
    m_scroll_content.append(*rev);
    m_sec_characters.revealer = rev;
  }
  rebuild_section(Section::Characters);

  {
    auto *sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
    sep->add_css_class("binder-section-sep");
    m_scroll_content.append(*sep);
  }
  build_section_header(Section::Places);
  m_places_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
  {
    auto *rev = Gtk::make_managed<Gtk::Revealer>();
    rev->set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);
    rev->set_transition_duration(180);
    rev->set_reveal_child(true);
    rev->set_child(*m_places_box);
    m_scroll_content.append(*rev);
    m_sec_places.revealer = rev;
  }
  rebuild_section(Section::Places);

  {
    auto *sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
    sep->add_css_class("binder-section-sep");
    m_scroll_content.append(*sep);
  }
  build_section_header(Section::References);
  m_references_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
  {
    auto *rev = Gtk::make_managed<Gtk::Revealer>();
    rev->set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);
    rev->set_transition_duration(180);
    rev->set_reveal_child(true);
    rev->set_child(*m_references_box);
    m_scroll_content.append(*rev);
    m_sec_references.revealer = rev;
  }
  rebuild_section(Section::References);

  {
    auto *sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
    sep->add_css_class("binder-section-sep");
    m_scroll_content.append(*sep);
  }
  build_section_header(Section::Templates);
  m_templates_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
  {
    auto *rev = Gtk::make_managed<Gtk::Revealer>();
    rev->set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);
    rev->set_transition_duration(180);
    rev->set_reveal_child(true);
    rev->set_child(*m_templates_box);
    m_scroll_content.append(*rev);
    m_sec_templates.revealer = rev;
  }
  rebuild_section(Section::Templates);

  {
    auto *sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
    sep->add_css_class("binder-section-sep");
    m_scroll_content.append(*sep);
  }
  build_section_header(Section::Trash);
  m_trash_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
  {
    auto *rev = Gtk::make_managed<Gtk::Revealer>();
    rev->set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);
    rev->set_transition_duration(180);
    rev->set_reveal_child(true);
    rev->set_child(*m_trash_box);
    m_scroll_content.append(*rev);
    m_sec_trash.revealer = rev;
  }
  rebuild_section(Section::Trash);

  build_pomodoro_tile();
  build_session_footer();
}

// ─────────────────────────────────────────────────────────────────────────────
// Pomodoro disclosure tile
// Sits above the SESSION card. Shows a mini horizontal ring + time/phase label.
// Clicking anywhere on it fires m_on_pomodoro_tile_clicked (opens the dialog).
// ─────────────────────────────────────────────────────────────────────────────

void Sidebar::build_pomodoro_tile() {
  auto *sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
  append(*sep);

  // ── Outer card ───────────────────────────────────────────────────────────
  auto *card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  card->add_css_class("pomo-tile-card");
  card->set_margin_start(8);
  card->set_margin_end(8);
  card->set_margin_top(6);
  card->set_margin_bottom(0);

  // ── Header row: "POMODORO" label | chevron (right) ───────────────────────
  auto *hdr_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
  hdr_row->set_cursor(Gdk::Cursor::create("pointer"));
  hdr_row->add_css_class("tile-header-row");

  auto *hdr_lbl = Gtk::make_managed<Gtk::Label>("POMODORO");
  hdr_lbl->add_css_class("session-label");
  hdr_lbl->set_halign(Gtk::Align::START);
  hdr_lbl->set_hexpand(true);

  m_pomo_tile_arrow = Gtk::make_managed<Gtk::Label>("▾");
  m_pomo_tile_arrow->add_css_class("section-arrow");
  m_pomo_tile_arrow->set_margin_end(2);

  hdr_row->append(*hdr_lbl);
  hdr_row->append(*m_pomo_tile_arrow);
  card->append(*hdr_row);

  // ── Revealer wraps the collapsible body ───────────────────────────────────
  m_pomo_tile_revealer = Gtk::make_managed<Gtk::Revealer>();
  m_pomo_tile_revealer->set_reveal_child(m_pomo_tile_expanded);
  m_pomo_tile_revealer->set_transition_type(
      Gtk::RevealerTransitionType::SLIDE_DOWN);
  m_pomo_tile_revealer->set_transition_duration(180);

  auto *body = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  body->set_margin_top(8);
  body->set_margin_bottom(8);

  // ── Horizontal content row: ring-with-play-pause | time+phase+dots ────────
  auto *content_row =
      Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
  content_row->set_valign(Gtk::Align::CENTER);

  // Ring + play/pause overlay via Gtk::Overlay
  auto *ring_overlay = Gtk::make_managed<Gtk::Overlay>();
  ring_overlay->set_valign(Gtk::Align::CENTER);
  ring_overlay->set_halign(Gtk::Align::CENTER);

  // Drawing area: CCW countdown ring (52×52)
  m_pomo_ring = Gtk::make_managed<Gtk::DrawingArea>();
  m_pomo_ring->set_size_request(52, 52);
  m_pomo_ring->set_draw_func([this](const Cairo::RefPtr<Cairo::Context> &cr,
                                    int w, int h) {
    bool dark = true;
    auto style = get_style_context();
    if (style) {
      Gdk::RGBA bg;
      if (style->lookup_color("adw_bg", bg)) {
        double lum = 0.299 * bg.get_red() + 0.587 * bg.get_green() +
                     0.114 * bg.get_blue();
        dark = (lum < 0.5);
      }
    }
    double cx = w * 0.5, cy = h * 0.5;
    double radius = (std::min(w, h) * 0.5) - 5.0;
    constexpr double TWO_PI = 2.0 * M_PI;
    cr->set_line_cap(Cairo::Context::LineCap::ROUND);
    cr->set_line_width(5.0);

    // Background track (full circle)
    cr->arc(cx, cy, radius, -M_PI / 2.0, -M_PI / 2.0 + TWO_PI);
    cr->set_source_rgba(dark ? 1.0 : 0.0, dark ? 1.0 : 0.0, dark ? 1.0 : 0.0,
                        dark ? 0.08 : 0.10);
    cr->stroke();

    // Remaining-time arc — CCW countdown:
    // Draw remaining fraction as a CW arc from 12 o'clock.
    // Arc tail retreats CCW as time passes → visually CCW shrink.
    double remaining = std::max(0.0, std::min(1.0, 1.0 - m_pomo_progress));
    if (remaining > 0.001) {
      // Parse hex color from prefs (shared helper Folio::color::hex_to_rgb01).
      double r, g, b;
      if (m_pomo_phase_str == "Focus") {
        auto [pr, pg, pb] =
            dark ? Folio::color::hex_to_rgb01(m_prefs.pomodoro.focus_color, 0.357, 0.784,
                                     0.686)
                 : std::make_tuple(0.055, 0.388, 0.408);
        r = pr;
        g = pg;
        b = pb;
      } else if (m_pomo_phase_str == "Short Break") {
        auto [pr, pg, pb] =
            dark ? Folio::color::hex_to_rgb01(m_prefs.pomodoro.short_break_color, 0.651,
                                     0.890, 0.631)
                 : std::make_tuple(0.141, 0.376, 0.094);
        r = pr;
        g = pg;
        b = pb;
      } else {
        auto [pr, pg, pb] =
            dark ? Folio::color::hex_to_rgb01(m_prefs.pomodoro.long_break_color, 0.796,
                                     0.651, 0.969)
                 : std::make_tuple(0.353, 0.122, 0.690);
        r = pr;
        g = pg;
        b = pb;
      }
      cr->set_source_rgb(r, g, b);
      cr->arc(cx, cy, radius, -M_PI / 2.0, -M_PI / 2.0 + remaining * TWO_PI);
      cr->stroke();
    }
  });
  ring_overlay->set_child(*m_pomo_ring);
  m_pomo_ring->set_tooltip_text(
      "Time remaining — ring shrinks counter-clockwise");

  // Play/pause button centred inside the ring
  m_pomo_tile_play_btn = Gtk::make_managed<Gtk::Button>();
  m_pomo_tile_play_btn->set_icon_name("media-playback-start-symbolic");
  m_pomo_tile_play_btn->add_css_class("pomo-tile-play-btn");
  m_pomo_tile_play_btn->set_halign(Gtk::Align::CENTER);
  m_pomo_tile_play_btn->set_valign(Gtk::Align::CENTER);
  m_pomo_tile_play_btn->set_tooltip_text("Start / Pause timer");
  m_pomo_tile_play_btn->signal_clicked().connect([this]() {
    if (m_on_pomodoro_tile_play_pause)
      m_on_pomodoro_tile_play_pause();
  });
  ring_overlay->add_overlay(*m_pomo_tile_play_btn);

  content_row->append(*ring_overlay);

  // Right column: time, phase label, session dots
  auto *right_col = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
  right_col->set_valign(Gtk::Align::CENTER);
  right_col->set_hexpand(true);

  // Initial time label from prefs (updated each tick via refresh_pomodoro_tile)
  {
    int focus_sec = m_prefs.pomodoro.focus_min * 60;
    int mm = focus_sec / 60, ss = focus_sec % 60;
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << mm << ':' << std::setfill('0')
        << std::setw(2) << ss;
    m_pomo_time_lbl = Gtk::make_managed<Gtk::Label>(oss.str());
  }
  m_pomo_time_lbl->add_css_class("pomo-tile-time");
  m_pomo_time_lbl->set_halign(Gtk::Align::START);
  m_pomo_time_lbl->set_tooltip_text("Time remaining in the current phase");

  m_pomo_phase_lbl = Gtk::make_managed<Gtk::Label>("Focus");
  m_pomo_phase_lbl->add_css_class("pomo-tile-phase");
  m_pomo_phase_lbl->set_halign(Gtk::Align::START);
  m_pomo_phase_lbl->set_tooltip_text(
      "Current phase: Focus = writing time, Short/Long Break = rest time");

  m_pomo_dot_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
  m_pomo_dot_row->set_halign(Gtk::Align::START);
  m_pomo_dot_row->set_margin_top(3);
  m_pomo_dot_row->set_tooltip_text(
      "Session pips — filled dots are completed focus sessions this cycle");

  right_col->append(*m_pomo_time_lbl);
  right_col->append(*m_pomo_phase_lbl);
  right_col->append(*m_pomo_dot_row);
  content_row->append(*right_col);
  body->append(*content_row);

  // ── Bottom action row: "Open Timer" + settings gear ──────────────────────
  auto *action_row =
      Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
  action_row->set_margin_top(8);
  action_row->set_halign(Gtk::Align::FILL);

  auto *open_btn = Gtk::make_managed<Gtk::Button>("Open Timer ↗");
  open_btn->add_css_class("pomo-tile-open-btn");
  open_btn->set_hexpand(true);
  open_btn->set_halign(Gtk::Align::FILL);
  open_btn->set_tooltip_text("Open the floating Pomodoro timer window");
  open_btn->signal_clicked().connect([this]() {
    if (m_on_pomodoro_tile_clicked)
      m_on_pomodoro_tile_clicked();
  });

  auto *settings_btn = Gtk::make_managed<Gtk::Button>();
  settings_btn->set_icon_name("org.gnome.Settings-system-symbolic");
  settings_btn->add_css_class("pomo-ctrl-btn");
  settings_btn->set_tooltip_text("Open Pomodoro settings");
  settings_btn->signal_clicked().connect([this]() {
    if (m_on_pomodoro_tile_settings)
      m_on_pomodoro_tile_settings();
  });

  action_row->append(*open_btn);
  action_row->append(*settings_btn);
  body->append(*action_row);

  m_pomo_tile_revealer->set_child(*body);
  card->append(*m_pomo_tile_revealer);
  append(*card);

  // ── Disclosure: click on header row toggles revealer ─────────────────────
  auto gc = Gtk::GestureClick::create();
  gc->set_button(1);
  gc->signal_pressed().connect([this](int, double, double) {
    m_pomo_tile_expanded = !m_pomo_tile_expanded;
    m_pomo_tile_revealer->set_reveal_child(m_pomo_tile_expanded);
    if (m_pomo_tile_arrow)
      m_pomo_tile_arrow->set_text(m_pomo_tile_expanded ? "▾" : "▸");
    if (m_on_disclosure_changed)
      m_on_disclosure_changed();
  });
  hdr_row->add_controller(gc);
}

// ─────────────────────────────────────────────────────────────────────────────
// refresh_pomodoro_tile — push live state from MainWindow
// ─────────────────────────────────────────────────────────────────────────────

void Sidebar::refresh_pomodoro_tile(double progress, int remaining_sec,
                                    bool running,
                                    const std::string &phase_label,
                                    int session_in_cycle,
                                    int sessions_before_long) {
  m_pomo_progress = progress;
  m_pomo_running = running;
  m_pomo_phase_str = phase_label;
  m_pomo_session_in_cycle = session_in_cycle;
  m_pomo_sessions_before_long = sessions_before_long;

  // Time label
  if (m_pomo_time_lbl) {
    int m = remaining_sec / 60, s = remaining_sec % 60;
    std::ostringstream ss;
    ss << std::setfill('0') << std::setw(2) << m << ':' << std::setfill('0')
       << std::setw(2) << s;
    m_pomo_time_lbl->set_text(ss.str());
  }

  // Phase label — plain, no ▶ clutter (play/pause button shows state)
  if (m_pomo_phase_lbl)
    m_pomo_phase_lbl->set_text(phase_label);

  // Play/pause button icon
  if (m_pomo_tile_play_btn) {
    m_pomo_tile_play_btn->set_icon_name(running
                                            ? "media-playback-pause-symbolic"
                                            : "media-playback-start-symbolic");
  }

  // Session dots — color driven by display-level CSS override in apply_theme()
  if (m_pomo_dot_row) {
    while (auto *ch = m_pomo_dot_row->get_first_child())
      m_pomo_dot_row->remove(*ch);
    bool in_focus = (phase_label == "Focus");

    for (int i = 0; i < sessions_before_long; ++i) {
      auto *dot = Gtk::make_managed<Gtk::Box>();
      dot->set_size_request(7, 7);
      dot->set_valign(Gtk::Align::CENTER);
      dot->add_css_class("pomo-dot");

      // Per-pip tooltip
      std::string tip = "Session " + std::to_string(i + 1) + " of " +
                        std::to_string(sessions_before_long);
      if (i < session_in_cycle)
        tip += " — completed ✓";
      else if (i == session_in_cycle && in_focus)
        tip += " — in progress ▶";
      else
        tip += " — pending";
      dot->set_tooltip_text(tip);

      if (i < session_in_cycle)
        dot->add_css_class("pomo-dot-done");
      else if (i == session_in_cycle && in_focus)
        dot->add_css_class("pomo-dot-active");
      else
        dot->add_css_class("pomo-dot-empty");

      m_pomo_dot_row->append(*dot);
    }
  }

  // Redraw ring
  if (m_pomo_ring)
    m_pomo_ring->queue_draw();
}

// ─────────────────────────────────────────────────────────────────────────────
// Section header
// ─────────────────────────────────────────────────────────────────────────────

void Sidebar::expand_all_in_section(Section section) {
  for (int i = 0; i < (int)m_collapse_entries.size(); ++i) {
    auto &ce = m_collapse_entries[i];
    if (ce.section != section || ce.expanded)
      continue;
    ce.expanded = true;
    if (ce.revealer) ce.revealer->set_reveal_child(true);
    if (ce.arrow) {
      ce.arrow->set_text("▾");
      ce.arrow->remove_css_class("section-arrow-collapsed");
    }
  }
}

void Sidebar::collapse_all_in_section(Section section) {
  for (int i = 0; i < (int)m_collapse_entries.size(); ++i) {
    auto &ce = m_collapse_entries[i];
    if (ce.section != section || !ce.expanded)
      continue;
    ce.expanded = false;
    if (ce.revealer) ce.revealer->set_reveal_child(false);
    if (ce.arrow) {
      ce.arrow->set_text("▸");
      ce.arrow->add_css_class("section-arrow-collapsed");
    }
  }
}

void Sidebar::build_section_header(Section section) {
  const char *label_text = section == Section::Manuscript   ? "Manuscript"
                           : section == Section::Characters ? "Characters"
                           : section == Section::Places     ? "Places"
                           : section == Section::References ? "References"
                           : section == Section::Templates  ? "Templates"
                                                            : "Trash";

  // Symbolic icon for each section. Characters/Places/References/Templates(group)
  // now use the Curvz-drawn folio-*-symbolic set (gresource); Manuscript and
  // Trash stay on system icons until drawn.
  const char *icon_name =
      section == Section::Manuscript   ? "document-edit-symbolic"
      : section == Section::Characters ? "folio-characters-symbolic"
      : section == Section::Places     ? "folio-places-symbolic"
      : section == Section::References ? "folio-references-symbolic"
      : section == Section::Templates  ? "folio-group-symbolic"
                                       : "user-trash-symbolic";

  const char *tip = section == Section::Trash
                        ? "Click to collapse · Ctrl+click to expand all · Alt+click to collapse all · Right-click to empty trash"
                        : "Click to collapse · Ctrl+click to expand all in section · Alt+click to collapse all in section · Right-click to add\nCtrl+Alt+E = expand all · Ctrl+Alt+K = collapse all";

  auto *row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
  row->set_cursor(Gdk::Cursor::create("pointer"));
  row->set_tooltip_text(tip);
  row->set_margin_start(4);

  // Section icon
  auto *icon = Gtk::make_managed<Gtk::Image>();
  icon->set_from_icon_name(icon_name);
  icon->set_pixel_size(14);
  icon->add_css_class("sidebar-section-icon");
  icon->set_valign(Gtk::Align::CENTER);
  row->append(*icon);

  auto *lbl = Gtk::make_managed<Gtk::Label>(label_text);
  lbl->add_css_class("sidebar-section-label");
  lbl->set_halign(Gtk::Align::START);
  lbl->set_hexpand(true);
  row->append(*lbl);

  auto *arrow = Gtk::make_managed<Gtk::Label>("▾");
  arrow->add_css_class("section-arrow");
  arrow->set_margin_end(8);
  row->append(*arrow);

  section_header(section).arrow = arrow;

  m_scroll_content.append(*row);

  // Left-click: toggle section collapse
  // Ctrl+click: expand all groups in section
  // Alt+click: collapse all groups in section
  auto gc_left = Gtk::GestureClick::create();
  gc_left->set_button(1);
  gc_left->signal_pressed().connect(
      [this, gc_left, section](int, double, double) {
        Gdk::ModifierType mods = gc_left->get_current_event_state();
        bool ctrl = (mods & Gdk::ModifierType::CONTROL_MASK) != Gdk::ModifierType{};
        bool alt  = (mods & Gdk::ModifierType::ALT_MASK)     != Gdk::ModifierType{};
        if (ctrl)
          expand_all_in_section(section);
        else if (alt)
          collapse_all_in_section(section);
        else
          toggle_section(section);
      });
  row->add_controller(gc_left);

  // Right-click: context menu
  auto gc_right = Gtk::GestureClick::create();
  gc_right->set_button(3);
  gc_right->signal_pressed().connect(
      [this, section, row](int, double x, double y) {
        show_section_ctx_menu(section, x, y, row);
      });
  row->add_controller(gc_right);
}

void Sidebar::toggle_section(Section s) {
  auto &sh = section_header(s);
  sh.expanded = !sh.expanded;
  if (sh.revealer)
    sh.revealer->set_reveal_child(sh.expanded);
  if (sh.arrow) {
    if (sh.expanded) {
      sh.arrow->set_text("▾");
      sh.arrow->remove_css_class("section-arrow-collapsed");
    } else {
      sh.arrow->set_text("▸");
      sh.arrow->add_css_class("section-arrow-collapsed");
    }
  }
  if (m_on_disclosure_changed)
    m_on_disclosure_changed();
}

// ─────────────────────────────────────────────────────────────────────────────
// Session footer
// ─────────────────────────────────────────────────────────────────────────────

void Sidebar::build_session_footer() {
  auto *sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
  append(*sep);

  // ── Outer wrapper (holds header row + revealer) ───────────────────────────
  auto *wrapper = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  wrapper->add_css_class("session-tile-wrapper");
  wrapper->set_margin_start(8);
  wrapper->set_margin_end(8);
  wrapper->set_margin_top(6);
  wrapper->set_margin_bottom(6);

  // ── Header row: "SESSION" label | chevron (right) ────────────────────────
  auto *top = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
  top->set_cursor(Gdk::Cursor::create("pointer"));
  top->add_css_class("tile-header-row");

  auto *hdr = Gtk::make_managed<Gtk::Label>("SESSION");
  hdr->add_css_class("session-label");
  hdr->set_halign(Gtk::Align::START);
  hdr->set_hexpand(true);

  m_session_tile_arrow = Gtk::make_managed<Gtk::Label>("▾");
  m_session_tile_arrow->add_css_class("section-arrow");
  m_session_tile_arrow->set_margin_end(2);

  top->append(*hdr);
  top->append(*m_session_tile_arrow);
  wrapper->append(*top);

  // ── Revealer wraps the collapsible body ───────────────────────────────────
  m_session_tile_revealer = Gtk::make_managed<Gtk::Revealer>();
  m_session_tile_revealer->set_reveal_child(m_session_tile_expanded);
  m_session_tile_revealer->set_transition_type(
      Gtk::RevealerTransitionType::SLIDE_DOWN);
  m_session_tile_revealer->set_transition_duration(180);

  auto *card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  card->add_css_class("session-card");
  card->set_margin_top(6);

  // ── Words written (big number) ───────────────────────────────────────────
  auto *words_row =
      Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
  words_row->set_margin_top(4);

  m_session_words_lbl = Gtk::make_managed<Gtk::Label>("0");
  m_session_words_lbl->add_css_class("session-words");
  m_session_words_lbl->set_halign(Gtk::Align::START);
  m_session_words_lbl->set_hexpand(true);

  auto *words_unit = Gtk::make_managed<Gtk::Label>("words");
  words_unit->add_css_class("session-pct");
  words_unit->set_valign(Gtk::Align::END);
  words_unit->set_margin_bottom(3);

  // Reset button lives here, inside the body
  auto *reset_btn = Gtk::make_managed<Gtk::Button>();
  reset_btn->set_icon_name("view-refresh-symbolic");
  reset_btn->add_css_class("flat");
  reset_btn->set_tooltip_text("Reset session word count");
  reset_btn->set_valign(Gtk::Align::CENTER);
  reset_btn->signal_clicked().connect([this]() {
    m_model.session_words = 0;
    m_model.mark_modified();
    refresh_session();
  });

  words_row->append(*m_session_words_lbl);
  words_row->append(*words_unit);
  words_row->append(*reset_btn);
  card->append(*words_row);

  // ── Target line ──────────────────────────────────────────────────────────
  m_session_target_lbl = Gtk::make_managed<Gtk::Label>("Goal: 0 words");
  m_session_target_lbl->add_css_class("session-pct");
  m_session_target_lbl->set_halign(Gtk::Align::START);
  m_session_target_lbl->set_margin_top(1);
  card->append(*m_session_target_lbl);

  // ── Progress bar ─────────────────────────────────────────────────────────
  m_session_bar.set_min_value(0);
  m_session_bar.set_max_value(1);
  m_session_bar.set_value(0);
  m_session_bar.add_css_class("session-bar");
  m_session_bar.set_margin_top(8);
  card->append(m_session_bar);

  // ── Percentage ───────────────────────────────────────────────────────────
  m_session_pct_lbl = Gtk::make_managed<Gtk::Label>("0%");
  m_session_pct_lbl->add_css_class("session-pct");
  m_session_pct_lbl->set_halign(Gtk::Align::END);
  m_session_pct_lbl->set_margin_top(2);
  card->append(*m_session_pct_lbl);

  m_session_tile_revealer->set_child(*card);
  wrapper->append(*m_session_tile_revealer);
  append(*wrapper);

  // ── Disclosure click on header row ────────────────────────────────────────
  auto gc = Gtk::GestureClick::create();
  gc->set_button(1);
  gc->signal_pressed().connect([this](int, double, double) {
    m_session_tile_expanded = !m_session_tile_expanded;
    m_session_tile_revealer->set_reveal_child(m_session_tile_expanded);
    if (m_session_tile_arrow)
      m_session_tile_arrow->set_text(m_session_tile_expanded ? "▾" : "▸");
    if (m_on_disclosure_changed)
      m_on_disclosure_changed();
  });
  top->add_controller(gc);

  refresh_session();
}

void Sidebar::refresh_session() {
  if (!m_session_words_lbl)
    return;

  int words = m_model.session_words;
  int target = m_prefs.daily_word_goal;

  m_session_words_lbl->set_text(std::to_string(words));

  if (target > 0) {
    m_session_target_lbl->set_text("Goal: " + std::to_string(target) +
                                   " words");
    double frac = std::min(1.0, (double)words / target);
    m_session_bar.set_value(frac);
    int pct = (int)(frac * 100.0);
    m_session_pct_lbl->set_text(std::to_string(pct) + "%");
    m_session_bar.set_visible(true);
    m_session_pct_lbl->set_visible(true);
    m_session_target_lbl->set_visible(true);
  } else {
    m_session_target_lbl->set_visible(false);
    m_session_bar.set_visible(false);
    m_session_pct_lbl->set_visible(false);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Popup helper
// ─────────────────────────────────────────────────────────────────────────────

void Sidebar::popup_menu(Glib::RefPtr<Gio::Menu> menu,
                         Glib::RefPtr<Gio::SimpleActionGroup> ag,
                         Gtk::Widget *anchor, double x, double y) {
  if (m_ctx_popover) {
    m_ctx_popover->unparent();
    m_ctx_popover = nullptr;
  }

  double sx = x, sy = y;
  graphene_point_t src = GRAPHENE_POINT_INIT((float)x, (float)y);
  graphene_point_t dest = GRAPHENE_POINT_INIT(0.f, 0.f);
  if (gtk_widget_compute_point(anchor->gobj(), GTK_WIDGET(this->gobj()), &src,
                               &dest)) {
    sx = dest.x;
    sy = dest.y;
  }

  m_ctx_popover = Gtk::make_managed<Gtk::PopoverMenu>(menu);
  m_ctx_popover->insert_action_group("ctx", ag);
  m_ctx_popover->set_parent(*this);
  m_ctx_popover->set_has_arrow(false);
  Gdk::Rectangle r;
  r.set_x((int)sx);
  r.set_y((int)sy);
  r.set_width(1);
  r.set_height(1);
  m_ctx_popover->set_pointing_to(r);

  m_ctx_popover->signal_closed().connect([this]() {
    Glib::signal_idle().connect_once([this]() {
      if (m_ctx_popover) {
        m_ctx_popover->unparent();
        m_ctx_popover = nullptr;
      }
    });
  });

  m_ctx_popover->popup();
}

// ─────────────────────────────────────────────────────────────────────────────
// Template picker popover
// ─────────────────────────────────────────────────────────────────────────────

void Sidebar::show_template_picker(
    Gtk::Widget *anchor, std::function<void(const BinderNode &)> on_chosen,
    const std::string &category) {
  // Collect available templates: doc first, then globals. When `category` is set
  // (the instance path), filter to template nodes of that category and skip
  // globals (cross-project boilerplate — reconciled later).
  struct Entry {
    std::string label;
    BinderNode node;
  };
  std::vector<Entry> entries;

  auto node_category = [](const BinderNode &n) -> std::string {
    return n.form_schema.is_object() ? n.form_schema.value("category", std::string{})
                                     : std::string{};
  };

  std::function<void(const std::vector<BinderNode> &, bool)> collect;
  collect = [&](const std::vector<BinderNode> &nodes, bool is_global) {
    for (const auto &n : nodes) {
      if (n.kind == BinderKind::Template) {
        if (category.empty() || node_category(n) == category) {
          std::string lbl = n.title.empty() ? "Untitled" : n.title;
          if (is_global)
            lbl = "[Global]  " + lbl;
          entries.push_back({lbl, n});
        }
      }
      if (!n.children.empty())
        collect(n.children, is_global);
    }
  };
  collect(m_model.root(Section::Templates), false);
  if (category.empty()) {                       // globals only in the legacy path
    auto globals = m_prefs.global_templates_get();
    collect(globals, true);
  }

  if (entries.empty())
    return; // nothing to show

  // Build a popover with a scrollable ListBox
  auto *pop = Gtk::make_managed<Gtk::Popover>();
  pop->set_has_arrow(true);
  pop->set_parent(*anchor);

  auto *outer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  outer->set_margin_top(6);
  outer->set_margin_bottom(6);

  // Header label
  auto *hdr = Gtk::make_managed<Gtk::Label>("Apply Template");
  hdr->add_css_class("pref-group-title");
  hdr->set_halign(Gtk::Align::START);
  hdr->set_margin_start(10);
  hdr->set_margin_end(10);
  hdr->set_margin_bottom(4);
  outer->append(*hdr);

  auto *scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
  scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  scroll->set_max_content_height(240);
  scroll->set_propagate_natural_height(true);

  auto *lb = Gtk::make_managed<Gtk::ListBox>();
  lb->set_selection_mode(Gtk::SelectionMode::NONE);
  lb->add_css_class("boxed-list");

  // Capture entries by value for the lambda
  for (int i = 0; i < (int)entries.size(); ++i) {
    auto *row = Gtk::make_managed<Gtk::ListBoxRow>();
    auto *lbl = Gtk::make_managed<Gtk::Label>(entries[i].label);
    lbl->set_halign(Gtk::Align::START);
    lbl->set_margin_start(12);
    lbl->set_margin_end(12);
    lbl->set_margin_top(8);
    lbl->set_margin_bottom(8);
    row->set_child(*lbl);
    lb->append(*row);
  }

  lb->signal_row_activated().connect(
      [pop, entries, on_chosen](Gtk::ListBoxRow *row) {
        int idx = row->get_index();
        if (idx >= 0 && idx < (int)entries.size()) {
          pop->popdown();
          on_chosen(entries[idx].node);
        }
      });

  scroll->set_child(*lb);
  outer->append(*scroll);
  pop->set_child(*outer);
  pop->popup();
}

// ─────────────────────────────────────────────────────────────────────────────
// Context menus — section header level
// ─────────────────────────────────────────────────────────────────────────────

void Sidebar::show_section_ctx_menu(Section section, double x, double y,
                                    Gtk::Widget *anchor) {
  // Trash has no "Add" action — stub for Stage 3 (Empty Trash)
  if (section == Section::Trash) {
    auto gm = Gio::Menu::create();
    auto sec = Gio::Menu::create();
    auto ag = Gio::SimpleActionGroup::create();
    sec->append_item(Gio::MenuItem::create("Empty Trash", "ctx.empty-trash"));
    ag->add_action("empty-trash", [this]() {
      auto *win = dynamic_cast<Gtk::Window *>(get_root());
      if (!win)
        return;
      if (m_model.root(Section::Trash).empty())
        return;
      auto dlg = Gtk::AlertDialog::create("Empty Trash?");
      dlg->set_detail("All " + std::to_string(m_model.root(Section::Trash).size()) +
                      " items in Trash will be permanently deleted. This "
                      "cannot be undone.");
      dlg->set_modal(true);
      dlg->set_buttons({"Cancel", "Empty Trash"});
      dlg->set_cancel_button(0);
      dlg->set_default_button(0);
      dlg->choose(*win,
                  [this, dlg](Glib::RefPtr<Gio::AsyncResult> &res) mutable {
                    int resp = 0;
                    try {
                      resp = dlg->choose_finish(res);
                    } catch (...) {
                    }
                    if (resp == 1) {
                      m_model.empty_trash();
                      rebuild_section(Section::Trash);
                    }
                  });
    });
    gm->append_section({}, sec);
    popup_menu(gm, ag, anchor, x, y);
    return;
  }

  const char *leaf_label =
      section == Section::Manuscript   ? "Add Scene at Top Level"
      : section == Section::Characters ? "Add Character at Top Level"
      : section == Section::Places     ? "Add Place at Top Level"
      : section == Section::References ? "Add Reference at Top Level"
                                       : "Add Template at Top Level";
  const char *leaf_accel = section == Section::Manuscript   ? "<Ctrl><Alt>s"
                           : section == Section::Characters ? "<Ctrl><Alt>c"
                           : section == Section::Places     ? "<Ctrl><Alt>p"
                                                            : "";
  const char *group_accel =
      section == Section::Manuscript   ? "<Ctrl><Alt><Shift>s"
      : section == Section::Characters ? "<Ctrl><Alt><Shift>c"
      : section == Section::Places     ? "<Ctrl><Alt><Shift>p"
                                       : "";

  auto mi = [](const Glib::ustring &label, const Glib::ustring &action,
               const Glib::ustring &accel) {
    auto item = Gio::MenuItem::create(label, action);
    item->set_attribute_value("accel",
                              Glib::Variant<Glib::ustring>::create(accel));
    return item;
  };

  auto gm = Gio::Menu::create();
  auto sec = Gio::Menu::create();
  auto ag = Gio::SimpleActionGroup::create();
  sec->append_item(mi("Add Group at Top Level", "ctx.add-group", group_accel));
  sec->append_item(mi(leaf_label, "ctx.add-leaf", leaf_accel));
  ag->add_action("add-group", [this, section] { on_add_group(section, {}); });
  ag->add_action("add-leaf", [this, section] { on_add_leaf(section, {}); });
  // s43 — "New Form": the front door. Create the template node AND drop straight
  // into the builder, named and ready — one verb, not the old two-step (add bare
  // node → right-click → Edit Form…).
  if (section == Section::Templates) {
    sec->append_item(mi("New Form\xE2\x80\xA6", "ctx.tpl-new-form", ""));
    ag->add_action("tpl-new-form", [this]() {
      if (m_ctx_popover)
        m_ctx_popover->popdown();
      // Off the live popover handler: create the node, rebuild, then open the
      // builder for the fresh node's iid (its empty form_schema is seeded with
      // the Character floor by open_template_builder_for_template_node).
      Glib::signal_idle().connect_once([this]() {
        auto new_path = m_model.add_leaf(Section::Templates, {}, "");
        BinderNode *n = m_model.node_at(Section::Templates, new_path);
        if (!n)
          return;
        const std::string iid = n->iid;   // capture before any rebuild
        m_model.mark_modified();
        rebuild_section(Section::Templates);
        if (m_on_edit_template)
          m_on_edit_template(iid);
      });
    });
  }
  gm->append_section({}, sec);

  // "New from Template…" — only shown when templates exist
  bool has_templates =
      !m_model.root(Section::Templates).empty() || !m_prefs.global_templates_json.empty();
  if (has_templates) {
    auto tpl_sec = Gio::Menu::create();
    tpl_sec->append_item(
        mi("New from Template\xE2\x80\xA6", "ctx.new-from-template", ""));
    ag->add_action("new-from-template", [this, section, anchor]() {
      // Dismiss the ctx popover first, then show picker
      if (m_ctx_popover)
        m_ctx_popover->popdown();
      Glib::signal_idle().connect_once([this, section, anchor]() {
        const std::string cat =
            section == Section::Characters ? "character"
          : section == Section::Places     ? "place"
          : section == Section::References ? "reference"
                                           : std::string{};   // Manuscript etc → legacy
        show_template_picker(anchor, [this, section](const BinderNode &tpl) {
          auto new_path = m_model.add_leaf(section, {}, "");
          BinderNode *n = m_model.node_at(section, new_path);
          if (n) {
            if (tpl.form_schema.is_object() && !tpl.form_schema.empty()) {
              // slice3 — a FORM template: the new leaf adopts it by id (the
              // template node's iid). rebuild_object_store instantiates the
              // object against it; the author titles the instance.
              n->template_id = tpl.iid;
            } else {
              // schema-less BOILERPLATE template: copy the starting content (the
              // buffer-only case, preserved from the legacy "New from Template").
              n->content = tpl.content;
              n->title = tpl.title;
              n->color_idx = tpl.color_idx;
              n->status = tpl.status;
              if (tpl.word_target > 0)
                n->word_target = tpl.word_target;
            }
            m_model.mark_modified();
          }
          m_model.set_active(section, new_path);
          rebuild_section(section);
          m_board_selection = {SelPath::make(section, new_path)};
          refresh_all_highlights();
          fire_board_selection();
          if (m_on_selected)
            m_on_selected(section, new_path);
        }, cat);
      });
    });
    gm->append_section({}, tpl_sec);
  }

  popup_menu(gm, ag, anchor, x, y);
}

// ─────────────────────────────────────────────────────────────────────────────
// Context menu — node level
// ─────────────────────────────────────────────────────────────────────────────

void Sidebar::show_node_ctx_menu(Section section, const std::vector<int> &path,
                                 double x, double y, Gtk::Widget *anchor) {
  const BinderNode *node = m_model.node_at(section, path);
  if (!node)
    return;

  bool is_group = binder_kind_is_group(node->kind);
  bool multi = m_board_selection.size() > 1;

  auto gm = Gio::Menu::create();
  auto ag = Gio::SimpleActionGroup::create();

  auto mi = [](const Glib::ustring &label, const Glib::ustring &action,
               const Glib::ustring &accel = "") {
    auto item = Gio::MenuItem::create(label, action);
    if (!accel.empty())
      item->set_attribute_value("accel",
                                Glib::Variant<Glib::ustring>::create(accel));
    return item;
  };

  if (!multi) {
    // ── Single-item menu ──────────────────────────────────────────────────

    if (section == Section::Trash) {
      // Trash item: Restore to a section or Delete Permanently
      int trash_idx = path.empty() ? -1 : path[0];
      auto restore_sec = Gio::Menu::create();
      restore_sec->append_item(
          mi("Restore to Manuscript", "ctx.restore-manuscript"));
      restore_sec->append_item(
          mi("Restore to Characters", "ctx.restore-characters"));
      restore_sec->append_item(mi("Restore to Places", "ctx.restore-places"));
      restore_sec->append_item(
          mi("Restore to References", "ctx.restore-references"));
      restore_sec->append_item(
          mi("Restore to Templates", "ctx.restore-templates"));
      auto rebuild_both = [this](Section target) {
        rebuild_section(Section::Trash);
        rebuild_section(target);
      };
      ag->add_action("restore-manuscript", [this, trash_idx, rebuild_both] {
        m_model.restore_node(trash_idx, Section::Manuscript);
        rebuild_both(Section::Manuscript);
      });
      ag->add_action("restore-characters", [this, trash_idx, rebuild_both] {
        m_model.restore_node(trash_idx, Section::Characters);
        rebuild_both(Section::Characters);
      });
      ag->add_action("restore-places", [this, trash_idx, rebuild_both] {
        m_model.restore_node(trash_idx, Section::Places);
        rebuild_both(Section::Places);
      });
      ag->add_action("restore-references", [this, trash_idx, rebuild_both] {
        m_model.restore_node(trash_idx, Section::References);
        rebuild_both(Section::References);
      });
      ag->add_action("restore-templates", [this, trash_idx, rebuild_both] {
        m_model.restore_node(trash_idx, Section::Templates);
        rebuild_both(Section::Templates);
      });
      gm->append_section({}, restore_sec);

      auto del_sec = Gio::Menu::create();
      del_sec->append_item(mi("Delete Permanently", "ctx.remove"));
      ag->add_action("remove",
                     [this, section, path] { on_remove_node(section, path); });
      gm->append_section({}, del_sec);

      popup_menu(gm, ag, anchor, x, y);
      return;
    }

    const char *leaf_label =
        section == Section::Characters   ? "Add Character Inside"
        : section == Section::Places     ? "Add Place Inside"
        : section == Section::References ? "Add Reference Inside"
        : section == Section::Templates  ? "Add Template Inside"
                                         : "Add Scene Inside";

    // Section 1: open + add children
    auto open_sec = Gio::Menu::create();
    open_sec->append_item(mi("Open in Editor", "ctx.open"));
    ag->add_action("open", [this, section, path] {
      if (m_on_opened)
        m_on_opened(section, path);
    });
    if (is_group) {
      const char *leaf_accel = section == Section::Characters ? "<Ctrl><Alt>c"
                               : section == Section::Places   ? "<Ctrl><Alt>p"
                                                              : "<Ctrl><Alt>s";
      const char *group_accel =
          section == Section::Characters ? "<Ctrl><Alt><Shift>c"
          : section == Section::Places   ? "<Ctrl><Alt><Shift>p"
                                         : "<Ctrl><Alt><Shift>s";
      open_sec->append_item(mi(leaf_label, "ctx.add-leaf", leaf_accel));
      open_sec->append_item(mi("Add Sub-Group", "ctx.add-group", group_accel));
      ag->add_action("add-leaf",
                     [this, section, path] { on_add_leaf(section, path); });
      ag->add_action("add-group",
                     [this, section, path] { on_add_group(section, path); });
    }
    gm->append_section({}, open_sec);

    // Section 2 (Templates only): Edit Form + Make Global
    if (section == Section::Templates && !is_group) {
      auto tpl_sec = Gio::Menu::create();
      tpl_sec->append_item(mi("Edit Form\xE2\x80\xA6", "ctx.tpl-edit-form"));
      ag->add_action("tpl-edit-form", [this, section, path]() {
        BinderNode *n = m_model.node_at(section, path);
        if (n && m_on_edit_template)
          m_on_edit_template(n->iid);   // → Inspector::open_template_builder_for_template_node
      });
      tpl_sec->append_item(mi("Make Global", "ctx.tpl-make-global"));
      ag->add_action("tpl-make-global", [this, section, path]() {
        BinderNode *n = m_model.node_at(section, path);
        if (!n)
          return;
        BinderNode copy = *n;
        copy.trash_origin_section.clear();
        copy.trash_origin_path_str.clear();
        auto globals = m_prefs.global_templates_get();
        globals.push_back(std::move(copy));
        m_prefs.global_templates_set(globals);
        try {
          m_prefs.save();
        } catch (...) {
        }
        rebuild_section(Section::Templates);
      });
      gm->append_section({}, tpl_sec);
    }

    // Section 3: move to trash
    const char *remove_label =
        is_group                         ? "Move Group to Trash"
        : section == Section::Characters ? "Move Character to Trash"
        : section == Section::Places     ? "Move Place to Trash"
        : section == Section::References ? "Move Reference to Trash"
        : section == Section::Templates  ? "Move Template to Trash"
                                         : "Move Scene to Trash";

    // Section 2b: split (Manuscript scenes only)
    if (section == Section::Manuscript && !is_group && m_on_split_node) {
      auto split_sec = Gio::Menu::create();
      split_sec->append_item(mi("Split on Separator", "ctx.split-sep"));
      ag->add_action("split-sep", [this, section, path] {
        if (m_on_split_node)
          m_on_split_node(section, path);
      });
      gm->append_section({}, split_sec);
    }

    auto del_sec = Gio::Menu::create();
    del_sec->append_item(mi(remove_label, "ctx.remove"));
    ag->add_action("remove",
                   [this, section, path] { on_remove_node(section, path); });
    gm->append_section({}, del_sec);

  } else {
    // ── Multi-item menu ───────────────────────────────────────────────────
    int n = (int)m_board_selection.size();

    // Open all selected items in editor
    auto open_sec = Gio::Menu::create();
    open_sec->append_item(Gio::MenuItem::create(
        "Open " + std::to_string(n) + " Items in Editor", "ctx.open-all"));
    ag->add_action("open-all", [this] {
      if (!m_on_opened)
        return;
      for (const auto &item : m_board_selection)
        m_on_opened(item.section, item.path);
      m_board_selection.clear();
      refresh_all_highlights();
      fire_board_selection();
    });
    gm->append_section({}, open_sec);

    // Combine + Split (Manuscript scenes only — all selected must be non-group)
    if (section == Section::Manuscript && (m_on_combine || m_on_split_node)) {
      bool all_scenes = true;
      for (const auto &item : m_board_selection) {
        const BinderNode *nd = m_model.node_at(item.section, item.path);
        if (!nd || binder_kind_is_group(nd->kind)) {
          all_scenes = false;
          break;
        }
      }
      if (all_scenes) {
        auto ops_sec = Gio::Menu::create();

        if (m_on_split_node) {
          ops_sec->append_item(Gio::MenuItem::create(
              "Split " + std::to_string(n) + " Scenes on Separator",
              "ctx.split-multi"));
          ag->add_action("split-multi", [this, section] {
            if (!m_on_split_node)
              return;
            // Collect paths in reverse document order so earlier
            // splits don't shift the indices of later ones
            std::vector<std::vector<int>> paths;
            for (const auto &item : m_board_selection)
              if (item.section == section)
                paths.push_back(item.path);
            std::sort(paths.rbegin(), paths.rend());
            for (auto &p : paths)
              m_on_split_node(section, p);
          });
        }

        if (m_on_combine) {
          ops_sec->append_item(Gio::MenuItem::create(
              "Combine " + std::to_string(n) + " Scenes", "ctx.combine"));
          ag->add_action("combine", [this, section] {
            if (!m_on_combine)
              return;
            std::vector<std::vector<int>> paths;
            for (const auto &item : m_board_selection)
              if (item.section == section)
                paths.push_back(item.path);
            std::sort(paths.begin(), paths.end());
            m_on_combine(section, paths);
          });
        }

        gm->append_section({}, ops_sec);
      }
    }

    // Remove all selected items
    auto del_sec = Gio::Menu::create();
    std::string del_label =
        (section == Section::Trash)
            ? "Delete " + std::to_string(n) + " Items Permanently"
            : "Move " + std::to_string(n) + " Items to Trash";
    del_sec->append_item(
        Gio::MenuItem::create(del_label, "ctx.remove-selected"));
    ag->add_action("remove-selected",
                   [this, section] { on_remove_selected(section); });

    // Trash section also gets a Restore submenu for multi-select
    if (section == Section::Trash) {
      auto restore_sub = Gio::Menu::create();
      auto add_restore = [&](const char *label, const char *action,
                             Section target) {
        restore_sub->append_item(Gio::MenuItem::create(label, action));
        ag->add_action(action, [this, target]() {
          // Collect trash indices from selection, sort descending so
          // restoring high indices first doesn't shift lower ones
          std::vector<int> idxs;
          for (const auto &item : m_board_selection)
            if (!item.path.empty())
              idxs.push_back(item.path[0]);
          std::sort(idxs.rbegin(), idxs.rend());
          for (int i : idxs)
            m_model.restore_node(i, target);
          m_board_selection.clear();
          rebuild_section(Section::Trash);
          rebuild_section(target);
        });
      };
      add_restore("Restore Selected to Manuscript", "ctx.restore-ms",
                  Section::Manuscript);
      add_restore("Restore Selected to Characters", "ctx.restore-ch",
                  Section::Characters);
      add_restore("Restore Selected to Places", "ctx.restore-pl",
                  Section::Places);
      add_restore("Restore Selected to References", "ctx.restore-ref",
                  Section::References);
      add_restore("Restore Selected to Templates", "ctx.restore-tpl",
                  Section::Templates);
      del_sec->append_section({}, restore_sub);
    }

    gm->append_section({}, del_sec);
  }

  popup_menu(gm, ag, anchor, x, y);
}

// ─────────────────────────────────────────────────────────────────────────────
// rebuild — public entry point
// ─────────────────────────────────────────────────────────────────────────────

void Sidebar::apply_disclosure_state(bool manuscript, bool characters,
                                     bool places, bool pomo_tile,
                                     bool session_tile, bool references,
                                     bool templates, bool trash) {
  // Section headers
  auto apply_section = [this](Section s, bool expanded) {
    auto &sh = section_header(s);
    if (sh.expanded == expanded)
      return;
    sh.expanded = expanded;
    if (sh.revealer)
      sh.revealer->set_reveal_child(expanded);
    if (sh.arrow) {
      sh.arrow->set_text(expanded ? "▾" : "▸");
      if (expanded)
        sh.arrow->remove_css_class("section-arrow-collapsed");
      else
        sh.arrow->add_css_class("section-arrow-collapsed");
    }
  };
  apply_section(Section::Manuscript, manuscript);
  apply_section(Section::Characters, characters);
  apply_section(Section::Places, places);
  apply_section(Section::References, references);
  apply_section(Section::Templates, templates);
  apply_section(Section::Trash, trash);

  // Pomodoro tile
  if (m_pomo_tile_expanded != pomo_tile) {
    m_pomo_tile_expanded = pomo_tile;
    if (m_pomo_tile_revealer)
      m_pomo_tile_revealer->set_reveal_child(pomo_tile);
    if (m_pomo_tile_arrow)
      m_pomo_tile_arrow->set_text(pomo_tile ? "▾" : "▸");
  }

  // Session tile
  if (m_session_tile_expanded != session_tile) {
    m_session_tile_expanded = session_tile;
    if (m_session_tile_revealer)
      m_session_tile_revealer->set_reveal_child(session_tile);
    if (m_session_tile_arrow)
      m_session_tile_arrow->set_text(session_tile ? "▾" : "▸");
  }
}

void Sidebar::rebuild() {
  rebuild_section(Section::Manuscript);
  rebuild_section(Section::Characters);
  rebuild_section(Section::Places);
  rebuild_section(Section::References);
  rebuild_section(Section::Templates);
  rebuild_section(Section::Trash);
}

void Sidebar::rebuild_section(Section section) {
  Gtk::Box *box = section == Section::Manuscript   ? m_manuscript_box
                  : section == Section::Characters ? m_characters_box
                  : section == Section::Places     ? m_places_box
                  : section == Section::References ? m_references_box
                  : section == Section::Templates  ? m_templates_box
                  : section == Section::Trash      ? m_trash_box
                                                   : nullptr;
  if (!box)
    return;

  // Clear rows registered for this section
  m_row_entries.erase(std::remove_if(m_row_entries.begin(), m_row_entries.end(),
                                     [section](const RowEntry &e) {
                                       return e.section == section;
                                     }),
                      m_row_entries.end());
  m_collapse_entries.erase(std::remove_if(m_collapse_entries.begin(),
                                          m_collapse_entries.end(),
                                          [section](const CollapseEntry &e) {
                                            return e.section == section;
                                          }),
                           m_collapse_entries.end());

  // Remove all child widgets
  while (auto *child = box->get_first_child())
    box->remove(*child);

  const auto &tree = m_model.root(section);
  for (int i = 0; i < (int)tree.size(); ++i)
    add_node_recursive(section, {i}, 0, box);

  // For Templates section, also render app-wide (global) templates
  if (section == Section::Templates) {
    auto globals = m_prefs.global_templates_get();
    if (!globals.empty()) {
      // Separator + "App-Wide" label
      auto *sep =
          Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
      sep->add_css_class("binder-section-sep");
      sep->set_margin_top(4);
      sep->set_margin_bottom(2);
      box->append(*sep);

      auto *glbl = Gtk::make_managed<Gtk::Label>("App-Wide");
      glbl->add_css_class("sidebar-section-label");
      glbl->set_halign(Gtk::Align::START);
      glbl->set_margin_start(8);
      glbl->set_margin_bottom(2);
      box->append(*glbl);

      for (int i = 0; i < (int)globals.size(); ++i)
        add_global_template_row(i, box);
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Global template rows (app-wide templates in Templates section)
// ─────────────────────────────────────────────────────────────────────────────

void Sidebar::add_global_template_row(int global_idx, Gtk::Box *parent_box) {
  auto globals = m_prefs.global_templates_get();
  if (global_idx < 0 || global_idx >= (int)globals.size())
    return;
  const BinderNode &n = globals[global_idx];

  auto *row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
  row->add_css_class("binder-row");
  row->set_margin_start(8);
  row->set_margin_end(8);
  row->set_margin_top(1);
  row->set_margin_bottom(1);

  // Symbolic globe icon badge (matches sidebar icon style)
  auto *globe = Gtk::make_managed<Gtk::Image>();
  globe->set_from_icon_name("org.gnome.Epiphany-symbolic");
  globe->set_pixel_size(12);
  globe->set_valign(Gtk::Align::CENTER);
  globe->set_tooltip_text("App-wide template");
  globe->add_css_class("global-tpl-badge");
  row->append(*globe);

  // Title
  auto *tbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  tbox->set_hexpand(true);
  tbox->set_valign(Gtk::Align::CENTER);
  auto *tlbl =
      Gtk::make_managed<Gtk::Label>(n.title.empty() ? "Untitled" : n.title);
  tlbl->add_css_class("row-title");
  tlbl->set_halign(Gtk::Align::START);
  tlbl->set_ellipsize(Pango::EllipsizeMode::END);
  tbox->append(*tlbl);
  row->append(*tbox);

  parent_box->append(*row);

  // Right-click: Copy to Document / Remove from App-Wide
  auto gc = Gtk::GestureClick::create();
  gc->set_button(3);
  gc->signal_pressed().connect(
      [this, global_idx, row](int, double x, double y) {
        auto gm = Gio::Menu::create();
        auto ag = Gio::SimpleActionGroup::create();

        auto sec = Gio::Menu::create();
        sec->append_item(
            Gio::MenuItem::create("Copy to Document", "ctx.copy-to-doc"));
        ag->add_action("copy-to-doc", [this, global_idx]() {
          auto globals = m_prefs.global_templates_get();
          if (global_idx >= (int)globals.size())
            return;
          BinderNode copy = globals[global_idx];
          m_model.root(Section::Templates).push_back(std::move(copy));
          m_model.mark_modified();
          rebuild_section(Section::Templates);
        });
        gm->append_section({}, sec);

        auto del_sec = Gio::Menu::create();
        del_sec->append_item(
            Gio::MenuItem::create("Remove from App-Wide", "ctx.remove-global"));
        ag->add_action("remove-global", [this, global_idx]() {
          auto globals = m_prefs.global_templates_get();
          if (global_idx >= (int)globals.size())
            return;
          globals.erase(globals.begin() + global_idx);
          m_prefs.global_templates_set(globals);
          try {
            m_prefs.save();
          } catch (...) {
          }
          rebuild_section(Section::Templates);
        });
        gm->append_section({}, del_sec);

        popup_menu(gm, ag, row, x, y);
      });
  row->add_controller(gc);
}
// ─────────────────────────────────────────────────────────────────────────────

// Build a status indicator for a group header showing the group's own status.
// Renders as a single 10x10 square matching the leaf status dot style.
// Returns nullptr if the group has no status set.
static Gtk::Box *make_status_indicator(const BinderNode &group,
                                       const FolioPrefs &prefs) {
  if (group.status == NodeStatus::Untitled)
    return nullptr;

  std::string status_name = node_status_label(group.status);
  std::string hex = prefs.status_color_for_name(status_name);

  auto *row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
  row->set_valign(Gtk::Align::CENTER);

  auto *sq = Gtk::make_managed<Gtk::Box>();
  sq->set_size_request(10, 10);
  sq->set_valign(Gtk::Align::CENTER);
  sq->set_tooltip_text(status_name);
  auto prov = Gtk::CssProvider::create();
  prov->load_from_data(
      "box { background-color: " + hex +
      ";"
      " border-radius: 0; min-width: 10px; min-height: 10px; }");
  sq->get_style_context()->add_provider(
      prov, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  row->append(*sq);

  return row;
}

// ─────────────────────────────────────────────────────────────────────────────
// add_node_recursive — builds one row (Group or leaf) and recurses
// ─────────────────────────────────────────────────────────────────────────────

void Sidebar::add_node_recursive(Section section, const std::vector<int> &path,
                                 int depth, Gtk::Box *parent_box) {
  const BinderNode *node = m_model.node_at(section, path);
  if (!node)
    return;

  // Search filter
  if (!m_search_filter.empty()) {
    bool ok = matches_filter(m_search_filter, node->title) ||
              matches_filter(m_search_filter, node->synopsis) ||
              matches_filter(m_search_filter, node->description) ||
              matches_filter_html(m_search_filter, node->content);
    if (!ok && binder_kind_is_group(node->kind)) {
      std::function<bool(const BinderNode &)> any_match =
          [&](const BinderNode &n) -> bool {
        if (matches_filter(m_search_filter, n.title))
          return true;
        if (matches_filter(m_search_filter, n.synopsis))
          return true;
        if (matches_filter(m_search_filter, n.description))
          return true;
        if (matches_filter_html(m_search_filter, n.content))
          return true;
        for (const auto &c : n.children)
          if (any_match(c))
            return true;
        return false;
      };
      ok = any_match(*node);
    }
    if (!ok)
      return;
  }

  const int INDENT = 6;
  const int BASE = 4;

  if (binder_kind_is_group(node->kind)) {
    // ── Group card ────────────────────────────────────────────────────────
    auto *card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
    card->set_margin_start(BASE + depth * INDENT);
    card->set_margin_end(0);
    card->set_margin_bottom(0);

    auto *hdr = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    hdr->add_css_class("part-header-btn");
    hdr->set_hexpand(true);
    hdr->set_focusable(true);
    hdr->set_cursor(Gdk::Cursor::create("pointer"));
    hdr->set_margin_start(0);
    hdr->set_margin_end(10);
    hdr->set_margin_top(4);
    hdr->set_margin_bottom(4);
    hdr->set_tooltip_text("Click to select · Double-click to open · Alt-click "
                          "to expand/collapse · Right-click for options");

    std::string icon_hex = m_prefs.color_hex_for_idx(node->color_idx);
    // Curvz folio-group-symbolic (gresource). Symbolic icons honor the CSS
    // `color` property, so the per-node color_idx tint still applies — same
    // mechanism as the old "⊞" label, just `image {}` instead of `label {}`.
    auto *icon = Gtk::make_managed<Gtk::Image>();
    icon->set_from_icon_name("folio-group-symbolic");
    icon->set_icon_size(Gtk::IconSize::NORMAL);
    icon->add_css_class("scene-icon");
    icon->set_size_request(28, 28);
    if (!icon_hex.empty()) {
      auto prov = Gtk::CssProvider::create();
      prov->load_from_data("image { color: " + icon_hex + "; }");
      icon->get_style_context()->add_provider(
          prov, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

    // Label-colour swatch — a small rounded pill to the right of the arrow
    Gtk::Box *swatch = nullptr;
    if (!icon_hex.empty()) {
      swatch = Gtk::make_managed<Gtk::Box>();
      swatch->set_size_request(10, 10);
      swatch->set_valign(Gtk::Align::CENTER);
      swatch->set_margin_start(8);
      swatch->set_margin_end(4);
      auto prov = Gtk::CssProvider::create();
      prov->load_from_data(
          "box { background-color: " + icon_hex +
          ";"
          " border-radius: 9999px; min-width: 10px; min-height: 10px; }");
      swatch->get_style_context()->add_provider(
          prov, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

    auto *tbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 1);
    auto *tlbl = Gtk::make_managed<Gtk::Label>(
        node->title.empty() ? "Untitled" : node->title);
    tlbl->add_css_class("row-title");
    tlbl->set_halign(Gtk::Align::START);
    tlbl->set_ellipsize(Pango::EllipsizeMode::END);
    auto *slbl = Gtk::make_managed<Gtk::Label>(
        std::to_string(node->children.size()) + " items · " +
        std::to_string(node->total_words()) + "w");
    slbl->add_css_class("row-subtitle");
    slbl->set_halign(Gtk::Align::START);
    tbox->append(*tlbl);
    tbox->append(*slbl);
    tbox->set_hexpand(true);
    tbox->set_valign(Gtk::Align::CENTER);

    auto *arrow = Gtk::make_managed<Gtk::Label>("▾");
    arrow->add_css_class("part-arrow");
    arrow->add_css_class("expanded");

    // Status indicator — dot summary of all descendant leaf statuses, right
    // side
    Gtk::Box *status_ind = make_status_indicator(*node, m_prefs);

    hdr->append(*icon);
    hdr->append(*tbox);
    hdr->append(*arrow);
    if (swatch)
      hdr->append(*swatch);
    if (status_ind) {
      status_ind->set_margin_start(6);
      hdr->append(*status_ind);
    }
    card->append(*hdr);

    auto *revealer = Gtk::make_managed<Gtk::Revealer>();
    revealer->set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);
    revealer->set_transition_duration(200);
    revealer->set_reveal_child(true);

    auto *inner = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
    for (int i = 0; i < (int)node->children.size(); ++i) {
      std::vector<int> child_path = path;
      child_path.push_back(i);
      add_node_recursive(section, child_path, depth + 1, inner);
    }
    revealer->set_child(*inner);
    card->append(*revealer);
    parent_box->append(*card);

    m_collapse_entries.push_back({section, path, revealer, arrow, true});
    m_row_entries.push_back({section, path, hdr});

    // Left-click: select · Double-click: open in timeline · Alt-click:
    // expand/collapse
    auto gc_left = Gtk::GestureClick::create();
    gc_left->set_button(1);
    gc_left->signal_pressed().connect([this, gc_left, section,
                                       path](int np, double, double) {
      grab_focus();

      Gdk::ModifierType mods = gc_left->get_current_event_state();
      bool alt   = (mods & Gdk::ModifierType::ALT_MASK)     != Gdk::ModifierType{};
      bool ctrl  = (mods & Gdk::ModifierType::CONTROL_MASK) != Gdk::ModifierType{};
      bool shift = (mods & Gdk::ModifierType::SHIFT_MASK)   != Gdk::ModifierType{};

      // Alt-click: toggle expand/collapse only — never touches selection
      if (alt) {
        for (int i = 0; i < (int)m_collapse_entries.size(); ++i) {
          if (m_collapse_entries[i].section == section &&
              m_collapse_entries[i].path == path) {
            toggle_node(i);
            break;
          }
        }
        return;
      }

      auto item     = SelPath::make(section, path);
      bool is_closed = !is_node_expanded(section, path);
      BinderNode *node = m_model.node_at(section, path);
      bool selected  = m_board_selection.count(item) > 0;

      // Double-click: select + open
      if (np == 2) {
        m_board_selection.clear();
        if (is_closed && node)
          collect_group_and_descendants(node, section, path, m_board_selection);
        else
          m_board_selection = {item};
        m_selection_anchor = item;
        refresh_all_highlights();
        fire_board_selection();
        if (m_on_opened) m_on_opened(section, path);
        return;
      }

      // Shift+click: range from anchor to here, replaces selection
      if (shift) {
        if (m_selection_anchor.path.empty()) {
          // No anchor — treat as plain click
          m_board_selection.clear();
          if (is_closed && node)
            collect_group_and_descendants(node, section, path, m_board_selection);
          else
            m_board_selection = {item};
          m_selection_anchor = item;
        } else {
          int anchor_idx = -1, clicked_idx = -1;
          for (int i = 0; i < (int)m_row_entries.size(); ++i) {
            if (m_row_entries[i].section == m_selection_anchor.section &&
                m_row_entries[i].path   == m_selection_anchor.path)
              anchor_idx = i;
            if (m_row_entries[i].section == section &&
                m_row_entries[i].path   == path)
              clicked_idx = i;
          }
          m_board_selection.clear();
          if (anchor_idx >= 0 && clicked_idx >= 0) {
            int lo = std::min(anchor_idx, clicked_idx);
            int hi = std::max(anchor_idx, clicked_idx);
            for (int i = lo; i <= hi; ++i)
              m_board_selection.insert(
                  SelPath::make(m_row_entries[i].section, m_row_entries[i].path));
          } else {
            // Anchor not in list — fall back to plain click
            if (is_closed && node)
              collect_group_and_descendants(node, section, path, m_board_selection);
            else
              m_board_selection = {item};
            m_selection_anchor = item;
          }
        }
        refresh_all_highlights();
        fire_board_selection();
        return;
      }

      // Ctrl+click: add or remove
      if (ctrl) {
        if (is_closed && node) {
          if (selected) {
            std::set<SelPath> to_remove;
            collect_group_and_descendants(node, section, path, to_remove);
            for (const auto &bi : to_remove) m_board_selection.erase(bi);
          } else {
            collect_group_and_descendants(node, section, path, m_board_selection);
            m_selection_anchor = item;
          }
        } else {
          if (selected)
            m_board_selection.erase(item);
          else {
            m_board_selection.insert(item);
            m_selection_anchor = item;
          }
        }
        refresh_all_highlights();
        fire_board_selection();
        return;
      }

      // Plain click
      if (!selected) {
        m_board_selection.clear();
        if (is_closed && node)
          collect_group_and_descendants(node, section, path, m_board_selection);
        else
          m_board_selection = {item};
        m_selection_anchor = item;
      } else if (m_board_selection.size() == 1) {
        // Sole selected — stay selected, no change
        return;
      } else {
        // In multi-selection — collapse to just this item
        m_board_selection.clear();
        if (is_closed && node)
          collect_group_and_descendants(node, section, path, m_board_selection);
        else
          m_board_selection = {item};
        m_selection_anchor = item;
      }
      refresh_all_highlights();
      fire_board_selection();
    });
    gc_left->signal_released().connect(
        [this](int, double, double) {
          // Clear drag flag — no selection change on release.
          if (m_drag.was_dragged)
            m_drag.was_dragged = false;
        });
    hdr->add_controller(gc_left);

    auto gc_right = Gtk::GestureClick::create();
    gc_right->set_button(3);
    gc_right->signal_pressed().connect(
        [this, section, path, hdr](int, double x, double y) {
          // If right-clicked item is not in the selection, replace selection
          // with it. Never remove items from selection on right-click.
          auto item = SelPath::make(section, path);
          if (!m_board_selection.count(item)) {
            m_board_selection = {item};
            refresh_all_highlights();
            fire_board_selection();
          }
          show_node_ctx_menu(section, path, x, y, hdr);
        });
    hdr->add_controller(gc_right);

    // Drag-and-drop (group header)
    setup_drag_source(hdr, section, path);
    setup_drop_target(hdr, section, path, true);

    // s19: name the widget by the node's iid (model-bound row).
    if (const BinderNode *gn = m_model.node_at(section, path))
      hdr->set_name(Folio::widget_name(sidebar_row_role(gn->kind), gn->iid));

  } else {
    // ── Leaf row ──────────────────────────────────────────────────────────
    auto *row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    row->set_margin_start(BASE + depth * INDENT);
    row->set_margin_end(10);
    row->set_margin_top(2);
    row->set_margin_bottom(2);
    row->add_css_class("binder-row");
    row->set_focusable(true);
    row->set_cursor(Gdk::Cursor::create("pointer"));
    row->set_tooltip_text(
        "Click to select · Double-click to open · Right-click for options");

    std::string icon_hex = m_prefs.color_hex_for_idx(node->color_idx);
    // Every leaf renders a Curvz symbolic icon as a Gtk::Image, tinted via the
    // CSS `color` property from the per-node color_idx. Scene leaves use
    // folio-scene-symbolic; Character/Place/Reference/Template use their own
    // leaf icons (s15 — replaced the ◈/⌖/⇥/T glyph-label placeholders now that
    // the Curvz icons are drawn).
    const char *icon_name =
        node->kind == BinderKind::Character ? "folio-character-symbolic"
      : node->kind == BinderKind::Place     ? "folio-place-symbolic"
      : node->kind == BinderKind::Reference ? "folio-reference-symbolic"
      : node->kind == BinderKind::Template  ? "folio-template-leaf-symbolic"
                                            : "folio-scene-symbolic";
    auto *img = Gtk::make_managed<Gtk::Image>();
    img->set_from_icon_name(icon_name);
    img->set_icon_size(Gtk::IconSize::NORMAL);
    const char *css_selector = "image";
    Gtk::Widget *ic = img;
    ic->add_css_class("scene-icon");
    ic->set_size_request(24, 24);
    if (!icon_hex.empty()) {
      auto prov = Gtk::CssProvider::create();
      prov->load_from_data(std::string(css_selector) + " { color: " + icon_hex + "; }");
      ic->get_style_context()->add_provider(
          prov, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

    // Label-colour swatch pill (right side of row, before status/badge)
    Gtk::Box *swatch = nullptr;
    if (!icon_hex.empty()) {
      swatch = Gtk::make_managed<Gtk::Box>();
      swatch->set_size_request(10, 10);
      swatch->set_valign(Gtk::Align::CENTER);
      auto prov = Gtk::CssProvider::create();
      prov->load_from_data(
          "box { background-color: " + icon_hex +
          ";"
          " border-radius: 9999px; min-width: 10px; min-height: 10px; }");
      swatch->get_style_context()->add_provider(
          prov, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    }

    auto *tbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 1);
    auto *tlbl = Gtk::make_managed<Gtk::Label>(
        node->title.empty() ? "Untitled" : node->title);
    tlbl->add_css_class("row-title");
    tlbl->set_halign(Gtk::Align::START);
    tlbl->set_ellipsize(Pango::EllipsizeMode::END);
    tlbl->set_hexpand(true);
    tbox->append(*tlbl);

    // Subtitle: description for chars/places, URL pill for references, synopsis
    // hint for scenes
    if (node->kind == BinderKind::Reference && !node->url.empty()) {
      // Clickable URL pill
      auto *url_btn = Gtk::make_managed<Gtk::Button>();
      url_btn->add_css_class("ref-url-pill");
      url_btn->set_halign(Gtk::Align::START);
      url_btn->set_tooltip_text(node->url);
      // Truncate display URL to fit
      std::string display_url = node->url;
      if (display_url.substr(0, 8) == "https://")
        display_url = display_url.substr(8);
      else if (display_url.substr(0, 7) == "http://")
        display_url = display_url.substr(7);
      if (display_url.length() > 28)
        display_url = display_url.substr(0, 28) + "…";
      auto *url_lbl = Gtk::make_managed<Gtk::Label>(display_url);
      url_lbl->add_css_class("ref-url-label");
      url_btn->set_child(*url_lbl);
      std::string url_copy = node->url;
      url_btn->signal_clicked().connect([url_copy]() {
        try {
          Gio::AppInfo::launch_default_for_uri(url_copy);
        } catch (...) {
        }
      });
      tbox->append(*url_btn);
    } else if (!node->description.empty()) {
      auto *dlbl = Gtk::make_managed<Gtk::Label>(node->description);
      dlbl->add_css_class("row-subtitle");
      dlbl->set_halign(Gtk::Align::START);
      dlbl->set_ellipsize(Pango::EllipsizeMode::END);
      tbox->append(*dlbl);
    }
    tbox->set_hexpand(true);
    tbox->set_valign(Gtk::Align::CENTER);
    row->append(*ic);
    row->append(*tbox);

    // Pin marker (scenes only) — a pinned hinge surfaced next to the colour
    // swatch as a write-first milestone (BinderNode.pin, stamped at materialize).
    if (node->kind == BinderKind::Scene && node->pin) {
      auto *pin_img = Gtk::make_managed<Gtk::Image>();
      pin_img->set_from_icon_name("folio-pin-symbolic");
      pin_img->set_icon_size(Gtk::IconSize::NORMAL);
      pin_img->set_size_request(14, 14);
      pin_img->set_valign(Gtk::Align::CENTER);
      pin_img->set_margin_end(2);
      pin_img->add_css_class("scene-pin");
      pin_img->set_tooltip_text(
          "Pinned hinge — a turn the story rests on. Write this one first.");
      row->append(*pin_img);
    }
    if (swatch)
      row->append(*swatch);

    // Status square (scenes only) — same size/style as label swatch,
    // rounded-square shape
    if (node->kind == BinderKind::Scene) {
      bool has_status = node->status != NodeStatus::Untitled;
      std::string status_name = node_status_label(node->status);
      std::string dot_hex =
          has_status ? m_prefs.status_color_for_name(status_name) : "#6c7086";
      auto *sq = Gtk::make_managed<Gtk::Box>();
      sq->set_size_request(10, 10);
      sq->set_valign(Gtk::Align::CENTER);
      sq->set_tooltip_text(status_name);
      auto prov = Gtk::CssProvider::create();
      prov->load_from_data(
          "box { background-color: " + dot_hex +
          ";"
          " border-radius: 0; min-width: 10px; min-height: 10px; }");
      sq->get_style_context()->add_provider(
          prov, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
      row->append(*sq);
    }

    // Role badge (characters only)
    if (node->kind == BinderKind::Character && !node->role.empty()) {
      auto *b = Gtk::make_managed<Gtk::Label>(node->role.substr(0, 4));
      b->add_css_class("badge-chip");
      row->append(*b);
    }

    parent_box->append(*row);
    m_row_entries.push_back({section, path, row});

    // Left-click
    auto gc_left = Gtk::GestureClick::create();
    gc_left->set_button(1);
    gc_left->signal_pressed().connect(
        [this, gc_left, section, path](int np, double, double) {
          grab_focus();
          Gdk::ModifierType mods = gc_left->get_current_event_state();
          bool ctrl  = (mods & Gdk::ModifierType::CONTROL_MASK) != Gdk::ModifierType{};
          bool shift = (mods & Gdk::ModifierType::SHIFT_MASK)   != Gdk::ModifierType{};
          auto item  = SelPath::make(section, path);
          bool selected = m_board_selection.count(item) > 0;

          // Double-click: select + open
          if (np == 2) {
            m_board_selection = {item};
            m_selection_anchor = item;
            refresh_all_highlights();
            fire_board_selection();
            if (m_on_opened) m_on_opened(section, path);
            return;
          }

          // Shift+click: range from anchor to here, replaces selection
          if (shift) {
            if (m_selection_anchor.path.empty()) {
              // No anchor — treat as plain click
              m_board_selection = {item};
              m_selection_anchor = item;
            } else {
              int anchor_idx = -1, clicked_idx = -1;
              for (int i = 0; i < (int)m_row_entries.size(); ++i) {
                if (m_row_entries[i].section == m_selection_anchor.section &&
                    m_row_entries[i].path   == m_selection_anchor.path)
                  anchor_idx = i;
                if (m_row_entries[i].section == section &&
                    m_row_entries[i].path   == path)
                  clicked_idx = i;
              }
              m_board_selection.clear();
              if (anchor_idx >= 0 && clicked_idx >= 0) {
                int lo = std::min(anchor_idx, clicked_idx);
                int hi = std::max(anchor_idx, clicked_idx);
                for (int i = lo; i <= hi; ++i)
                  m_board_selection.insert(
                      SelPath::make(m_row_entries[i].section, m_row_entries[i].path));
              } else {
                m_board_selection = {item};
                m_selection_anchor = item;
              }
            }
            refresh_all_highlights();
            fire_board_selection();
            return;
          }

          // Ctrl+click: add or remove
          if (ctrl) {
            if (selected)
              m_board_selection.erase(item);
            else {
              m_board_selection.insert(item);
              m_selection_anchor = item;
            }
            refresh_all_highlights();
            fire_board_selection();
            return;
          }

          // Plain click
          if (!selected) {
            m_board_selection = {item};
            m_selection_anchor = item;
          } else if (m_board_selection.size() == 1) {
            // Sole selected — stay selected, no change
            return;
          } else {
            // In multi-selection — collapse to just this item
            m_board_selection = {item};
            m_selection_anchor = item;
          }
          refresh_all_highlights();
          fire_board_selection();
        });
    gc_left->signal_released().connect(
        [this](int, double, double) {
          // Clear drag flag — no selection change on release.
          if (m_drag.was_dragged)
            m_drag.was_dragged = false;
        });
    row->add_controller(gc_left);

    // Right-click
    auto gc_right = Gtk::GestureClick::create();
    gc_right->set_button(3);
    gc_right->signal_pressed().connect(
        [this, section, path, row](int, double x, double y) {
          // If right-clicked item is not in the selection, replace selection
          // with it. Never remove items from selection on right-click.
          auto item = SelPath::make(section, path);
          if (!m_board_selection.count(item)) {
            m_board_selection = {item};
            refresh_all_highlights();
            fire_board_selection();
          }
          show_node_ctx_menu(section, path, x, y, row);
        });
    row->add_controller(gc_right);

    // Drag-and-drop (leaf row)
    setup_drag_source(row, section, path);
    setup_drop_target(row, section, path, false);

    // s19: name the widget by the node's iid (model-bound row).
    if (const BinderNode *ln = m_model.node_at(section, path))
      row->set_name(Folio::widget_name(sidebar_row_role(ln->kind), ln->iid));
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Selection / highlight
// ─────────────────────────────────────────────────────────────────────────────

void Sidebar::refresh_all_highlights() {
  for (auto &e : m_row_entries) {
    if (!e.row)
      continue;
    auto item = SelPath::make(e.section, e.path);
    if (m_board_selection.count(item))
      e.row->add_css_class("binder-selected");
    else
      e.row->remove_css_class("binder-selected");
  }
}

void Sidebar::fire_board_selection() {
  if (m_on_board_sel) {
    // Edge: convert the positional working set into iid-keyed BoardItems. A
    // selected row whose node no longer resolves is dropped from the broadcast.
    std::vector<BoardItem> items;
    for (const auto &s : m_board_selection) {
      const BinderNode *n = m_model.node_at(s.section, s.path);
      if (n) items.push_back(BoardItem::make(s.section, n->iid));
    }
    m_on_board_sel(std::move(items));
  }
}

void Sidebar::set_active(Section section, const std::string &iid) {
  // Used for external restores (app open, tab click) — replaces selection.
  // Edge: resolve the stable iid to the current row path.
  m_board_selection.clear();
  if (!iid.empty()) {
    auto path = m_model.path_for_iid(section, iid);
    if (!path.empty())
      m_board_selection.insert(SelPath::make(section, path));
  }
  refresh_all_highlights();
}

void Sidebar::set_selection(const std::vector<BoardItem> &items) {
  // Restore a full multi-selection without firing board_selection_callback.
  // Edge: items are iid-keyed; resolve each to its current row path.
  m_board_selection.clear();
  for (const auto &item : items) {
    auto path = m_model.path_for_iid(item.section, item.iid);
    if (!path.empty())
      m_board_selection.insert(SelPath::make(item.section, path));
  }
  refresh_all_highlights();
}

// ─────────────────────────────────────────────────────────────────────────────
// Collapse
// ─────────────────────────────────────────────────────────────────────────────

void Sidebar::toggle_node(int idx) {
  if (idx < 0 || idx >= (int)m_collapse_entries.size())
    return;
  auto &e = m_collapse_entries[idx];
  e.expanded = !e.expanded;
  if (e.revealer)
    e.revealer->set_reveal_child(e.expanded);
  if (e.arrow) {
    if (e.expanded) {
      e.arrow->set_text("▾");
      e.arrow->add_css_class("expanded");
      e.arrow->remove_css_class("collapsed");
    } else {
      e.arrow->set_text("▸");
      e.arrow->add_css_class("collapsed");
      e.arrow->remove_css_class("expanded");
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Keyboard-triggered add (public, called from window hotkeys)
// ─────────────────────────────────────────────────────────────────────────────

static std::vector<int> sibling_parent(const std::set<SelPath> &sel,
                                       Section section) {
  for (const auto &item : sel)
    if (item.section == section && item.path.size() > 1)
      return std::vector<int>(item.path.begin(), item.path.end() - 1);
  return {};
}

void Sidebar::add_scene_to_active() {
  on_add_leaf(Section::Manuscript,
              sibling_parent(m_board_selection, Section::Manuscript));
}
void Sidebar::add_character_to_active() {
  on_add_leaf(Section::Characters,
              sibling_parent(m_board_selection, Section::Characters));
}
void Sidebar::add_place_to_active() {
  on_add_leaf(Section::Places,
              sibling_parent(m_board_selection, Section::Places));
}
void Sidebar::add_reference_to_active() {
  on_add_leaf(Section::References,
              sibling_parent(m_board_selection, Section::References));
}
void Sidebar::add_template_to_active() {
  on_add_leaf(Section::Templates,
              sibling_parent(m_board_selection, Section::Templates));
}
void Sidebar::add_group_to_manuscript() {
  on_add_group(Section::Manuscript,
               sibling_parent(m_board_selection, Section::Manuscript));
}
void Sidebar::add_group_to_characters() {
  on_add_group(Section::Characters,
               sibling_parent(m_board_selection, Section::Characters));
}
void Sidebar::add_group_to_places() {
  on_add_group(Section::Places,
               sibling_parent(m_board_selection, Section::Places));
}

// ─────────────────────────────────────────────────────────────────────────────
// Keyboard move
// ─────────────────────────────────────────────────────────────────────────────
// Mutations
// ─────────────────────────────────────────────────────────────────────────────

void Sidebar::on_add_group(Section section,
                           const std::vector<int> &parent_path) {
  const NodeDefaults *nd =
      section == Section::Manuscript   ? &m_prefs.group_defaults
      : section == Section::Characters ? &m_prefs.char_group_defaults
                                       : &m_prefs.place_group_defaults;
  auto new_path = m_model.add_group(section, parent_path, "New Group", nd);
  rebuild_section(section);
  m_board_selection = {SelPath::make(section, new_path)};
  refresh_all_highlights();
  fire_board_selection();
  if (m_on_selected)
    m_on_selected(section, new_path);
}

void Sidebar::on_add_leaf(Section section,
                          const std::vector<int> &parent_path) {
  const NodeDefaults *nd =
      section == Section::Manuscript   ? &m_prefs.scene_defaults
      : section == Section::Characters ? &m_prefs.character_defaults
      : section == Section::Places     ? &m_prefs.place_defaults
      : section == Section::References ? &m_prefs.reference_defaults
      : section == Section::Templates  ? &m_prefs.template_defaults
                                       : nullptr;
  auto new_path = m_model.add_leaf(section, parent_path, "", nd);

  // Apply default template if one is configured for this section
  if (nd && !nd->template_name.empty()) {
    // Search document templates for a matching title
    const BinderNode *tpl = nullptr;
    std::function<const BinderNode *(const std::vector<BinderNode> &)> find_tpl;
    find_tpl = [&](const std::vector<BinderNode> &nodes) -> const BinderNode * {
      for (const auto &n : nodes) {
        if (n.kind == BinderKind::Template && n.title == nd->template_name)
          return &n;
        if (auto *found = find_tpl(n.children))
          return found;
      }
      return nullptr;
    };
    tpl = find_tpl(m_model.root(Section::Templates));

    // If not found in doc templates, check global templates
    if (!tpl) {
      std::string bare_name = nd->template_name;
      const std::string prefix = "[Global] ";
      if (bare_name.size() >= prefix.size() &&
          bare_name.substr(0, prefix.size()) == prefix)
        bare_name = bare_name.substr(prefix.size());
      // Cache globals locally to avoid repeated deserialisation
      static thread_local std::vector<BinderNode> s_globals;
      s_globals = m_prefs.global_templates_get();
      for (const auto &g : s_globals) {
        if (g.kind == BinderKind::Template && g.title == bare_name) {
          tpl = &g;
          break;
        }
      }
    }

    if (tpl) {
      BinderNode *new_node = m_model.node_at(section, new_path);
      if (new_node) {
        // Content always copied
        new_node->content = tpl->content;
        if (nd->template_copy_title && !tpl->title.empty())
          new_node->title = tpl->title;
        if (nd->template_copy_color)
          new_node->color_idx = tpl->color_idx;
        if (nd->template_copy_status)
          new_node->status = tpl->status;
        if (nd->template_copy_word_target && tpl->word_target > 0)
          new_node->word_target = tpl->word_target;
        m_model.mark_modified();
      }
    }
  }

  // s44 §11 — born on the category default: stamp the new object leaf's
  // template_id with its category's EXPLICIT default Template (a user template
  // marked default). The floor is the implicit default and needs no stamp (empty
  // template_id already resolves to it). rebuild_object_store first so the
  // registry reflects any is_default flags carried on Template nodes.
  {
    const char* cat = section == Section::Characters ? "character"
                    : section == Section::Places     ? "place"
                    : section == Section::References ? "reference"
                                                     : nullptr;
    if (cat) {
      m_model.rebuild_object_store();
      const std::string def = m_model.object_store().category_default_id(cat);
      if (!def.empty() && def != cat) {          // explicit user default, not the floor
        if (BinderNode* nn = m_model.node_at(section, new_path)) {
          nn->template_id = def;
          m_model.mark_modified();
        }
      }
    }
  }

  m_model.set_active(section, new_path);
  rebuild_section(section);
  m_board_selection = {SelPath::make(section, new_path)};
  refresh_all_highlights();
  fire_board_selection();
  if (m_on_selected)
    m_on_selected(section, new_path);
}

void Sidebar::on_remove_node(Section section, const std::vector<int> &path) {
  auto *win = dynamic_cast<Gtk::Window *>(get_root());
  if (!win)
    return;
  const BinderNode *node = m_model.node_at(section, path);
  if (!node)
    return;

  std::string name =
      node->title.empty() ? "this item" : "\"" + node->title + "\"";
  bool is_group = binder_kind_is_group(node->kind);
  bool in_trash = (section == Section::Trash);

  auto dlg =
      Gtk::AlertDialog::create(in_trash ? "Delete " + name + " permanently?"
                                        : "Move " + name + " to Trash?");
  dlg->set_detail(
      in_trash
          ? (is_group ? "This group and all its contents will be permanently "
                        "deleted."
                      : "This item will be permanently deleted.")
          : (is_group
                 ? "This group and all its contents will be moved to Trash."
                 : "This item will be moved to Trash."));
  dlg->set_modal(true);
  dlg->set_buttons({"Cancel", in_trash ? "Delete" : "Move to Trash"});
  dlg->set_cancel_button(0);
  dlg->set_default_button(0);

  dlg->choose(*win, [this, dlg, section, path,
                     in_trash](Glib::RefPtr<Gio::AsyncResult> &res) mutable {
    int response = 0;
    try {
      response = dlg->choose_finish(res);
    } catch (...) {
    }
    if (response == 1) {
      if (in_trash)
        m_model.remove_node(section, path);
      else
        m_model.trash_node(section, path);
      rebuild_section(section);
      if (!in_trash)
        rebuild_section(Section::Trash);
    }
  });
}

void Sidebar::on_remove_selected(Section section) {
  auto *win = dynamic_cast<Gtk::Window *>(get_root());
  if (!win)
    return;
  if (m_board_selection.empty())
    return;

  bool in_trash = (section == Section::Trash);
  int n = (int)m_board_selection.size();
  std::string title =
      in_trash ? "Delete " + std::to_string(n) + " items permanently?"
               : "Move " + std::to_string(n) + " items to Trash?";
  std::string detail =
      in_trash
          ? "These " + std::to_string(n) +
                " items will be permanently deleted. This cannot be undone."
          : "These " + std::to_string(n) + " items will be moved to Trash.";

  auto dlg = Gtk::AlertDialog::create(title);
  dlg->set_detail(detail);
  dlg->set_modal(true);
  dlg->set_buttons({"Cancel", in_trash ? "Delete All" : "Move to Trash"});
  dlg->set_cancel_button(0);
  dlg->set_default_button(0);

  std::vector<std::vector<int>> paths;
  for (const auto &item : m_board_selection)
    paths.push_back(item.path);

  dlg->choose(*win, [this, dlg, section, paths,
                     in_trash](Glib::RefPtr<Gio::AsyncResult> &res) mutable {
    int response = 0;
    try {
      response = dlg->choose_finish(res);
    } catch (...) {
    }
    if (response != 1)
      return;

    std::sort(paths.begin(), paths.end(), [](const auto &a, const auto &b) {
      if (a.size() != b.size())
        return a.size() > b.size();
      return a > b;
    });

    for (const auto &p : paths) {
      if (in_trash)
        m_model.remove_node(section, p);
      else
        m_model.trash_node(section, p);
    }

    m_board_selection.clear();
    rebuild_section(section);
    if (!in_trash)
      rebuild_section(Section::Trash);
  });
}

// ─────────────────────────────────────────────────────────────────────────────
// Cross-category multi-select guard
// ─────────────────────────────────────────────────────────────────────────────

void Sidebar::show_cross_category_dialog() {
  auto *win = dynamic_cast<Gtk::Window *>(get_root());
  if (!win)
    return;

  auto dlg = Gtk::AlertDialog::create("Cannot select across categories");
  dlg->set_detail(
      "Multi-selection is restricted to items within the same category.\n\n"
      "Scenes, Characters, and Places must each be selected separately — "
      "they represent different kinds of content that cannot be operated on "
      "together.");
  dlg->set_modal(true);
  dlg->set_buttons({"OK"});
  dlg->set_default_button(0);
  dlg->set_cancel_button(0);
  dlg->choose(*win, [dlg](Glib::RefPtr<Gio::AsyncResult> &res) mutable {
    try {
      dlg->choose_finish(res);
    } catch (...) {
    }
  });
}

// ─────────────────────────────────────────────────────────────────────────────
// Drag-and-drop
// ─────────────────────────────────────────────────────────────────────────────

Sidebar::DropZone Sidebar::compute_drop_zone(Gtk::Widget *widget, double y,
                                             bool is_group) const {
  int h = widget->get_height();
  if (h <= 0)
    return DropZone::After;

  if (is_group) {
    if (y < h * 0.25)
      return DropZone::Before;
    if (y > h * 0.75)
      return DropZone::After;
    return DropZone::Inside;
  }
  return (y < h * 0.5) ? DropZone::Before : DropZone::After;
}

// Clear the stored highlight state without touching any widget pointer —
// the widget may already be dead after a rebuild.
void Sidebar::clear_drop_highlight() {
  // m_drop_highlight_widget is kept as a Gtk::Widget* only while the drag
  // is live; we null it here so subsequent leave/motion signals are no-ops.
  m_drop_target = {};
}

void Sidebar::apply_drop_highlight(Gtk::Widget *widget, DropZone zone) {
  if (m_drop_target.widget == widget && m_drop_target.zone == zone)
    return;

  // Remove classes from previous target if it is still the same widget
  if (m_drop_target.widget && m_drop_target.widget != widget) {
    m_drop_target.widget->remove_css_class("binder-drop-before");
    m_drop_target.widget->remove_css_class("binder-drop-after");
    m_drop_target.widget->remove_css_class("binder-drop-inside");
  } else if (m_drop_target.widget == widget) {
    // Same widget, different zone — strip old classes
    widget->remove_css_class("binder-drop-before");
    widget->remove_css_class("binder-drop-after");
    widget->remove_css_class("binder-drop-inside");
  }

  m_drop_target = {widget, zone};

  switch (zone) {
  case DropZone::Before:
    widget->add_css_class("binder-drop-before");
    break;
  case DropZone::After:
    widget->add_css_class("binder-drop-after");
    break;
  case DropZone::Inside:
    widget->add_css_class("binder-drop-inside");
    break;
  default:
    break;
  }
}

void Sidebar::setup_drag_source(Gtk::Widget *widget, Section section,
                                const std::vector<int> &path) {
  auto drag_src = Gtk::DragSource::create();
  drag_src->set_actions(Gdk::DragAction::MOVE);

  drag_src->signal_prepare().connect(
      [this, widget, section,
       path](double, double) -> Glib::RefPtr<Gdk::ContentProvider> {
        m_drag.active = true;
        m_drag.was_dragged = false;
        m_drag.section = section;
        m_drag.path = path;
        m_drag.source_widget = widget;

        // Snapshot the full selection. If the dragged item is not in the
        // selection (e.g. user dragged without clicking first), treat it as
        // a single-item drag of just that widget's path.
        if (m_board_selection.count(SelPath::make(section, path)) &&
            !m_board_selection.empty()) {
          m_drag.all_paths.clear();
          for (const auto &item : m_board_selection)
            m_drag.all_paths.push_back(item.path);
        } else {
          m_drag.all_paths = {path};
        }

        widget->add_css_class("binder-drag-source");

        Glib::Value<int> val;
        val.init(G_TYPE_INT);
        val.set(0);
        return Gdk::ContentProvider::create(val);
      },
      false);

  drag_src->signal_drag_end().connect(
      [this](const Glib::RefPtr<Gdk::Drag> &, bool) {
        m_drag.active = false;
        m_drag.was_dragged =
            true; // release fires after this; suppress selection collapse
        m_drag.source_widget = nullptr;
        m_drag.all_paths.clear();
        m_drop_target = {};
      },
      false);

  widget->add_controller(drag_src);
}

void Sidebar::setup_drop_target(Gtk::Widget *widget, Section section,
                                const std::vector<int> &path, bool is_group) {
  auto drop_tgt = Gtk::DropTarget::create(G_TYPE_INT, Gdk::DragAction::MOVE);

  // Shared alive-guard: set to false when the widget is destroyed so that
  // leave/drop callbacks that arrive after a rebuild are no-ops.
  auto alive = std::make_shared<bool>(true);
  widget->signal_destroy().connect([alive]() { *alive = false; });

  drop_tgt->signal_motion().connect(
      [this, widget, section, path, is_group,
       alive](double, double y) -> Gdk::DragAction {
        if (!*alive)
          return Gdk::DragAction{};
        if (!m_drag.active || m_drag.section != section)
          return Gdk::DragAction{};

        // Reject drop onto any item that is part of the drag
        for (const auto &p : m_drag.all_paths)
          if (p == path)
            return Gdk::DragAction{};

        // Block dropping onto a descendant of any dragged node
        for (const auto &src : m_drag.all_paths) {
          if (path.size() > src.size()) {
            bool is_desc = true;
            for (int i = 0; i < (int)src.size(); ++i)
              if (path[i] != src[i]) {
                is_desc = false;
                break;
              }
            if (is_desc)
              return Gdk::DragAction{};
          }
        }

        apply_drop_highlight(widget, compute_drop_zone(widget, y, is_group));
        return Gdk::DragAction::MOVE;
      },
      false);

  drop_tgt->signal_leave().connect(
      [this, widget, alive]() {
        // Widget may be dead after a drop-triggered rebuild — check guard.
        if (!*alive)
          return;
        if (m_drop_target.widget == widget) {
          widget->remove_css_class("binder-drop-before");
          widget->remove_css_class("binder-drop-after");
          widget->remove_css_class("binder-drop-inside");
          m_drop_target = {};
        }
      },
      false);

  drop_tgt->signal_drop().connect(
      [this, widget, section, path, is_group, alive](const Glib::ValueBase &,
                                                     double, double y) -> bool {
        if (!*alive)
          return false;
        if (!m_drag.active || m_drag.section != section)
          return false;
        // Reject drop onto any item in the drag set
        for (const auto &p : m_drag.all_paths)
          if (p == path)
            return false;

        DropZone zone = compute_drop_zone(widget, y, is_group);
        m_drop_target = {};

        // Capture everything needed now — signal_drag_end fires before the
        // idle and clears m_drag, so we can't read it there.
        auto src_paths = m_drag.all_paths;
        Glib::signal_idle().connect_once(
            [this, section, path, zone, src_paths = std::move(src_paths)]() {
              execute_drop(section, path, zone, src_paths);
            });
        return true;
      },
      false);

  widget->add_controller(drop_tgt);
}

void Sidebar::execute_drop(Section section, const std::vector<int> &target_path,
                           DropZone zone,
                           const std::vector<std::vector<int>> &src_paths) {
  if (src_paths.empty())
    return;

  // s19: trace the move through named objects (iid ↔ widget ↔ log). Reads as
  // e.g. "drop scn_k3f9a2b7 -> inside grp_9q2x7m4a" in folio.log.
  if (spdlog::should_log(spdlog::level::debug)) {
    const char *zname = zone == DropZone::Before ? "before"
                      : zone == DropZone::After  ? "after"
                                                 : "inside";
    const BinderNode *tn = m_model.node_at(section, target_path);
    std::string srcs;
    for (const auto &p : src_paths) {
      const BinderNode *sn = m_model.node_at(section, p);
      if (!srcs.empty()) srcs += ",";
      srcs += sn ? sn->iid : std::string("?");
    }
    LOG_DEBUG("Sidebar drop [{}]: {} -> {} {}", section_name(section), srcs,
              zname, tn ? tn->iid : std::string("?"));
  }

  // Snapshot stable node IDs before any mutation so we can find the
  // actual landed paths afterwards regardless of index shifting.
  std::vector<int> src_ids;
  for (const auto &p : src_paths) {
    const BinderNode *n = m_model.node_at(section, p);
    src_ids.push_back(n ? n->id : -1);
  }

  // Determine destination parent and base insertion index
  std::vector<int> dest_parent;
  int dest_index = 0;

  if (zone == DropZone::Inside) {
    dest_parent = target_path;
    const BinderNode *grp = m_model.node_at(section, target_path);
    dest_index = grp ? (int)grp->children.size() : 0;
  } else {
    dest_parent = std::vector<int>(target_path.begin(), target_path.end() - 1);
    int tgt_idx = target_path.back();
    dest_index = (zone == DropZone::Before) ? tgt_idx : tgt_idx + 1;
  }

  // Sort sources deepest-first, highest-index-first so each removal
  // doesn't shift the paths of items not yet moved.
  // Build (path, id) pairs so the sort keeps ids aligned with paths.
  std::vector<std::pair<std::vector<int>, int>> src_with_ids;
  for (int i = 0; i < (int)src_paths.size(); ++i)
    src_with_ids.push_back({src_paths[i], src_ids[i]});

  std::sort(src_with_ids.begin(), src_with_ids.end(),
            [](const auto &a, const auto &b) {
              if (a.first.size() != b.first.size())
                return a.first.size() > b.first.size();
              return a.first > b.first;
            });

  int effective_dest = dest_index;
  for (const auto &[src, id] : src_with_ids) {
    if (std::vector<int>(src.begin(), src.end() - 1) == dest_parent) {
      if (src.back() < effective_dest)
        --effective_dest;
    }
    m_model.move_node(section, src, dest_parent, effective_dest);
    ++effective_dest;
  }

  rebuild_section(section);

  // Now look up actual landed paths by stable ID and build the move map.
  m_board_selection.clear();
  std::vector<std::pair<std::vector<int>, std::vector<int>>> moves;
  for (const auto &[old_p, id] : src_with_ids) {
    if (id < 0)
      continue;
    std::vector<int> new_p = m_model.path_for_id(section, id);
    if (!new_p.empty()) {
      m_board_selection.insert(SelPath::make(section, new_p));
      moves.push_back({old_p, new_p});
    }
  }

  if (m_on_nodes_moved)
    m_on_nodes_moved(section, moves);
  m_board_selection.clear();
  refresh_all_highlights();
  fire_board_selection();
}

} // namespace Folio
