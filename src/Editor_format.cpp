// ─────────────────────────────────────────────────────────────────────────────
// Folio — Editor_format.cpp  (FORMAT TU — split from Editor.cpp in s13)
//
// Formatting application: format tags, indent/spacing/font/geometry/style,
// ruler sync, tab stops, writing-mode, outline levels & indicators,
// screenplay element tags, and the tool-menu dialog entry points.
// See the manifest banner in Editor.hpp for the full routing map.
// ─────────────────────────────────────────────────────────────────────────────

#include <Editor.hpp>
#include <Editor_internal.hpp>
#include <EditorHtmlSerializer.hpp>
#include <EditorRuler.hpp>
#include <FolioLog.hpp>
#include <RulerUnits.hpp>
#include <algorithm>
#include <cmath>
#include <pango/pango.h>
#include <set>
#include <sstream>
#include <string>

namespace Folio {
// ─────────────────────────────────────────────────────────────────────────────
// toggle_format_tag / expand_to_paragraphs
// ─────────────────────────────────────────────────────────────────────────────

void Editor::toggle_format_tag(const Glib::RefPtr<Gtk::TextTag> &tag,
                               Gtk::TextBuffer::iterator start,
                               Gtk::TextBuffer::iterator end) {
  bool fully_tagged = (start != end);
  auto it = start;
  while (fully_tagged && it != end) {
    if (!it.has_tag(tag)) {
      fully_tagged = false;
      break;
    }
    it.forward_char();
  }
  m_buffer->begin_user_action();
  if (fully_tagged)
    m_buffer->remove_tag(tag, start, end);
  else
    m_buffer->apply_tag(tag, start, end);
  m_buffer->end_user_action();
}

void Editor::expand_to_paragraphs(Gtk::TextBuffer::iterator &start,
                                  Gtk::TextBuffer::iterator &end) {
  start.set_line_offset(0);
  if (!end.ends_line())
    end.forward_to_line_end();
  if (!end.is_end())
    end.forward_char();
}

// ─────────────────────────────────────────────────────────────────────────────
// buffer_to_html / html_to_buffer — delegated to EditorHtmlSerializer
// ─────────────────────────────────────────────────────────────────────────────

std::string Editor::buffer_to_html() const { return m_serializer->to_html(); }


void Editor::html_to_buffer(const std::string &html) {
  m_serializer->from_html(html);

  // s21 normalize-on-load: a font: tag whose family AND size equal the document
  // default body font is redundant (the base tag governs default body). Drop it
  // over the whole buffer so the base tag governs the body UNIFORMLY; genuine
  // overrides (a run set to a different family/size) keep their tag. Without
  // this, scattered default-tagged runs override the base tag and resize
  // non-uniformly (the "scattered giant characters" bug).
  {
    std::string deftag =
        "font:" + m_current_font + ":" + std::to_string(m_current_font_size);
    if (auto t = m_buffer->get_tag_table()->lookup(deftag))
      m_buffer->remove_tag(t, m_buffer->begin(), m_buffer->end());
  }

  // Re-apply visual style to link:, anchor:, dt:, and concept: tags the
  // serializer created, since tags created by from_html have no visual
  // properties set.
  m_buffer->get_tag_table()->foreach (
      [this](const Glib::RefPtr<Gtk::TextTag> &t) {
        std::string tn = t->property_name().get_value();
        if (tn.size() > 5 && tn.substr(0, 5) == "link:")
          apply_link_tag_style(t);
        else if (tn.size() > 7 && tn.substr(0, 7) == "anchor:")
          apply_anchor_tag_style(t);
        else if (tn.size() > 3 && tn.substr(0, 3) == "dt:")
          apply_dt_tag_style(t);
        else if (tn.size() > 8 && tn.substr(0, 8) == "concept:")
          apply_concept_tag_style(t);
      });
}

// ─────────────────────────────────────────────────────────────────────────────
// Font / geometry helpers
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// apply_indent — applies or removes first-line indent tag across entire buffer
// ─────────────────────────────────────────────────────────────────────────────

void Editor::apply_indent() {
  auto start = m_buffer->begin();
  auto end = m_buffer->end();
  m_loading = true;
  m_buffer->remove_tag(m_tag_indent, start, end);
  if (m_first_line_indent) {
    m_tag_indent->property_indent() = m_first_line_indent_px;
    m_buffer->apply_tag(m_tag_indent, start, end);
  }
  m_loading = false;
  m_line_number_gutter.queue_draw();
}

// Set displayed body size + zoom together, re-stamping the base tag and
// rescaling override tags, then syncing the editor's own controls. FocusWindow
// uses this to show its own size at zoom 1.0 while open (so focus size is the
// literal size, not editor-size × editor-zoom), and to restore the editor's
// pre-focus size+zoom on exit. This is a single synchronous re-stamp — no timed
// restore — so the editor cannot end up disagreeing with the tag.
void Editor::set_body_display(int size_pt, double zoom, int reference_pt) {
  int s = size_pt < 6 ? 6 : (size_pt > 72 ? 72 : size_pt);
  m_current_font_size = s;
  m_zoom_factor = zoom;
  // Styled (font:) runs carry an absolute authored point size, so the body-only
  // base tag can't resize them. When a reference (authored) body size is given,
  // scale them by displayed/authored so they grow WITH the body — the focus
  // sizer then moves the whole document together. reference_pt <= 0 (regular
  // editing, or restore where displayed == authored) leaves them literal.
  m_font_tag_scale =
      (reference_pt > 0) ? (double)s / (double)reference_pt : 1.0;
  apply_base_font_tag();      // base = size × zoom
  apply_zoom_to_font_tags();  // rescale genuine override runs by zoom × scale
  m_updating_font_controls = true;
  if (m_font_size_spin) m_font_size_spin->set_value((double)m_current_font_size);
  if (m_zoom_scale)     m_zoom_scale->set_value(zoom * 100.0);
  m_updating_font_controls = false;
}

void Editor::apply_base_font_tag() {
  // The base tag carries the body font family + size, applied over the whole
  // buffer (lowest priority; per-run font: tags override it). It is the body
  // size carrier — GtkTextView does not honor CSS font-size for body text, so
  // this tag, not view CSS, governs size. (s21: normalize-on-load strips the
  // redundant default font: tags so this governs the body UNIFORMLY.)
  m_tag_base_font->property_family() = m_current_font;
  m_tag_base_font->property_size_points() = m_current_font_size * m_zoom_factor;
  m_loading = true;
  m_buffer->apply_tag(m_tag_base_font, m_buffer->begin(), m_buffer->end());
  m_loading = false;
}

void Editor::apply_zoom_to_font_tags() {
  // Per-character font: tags are named "font:FamilyName:BaseSize" and have
  // higher priority than base_font — so zoom must be applied to them too.
  // We read the base size from the tag name and multiply by m_zoom_factor.
  //
  // FOCUS scaling: a styled run carries an explicit authored size in its tag
  // name, which otherwise scales only with zoom — so the focus size control
  // couldn't resize it. m_font_tag_scale (set by set_body_display, the
  // FocusWindow sizing primitive) multiplies it so styled text scales WITH the
  // focus sizer, proportional to the body. It is 1.0 in regular editing, so
  // regular-mode behaviour is exactly as before.
  const double focus_ratio = m_font_tag_scale;
  auto table = m_buffer->get_tag_table();
  table->foreach ([this, focus_ratio](const Glib::RefPtr<Gtk::TextTag> &tag) {
    std::string n = tag->property_name().get_value();
    if (n.size() <= 5 || n.substr(0, 5) != "font:")
      return;
    // Name format: "font:FamilyName:BaseSize"
    auto last_colon = n.rfind(':');
    if (last_colon == std::string::npos || last_colon == 4)
      return;
    try {
      double base_pt = std::stod(n.substr(last_colon + 1));
      tag->property_size_points() = base_pt * m_zoom_factor * focus_ratio;
    } catch (...) {
    }
  });
  // Also rescale the CSS layer for any text not covered by tags
  double zoomed_size = m_current_font_size * m_zoom_factor;
  std::string sf = m_current_font;
  for (size_t k = 0; k < sf.size(); ++k)
    if (sf[k] == '\'') {
      sf.insert(k, "\\");
      k += 2;
    }
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "textview text { font-family: '%s'; font-size: %.2fpt; }",
                sf.c_str(), zoomed_size);
  if (m_font_css_provider)
    m_font_css_provider->load_from_data(buf);
  // Page width is intentionally NOT scaled — the scroll window has no
  // horizontal scroll, so widening the card would push it off screen.
  // Zoom is purely a font-size operation.
}

void Editor::apply_paragraph_spacing() {
  m_text_view.set_pixels_above_lines(m_paragraph_spacing_px);
  m_line_number_gutter.queue_draw();
}

void Editor::apply_font_prefs(const FolioPrefs &prefs) {
  m_current_font = prefs.editor_font;
  m_current_font_size = prefs.editor_font_size;
  m_typewriter_mode = prefs.typewriter_mode;
  // Sync indent state
  m_first_line_indent = prefs.first_line_indent;
  m_first_line_indent_px = prefs.first_line_indent_px;

  m_tag_indent->property_indent() = m_first_line_indent_px;
  apply_indent();
  // Sync paragraph spacing
  m_paragraph_spacing_px = prefs.paragraph_spacing_px;
  apply_paragraph_spacing();
  // Sync button without re-triggering the toggled signal
  if (m_btn_typewriter.get_active() != m_typewriter_mode)
    m_btn_typewriter.set_active(m_typewriter_mode);
  if (!m_in_focus)
    apply_typewriter_padding();
  // Restore saved page width — updating the shared adjustment syncs both
  // the scrollbar and spinbutton automatically.
  m_page_width_pct = std::max(15, std::min(100, prefs.editor_page_width_pct));
  // Guard all widget set_value / set_selected calls so their signals do NOT
  // call apply_font_to_selection() and corrupt whatever text happens to be
  // selected when the prefs dialog closes or focus mode exits.
  m_updating_font_controls = true;
  if (m_font_size_spin)
    m_font_size_spin->set_value(m_current_font_size);
  if (m_font_dropdown) {
    for (guint i = 0; i < (guint)m_font_names.size(); ++i) {
      if (m_font_names[i] == m_current_font) {
        m_font_dropdown->set_selected(i);
        break;
      }
    }
  }
  // Restore saved zoom level — must come after
  // apply_editor_font/apply_base_font_tag so the zoom multiply runs on top of
  // the freshly-set base sizes.
  if (m_zoom_scale) {
    double saved_zoom =
        std::max(50.0, std::min(300.0, (double)prefs.editor_zoom_pct));
    m_zoom_factor = saved_zoom / 100.0;
    m_zoom_scale->set_value(saved_zoom); // updates label only (guard active)
  }
  m_updating_font_controls = false;
  // Restore header disclosure state
  m_header_revealer.set_reveal_child(prefs.editor_header_visible);
  update_header_toggle_icon();
  apply_editor_font();
  apply_base_font_tag();
  apply_zoom_to_font_tags();
  rebuild_style_dropdown(); // reflect any style changes from prefs
}

