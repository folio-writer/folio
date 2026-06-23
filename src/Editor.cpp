// ─────────────────────────────────────────────────────────────────────────────
// Folio — Editor.cpp  (CORE TU — split into Editor_{build,format,views,text}.cpp in s13)
//
// Lifecycle + cross-cutting glue: ctor/dtor, node load/save, editor &
// view-mode switching, word count, toast + scroll utilities, status CSS.
// See the manifest banner in Editor.hpp for the full routing map.
// ─────────────────────────────────────────────────────────────────────────────

#include <Editor.hpp>
#include <Editor_internal.hpp>
#include <EditorHtmlSerializer.hpp>
#include <FolioLog.hpp>
#include <Iid.hpp>
#include <ObjectIO.hpp>   // s41 — floor_field_to_leaf for the form write-through
#include <SpellCheckHighlighter.hpp>
#include <TextSubstitution.hpp>
#include <algorithm>
#include <chrono>
#include <string>

namespace Folio {
// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

Editor::~Editor() {
  // Unparent the character picker before the widget tree is torn down.
  if (m_char_picker) {
    m_char_picker->popdown();
    m_char_picker->unparent();
    m_char_picker.reset();
  }
}

Editor::Editor(DocumentModel &model, FolioPrefs &prefs)
    : Gtk::Box(Gtk::Orientation::VERTICAL), m_model(model), m_prefs(prefs),
      m_toolbar(Gtk::Orientation::HORIZONTAL, 1),
      m_paper_card(Gtk::Orientation::VERTICAL, 0),
      m_paper_inner(Gtk::Orientation::VERTICAL, 12),
      m_avatar_strip(Gtk::Orientation::HORIZONTAL, 12),
      m_multi_placeholder_box(Gtk::Orientation::VERTICAL, 16),
      m_footer(Gtk::Orientation::HORIZONTAL, 12), m_map_canvas(model, prefs),
      m_ruler(prefs) {
  set_vexpand(true);
  set_hexpand(true);

  m_buffer = Gtk::TextBuffer::create();
  m_buffer->signal_changed().connect([this]() {
    if (!m_loading)
      on_text_changed();
  });

  // ── Tag-boundary extension ────────────────────────────────────────────────
  // GTK TextBuffer inserts new text *outside* a tag when the cursor sits at
  // the right edge of that tag's range. To prevent formatting from silently
  // dropping, we capture the active format tags just before each insertion and
  // re-apply them to the newly inserted range on the next idle.
  //
  // Only character-level format tags are extended: bold, italic, underline,
  // strikethrough, fg:, bg:, font:. Structural/semantic tags (links, anchors,
  // annotations, headings, base_font, justify, indent) are excluded.
  m_buffer->signal_insert().connect(
      [this](const Gtk::TextBuffer::iterator &pos, const Glib::ustring &text,
             int /*bytes*/) {
        if (m_loading || m_extend_tags_pending)
          return;
        // Only extend on real typing (single char or paste) — not programmatic
        // loads
        if (text.empty())
          return;

        // Look at the character just before the insertion point for active
        // tags. If pos is at offset 0 there's nothing before it — no extension
        // needed.
        auto before = pos;
        if (!before.backward_char())
          return;

        // Collect extendable format tags active at the preceding character
        m_extend_tags.clear();
        for (auto &tag : before.get_tags()) {
          const std::string tn = tag->property_name().get_value();
          // Extend: named format tags and per-char colour/font tags
          if (tn == "bold" || tn == "italic" || tn == "underline" ||
              tn == "strikethrough" ||
              (tn.size() > 3 &&
               (tn.substr(0, 3) == "fg:" || tn.substr(0, 3) == "bg:")) ||
              (tn.size() > 5 && tn.substr(0, 5) == "font:")) {
            m_extend_tags.push_back(tag);
          }
        }

        if (m_extend_tags.empty())
          return;

        m_extend_from = pos.get_offset(); // offset where text was inserted
        m_extend_len = (int)text.size();  // in characters (ustring)
        m_extend_tags_pending = true;

        Glib::signal_idle().connect_once([this]() {
          m_extend_tags_pending = false;
          if (!m_buffer || m_extend_from < 0 || m_extend_len <= 0)
            return;
          int buf_len = m_buffer->get_char_count();
          int from = m_extend_from;
          int to = std::min(from + m_extend_len, buf_len);
          if (from >= to)
            return;
          auto s = m_buffer->get_iter_at_offset(from);
          auto e = m_buffer->get_iter_at_offset(to);
          m_loading = true; // suppress on_text_changed cascade
          for (auto &tag : m_extend_tags)
            m_buffer->apply_tag(tag, s, e);
          m_loading = false;
          // Now let on_text_changed flush the updated content to the model
          on_text_changed();
          m_extend_from = -1;
          m_extend_len = 0;
          m_extend_tags.clear();
        });
      },
      false); // false = connect before default handler

  // Tag apply/remove don't fire signal_changed — connect separately so
  // formatting edits also flush the buffer to the model.
  m_buffer->signal_apply_tag().connect(
      [this](const Glib::RefPtr<Gtk::TextTag> &,
             const Gtk::TextBuffer::iterator &,
             const Gtk::TextBuffer::iterator &) {
        if (!m_loading)
          on_text_changed();
      });
  m_buffer->signal_remove_tag().connect(
      [this](const Glib::RefPtr<Gtk::TextTag> &,
             const Gtk::TextBuffer::iterator &,
             const Gtk::TextBuffer::iterator &) {
        if (!m_loading)
          on_text_changed();
      });

  // Persistent named format tags
  m_tag_bold = m_buffer->create_tag("bold");
  m_tag_italic = m_buffer->create_tag("italic");
  m_tag_underline = m_buffer->create_tag("underline");
  m_tag_strikethrough = m_buffer->create_tag("strikethrough");
  m_tag_justify_left = m_buffer->create_tag("justify_left");
  m_tag_justify_center = m_buffer->create_tag("justify_center");
  m_tag_justify_right = m_buffer->create_tag("justify_right");
  m_tag_justify_full = m_buffer->create_tag("justify_full");

  m_tag_bold->property_weight() = Pango::Weight::BOLD;
  m_tag_italic->property_style() = Pango::Style::ITALIC;
  m_tag_underline->property_underline() = Pango::Underline::SINGLE;
  m_tag_strikethrough->property_strikethrough() = true;
  m_tag_justify_left->property_justification() = Gtk::Justification::LEFT;
  m_tag_justify_center->property_justification() = Gtk::Justification::CENTER;
  m_tag_justify_right->property_justification() = Gtk::Justification::RIGHT;
  m_tag_justify_full->property_justification() = Gtk::Justification::FILL;

  // First-line indent tag — applied to whole buffer, low priority
  m_tag_indent = m_buffer->create_tag("first_line_indent");
  m_tag_indent->property_indent() = m_first_line_indent_px;

  // Joined View divider tag — non-editable, visually distinct
  m_tag_joined_divider = m_buffer->create_tag("joined_divider");
  m_tag_joined_divider->property_editable() = false;
  m_tag_joined_divider->property_foreground() = "#888888";
  m_tag_joined_divider->property_scale() = 0.8;
  m_tag_joined_divider->property_pixels_above_lines() = 8;
  m_tag_joined_divider->property_pixels_below_lines() = 2;

  // Base font tag — applied to whole buffer at lowest priority so every
  // character inherits the user's chosen font/size even when untagged.
  // Per-character font: tags override this because they have higher priority
  // (they are applied after this tag in the tag table order).
  m_tag_base_font = m_buffer->create_tag("base_font");
  m_tag_base_font->property_family() = m_current_font;
  m_tag_base_font->property_size_points() = (double)m_current_font_size;

  // Find/replace highlight tags
  m_tag_find_match = m_buffer->create_tag("find_match");
  m_tag_find_match->property_background() = "#fef08a"; // yellow
  m_tag_find_current = m_buffer->create_tag("find_current");
  m_tag_find_current->property_background() = "#f97316"; // orange
  m_tag_find_current->property_foreground() = "#ffffff";
  // Arrival highlight — briefly highlights the target paragraph after link
  // navigation
  m_tag_link_highlight = m_buffer->create_tag("link-highlight");
  m_tag_link_highlight->property_background() = "#bfdbfe"; // light blue

  m_current_text_color.set_rgba(0.0f, 0.0f, 0.0f, 1.0f);
  m_current_bg_color.set_rgba(1.0f, 1.0f, 0.0f, 1.0f);

  // Seed geometry + mode from prefs before widgets are built
  m_page_width_pct = std::max(15, std::min(100, prefs.editor_page_width_pct));
  m_focus_page_width_pct =
      std::max(15, std::min(100, prefs.focus_page_width_pct));
  m_typewriter_mode = prefs.typewriter_mode;
  m_focus_zoom_factor =
      std::max(0.5, std::min(3.0, prefs.focus_zoom_pct / 100.0));
  m_focus_typewriter = prefs.focus_typewriter_mode;
  m_focus_page_margin_px =
      std::max(8, std::min(300, prefs.focus_page_margin_px));
  m_page_margin_px = std::max(0, std::min(300, prefs.editor_page_margin_px));
  m_left_margin_px = std::max(0, std::min(300, prefs.editor_left_margin_px));
  m_right_margin_px = std::max(0, std::min(300, prefs.editor_right_margin_px));
  m_focus_font = prefs.focus_font;
  m_focus_font_size = prefs.focus_font_size > 0 ? prefs.focus_font_size
                                                : prefs.editor_font_size;
  m_focus_line_spacing = prefs.focus_line_spacing > 0.0
                             ? prefs.focus_line_spacing
                             : prefs.line_spacing;
  m_focus_text_color = prefs.focus_text_color;
  m_show_line_numbers = prefs.show_line_numbers;
  // Seed font from prefs so build_font_controls() selects the right defaults
  m_current_font = prefs.editor_font;
  m_current_font_size = prefs.editor_font_size;
  m_current_line_spacing = prefs.line_spacing;
  m_first_line_indent = prefs.first_line_indent;
  m_first_line_indent_px = prefs.first_line_indent_px;
  m_paragraph_spacing_px = prefs.paragraph_spacing_px;

  // Initialise the HTML serializer now that all tags exist
  // Outline indent level tags — one per level up to MAX_OUTLINE_LEVELS  //
  // Outline indent level tags — one per level up to MAX_OUTLINE_LEVELS
  static const int OL_PX = 40;
  for (int i = 0; i < MAX_OUTLINE_LEVELS; ++i) {
    m_tag_ol[i] =
        m_buffer->create_tag("outline-level-" + std::to_string(i + 1));
    m_tag_ol[i]->property_left_margin() = OL_PX * (i + 1);
    m_tag_ol[i]->property_indent() = 0;
  }

  // ── Screenplay element tags ──────────────────────────────────────────────
  // Margins in pixels, calibrated for a ~680px paper width.
  // Industry standard (Courier 12pt on 8.5" paper, 1.5" left / 1" right):
  //   scene/action  : full width (0 left, 0 right extra)
  //   character cue : left ~170px, right 0  (centred in the text column)
  //   parenthetical : left ~120px, right ~120px
  //   dialogue      : left ~90px,  right ~90px
  //   transition    : right-justified
  //
  // All sp- tags are paragraph-level (applied line-wide) — do NOT set
  // pixels_above/below here; spacing comes from the base text view settings.
  {
    // sp-scene: ALL CAPS + bold, full width
    m_tag_sp[0] = m_buffer->create_tag("sp-scene");
    m_tag_sp[0]->property_weight() = Pango::Weight::BOLD;
    m_tag_sp[0]->property_left_margin() = 0;
    m_tag_sp[0]->property_right_margin() = 0;

    // sp-action: normal weight, full width
    m_tag_sp[1] = m_buffer->create_tag("sp-action");
    m_tag_sp[1]->property_left_margin() = 0;
    m_tag_sp[1]->property_right_margin() = 0;

    // sp-character: ALL CAPS (enforced by auto-sense), indented centre
    m_tag_sp[2] = m_buffer->create_tag("sp-character");
    m_tag_sp[2]->property_left_margin() = 170;
    m_tag_sp[2]->property_right_margin() = 0;

    // sp-parenthetical: indented both sides, italic hint
    m_tag_sp[3] = m_buffer->create_tag("sp-parenthetical");
    m_tag_sp[3]->property_left_margin() = 120;
    m_tag_sp[3]->property_right_margin() = 120;
    m_tag_sp[3]->property_style() = Pango::Style::ITALIC;

    // sp-dialogue: indented both sides
    m_tag_sp[4] = m_buffer->create_tag("sp-dialogue");
    m_tag_sp[4]->property_left_margin() = 90;
    m_tag_sp[4]->property_right_margin() = 90;

    // sp-transition: ALL CAPS, right-justified
    m_tag_sp[5] = m_buffer->create_tag("sp-transition");
    m_tag_sp[5]->property_justification() = Gtk::Justification::RIGHT;
    m_tag_sp[5]->property_left_margin() = 0;
    m_tag_sp[5]->property_right_margin() = 0;
  }

  EditorHtmlSerializer::Tags ser_tags{
      m_tag_bold,          m_tag_italic,       m_tag_underline,
      m_tag_strikethrough, m_tag_justify_left, m_tag_justify_center,
      m_tag_justify_right, m_tag_justify_full, m_tag_ol};
  m_serializer = std::make_unique<EditorHtmlSerializer>(m_buffer, ser_tags);

  // Initialise the text substitution engine and connect it
  m_substitution = std::make_unique<TextSubstitution>(m_buffer, m_prefs);
  m_substitution->connect();

  // Initialise the spell check highlighter and load the configured language
  m_highlighter = std::make_unique<SpellCheckHighlighter>(m_buffer, m_prefs);
  if (m_prefs.spell_check_enabled) {
    if (m_highlighter->load_language(m_prefs.spell_language))
      m_highlighter->connect();
  }

  build_toolbar();
  build_editor_area();
  build_find_bar();
  build_footer();
}

void Editor::load_node(BinderNode *node) {
  LOG_DEBUG("load_node: {} ({})", node ? node->iid : "<null>",
            node ? node->title : "");
  // Block spurious load_node calls fired by on_node_changed during exit_joined
  if (m_exiting_joined) {
    LOG_DEBUG("load_node: suppressed — exiting joined mode");
    return;
  }
  save_current();
  m_current_node = node;
  // s19: name the editing surface by the node it's bound to (editor ↔ iid ↔ log).
  m_text_view.set_name(Folio::widget_name("editor-textview",
                                          node ? node->iid : std::string()));

  // Reset find state — tag positions are buffer-offset based and invalid
  // after a reload. Don't call find_clear_tags here as remove_tag fires
  // signal_remove_tag → on_text_changed which can corrupt node content.
  // The tags become harmless once the buffer is reloaded with new text.
  m_find_matches.clear();
  m_find_current = -1;
  if (m_find_count_lbl)
    m_find_count_lbl->set_text("");

  // Suspend substitutions and spell highlighting during programmatic buffer
  // loads
  if (m_substitution)
    m_substitution->disconnect();
  if (m_highlighter)
    m_highlighter->disconnect();

  // Reset first-click guard — the new node load may change layout, so we
  // suppress notify::width reflows until after the first real click settles.
  m_first_click_done = false;
  // Reset per-node focus-scroll guard so the scroll snapshot fires on the
  // first click after every node switch, not just the very first ever.
  m_first_node_click = true;

  if (!node) {
    m_editor_mode = EditorMode::Empty;
    m_loading = true;
    m_chapter_tag.set_text("");
    m_title_label.set_text("");
    m_chapter_tag_css->load_from_data(""); // clear any colour override
    m_buffer->set_text("");
    m_loading = false;
    set_editor_mode(EditorMode::Empty);
    update_word_count();
    if (m_substitution)
      m_substitution->connect();
    if (m_highlighter && m_prefs.spell_check_enabled &&
        m_highlighter->is_connected())
      m_highlighter->connect();
    return;
  }

  m_loading = true;

  // Apply label colour to the chapter-tag pill. Uses a dedicated CSS provider
  // that is swapped on every load so no providers accumulate.
  refresh_chapter_tag();

  switch (node->kind) {
  case BinderKind::Character:
    m_editor_mode = EditorMode::Character;
    m_chapter_tag.set_text("Character  ·  " +
                           (!node->role.empty() ? node->role : "Character"));
    m_title_label.set_text(node->title.empty() ? "Unnamed" : node->title);
    html_to_buffer(node->content);
    m_avatar_image.set_from_icon_name("avatar-default-symbolic");
    if (!node->image_path.empty()) {
      try {
        auto pixbuf =
            Gdk::Pixbuf::create_from_file(node->image_path, 96, 96, true);
        m_avatar_image.set(Gdk::Texture::create_for_pixbuf(pixbuf));
      } catch (...) {
      }
    }
    break;

  case BinderKind::Place:
    m_editor_mode = EditorMode::Place;
    m_chapter_tag.set_text("Place");
    m_title_label.set_text(node->title.empty() ? "Unnamed" : node->title);
    html_to_buffer(node->content);
    m_avatar_image.set_from_icon_name("image-missing-symbolic");
    if (!node->image_path.empty()) {
      try {
        auto pixbuf =
            Gdk::Pixbuf::create_from_file(node->image_path, 96, 96, true);
        m_avatar_image.set(Gdk::Texture::create_for_pixbuf(pixbuf));
      } catch (...) {
      }
    }
    break;

  case BinderKind::Reference:   // s42 — draws its form as the Editor document
    m_editor_mode = EditorMode::Reference;
    m_chapter_tag.set_text("Reference");
    m_title_label.set_text(node->title.empty() ? "Unnamed" : node->title);
    html_to_buffer(node->content);   // hidden; the form owns the description field
    break;

  default: { // Scene, Group, or Template
    m_editor_mode = EditorMode::Node;
    const char *prefix = node->kind == BinderKind::Template ? "Template  ·  "
                         : node->kind == BinderKind::Scene  ? "Scene  ·  "
                                                            : "Group  ·  ";
    m_chapter_tag.set_text(std::string(prefix) + node->title);
    m_title_label.set_text(node->title.empty() ? "Untitled" : node->title);
    html_to_buffer(node->content);
    break;
  }
  }

  m_loading = false;
  apply_indent();        // re-apply doc-level first-line indent to fresh buffer
  apply_base_font_tag(); // stamp every character with the user's default
                         // font/size
  apply_zoom_to_font_tags();

  // Re-apply heading and outline-level tags AFTER base_font so their
  // size/weight properties take priority over base_font at render time.
  // Also re-apply screenplay sp- tags for the same reason.
  // Guard with m_loading so signal_apply_tag doesn't fire on_text_changed
  // and overwrite node->content with a partially-loaded buffer.
  m_loading = true;
  {
    int nlines = m_buffer->get_line_count();
    for (int ln = 0; ln < nlines; ++ln) {
      auto ls = m_buffer->get_iter_at_line(ln);
      auto le = ls;
      if (!le.ends_line())
        le.forward_to_line_end();
      for (auto &t : ls.get_tags()) {
        std::string tn = t->property_name().get_value();
        if (tn.size() > 3 && tn.substr(0, 3) == "sp-") {
          // Re-apply via tag table lookup so we use the live tag object
          auto live = m_buffer->get_tag_table()->lookup(tn);
          if (live)
            m_buffer->apply_tag(live, ls, le);
          break;
        }
        if (tn.size() > 14 && tn.substr(0, 14) == "outline-level-") {
          int lv = std::stoi(tn.substr(14)) - 1;
          if (lv >= 0 && lv < (int)m_tag_ol.size() && m_tag_ol[lv])
            m_buffer->apply_tag(m_tag_ol[lv], ls, le);
          break;
        }
      }
    }
  }
  m_loading = false;
  rebuild_annotation_tags(); // re-stamp annotation highlights
  // Respect show/hide toolbar state after every node load
  if (!m_prefs.show_annotations)
    refresh_annotation_visibility();
  if (!m_prefs.show_links)
    refresh_link_visibility();
  m_line_number_gutter.queue_draw();
  m_backtrace_gutter.queue_draw();
  m_invis_overlay.queue_draw();
  set_editor_mode(m_editor_mode);
  // s41: a Character/Place draws its template form as the Editor document.
  if (node_is_form_kind(m_current_node))
    populate_object_form();
  update_word_count();

  // Re-enable substitutions and spell check now that the buffer is fully loaded
  if (m_substitution)
    m_substitution->connect();
  if (m_highlighter && m_prefs.spell_check_enabled) {
    m_highlighter->connect();
    m_highlighter->check_all();
  }

  // Apply any saved tab stops to the text view
  apply_tab_stops();

  // Template button visible only for node kinds that can accept a template
  bool show_tpl =
      (node->kind == BinderKind::Scene || node->kind == BinderKind::Group ||
       node->kind == BinderKind::Template);
  m_btn_template.set_visible(show_tpl);

  // Restore saved cursor position, or start at beginning for new nodes.
  // Clamp the saved offset against the actual buffer length so a truncated
  // content doesn't produce an out-of-range iterator.
  // NOTE: placed here synchronously so the buffer has a valid insert mark,
  // but also re-applied in the timeout below after GTK's focus machinery
  // has settled — otherwise GTK resets the cursor to 0 on first focus.
  int saved_cursor = node->cursor_offset;
  {
    int buf_chars = m_buffer->get_char_count();
    if (saved_cursor < 0 || saved_cursor > buf_chars)
      saved_cursor = 0;
    auto it = m_buffer->get_iter_at_offset(saved_cursor);
    m_loading = true;
    m_buffer->select_range(it, it); // clears selection and places cursor
    m_loading = false;
  }

  // Restore scroll position after layout settles (vadjustment upper-bound isn't
  // valid until the text view has been allocated and measured).
  double saved_scroll = node->scroll_value;

  // Allow notify::width reflows after layout settles.
  // Also restore the saved scroll position here — the vadjustment upper-bound
  // only becomes valid once the text view has been measured after layout.
  Glib::signal_timeout().connect_once(
      [this, saved_scroll, saved_cursor]() {
        m_first_click_done = true;

        double vadj_before = 0.0;
        if (auto vadj = m_write_scroll.get_vadjustment())
          vadj_before = vadj->get_value();
        LOG_DEBUG("load_node timeout: saved_cursor={} saved_scroll={:.1f} "
                  "vadj_now={:.1f}",
                  saved_cursor, saved_scroll, vadj_before);

        if (m_geometry_ready)
          apply_page_geometry();

        // Detect writing mode and restore font BEFORE placing cursor, so any
        // font-switch layout reflow happens before we set the final scroll
        // position.
        detect_writing_mode_from_buffer();

        double vadj_after_mode = 0.0;
        if (auto vadj = m_write_scroll.get_vadjustment())
          vadj_after_mode = vadj->get_value();
        LOG_DEBUG("load_node timeout: after detect_writing_mode vadj={:.1f}",
                  vadj_after_mode);

        // Re-apply cursor and explicitly clear any selection GTK's focus
        // machinery may have created by resetting the insert mark to 0.
        {
          int buf_chars = m_buffer->get_char_count();
          int off = (saved_cursor >= 0 && saved_cursor <= buf_chars)
                        ? saved_cursor
                        : 0;
          auto it = m_buffer->get_iter_at_offset(off);
          m_loading = true;
          m_buffer->select_range(it, it); // moves both marks → clears selection
          m_loading = false;
          LOG_DEBUG("load_node timeout: cursor placed at offset={}", off);
        }

        // Restore scroll last — after font reflow and cursor placement.
        if (saved_scroll > 0.0) {
          if (auto vadj = m_write_scroll.get_vadjustment()) {
            double upper = vadj->get_upper() - vadj->get_page_size();
            double target = std::min(saved_scroll, std::max(0.0, upper));
            vadj->set_value(target);
            LOG_DEBUG("load_node timeout: scroll set to {:.1f} (upper={:.1f})",
                      target, upper);
          }
        } else {
          LOG_DEBUG("load_node timeout: no saved scroll, leaving at {:.1f}",
                    vadj_after_mode);
        }

        // s44 — Consume GTK's first-focus insert-mark reset HERE, during load,
        // where no click is in flight to extend it into a selection. The user's
        // first click then lands in an already-focused view: no reset, no
        // spurious "select to end" glob. Re-assert the caret on a short timeout
        // so it lands after GTK's focus-in reset whether that fires sync or async.
        m_text_view.grab_focus();
        Glib::signal_timeout().connect_once(
            [this, saved_cursor, saved_scroll]() {
              if (!m_buffer) return;
              int n = m_buffer->get_char_count();
              int o = (saved_cursor >= 0 && saved_cursor <= n) ? saved_cursor : 0;
              auto it = m_buffer->get_iter_at_offset(o);
              m_loading = true;
              m_buffer->select_range(it, it);   // clear any focus-reset selection
              m_loading = false;
              if (saved_scroll > 0.0) {
                if (auto v = m_write_scroll.get_vadjustment()) {
                  double upper = v->get_upper() - v->get_page_size();
                  v->set_value(std::min(saved_scroll, std::max(0.0, upper)));
                }
              }
            },
            50);
      },
      100);

  // In typewriter/focus mode scroll to cursor centre after layout settles,
  // but only when there is no saved scroll position to restore.
  if ((m_typewriter_mode || m_in_focus) && saved_scroll <= 0.0) {
    queue_scroll_to_center();
  }
}

void Editor::load_empty() { load_node(nullptr); }


void Editor::set_editor_mode(EditorMode mode) {
  m_editor_mode = mode;
  bool is_form_mode =
      (mode == EditorMode::Character || mode == EditorMode::Place ||
       mode == EditorMode::Reference);   // s42 — all three draw forms
  m_avatar_strip.set_visible(is_form_mode);
  m_btn_snapshot.set_visible(m_current_node != nullptr);
  bool is_empty = (mode == EditorMode::Empty);
  // Show the hint overlay when nothing is loaded in Write mode
  m_write_placeholder.set_visible(is_empty && m_view_mode == ViewMode::Write);
  // s41/s42: in Write/Joined, a form-kind (Character/Place/Reference) shows its
  // FORM as the document; every other kind shows the prose write view.
  if (m_view_mode == ViewMode::Write || m_view_mode == ViewMode::Joined) {
    if (is_form_mode)
      m_view_stack.set_visible_child(m_form_scroll);
    else
      m_view_stack.set_visible_child(m_scroll_overlay);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// s41 — the inversion: render the current node's template form as the document.
// Mirrors the write-through the Inspector ran before the form moved here: floor
// fields map to the binder leaf (ObjectIO::floor_field_to_leaf), custom fields
// write to the store object (apply_field). The store is rebuilt before populate
// (binder node = truth, registry = projection) and resolved LIVE in the on_change
// sink — never a held pointer (the objects vector may re-project between renders).
// ─────────────────────────────────────────────────────────────────────────────
void Editor::populate_object_form() {
  if (!m_current_node) { m_object_form.clear(); return; }
  const std::string iid = m_current_node->iid;

  m_model.rebuild_object_store();
  const Folio::ObjectStore& store = m_model.object_store();
  const Folio::Object* obj = store.find_object(iid);
  if (!obj) { m_object_form.clear(); return; }
  const Folio::Template* tmpl = store.find_template(obj->type);
  if (!tmpl) { m_object_form.clear(); return; }

  m_object_form.populate(*tmpl, *obj, /*editable=*/true,
      [this, iid](const std::string& field_id, const Folio::json& raw) {
        if (m_loading || !m_current_node || m_current_node->iid != iid)
          return;
        // s44 — reserved preview-state key (per-instance image height): write
        // straight to the store object's values. It round-trips via ObjectIO and
        // survives the merge-preserving reconcile; it is not a schema field, so it
        // has no leaf home and must not go through floor_field_to_leaf.
        if (field_id.rfind(Folio::kImagePreviewKeyPrefix, 0) == 0) {
          Folio::ObjectStore& st = m_model.object_store();
          if (Folio::Object* mo = st.find_object(iid)) {
            mo->values[field_id] = raw;
            m_model.mark_modified();
          }
          return;
        }
        const std::string s = raw.is_string() ? raw.get<std::string>()
                                              : std::string{};
        switch (Folio::ObjectIO::floor_field_to_leaf(field_id)) {
          case Folio::ObjectIO::LeafField::Title:       m_current_node->title       = s; break;
          case Folio::ObjectIO::LeafField::Content:     m_current_node->content     = s; break;
          case Folio::ObjectIO::LeafField::ImagePath:   m_current_node->image_path  = s; break;
          case Folio::ObjectIO::LeafField::Description: m_current_node->description = s; break;
          case Folio::ObjectIO::LeafField::Role:        m_current_node->role        = s; break;
          case Folio::ObjectIO::LeafField::None: {
            // Custom (object-only) field: no projected leaf home — write THROUGH to
            // the store object, coerced against the field schema. Resolved fresh
            // each fire (no held pointer). Survives the next rebuild's reconcile.
            Folio::ObjectStore& st = m_model.object_store();
            Folio::Object* mo = st.find_object(iid);
            if (!mo) return;
            const Folio::Template* t = st.find_template(mo->type);
            if (!t) return;
            const Folio::FieldSchema* fs = t->find_field(field_id);
            if (!fs) return;
            Folio::apply_field(*mo, *fs, raw);
            m_model.mark_modified();
            return;   // custom fields don't touch the leaf title
          }
        }
        m_model.mark_modified();
        // A floor field changed (title/image/etc.) — refresh chrome that reads the
        // leaf (sidebar title, chapter tag) via the meta-changed path.
        refresh_chapter_tag();
        if (m_on_meta_changed)
          m_on_meta_changed(m_current_node);
      });
}

void Editor::refresh_object_form() {
  if (node_is_form_kind(m_current_node))
    populate_object_form();
}

// ─────────────────────────────────────────────────────────────────────────────
// save_current / on_text_changed / update_word_count
// ─────────────────────────────────────────────────────────────────────────────

void Editor::save_current() {
  if (m_loading)
    return;
  if (m_joined_active) {
    save_joined();
    return;
  }
  if (m_current_node) {
    // s41: form-kind nodes (Character/Place) persist through the ObjectForm's
    // write-through path, not the prose buffer — the buffer is not their editing
    // surface and is hidden. Skip the buffer→content write so a stale hidden
    // buffer can't clobber the form's edits on the next load_node.
    if (node_is_form_kind(m_current_node))
      return;
    m_current_node->content = buffer_to_html();
    // Persist cursor position
    m_current_node->cursor_offset =
        m_buffer->get_insert()->get_iter().get_offset();
    // Persist scroll position
    if (auto vadj = m_write_scroll.get_vadjustment())
      m_current_node->scroll_value = vadj->get_value();
  }
}

// s46 — snapshot the current node through the editor's own load/save path. Flush
// first so the snapshot captures exactly what is on screen (content is normally
// live per keystroke, but the explicit flush also persists cursor/scroll and is
// the architectural "Editor owns load/save" contract FocusWindow leans on). The
// callback drives the Inspector's history refresh.
void Editor::snapshot_current(const std::string& name) {
  if (!m_current_node)
    return;
  save_current();   // buffer -> content (no-op for form-kind nodes / while loading)
  if (node_is_form_kind(m_current_node))
    return;         // prose-only; focus never targets these, but stay defensive
  m_current_node->save_snapshot(name);
  m_model.mark_modified();
  if (m_on_snapshot_saved)
    m_on_snapshot_saved();
  LOG_INFO("snapshot_current: node={} name='{}'", m_current_node->iid, name);
}

void Editor::on_text_changed() {
  if (m_loading)
    return;

  // In Joined mode: mark the segment under the cursor dirty, then
  // debounce-save.
  if (m_joined_active) {
    if (m_loading_joined)
      return; // still loading — don't arm debounce timer
    auto ins = m_buffer->get_iter_at_mark(m_buffer->get_insert());
    int idx = segment_index_at_iter(ins);
    LOG_DEBUG("on_text_changed(joined): cursor_off={} seg_idx={}",
              ins.get_offset(), idx);
    if (idx >= 0 && idx < (int)m_joined_segments.size())
      m_joined_segments[idx].dirty = true;

    // Debounce: reset timer so we save ~800ms after last keystroke
    m_joined_save_conn.disconnect();
    m_joined_save_conn = Glib::signal_timeout().connect(
        [this]() -> bool {
          save_joined();
          if (m_on_session_changed)
            m_on_session_changed();
          return false; // one-shot
        },
        800);
    return;
  }

  // Extend or remove the indent tag after every buffer change.
  // Gated behind !m_mouse_btn_held — firing during a click causes a layout
  // reflow that shifts the viewport and creates a spurious text selection.
  Glib::signal_idle().connect_once([this]() {
    if (!m_mouse_btn_held) {
      apply_indent();
    } else {
      Glib::signal_idle().connect_once([this]() { apply_indent(); });
    }
  });

  // In Screenplay mode re-stamp the base font tag over the current line on
  // every text change so zoom is applied to newly typed characters.
  // Scoped to the current line only (not the whole buffer) for performance.
  if (m_writing_mode == WritingMode::Screenplay && m_buffer) {
    int ln = m_buffer->get_insert()->get_iter().get_line();
    auto ts = m_buffer->get_iter_at_line(ln);
    auto te = ts;
    if (!te.ends_line())
      te.forward_to_line_end();
    m_loading = true;
    m_buffer->apply_tag(m_tag_base_font, ts, te);
    m_loading = false;
  }

  // Auto-sense screenplay element type for the current line.
  // Only triggered from the key handler (Enter/Tab), not on every text change,
  // to avoid an idle-queue cascade (apply_sp_element modifies the buffer,
  // which would re-trigger on_text_changed indefinitely).
  // sp_auto_sense() is called explicitly from the screenplay key handler.

  std::string html = buffer_to_html();
  if (m_current_node) {
    int old_wc = count_words(m_current_node->content);
    m_current_node->content = html;
    m_current_node->content_modified = true;
    m_model.add_session_words(count_words(html) - old_wc);
    m_model.mark_modified();
    update_word_count();
    if (m_on_session_changed)
      m_on_session_changed();
  }
}

bool Editor::detect_dark_mode() const {
  auto style = m_text_view.get_style_context();
  Gdk::RGBA fg = style->get_color();
  double lum =
      0.2126 * fg.get_red() + 0.7152 * fg.get_green() + 0.0722 * fg.get_blue();
  return lum > 0.5; // near-white foreground → dark background
}

void Editor::show_toast(const std::string &message) {
  if (message.empty()) {
    if (m_toast_timer.connected())
      m_toast_timer.disconnect();
    m_toast_revealer.set_reveal_child(false);
    return;
  }
  m_toast_label.set_text(message);
  m_toast_revealer.set_reveal_child(true);
  // Cancel any existing dismiss timer
  if (m_toast_timer.connected())
    m_toast_timer.disconnect();
  m_toast_timer = Glib::signal_timeout().connect(
      [this]() {
        m_toast_revealer.set_reveal_child(false);
        return false; // one-shot
      },
      2000);
}

void Editor::queue_scroll_to_center() {
  // Cancel any previously queued scroll before queuing a new one.
  // This ensures that stale idles queued by load_node or mark_set never
  // fire during a subsequent user click and shift the viewport mid-event.
  if (m_pending_scroll.connected())
    m_pending_scroll.disconnect();
  m_pending_scroll = Glib::signal_idle().connect([this]() {
    scroll_to_cursor_center();
    m_pending_scroll.disconnect();
    return false;
  });
}

void Editor::refresh_chapter_tag() {
  if (!m_chapter_tag_css)
    return;
  std::string hex = m_current_node
                        ? m_prefs.color_hex_for_idx(m_current_node->color_idx)
                        : "";
  if (!hex.empty()) {
    m_chapter_tag_css->load_from_data("* {"
                                      " background-color: " +
                                      hex +
                                      "22;"
                                      " border-color:     " +
                                      hex +
                                      "88;"
                                      " color:            " +
                                      hex +
                                      ";"
                                      "}");
  } else {
    m_chapter_tag_css->load_from_data(""); // revert to theme default
  }
}

void Editor::update_word_count() {
  if (!m_current_node) {
    if (m_wc_label)
      m_wc_label->set_text("—");
    if (m_chars_label)
      m_chars_label->set_text("—");
    if (m_read_label)
      m_read_label->set_text("—");
    return;
  }
  const std::string &content = m_current_node->content;

  int wc = count_words(content);
  int chars = (int)content.size();
  int read_sec = std::max(1, wc / 200);

  if (m_wc_label)
    m_wc_label->set_text(std::to_string(wc));
  if (m_chars_label)
    m_chars_label->set_text(std::to_string(chars));
  if (m_read_label)
    m_read_label->set_text(
        read_sec < 60 ? "<1 min" : std::to_string(read_sec / 60) + " min");
}

// ─────────────────────────────────────────────────────────────────────────────
// set_view_mode / rebuild_outline
// ─────────────────────────────────────────────────────────────────────────────

void Editor::set_view_mode(ViewMode mode) {
  // Leaving Write mode → save and clean up any active JV state
  if (m_joined_active && mode != ViewMode::Write)
    exit_joined();

  m_view_mode = mode;
  bool is_write = (mode == ViewMode::Write || mode == ViewMode::Joined);
  bool is_grid = (mode == ViewMode::Outline);
  m_toolbar.set_visible(is_write);
  m_grid_toolbar.set_visible(is_grid);
  m_ruler.set_visible(is_write && m_prefs.show_ruler);
  switch (mode) {
  case ViewMode::Write:
  case ViewMode::Joined:
    m_write_placeholder.set_visible(m_editor_mode == EditorMode::Empty);
    // s41: form-kind nodes show their form; everything else the prose write view.
    if (node_is_form_kind(m_current_node))
      m_view_stack.set_visible_child(m_form_scroll);
    else
      m_view_stack.set_visible_child(m_scroll_overlay);
    break;
  case ViewMode::Outline:
    rebuild_outline();
    m_view_stack.set_visible_child(m_outline_overlay);
    break;
  case ViewMode::Board:
    m_view_stack.set_visible_child(m_board_overlay);
    break;
  case ViewMode::Map:
    // s48 — the fourth lens. Whole-graph projection; rebuild from the model on
    // entry (truth → projection, never cached across a mutation), then show it.
    m_map_canvas.rebuild();
    m_view_stack.set_visible_child(m_map_canvas);
    break;
  }
}

// s44 — the typewriter rail fraction (0 = top, 0.5 = centre), clamped to a sane
// band so the caret can never be pinned to the very top/bottom (which would defeat
// the platen feel). Set by the floating alt-click slider, persisted in prefs.
double Editor::typewriter_pos() const {
  double p = m_prefs.typewriter_position;
  if (p < 0.15) p = 0.15;
  if (p > 0.85) p = 0.85;
  return p;
}

// s44 — alt-click the typewriter button floats a vertical slider at the editor's
// right edge to set the rail fraction by eye. Toggles visibility; on dismiss the
// value persists. The live value updates prefs + re-rails immediately (only while
// typewriter mode is actually engaged, so pre-setting it while off doesn't jump
// the view).
void Editor::toggle_typewriter_slider() {
  // s44 — the rail slider only exists while typewriter mode is live. Alt-click does
  // NOTHING in normal mode (and never toggles the button — the gesture claims the
  // event so the mode state is untouched). Show/hide only when the rail is on.
  if (!m_typewriter_mode || m_in_focus) {
    if (m_typewriter_pos_slider.get_visible())
      m_typewriter_pos_slider.set_visible(false);
    return;
  }
  bool show = !m_typewriter_pos_slider.get_visible();
  if (show) m_typewriter_pos_slider.set_value(typewriter_pos());
  m_typewriter_pos_slider.set_visible(show);
  if (!show) { try { m_prefs.save(); } catch (...) {} }   // persist on dismiss
}

void Editor::scroll_to_cursor_center() {
  auto vadj = m_write_scroll.get_vadjustment();
  if (!vadj)
    return;
  double viewport_h = vadj->get_page_size();
  if (viewport_h < 1)
    return;

  // Cursor Y in the TextView's buffer coordinate space.
  // Y=0 is the top of the first character, scroll-independent.
  Gtk::TextBuffer::iterator cursor =
      m_buffer->get_iter_at_mark(m_buffer->get_insert());
  Gdk::Rectangle buf_rect;
  m_text_view.get_iter_location(cursor, buf_rect);

  // Build the cursor's absolute Y within the scroll content using
  // widget allocations. get_allocation() returns a widget's position
  // within its *parent's* coordinate space — this is layout-space,
  // not screen-space, so it does NOT shift when the user scrolls.
  //
  // Layout tree:
  //   m_write_scroll  (ScrolledWindow — scroll content starts at Y=0)
  //     m_paper_card  (margin_top px from scroll content top)
  //       m_paper_inner  (alloc.y within paper_card)
  //         [avatar_strip, divider]
  //         m_text_view  (alloc.y within paper_inner)
  //           [top_margin px]
  //           buf_rect.get_y()  ← cursor line top

  auto card_alloc = m_paper_card.get_allocation();
  auto inner_alloc = m_paper_inner.get_allocation();
  auto tv_alloc = m_text_view.get_allocation();

  // card_alloc.get_y() is paper_card's Y within the ScrolledWindow's
  // viewport — it equals paper_card's margin_top when vadj=0, but
  // shifts as the user scrolls. To get the scroll-invariant position
  // we add back vadj->get_value():
  double card_abs_y = (double)card_alloc.get_y() + vadj->get_value();
  // inner_alloc and tv_alloc are within their parent's layout space —
  // they never shift with scroll:
  double inner_abs_y = card_abs_y + (double)inner_alloc.get_y();
  double tv_abs_y = inner_abs_y + (double)tv_alloc.get_y();

  double cursor_abs_y = tv_abs_y + (double)m_text_view.get_top_margin() +
                        (double)buf_rect.get_y() + buf_rect.get_height() / 2.0;

  double target = cursor_abs_y - viewport_h * typewriter_pos();

  double lo = vadj->get_lower();
  double hi = vadj->get_upper() - viewport_h;
  if (hi < lo)
    return;
  target = std::max(lo, std::min(hi, target));

  vadj->set_value(target);
}

// ─────────────────────────────────────────────────────────────────────────────
// update_gutter_width — recalculate the minimum width of the line-number gutter
// based on the number of digits in the longest line number.
// ─────────────────────────────────────────────────────────────────────────────

void Editor::update_gutter_width() {
  if (!m_show_line_numbers) {
    m_line_number_gutter.set_size_request(0, -1);
    m_line_number_gutter.queue_draw();
    return;
  }
  int total = m_buffer->get_line_count();
  // Count digits
  int digits = 1;
  int tmp = total;
  while (tmp >= 10) {
    tmp /= 10;
    ++digits;
  }
  // digits * ~9px + 28px padding (14px right pad + 14px left breathing room)
  int gutter_w = digits * 9 + 32;
  m_line_number_gutter.set_size_request(gutter_w, -1);
  m_line_number_gutter.queue_draw();
}

// ─────────────────────────────────────────────────────────────────────────────
// status_css — returns string literal, never a dangling pointer (README Rule 4)
// ─────────────────────────────────────────────────────────────────────────────

const char *Editor::status_css(NodeStatus s) {
  switch (s) {
  case NodeStatus::RoughDraft:
    return "status-dot-yellow";
  case NodeStatus::InProgress:
    return "badge-chip accent";
  case NodeStatus::Polished:
    return "status-dot-green";
  case NodeStatus::Skip:
    return "status-dot-dim";
  default:
    return "status-dot-dim";
  }
}

}  // namespace Folio
