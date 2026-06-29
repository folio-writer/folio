// ─────────────────────────────────────────────────────────────────────────────
// Folio — Editor_text.cpp  (TEXT TU — split from Editor.cpp in s13)
//
// Text features: find/replace bar, internal hyperlinks + anchors +
// backtrace + link picker, and annotations.
// See the manifest banner in Editor.hpp for the full routing map.
// ─────────────────────────────────────────────────────────────────────────────

#include <Editor.hpp>
#include <Editor_internal.hpp>
#include <color_utils.hpp>
#include <FolioLog.hpp>
#include <algorithm>
#include <chrono>
#include <ctime>
#include <random>
#include <regex>
#include <set>
#include <string>

namespace Folio {
// ─────────────────────────────────────────────────────────────────────────────
// open_find / close_find / find_bar_visible
// ─────────────────────────────────────────────────────────────────────────────

void Editor::open_find(bool with_replace) {
  m_find_revealer.set_reveal_child(true);
  if (with_replace && m_find_replace_toggle) {
    m_find_replace_toggle->set_active(true);
    m_replace_revealer.set_reveal_child(true);
    if (m_replace_entry)
      m_replace_entry->grab_focus();
  } else {
    if (m_find_entry) {
      m_find_entry->grab_focus();
      m_find_entry->select_region(0, -1);
    }
  }
  find_update();
}

void Editor::close_find() {
  m_find_revealer.set_reveal_child(false);
  find_clear_tags();
  m_find_matches.clear();
  m_find_current = -1;
  if (m_find_count_lbl)
    m_find_count_lbl->set_text("");
  m_text_view.grab_focus();
}

bool Editor::find_bar_visible() const {
  return m_find_revealer.get_reveal_child();
}

// ─────────────────────────────────────────────────────────────────────────────
// current_find_opts
// ─────────────────────────────────────────────────────────────────────────────

SearchOptions Editor::current_find_opts() const {
  SearchOptions opts;
  opts.case_sensitive = m_find_case_btn && m_find_case_btn->get_active();
  opts.whole_word = m_find_word_btn && m_find_word_btn->get_active();
  opts.use_regex = m_find_regex_btn && m_find_regex_btn->get_active();
  opts.search_title = false;
  opts.search_body = true;
  return opts;
}

// ─────────────────────────────────────────────────────────────────────────────
// find_clear_tags
// ─────────────────────────────────────────────────────────────────────────────

void Editor::find_clear_tags() {
  if (!m_buffer || !m_tag_find_match || !m_tag_find_current)
    return;
  auto s = m_buffer->begin();
  auto e = m_buffer->end();
  m_buffer->remove_tag(m_tag_find_match, s, e);
  m_buffer->remove_tag(m_tag_find_current, s, e);
}

// ─────────────────────────────────────────────────────────────────────────────
// find_update — rebuild match list and highlight
// ─────────────────────────────────────────────────────────────────────────────

void Editor::find_update() {
  find_clear_tags();
  m_find_matches.clear();
  m_find_current = -1;
  if (!m_find_entry || !m_buffer)
    return;

  std::string query = std::string(m_find_entry->get_text());
  if (query.empty()) {
    if (m_find_count_lbl)
      m_find_count_lbl->set_text("");
    m_find_entry->remove_css_class("find-error");
    return;
  }

  // Get buffer text
  std::string plain =
      std::string(m_buffer->get_text(m_buffer->begin(), m_buffer->end()));

  auto opts = current_find_opts();
  // For buffer search we work on plain text directly
  opts.search_body = true;

  auto raw_matches = SearchEngine::search_plain(plain, query, opts);

  if (raw_matches.empty() && !query.empty()) {
    // Check if it's a regex error
    try {
      SearchEngine::build_regex(query, opts);
      m_find_entry->remove_css_class("find-error");
    } catch (...) {
      m_find_entry->add_css_class("find-error");
    }
    if (m_find_count_lbl)
      m_find_count_lbl->set_text("No matches");
    return;
  }
  m_find_entry->remove_css_class("find-error");

  // search_plain returns BYTE offsets (std::sregex_iterator positions into the
  // UTF-8 std::string), but Gtk::TextBuffer::get_iter_at_offset expects CHARACTER
  // offsets. For ASCII the two coincide; once the text holds multi-byte UTF-8
  // (curly quotes, em dashes, accents) they diverge, and the gap ACCUMULATES down
  // the document — so the first match lands right and later ones drift forward
  // onto random spans. Build a byte->char table over the buffer text once (O(n))
  // and translate every match through it. byte_to_char[b] = number of characters
  // before byte b (count of UTF-8 lead bytes in plain[0..b)).
  std::vector<int> byte_to_char(plain.size() + 1, 0);
  {
    int ch = 0;
    for (size_t b = 0; b <= plain.size(); ++b) {
      byte_to_char[b] = ch;
      if (b < plain.size() &&
          (static_cast<unsigned char>(plain[b]) & 0xC0) != 0x80)
        ++ch; // advance on each lead byte, skip continuation bytes (10xxxxxx)
    }
  }
  auto b2c = [&](int byte_off) -> int {
    if (byte_off < 0) byte_off = 0;
    if (byte_off > static_cast<int>(plain.size())) byte_off = static_cast<int>(plain.size());
    return byte_to_char[static_cast<size_t>(byte_off)];
  };

  // Convert plain-text offsets to buffer iterators and apply tags
  m_loading = true; // suppress on_text_changed
  for (auto &rm : raw_matches) {
    int cs_off = b2c(rm.offset);
    int ce_off = b2c(rm.offset + rm.length);
    auto it_s = m_buffer->get_iter_at_offset(cs_off);
    auto it_e = m_buffer->get_iter_at_offset(ce_off);
    m_buffer->apply_tag(m_tag_find_match, it_s, it_e);
    FindMatch fm;
    fm.start = cs_off;
    fm.end = ce_off;
    m_find_matches.push_back(fm);
  }
  m_loading = false;

  if (!m_find_matches.empty()) {
    m_find_current = 0;
    // Highlight first match as current
    auto cs = m_buffer->get_iter_at_offset(m_find_matches[0].start);
    auto ce = m_buffer->get_iter_at_offset(m_find_matches[0].end);
    m_loading = true;
    m_buffer->apply_tag(m_tag_find_current, cs, ce);
    m_loading = false;
    m_buffer->place_cursor(cs);
    m_text_view.scroll_to(cs, 0.1);
  }

  if (m_find_count_lbl) {
    std::string lbl = std::to_string(m_find_current + 1) + " / " +
                      std::to_string((int)m_find_matches.size());
    m_find_count_lbl->set_text(lbl);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// find_navigate
// ─────────────────────────────────────────────────────────────────────────────

void Editor::find_navigate(int delta) {
  if (m_find_matches.empty()) {
    find_update();
    return;
  }

  // Remove current highlight from old match
  m_loading = true;
  if (m_find_current >= 0) {
    auto os =
        m_buffer->get_iter_at_offset(m_find_matches[m_find_current].start);
    auto oe = m_buffer->get_iter_at_offset(m_find_matches[m_find_current].end);
    m_buffer->remove_tag(m_tag_find_current, os, oe);
    m_buffer->apply_tag(m_tag_find_match, os, oe);
  }
  m_loading = false;

  int n = (int)m_find_matches.size();
  m_find_current = ((m_find_current + delta) % n + n) % n;

  // Apply current highlight
  m_loading = true;
  auto cs = m_buffer->get_iter_at_offset(m_find_matches[m_find_current].start);
  auto ce = m_buffer->get_iter_at_offset(m_find_matches[m_find_current].end);
  m_buffer->remove_tag(m_tag_find_match, cs, ce);
  m_buffer->apply_tag(m_tag_find_current, cs, ce);
  m_loading = false;

  m_buffer->place_cursor(cs);
  m_text_view.scroll_to(cs, 0.1);

  if (m_find_count_lbl) {
    std::string lbl =
        std::to_string(m_find_current + 1) + " / " + std::to_string(n);
    m_find_count_lbl->set_text(lbl);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// find_replace_current
// ─────────────────────────────────────────────────────────────────────────────

void Editor::find_replace_current() {
  if (!m_replace_entry || m_find_current < 0 ||
      m_find_current >= (int)m_find_matches.size())
    return;

  std::string query = std::string(m_find_entry ? m_find_entry->get_text() : "");
  std::string rep_str = std::string(m_replace_entry->get_text());
  if (query.empty())
    return;

  auto opts = current_find_opts();
  auto &fm = m_find_matches[m_find_current];

  auto it_s = m_buffer->get_iter_at_offset(fm.start);
  auto it_e = m_buffer->get_iter_at_offset(fm.end);

  // Build the actual replacement string (handles $1/$2 via regex)
  std::string matched = std::string(m_buffer->get_text(it_s, it_e));
  std::string actual_rep;
  try {
    auto re = SearchEngine::build_regex(query, opts);
    actual_rep = std::regex_replace(matched, re, rep_str,
                                    std::regex_constants::format_first_only);
  } catch (...) {
    actual_rep = rep_str;
  }

  m_buffer->erase(it_s, it_e);
  m_buffer->insert(m_buffer->get_iter_at_offset(fm.start), actual_rep);

  // Re-run search to refresh match list
  find_update();
}

// ─────────────────────────────────────────────────────────────────────────────
// find_replace_all
// ─────────────────────────────────────────────────────────────────────────────

void Editor::find_replace_all() {
  if (!m_replace_entry || !m_current_node)
    return;
  std::string query = std::string(m_find_entry ? m_find_entry->get_text() : "");
  std::string rep_str = std::string(m_replace_entry->get_text());
  if (query.empty())
    return;

  auto opts = current_find_opts();

  // Work on the HTML content directly for clean replace
  auto result =
      SearchEngine::replace_html(m_current_node->content, query, rep_str, opts);
  if (!result.error.empty() || result.replacements == 0) {
    if (!result.error.empty() && m_find_entry)
      m_find_entry->add_css_class("find-error");
    return;
  }

  // Reload buffer with new content
  m_loading = true;
  html_to_buffer(result.new_html);
  apply_base_font_tag();
  apply_indent();
  m_loading = false;

  m_current_node->content = result.new_html;
  m_current_node->content_modified = true;
  m_model.mark_modified();

  // Refresh search
  find_update();

  // Show toast with count
  show_toast("Replaced " + std::to_string(result.replacements) + " occurrence" +
             (result.replacements == 1 ? "" : "s"));
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal hyperlink helpers
// ─────────────────────────────────────────────────────────────────────────────

std::string Editor::generate_anchor_id() {
  static std::mt19937 rng{std::random_device{}()};
  static std::uniform_int_distribution<int> dist(0, 15);
  static const char hex[] = "0123456789abcdef";
  std::string id(6, '0');
  for (auto &c : id)
    c = hex[dist(rng)];
  return id;
}

void Editor::apply_link_tag_style(Glib::RefPtr<Gtk::TextTag> tag) {
  if (!tag)
    return;
  if (!m_prefs.show_links) {
    tag->property_foreground_set() = false;
    tag->property_underline() = Pango::Underline::NONE;
    return;
  }
  // blue-400 (#60a5fa) for dark, blue-600 (#2563eb) for light.
  tag->property_foreground() = detect_dark_mode() ? "#60a5fa" : "#2563eb";
  tag->property_underline() = Pango::Underline::SINGLE;
}

void Editor::apply_anchor_tag_style(Glib::RefPtr<Gtk::TextTag> tag) {
  if (!tag)
    return;
  // Subtle paragraph tint: amber-100 in light mode, a muted amber in dark mode.
  tag->property_paragraph_background() =
      detect_dark_mode() ? "#3d2e00" : "#fef3c7";
}

Glib::RefPtr<Gtk::TextTag>
Editor::link_tag_at(Gtk::TextBuffer::iterator iter) const {
  for (auto &t : iter.get_tags()) {
    std::string tn = t->property_name().get_value();
    if (tn.size() > 5 && tn.substr(0, 5) == "link:")
      return t;
  }
  return {};
}

bool Editor::follow_link_at(Gtk::TextBuffer::iterator iter) {
  auto tag = link_tag_at(iter);
  if (!tag)
    return false;
  std::string tn =
      tag->property_name().get_value(); // "link:<iid>:<anchor>"
  std::string payload = tn.substr(5);
  auto colon = payload.find(':');
  if (colon == std::string::npos)
    return false;
  std::string target_iid = payload.substr(0, colon);
  std::string anchor      = payload.substr(colon + 1);
  if (target_iid.empty())
    return false;

  // Validate target still exists
  auto *target = m_model.find_node_by_iid(target_iid);
  if (!target) {
    // Dead link — remove tag and notify
    auto start = iter, end = iter;
    start.backward_to_tag_toggle(tag);
    end.forward_to_tag_toggle(tag);
    m_buffer->remove_tag(tag, start, end);
    show_toast("Link target no longer exists — link removed.");
    if (m_current_node) {
      // Re-serialize from the live buffer so the dead link is gone from the
      // stored HTML before updating the backlink index.
      std::string html = buffer_to_html();
      m_current_node->content = html;
      m_model.update_backlinks_for_node(m_current_node->iid, html);
    }
    LOG_INFO("follow_link: dead link to node {} removed", target_iid);
    return true;
  }

  LOG_INFO("follow_link: navigating to node {} anchor '{}'", target_iid, anchor);
  if (m_on_follow_link)
    m_on_follow_link(target_iid, anchor);
  return true;
}

void Editor::start_arrival_highlight(Gtk::TextBuffer::iterator start,
                                     Gtk::TextBuffer::iterator end) {
  if (!m_tag_link_highlight || !m_buffer)
    return;
  if (m_link_highlight_conn.connected())
    m_link_highlight_conn.disconnect();
  m_buffer->apply_tag(m_tag_link_highlight, start, end);
  // Remove after 1200ms — connect() returns a connection we can store
  m_link_highlight_conn = Glib::signal_timeout().connect(
      [this]() -> bool {
        if (m_buffer && m_tag_link_highlight) {
          auto s = m_buffer->begin(), e = m_buffer->end();
          m_buffer->remove_tag(m_tag_link_highlight, s, e);
        }
        return false; // one-shot: don't repeat
      },
      1200);
}

void Editor::set_anchor_at_cursor() {
  if (!m_buffer || !m_current_node)
    return;
  auto ins = m_buffer->get_iter_at_mark(m_buffer->get_insert());
  int line = ins.get_line();
  auto ls = m_buffer->get_iter_at_line(line);
  auto le = ls;
  if (!le.ends_line())
    le.forward_to_line_end();

  // Remove any existing anchor: tag on this paragraph first
  for (auto &t : ls.get_tags()) {
    std::string tn = t->property_name().get_value();
    if (tn.size() > 7 && tn.substr(0, 7) == "anchor:") {
      m_buffer->remove_tag(t, ls, le);
      break;
    }
  }

  std::string aid = generate_anchor_id();
  std::string tname = "anchor:" + aid;
  auto tt = m_buffer->get_tag_table()->lookup(tname);
  if (!tt)
    tt = m_buffer->create_tag(tname);
  apply_anchor_tag_style(tt);
  m_buffer->apply_tag(tt, ls, le);

  // Trigger serialization so data-anchor appears in stored HTML
  on_text_changed();

  LOG_INFO("set_anchor: node={} anchor='{}' line={}", m_current_node->id, aid,
           line);
  show_toast(
      "Anchor set — insert a link to this node to target this paragraph.");
}

void Editor::insert_link(const std::string &target_iid, const std::string &anchor_id,
                         const std::string &display_text) {
  if (!m_buffer || !m_current_node)
    return;
  std::string tname = "link:" + target_iid + ":" + anchor_id;
  auto tt = m_buffer->get_tag_table()->lookup(tname);
  if (!tt) {
    tt = m_buffer->create_tag(tname);
    apply_link_tag_style(tt);
  }

  // Wrap in a user action so Ctrl+Z undoes the whole insertion atomically
  m_buffer->begin_user_action();

  // Use selected text as display text if available, else the provided text
  Gtk::TextBuffer::iterator sel_s, sel_e;
  bool has_sel = m_buffer->get_selection_bounds(sel_s, sel_e);
  if (has_sel) {
    m_buffer->apply_tag(tt, sel_s, sel_e);
  } else {
    auto ins = m_buffer->get_iter_at_mark(m_buffer->get_insert());
    m_buffer->insert_with_tag(ins, display_text, tt);
  }

  m_buffer->end_user_action();

  // Update backlink index
  if (m_current_node) {
    std::string html = buffer_to_html();
    m_current_node->content = html;
    m_model.update_backlinks_for_node(m_current_node->iid, html);
  }
  LOG_INFO("insert_link: target={} anchor='{}' display='{}'", target_iid,
           anchor_id, display_text);
}

void Editor::remove_link_at_cursor() {
  if (!m_buffer || !m_current_node)
    return;
  auto ins = m_buffer->get_iter_at_mark(m_buffer->get_insert());
  auto tag = link_tag_at(ins);
  if (!tag)
    return;
  auto start = ins, end = ins;
  start.backward_to_tag_toggle(tag);
  end.forward_to_tag_toggle(tag);
  m_buffer->begin_user_action();
  m_buffer->remove_tag(tag, start, end);
  m_buffer->end_user_action();
  if (m_current_node)
    m_model.update_backlinks_for_node(m_current_node->iid,
                                      m_current_node->content);
  LOG_INFO("remove_link: tag '{}'",
           std::string(tag->property_name().get_value()));
}

void Editor::do_backtrace() {
  if (!m_current_node)
    return;
  const auto &bl = m_model.backlinks();
  auto it = bl.find(m_current_node->iid);
  if (it == bl.end() || it->second.empty()) {
    show_toast("No links point to this node.");
    return;
  }

  // Single incoming link — navigate directly, no dialog needed.
  if (it->second.size() == 1) {
    const auto &e = it->second.front();
    if (m_on_follow_link)
      m_on_follow_link(e.source_iid, e.source_anchor);
    return;
  }

  // Multiple incoming links — show a picker so the user can choose.
  if (!m_backtrace_popover) {
    m_backtrace_popover = Gtk::make_managed<Gtk::Popover>();
    m_backtrace_popover->set_has_arrow(false);
    m_backtrace_popover->add_css_class("backtrace-picker");

    auto *vbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    vbox->set_margin_top(10);
    vbox->set_margin_bottom(10);
    vbox->set_margin_start(10);
    vbox->set_margin_end(10);
    vbox->set_size_request(280, -1);

    // Header row: diamond icon + live count label
    auto *hdr_box =
        Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto *hdr_icon = Gtk::make_managed<Gtk::Label>("\u25c6"); // ◆
    hdr_icon->add_css_class("backtrace-picker-icon");
    m_backtrace_header_label = Gtk::make_managed<Gtk::Label>("");
    m_backtrace_header_label->add_css_class("heading");
    m_backtrace_header_label->set_xalign(0.0f);
    m_backtrace_header_label->set_hexpand(true);
    hdr_box->append(*hdr_icon);
    hdr_box->append(*m_backtrace_header_label);
    vbox->append(*hdr_box);

    m_backtrace_list = Gtk::make_managed<Gtk::ListBox>();
    m_backtrace_list->add_css_class("boxed-list");
    m_backtrace_list->add_css_class("backtrace-list");
    m_backtrace_list->set_selection_mode(Gtk::SelectionMode::SINGLE);
    vbox->append(*m_backtrace_list);
    m_backtrace_popover->set_child(*vbox);

    m_backtrace_list->signal_row_activated().connect(
        [this](Gtk::ListBoxRow *row) {
          if (!row)
            return;
          std::string nm = row->get_name(); // "<iid>:<anchor>"
          auto colon = nm.find(':');
          std::string src_iid =
              (colon != std::string::npos) ? nm.substr(0, colon) : nm;
          std::string anchor =
              (colon != std::string::npos) ? nm.substr(colon + 1) : "";
          m_backtrace_popover->popdown();
          if (m_on_follow_link && !src_iid.empty())
            m_on_follow_link(src_iid, anchor);
        });
  }

  // Parent to window on first use — get_root() may be null during early
  // construction so we retry here, but only if not already parented.
  if (!m_backtrace_popover->get_parent())
    if (auto *win = dynamic_cast<Gtk::Window *>(get_root()))
      m_backtrace_popover->set_parent(*win);

  // ── Repopulate ────────────────────────────────────────────────────────────
  while (auto *row = m_backtrace_list->get_row_at_index(0))
    m_backtrace_list->remove(*row);

  // Update header: count unique source nodes vs total entries
  {
    std::set<std::string> unique_nodes;
    for (const auto &e : it->second)
      unique_nodes.insert(e.source_iid);
    int n_entries = (int)it->second.size();
    int n_nodes = (int)unique_nodes.size();
    std::string hdr =
        std::to_string(n_entries) + (n_entries == 1 ? " link" : " links");
    if (n_nodes < n_entries) // multiple links from same node
      hdr += " from " + std::to_string(n_nodes) +
             (n_nodes == 1 ? " node" : " nodes");
    else
      hdr += " point here";
    if (m_backtrace_header_label)
      m_backtrace_header_label->set_text(hdr);
  }

  // Helper: truncate link_text to ~50 chars for the subtitle
  auto excerpt = [](const std::string &s) -> std::string {
    if (s.empty())
      return "";
    // strip leading/trailing whitespace
    size_t a = s.find_first_not_of(" \t\n\r");
    if (a == std::string::npos)
      return "";
    size_t b = s.find_last_not_of(" \t\n\r");
    std::string t = s.substr(a, b - a + 1);
    if (t.size() > 52)
      t = t.substr(0, 50) + "\u2026"; // …
    return "\u201c" + t + "\u201d";   // "…"
  };

  for (const auto &e : it->second) {
    auto *src = m_model.find_node_by_iid(e.source_iid);
    if (!src)
      continue;

    auto *row = Gtk::make_managed<Gtk::ListBoxRow>();
    auto *cell = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    cell->set_margin_top(6);
    cell->set_margin_bottom(6);
    cell->set_margin_start(8);
    cell->set_margin_end(8);

    // Primary: source node title (bold)
    auto *title_lbl = Gtk::make_managed<Gtk::Label>(
        src->title.empty() ? "(Untitled)" : src->title);
    title_lbl->add_css_class("backtrace-row-title");
    title_lbl->set_xalign(0.0f);
    title_lbl->set_ellipsize(Pango::EllipsizeMode::END);
    cell->append(*title_lbl);

    // Secondary: link text excerpt, or synopsis fallback (dimmed italic)
    std::string sub = excerpt(e.link_text);
    if (sub.empty() && !src->synopsis.empty())
      sub = excerpt(src->synopsis);
    if (!sub.empty()) {
      auto *sub_lbl = Gtk::make_managed<Gtk::Label>(sub);
      sub_lbl->add_css_class("backtrace-row-sub");
      sub_lbl->set_xalign(0.0f);
      sub_lbl->set_ellipsize(Pango::EllipsizeMode::END);
      cell->append(*sub_lbl);
    }

    row->set_child(*cell);
    row->set_name(e.source_iid + ":" + e.source_anchor);
    m_backtrace_list->append(*row);
  }

  // Position at cursor line
  {
    double wx = 0, wy = 0;
    if (auto *win = dynamic_cast<Gtk::Window *>(get_root())) {
      auto ins = m_buffer->get_iter_at_mark(m_buffer->get_insert());
      Gdk::Rectangle iter_rect;
      m_text_view.get_iter_location(ins, iter_rect);
      int cx = 0, cy = 0;
      m_text_view.buffer_to_window_coords(Gtk::TextWindowType::TEXT,
                                          iter_rect.get_x(), iter_rect.get_y(),
                                          cx, cy);
      m_text_view.translate_coordinates(*win, cx, cy, wx, wy);
    }
    m_backtrace_popover->set_pointing_to(
        Gdk::Rectangle((int)wx, (int)wy, 1, 1));
  }
  m_backtrace_popover->popup();
}

void Editor::remove_anchor_at_cursor() {
  if (!m_buffer || !m_current_node)
    return;
  auto ins = m_buffer->get_iter_at_mark(m_buffer->get_insert());
  int line = ins.get_line();
  auto ls = m_buffer->get_iter_at_line(line);
  auto le = ls;
  if (!le.ends_line())
    le.forward_to_line_end();
  // Find and remove the anchor: tag on this paragraph
  for (auto &t : ls.get_tags()) {
    std::string tn = t->property_name().get_value();
    if (tn.size() > 7 && tn.substr(0, 7) == "anchor:") {
      m_buffer->remove_tag(t, ls, le);
      on_text_changed(); // flush to HTML so data-anchor disappears
      LOG_INFO("remove_anchor: removed '{}' on line {}", tn, line);
      show_toast("Anchor removed.");
      return;
    }
  }
  show_toast("No anchor on this paragraph.");
}

void Editor::open_link_picker() {
  if (!m_current_node)
    return;

  // Build popover lazily — only constructed once, reused on subsequent calls
  if (!m_link_picker_popover) {
    m_link_picker_popover = Gtk::make_managed<Gtk::Popover>();
    m_link_picker_popover->set_has_arrow(false);
  }

  // Parent to window on first use — only if not already parented.
  if (!m_link_picker_popover->get_parent())
    if (auto *win = dynamic_cast<Gtk::Window *>(get_root()))
      m_link_picker_popover->set_parent(*win);

  if (!m_link_picker_search) {
    // Build the inner UI (only once)

    auto *vbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    vbox->set_margin_top(8);
    vbox->set_margin_bottom(8);
    vbox->set_margin_start(8);
    vbox->set_margin_end(8);
    vbox->set_size_request(280, -1);

    auto *label = Gtk::make_managed<Gtk::Label>("Insert Link");
    label->add_css_class("heading");
    label->set_xalign(0.0f);
    vbox->append(*label);

    m_link_picker_search = Gtk::make_managed<Gtk::SearchEntry>();
    m_link_picker_search->set_placeholder_text("Search nodes…");
    vbox->append(*m_link_picker_search);

    auto *scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroll->set_size_request(-1, 240);
    m_link_picker_list = Gtk::make_managed<Gtk::ListBox>();
    m_link_picker_list->add_css_class("boxed-list");
    m_link_picker_list->set_selection_mode(Gtk::SelectionMode::SINGLE);
    scroll->set_child(*m_link_picker_list);
    vbox->append(*scroll);

    m_link_picker_popover->set_child(*vbox);
  }

  // Rebuild entry cache from current model state
  m_link_picker_entries.clear();
  std::function<void(const std::vector<BinderNode> &, const std::string &)>
      collect;
  collect = [&](const std::vector<BinderNode> &nodes, const std::string &sec) {
    for (const auto &n : nodes) {
      if (!binder_kind_is_group(n.kind))
        m_link_picker_entries.push_back(
            {n.iid, n.title.empty() ? "(Untitled)" : n.title, sec});
      collect(n.children, sec);
    }
  };
  collect(m_model.root(Section::Manuscript), "Manuscript");
  collect(m_model.root(Section::Characters), "Characters");
  collect(m_model.root(Section::Places), "Places");
  collect(m_model.root(Section::References), "References");

  // Helper: repopulate the list box for a given filter string.
  // Captures only `this` — entries are in m_link_picker_entries (member).
  auto repopulate = [this](const std::string &filter) {
    while (auto *row = m_link_picker_list->get_row_at_index(0))
      m_link_picker_list->remove(*row);
    for (const auto &e : m_link_picker_entries) {
      std::string lt = e.title, lf = filter;
      for (auto &c : lt)
        c = std::tolower((unsigned char)c);
      for (auto &c : lf)
        c = std::tolower((unsigned char)c);
      if (!lf.empty() && lt.find(lf) == std::string::npos)
        continue;

      auto *row = Gtk::make_managed<Gtk::ListBoxRow>();
      auto *hbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
      hbox->set_margin_top(4);
      hbox->set_margin_bottom(4);
      hbox->set_margin_start(6);
      hbox->set_margin_end(6);
      auto *tl = Gtk::make_managed<Gtk::Label>(e.title);
      tl->set_xalign(0.0f);
      tl->set_hexpand(true);
      auto *sl = Gtk::make_managed<Gtk::Label>(e.section);
      sl->add_css_class("dim-label");
      sl->set_xalign(1.0f);
      hbox->append(*tl);
      hbox->append(*sl);
      row->set_child(*hbox);
      // Encode "iid:title" in the row name for retrieval on activation
      row->set_name(e.iid + ":" + e.title);
      m_link_picker_list->append(*row);
    }
  };

  repopulate("");

  // Disconnect previous signal connections before reconnecting —
  // prevents accumulating duplicate handlers each time the picker opens.
  if (m_link_search_conn.connected())
    m_link_search_conn.disconnect();
  if (m_link_row_conn.connected())
    m_link_row_conn.disconnect();

  m_link_search_conn = m_link_picker_search->signal_search_changed().connect(
      [this, repopulate]() mutable {
        repopulate(std::string(m_link_picker_search->get_text()));
      });

  m_link_row_conn = m_link_picker_list->signal_row_activated().connect(
      [this](Gtk::ListBoxRow *row) {
        if (!row)
          return;
        std::string nm = row->get_name(); // "<iid>:<title>"
        auto colon = nm.find(':');
        if (colon == std::string::npos)
          return;
        std::string iid   = nm.substr(0, colon);
        std::string title = nm.substr(colon + 1);
        if (iid.empty())
          return;
        m_link_picker_popover->popdown();
        insert_link(iid, "", title);
      });

  // Position the popover centred over the text view (same pattern as the
  // Unicode character picker).  Without a pointing rect the popover has no
  // anchor and silently fails to appear on some GTK4 backends.
  {
    double wx = 0, wy = 0;
    if (auto *win = dynamic_cast<Gtk::Window *>(get_root())) {
      m_text_view.translate_coordinates(*win, m_text_view.get_width() / 2,
                                        m_text_view.get_height() / 2, wx, wy);
    }
    m_link_picker_popover->set_pointing_to(
        Gdk::Rectangle((int)wx, (int)wy, 1, 1));
  }

  // Clear search text from previous use and show
  m_link_picker_search->set_text("");
  m_link_picker_popover->popup();
  m_link_picker_search->grab_focus();
}

void Editor::scroll_to_anchor(const std::string &anchor_id) {
  if (!m_buffer || anchor_id.empty())
    return;
  std::string tname = "anchor:" + anchor_id;
  auto tag = m_buffer->get_tag_table()->lookup(tname);
  if (!tag) {
    LOG_WARN("scroll_to_anchor: tag '{}' not found in buffer", tname);
    return;
  }

  // Walk buffer lines looking for the first iter that has this tag
  int nlines = m_buffer->get_line_count();
  for (int ln = 0; ln < nlines; ++ln) {
    auto it = m_buffer->get_iter_at_line(ln);
    if (it.has_tag(tag)) {
      // Found the paragraph — place cursor and scroll
      auto line_end = it;
      if (!line_end.ends_line())
        line_end.forward_to_line_end();
      m_buffer->place_cursor(it);
      m_text_view.scroll_to(m_buffer->get_insert(), 0.0, 1.0, 0.15);
      // Highlight the paragraph
      start_arrival_highlight(it, line_end);
      LOG_INFO("scroll_to_anchor: found anchor '{}' on line {}", anchor_id, ln);
      return;
    }
  }
  LOG_WARN("scroll_to_anchor: anchor '{}' not found on any line", anchor_id);
}

// ─────────────────────────────────────────────────────────────────────────────
// apply_tab_stops — push prefs tab stop positions to the GtkTextView
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// Annotations
// ─────────────────────────────────────────────────────────────────────────────

// Helper: parse a hex color string "#rrggbb" -> Gdk::RGBA (fully opaque, no
// blending). Delegates to the shared parser; fallback is yellow.
static Gdk::RGBA hex_to_rgba_raw(const std::string &hex) {
  return Folio::color::from_hex(hex, Folio::color::rgba(1, 1, 0, 1));
}

// blend_annotation_bg -- pre-blend annotation colour against the real page
// background at weight 0.15 so it renders as a subtle tint.
//
// GTK4 TextTag background_rgba composites against black (not the page colour),
// making alpha values produce dark muddy tints instead of pastels.  The fix is
// to discover the actual page background and bake the blend ourselves, yielding
// a fully opaque colour that GTK composites correctly.
Gdk::RGBA Editor::blend_annotation_bg(const std::string &hex) const {
  // --- 1. Parse the annotation colour ---
  Gdk::RGBA ann = hex_to_rgba_raw(hex);

  // --- 2. Discover the real page background ---
  // Try theme_base_color first (canonical text-area bg), fall back to
  // window_bg_color, and finally infer from fg luminance.
  Gdk::RGBA bg;
  // lookup_color is not const in gtkmm — const_cast is safe, no mutation occurs
  auto style = const_cast<Gtk::TextView &>(m_text_view).get_style_context();
  if (!style->lookup_color("paper_bg", bg) && // Folio palette first
      !style->lookup_color("theme_base_color", bg) &&
      !style->lookup_color("window_bg_color", bg)) {
    // Hard fallback: use Folio paper_bg values directly
    bool dark = detect_dark_mode();
    bg.set_rgba(dark ? 0.141f : 0.831f, // #242436 / #d4d6dc
                dark ? 0.141f : 0.839f, dark ? 0.212f : 0.863f, 1.0f);
  }
  bg.set_alpha(1.0f); // ensure opaque

  // --- 3. Blend: result = ann*W + bg*(1-W)
  // Dark mode needs a heavier annotation weight to be readable against the
  // dark paper; light mode stays subtle.
  float W = detect_dark_mode() ? 0.12f : 0.08f;
  Gdk::RGBA result;
  result.set_rgba(ann.get_red() * W + bg.get_red() * (1.0f - W),
                  ann.get_green() * W + bg.get_green() * (1.0f - W),
                  ann.get_blue() * W + bg.get_blue() * (1.0f - W), 1.0f);
  return result;
}

void Editor::apply_annotation_tag(const Annotation &ann) {
  if (!m_buffer)
    return;
  if (ann.range_start >= ann.range_end)
    return;
  auto s = m_buffer->get_iter_at_offset(ann.range_start);
  auto e = m_buffer->get_iter_at_offset(ann.range_end);
  if (s == e)
    return;

  std::string tn = "ann:" + std::to_string(ann.id);
  auto table = m_buffer->get_tag_table();
  auto tag = table->lookup(tn);
  if (!tag) {
    tag = m_buffer->create_tag(tn);
    tag->property_background_rgba() = blend_annotation_bg(ann.color_hex);
    tag->property_underline() = Pango::Underline::LOW;
    tag->property_underline_rgba() = hex_to_rgba_raw(ann.color_hex);
  } else {
    // Update colour in case it was edited (e.g. user changed annotation colour)
    tag->property_background_rgba() = blend_annotation_bg(ann.color_hex);
    tag->property_underline_rgba() = hex_to_rgba_raw(ann.color_hex);
  }
  // If annotations are hidden, suppress visuals immediately
  if (!m_prefs.show_annotations) {
    Gdk::RGBA transparent;
    transparent.set_rgba(0, 0, 0, 0);
    tag->property_background_rgba() = transparent;
    tag->property_underline() = Pango::Underline::NONE;
  }
  m_buffer->apply_tag(tag, s, e);
}

void Editor::rebuild_annotation_tags() {
  if (!m_buffer || !m_current_node)
    return;
  // Remove all existing ann: tags from the entire buffer
  std::vector<Glib::RefPtr<Gtk::TextTag>> ann_tags;
  m_buffer->get_tag_table()->foreach (
      [&](const Glib::RefPtr<Gtk::TextTag> &tag) {
        if (tag->property_name().get_value().substr(0, 4) == "ann:")
          ann_tags.push_back(tag);
      });
  auto begin = m_buffer->begin(), end = m_buffer->end();
  m_loading = true;
  for (auto &t : ann_tags)
    m_buffer->remove_tag(t, begin, end);
  // Re-apply all current annotations
  for (const auto &ann : m_current_node->annotations)
    apply_annotation_tag(ann);
  m_loading = false;
}

// refresh_annotation_visibility — show or hide annotation highlights without
// touching the underlying data.  When hidden, background and underline are
// cleared; when shown, they are restored from each annotation's color_hex.
void Editor::refresh_annotation_visibility() {
  if (!m_buffer)
    return;
  bool show = m_prefs.show_annotations;
  m_buffer->get_tag_table()->foreach (
      [&](const Glib::RefPtr<Gtk::TextTag> &tag) {
        std::string tn = tag->property_name().get_value();
        if (tn.size() <= 4 || tn.substr(0, 4) != "ann:")
          return;
        if (show) {
          // Restore visual from stored annotation color
          if (m_current_node) {
            try {
              int id = std::stoi(tn.substr(4));
              for (const auto &ann : m_current_node->annotations) {
                if (ann.id == id) {
                  tag->property_background_rgba() =
                      blend_annotation_bg(ann.color_hex);
                  tag->property_underline() = Pango::Underline::LOW;
                  tag->property_underline_rgba() =
                      hex_to_rgba_raw(ann.color_hex);
                  break;
                }
              }
            } catch (...) {
            }
          }
        } else {
          // Make invisible: transparent background, no underline
          Gdk::RGBA transparent;
          transparent.set_rgba(0, 0, 0, 0);
          tag->property_background_rgba() = transparent;
          tag->property_underline() = Pango::Underline::NONE;
        }
      });
}

// refresh_link_visibility — show or hide hyperlink colouring without losing
// the tag ranges.  When hidden links render as plain body text.
void Editor::refresh_link_visibility() {
  if (!m_buffer)
    return;
  bool show = m_prefs.show_links;
  m_buffer->get_tag_table()->foreach (
      [&](const Glib::RefPtr<Gtk::TextTag> &tag) {
        std::string tn = tag->property_name().get_value();
        if (tn.size() <= 5 || tn.substr(0, 5) != "link:")
          return;
        if (show) {
          apply_link_tag_style(tag);
        } else {
          // Render as plain text: unset colour and underline
          tag->property_foreground_set() = false;
          tag->property_underline() = Pango::Underline::NONE;
        }
      });
}

void Editor::add_annotation(int range_start, int range_end,
                            const std::string &text, const std::string &kind,
                            const std::string &color_hex) {
  if (!m_current_node)
    return;

  Annotation ann;
  ann.id = m_current_node->next_annotation_id++;
  ann.range_start = range_start;
  ann.range_end = range_end;
  ann.text = text;
  ann.kind = kind;
  ann.color_hex = color_hex;

  // Timestamp
  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::gmtime(&t));
  ann.created_at = buf;

  m_current_node->annotations.push_back(ann);
  apply_annotation_tag(ann);
  m_model.mark_modified();
  if (on_annotations_changed)
    on_annotations_changed();
}

// JV-aware: stores node-relative offsets, applies tag at buffer-absolute offsets
void Editor::add_annotation_to_node(BinderNode *node,
                                    int node_start, int node_end,
                                    int buf_start, int buf_end,
                                    const std::string &text,
                                    const std::string &kind,
                                    const std::string &color_hex) {
  if (!node) return;

  Annotation ann;
  ann.id = node->next_annotation_id++;
  ann.range_start = node_start;  // stored relative to node content
  ann.range_end   = node_end;
  ann.text = text;
  ann.kind = kind;
  ann.color_hex = color_hex;

  auto now = std::chrono::system_clock::now();
  std::time_t t = std::chrono::system_clock::to_time_t(now);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", std::gmtime(&t));
  ann.created_at = buf;

  node->annotations.push_back(ann);

  // Apply tag at buffer-absolute offsets for immediate visual feedback
  Annotation rebased = ann;
  rebased.range_start = buf_start;
  rebased.range_end   = buf_end;
  apply_annotation_tag(rebased);

  m_model.mark_modified();
  if (on_annotations_changed)
    on_annotations_changed();
}

void Editor::remove_annotation(int id) {
  if (!m_current_node)
    return;
  auto &anns = m_current_node->annotations;
  anns.erase(std::remove_if(anns.begin(), anns.end(),
                            [id](const Annotation &a) { return a.id == id; }),
             anns.end());

  // Remove the tag
  std::string tn = "ann:" + std::to_string(id);
  auto tag = m_buffer->get_tag_table()->lookup(tn);
  if (tag)
    m_buffer->remove_tag(tag, m_buffer->begin(), m_buffer->end());

  m_model.mark_modified();
  if (on_annotations_changed)
    on_annotations_changed();
}

void Editor::edit_annotation(int id, const std::string &text,
                             const std::string &kind,
                             const std::string &color_hex) {
  if (!m_current_node)
    return;
  for (auto &ann : m_current_node->annotations) {
    if (ann.id == id) {
      ann.text = text;
      ann.kind = kind;
      ann.color_hex = color_hex;
      apply_annotation_tag(ann); // refreshes colour
      break;
    }
  }
  m_model.mark_modified();
  if (on_annotations_changed)
    on_annotations_changed();
}

// JV-aware: operates on an explicit node, removes tag from buffer by id
void Editor::remove_annotation_from_node(BinderNode *node, int id) {
  if (!node)
    return;
  auto &anns = node->annotations;
  anns.erase(std::remove_if(anns.begin(), anns.end(),
                            [id](const Annotation &a) { return a.id == id; }),
             anns.end());
  // Remove the tag from the buffer (valid in both single and JV mode)
  std::string tn = "ann:" + std::to_string(id);
  auto tag = m_buffer->get_tag_table()->lookup(tn);
  if (tag)
    m_buffer->remove_tag(tag, m_buffer->begin(), m_buffer->end());
  m_model.mark_modified();
  if (on_annotations_changed)
    on_annotations_changed();
}

// JV-aware: operates on an explicit node, refreshes tag in buffer by id
void Editor::edit_annotation_on_node(BinderNode *node, int id,
                                     const std::string &text,
                                     const std::string &kind,
                                     const std::string &color_hex) {
  if (!node)
    return;
  for (auto &ann : node->annotations) {
    if (ann.id == id) {
      ann.text = text;
      ann.kind = kind;
      ann.color_hex = color_hex;
      // Find segment base offset for JV mode
      int base = 0;
      for (const auto &seg : m_joined_segments) {
        if (seg.node == node) {
          base = m_buffer->get_iter_at_mark(seg.start).get_offset();
          break;
        }
      }
      Annotation rebased = ann;
      rebased.range_start = base + ann.range_start;
      rebased.range_end   = base + ann.range_end;
      apply_annotation_tag(rebased);
      break;
    }
  }
  m_model.mark_modified();
  if (on_annotations_changed)
    on_annotations_changed();
}

void Editor::scroll_to_annotation(int id) {
  if (!m_buffer)
    return;

  // Find the annotation — search current node in single mode, all segments in JV
  const Annotation *found = nullptr;
  int base = 0;

  if (m_current_node) {
    for (const auto &ann : m_current_node->annotations)
      if (ann.id == id) { found = &ann; base = 0; break; }
  } else {
    for (const auto &seg : m_joined_segments) {
      if (!seg.node) continue;
      for (const auto &ann : seg.node->annotations) {
        if (ann.id == id) {
          found = &ann;
          base = m_buffer->get_iter_at_mark(seg.start).get_offset();
          break;
        }
      }
      if (found) break;
    }
  }

  if (!found) return;
  auto it = m_buffer->get_iter_at_offset(base + found->range_start);
  m_buffer->place_cursor(it);
  m_text_view.scroll_to(m_buffer->get_insert(), 0.1);
  std::string tn = "ann:" + std::to_string(id);
  auto tag = m_buffer->get_tag_table()->lookup(tn);
  if (tag) {
    auto orig = tag->property_background_rgba().get_value();
    Gdk::RGBA flash;
    flash.set_rgba(orig.get_red() * 0.7, orig.get_green() * 0.7,
                   orig.get_blue() * 0.7, 0.7f);
    tag->property_background_rgba() = flash;
    Glib::signal_timeout().connect_once(
        [tag, orig]() { tag->property_background_rgba() = orig; }, 400);
  }
}

void Editor::show_annotation_popover(double x, double y) {
  if (!m_buffer)
    return;

  // Get selection bounds — use the snapshot taken at right-click time
  // because the selection is cleared by the time the popover builds
  bool has_sel = (m_rc_sel_start >= 0 && m_rc_sel_end > m_rc_sel_start);

  // Check if we clicked inside an existing annotation
  int bx2, by2, trailing2 = 0;
  m_text_view.window_to_buffer_coords(Gtk::TextWindowType::WIDGET, (int)x,
                                      (int)y, bx2, by2);
  Gtk::TextBuffer::iterator click_iter;
  m_text_view.get_iter_at_position(click_iter, trailing2, bx2, by2);
  int existing_id = -1;
  for (auto &tag : click_iter.get_tags()) {
    std::string tn = tag->property_name().get_value();
    if (tn.size() > 4 && tn.substr(0, 4) == "ann:") {
      try {
        existing_id = std::stoi(tn.substr(4));
      } catch (...) {
      }
      break;
    }
  }

  // Build the popover
  if (m_ann_popover) {
    m_ann_popover->unparent();
    m_ann_popover = nullptr;
  }
  m_ann_popover = Gtk::make_managed<Gtk::Popover>();
  m_ann_popover->set_parent(m_text_view);
  m_ann_popover->set_autohide(true);

  auto *box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 8);
  box->set_margin_top(12);
  box->set_margin_bottom(12);
  box->set_margin_start(12);
  box->set_margin_end(12);
  box->set_size_request(280, -1);

  // Title
  auto *title = Gtk::make_managed<Gtk::Label>(
      existing_id >= 0 ? "Edit Annotation" : "Add Annotation");
  title->add_css_class("heading");
  title->set_halign(Gtk::Align::START);
  box->append(*title);

  // Kind selector
  auto *kind_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
  auto kind_items =
      Gtk::StringList::create({"Writer", "Editor", "Proofreader"});
  auto *kind_dd = Gtk::make_managed<Gtk::DropDown>(kind_items);
  kind_dd->set_hexpand(true);

  // Color selector (three swatches)
  struct ColorOpt {
    const char *hex;
    const char *label;
  };
  static constexpr ColorOpt kColors[] = {
      {"#fef08a", "Yellow"},
      {"#fca5a5", "Red"},
      {"#86efac", "Green"},
      {"#bfdbfe", "Blue"},
  };
  auto *color_box =
      Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
  auto selected_color = std::make_shared<std::string>("#fef08a");
  Gtk::ToggleButton *first_swatch = nullptr;

  for (auto &co : kColors) {
    auto *sw = Gtk::make_managed<Gtk::ToggleButton>();
    sw->add_css_class("ann-swatch");
    sw->set_tooltip_text(co.label);
    // Inline CSS for swatch background color only
    auto css = Gtk::CssProvider::create();
    css->load_from_data(std::string(".ann-swatch { background: ") + co.hex +
                        "; }");
    sw->get_style_context()->add_provider(css,
                                          GTK_STYLE_PROVIDER_PRIORITY_USER);
    if (!first_swatch) {
      first_swatch = sw;
      sw->set_active(true);
      *selected_color = co.hex;
    } else {
      sw->set_group(*first_swatch);
    }
    std::string hex = co.hex;
    sw->signal_toggled().connect([sw, hex, selected_color]() {
      if (sw->get_active())
        *selected_color = hex;
    });
    color_box->append(*sw);
  }

  kind_box->append(*kind_dd);
  kind_box->append(*color_box);
  box->append(*kind_box);

  // Text entry
  auto *tv = Gtk::make_managed<Gtk::TextView>();
  tv->set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
  tv->set_size_request(-1, 80);
  tv->add_css_class("annotation-entry");
  auto *tv_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
  tv_scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  tv_scroll->set_child(*tv);
  tv_scroll->set_size_request(-1, 88);
  box->append(*tv_scroll);

  // Pre-populate if editing existing annotation.
  // In JV mode m_current_node is null — search segments instead.
  BinderNode *existing_node = nullptr;
  if (existing_id >= 0) {
    if (m_current_node) {
      existing_node = m_current_node;
    } else {
      for (const auto &seg : m_joined_segments) {
        if (!seg.node) continue;
        for (const auto &ann : seg.node->annotations) {
          if (ann.id == existing_id) { existing_node = seg.node; break; }
        }
        if (existing_node) break;
      }
    }
  }
  if (existing_id >= 0 && existing_node) {
    for (auto &ann : existing_node->annotations) {
      if (ann.id == existing_id) {
        tv->get_buffer()->set_text(ann.text);
        guint kind_sel = 0;
        if (ann.kind == "Editor")
          kind_sel = 1;
        else if (ann.kind == "Proofreader")
          kind_sel = 2;
        kind_dd->set_selected(kind_sel);
        *selected_color = ann.color_hex;
        break;
      }
    }
  }

  // Buttons row
  auto *btn_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  btn_row->set_halign(Gtk::Align::END);

  if (existing_id >= 0) {
    auto *del_btn = Gtk::make_managed<Gtk::Button>("Delete");
    del_btn->add_css_class("destructive-action");
    int del_id = existing_id;
    del_btn->signal_clicked().connect([this, del_id, existing_node]() {
      if (m_ann_popover)
        m_ann_popover->popdown();
      if (m_current_node)
        remove_annotation(del_id);
      else
        remove_annotation_from_node(existing_node, del_id);
    });
    btn_row->append(*del_btn);
  }

  auto *cancel_btn = Gtk::make_managed<Gtk::Button>("Cancel");
  cancel_btn->signal_clicked().connect([this]() {
    if (m_ann_popover)
      m_ann_popover->popdown();
  });

  auto *save_btn =
      Gtk::make_managed<Gtk::Button>(existing_id >= 0 ? "Save" : "Add");
  save_btn->add_css_class("suggested-action");

  int cap_existing = existing_id;
  int cap_start = (has_sel && m_rc_sel_start >= 0) ? m_rc_sel_start
                                                   : click_iter.get_offset();
  int cap_end = (has_sel && m_rc_sel_end > m_rc_sel_start)
                    ? m_rc_sel_end
                    : click_iter.get_offset() + 1;

  // In JV mode, find which segment owns the click/selection so we can
  // store node-relative offsets and route edits correctly.
  BinderNode *target_node = m_current_node;
  int seg_base = 0;
  if (!m_current_node && !m_joined_segments.empty()) {
    // Find the segment that contains cap_start
    for (int si = 0; si < (int)m_joined_segments.size(); ++si) {
      const auto &seg = m_joined_segments[si];
      if (!seg.node) continue;
      int seg_start = m_buffer->get_iter_at_mark(seg.start).get_offset();
      int seg_end = (si + 1 < (int)m_joined_segments.size())
                       ? m_buffer->get_iter_at_mark(m_joined_segments[si + 1].start).get_offset()
                       : m_buffer->end().get_offset();
      if (cap_start >= seg_start && cap_start < seg_end) {
        target_node = seg.node;
        seg_base = seg_start;
        break;
      }
    }
  }

  save_btn->signal_clicked().connect(
      [this, tv, kind_dd, selected_color, cap_existing, cap_start, cap_end,
       target_node, seg_base, existing_node]() {
        std::string text = tv->get_buffer()->get_text();
        if (text.empty()) {
          if (m_ann_popover)
            m_ann_popover->popdown();
          return;
        }
        static const char *kinds[] = {"Writer", "Editor", "Proofreader"};
        std::string kind = kinds[std::min((guint)2, kind_dd->get_selected())];
        if (cap_existing >= 0) {
          if (m_current_node)
            edit_annotation(cap_existing, text, kind, *selected_color);
          else
            edit_annotation_on_node(existing_node, cap_existing, text, kind,
                                    *selected_color);
        } else {
          // New annotation: store node-relative offsets
          int node_start = cap_start - seg_base;
          int node_end   = cap_end   - seg_base;
          if (m_current_node)
            add_annotation(cap_start, cap_end, text, kind, *selected_color);
          else
            add_annotation_to_node(target_node, node_start, node_end,
                                   cap_start, cap_end, text, kind,
                                   *selected_color);
        }
        if (m_ann_popover)
          m_ann_popover->popdown();
      });

  btn_row->append(*cancel_btn);
  btn_row->append(*save_btn);
  box->append(*btn_row);

  m_ann_popover->set_child(*box);

  // Position at click point
  double wx = 0, wy = 0;
  auto *root = dynamic_cast<Gtk::Window *>(get_root());
  if (root)
    m_text_view.translate_coordinates(*root, (int)x, (int)y, wx, wy);
  m_ann_popover->set_pointing_to(Gdk::Rectangle((int)wx, (int)wy, 1, 1));
  m_ann_popover->popup();
}

}  // namespace Folio