void Editor::apply_editor_font() { apply_page_geometry(); }

// ─────────────────────────────────────────────────────────────────────────────
// apply_editing_prefs — re-read substitution settings from m_prefs
// Call after preferences dialog closes to pick up any changes.
// ─────────────────────────────────────────────────────────────────────────────


void Editor::apply_editing_prefs() {
  // ── Text substitution ──────────────────────────────────────────────────────
  // Prefs are read by reference so no sync needed — just ensure connected.
  if (m_substitution && !m_substitution->is_connected())
    m_substitution->connect();

  // ── Sync toolbar toggle to current pref (guard against signal recursion) ──
  if (m_btn_spell_check.get_active() != m_prefs.spell_check_enabled)
    m_btn_spell_check.set_active(m_prefs.spell_check_enabled);

  // ── Spell check ───────────────────────────────────────────────────────────
  if (!m_highlighter)
    return;

  if (!m_prefs.spell_check_enabled) {
    // User turned spell check off — clear all highlights and disconnect
    m_highlighter->clear_highlights();
    m_highlighter->disconnect();
    return;
  }

  // Refresh appearance (color, style, background tint may have changed)
  m_highlighter->refresh_appearance();

  // Reload dictionary if language changed or dict not yet loaded
  if (!m_highlighter->is_connected() ||
      m_highlighter->load_language(m_prefs.spell_language)) {
    m_highlighter->connect();
    m_highlighter->check_all();
  }
}

void Editor::spell_ignore_word(const std::string &word) {
  if (m_highlighter)
    m_highlighter->ignore_word(word);
}

void Editor::spell_add_to_dict(const std::string &word) {
  if (m_highlighter)
    m_highlighter->add_to_dict(word);
}

// ─────────────────────────────────────────────────────────────────────────────
// split_at_cursor
// Splits the current scene at the cursor paragraph boundary.
// The text from the cursor to the end becomes a new sibling scene.
// ─────────────────────────────────────────────────────────────────────────────

// s87 — extract whole <p>…</p> blocks, tolerant of ATTRIBUTES on the opening
// tag. The HTML serializer emits styled paragraphs as <p data-sp="…">, <p
// data-ol="…">, or <p data-anchor="…"> (see EditorHtmlSerializer::para_tag), so
// a literal find("<p>") silently skips them and mis-pairs the </p> tags — which
// garbled every split of any scene that contained a styled paragraph. Matching
// "<p" followed by '>' , whitespace, or '/' catches all paragraph forms while
// not false-matching <pre>/<param>.
static std::vector<std::string> split_html_paragraphs(const std::string &html) {
  std::vector<std::string> paras;
  size_t pos = 0;
  while (pos < html.size()) {
    size_t open = html.find("<p", pos);
    if (open == std::string::npos)
      break;
    const char after = (open + 2 < html.size()) ? html[open + 2] : '\0';
    if (after != '>' && after != ' ' && after != '\t' && after != '\n' &&
        after != '\r' && after != '/') {
      pos = open + 2; // e.g. <pre> / <param> — not a paragraph, keep scanning
      continue;
    }
    size_t close = html.find("</p>", open);
    if (close == std::string::npos)
      break;
    paras.push_back(html.substr(open, close + 4 - open));
    pos = close + 4;
  }
  return paras;
}

void Editor::split_at_cursor() {
  if (!m_current_node || binder_kind_is_group(m_current_node->kind))
    return;
  if (!on_split_requested)
    return;

  // Save buffer → model first
  save_current();

  // Get cursor paragraph index
  auto cursor = m_buffer->get_iter_at_mark(m_buffer->get_insert());
  int cursor_line = cursor.get_line();
  if (cursor_line == 0)
    return; // nothing above cursor to keep

  // Split the stored HTML at the paragraph boundary corresponding to
  // cursor_line. HTML is stored as <p>…</p><p>…</p>… — count paragraphs to find
  // split point.
  const std::string &html = m_current_node->content;
  std::vector<std::string> paras = split_html_paragraphs(html);
  if ((int)paras.size() <= cursor_line)
    return;

  std::string head_html, tail_html;
  for (int i = 0; i < cursor_line; ++i)
    head_html += paras[i];
  for (int i = cursor_line; i < (int)paras.size(); ++i)
    tail_html += paras[i];

  if (head_html.empty())
    head_html = "<p></p>";
  if (tail_html.empty() || tail_html == "<p></p>")
    return;

  // Update current node content directly
  m_current_node->content = head_html;
  m_current_node->content_modified = true;
  m_model.mark_modified();

  int node_id = m_current_node->id;
  std::vector<std::string> new_chunks = {tail_html};
  Glib::signal_idle().connect_once([this, node_id, new_chunks, head_html]() {
    auto path = m_model.path_for_id(Section::Manuscript, node_id);
    auto *node = m_model.node_at(Section::Manuscript, path);
    if (!node)
      return;
    node->content = head_html;
    node->content_modified = true;
    m_current_node = nullptr;
    if (on_split_requested)
      on_split_requested(node, new_chunks);
    // Re-fetch after mutations (add_leaf/move_node invalidate all pointers)
    auto path2 = m_model.path_for_id(Section::Manuscript, node_id);
    auto *node2 = m_model.node_at(Section::Manuscript, path2);
    if (!node2)
      return;
    node2->content = head_html;
    load_node(node2);
  });
}

// ─────────────────────────────────────────────────────────────────────────────
// split_on_separator
// Splits the current scene's content on lines matching `sep`.
// The first chunk stays in the current node; each subsequent chunk
// becomes a new sibling scene.
// ─────────────────────────────────────────────────────────────────────────────

void Editor::split_on_separator(const std::string &sep) {
  if (!m_current_node || binder_kind_is_group(m_current_node->kind))
    return;
  if (!on_split_requested)
    return;
  if (sep.empty())
    return;

  save_current();
  const std::string html = m_current_node->content;

  // Build a set of separator patterns from the comma-separated string.
  // Each pattern is trimmed; empty patterns are ignored.
  // e.g. "---,* * *,###" → {"---", "* * *", "###"}
  std::set<std::string> patterns;
  {
    std::istringstream ss(sep);
    std::string tok;
    while (std::getline(ss, tok, ',')) {
      size_t a = tok.find_first_not_of(" \t");
      size_t b = tok.find_last_not_of(" \t");
      if (a != std::string::npos)
        patterns.insert(tok.substr(a, b - a + 1));
    }
  }
  if (patterns.empty())
    return;

  // Split the stored HTML on </p><p> boundaries, then scan for separator text
  // We work in plain-paragraph units: split HTML into <p>…</p> blocks (tolerant
  // of attributed <p …> tags — see split_html_paragraphs).
  std::vector<std::string> paras = split_html_paragraphs(html);

  if (paras.size() < 2)
    return; // nothing to split

  // Build chunks separated by the separator line
  // A paragraph matches the separator if its plain text (stripped of tags)
  // trims to equal `sep`.
  auto para_text = [](const std::string &p) -> std::string {
    std::string out;
    bool in_tag = false;
    for (unsigned char c : p) {
      if (c == '<') {
        in_tag = true;
        continue;
      }
      if (c == '>') {
        in_tag = false;
        continue;
      }
      if (!in_tag)
        out += static_cast<char>(c);
    }
    // trim
    size_t a = out.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
      return {};
    size_t b = out.find_last_not_of(" \t\r\n");
    return out.substr(a, b - a + 1);
  };

  std::vector<std::vector<std::string>> chunks;
  chunks.push_back({});
  for (auto &p : paras) {
    if (patterns.count(para_text(p))) {
      chunks.push_back({});
    } else {
      chunks.back().push_back(p);
    }
  }

  if (chunks.size() < 2)
    return; // separator not found

  // First chunk stays in the current node
  std::string first_html;
  for (auto &p : chunks[0])
    first_html += p;
  if (first_html.empty())
    first_html = "<p></p>";

  // Update the current node's content directly — no buffer reload here.
  // The idle callback will fire load_node after the sidebar rebuild,
  // which does the full safe buffer reload.
  m_current_node->content = first_html;
  m_current_node->content_modified = true;
  m_model.mark_modified();

  // Build new_chunks for the callback
  std::vector<std::string> new_chunks;
  for (size_t i = 1; i < chunks.size(); ++i) {
    std::string ch;
    for (auto &p : chunks[i])
      ch += p;
    if (ch.empty())
      ch = "<p></p>";
    new_chunks.push_back(ch);
  }

  // Defer to idle — on_split_requested triggers add_leaf/move_node which
  // reallocate the binder vector, invalidating all BinderNode* pointers.
  // Order: fire callback (all mutations) → clear m_current_node → re-fetch
  // by ID → load_node with fresh pointer.
  int node_id = m_current_node->id;
  Glib::signal_idle().connect_once([this, node_id, new_chunks, first_html]() {
    // Re-fetch before any mutation
    auto path = m_model.path_for_id(Section::Manuscript, node_id);
    auto *node = m_model.node_at(Section::Manuscript, path);
    if (!node)
      return;
    // Set trimmed content; survives load_node's internal save_current()
    node->content = first_html;
    node->content_modified = true;
    // Null m_current_node before mutations — add_leaf/move_node reallocate
    // the binder vector, invalidating all raw BinderNode* pointers including
    // m_current_node. save_current() inside load_node checks m_current_node.
    m_current_node = nullptr;
    if (on_split_requested)
      on_split_requested(node, new_chunks);
    // Re-fetch AGAIN after all mutations
    auto path2 = m_model.path_for_id(Section::Manuscript, node_id);
    auto *node2 = m_model.node_at(Section::Manuscript, path2);
    if (!node2)
      return;
    node2->content = first_html; // ensure content survived reallocation
    // Now safe to reload — all vector mutations are complete
    load_node(node2);
  });
}

void Editor::apply_page_geometry() {
  // Never reflow mid-click — changing set_size_request() on the paper card
  // during a button-press/release sequence shifts the vadjustment and causes
  // GTK to register a drag, producing a spurious text selection.
  if (m_mouse_btn_held)
    return;

  // Use the hadjustment's page_size — this is the exact visible viewport width
  // // and is always valid after the first layout pass.
  int scroll_w = 0;
  auto adj = m_write_scroll.get_hadjustment();
  if (adj)
    scroll_w = (int)adj->get_page_size();
  if (scroll_w < 1)
    scroll_w = m_write_scroll.get_width();
  if (scroll_w < 1)
    return; // not yet laid out — signal_map will retry

  int active_pct = m_in_focus ? m_focus_page_width_pct : m_page_width_pct;
  int page_px = std::max(1, (int)(scroll_w * active_pct / 100.0));

  // Page width and margins are unaffected by zoom — zoom is purely font size.
  m_paper_card.set_size_request(page_px, -1);
  int active_left_margin =
      m_in_focus ? m_focus_page_margin_px : m_left_margin_px;
  int active_right_margin =
      m_in_focus ? m_focus_page_margin_px : m_right_margin_px;
  m_paper_inner.set_margin_start(active_left_margin);
  m_paper_inner.set_margin_end(active_right_margin);

  double zoomed_size = m_current_font_size * m_zoom_factor;
  std::string sf = m_current_font;
  for (size_t k = 0; k < sf.size(); ++k)
    if (sf[k] == '\'') {
      sf.insert(k, "\\");
      k += 2;
    }
  char buf[256];
  std::snprintf(buf, sizeof(buf),
                "textview text { font-family: '%s'; font-size: %.2fpt; }",
                sf.c_str(), zoomed_size);
  if (!m_font_css_provider) {
    m_font_css_provider = Gtk::CssProvider::create();
    m_text_view.get_style_context()->add_provider(
        m_font_css_provider, GTK_STYLE_PROVIDER_PRIORITY_USER + 1);
  }
  m_font_css_provider->load_from_data(buf);

  // After resizing the paper card, re-scroll to keep the cursor visible.
  // This ensures that when GTK fires its internal focus-in scroll on the
  // first click, the cursor is already in view and the scroll is a no-op —
  // preventing the viewport from shifting mid-click and creating a
  // spurious text selection.
  if (m_current_node && !m_typewriter_mode && !m_in_focus)
    m_text_view.scroll_to(m_buffer->get_insert(), 0.0);

  // Keep the ruler in sync with the current page geometry
  sync_ruler();
}

// ─────────────────────────────────────────────────────────────────────────────
// apply_paragraph_left_indent / apply_paragraph_right_indent
//
// Applies left/right margin Pango tags to the current paragraph or selection.
// Uses named tags "li:N" and "ri:N" where N is pixels.
//
// Tag removal strategy: iterate the tag table and remove ALL li:/ri: tags from
// the target range.  There are only ever a handful of these (one per distinct
// px value used in the document), so table iteration is cheap.  This is simpler
// and more correct than walking the range with forward_to_tag_toggle, which can
// miss tags whose boundary falls exactly at the range end.
// ─────────────────────────────────────────────────────────────────────────────

static void get_para_range(const Glib::RefPtr<Gtk::TextBuffer> &buf,
                           Gtk::TextBuffer::iterator &start,
                           Gtk::TextBuffer::iterator &end) {
  if (!buf->get_selection_bounds(start, end)) {
    start = end = buf->get_iter_at_mark(buf->get_insert());
  }
  start.set_line_offset(0);
  // Expand end to cover the full last line
  if (end.get_line_offset() > 0 || end == start) {
    if (!end.ends_line())
      end.forward_to_line_end();
    if (!end.is_end())
      end.forward_char(); // include the newline
  }
  // Guarantee non-zero range
  if (start == end && !end.is_end())
    end.forward_char();
}

static void remove_prefix_tags(const Glib::RefPtr<Gtk::TextBuffer> &buf,
                               const std::string &prefix,
                               const Gtk::TextBuffer::iterator &start,
                               const Gtk::TextBuffer::iterator &end) {
  // Collect all tags with the given prefix from the tag table
  std::vector<Glib::RefPtr<Gtk::TextTag>> matches;
  buf->get_tag_table()->foreach ([&](const Glib::RefPtr<Gtk::TextTag> &tag) {
    const std::string &name = tag->property_name().get_value();
    if (name.size() > prefix.size() && name.substr(0, prefix.size()) == prefix)
      matches.push_back(tag);
  });
  for (auto &t : matches)
    buf->remove_tag(t, start, end);
}

void Editor::apply_paragraph_left_indent(int px) {
  Gtk::TextBuffer::iterator start, end;
  get_para_range(m_buffer, start, end);
  remove_prefix_tags(m_buffer, "li:", start, end);

  if (px > 0) {
    auto table = m_buffer->get_tag_table();
    std::string tn = "li:" + std::to_string(px);
    auto tag = table->lookup(tn);
    if (!tag) {
      tag = m_buffer->create_tag(tn);
      tag->property_left_margin() = px;
    }
    m_buffer->apply_tag(tag, start, end);
  }
  sync_ruler();
}

void Editor::apply_paragraph_right_indent(int px) {
  Gtk::TextBuffer::iterator start, end;
  get_para_range(m_buffer, start, end);
  remove_prefix_tags(m_buffer, "ri:", start, end);

  if (px > 0) {
    auto table = m_buffer->get_tag_table();
    std::string tn = "ri:" + std::to_string(px);
    auto tag = table->lookup(tn);
    if (!tag) {
      tag = m_buffer->create_tag(tn);
      tag->property_right_margin() = px;
    }
    m_buffer->apply_tag(tag, start, end);
  }
  sync_ruler();
}

void Editor::sync_ruler() {
  if (!m_prefs.show_ruler)
    return;

  int ruler_w = m_ruler.get_width();
  if (ruler_w < 1)
    return;

  // Translate the paper card's top-left corner into ruler widget space.
  // This gives us the ruler x where the page card begins.
  double card_x_in_ruler = 0.0, dummy = 0.0;
  bool translated =
      m_paper_card.translate_coordinates(m_ruler, 0, 0, card_x_in_ruler, dummy);

  auto card_alloc = m_paper_card.get_allocation();
  int page_px_actual = card_alloc.get_width();

  int page_left_in_ruler = translated ? (int)std::round(card_x_in_ruler)
                                      : (ruler_w - page_px_actual) / 2;

  // Use the text view's actual left edge as the page reference for the ruler.
  // This means ruler zero = left edge of where text can appear (inside card
  // shadow). The "page" in ruler terms is the text view width plus left+right
  // margins. We translate both card and text_view to get correct positions.
  double tv_x_in_ruler = 0.0, dummy2 = 0.0;
  bool tv_translated =
      m_text_view.translate_coordinates(m_ruler, 0, 0, tv_x_in_ruler, dummy2);

  // The ruler page origin = card left (for page edge bars)
  // The margin handle position = tv_x_in_ruler - card_x_in_ruler (from page
  // edge)
  int margin_for_ruler;
  if (tv_translated && translated) {
    margin_for_ruler = (int)std::round(tv_x_in_ruler - card_x_in_ruler);
    margin_for_ruler = std::max(0, margin_for_ruler);
  } else {
    margin_for_ruler =
        28 + (m_in_focus ? m_focus_page_margin_px : m_left_margin_px);
  }

  auto adj = m_write_scroll.get_hadjustment();
  int scroll_w = adj ? (int)adj->get_page_size() : m_write_scroll.get_width();
  int active_pct = m_in_focus ? m_focus_page_width_pct : m_page_width_pct;
  int page_px = page_px_actual > 0
                    ? page_px_actual
                    : std::max(1, (int)(scroll_w * active_pct / 100.0));
  int indent_px = m_first_line_indent ? m_first_line_indent_px : 0;

  // Read li:/ri:/fi: tags for the caret's paragraph to reflect its indents on
  // the ruler. Probe the START of the line: these tags span the whole paragraph,
  // so reading at the first character is reliable even when the caret sits at
  // the paragraph end (where get_tags() on the bare cursor can miss them).
  int left_px = m_left_indent_px;
  int right_px = m_right_indent_px;
  if (m_buffer) {
    auto line_it = m_buffer->get_iter_at_mark(m_buffer->get_insert());
    line_it.set_line_offset(0);
    for (auto &tag : line_it.get_tags()) {
      std::string tn = tag->property_name().get_value();
      if (tn.size() > 3 && tn.substr(0, 3) == "li:")
        try {
          left_px = std::stoi(tn.substr(3));
        } catch (...) {
        }
      else if (tn.size() > 3 && tn.substr(0, 3) == "ri:")
        try {
          right_px = std::stoi(tn.substr(3));
        } catch (...) {
        }
      else if (tn.size() > 3 && tn.substr(0, 3) == "fi:")
        try {
          // Per-style first-line indent (tri-state ≥0) overrides the global
          // first-line indent for this paragraph.
          indent_px = std::stoi(tn.substr(3));
        } catch (...) {
        }
    }
  }

  // The ruler draw code adds TYPE_W to m_page_left_x when drawing.
  // m_page_left_x = (fake_viewport - page_px) / 2
  // We want: TYPE_W + m_page_left_x = page_left_in_ruler
  // So: m_page_left_x = page_left_in_ruler - TYPE_W
  // => (fake_viewport - page_px) / 2 = page_left_in_ruler - TYPE_W
  // => fake_viewport = page_px + 2 * (page_left_in_ruler - TYPE_W)
  int adjusted_left = page_left_in_ruler - EditorRuler::TYPE_W;
  int fake_viewport = page_px + 2 * std::max(0, adjusted_left);
  m_ruler.sync_geometry(fake_viewport, page_px, margin_for_ruler, indent_px,
                        left_px, right_px);
}

void Editor::apply_tab_stops() {
  if (m_prefs.tab_stops.empty()) {
    gtk_text_view_set_tabs(m_text_view.gobj(), nullptr);
    return;
  }

  int n = (int)m_prefs.tab_stops.size();
  PangoTabArray *tabs = pango_tab_array_new(n, TRUE); // positions in pixels
  for (int i = 0; i < n; ++i) {
    const auto &ts = m_prefs.tab_stops[i];
    double px = RulerUnits::pt_to_px(ts.position_pt) + m_page_margin_px;
    // Map type string to PangoTabAlign
    PangoTabAlign align = PANGO_TAB_LEFT;
    if (ts.type == "right")
      align = PANGO_TAB_RIGHT;
    else if (ts.type == "center")
      align = PANGO_TAB_CENTER;
    else if (ts.type == "decimal")
      align = PANGO_TAB_DECIMAL;
    pango_tab_array_set_tab(tabs, i, align, (int)std::round(px));
  }
  gtk_text_view_set_tabs(m_text_view.gobj(), tabs);
  pango_tab_array_free(tabs);
}

void Editor::set_page_width_pct(int pct) {
  pct = std::max(15, std::min(100, pct));
  m_page_width_pct = pct;
  apply_page_geometry();
}

void Editor::apply_font_to_selection() {
  Gtk::TextBuffer::iterator start, end;
  bool has_sel = false;
  if (m_saved_sel_start != -1 && m_saved_sel_end != -1 &&
      m_saved_sel_start != m_saved_sel_end) {
    start = m_buffer->get_iter_at_offset(m_saved_sel_start);
    end = m_buffer->get_iter_at_offset(m_saved_sel_end);
    has_sel = true;
  } else {
    has_sel = m_buffer->get_selection_bounds(start, end);
  }
  if (!has_sel)
    return;

  auto table = m_buffer->get_tag_table();

  // Remove every existing font: tag from the range first — stacked font tags
  // cause ambiguous priority and make size reductions appear to have no effect.
  std::vector<Glib::RefPtr<Gtk::TextTag>> font_tags;
  table->foreach ([&](const Glib::RefPtr<Gtk::TextTag> &t) {
    std::string n = t->property_name().get_value();
    if (n.size() > 5 && n.substr(0, 5) == "font:")
      font_tags.push_back(t);
  });
  for (auto &t : font_tags)
    m_buffer->remove_tag(t, start, end);

  // Encode the regular (non-focus) authored size in the tag name so the
  // tag remains correct after exiting focus mode.
  int canonical_size2 = (m_in_focus && m_saved_font_size > 0)
                            ? m_saved_font_size
                            : m_current_font_size;
  std::string tn =
      "font:" + m_current_font + ":" + std::to_string(canonical_size2);
  auto tt = table->lookup(tn);
  if (!tt) {
    tt = m_buffer->create_tag(tn);
    tt->property_family() = m_current_font;
    tt->property_size_points() = (double)canonical_size2 * m_zoom_factor;
  }
  m_buffer->begin_user_action();
  m_buffer->apply_tag(tt, start, end);
  m_buffer->end_user_action();
}

void Editor::apply_line_spacing_to_selection() {
  Gtk::TextBuffer::iterator start, end;
  bool has_sel = false;
  if (m_saved_sel_start != -1 && m_saved_sel_end != -1 &&
      m_saved_sel_start != m_saved_sel_end) {
    start = m_buffer->get_iter_at_offset(m_saved_sel_start);
    end = m_buffer->get_iter_at_offset(m_saved_sel_end);
    has_sel = true;
  } else {
    has_sel = m_buffer->get_selection_bounds(start, end);
  }
  if (!has_sel)
    return;

  char buf[16];
  std::snprintf(buf, sizeof(buf), "lh:%.1f", m_current_line_spacing);
  std::string tn(buf);
  auto table = m_buffer->get_tag_table();
  auto tag = table->lookup(tn);
  if (!tag) {
    tag = m_buffer->create_tag(tn);
    tag->property_line_height() = (float)m_current_line_spacing;
  }
  table->foreach ([this, &start, &end](const Glib::RefPtr<Gtk::TextTag> &t) {
    std::string n = t->property_name().get_value();
    if (n.size() > 3 && n.substr(0, 3) == "lh:")
      m_buffer->remove_tag(t, start, end);
  });
  m_buffer->begin_user_action();
  m_buffer->apply_tag(tag, start, end);
  m_buffer->end_user_action();
}

void Editor::update_font_controls_from_selection() {
  if (!m_font_dropdown || !m_font_size_spin || !m_line_spacing_spin)
    return;
  // While the format popover is open the user is actively editing font
  // controls — don't let mark_set signals overwrite the spin/dropdown values.
  if (m_format_popover && m_format_popover->get_visible())
    return;
  Gtk::TextBuffer::iterator sel_start, sel_end;
  if (!m_buffer->get_selection_bounds(sel_start, sel_end)) {
    // No selection: probe the formatting at the cursor so the toolbar still
    // reflects the run the caret sits in. Inspect the character to the LEFT of
    // the caret (the run you are typing within); at the very start of the
    // buffer, inspect the character to the right instead. Without this the
    // style dropdown and font controls only updated on a drag-select, never on
    // a plain click.
    auto ci = m_buffer->get_insert()->get_iter();
    sel_start = ci;
    sel_end = ci;
    if (!sel_start.is_start())
      sel_start.backward_char();
    else
      sel_end.forward_char();
  }

  std::string first_family;
  int first_size = -1;
  double first_spacing = -1.0;
  bool multi_family = false, multi_size = false, multi_spacing = false;

  auto it = sel_start;
  while (it != sel_end) {
    std::string hf;
    int hs = -1;
    double hsp = -1.0;
    for (auto &tag : it.get_tags()) {
      std::string tn = tag->property_name().get_value();
      if (tn.size() > 5 && tn.substr(0, 5) == "font:") {
        std::string rest = tn.substr(5);
        auto colon = rest.rfind(':');
        if (colon != std::string::npos) {
          hf = rest.substr(0, colon);
          try {
            hs = std::stoi(rest.substr(colon + 1));
          } catch (...) {
          }
        }
      } else if (tn.size() > 3 && tn.substr(0, 3) == "lh:") {
        try {
          hsp = std::stod(tn.substr(3));
        } catch (...) {
        }
      }
    }
    if (hf.empty())
      hf = m_current_font;
    if (hs < 0)
      hs = m_current_font_size;
    if (hsp < 0.0)
      hsp = m_current_line_spacing;

    if (first_family.empty()) {
      first_family = hf;
      first_size = hs;
      first_spacing = hsp;
    } else {
      if (hf != first_family)
        multi_family = true;
      if (hs != first_size)
        multi_size = true;
      if (std::abs(hsp - first_spacing) > 0.05)
        multi_spacing = true;
    }
    if (multi_family && multi_size && multi_spacing)
      break;
    it.forward_char();
  }

  m_updating_font_controls = true;
  if (multi_family)
    m_font_dropdown->set_selected(m_font_multiple_idx);
  else if (!first_family.empty()) {
    bool found = false;
    for (guint i = 0; i < (guint)m_font_names.size(); ++i) {
      if (m_font_names[i] == first_family) {
        m_font_dropdown->set_selected(i);
        found = true;
        break;
      }
    }
    if (!found)
      m_font_dropdown->set_selected(m_font_multiple_idx);
  }
  if (multi_size)
    m_font_size_spin->set_value(0.0);
  else if (first_size > 0)
    m_font_size_spin->set_value((double)first_size);
  if (multi_spacing)
    m_line_spacing_spin->set_value(0.0);
  else if (first_spacing > 0.0)
    m_line_spacing_spin->set_value(first_spacing);
  m_updating_font_controls = false;

  // ── Sync style dropdown to cursor position ────────────────────────────
  // Scan the tags at the cursor (insert mark) for a "folio-style:NAME"
  // marker tag and update the dropdown to show the matching named style.
  // If no marker tag is present, reset the dropdown to the placeholder.
  if (m_style_dropdown && !m_inhibit_style_dd) {
    std::string found_style_name;
    for (auto &tag : sel_start.get_tags()) {
      std::string tn = tag->property_name().get_value();
      if (tn.size() > 12 && tn.substr(0, 12) == "folio-style:") {
        found_style_name = tn.substr(12);
        break;
      }
    }
    guint target = 0; // placeholder
    if (!found_style_name.empty()) {
      for (guint i = 0; i < (guint)m_prefs.text_styles.size(); ++i) {
        if (m_prefs.text_styles[i].name == found_style_name) {
          target = i + 1;
          break;
        }
      }
    }
    if (m_style_dropdown->get_selected() != target) {
      m_inhibit_style_dd = true;
      m_style_dropdown->set_selected(target);
      m_inhibit_style_dd = false;
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// apply_style
//
// Paragraph style  — expands range to full paragraph boundaries, removes ALL
//                    existing tags, then applies the style properties.
// Character style  — works ONLY on the current selection, removes all tags
//                    in that selection, then applies the style properties.
//
// "Remove all tags" means clearing the tag table's every tag over the range
// so the document is clean before the named style is stamped in.
// ─────────────────────────────────────────────────────────────────────────────

void Editor::apply_style(const TextStyle &style) {
  // Capture the user's caret/selection up front. Applying a style from the
  // toolbar dropdown moves focus out of the text view; when focus returns, GTK
  // resets the insert mark to offset 0, leaving a spurious selection from the
  // old anchor to the top of the scene. We restore these at the end.
  const int orig_ins =
      m_buffer->get_insert()->get_iter().get_offset();
  const int orig_bound =
      m_buffer->get_selection_bound()->get_iter().get_offset();

  // Determine the range to work with
  Gtk::TextBuffer::iterator s, e;
  bool has_sel = m_buffer->get_selection_bounds(s, e);

  // Use saved offsets if the popover is open (no live selection available)
  if (!has_sel && m_saved_sel_start >= 0 &&
      m_saved_sel_end > m_saved_sel_start) {
    s = m_buffer->get_iter_at_offset(m_saved_sel_start);
    e = m_buffer->get_iter_at_offset(m_saved_sel_end);
    has_sel = true;
  }

  if (!has_sel) {
    // No selection: use cursor paragraph
    s = e = m_buffer->get_insert()->get_iter();
  }

  if (style.kind == "paragraph") {
    // Expand to full paragraph boundaries
    expand_to_paragraphs(s, e);
  } else {
    // Character style — must have a real selection
    if (s == e)
      return;
  }

  m_buffer->begin_user_action();

  // ── Step 1: Remove all tags from the range ────────────────────────────
  // Guard m_loading so that signal_apply_tag / signal_remove_tag callbacks
  // (on_text_changed → apply_indent idle) cannot fire mid-operation and
  // re-apply tags we are in the middle of removing, causing accumulation.
  m_loading = true;
  auto table = m_buffer->get_tag_table();
  // Collect tags first — modifying the table while iterating is unsafe.
  std::vector<Glib::RefPtr<Gtk::TextTag>> all_tags;
  table->foreach ([&all_tags](const Glib::RefPtr<Gtk::TextTag> &tag) {
    all_tags.push_back(tag);
  });
  // Remove all existing paragraph/character style tags from the range.
  // Deliberately preserve m_tag_base_font and m_tag_indent — these are
  // document-level layout tags managed independently and should not be
  // clobbered by a named style application.
  for (auto &tag : all_tags) {
    if (tag == m_tag_base_font || tag == m_tag_indent)
      continue;
    m_buffer->remove_tag(tag, s, e);
  }

  // ── Step 2: Apply style properties ───────────────────────────────────

  // Font family + size — use a "font:family:size" tag (same scheme as the
  // format popover) so html serialisation picks it up.
  std::string eff_family =
      style.font_family.empty() ? m_current_font : style.font_family;
  // Use regular editor size as canonical — in focus mode m_current_font_size
  // is the focus size which must not be baked into the tag name.
  int base_size = (m_in_focus && m_saved_font_size > 0) ? m_saved_font_size
                                                        : m_current_font_size;
  int eff_size = (style.font_size <= 0) ? base_size : style.font_size;
  {
    std::string tn = "font:" + eff_family + ":" + std::to_string(eff_size);
    auto ft = table->lookup(tn);
    if (!ft) {
      ft = m_buffer->create_tag(tn);
      ft->property_family() = eff_family;
      ft->property_size_points() = (double)eff_size;
    }
    m_buffer->apply_tag(ft, s, e);
  }

  // Bold
  if (style.bold)
    m_buffer->apply_tag(m_tag_bold, s, e);

  // Italic
  if (style.italic)
    m_buffer->apply_tag(m_tag_italic, s, e);

  // Underline
  if (style.underline)
    m_buffer->apply_tag(m_tag_underline, s, e);

  // Justification (paragraph styles only)
  if (style.kind == "paragraph") {
    if (style.justification == "center")
      m_buffer->apply_tag(m_tag_justify_center, s, e);
    else if (style.justification == "right")
      m_buffer->apply_tag(m_tag_justify_right, s, e);
    else if (style.justification == "full")
      m_buffer->apply_tag(m_tag_justify_full, s, e);
    // "left" or "" — leave (default after clear)
  }

  // Foreground color
  if (!style.fg_color.empty()) {
    Gdk::RGBA rgba;
    rgba.set(style.fg_color);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "fg:%.3f:%.3f:%.3f", rgba.get_red(),
                  rgba.get_green(), rgba.get_blue());
    std::string tn(buf);
    auto ct = table->lookup(tn);
    if (!ct) {
      ct = m_buffer->create_tag(tn);
      ct->property_foreground_rgba() = rgba;
    }
    m_buffer->apply_tag(ct, s, e);
  }

  // Background color
  if (!style.bg_color.empty()) {
    Gdk::RGBA rgba;
    rgba.set(style.bg_color);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "bg:%.3f:%.3f:%.3f", rgba.get_red(),
                  rgba.get_green(), rgba.get_blue());
    std::string tn(buf);
    auto ct = table->lookup(tn);
    if (!ct) {
      ct = m_buffer->create_tag(tn);
      ct->property_background_rgba() = rgba;
    }
    m_buffer->apply_tag(ct, s, e);
  }

  // Line height (paragraph styles only)
  if (style.kind == "paragraph" && style.line_height > 0.1) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", style.line_height);
    std::string tn = std::string("lh:") + buf;
    auto lt = table->lookup(tn);
    if (!lt) {
      lt = m_buffer->create_tag(tn);
      try {
        lt->property_line_height() = (float)style.line_height;
      } catch (...) {
      }
    }
    m_buffer->apply_tag(lt, s, e);
  }

  // Paragraph spacing + first-line indent (paragraph styles only) — s88.
  // These map onto GtkTextTag's pixels_above_lines / pixels_below_lines /
  // indent. Tags are created on demand (so they carry higher priority than the
  // document-wide m_tag_indent created at setup) and named pa:/pb:/fi: so the
  // serializer round-trips them as margin-top / margin-bottom / text-indent.
  if (style.kind == "paragraph") {
    if (style.space_above_px > 0) {
      std::string tn = "pa:" + std::to_string(style.space_above_px);
      auto tt = table->lookup(tn);
      if (!tt) {
        tt = m_buffer->create_tag(tn);
        tt->property_pixels_above_lines() = style.space_above_px;
      }
      m_buffer->apply_tag(tt, s, e);
    }
    if (style.space_below_px > 0) {
      std::string tn = "pb:" + std::to_string(style.space_below_px);
      auto tt = table->lookup(tn);
      if (!tt) {
        tt = m_buffer->create_tag(tn);
        tt->property_pixels_below_lines() = style.space_below_px;
      }
      m_buffer->apply_tag(tt, s, e);
    }
    // First-line indent — tri-state. -1 means "inherit the global indent", so we
    // apply nothing and leave the preserved m_tag_indent to act. >= 0 is an
    // explicit value (0 = none): the fi: tag, being higher priority than
    // m_tag_indent, wins and overrides the global for this paragraph.
    if (style.first_line_indent_px >= 0) {
      std::string tn = "fi:" + std::to_string(style.first_line_indent_px);
      auto tt = table->lookup(tn);
      if (!tt) {
        tt = m_buffer->create_tag(tn);
        tt->property_indent() = style.first_line_indent_px;
      }
      m_buffer->apply_tag(tt, s, e);
    }
  }

  // ── Style name marker tag ─────────────────────────────────────────────
  // A zero-property tag named "folio-style:NAME" is applied over the
  // styled range so the toolbar dropdown can detect the active style when
  // the cursor moves into that range.
  {
    std::string stn = "folio-style:" + style.name;
    auto st = table->lookup(stn);
    if (!st)
      st = m_buffer->create_tag(stn); // no visual properties
    m_buffer->apply_tag(st, s, e);
  }

  m_loading = false;
  // A freshly applied style's font: tag must immediately reflect the current zoom
  // (and focus size ratio) — otherwise it renders at unscaled base size until the
  // next zoom change. Re-stamp now, while still inside the user action.
  apply_zoom_to_font_tags();
  m_buffer->end_user_action();

  // Manually flush the buffer once — the m_loading guard above suppressed
  // all intermediate tag signals, so we fire exactly one save here.
  on_text_changed();

  // ── Sync dropdown to the style we just applied ────────────────────────
  {
    m_inhibit_style_dd = true;
    if (m_style_dropdown) {
      guint match = 0; // 0 = placeholder
      for (guint i = 0; i < (guint)m_prefs.text_styles.size(); ++i) {
        if (m_prefs.text_styles[i].name == style.name) {
          match = i + 1;
          break;
        }
      }
      m_style_dropdown->set_selected(match);
    }
    m_inhibit_style_dd = false;
  }

  // Restore the caret/selection the user had and return focus to the editor.
  // Re-assert on a short timeout to override GTK's focus-in insert-mark reset
  // (which otherwise selects from the top of the scene) — same approach as
  // load_node's caret re-assert.
  auto restore_sel = [this](int a, int b) {
    int n = m_buffer->get_char_count();
    auto ia = m_buffer->get_iter_at_offset(std::min(std::max(0, a), n));
    auto ib = m_buffer->get_iter_at_offset(std::min(std::max(0, b), n));
    m_loading = true;
    m_buffer->select_range(ia, ib); // ia = insert, ib = selection_bound
    m_loading = false;
  };
  m_text_view.grab_focus();
  restore_sel(orig_ins, orig_bound);
  Glib::signal_timeout().connect_once(
      [orig_ins, orig_bound, restore_sel]() {
        restore_sel(orig_ins, orig_bound);
      },
      60);
}

// ─────────────────────────────────────────────────────────────────────────────
// Writing mode
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// Outline level helpers
// ─────────────────────────────────────────────────────────────────────────────

int Editor::current_outline_level() const {
  if (!m_buffer)
    return 0;
  auto iter = m_buffer->get_insert()->get_iter();
  iter.set_line_offset(0);
  for (auto &t : iter.get_tags()) {
    std::string tn = t->property_name().get_value();
    if (tn.size() > 14 && tn.substr(0, 14) == "outline-level-") {
      try {
        return std::stoi(tn.substr(14));
      } catch (...) {
      }
    }
  }
  return 0;
}

void Editor::apply_outline_level(int level) {
  if (!m_buffer)
    return;
  Gtk::TextBuffer::iterator s, e;
  if (!m_buffer->get_selection_bounds(s, e))
    s = e = m_buffer->get_insert()->get_iter();
  s.set_line_offset(0);
  if (!e.ends_line())
    e.forward_to_line_end();
  // Don't set m_loading here — callers manage it themselves
  for (auto &t : m_tag_ol)
    if (t)
      m_buffer->remove_tag(t, s, e);
  if (level >= 1 && level <= MAX_OUTLINE_LEVELS && m_tag_ol[level - 1])
    m_buffer->apply_tag(m_tag_ol[level - 1], s, e);
  if (!m_loading && m_current_node) {
    m_current_node->content = buffer_to_html();
    m_current_node->content_modified = true;
    m_model.mark_modified();
  }
}

void Editor::update_writing_mode_dd() {
  if (m_updating_wm_dd || !m_writing_mode_dd)
    return;

  WritingMode desired;
  if (current_sp_element() >= 0)
    desired = WritingMode::Screenplay;
  else if (current_outline_level() > 0)
    desired = WritingMode::Outline;
  else
    desired = WritingMode::Novel;

  LOG_DEBUG("update_writing_mode_dd: desired={} current={}", (int)desired,
            (int)m_writing_mode);

  if (desired == m_writing_mode)
    return;

  m_writing_mode = desired;
  m_updating_wm_dd = true;
  m_writing_mode_dd->set_selected((guint)desired);
  m_updating_wm_dd = false;

  bool sp = (desired == WritingMode::Screenplay);
  m_btn_sp_help.set_visible(sp);
  m_toolbar.queue_resize();
  LOG_INFO("update_writing_mode_dd: set sp_help visible={}", sp);
}

void Editor::set_writing_mode(WritingMode mode) {
  LOG_INFO("set_writing_mode: mode={}", (int)mode);
  m_writing_mode = mode;

  // Sync dropdown
  if (m_writing_mode_dd) {
    m_updating_wm_dd = true;
    m_writing_mode_dd->set_selected((guint)mode);
    m_updating_wm_dd = false;
  }

  switch (mode) {
  case WritingMode::Novel:
    m_view_stack.set_visible_child("write");
    // Restore font if we're coming back from Screenplay mode
    if (!m_pre_sp_font.empty()) {
      m_current_font = m_pre_sp_font;
      m_current_font_size = m_pre_sp_font_size;
      m_pre_sp_font.clear();
      m_pre_sp_font_size = 0;
      m_loading = true; // suppress signal_apply_tag → on_text_changed cascade
      apply_base_font_tag();
      apply_zoom_to_font_tags();
      m_loading = false;
      if (m_font_dropdown) {
        for (guint i = 0; i < m_font_names.size(); ++i) {
          if (m_font_names[i] == m_current_font) {
            m_updating_font_controls = true;
            m_font_dropdown->set_selected(i);
            m_updating_font_controls = false;
            break;
          }
        }
      }
      if (m_font_size_spin) {
        m_updating_font_controls = true;
        m_font_size_spin->set_value((double)m_current_font_size);
        m_updating_font_controls = false;
      }
    }
    break;
  case WritingMode::Outline:
    m_view_stack.set_visible_child("write");
    // Restore font if coming from Screenplay mode
    if (!m_pre_sp_font.empty()) {
      m_current_font = m_pre_sp_font;
      m_current_font_size = m_pre_sp_font_size;
      m_pre_sp_font.clear();
      m_pre_sp_font_size = 0;
      m_loading = true; // suppress signal_apply_tag → on_text_changed cascade
      apply_base_font_tag();
      apply_zoom_to_font_tags();
      m_loading = false;
      if (m_font_size_spin) {
        m_updating_font_controls = true;
        m_font_size_spin->set_value((double)m_current_font_size);
        m_updating_font_controls = false;
      }
    }
    // If cursor is on a plain line with no outline level, auto-start at level 1
    if (m_buffer && current_outline_level() == 0) {
      Glib::signal_idle().connect_once([this]() {
        if (m_writing_mode == WritingMode::Outline &&
            current_outline_level() == 0)
          replace_line_indicator(1);
      });
    }
    break;
  case WritingMode::Screenplay:
    LOG_INFO("set_writing_mode: Screenplay");
    m_view_stack.set_visible_child("write");
    // Save current font and switch to Courier 12pt (industry standard).
    // Only save once — guard against re-entry if mode is set multiple times.
    if (m_pre_sp_font.empty()) {
      m_pre_sp_font = m_current_font;
      m_pre_sp_font_size = m_current_font_size;
    }
    m_current_font = m_screenplay_font;
    m_current_font_size = 12;
    LOG_INFO("Screenplay font: {} 12pt", m_screenplay_font);
    m_loading = true; // suppress signal_apply_tag → on_text_changed cascade
    apply_base_font_tag();
    apply_zoom_to_font_tags();
    m_loading = false;
    if (m_font_size_spin) {
      m_updating_font_controls = true;
      m_font_size_spin->set_value(12.0);
      m_updating_font_controls = false;
    }
    // If the cursor line has no sp- element yet, auto-sense or default to
    // action.
    if (m_buffer && current_sp_element() < 0) {
      Glib::signal_idle().connect_once([this]() {
        if (m_writing_mode != WritingMode::Screenplay)
          return;
        sp_auto_sense(); // tries to classify; falls back to action if unsure
        if (current_sp_element() < 0)
          apply_sp_element(1); // force action
      });
    }
    break;
  }

  // Show the screenplay reference button only in Screenplay mode
  bool sp_vis = (mode == WritingMode::Screenplay);
  m_btn_sp_help.set_visible(sp_vis);
  m_toolbar.queue_resize();
  LOG_INFO("set_writing_mode: sp_help visible={} toolbar_visible={}", sp_vis,
           m_toolbar.get_visible());
}

void Editor::rebuild_heading_outline() {
  // Clear existing rows
  while (auto *child = m_houtline_box.get_first_child())
    m_houtline_box.remove(*child);

  if (!m_current_node) {
    auto *lbl = Gtk::make_managed<Gtk::Label>("No document open.");
    lbl->add_css_class("houtline-empty");
    lbl->set_halign(Gtk::Align::CENTER);
    lbl->set_margin_top(48);
    m_houtline_box.append(*lbl);
    return;
  }

  // Parse headings from stored HTML
  const std::string &html = m_current_node->content;
  struct HeadingEntry {
    int level;
    std::string text;
    int char_offset;
  };
  std::vector<HeadingEntry> headings;

  size_t pos = 0;
  int plain_offset = 0; // track character offset in plain text
  size_t prev = 0;

  // Quick plain-text counter helper
  auto count_plain = [](const std::string &h, size_t from, size_t to) -> int {
    int n = 0;
    bool in_tag = false;
    for (size_t i = from; i < to && i < h.size(); ++i) {
      if (h[i] == '<') {
        in_tag = true;
        continue;
      }
      if (h[i] == '>') {
        in_tag = false;
        continue;
      }
      if (!in_tag)
        ++n;
    }
    return n;
  };

  while (pos < html.size()) {
    // Find next opening h1/h2/h3 tag
    size_t open = html.find('<', pos);
    if (open == std::string::npos)
      break;
    size_t gt = html.find('>', open);
    if (gt == std::string::npos)
      break;

    std::string tag = html.substr(open + 1, gt - open - 1);
    // lowercase
    for (auto &c : tag)
      c = std::tolower((unsigned char)c);
    int level = 0;
    if (tag == "h1")
      level = 1;
    else if (tag == "h2")
      level = 2;
    else if (tag == "h3")
      level = 3;

    if (level > 0) {
      // Count plain chars from prev to here for offset tracking
      plain_offset += count_plain(html, prev, open);
      prev = gt + 1;

      // Find closing tag
      std::string ctag = "</h" + std::to_string(level) + ">";
      size_t close = html.find(ctag, gt + 1);
      if (close == std::string::npos) {
        pos = gt + 1;
        continue;
      }

      // Extract inner text (strip inner tags)
      std::string inner = html.substr(gt + 1, close - gt - 1);
      std::string text;
      bool in_tag = false;
      for (unsigned char c : inner) {
        if (c == '<') {
          in_tag = true;
          continue;
        }
        if (c == '>') {
          in_tag = false;
          continue;
        }
        if (!in_tag)
          text += (char)c;
      }
      // Decode basic entities
      auto decode = [](std::string s) {
        for (size_t i = 0; i < s.size();) {
          if (s[i] == '&') {
            if (s.substr(i, 5) == "&amp;") {
              s.replace(i, 5, "&");
              continue;
            } else if (s.substr(i, 4) == "&lt;") {
              s.replace(i, 4, "<");
              continue;
            } else if (s.substr(i, 4) == "&gt;") {
              s.replace(i, 4, ">");
              continue;
            }
          }
          ++i;
        }
        return s;
      };
      text = decode(text);

      if (!text.empty())
        headings.push_back({level, text, plain_offset});

      plain_offset += count_plain(html, prev, close);
      prev = close + ctag.size();
      pos = close + ctag.size();
    } else {
      pos = gt + 1;
    }
  }

  if (headings.empty()) {
    auto *lbl = Gtk::make_managed<Gtk::Label>(
        "No headings found.\nUse Ctrl+1/2/3 to add headings.");
    lbl->add_css_class("houtline-empty");
    lbl->set_justify(Gtk::Justification::CENTER);
    lbl->set_halign(Gtk::Align::CENTER);
    lbl->set_margin_top(48);
    m_houtline_box.append(*lbl);
    return;
  }

  // ── Numbering helpers ─────────────────────────────────────────────────────
  // Convert counter to string using the marker type
  auto format_counter = [](int n, const std::string &marker) -> std::string {
    if (marker.empty())
      return "";
    if (marker == "1")
      return std::to_string(n);
    if (marker == "A") {
      std::string s;
      int v = n - 1;
      do {
        s = (char)('A' + v % 26) + s;
        v = v / 26 - 1;
      } while (v >= 0);
      return s;
    }
    if (marker == "a") {
      std::string s;
      int v = n - 1;
      do {
        s = (char)('a' + v % 26) + s;
        v = v / 26 - 1;
      } while (v >= 0);
      return s;
    }
    auto to_roman = [](int n, bool upper) -> std::string {
      static const int vals[] = {1000, 900, 500, 400, 100, 90, 50,
                                 40,   10,  9,   5,   4,   1};
      static const char *lo[] = {"m",  "cm", "d",  "cd", "c",  "xc", "l",
                                 "xl", "x",  "ix", "v",  "iv", "i"};
      static const char *up[] = {"M",  "CM", "D",  "CD", "C",  "XC", "L",
                                 "XL", "X",  "IX", "V",  "IV", "I"};
      const char **sym = upper ? up : lo;
      std::string r;
      for (int i = 0; i < 13; ++i)
        while (n >= vals[i]) {
          r += sym[i];
          n -= vals[i];
        }
      return r;
    };
    if (marker == "I")
      return to_roman(n, true);
    if (marker == "i")
      return to_roman(n, false);
    return std::to_string(n);
  };

  // Track counters per level [0]=H1 [1]=H2 [2]=H3
  int counters[3] = {0, 0, 0};
  int last_level = 0;

  // Build rows
  for (auto &h : headings) {
    int li = h.level - 1; // 0-based index into heading_styles

    // Reset child counters when parent changes
    if (h.level < last_level)
      for (int j = h.level; j < 3; ++j)
        counters[j] = 0;
    else if (h.level > last_level)
      for (int j = last_level; j < li; ++j)
        counters[j] = counters[j] ? counters[j] : 0;

    ++counters[li];
    // Reset deeper levels
    for (int j = li + 1; j < 3; ++j)
      counters[j] = 0;
    last_level = h.level;

    // Build hierarchical prefix: e.g. "I.A.1."
    std::string prefix;
    for (int j = 0; j <= li; ++j) {
      const auto &hs = m_prefs.heading_styles[j];
      if (!hs.marker.empty()) {
        prefix += format_counter(counters[j], hs.marker);
        prefix += hs.separator;
      }
    }

    std::string display = prefix.empty() ? h.text : prefix + "  " + h.text;

    auto *row = Gtk::make_managed<Gtk::Button>();
    row->add_css_class("houtline-row");
    row->add_css_class("flat");
    row->add_css_class("houtline-h" + std::to_string(h.level));

    auto *lbl = Gtk::make_managed<Gtk::Label>(display);
    lbl->set_halign(Gtk::Align::START);
    lbl->set_ellipsize(Pango::EllipsizeMode::END);
    lbl->set_xalign(0.0f);
    row->set_child(*lbl);

    int offset = h.char_offset;
    row->signal_clicked().connect([this, offset] {
      set_writing_mode(WritingMode::Novel);
      if (m_buffer) {
        auto iter = m_buffer->get_iter_at_offset(offset);
        m_buffer->place_cursor(iter);
        m_text_view.scroll_to(iter, 0.15);
      }
    });

    m_houtline_box.append(*row);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Outline prefix insertion / removal

// ─────────────────────────────────────────────────────────────────────────────
// Outline indicator helpers — real editable text, no hidden tags
// ─────────────────────────────────────────────────────────────────────────────

// Compute the indicator string for a given line and level.
// Counts preceding siblings at the same level for the counter.
std::string Editor::compute_indicator(int line, int level) const {
  if (level <= 0 || !m_buffer)
    return "";

  const auto &hs = m_prefs.heading_styles[level - 1];
  if (hs.marker.empty())
    return "";

  // Count how many outline items at this level precede this line
  // (resetting when a shallower level is encountered)
  int count = 0;
  for (int ln = 0; ln < line; ++ln) {
    auto it = m_buffer->get_iter_at_line(ln);
    auto tags = it.get_tags();
    int lv = 0;
    for (auto &t : tags) {
      std::string tn = t->property_name().get_value();
      if (tn.size() > 14 && tn.substr(0, 14) == "outline-level-") {
        try {
          lv = std::stoi(tn.substr(14));
        } catch (...) {
        }
        break;
      }
    }
    if (lv == level)
      ++count;
    else if (lv > 0 && lv < level)
      count = 0; // parent resets child counter
  }
  ++count; // this line is next

  // Format counter using marker style
  auto fmt = [](int n, const std::string &marker) -> std::string {
    if (marker == "1")
      return std::to_string(n);
    if (marker == "A") {
      std::string s;
      int v = n - 1;
      do {
        s = char('A' + v % 26) + s;
        v = v / 26 - 1;
      } while (v >= 0);
      return s;
    }
    if (marker == "a") {
      std::string s;
      int v = n - 1;
      do {
        s = char('a' + v % 26) + s;
        v = v / 26 - 1;
      } while (v >= 0);
      return s;
    }
    auto roman = [](int n, bool up) {
      static const int vals[] = {1000, 900, 500, 400, 100, 90, 50,
                                 40,   10,  9,   5,   4,   1};
      static const char *lo[] = {"m",  "cm", "d",  "cd", "c",  "xc", "l",
                                 "xl", "x",  "ix", "v",  "iv", "i"};
      static const char *hi[] = {"M",  "CM", "D",  "CD", "C",  "XC", "L",
                                 "XL", "X",  "IX", "V",  "IV", "I"};
      const char **s = up ? hi : lo;
      std::string r;
      for (int i = 0; i < 13; ++i)
        while (n >= vals[i]) {
          r += s[i];
          n -= vals[i];
        }
      return r;
    };
    if (marker == "I")
      return roman(n, true);
    if (marker == "i")
      return roman(n, false);
    return std::to_string(n);
  };

  return fmt(count, hs.marker) + hs.separator + " ";
}

// Replace the indicator text at the start of the cursor's line when level
// changes. Strips the old indicator (text up to first non-indicator char) and
// inserts new one.
void Editor::replace_line_indicator(int new_level) {
  if (!m_buffer)
    return;
  int line = m_buffer->get_insert()->get_iter().get_line();
  int old_level = current_outline_level();

  m_loading = true; // hold for entire operation — prevents mode flicker

  // Apply the new outline-level tag
  apply_outline_level(new_level);

  // Strip old indicator from line start
  if (old_level > 0) {
    std::string old_ind = compute_indicator(line, old_level);
    auto ls2 = m_buffer->get_iter_at_line(line);
    auto test = ls2;
    std::string start_text;
    for (int i = 0; i < (int)old_ind.size() && !test.ends_line(); ++i) {
      char buf[7]{};
      g_unichar_to_utf8(test.get_char(), buf);
      start_text += buf;
      test.forward_char();
    }
    if (start_text == old_ind) {
      auto erase_end = m_buffer->get_iter_at_line(line);
      erase_end.forward_chars((int)old_ind.size());
      m_buffer->erase(m_buffer->get_iter_at_line(line), erase_end);
    }
  }

  // Insert new indicator
  if (new_level > 0) {
    std::string new_ind = compute_indicator(line, new_level);
    if (!new_ind.empty()) {
      auto ins = m_buffer->get_iter_at_line(line);
      m_buffer->insert(ins, new_ind);
      // Re-apply level tag over whole line including new indicator
      auto ts = m_buffer->get_iter_at_line(line);
      auto te = ts;
      if (!te.ends_line())
        te.forward_to_line_end();
      Glib::RefPtr<Gtk::TextTag> tag =
          (new_level >= 1 && new_level <= MAX_OUTLINE_LEVELS)
              ? m_tag_ol[new_level - 1]
              : Glib::RefPtr<Gtk::TextTag>{};
      if (tag)
        m_buffer->apply_tag(tag, ts, te);
      m_loading = false;
      // Place cursor after indicator (outside m_loading so mark_set fires once)
      auto cur_pos = m_buffer->get_iter_at_line(line);
      cur_pos.forward_chars((int)new_ind.size());
      m_buffer->place_cursor(cur_pos);
    } else {
      m_loading = false;
    }
  } else {
    m_loading = false;
    m_buffer->place_cursor(m_buffer->get_iter_at_line(line));
  }

  if (m_current_node) {
    m_current_node->content = buffer_to_html();
    m_current_node->content_modified = true;
    m_model.mark_modified();
  }
  // Ensure dropdown reflects current line's mode
  update_writing_mode_dd();
}

// Detect whether the loaded buffer contains outline items and set dropdown
void Editor::detect_writing_mode_from_buffer() {
  if (!m_buffer || !m_writing_mode_dd)
    return;
  int nlines = m_buffer->get_line_count();
  LOG_DEBUG("detect_writing_mode: scanning {} lines", nlines);
  for (int ln = 0; ln < nlines; ++ln) {
    auto it = m_buffer->get_iter_at_line(ln);
    for (auto &t : it.get_tags()) {
      std::string tn = t->property_name().get_value();
      if (tn.size() > 3 && tn.substr(0, 3) == "sp-") {
        LOG_INFO(
            "detect_writing_mode: found sp tag '{}' on line {} → Screenplay",
            tn, ln);
        // Use set_writing_mode() so font switch and all side effects run.
        set_writing_mode(WritingMode::Screenplay);
        return;
      }
      if (tn == "outline-level-1" || tn == "outline-level-2" ||
          tn == "outline-level-3") {
        LOG_INFO("detect_writing_mode: found outline tag '{}' → Outline", tn);
        set_writing_mode(WritingMode::Outline);
        return;
      }
    }
  }
  LOG_DEBUG("detect_writing_mode: no sp/outline tags found → Novel");
  if (m_writing_mode == WritingMode::Outline ||
      m_writing_mode == WritingMode::Screenplay) {
    set_writing_mode(WritingMode::Novel);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Screenplay element names (index matches m_tag_sp order)
// ─────────────────────────────────────────────────────────────────────────────
static const char *SP_ELEMENTS[Editor::SP_COUNT] = {
    "scene", "action", "character", "parenthetical", "dialogue", "transition"};

// ─────────────────────────────────────────────────────────────────────────────
// current_sp_element
// Returns 0-based index of the sp- tag on the cursor line, or -1 if none.
// ─────────────────────────────────────────────────────────────────────────────

int Editor::current_sp_element() const {
  if (!m_buffer)
    return -1;
  auto iter = m_buffer->get_insert()->get_iter();
  iter.set_line_offset(0);
  for (auto &t : iter.get_tags()) {
    std::string tn = t->property_name().get_value();
    if (tn.size() > 3 && tn.substr(0, 3) == "sp-") {
      std::string el = tn.substr(3);
      for (int i = 0; i < SP_COUNT; ++i)
        if (el == SP_ELEMENTS[i])
          return i;
    }
  }
  return -1;
}

// ─────────────────────────────────────────────────────────────────────────────
// clear_sp_tags
// Removes all sp-* paragraph tags from the given range.
// ─────────────────────────────────────────────────────────────────────────────
void Editor::clear_sp_tags(Gtk::TextBuffer::iterator s,
                           Gtk::TextBuffer::iterator e) {
  for (auto &tag : m_tag_sp)
    if (tag)
      m_buffer->remove_tag(tag, s, e);
}

// ─────────────────────────────────────────────────────────────────────────────
// apply_sp_element
// Applies the screenplay element tag for idx to the cursor line, removing any
// existing sp- tag first.  Also handles case enforcement for the line text:
//   scene / character / transition → uppercase the entire line
// ─────────────────────────────────────────────────────────────────────────────
void Editor::apply_sp_element(int idx) {
  if (!m_buffer || idx < 0 || idx >= SP_COUNT)
    return;
  LOG_DEBUG("apply_sp_element: idx={} line={}", idx,
            m_buffer->get_insert()->get_iter().get_line());
  int line = m_buffer->get_insert()->get_iter().get_line();
  auto ls = m_buffer->get_iter_at_line(line);
  auto le = ls;
  if (!le.ends_line())
    le.forward_to_line_end();

  m_loading = true;
  clear_sp_tags(ls, le);
  m_buffer->apply_tag(m_tag_sp[idx], ls, le);

  // Enforce uppercase for scene headings, character cues and transitions.
  bool needs_caps =
      (idx == 0 || idx == 2 || idx == 5); // scene/character/transition
  if (needs_caps) {
    auto ls2 = m_buffer->get_iter_at_line(line);
    auto le2 = ls2;
    if (!le2.ends_line())
      le2.forward_to_line_end();
    std::string text = m_buffer->get_text(ls2, le2);
    std::string upper;
    upper.reserve(text.size());
    for (unsigned char c : text)
      upper += (char)std::toupper(c);
    if (text != upper) {
      int cursor_off = m_buffer->get_insert()->get_iter().get_offset();
      m_buffer->erase(ls2, le2);
      m_buffer->insert(m_buffer->get_iter_at_line(line), upper);
      int new_end =
          m_buffer->get_iter_at_line(line).get_offset() + (int)upper.size();
      m_buffer->place_cursor(
          m_buffer->get_iter_at_offset(std::min(cursor_off, new_end)));
      auto ts = m_buffer->get_iter_at_line(line);
      auto te = ts;
      if (!te.ends_line())
        te.forward_to_line_end();
      m_buffer->apply_tag(m_tag_sp[idx], ts, te);
    }
  }

  m_loading = false;

  // Re-stamp base_font over the line so the zoom factor is correctly applied
  // to any text that was erased+reinserted during uppercase enforcement.
  // This must run after m_loading is cleared so apply_base_font_tag's tag
  // application doesn't trigger on_text_changed.
  {
    int ln = m_buffer->get_insert()->get_iter().get_line();
    auto ts = m_buffer->get_iter_at_line(ln);
    auto te = ts;
    if (!te.ends_line())
      te.forward_to_line_end();
    m_loading = true;
    m_buffer->apply_tag(m_tag_base_font, ts, te);
    m_loading = false;
  }

  // Flush to model now that m_loading is clear (signal_changed was suppressed
  // above)
  if (m_current_node) {
    m_current_node->content = buffer_to_html();
    m_current_node->content_modified = true;
    m_model.mark_modified();
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// sp_tab_next
// Advances (or retreats) to the next element in the user-configured tab cycle.
// Special case: Tab from a character line goes to parenthetical (not the
// cycle).
// ─────────────────────────────────────────────────────────────────────────────
void Editor::sp_tab_next(bool reverse) {
  if (!m_buffer)
    return;
  int cur = current_sp_element();

  // Helper: does the current line have any non-whitespace text?
  auto line_has_content = [this]() -> bool {
    int line = m_buffer->get_insert()->get_iter().get_line();
    auto ls = m_buffer->get_iter_at_line(line);
    auto le = ls;
    if (!le.ends_line())
      le.forward_to_line_end();
    std::string text = m_buffer->get_text(ls, le);
    for (unsigned char c : text)
      if (!std::isspace(c))
        return true;
    return false;
  };

  // Helper: insert a newline and move cursor to the new line.
  // Returns the new element index after insertion.
  auto insert_newline = [this]() {
    auto insert = m_buffer->get_insert()->get_iter();
    // Move to end of current line before inserting
    if (!insert.ends_line())
      insert.forward_to_line_end();
    m_buffer->place_cursor(insert);
    m_buffer->insert_at_cursor("\n");
  };

  // Special: Tab from character → parenthetical (not in main cycle)
  if (!reverse && cur == 2) {
    if (line_has_content()) {
      insert_newline();
    }
    apply_sp_element(3);
    return;
  }
  // Special: Shift+Tab from parenthetical → character
  if (reverse && cur == 3) {
    if (line_has_content()) {
      insert_newline();
    }
    apply_sp_element(2);
    return;
  }

  // Build index into tab cycle list
  const auto &cycle = m_prefs.screenplay_tab_cycle;
  if (cycle.empty())
    return;

  // Find current element in cycle
  std::string cur_name = (cur >= 0) ? SP_ELEMENTS[cur] : "";
  int cycle_pos = -1;
  for (int i = 0; i < (int)cycle.size(); ++i)
    if (cycle[i] == cur_name) {
      cycle_pos = i;
      break;
    }

  int next_pos;
  if (reverse)
    next_pos = (cycle_pos <= 0) ? (int)cycle.size() - 1 : cycle_pos - 1;
  else
    next_pos = (cycle_pos < 0 || cycle_pos >= (int)cycle.size() - 1)
                   ? 0
                   : cycle_pos + 1;

  std::string next_name = cycle[(size_t)next_pos];
  for (int i = 0; i < SP_COUNT; ++i) {
    if (next_name == SP_ELEMENTS[i]) {
      // Insert a new line first if the current line has content, so Tab
      // never overwrites text the writer has already typed.
      if (line_has_content())
        insert_newline();
      apply_sp_element(i);
      return;
    }
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// sp_auto_sense
// Examines the current line and applies a screenplay element tag when the
// content strongly implies one.  Called on idle after every text change.
// Never overwrites a tag the user set deliberately via Tab/Enter — it only
// acts when the line currently has NO sp- tag, or when the tag is sp-action
// (the neutral default) and a stronger signal is present.
// ─────────────────────────────────────────────────────────────────────────────
void Editor::sp_auto_sense() {
  if (!m_buffer || m_loading || m_sp_sensing)
    return;
  if (m_writing_mode != WritingMode::Screenplay)
    return;
  // Guard against re-entry: apply_sp_element sets m_loading, but the
  // erase/insert path inside it temporarily clears m_loading before re-applying
  // the tag. This flag prevents the idle queued by on_text_changed from firing
  // again mid-sense.
  if (!m_tag_sp[0])
    return; // tags not yet created (constructor not complete)

  m_sp_sensing = true;
  struct SenseGuard {
    bool &flag;
    ~SenseGuard() { flag = false; }
  } _g{m_sp_sensing};

  int line = m_buffer->get_insert()->get_iter().get_line();
  auto ls = m_buffer->get_iter_at_line(line);
  auto le = ls;
  if (!le.ends_line())
    le.forward_to_line_end();
  std::string text = m_buffer->get_text(ls, le);
  if (text.empty())
    return;

  int cur = current_sp_element();
  LOG_DEBUG("sp_auto_sense: line={} cur_el={} text='{}'", line, cur, text);
  // Only auto-sense on lines with no element or with the neutral action
  // default. Leave character/dialogue/transition/parenthetical alone once set.
  if (cur >= 0 && cur != 1)
    return; // 1 = action

  // Helper: is the whole string uppercase (ignoring spaces, dots, digits)?
  auto is_all_caps = [](const std::string &s) {
    bool has_alpha = false;
    for (unsigned char c : s) {
      if (std::isalpha(c)) {
        if (std::islower(c))
          return false;
        has_alpha = true;
      }
    }
    return has_alpha;
  };

  // Rule 1: starts with INT. or EXT. → scene heading
  if (text.size() >= 3) {
    std::string pfx3 = text.substr(0, 3);
    std::string pfx4 = (text.size() >= 4) ? text.substr(0, 4) : "";
    for (auto &c : pfx3)
      c = (char)std::toupper((unsigned char)c);
    for (auto &c : pfx4)
      c = (char)std::toupper((unsigned char)c);
    if (pfx4 == "INT." || pfx4 == "EXT." || pfx3 == "I/E") {
      if (cur != 0)
        apply_sp_element(0);
      return;
    }
  }

  // Rule 2: line starts with '(' → parenthetical
  if (text[0] == '(') {
    if (cur != 3)
      apply_sp_element(3);
    return;
  }

  // Rule 3: ALL CAPS, ends with ':', short → transition (e.g. "FADE OUT:")
  if (is_all_caps(text) && text.back() == ':' && (int)text.size() < 30) {
    if (cur != 5)
      apply_sp_element(5);
    return;
  }

  // Rule 4: ALL CAPS, short line, previous line was action or dialogue
  // → character cue candidate
  if (is_all_caps(text) && (int)text.size() < 40 && cur != 2) {
    // Check previous line element
    int prev_el = -1;
    if (line > 0) {
      auto prev_ls = m_buffer->get_iter_at_line(line - 1);
      for (auto &t : prev_ls.get_tags()) {
        std::string tn = t->property_name().get_value();
        if (tn.size() > 3 && tn.substr(0, 3) == "sp-") {
          std::string el = tn.substr(3);
          for (int i = 0; i < SP_COUNT; ++i)
            if (el == SP_ELEMENTS[i]) {
              prev_el = i;
              break;
            }
          break;
        }
      }
    }
    // After action (1) or dialogue (4), an ALL CAPS short line is a character
    // cue
    if (prev_el == 1 || prev_el == 4 || prev_el < 0) {
      apply_sp_element(2);
      return;
    }
  }

  // No strong signal — apply action if line has no element yet
  if (cur < 0)
    apply_sp_element(1);
}

// ── Tools-menu entry points ───────────────────────────────────────────────────

void Editor::open_style_manager() {
  if (!m_style_mgr_dialog) {
    auto *top = dynamic_cast<Gtk::Window *>(get_root());
    if (!top) return;
    m_style_mgr_dialog = std::make_unique<StyleManagerDialog>(*top, m_prefs);
    m_style_mgr_dialog->on_styles_changed = [this]() {
      rebuild_style_dropdown();
    };
    m_style_mgr_dialog->signal_close_request().connect(
        [this]() -> bool { m_style_mgr_dialog.reset(); return false; }, false);
  }
  m_style_mgr_dialog->present();
}

void Editor::open_ruler_manager() {
  if (!m_ruler_manager) {
    auto *root = dynamic_cast<Gtk::Window *>(get_root());
    if (!root) return;
    m_ruler_manager = std::make_unique<RulerManagerDialog>(*root, m_prefs);
    m_ruler_manager->on_geometry_changed = [this]() {
      m_page_width_pct  = m_prefs.editor_page_width_pct;
      m_left_margin_px  = m_prefs.editor_left_margin_px;
      m_right_margin_px = m_prefs.editor_right_margin_px;
      m_page_margin_px  = m_left_margin_px;
      apply_page_geometry();
    };
    m_ruler_manager->on_indent_changed = [this]() {
      m_first_line_indent_px = m_prefs.first_line_indent_px;
      m_tag_indent->property_indent() = m_first_line_indent_px;
      if (m_first_line_indent) apply_indent();
      sync_ruler();
    };
    m_ruler_manager->on_tab_stops_changed = [this]() {
      apply_tab_stops();
      m_ruler.queue_draw();
    };
    m_ruler_manager->on_spacing_changed = [this]() {
      m_paragraph_spacing_px = m_prefs.paragraph_spacing_px;
      apply_paragraph_spacing();
    };
    m_ruler_manager->signal_close_request().connect(
        [this]() -> bool { m_ruler_manager.reset(); return false; }, false);
  }
  m_ruler_manager->present();
}

void Editor::open_screenplay_help() {
  auto *win = dynamic_cast<Gtk::Window *>(get_root());
  if (!win) return;
  if (!m_sp_help_dialog)
    m_sp_help_dialog = std::make_unique<ScreenplayHelpDialog>(*win);
  m_sp_help_dialog->present();
}

}  // namespace Folio
