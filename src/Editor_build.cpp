// ─────────────────────────────────────────────────────────────────────────────
// Folio — Editor_build.cpp  (BUILD TU — split from Editor.cpp in s13)
//
// UI construction: toolbar, font controls, editor area, footer, find bar,
// format popover, and the style / extra-menu rebuilders.
// See the manifest banner in Editor.hpp for the full routing map.
// ─────────────────────────────────────────────────────────────────────────────

#include <Editor.hpp>
#include <Editor_internal.hpp>
#include <FolioLog.hpp>
#include <UnicodePickerPopover.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <pango/pango.h>
#include <pango/pangocairo.h>
#include <regex>
#include <string>

namespace Folio {

namespace {
// This GTK build doesn't reliably apply :checked to GtkToggleButton (see the
// note in css.hpp), so a toolbar toggle's ON look is carried by a code-synced
// .active style class — the same idiom the typewriter and pin toggles already
// use. The .active CSS recolours both the button fill and the symbolic icon
// mask, so a synced toggle reads as clearly ON. Call once per toggle after its
// initial set_active(); the connect keeps the class in sync on every flip,
// including programmatic set_active (e.g. from apply_editing_prefs()).
void sync_active_class(Gtk::ToggleButton& b) {
  auto upd = [&b]() {
    if (b.get_active()) b.add_css_class("active");
    else                b.remove_css_class("active");
  };
  upd();
  b.signal_toggled().connect(upd);
}
}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// build_font_controls  (called first inside build_toolbar)
// ─────────────────────────────────────────────────────────────────────────────

void Editor::build_font_controls() {
  m_font_box.set_orientation(Gtk::Orientation::HORIZONTAL);
  m_font_box.set_spacing(1);
  m_font_box.set_valign(Gtk::Align::CENTER);

  // Enumerate installed font families via PangoFontMap C API
  PangoFontMap *fmap = pango_cairo_font_map_get_default();
  PangoFontFamily **families = nullptr;
  int n_families = 0;
  pango_font_map_list_families(fmap, &families, &n_families);

  m_font_names.clear();
  m_font_names.reserve(n_families);
  for (int i = 0; i < n_families; ++i)
    m_font_names.push_back(pango_font_family_get_name(families[i]));
  g_free(families);

  std::sort(m_font_names.begin(), m_font_names.end(),
            [](const std::string &a, const std::string &b) {
              std::string la = a, lb = b;
              for (auto &c : la)
                c = std::tolower((unsigned char)c);
              for (auto &c : lb)
                c = std::tolower((unsigned char)c);
              return la < lb;
            });

  auto font_model = Gtk::StringList::create({});
  guint default_idx = 0;
  for (guint i = 0; i < (guint)m_font_names.size(); ++i) {
    font_model->append(m_font_names[i]);
    if (m_font_names[i] == m_current_font)
      default_idx = i;
  }
  font_model->append("— multiple —");
  m_font_multiple_idx = (guint)m_font_names.size();

  m_font_dropdown = Gtk::make_managed<Gtk::DropDown>(font_model);
  m_font_dropdown->set_selected(default_idx);
  m_font_dropdown->set_tooltip_text("Font family of selected text");
  m_font_dropdown->set_enable_search(true);
  m_font_dropdown->set_size_request(100, -1);
  m_font_dropdown->property_selected().signal_changed().connect([this]() {
    if (m_updating_font_controls)
      return;
    guint idx = m_font_dropdown->get_selected();
    if (idx < (guint)m_font_names.size()) {
      m_current_font = m_font_names[idx];
      apply_font_to_selection();
      apply_base_font_tag(); // keep base tag in sync so untagged text uses new
                             // font
    }
  });

  m_font_size_spin = Gtk::make_managed<Gtk::SpinButton>();
  m_font_size_spin->set_adjustment(
      Gtk::Adjustment::create(m_current_font_size, 0, 96, 1, 4));
  m_font_size_spin->set_digits(0);
  m_font_size_spin->set_numeric(true);
  m_font_size_spin->set_snap_to_ticks(true);
  m_font_size_spin->set_width_chars(2);
  m_font_size_spin->set_tooltip_text("Font size (pt) — 0 = multiple sizes");
  m_font_size_spin->signal_value_changed().connect([this]() {
    if (m_updating_font_controls)
      return;
    int sz = (int)m_font_size_spin->get_value();
    if (sz > 0) {
      m_current_font_size = sz;
      apply_font_to_selection();
      apply_base_font_tag(); // base tag carries body size (CSS is ignored by
                             // GtkTextView), so this resizes the body
      apply_zoom_to_font_tags();
    }
  });

  // Also snapshot selection when font dropdown changes
  // (both live inside the format popover so m_saved_sel_* is set by
  // signal_show)

  m_line_spacing_spin = Gtk::make_managed<Gtk::SpinButton>();
  m_line_spacing_spin->set_adjustment(
      Gtk::Adjustment::create(m_current_line_spacing, 0.5, 4.0, 0.1, 0.5));
  m_line_spacing_spin->set_digits(1);
  m_line_spacing_spin->set_numeric(true);
  m_line_spacing_spin->set_width_chars(2);
  m_line_spacing_spin->set_tooltip_text(
      "Line spacing — multiplier relative to font size\n"
      "1.0 = single  1.5 = one-and-a-half  2.0 = double");
  m_line_spacing_spin->signal_value_changed().connect([this]() {
    if (m_updating_font_controls)
      return;
    double ls = m_line_spacing_spin->get_value();
    if (ls < 0.5)
      ls = 0.5;
    m_current_line_spacing = ls;
    apply_line_spacing_to_selection();
  });

  m_font_box.append(*m_font_dropdown);
  m_font_box.append(*m_font_size_spin);
  m_font_box.append(*m_line_spacing_spin);
  // m_font_box is appended by build_format_popover(), not the toolbar

  // Wire selection changes → update font controls + typewriter/focus centring
  m_buffer->signal_mark_set().connect(
      [this](const Gtk::TextBuffer::iterator &,
             const Glib::RefPtr<Gtk::TextMark> &mark) {
        if (mark == m_buffer->get_insert() ||
            mark == m_buffer->get_selection_bound()) {
          // Defer to idle so this never runs synchronously inside GTK's own
          // button-press event handling. Also gate behind m_mouse_btn_held —
          // if the button is still held, any font-control update that triggers
          // apply_page_geometry() would shift the viewport mid-drag and create
          // a spurious selection.
          if (!m_font_update_pending && !m_mouse_btn_held) {
            m_font_update_pending = true;
            Glib::signal_idle().connect_once([this]() {
              m_font_update_pending = false;
              if (!m_mouse_btn_held) {
                update_font_controls_from_selection();
                update_writing_mode_dd();
              }
            });
          }
        }
        // Only re-centre on the insert mark (cursor), not selection_bound.
        // Also don't scroll while the format popover is open — focus changes
        // caused by the popover move the insert mark and would jump the view.
        // Defer to idle — scroll_to_cursor_center calls get_allocation() and
        // adjusts the vadjustment synchronously; if this fires inside GTK's
        // button-press handler the scroll shift moves the pointer location,
        // making GTK think the button was pressed and released at different
        // positions and leaving a spurious selection.
        if (mark == m_buffer->get_insert()) {
          if ((m_typewriter_mode || m_in_focus) && !m_mouse_btn_held &&
              !(m_format_popover && m_format_popover->get_visible())) {
            queue_scroll_to_center();
          }
          // Redraw gutter on cursor/scroll changes
          m_line_number_gutter.queue_draw();
          m_backtrace_gutter.queue_draw();
        }
      });

  // signal_changed fires on every text insertion/deletion — mark_set does NOT
  // fire when typing, so we need both signals for typewriter mode.
  m_buffer->signal_changed().connect([this]() {
    if ((m_typewriter_mode || m_in_focus) &&
        !(m_format_popover && m_format_popover->get_visible()))
      queue_scroll_to_center();
  });

  apply_editor_font();

  // Create and attach the font CSS provider eagerly so the user's saved font
  // and size are applied immediately — even before the widget is realised.
  // Without this, apply_page_geometry returns early (scroll_w < 1) on the
  // pre-realisation call from apply_font_prefs, the provider is never created,
  // and the text view falls back to the .paper-body CSS default font.
  if (!m_font_css_provider) {
    m_font_css_provider = Gtk::CssProvider::create();
    m_text_view.get_style_context()->add_provider(
        m_font_css_provider, GTK_STYLE_PROVIDER_PRIORITY_USER + 1);
  }
  {
    std::string sf = m_current_font;
    for (size_t k = 0; k < sf.size(); ++k)
      if (sf[k] == '\'') {
        sf.insert(k, "\\");
        k += 2;
      }
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "textview text { font-family: '%s'; font-size: %.2fpt; }",
                  sf.c_str(), (double)m_current_font_size);
    m_font_css_provider->load_from_data(buf);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// build_toolbar
// ─────────────────────────────────────────────────────────────────────────────

void Editor::build_toolbar() {
  m_toolbar.add_css_class("folio-viewbar");

  build_font_controls(); // still builds the widgets; popover uses them

  auto make_sep = []() {
    auto *s = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL);
    s->set_margin_top(4);
    s->set_margin_bottom(4);
    return s;
  };

  // ── Style picker dropdown — FIRST in toolbar ──────────────────────────
  m_style_dropdown = Gtk::make_managed<Gtk::DropDown>(
      Gtk::StringList::create({"Styleâ¦"}));
  m_style_dropdown->set_size_request(130, -1);
  m_style_dropdown->set_tooltip_text("Apply a named style");
  m_style_dropdown->add_css_class("style-picker-dropdown");
  rebuild_style_dropdown();
  m_style_dropdown->property_selected().signal_changed().connect([this]() {
    if (m_inhibit_style_dd)
      return;
    guint idx = m_style_dropdown->get_selected();
    if (idx == 0 || idx == GTK_INVALID_LIST_POSITION)
      return;
    int si = (int)idx - 1;
    if (si < 0 || si >= (int)m_prefs.text_styles.size())
      return;
    apply_style(m_prefs.text_styles[si]);
    // Dropdown is now updated inside apply_style() itself — no reset needed.
  });
  // ── Writing mode dropdown (Novel / Outline / Screenplay) ─────────────────
  {
    auto model =
        Gtk::StringList::create({"Novel  (Ctrl+Alt+N)", "Outline  (Ctrl+Alt+O)",
                                 "Screenplay  (Ctrl+Alt+S)"});
    m_writing_mode_dd = Gtk::make_managed<Gtk::DropDown>(model);
    m_writing_mode_dd->set_selected(0);
    m_writing_mode_dd->set_tooltip_text("Writing mode");
    m_writing_mode_dd->add_css_class("style-picker-dropdown");
    m_writing_mode_dd->set_size_request(170, -1);
    m_writing_mode_dd->property_selected().signal_changed().connect([this]() {
      if (m_updating_wm_dd)
        return;
      switch (m_writing_mode_dd->get_selected()) {
      case 0:
        set_writing_mode(WritingMode::Novel);
        break;
      case 1:
        set_writing_mode(WritingMode::Outline);
        break;
      case 2:
        set_writing_mode(WritingMode::Screenplay);
        break;
      }
    });
    m_toolbar.append(*m_writing_mode_dd);
    m_toolbar.append(*make_sep());
  }

  m_toolbar.append(*m_style_dropdown);

  // ── Style manager button ──────────────────────────────────────────────
  m_btn_style_mgr = Gtk::make_managed<Gtk::Button>();
  m_btn_style_mgr->set_icon_name("folio-style-mgr-symbolic");
  m_btn_style_mgr->add_css_class("fmt-btn");
  m_btn_style_mgr->set_tooltip_text("Manage stylesâ¦");
  m_btn_style_mgr->signal_clicked().connect([this]() {
    if (!m_style_mgr_dialog) {
      auto *top = dynamic_cast<Gtk::Window *>(get_root());
      if (!top)
        return;
      m_style_mgr_dialog = std::make_unique<StyleManagerDialog>(*top, m_prefs);
      m_style_mgr_dialog->on_styles_changed = [this]() {
        rebuild_style_dropdown();
        if (!m_buffer) return;
        auto table = m_buffer->get_tag_table();

        // For each style, collect all tagged ranges first, then re-apply.
        // We must not modify the buffer while iterating iterators.
        for (const auto &ts : m_prefs.text_styles) {
          std::string stn = "folio-style:" + ts.name;
          auto marker = table->lookup(stn);
          if (!marker) continue;

          // Collect (start_offset, end_offset) pairs for all tagged ranges
          std::vector<std::pair<int,int>> ranges;
          auto it = m_buffer->begin();
          while (it != m_buffer->end()) {
            // Detect start of a tagged range: has tag, and previous char doesn't
            bool at_start = it.has_tag(marker);
            if (at_start) {
              auto prev = it;
              at_start = !prev.backward_char() || !prev.has_tag(marker);
            }
            if (at_start) {
              auto range_start = it;
              auto range_end = it;
              range_end.forward_to_tag_toggle(marker);
              ranges.push_back({range_start.get_offset(),
                                range_end.get_offset()});
              it = range_end;
            } else {
              if (!it.forward_char()) break;
            }
          }

          // Now re-apply the updated style to each collected range
          for (auto &[rs, re] : ranges) {
            auto s = m_buffer->get_iter_at_offset(rs);
            auto e = m_buffer->get_iter_at_offset(re);
            int saved_s = m_saved_sel_start;
            int saved_e = m_saved_sel_end;
            m_saved_sel_start = rs;
            m_saved_sel_end   = re;
            m_buffer->select_range(s, e);
            apply_style(ts);
            m_saved_sel_start = saved_s;
            m_saved_sel_end   = saved_e;
          }
        }
      };
      m_style_mgr_dialog->signal_close_request().connect(
          [this]() -> bool {
            m_style_mgr_dialog.reset();
            return false;
          },
          false);
    }
    m_style_mgr_dialog->present();
  });
  m_toolbar.append(*m_btn_style_mgr);
  m_toolbar.append(*make_sep());

  build_format_popover();

  m_btn_format = Gtk::make_managed<Gtk::MenuButton>();
  m_btn_format->set_icon_name("folio-format-symbolic");
  m_btn_format->add_css_class("style-picker-dropdown");
  m_btn_format->set_tooltip_text("Text formatting");
  m_btn_format->signal_map().connect([this]() {
    if (!m_btn_format->get_popover())
      m_btn_format->set_popover(*m_format_popover);
  });
  m_toolbar.append(*m_btn_format);
  m_toolbar.append(*make_sep());

  auto *spacer = Gtk::make_managed<Gtk::Box>();
  spacer->set_hexpand(true);
  m_toolbar.append(*spacer);

  m_toolbar.append(*make_sep());

  // ── Snapshot button ────────────────────────────────────────────────────────
  m_btn_snapshot.set_label("");
  m_btn_snapshot.set_icon_name("folio-snapshot-symbolic");
  m_btn_snapshot.add_css_class("fmt-btn");
  m_btn_snapshot.set_tooltip_text("Save a snapshot of this content");
  m_btn_snapshot.signal_clicked().connect([this]() {
    if (m_current_node) {
      m_current_node->save_snapshot("Manual snapshot");
      if (m_on_snapshot_saved)
        m_on_snapshot_saved();
      show_toast("📷  Snapshot saved");
    }
  });
  m_toolbar.append(m_btn_snapshot);

  // ── Typewriter mode button ────────────────────────────────────────────────
  m_btn_typewriter.set_label("");
  m_btn_typewriter.set_icon_name("folio-typewriter-symbolic");
  m_btn_typewriter.add_css_class("fmt-btn");
  m_btn_typewriter.set_tooltip_text(
      "Typewriter mode — keeps cursor vertically centred");
  m_btn_typewriter.set_active(m_typewriter_mode);
  if (m_typewriter_mode) m_btn_typewriter.add_css_class("active");  // s44 — :checked unreliable
  m_btn_typewriter.signal_toggled().connect([this]() {
    if (m_tw_toggle_guard) return;   // our own revert below — ignore
    if (m_tw_alt_press) {
      // s44 — Alt+click must NOT change the mode; it only shows the rail slider.
      // GTK4 toggles the button regardless of a claiming gesture, so revert the
      // flip (state untouched) and treat the click as "show adjustment".
      m_tw_alt_press = false;
      m_tw_toggle_guard = true;
      m_btn_typewriter.set_active(m_typewriter_mode);   // undo the visual flip
      m_tw_toggle_guard = false;
      toggle_typewriter_slider();
      return;
    }
    m_typewriter_mode = m_btn_typewriter.get_active();
    // s44 — drive the on-look from code: this GTK build doesn't reliably apply the
    // :checked CSS state to toggle buttons (see css.hpp).
    if (m_typewriter_mode) m_btn_typewriter.add_css_class("active");
    else                   m_btn_typewriter.remove_css_class("active");
    // The rail slider only belongs while the rail is live — hide it (and persist)
    // when typewriter mode turns off.
    if (!m_typewriter_mode && m_typewriter_pos_slider.get_visible()) {
      m_typewriter_pos_slider.set_visible(false);
      try { m_prefs.save(); } catch (...) {}
    }
    if (m_in_focus) {
      m_focus_typewriter = m_typewriter_mode;
      m_prefs.focus_typewriter_mode = m_focus_typewriter;
    } else {
      m_prefs.typewriter_mode = m_typewriter_mode;
    }
    try {
      m_prefs.save();
    } catch (...) {
    }
    if (!m_in_focus)
      apply_typewriter_padding();
    try {
      m_prefs.save();
    } catch (...) {
    }
    if (m_typewriter_mode && !m_in_focus)
      queue_scroll_to_center();   // s44 — defer past the margin relayout
  });
  m_toolbar.append(m_btn_typewriter);

  // s44 — record whether Alt was held at press time so the toggled handler can
  // tell an Alt+click (show slider, keep state) from a plain click (toggle mode).
  // No claim — GTK toggles the button regardless; the handler reverts when Alt.
  {
    auto tw_alt = Gtk::GestureClick::create();
    tw_alt->set_button(GDK_BUTTON_PRIMARY);
    tw_alt->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
    tw_alt->signal_pressed().connect(
        [this, tw_alt](int, double, double) {
          m_tw_alt_press =
              (tw_alt->get_current_event_state() & Gdk::ModifierType::ALT_MASK)
              == Gdk::ModifierType::ALT_MASK;
        });
    m_btn_typewriter.add_controller(tw_alt);
  }

  // ── Line numbers button ───────────────────────────────────────────────────
  m_btn_line_numbers.set_icon_name("folio-line-numbers-symbolic");
  m_btn_line_numbers.add_css_class("fmt-btn");
  m_btn_line_numbers.set_tooltip_text("Show / hide line numbers");
  m_btn_line_numbers.set_active(m_show_line_numbers);
  m_btn_line_numbers.signal_toggled().connect([this]() {
    m_show_line_numbers = m_btn_line_numbers.get_active();
    m_prefs.show_line_numbers = m_show_line_numbers;
    m_line_number_gutter.set_visible(m_show_line_numbers);
    update_gutter_width();
    try {
      m_prefs.save();
    } catch (...) {
    }
  });
  sync_active_class(m_btn_line_numbers);   // s46 — code-driven ON state (:checked unreliable here)
  m_toolbar.append(m_btn_line_numbers);

  // ── Spell check toggle ────────────────────────────────────────────────────
  m_btn_spell_check.set_icon_name("folio-spell-symbolic");
  m_btn_spell_check.add_css_class("fmt-btn");
  m_btn_spell_check.set_tooltip_text("Toggle spell checking");
  m_btn_spell_check.set_active(m_prefs.spell_check_enabled);
  m_btn_spell_check.signal_toggled().connect([this]() {
    m_prefs.spell_check_enabled = m_btn_spell_check.get_active();
    apply_editing_prefs();
    try {
      m_prefs.save();
    } catch (...) {
    }
  });
  sync_active_class(m_btn_spell_check);   // s46 — code-driven ON state (:checked unreliable here)
  m_toolbar.append(m_btn_spell_check);

  // ── Ruler toggle ──────────────────────────────────────────────────────────
  m_btn_ruler.set_icon_name("folio-ruler-symbolic");
  m_btn_ruler.add_css_class("fmt-btn");
  m_btn_ruler.set_tooltip_text("Show / hide ruler");
  m_btn_ruler.set_active(m_prefs.show_ruler);
  m_btn_ruler.signal_toggled().connect([this]() {
    m_prefs.show_ruler = m_btn_ruler.get_active();
    m_ruler.set_visible(m_prefs.show_ruler);
    try {
      m_prefs.save();
    } catch (...) {
    }
  });
  sync_active_class(m_btn_ruler);   // s46 — code-driven ON state (:checked unreliable here)
  m_toolbar.append(m_btn_ruler);

  // ── Show / hide annotations ───────────────────────────────────────────────
  m_btn_show_annotations.set_icon_name("folio-annotations-symbolic");
  m_btn_show_annotations.add_css_class("fmt-btn");
  m_btn_show_annotations.set_tooltip_text("Show / hide annotation highlights");
  m_btn_show_annotations.set_active(m_prefs.show_annotations);
  m_btn_show_annotations.signal_toggled().connect([this]() {
    m_prefs.show_annotations = m_btn_show_annotations.get_active();
    refresh_annotation_visibility();
    try {
      m_prefs.save();
    } catch (...) {
    }
  });
  sync_active_class(m_btn_show_annotations);   // s46 — code-driven ON state (:checked unreliable here)
  m_toolbar.append(m_btn_show_annotations);

  // ── Show / hide hyperlinks ────────────────────────────────────────────────
  m_btn_show_links.set_icon_name("folio-show-links-symbolic");
  m_btn_show_links.add_css_class("fmt-btn");
  m_btn_show_links.set_tooltip_text("Show / hide hyperlink formatting");
  m_btn_show_links.set_active(m_prefs.show_links);
  m_btn_show_links.signal_toggled().connect([this]() {
    m_prefs.show_links = m_btn_show_links.get_active();
    refresh_link_visibility();
    m_backtrace_gutter.queue_draw();
    try {
      m_prefs.save();
    } catch (...) {
    }
  });
  sync_active_class(m_btn_show_links);   // s46 — code-driven ON state (:checked unreliable here)
  m_toolbar.append(m_btn_show_links);

  // ── Show / hide invisible characters ─────────────────────────────────────
  m_btn_show_invisibles.set_icon_name("folio-invisibles-symbolic");
  m_btn_show_invisibles.add_css_class("fmt-btn");
  m_btn_show_invisibles.set_tooltip_text(
      "Show / hide invisible characters (·spaces ¶newlines →tabs)\n"
      "Ctrl+Space = word joiner (zero-width no-break)\n"
      "Ctrl+Shift+Space = non-breaking space\n"
      "Ctrl+Shift+\xe2\x88\x92 = non-breaking hyphen\n"
      "Ctrl+\xe2\x88\x92 = soft hyphen\n"
      "Ctrl+Shift+Z = zero-width space\n"
      "Ctrl+Shift+T = thin space");
  m_btn_show_invisibles.set_active(m_prefs.show_invisibles);
  m_btn_show_invisibles.signal_toggled().connect([this]() {
    m_prefs.show_invisibles = m_btn_show_invisibles.get_active();
    m_invis_overlay.set_visible(m_prefs.show_invisibles);
    m_invis_overlay.queue_draw();
    try {
      m_prefs.save();
    } catch (...) {
    }
  });
  sync_active_class(m_btn_show_invisibles);   // s46 — code-driven ON state (:checked unreliable here)
  m_toolbar.append(m_btn_show_invisibles);

  // ── Screenplay format reference button (only visible in Screenplay mode) ──
  m_btn_sp_help.set_icon_name("folio-sp-help-symbolic");
  m_btn_sp_help.add_css_class("fmt-btn");
  m_btn_sp_help.set_tooltip_text("Screenplay Format Reference (Ctrl+Shift+H)");
  m_btn_sp_help.set_visible(false); // shown only in Screenplay mode
  m_btn_sp_help.signal_clicked().connect([this]() {
    auto *win = dynamic_cast<Gtk::Window *>(get_root());
    if (!win)
      return;
    if (!m_sp_help_dialog)
      m_sp_help_dialog = std::make_unique<ScreenplayHelpDialog>(*win);
    m_sp_help_dialog->present();
  });
  m_toolbar.append(*make_sep());
  m_toolbar.append(m_btn_sp_help);
  m_btn_template.set_icon_name("folio-template-symbolic");
  m_btn_template.add_css_class("fmt-btn");
  m_btn_template.set_tooltip_text("Apply Template…");
  m_btn_template.set_visible(
      false); // shown only when a scene/group/template node is active
  m_btn_template.signal_clicked().connect([this]() {
    if (m_on_template_picker)
      m_on_template_picker(&m_btn_template);
  });
  m_toolbar.append(m_btn_template);

  // ── Insert Link button ────────────────────────────────────────────────────
  auto *link_btn = Gtk::make_managed<Gtk::Button>();
  link_btn->set_icon_name("folio-link-symbolic");
  link_btn->add_css_class("fmt-btn");
  link_btn->set_tooltip_text("Insert internal link (Ctrl+K)");
  link_btn->signal_clicked().connect([this]() { open_link_picker(); });
  m_toolbar.append(*link_btn);

  // ── Focus button ──────────────────────────────────────────────────────────
  auto *focus_btn = Gtk::make_managed<Gtk::Button>();
  focus_btn->set_label("");
  focus_btn->set_icon_name("folio-focus-symbolic");
  focus_btn->add_css_class("fmt-btn");
  focus_btn->set_tooltip_text(
      "Distraction-free full-screen writing (Escape to exit)");
  // Route through the window action so there is ONE entry point into focus mode
  // (the separate FocusWindow), not the retired in-place enter_focus_mode path.
  focus_btn->signal_clicked().connect(
      [focus_btn]() { focus_btn->activate_action("win.focus-mode"); });
  m_toolbar.append(*focus_btn);

  append(m_toolbar);

  // Grid-mode toolbar (shown instead of main toolbar in outline mode)
  m_grid_toolbar.set_orientation(Gtk::Orientation::HORIZONTAL);
  m_grid_toolbar.set_spacing(4);
  m_grid_toolbar.add_css_class("folio-viewbar");
  m_grid_toolbar.set_visible(false);
  {
    auto *lbl = Gtk::make_managed<Gtk::Label>("Show:");
    lbl->add_css_class("stat-label");
    lbl->set_margin_start(8);
    m_grid_toolbar.append(*lbl);

    // Section filter toggle buttons
    struct SFilter {
      const char *label;
      bool *flag;
    };
    for (auto &f : std::vector<SFilter>{{"Manuscript", &m_grid_show_manuscript},
                                        {"Characters", &m_grid_show_characters},
                                        {"Places", &m_grid_show_places}}) {
      auto *btn = Gtk::make_managed<Gtk::ToggleButton>(f.label);
      btn->set_active(*f.flag);
      btn->add_css_class("flat");
      bool *flag = f.flag;
      btn->signal_toggled().connect([this, btn, flag]() {
        *flag = btn->get_active();
        rebuild_outline();
      });
      m_grid_toolbar.append(*btn);
    }

    auto *sep1 = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL);
    sep1->set_margin_start(4);
    sep1->set_margin_end(4);
    m_grid_toolbar.append(*sep1);

    auto *grid_hint = Gtk::make_managed<Gtk::Label>(
        "Ctrl+click binder to cross-select  ·  ☐ = select all  ·  drag empty "
        "area = marquee  ·  "
        "edit any cell in a selected row to change all selected");
    grid_hint->add_css_class("dim-label");
    grid_hint->set_hexpand(true);
    grid_hint->set_margin_start(8);
    grid_hint->set_margin_end(8);
    m_grid_toolbar.append(*grid_hint);
  }
  append(m_grid_toolbar);

  // Ruler sits between toolbar and view stack — only visible when enabled
  m_ruler.set_visible(m_prefs.show_ruler);
  // Sync ruler geometry when it gets its allocation or is resized
  m_ruler.signal_map().connect([this]() { sync_ruler(); });
  g_signal_connect(m_ruler.gobj(), "notify::width",
                   G_CALLBACK(+[](GObject *, GParamSpec *, gpointer ud) {
                     static_cast<Editor *>(ud)->sync_ruler();
                   }),
                   this);
  // Connect ruler signals to editor geometry handlers
  m_ruler.signal_page_width_changed.connect([this](int new_pct) {
    // new_pct is relative to the fake_viewport passed to the ruler, not the
    // real scroll viewport.  Re-derive the actual page pixel width from the
    // card allocation and convert against scroll_w so the round-trip through
    // apply_page_geometry() (which uses scroll_w * pct / 100) is lossless.
    auto card_alloc = m_paper_card.get_allocation();
    int card_px = card_alloc.get_width();
    int scroll_w = 0;
    auto adj = m_write_scroll.get_hadjustment();
    if (adj)
      scroll_w = (int)adj->get_page_size();
    if (scroll_w < 1)
      scroll_w = m_write_scroll.get_width();
    if (card_px > 0 && scroll_w > 0) {
      int real_pct = std::max(
          15, std::min(100, (int)std::round(card_px * 100.0 / scroll_w)));
      m_page_width_pct = real_pct;
    } else {
      m_page_width_pct = std::max(15, std::min(100, new_pct));
    }
    m_prefs.editor_page_width_pct = m_page_width_pct;
    if (m_ruler_manager)
      m_ruler_manager->refresh();
    apply_page_geometry();
    try {
      m_prefs.save();
    } catch (...) {
    }
  });
  m_ruler.signal_margin_changed.connect([this](int new_margin) {
    m_left_margin_px = new_margin;
    m_right_margin_px = new_margin; // ruler only moves both together
    m_page_margin_px = new_margin;
    m_prefs.editor_left_margin_px = new_margin;
    m_prefs.editor_right_margin_px = new_margin;
    m_prefs.editor_page_margin_px = new_margin;
    if (m_ruler_manager)
      m_ruler_manager->refresh();
    apply_page_geometry();
    try {
      m_prefs.save();
    } catch (...) {
    }
  });
  m_ruler.signal_first_indent_changed.connect([this](int new_indent) {
    m_first_line_indent_px = new_indent;
    m_prefs.first_line_indent_px = new_indent;
    m_tag_indent->property_indent() = new_indent;
    if (m_first_line_indent)
      apply_indent();
    sync_ruler();
    try {
      m_prefs.save();
    } catch (...) {
    }
  });
  m_ruler.signal_left_indent_changed.connect([this](int new_indent) {
    m_left_indent_px = new_indent;
    apply_paragraph_left_indent(new_indent);
    sync_ruler();
    try {
      m_prefs.save();
    } catch (...) {
    }
  });
  m_ruler.signal_right_indent_changed.connect([this](int new_indent) {
    m_right_indent_px = new_indent;
    apply_paragraph_right_indent(new_indent);
    sync_ruler();
    try {
      m_prefs.save();
    } catch (...) {
    }
  });
  m_ruler.signal_tab_stop_added.connect([this](Folio::TabStop ts) {
    m_prefs.tab_stops.push_back(ts);
    std::sort(m_prefs.tab_stops.begin(), m_prefs.tab_stops.end(),
              [](const Folio::TabStop &a, const Folio::TabStop &b) {
                return a.position_pt < b.position_pt;
              });
    apply_tab_stops();
    m_ruler.queue_draw();
    try {
      m_prefs.save();
    } catch (...) {
    }
  });
  m_ruler.signal_tab_stop_moved.connect([this](int idx, Folio::TabStop ts) {
    if (idx >= 0 && idx < (int)m_prefs.tab_stops.size()) {
      m_prefs.tab_stops[idx] = ts;
      std::sort(m_prefs.tab_stops.begin(), m_prefs.tab_stops.end(),
                [](const Folio::TabStop &a, const Folio::TabStop &b) {
                  return a.position_pt < b.position_pt;
                });
    }
    apply_tab_stops();
    try {
      m_prefs.save();
    } catch (...) {
    }
  });
  m_ruler.signal_tab_stop_removed.connect([this](int idx) {
    if (idx >= 0 && idx < (int)m_prefs.tab_stops.size()) {
      m_prefs.tab_stops.erase(m_prefs.tab_stops.begin() + idx);
      apply_tab_stops();
      m_ruler.queue_draw();
      try {
        m_prefs.save();
      } catch (...) {
      }
    }
  });

  // Right-click on ruler opens the manager dialog
  auto ruler_rc = Gtk::GestureClick::create();
  ruler_rc->set_button(GDK_BUTTON_SECONDARY);
  ruler_rc->signal_pressed().connect([this](int, double, double) {
    if (!m_ruler_manager) {
      auto *root = dynamic_cast<Gtk::Window *>(get_root());
      if (!root)
        return;
      m_ruler_manager = std::make_unique<RulerManagerDialog>(*root, m_prefs);
      m_ruler_manager->on_geometry_changed = [this]() {
        m_page_width_pct = m_prefs.editor_page_width_pct;
        m_left_margin_px = m_prefs.editor_left_margin_px;
        m_right_margin_px = m_prefs.editor_right_margin_px;
        m_page_margin_px = m_left_margin_px; // keep legacy in sync
        apply_page_geometry();
      };
      m_ruler_manager->on_indent_changed = [this]() {
        m_first_line_indent_px = m_prefs.first_line_indent_px;
        m_tag_indent->property_indent() = m_first_line_indent_px;
        if (m_first_line_indent)
          apply_indent();
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
          [this]() -> bool {
            m_ruler_manager.reset();
            return false;
          },
          false);
    }
    m_ruler_manager->present();
  });
  m_ruler.add_controller(ruler_rc);
  append(m_ruler);
}

// ─────────────────────────────────────────────────────────────────────────────
// build_format_popover
// ─────────────────────────────────────────────────────────────────────────────

void Editor::build_format_popover() {
  m_format_popover = Gtk::make_managed<Gtk::Popover>();
  m_format_popover->set_has_arrow(true);
  m_format_popover->add_css_class("format-popover");
  m_format_popover->set_autohide(
      false); // don't dismiss when color dialog or other child window opens

  // Snapshot selection AND scroll position when popover opens.
  // Also read the current selection's font/size and push it into the controls
  // now — the popover-visible guard blocks mark_set from doing this later.
  m_format_popover->signal_show().connect([this]() {
    Gtk::TextBuffer::iterator s, e;
    if (m_buffer->get_selection_bounds(s, e)) {
      m_saved_sel_start = s.get_offset();
      m_saved_sel_end = e.get_offset();
    }
    // Capture scroll position now; restore after focus-leave scroll fires.
    if (auto vadj = m_write_scroll.get_vadjustment()) {
      double saved_scroll = vadj->get_value();
      Glib::signal_idle().connect_once([this, saved_scroll]() {
        if (auto v = m_write_scroll.get_vadjustment())
          v->set_value(saved_scroll);
      });
    }
    // Scan selection for font/size and update the spin controls immediately.
    // We do this inline rather than calling update_font_controls_from_selection
    // because that function is guarded against running while the popover is
    // open.
    if (m_font_size_spin && m_saved_sel_start != m_saved_sel_end) {
      auto it = m_buffer->get_iter_at_offset(m_saved_sel_start);
      auto end = m_buffer->get_iter_at_offset(m_saved_sel_end);
      int first_size = -1;
      bool multi_size = false;
      std::string first_family;
      bool multi_family = false;
      while (it != end) {
        int hs = -1;
        std::string hf;
        for (auto &tag : it.get_tags()) {
          std::string tn = tag->property_name().get_value();
          if (tn.size() > 5 && tn.substr(0, 5) == "font:") {
            auto colon = tn.rfind(':');
            if (colon != std::string::npos) {
              hf = tn.substr(5, colon - 5);
              try {
                hs = std::stoi(tn.substr(colon + 1));
              } catch (...) {
              }
            }
          }
        }
        if (hs < 0)
          hs = m_current_font_size;
        if (hf.empty())
          hf = m_current_font;
        if (first_size < 0) {
          first_size = hs;
          first_family = hf;
        } else {
          if (hs != first_size)
            multi_size = true;
          if (hf != first_family)
            multi_family = true;
        }
        if (multi_size && multi_family)
          break;
        it.forward_char();
      }
      m_updating_font_controls = true;
      if (multi_size)
        m_font_size_spin->set_value(0.0);
      else if (first_size > 0)
        m_font_size_spin->set_value((double)first_size);
      if (m_font_dropdown) {
        if (multi_family)
          m_font_dropdown->set_selected(m_font_multiple_idx);
        else {
          for (guint i = 0; i < (guint)m_font_names.size(); ++i) {
            if (m_font_names[i] == first_family) {
              m_font_dropdown->set_selected(i);
              break;
            }
          }
        }
      }
      m_updating_font_controls = false;
    }
  });
  // Restore selection and focus when popover is explicitly closed.
  m_format_popover->signal_closed().connect([this]() {
    if (m_saved_sel_start != m_saved_sel_end) {
      auto s = m_buffer->get_iter_at_offset(m_saved_sel_start);
      auto e = m_buffer->get_iter_at_offset(m_saved_sel_end);
      m_buffer->select_range(s, e);
    }
    m_text_view.grab_focus();
  });

  auto *outer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
  outer->set_margin_top(8);
  outer->set_margin_bottom(8);
  outer->set_margin_start(10);
  outer->set_margin_end(10);

  // Close button — needed because autohide is disabled
  auto *header_row =
      Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
  auto *title_lbl = Gtk::make_managed<Gtk::Label>("Format");
  title_lbl->add_css_class("stat-label");
  title_lbl->set_hexpand(true);
  title_lbl->set_xalign(0.0f);
  auto *close_btn = Gtk::make_managed<Gtk::Button>();
  close_btn->set_icon_name("window-close-symbolic");
  close_btn->add_css_class("fmt-btn");
  close_btn->set_tooltip_text("Close");
  close_btn->signal_clicked().connect(
      [this]() { m_format_popover->popdown(); });
  header_row->append(*title_lbl);
  header_row->append(*close_btn);
  outer->append(*header_row);

  auto make_row = [](int spacing = 6) {
    auto *row =
        Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, spacing);
    row->set_valign(Gtk::Align::CENTER);
    return row;
  };
  auto make_sep_h = []() {
    auto *s = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
    s->set_margin_top(2);
    s->set_margin_bottom(2);
    return s;
  };

  // ── Row 1: Font family + size + line spacing ──────────────────────────────
  outer->append(m_font_box); // built in build_font_controls, no prior parent
  outer->append(*make_sep_h());

  // ── Row 2: Bold / Italic / Underline / Strikethrough / Clear ─────────────
  // s6: glyph label removed — set_icon_name wins over set_label in GTK4,
  // so the "B"/"I"/"U"/"S" letters never rendered. Icons are the truth.
  m_btn_bold.set_icon_name(
      "format-text-bold-symbolic"); // override any inherited icon
  m_btn_bold.add_css_class("fmt-btn");
  m_btn_bold.set_tooltip_text("Bold (Ctrl+B)");
  m_btn_italic.set_icon_name(
      "format-text-italic-symbolic"); // override any inherited icon
  m_btn_italic.add_css_class("fmt-btn");
  m_btn_italic.set_tooltip_text("Italic (Ctrl+I)");
  m_btn_underline.set_icon_name(
      "format-text-underline-symbolic"); // override any inherited icon
  m_btn_underline.add_css_class("fmt-btn");
  m_btn_underline.set_tooltip_text("Underline (Ctrl+U)");
  m_btn_strikethrough.set_icon_name(
      "format-text-strikethrough-symbolic"); // override any inherited icon
  m_btn_strikethrough.add_css_class("fmt-btn");
  m_btn_strikethrough.set_tooltip_text("Strikethrough");
  m_btn_clear_format.set_icon_name("folio-clear-format-symbolic");
  m_btn_clear_format.add_css_class("fmt-btn");
  m_btn_clear_format.set_tooltip_text("Clear all formatting");

  m_btn_bold.signal_clicked().connect([this]() {
    auto s = m_buffer->get_iter_at_offset(m_saved_sel_start);
    auto e = m_buffer->get_iter_at_offset(m_saved_sel_end);
    if (m_saved_sel_start != m_saved_sel_end)
      toggle_format_tag(m_tag_bold, s, e);
  });
  m_btn_italic.signal_clicked().connect([this]() {
    auto s = m_buffer->get_iter_at_offset(m_saved_sel_start);
    auto e = m_buffer->get_iter_at_offset(m_saved_sel_end);
    if (m_saved_sel_start != m_saved_sel_end)
      toggle_format_tag(m_tag_italic, s, e);
  });
  m_btn_underline.signal_clicked().connect([this]() {
    auto s = m_buffer->get_iter_at_offset(m_saved_sel_start);
    auto e = m_buffer->get_iter_at_offset(m_saved_sel_end);
    if (m_saved_sel_start != m_saved_sel_end)
      toggle_format_tag(m_tag_underline, s, e);
  });
  m_btn_strikethrough.signal_clicked().connect([this]() {
    auto s = m_buffer->get_iter_at_offset(m_saved_sel_start);
    auto e = m_buffer->get_iter_at_offset(m_saved_sel_end);
    if (m_saved_sel_start != m_saved_sel_end)
      toggle_format_tag(m_tag_strikethrough, s, e);
  });
  m_btn_clear_format.signal_clicked().connect([this]() {
    if (m_saved_sel_start == m_saved_sel_end)
      return;
    auto s = m_buffer->get_iter_at_offset(m_saved_sel_start);
    auto e = m_buffer->get_iter_at_offset(m_saved_sel_end);
    m_buffer->begin_user_action();
    auto table = m_buffer->get_tag_table();
    table->foreach ([this, &s, &e](const Glib::RefPtr<Gtk::TextTag> &tag) {
      m_buffer->remove_tag(tag, s, e);
    });
    // Re-apply the user's preferred font+size so clearing doesn't revert to
    // the GTK default — all inline overrides are gone, but the base pref
    // stands.
    // Use the saved regular font size when naming the tag — in focus mode
    // m_current_font_size is the focus size; we must encode the regular
    // authored size so the tag remains correct after exiting focus.
    int canonical_size = (m_in_focus && m_saved_font_size > 0)
                             ? m_saved_font_size
                             : m_current_font_size;
    std::string tn =
        "font:" + m_current_font + ":" + std::to_string(canonical_size);
    auto base_tag = table->lookup(tn);
    if (!base_tag) {
      base_tag = m_buffer->create_tag(tn);
      base_tag->property_family() = m_current_font;
      base_tag->property_size_points() = (double)canonical_size * m_zoom_factor;
    }
    m_buffer->apply_tag(base_tag, s, e);
    m_buffer->end_user_action();
    // Refresh the size spin to show the pref default — the popover-visible
    // guard blocks mark_set from doing this automatically.
    if (m_font_size_spin) {
      m_updating_font_controls = true;
      m_font_size_spin->set_value((double)m_current_font_size);
      m_updating_font_controls = false;
    }
  });

  auto *style_row = make_row(2);
  style_row->add_css_class("view-toggle-group");
  style_row->append(m_btn_bold);
  style_row->append(m_btn_italic);
  style_row->append(m_btn_underline);
  style_row->append(m_btn_strikethrough);
  outer->append(*style_row);

  // ── Row 3: Justification ──────────────────────────────────────────────────
  m_justify_box.set_orientation(Gtk::Orientation::HORIZONTAL);
  m_justify_box.set_spacing(2);
  m_justify_box.add_css_class("view-toggle-group");

  m_btn_justify_left.set_icon_name(
      "format-justify-left-symbolic"); // override any inherited icon
  m_btn_justify_left.set_tooltip_text("Align left");
  m_btn_justify_center.set_icon_name(
      "format-justify-center-symbolic"); // override any inherited icon
  m_btn_justify_center.set_tooltip_text("Align center");
  m_btn_justify_right.set_icon_name(
      "format-justify-right-symbolic"); // override any inherited icon
  m_btn_justify_right.set_tooltip_text("Align right");
  m_btn_justify_full.set_icon_name(
      "format-justify-fill-symbolic"); // override any inherited icon
  m_btn_justify_full.set_tooltip_text("Justify");

  m_btn_justify_center.set_group(m_btn_justify_left);
  m_btn_justify_right.set_group(m_btn_justify_left);
  m_btn_justify_full.set_group(m_btn_justify_left);
  m_btn_justify_left.set_active(true);

  m_justify_box.append(m_btn_justify_left);
  m_justify_box.append(m_btn_justify_center);
  m_justify_box.append(m_btn_justify_right);
  m_justify_box.append(m_btn_justify_full);

  auto apply_just =
      [this](Glib::RefPtr<Gtk::TextTag> keep,
             std::initializer_list<Glib::RefPtr<Gtk::TextTag>> remove_list) {
        Gtk::TextBuffer::iterator s, e;
        if (m_saved_sel_start != m_saved_sel_end) {
          s = m_buffer->get_iter_at_offset(m_saved_sel_start);
          e = m_buffer->get_iter_at_offset(m_saved_sel_end);
        } else {
          s = e = m_buffer->get_insert()->get_iter();
        }
        expand_to_paragraphs(s, e);
        m_buffer->begin_user_action();
        for (auto &t : remove_list)
          m_buffer->remove_tag(t, s, e);
        if (keep)
          m_buffer->apply_tag(keep, s, e);
        m_buffer->end_user_action();
      };

  m_btn_justify_left.signal_toggled().connect([this, apply_just]() {
    if (m_btn_justify_left.get_active())
      apply_just(nullptr, {m_tag_justify_center, m_tag_justify_right,
                           m_tag_justify_full});
  });
  m_btn_justify_center.signal_toggled().connect([this, apply_just]() {
    if (m_btn_justify_center.get_active())
      apply_just(m_tag_justify_center,
                 {m_tag_justify_left, m_tag_justify_right, m_tag_justify_full});
  });
  m_btn_justify_right.signal_toggled().connect([this, apply_just]() {
    if (m_btn_justify_right.get_active())
      apply_just(m_tag_justify_right, {m_tag_justify_left, m_tag_justify_center,
                                       m_tag_justify_full});
  });
  m_btn_justify_full.signal_toggled().connect([this, apply_just]() {
    if (m_btn_justify_full.get_active())
      apply_just(m_tag_justify_full, {m_tag_justify_left, m_tag_justify_center,
                                      m_tag_justify_right});
  });

  outer->append(m_justify_box);
  outer->append(*make_sep_h());

  // ── Row 4: Color buttons ──────────────────────────────────────────────────
  // While the popover is open the text_view has no focus so
  // get_selection_bounds returns nothing. Use the saved offsets snapshot
  // instead.
  auto apply_color_tag = [this](bool foreground, const Gdk::RGBA &color) {
    if (m_saved_sel_start == m_saved_sel_end)
      return;
    auto s = m_buffer->get_iter_at_offset(m_saved_sel_start);
    auto e = m_buffer->get_iter_at_offset(m_saved_sel_end);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%s%.3f:%.3f:%.3f",
                  foreground ? "fg:" : "bg:", color.get_red(),
                  color.get_green(), color.get_blue());
    std::string tn(buf);
    auto tt = m_buffer->get_tag_table()->lookup(tn);
    if (!tt) {
      tt = m_buffer->create_tag(tn);
      if (foreground)
        tt->property_foreground_rgba() = color;
      else
        tt->property_background_rgba() = color;
    }
    m_buffer->begin_user_action();
    m_buffer->apply_tag(tt, s, e);
    m_buffer->end_user_action();
  };

  // ── Color pickers — ColorDialogButton + explicit clear button ────────────
  // GTK4 ColorDialog does not expose a palette API, so we put a separate
  // "✕" button next to each picker for one-click "remove colour".

  auto remove_fg_tags = [this]() {
    if (m_saved_sel_start == m_saved_sel_end)
      return;
    auto s = m_buffer->get_iter_at_offset(m_saved_sel_start);
    auto e = m_buffer->get_iter_at_offset(m_saved_sel_end);
    m_buffer->begin_user_action();
    m_buffer->get_tag_table()->foreach (
        [this, &s, &e](const Glib::RefPtr<Gtk::TextTag> &tag) {
          std::string tn = tag->property_name().get_value();
          if (tn.size() > 3 && tn.substr(0, 3) == "fg:")
            m_buffer->remove_tag(tag, s, e);
        });
    m_buffer->end_user_action();
  };

  auto remove_bg_tags = [this]() {
    if (m_saved_sel_start == m_saved_sel_end)
      return;
    auto s = m_buffer->get_iter_at_offset(m_saved_sel_start);
    auto e = m_buffer->get_iter_at_offset(m_saved_sel_end);
    m_buffer->begin_user_action();
    m_buffer->get_tag_table()->foreach (
        [this, &s, &e](const Glib::RefPtr<Gtk::TextTag> &tag) {
          std::string tn = tag->property_name().get_value();
          if (tn.size() > 3 && tn.substr(0, 3) == "bg:")
            m_buffer->remove_tag(tag, s, e);
        });
    m_buffer->end_user_action();
  };

  // Text colour row
  {
    auto *row = make_row(4);
    auto *lbl = Gtk::make_managed<Gtk::Label>("Text color");
    lbl->add_css_class("stat-label");
    lbl->set_hexpand(true);
    lbl->set_xalign(0.0f);
    row->append(*lbl);

    auto fg_dlg = Gtk::ColorDialog::create();
    fg_dlg->set_with_alpha(false);
    fg_dlg->set_title("Text color");
    m_btn_text_color = Gtk::make_managed<Gtk::ColorDialogButton>(fg_dlg);
    m_btn_text_color->set_rgba(m_current_text_color);
    m_btn_text_color->set_tooltip_text("Text colour");
    m_btn_text_color->add_css_class("color-picker-btn");
    // When the user picks a colour, apply it immediately
    m_btn_text_color->property_rgba().signal_changed().connect(
        [this, apply_color_tag]() {
          m_current_text_color = m_btn_text_color->get_rgba();
          apply_color_tag(true, m_current_text_color);
        });
    row->append(*m_btn_text_color);

    auto *clear_fg = Gtk::make_managed<Gtk::Button>();
    clear_fg->set_label("");
    clear_fg->set_icon_name("process-stop-symbolic"); // ✕
    clear_fg->add_css_class("fmt-btn");
    clear_fg->add_css_class("color-clear-btn");
    clear_fg->set_tooltip_text("Remove text colour");
    clear_fg->signal_clicked().connect(
        [remove_fg_tags]() { remove_fg_tags(); });
    row->append(*clear_fg);
    outer->append(*row);
  }

  // Highlight (background) colour row
  {
    auto *row = make_row(4);
    auto *lbl = Gtk::make_managed<Gtk::Label>("Highlight");
    lbl->add_css_class("stat-label");
    lbl->set_hexpand(true);
    lbl->set_xalign(0.0f);
    row->append(*lbl);

    auto bg_dlg = Gtk::ColorDialog::create();
    bg_dlg->set_with_alpha(false);
    bg_dlg->set_title("Highlight color");
    m_btn_bg_color = Gtk::make_managed<Gtk::ColorDialogButton>(bg_dlg);
    m_btn_bg_color->set_rgba(m_current_bg_color);
    m_btn_bg_color->set_tooltip_text("Highlight colour");
    m_btn_bg_color->add_css_class("color-picker-btn");
    m_btn_bg_color->property_rgba().signal_changed().connect(
        [this, apply_color_tag]() {
          m_current_bg_color = m_btn_bg_color->get_rgba();
          apply_color_tag(false, m_current_bg_color);
        });
    row->append(*m_btn_bg_color);

    auto *clear_bg = Gtk::make_managed<Gtk::Button>();
    clear_bg->set_label("");
    clear_bg->set_icon_name("process-stop-symbolic"); // ✕
    clear_bg->add_css_class("fmt-btn");
    clear_bg->add_css_class("color-clear-btn");
    clear_bg->set_tooltip_text("Remove highlight");
    clear_bg->signal_clicked().connect(
        [remove_bg_tags]() { remove_bg_tags(); });
    row->append(*clear_bg);
    outer->append(*row);
  }

  // ── Clear formatting — separated, destructive styling ─────────────────────
  outer->append(*make_sep_h());
  m_btn_clear_format.set_label("✕  Clear all formatting");
  m_btn_clear_format.add_css_class("fmt-btn");
  m_btn_clear_format.add_css_class("clear-fmt-btn");
  m_btn_clear_format.set_tooltip_text(
      "Remove all inline formatting from selection");
  m_btn_clear_format.set_hexpand(true);
  outer->append(m_btn_clear_format);

  // ── Insert Character — unicode picker ─────────────────────────────────────
  outer->append(*make_sep_h());
  {
    auto *insert_char_btn = Gtk::make_managed<Gtk::Button>();
    insert_char_btn->set_label("Ω  Insert Character…");
    insert_char_btn->add_css_class("fmt-btn");
    insert_char_btn->set_tooltip_text(
        "Insert a special Unicode character at the cursor");
    insert_char_btn->set_hexpand(true);

    insert_char_btn->signal_clicked().connect([this]() {
      if (m_format_popover)
        m_format_popover->popdown();

      // Create picker on first use and parent it to the top-level window.
      // Popovers must be parented to a top-level window — not a child widget.
      if (!m_char_picker) {
        m_char_picker = std::make_unique<UnicodePickerPopover>(&m_text_view);
        auto *root = dynamic_cast<Gtk::Window *>(get_root());
        if (root)
          m_char_picker->set_parent(*root);
      }

      // Centre the picker on the text view
      double wx = 0, wy = 0;
      auto *root = dynamic_cast<Gtk::Window *>(get_root());
      if (root) {
        m_text_view.translate_coordinates(*root, m_text_view.get_width() / 2,
                                          m_text_view.get_height() / 2, wx, wy);
      }
      Gdk::Rectangle rect((int)wx, (int)wy, 1, 1);
      m_char_picker->set_pointing_to(rect);
      m_char_picker->popup();
    });
    outer->append(*insert_char_btn);
  }

  m_format_popover->set_child(*outer);
}

void Editor::show_format_popover_at(double /*x*/, double /*y*/) {
  if (m_btn_format)
    m_btn_format->popup();
}

// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────

void Editor::build_editor_area() {
  m_view_stack.set_vexpand(true);
  m_view_stack.set_hexpand(true);
  m_view_stack.set_transition_type(Gtk::StackTransitionType::CROSSFADE);
  m_view_stack.set_transition_duration(150);

  // ── Write view ────────────────────────────────────────────────────────────
  m_write_scroll.set_policy(Gtk::PolicyType::AUTOMATIC,
                            Gtk::PolicyType::AUTOMATIC);
  m_write_scroll.set_kinetic_scrolling(false);
  m_write_scroll.set_vexpand(true);
  m_write_scroll.set_hexpand(true);

  // s44 — typewriter rail: the runway margins are half the VIEWPORT, so they must
  // be recomputed when the viewport resizes or the rail drifts off-centre. The
  // vadjustment's "changed" fires when page-size (viewport) or upper (content)
  // changes; guard on the page-size so setting margins here can't loop.
  if (auto vadj = m_write_scroll.get_vadjustment()) {
    vadj->signal_changed().connect([this]() {
      if (!m_typewriter_mode || m_in_focus) return;
      auto v = m_write_scroll.get_vadjustment();
      if (!v) return;
      const int ph = static_cast<int>(v->get_page_size());
      if (ph == m_typewriter_page_h) return;     // viewport unchanged — no work
      m_typewriter_page_h = ph;
      apply_typewriter_padding();
      queue_scroll_to_center();
    });
  }

  m_paper_card.add_css_class("folio-paper");
  m_paper_card.set_margin_top(28);
  m_paper_card.set_margin_bottom(28);
  m_paper_card.set_margin_start(28);
  m_paper_card.set_margin_end(28);
  m_paper_card.set_hexpand(false);
  m_paper_card.set_halign(Gtk::Align::CENTER);
  m_paper_card.set_size_request(680, -1);

  m_paper_inner.set_margin_top(52);
  m_paper_inner.set_margin_bottom(52);
  m_paper_inner.set_margin_start(64);
  m_paper_inner.set_margin_end(64);

  m_chapter_tag.add_css_class("paper-chapter-tag");
  m_chapter_tag.set_valign(Gtk::Align::CENTER);
  // One reusable provider attached above GTK_STYLE_PROVIDER_PRIORITY_USER so
  // it beats the global theme sheet. Uses "*" selector — applied to this
  // widget's own context so it only affects m_chapter_tag.
  m_chapter_tag_css = Gtk::CssProvider::create();
  m_chapter_tag.get_style_context()->add_provider(
      m_chapter_tag_css, GTK_STYLE_PROVIDER_PRIORITY_USER + 1);

  m_title_label.add_css_class("paper-title");
  m_title_label.set_valign(Gtk::Align::CENTER);
  m_title_label.set_hexpand(
      false); // don't expand in early setup; row will set this

  m_divider.set_margin_top(4);
  m_divider.set_margin_bottom(4);

  m_text_view.set_buffer(m_buffer);
  m_text_view.set_wrap_mode(Gtk::WrapMode::WORD_CHAR);

  // Prime the extra menu with an empty model so GTK initialises its internal
  // popup machinery before the first right-click. Without this, the first
  // popup appears at the wrong position.
  gtk_text_view_set_extra_menu(m_text_view.gobj(),
                               G_MENU_MODEL(Gio::Menu::create()->gobj()));
  m_text_view.set_top_margin(4);
  m_text_view.set_left_margin(0);
  m_text_view.set_right_margin(0);
  m_text_view.add_css_class("paper-body");
  m_text_view.set_vexpand(true);
  m_text_view.set_cursor_visible(true);
  m_text_view.set_pixels_above_lines(m_paragraph_spacing_px);
  m_text_view.set_pixels_below_lines(0);

  // Save selection offsets when text view loses focus so font controls
  // can still apply to the previously selected range.
  auto focus_ctrl = Gtk::EventControllerFocus::create();
  focus_ctrl->signal_leave().connect([this]() {
    Gtk::TextBuffer::iterator s, e;
    if (m_buffer->get_selection_bounds(s, e)) {
      m_saved_sel_start = s.get_offset();
      m_saved_sel_end = e.get_offset();
    }
  });
  focus_ctrl->signal_enter().connect([this]() {
    m_saved_sel_start = -1;
    m_saved_sel_end = -1;
  });
  m_text_view.add_controller(focus_ctrl);

  // Ctrl+B/I/U/Shift+S keyboard shortcuts (CAPTURE phase)
  auto fmt_key = Gtk::EventControllerKey::create();
  fmt_key->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
  fmt_key->signal_key_pressed().connect(
      [this](guint keyval, guint, Gdk::ModifierType state) -> bool {
        const bool ctrl =
            (state & Gdk::ModifierType::CONTROL_MASK) != Gdk::ModifierType{};
        const bool shift =
            (state & Gdk::ModifierType::SHIFT_MASK) != Gdk::ModifierType{};
        const bool alt =
            (state & Gdk::ModifierType::ALT_MASK) != Gdk::ModifierType{};
        if (!ctrl)
          return false;

        // Ctrl+Alt+N/O/S — writing mode
        if (ctrl && alt && !shift) {
          if (keyval == GDK_KEY_n || keyval == GDK_KEY_N) {
            set_writing_mode(WritingMode::Novel);
            return true;
          }
          if (keyval == GDK_KEY_o || keyval == GDK_KEY_O) {
            set_writing_mode(WritingMode::Outline);
            return true;
          }
          if (keyval == GDK_KEY_s || keyval == GDK_KEY_S) {
            set_writing_mode(WritingMode::Screenplay);
            return true;
          }
        }
        if (shift) {
          if (keyval == GDK_KEY_Up || keyval == GDK_KEY_KP_Up) {
            if (auto *win = dynamic_cast<Gtk::Window *>(get_root()))
              win->activate_action("move-up");
            return true;
          }
          if (keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down) {
            if (auto *win = dynamic_cast<Gtk::Window *>(get_root()))
              win->activate_action("move-down");
            return true;
          }
        }
        // Ctrl+F / Ctrl+H — open find bar (no selection needed)
        if (!shift && (keyval == GDK_KEY_f || keyval == GDK_KEY_F)) {
          open_find(false);
          return true;
        }
        if (!shift && (keyval == GDK_KEY_h || keyval == GDK_KEY_H)) {
          open_find(true);
          return true;
        }
        // Ctrl+Shift+H — screenplay format reference (only in Screenplay mode)
        if (shift && (keyval == GDK_KEY_h || keyval == GDK_KEY_H)) {
          if (m_writing_mode == WritingMode::Screenplay) {
            auto *win = dynamic_cast<Gtk::Window *>(get_root());
            if (win) {
              if (!m_sp_help_dialog)
                m_sp_help_dialog = std::make_unique<ScreenplayHelpDialog>(*win);
              m_sp_help_dialog->present();
            }
          }
          return true;
        }
        // Ctrl+K — insert internal link
        if (!shift && (keyval == GDK_KEY_k || keyval == GDK_KEY_K)) {
          open_link_picker();
          return true;
        }
        // Ctrl+J — stamp a new journal entry; Ctrl+Shift+J — resume (jump to the
        // end of the newest entry). s54: the journal owns its surface + caret, so
        // these act on it directly (no shared-buffer round-trip). A no-op outside
        // a journal Reference.
        if (keyval == GDK_KEY_j || keyval == GDK_KEY_J) {
          if (!node_is_journal_form(m_current_node))
            return false;
          if (shift)
            m_journal_surface.resume_caret();
          else
            m_journal_surface.stamp_new_entry();
          return true;
        }
        // ── Special character insertion ──────────────────────────────────────
        // Ctrl+Space             → word joiner           (U+2060)  zero-width
        // no-break Ctrl+Shift+Space       → non-breaking space    (U+00A0) like
        // LibreOffice Ctrl+Shift+Minus       → non-breaking hyphen   (U+2011)
        // like LibreOffice Ctrl+Minus             → soft hyphen (U+00AD)  like
        // LibreOffice Ctrl+Shift+W           → word joiner (alternate)(U+2060)
        // Ctrl+Shift+Z           → zero-width space       (U+200B)
        // Ctrl+Shift+T           → thin space             (U+2009)
        if (ctrl && !shift && keyval == GDK_KEY_space) {
          m_buffer->insert_at_cursor(
              Glib::ustring(1, (gunichar)0x2060)); // word joiner
          return true;
        }
        if (ctrl && shift && keyval == GDK_KEY_space) {
          m_buffer->insert_at_cursor(
              Glib::ustring(1, (gunichar)0x00A0)); // nbsp
          return true;
        }
        if (ctrl && shift &&
            (keyval == GDK_KEY_minus || keyval == GDK_KEY_KP_Subtract)) {
          m_buffer->insert_at_cursor(
              Glib::ustring(1, (gunichar)0x2011)); // nb hyphen
          return true;
        }
        if (ctrl && !shift &&
            (keyval == GDK_KEY_minus || keyval == GDK_KEY_KP_Subtract)) {
          m_buffer->insert_at_cursor(
              Glib::ustring(1, (gunichar)0x00AD)); // soft hyphen
          return true;
        }
        if (ctrl && shift && (keyval == GDK_KEY_w || keyval == GDK_KEY_W)) {
          m_buffer->insert_at_cursor(
              Glib::ustring(1, (gunichar)0x2060)); // word joiner
          return true;
        }
        if (ctrl && shift && (keyval == GDK_KEY_z || keyval == GDK_KEY_Z)) {
          m_buffer->insert_at_cursor(
              Glib::ustring(1, (gunichar)0x200B)); // zwsp
          return true;
        }
        if (ctrl && shift && (keyval == GDK_KEY_t || keyval == GDK_KEY_T)) {
          m_buffer->insert_at_cursor(
              Glib::ustring(1, (gunichar)0x2009)); // thin space
          return true;
        }
        Gtk::TextBuffer::iterator s, e;
        if (!m_buffer->get_selection_bounds(s, e))
          return false;
        if (keyval == GDK_KEY_b || keyval == GDK_KEY_B) {
          toggle_format_tag(m_tag_bold, s, e);
          return true;
        }
        if (keyval == GDK_KEY_i || keyval == GDK_KEY_I) {
          toggle_format_tag(m_tag_italic, s, e);
          return true;
        }
        if (keyval == GDK_KEY_u || keyval == GDK_KEY_U) {
          toggle_format_tag(m_tag_underline, s, e);
          return true;
        }
        if (shift && (keyval == GDK_KEY_s || keyval == GDK_KEY_S)) {
          toggle_format_tag(m_tag_strikethrough, s, e);
          return true;
        }
        return false;
      },
      false);
  m_text_view.add_controller(fmt_key);

  // Outline mode key behaviour
  auto outline_key = Gtk::EventControllerKey::create();
  outline_key->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
  outline_key->signal_key_pressed().connect(
      [this](guint keyval, guint, Gdk::ModifierType state) -> bool {
        const bool shift =
            (state & Gdk::ModifierType::SHIFT_MASK) != Gdk::ModifierType{};

        if (keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab) {
          int cur = current_outline_level();
          // In outline mode: Tab/Shift+Tab always indent/outdent
          // In other modes: only act if line already has an outline level
          //   (so normal Tab still works for prose indentation)
          if (m_writing_mode != WritingMode::Outline && cur == 0)
            return false;

          int next = shift ? std::max(cur - 1, 0)
                           : std::min(cur + 1, m_prefs.outline_levels);
          if (next != cur)
            replace_line_indicator(next);
          return true;
        }

        if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
          int cur = current_outline_level();
          // Continue outline on Enter when in Outline mode OR when cursor is on
          // an outline line
          if (m_writing_mode != WritingMode::Outline && cur == 0)
            return false;
          int cur_line = m_buffer->get_insert()->get_iter().get_line();
          // Let GTK insert newline, then on idle set level + insert indicator
          Glib::signal_idle().connect_once([this, cur, cur_line]() {
            if (cur > 0) {
              int new_line = cur_line + 1;
              m_loading = true; // hold loading for entire operation
              // Apply outline-level tag to new line
              auto ls = m_buffer->get_iter_at_line(new_line);
              auto le = ls;
              if (!le.ends_line())
                le.forward_to_line_end();
              for (auto &t : m_tag_ol)
                if (t)
                  m_buffer->remove_tag(t, ls, le);
              Glib::RefPtr<Gtk::TextTag> tag =
                  (cur >= 1 && cur <= MAX_OUTLINE_LEVELS)
                      ? m_tag_ol[cur - 1]
                      : Glib::RefPtr<Gtk::TextTag>{};
              if (tag)
                m_buffer->apply_tag(tag, ls, le);
              // Insert the indicator text at the start of the new line
              std::string ind = compute_indicator(new_line, cur);
              if (!ind.empty()) {
                auto ins = m_buffer->get_iter_at_line(new_line);
                m_buffer->insert(ins, ind);
                // Re-apply level tag over whole line including indicator
                auto ts = m_buffer->get_iter_at_line(new_line);
                auto te = ts;
                if (!te.ends_line())
                  te.forward_to_line_end();
                if (tag)
                  m_buffer->apply_tag(tag, ts, te);
              }
              m_loading = false;
              // Sync mode and save
              update_writing_mode_dd();
              if (m_current_node) {
                m_current_node->content = buffer_to_html();
                m_current_node->content_modified = true;
                m_model.mark_modified();
              }
            }
          });
          return false; // let GTK insert the newline
        }

        return false;
      },
      false);
  m_text_view.add_controller(outline_key);

  // ── Screenplay mode key behaviour ────────────────────────────────────────
  // Tab/Shift+Tab cycle through elements per prefs.
  // Enter continues with the contextually appropriate next element.
  auto sp_key = Gtk::EventControllerKey::create();
  sp_key->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
  sp_key->signal_key_pressed().connect(
      [this](guint keyval, guint, Gdk::ModifierType state) -> bool {
        if (m_writing_mode != WritingMode::Screenplay)
          return false;
        const bool shift =
            (state & Gdk::ModifierType::SHIFT_MASK) != Gdk::ModifierType{};

        if (keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab) {
          sp_tab_next(shift);
          return true;
        }

        if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter) {
          int cur = current_sp_element();
          // Capture current element before GTK inserts the newline
          Glib::signal_idle().connect_once([this, cur]() {
            // Decide what element the new line should be
            // scene      → action
            // action     → action  (Tab to start character dialogue block)
            // character  → dialogue
            // parenthetical → dialogue
            // dialogue   → character  (ready for next speaker)
            // transition → scene
            // plain (-1) → action
            static const int next_on_enter[] = {
                1, // scene      → action
                1, // action     → action
                4, // character  → dialogue
                4, // parenthetical → dialogue
                2, // dialogue   → character
                0, // transition → scene
            };
            int next = (cur >= 0 && cur < SP_COUNT) ? next_on_enter[cur] : 1;
            apply_sp_element(next);
            update_writing_mode_dd();
          });
          return false; // let GTK insert newline
        }

        return false;
      },
      false);
  m_text_view.add_controller(sp_key);

  // Auto-sense on key release — fires after GTK has inserted the character,
  // so we see the updated line content.  Uses key-release (not text-changed)
  // to avoid re-triggering on_text_changed from inside apply_sp_element.
  auto sp_sense_key = Gtk::EventControllerKey::create();
  sp_sense_key->set_propagation_phase(Gtk::PropagationPhase::BUBBLE);
  sp_sense_key->signal_key_released().connect(
      [this](guint keyval, guint, Gdk::ModifierType) {
        if (m_writing_mode != WritingMode::Screenplay)
          return;
        // Skip modifier-only, navigation, and function keys — only sense after
        // printable input that could change the line prefix.
        if (keyval == GDK_KEY_Tab || keyval == GDK_KEY_ISO_Left_Tab)
          return;
        if (keyval == GDK_KEY_Return || keyval == GDK_KEY_KP_Enter)
          return;
        if (keyval >= GDK_KEY_F1 && keyval <= GDK_KEY_F35)
          return;
        if (keyval == GDK_KEY_Shift_L || keyval == GDK_KEY_Shift_R)
          return;
        if (keyval == GDK_KEY_Control_L || keyval == GDK_KEY_Control_R)
          return;
        if (keyval == GDK_KEY_Alt_L || keyval == GDK_KEY_Alt_R)
          return;
        sp_auto_sense();
      });
  m_text_view.add_controller(sp_sense_key);
  // space, matching what set_pointing_to expects on a popover parented to
  // m_text_view. We snapshot the selection first via a separate CAPTURE
  // Snapshot selection on right-click before GTK clears it
  auto right_click_capture = Gtk::GestureClick::create();
  right_click_capture->set_button(GDK_BUTTON_SECONDARY);
  right_click_capture->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
  right_click_capture->signal_pressed().connect(
      [this](int, double x, double y) {
        // Snapshot selection before GTK clears it on secondary click
        Gtk::TextBuffer::iterator s, e;
        if (m_buffer && m_buffer->get_selection_bounds(s, e)) {
          m_rc_sel_start = s.get_offset();
          m_rc_sel_end = e.get_offset();
        } else {
          m_rc_sel_start = m_rc_sel_end = -1;
        }
        m_last_rc_x = x;
        m_last_rc_y = y;
        // Move insert mark to click position so GTK positions its menu
        // correctly. GTK's TextView menu anchors to the insert mark, not the
        // mouse coords.
        int bx = 0, by = 0;
        m_text_view.window_to_buffer_coords(Gtk::TextWindowType::WIDGET, (int)x,
                                            (int)y, bx, by);
        Gtk::TextBuffer::iterator click_iter;
        int trailing = 0;
        m_text_view.get_iter_at_position(click_iter, trailing, bx, by);
        m_loading = true; // suppress mark_set side effects
        m_buffer->place_cursor(click_iter);
        m_loading = false;
        // Rebuild extra menu with context-sensitive items before GTK shows it
        rebuild_extra_menu(x, y);
      });
  m_text_view.add_controller(right_click_capture);

  // Hover tooltip — shows annotation text when hovering over highlighted text
  m_text_view.set_has_tooltip(true);
  m_text_view.signal_query_tooltip().connect(
      [this](int x, int y, bool /*kb*/,
             const Glib::RefPtr<Gtk::Tooltip> &tip) -> bool {
        if (!m_current_node)
          return false;
        // Convert window coords to buffer coords
        int bx, by;
        m_text_view.window_to_buffer_coords(Gtk::TextWindowType::WIDGET, x, y,
                                            bx, by);
        Gtk::TextBuffer::iterator it;
        m_text_view.get_iter_at_location(it, bx, by);
        for (auto &tag : it.get_tags()) {
          std::string tn = tag->property_name().get_value();
          // Annotation tooltip
          if (tn.size() > 4 && tn.substr(0, 4) == "ann:") {
            try {
              int id = std::stoi(tn.substr(4));
              for (const auto &ann : m_current_node->annotations) {
                if (ann.id == id) {
                  std::string msg = "[" + ann.kind + "] " + ann.text;
                  msg += "\nAlt+click to edit";
                  tip->set_text(msg);
                  return true;
                }
              }
            } catch (...) {
            }
          }
          // Link tooltip — show target node title and Alt+click hint
          if (tn.size() > 5 && tn.substr(0, 5) == "link:") {
            std::string payload = tn.substr(5); // "iid:anchor"
            auto colon = payload.find(':');
            if (colon != std::string::npos) {
              std::string target_iid = payload.substr(0, colon);
              std::string anchor     = payload.substr(colon + 1);
              if (!target_iid.empty()) {
                auto *target = m_model.find_node_by_iid(target_iid);
                std::string title =
                    target
                        ? (target->title.empty() ? "(Untitled)" : target->title)
                        : "(deleted node)";
                std::string msg = "→ " + title;
                if (!anchor.empty())
                  msg += "  ¶";
                msg += "\nAlt+click to follow";
                tip->set_text(msg);
                return true;
              }
            }
          }
        }
        return false;
      },
      false);

  // Track whether the primary mouse button is held. While it is, we suppress
  // the typewriter/focus-mode scroll-to-cursor that mark_set would queue —
  // scrolling mid-click shifts the viewport, GTK detects a drag between press
  // and release positions, and leaves a spurious selection.
  auto left_click = Gtk::GestureClick::create();
  left_click->set_button(GDK_BUTTON_PRIMARY);
  left_click->set_propagation_phase(Gtk::PropagationPhase::CAPTURE);
  left_click->signal_pressed().connect([this, left_click](int, double x,
                                                          double y) {
    m_mouse_btn_held = true;
    m_last_lc_x = x;
    m_last_lc_y = y;
    // Capture Alt modifier state at press time for Alt+click link follow
    m_last_lc_alt = false;
    if (auto ev = left_click->get_last_event(nullptr)) {
      auto mods = gdk_event_get_modifier_state(ev->gobj());
      m_last_lc_alt = (mods & GDK_ALT_MASK) != 0;
    }
    // Cancel any pending scroll-to-center. If it fires between button-press
    // and button-release, GTK sees a viewport shift and creates a spurious
    // text selection.
    if (m_pending_scroll.connected())
      m_pending_scroll.disconnect();
    // On the first click after each node load, GTK's internal focus handler
    // scrolls to make the insert mark visible, shifting the viewport between
    // press and release and causing a spurious selection. Snapshot the scroll
    // position now and restore it on idle after GTK's focus-scroll has fired.
    if (m_first_node_click) {
      m_first_node_click = false;
      if (auto vadj = m_write_scroll.get_vadjustment()) {
        double saved = vadj->get_value();
        Glib::signal_idle().connect_once([this, saved]() {
          if (auto v = m_write_scroll.get_vadjustment())
            v->set_value(saved);
        });
      }
    }
  });
  left_click->signal_released().connect([this](int, double, double) {
    m_mouse_btn_held = false;
    // Allow notify::width reflows after the first click completes.
    // This is the fast path — if the user clicks before the 150ms
    // pre-warm timer fires, we set it here instead.
    if (!m_first_click_done) {
      m_first_click_done = true;
      if (m_geometry_ready)
        apply_page_geometry();
    }

    // Alt+click: follow link or edit annotation — plain click is just cursor.
    // Only fires when no selection was dragged out.
    if (m_buffer && m_last_lc_alt) {
      Gtk::TextBuffer::iterator sel_s, sel_e;
      bool has_sel = m_buffer->get_selection_bounds(sel_s, sel_e);
      if (!has_sel) {
        int bx = 0, by = 0;
        m_text_view.window_to_buffer_coords(Gtk::TextWindowType::WIDGET,
                                            (int)m_last_lc_x, (int)m_last_lc_y,
                                            bx, by);
        Gtk::TextBuffer::iterator it;
        int trailing = 0;
        m_text_view.get_iter_at_position(it, trailing, bx, by);
        // Link takes priority; fall through to annotation if not on a link
        if (!follow_link_at(it)) {
          // Check for annotation tag under click
          for (auto &tag : it.get_tags()) {
            std::string tn = tag->property_name().get_value();
            if (tn.size() > 4 && tn.substr(0, 4) == "ann:") {
              show_annotation_popover(m_last_lc_x, m_last_lc_y);
              break;
            }
          }
        }
      }
    }
  });
  left_click->signal_cancel().connect(
      [this](Gdk::EventSequence *) { m_mouse_btn_held = false; });
  m_text_view.add_controller(left_click);

  // ── Header (title + path) — single compact centred row ─────────────────
  // The toggle button lives OUTSIDE the revealer so it's always visible.
  // When the header is collapsed the toggle is the only thing showing.
  m_header_toggle.add_css_class("header-disclosure-btn");
  m_header_toggle.set_has_frame(false);
  m_header_toggle.set_halign(Gtk::Align::CENTER);
  m_header_toggle.set_valign(Gtk::Align::CENTER);
  m_header_toggle.set_tooltip_text("Show/hide title");
  m_header_toggle.signal_clicked().connect([this]() {
    bool now = !m_header_revealer.get_reveal_child();
    m_header_revealer.set_reveal_child(now);
    update_header_toggle_icon();
    m_prefs.editor_header_visible = now;
    m_prefs.save();
  });

  m_chapter_tag.add_css_class("paper-chapter-tag");
  m_chapter_tag.set_valign(Gtk::Align::CENTER);

  m_title_label.add_css_class("paper-title");
  m_title_label.set_wrap(false);
  m_title_label.set_ellipsize(Pango::EllipsizeMode::END);
  m_title_label.set_valign(Gtk::Align::CENTER);
  m_title_label.set_hexpand(true);
  m_title_label.set_xalign(0.5f);

  // Chapter pill + title inside the revealer
  auto *hdr_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  hdr_row->set_halign(Gtk::Align::CENTER);
  hdr_row->set_margin_bottom(8);
  hdr_row->append(m_chapter_tag);
  hdr_row->append(m_title_label);

  m_header_revealer.set_child(*hdr_row);
  m_header_revealer.set_transition_type(
      Gtk::RevealerTransitionType::SLIDE_DOWN);
  m_header_revealer.set_transition_duration(160);
  m_header_revealer.set_reveal_child(true);

  m_paper_inner.append(m_header_toggle);
  m_paper_inner.append(m_header_revealer);

  // Avatar strip (characters + places)
  m_avatar_image.set_pixel_size(96);
  m_avatar_image.set_from_icon_name("avatar-default-symbolic");
  m_avatar_image.add_css_class("scene-icon");
  m_avatar_image.set_size_request(96, 96);
  m_avatar_image.set_valign(Gtk::Align::CENTER);
  m_avatar_btn.set_child(m_avatar_image);
  m_avatar_btn.add_css_class("icon-btn");
  m_avatar_btn.set_tooltip_text("Click to set image…");
  m_avatar_btn.set_valign(Gtk::Align::CENTER);
  m_avatar_btn.signal_clicked().connect([this]() {
    auto *win = dynamic_cast<Gtk::Window *>(get_root());
    if (!win)
      return;
    auto dlg = Gtk::FileChooserNative::create(
        "Choose Image", *win, Gtk::FileChooser::Action::OPEN, "Open", "Cancel");
    auto filter = Gtk::FileFilter::create();
    filter->set_name("Images (PNG, JPG, WebP)");
    filter->add_mime_type("image/png");
    filter->add_mime_type("image/jpeg");
    filter->add_mime_type("image/webp");
    dlg->add_filter(filter);
    dlg->signal_response().connect([this, dlg](int response) {
      if (response != Gtk::ResponseType::ACCEPT)
        return;
      auto file = dlg->get_file();
      if (!file)
        return;
      std::string path = file->get_path();
      if (m_current_node) {
        m_current_node->image_path = path;
        m_model.mark_modified();
      }
      try {
        auto pixbuf = Gdk::Pixbuf::create_from_file(path, 96, 96, true);
        m_avatar_image.set(Gdk::Texture::create_for_pixbuf(pixbuf));
      } catch (...) {
        m_avatar_image.set_from_icon_name("avatar-default-symbolic");
      }
    });
    dlg->show();
  });

  m_avatar_strip.append(m_avatar_btn);
  m_avatar_strip.set_margin_bottom(8);
  m_avatar_strip.set_visible(false);

  m_paper_inner.append(m_avatar_strip);
  m_paper_inner.append(m_divider);

  // ── Line-number gutter setup ──────────────────────────────────────────────
  m_text_area_row.set_orientation(Gtk::Orientation::HORIZONTAL);
  m_text_area_row.set_spacing(0);
  m_text_area_row.set_vexpand(true);

  m_line_number_gutter.set_vexpand(true);
  m_line_number_gutter.set_hexpand(false);
  m_line_number_gutter.set_visible(m_show_line_numbers);
  m_line_number_gutter.add_css_class("line-number-gutter");

  // Draw handler — renders line numbers aligned to text_view line positions.
  // Key insight: gtk_text_view_buffer_to_window_coords converts buffer-space Y
  // (what get_iter_location returns) into text_view window-space Y, which
  // already accounts for scroll position. We then just add the fixed Y offset
  // between the two sibling widgets to land in gutter draw coords.
  m_line_number_gutter.set_draw_func(
      [this](const Cairo::RefPtr<Cairo::Context> &cr, int w, int h) {
        if (!m_show_line_numbers)
          return;

        auto style = m_line_number_gutter.get_style_context();
        Gdk::RGBA fg = style->get_color();

        // Detect dark/light mode from the foreground colour luminance.
        // In dark mode fg is near-white (high luminance) → use higher alpha
        // so the dim elements are actually visible against the dark surface.
        // In light mode fg is near-black (low luminance) → use a moderate
        // alpha that reads clearly without being too heavy.
        double lum = 0.2126 * fg.get_red() + 0.7152 * fg.get_green() +
                     0.0722 * fg.get_blue();
        bool dark_mode = (lum > 0.5);
        double sep_alpha = dark_mode ? 0.30 : 0.20;
        double num_alpha = dark_mode ? 0.55 : 0.40;

        // ── Separator — inset from right edge to leave a gap before the text
        // ── sep_x: where the vertical line sits; gap_right px of space to its
        // right
        const double gap_right = 10.0;
        const double sep_x = (double)w - gap_right - 0.5;
        cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(),
                            sep_alpha);
        cr->set_line_width(1.0);
        cr->move_to(sep_x, 0);
        cr->line_to(sep_x, h);
        cr->stroke();

        // ── Line numbers (right-aligned up to the separator)
        // ──────────────────
        cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(),
                            num_alpha);

        // Fixed Y offset: both widgets are siblings in m_text_area_row so their
        // relative Y never changes (just CSS alignment differences, usually 0).
        auto tv_alloc = m_text_view.get_allocation();
        auto gut_alloc = m_line_number_gutter.get_allocation();
        int sibling_dy = tv_alloc.get_y() - gut_alloc.get_y();

        // Determine which buffer lines are visible using the text_view's own
        // visible rect (in buffer coords, scroll-aware).
        Gdk::Rectangle visible_rect;
        m_text_view.get_visible_rect(visible_rect);

        auto layout = m_line_number_gutter.create_pango_layout("");
        Pango::FontDescription fd("monospace 9");
        layout->set_font_description(fd);

        // Start from the first visible line
        Gtk::TextBuffer::iterator iter;
        int first_line_top = 0;
        m_text_view.get_line_at_y(iter, visible_rect.get_y(), first_line_top);
        iter.set_line(iter.get_line()); // ensure we start at line start

        while (true) {
          // Get buffer-space rect for the start of this line
          Gdk::Rectangle buf_rect;
          m_text_view.get_iter_location(iter, buf_rect);

          // Convert buffer Y → text_view window Y (scroll-compensated)
          int win_x = 0, win_y = 0;
          m_text_view.buffer_to_window_coords(Gtk::TextWindowType::TEXT, 0,
                                              buf_rect.get_y(), win_x, win_y);

          // Translate into gutter draw coords
          double gutter_y = (double)(win_y + sibling_dy);

          if (gutter_y > (double)h)
            break;

          if (gutter_y + (double)buf_rect.get_height() >= 0) {
            int ln = iter.get_line();
            layout->set_text(std::to_string(ln + 1));
            int lw = 0, lh = 0;
            layout->get_pixel_size(lw, lh);
            double draw_x = sep_x - (double)lw - 6.0;
            double draw_y =
                gutter_y + ((double)buf_rect.get_height() - (double)lh) / 2.0;
            cr->move_to(draw_x, draw_y);
            pango_cairo_show_layout(cr->cobj(), layout->gobj());
          }

          // Advance to next line
          if (!iter.forward_line())
            break;
        }
      });

  // Requeue gutter draw whenever the buffer or scroll position changes
  m_buffer->signal_changed().connect([this]() {
    update_gutter_width();
    m_line_number_gutter.queue_draw();
    m_invis_overlay.queue_draw();
  });

  m_text_area_row.append(m_line_number_gutter);
  m_text_area_row.append(m_text_view);
  m_text_view.set_hexpand(true);

  // ── Backtrace indicator gutter ────────────────────────────────────────────
  // A 14px-wide DrawingArea installed as the TextView's left gutter.
  // Shows an amber diamond when the current node has incoming backlinks,
  // aligned with the cursor line.  Always visible regardless of line numbers.
  m_backtrace_gutter.set_size_request(14, -1);
  m_backtrace_gutter.add_css_class("backtrace-gutter");
  m_backtrace_gutter.set_draw_func(
      [this](const Cairo::RefPtr<Cairo::Context> &cr, int w, int h) {
        if (!m_current_node)
          return;
        if (!m_prefs.show_links)
          return; // hidden with hyperlinks
        const auto &bl = m_model.backlinks();
        auto it = bl.find(m_current_node->iid);
        if (it == bl.end() || it->second.empty())
          return;
        if (!m_buffer)
          return;

        // Pin diamond to the first line of the buffer — it scrolls off
        // naturally as the user writes down the page.
        auto ins = m_buffer->begin();
        Gdk::Rectangle buf_rect;
        m_text_view.get_iter_location(ins, buf_rect);
        int wx = 0, wy = 0;
        m_text_view.buffer_to_window_coords(Gtk::TextWindowType::LEFT, 0,
                                            buf_rect.get_y(), wx, wy);
        double cy = (double)wy + (double)buf_rect.get_height() / 2.0;
        if (cy < 0 || cy > h)
          return;

        // Diamond — same blue as hyperlinks: #60a5fa dark, #2563eb light
        const double dx = 4.5, dy = 4.5;
        double bx = w / 2.0;
        if (detect_dark_mode())
          cr->set_source_rgba(0x60 / 255.0, 0xa5 / 255.0, 0xfa / 255.0,
                              0.90); // #60a5fa
        else
          cr->set_source_rgba(0x25 / 255.0, 0x63 / 255.0, 0xeb / 255.0,
                              0.90); // #2563eb
        cr->move_to(bx, cy - dy);
        cr->line_to(bx + dx, cy);
        cr->line_to(bx, cy + dy);
        cr->line_to(bx - dx, cy);
        cr->close_path();
        cr->fill();
      });
  m_backtrace_gutter.set_has_tooltip(true);
  m_backtrace_gutter.signal_query_tooltip().connect(
      [this](int, int, bool, const Glib::RefPtr<Gtk::Tooltip> &tip) -> bool {
        if (!m_current_node)
          return false;
        const auto &bl = m_model.backlinks();
        auto it = bl.find(m_current_node->iid);
        if (it == bl.end() || it->second.empty())
          return false;
        int n = (int)it->second.size();
        std::string msg;
        if (n == 1) {
          auto *src =
              m_model.find_node_by_iid(it->second.front().source_iid);
          std::string title =
              src ? (src->title.empty() ? "(Untitled)" : src->title)
                  : "(deleted)";
          msg = "Linked from: " + title + "\nClick to go back";
        } else {
          msg =
              std::to_string(n) + " links point to this node\nClick to choose";
        }
        tip->set_text(msg);
        return true;
      },
      false);
  // Click on the gutter diamond navigates back (or shows picker if multiple)
  {
    auto gc = Gtk::GestureClick::create();
    gc->set_button(GDK_BUTTON_PRIMARY);
    gc->signal_released().connect(
        [this](int, double, double) { do_backtrace(); });
    m_backtrace_gutter.add_controller(gc);
  }
  m_text_view.set_gutter(Gtk::TextWindowType::LEFT, m_backtrace_gutter);

  m_paper_inner.append(m_text_area_row);
  m_paper_card.append(m_paper_inner);

  m_write_scroll.set_child(m_paper_card);

  // signal_map fires after the first layout — allocations are real by then.
  m_write_scroll.signal_map().connect([this]() {
    m_geometry_ready = true; // now safe for notify::width to trigger reflows
    apply_page_geometry();
    update_gutter_width();
    // Detect best available Courier font for screenplay mode.
    // Done here because Pango context font list is only valid after
    // realization.
    {
      static const char *candidates[] = {"Courier New", "Courier Prime",
                                         "Courier 10 Pitch", "Courier",
                                         nullptr};
      auto pctx = m_text_view.get_pango_context();
      if (pctx) {
        auto families = pctx->list_families();
        for (const char **c = candidates; *c; ++c) {
          for (auto &fam : families) {
            if (fam->get_name() == *c) {
              m_screenplay_font = *c;
              LOG_INFO("Screenplay font detected: {}", m_screenplay_font);
              goto font_found;
            }
          }
        }
      font_found:;
      }
    }
    // Defer a ruler sync to ensure the ruler widget is also fully allocated
    // by the time we call translate_coordinates on the paper card.
    Glib::signal_timeout().connect_once([this]() { sync_ruler(); }, 50);
  });

  // Hook the hadjustment's page-size: this fires when the viewport receives
  // its first real allocation, guaranteeing scroll_w > 0. This is the
  // reliable trigger that restores the saved page-width percentage on startup.
  // IMPORTANT: disconnect after the first valid call — signal_changed fires
  // whenever upper/lower/page-size change, which includes every time
  // apply_page_geometry resizes the paper card. Leaving it connected creates
  // a reflow cycle that fires mid-click, shifts the viewport, and produces
  // a spurious text selection on the first click into the editor.
  if (auto hadj = m_write_scroll.get_hadjustment()) {
    auto conn = std::make_shared<sigc::connection>();
    *conn = hadj->signal_changed().connect([this, conn]() {
      auto adj = m_write_scroll.get_hadjustment();
      if (adj && adj->get_page_size() > 0) {
        conn->disconnect(); // one-shot: never fire again after first valid size
        apply_page_geometry();
      }
    });
  }

  // Redraw gutter on every vertical scroll tick
  if (auto vadj = m_write_scroll.get_vadjustment())
    vadj->signal_value_changed().connect([this]() {
      m_line_number_gutter.queue_draw();
      m_invis_overlay.queue_draw();
    });

  // Sync ruler on horizontal scroll so it tracks the page position
  if (auto hadj = m_write_scroll.get_hadjustment())
    hadj->signal_value_changed().connect([this]() { sync_ruler(); });

  // Reflow whenever the scroll window's allocated width changes.
  // Gated behind m_geometry_ready — notify::width also fires during GTK's
  // internal focus/input-method setup on first click, which would shift the
  // viewport mid-click and create a spurious text selection.
  // Also gated behind m_first_click_done for the same reason on the very
  // first interaction after a node is loaded.
  g_signal_connect(m_write_scroll.gobj(), "notify::width",
                   G_CALLBACK(+[](GObject *, GParamSpec *, gpointer ud) {
                     auto *self = static_cast<Editor *>(ud);
                     if (self->m_geometry_ready && self->m_first_click_done)
                       self->apply_page_geometry();
                   }),
                   this);
  m_scroll_overlay.set_child(m_write_scroll);
  m_scroll_overlay.set_vexpand(true);
  m_scroll_overlay.set_hexpand(true);

  // ── Toast notification (bottom-centre, auto-dismisses) ────────────────────
  m_toast_label.add_css_class("editor-toast");
  m_toast_label.set_halign(Gtk::Align::CENTER);
  m_toast_label.set_valign(Gtk::Align::END);
  m_toast_label.set_margin_bottom(24);
  m_toast_revealer.set_child(m_toast_label);
  m_toast_revealer.set_transition_type(Gtk::RevealerTransitionType::CROSSFADE);
  m_toast_revealer.set_transition_duration(180);
  m_toast_revealer.set_halign(Gtk::Align::CENTER);
  m_toast_revealer.set_valign(Gtk::Align::END);
  m_toast_revealer.set_margin_bottom(24);
  m_toast_revealer.set_can_target(false); // don't block mouse events
  m_scroll_overlay.add_overlay(m_toast_revealer);

  // ── Typewriter rail slider (s44) — alt-click the typewriter button floats this
  // vertical slider at the editor's right edge to set the rail position by eye.
  // Inverted so dragging UP raises the caret (smaller fraction). Hidden until
  // summoned; live value re-rails immediately while typewriter mode is engaged.
  m_typewriter_pos_slider.set_orientation(Gtk::Orientation::VERTICAL);
  m_typewriter_pos_slider.set_range(0.15, 0.85);
  m_typewriter_pos_slider.set_increments(0.01, 0.05);
  m_typewriter_pos_slider.set_inverted(true);
  m_typewriter_pos_slider.set_draw_value(false);
  m_typewriter_pos_slider.set_value(typewriter_pos());
  m_typewriter_pos_slider.set_size_request(-1, 200);
  m_typewriter_pos_slider.set_halign(Gtk::Align::END);
  m_typewriter_pos_slider.set_valign(Gtk::Align::CENTER);
  m_typewriter_pos_slider.set_margin_end(10);
  m_typewriter_pos_slider.add_css_class("typewriter-pos-slider");
  m_typewriter_pos_slider.set_tooltip_text("Typewriter rail position");
  m_typewriter_pos_slider.set_visible(false);
  m_typewriter_pos_slider.signal_value_changed().connect([this]() {
    m_prefs.typewriter_position = m_typewriter_pos_slider.get_value();
    if (m_typewriter_mode && !m_in_focus) {
      apply_typewriter_padding();
      queue_scroll_to_center();
    }
  });
  m_scroll_overlay.add_overlay(m_typewriter_pos_slider);

  // ── Invisible characters — drawn via snapshot on the TextView itself
  // ──────── Using g_signal_connect_after("snapshot") paints in the text_view's
  // own coordinate space — no translation needed, no overlay positioning
  // issues. The snapshot fires after GTK renders the text, so glyphs paint on
  // top. ── Invisible characters overlay
  // ────────────────────────────────────────── A full-size transparent
  // DrawingArea added as an overlay above the scroll.
  // gtk_widget_compute_bounds() gives the text view's position within the
  // overlay in a single call — correct across the full widget hierarchy.
  m_invis_overlay.set_can_target(false);
  m_invis_overlay.set_hexpand(true);
  m_invis_overlay.set_vexpand(true);
  m_invis_overlay.set_halign(Gtk::Align::FILL);
  m_invis_overlay.set_valign(Gtk::Align::FILL);
  m_invis_overlay.add_css_class("invis-overlay");
  m_invis_overlay.set_visible(m_prefs.show_invisibles);
  m_invis_overlay.set_draw_func([this](const Cairo::RefPtr<Cairo::Context> &cr,
                                       int /*w*/, int /*h*/) {
    // Clear to fully transparent — without this the DrawingArea background
    // paints over the text view, hiding all content beneath.
    cr->save();
    cr->set_operator(Cairo::Context::Operator::CLEAR);
    cr->paint();
    cr->restore();
    if (!m_buffer || !m_prefs.show_invisibles)
      return;

    // gtk_widget_compute_bounds gives text_view bounds relative to
    // invis_overlay.
    graphene_rect_t tv_bounds;
    bool bounds_ok = gtk_widget_compute_bounds(
        GTK_WIDGET(m_text_view.gobj()), GTK_WIDGET(m_invis_overlay.gobj()),
        &tv_bounds);
    if (!bounds_ok)
      return;
    cr->translate(tv_bounds.origin.x, tv_bounds.origin.y);

    // Dim colour
    bool dark = detect_dark_mode();
    // Use link-blue family — clearly intentional, matches other editor markers
    cr->set_source_rgba(dark ? 0.38 : 0.15, dark ? 0.65 : 0.39,
                        dark ? 0.98 : 0.92, 1.0);

    // Visible rect in buffer-space coords
    Gdk::Rectangle vis;
    m_text_view.get_visible_rect(vis);

    // One reusable Pango layout — use sans for reliable symbol coverage
    auto layout = m_text_view.create_pango_layout("");
    int font_size_pt = std::max(8, (int)(m_current_font_size * m_zoom_factor));
    Pango::FontDescription fd("sans " + std::to_string(font_size_pt));
    layout->set_font_description(fd);

    // Start from first visible line
    Gtk::TextBuffer::iterator iter;
    int dummy = 0;
    m_text_view.get_line_at_y(iter, vis.get_y(), dummy);
    iter.set_line_offset(0);

    // Get the layout's ascent once — used to convert cell-top Y to baseline Y.
    // Pango's show_in_cairo_context draws from the baseline, but
    // get_iter_location returns the top of the line cell, so we add the ascent
    // to align correctly.
    layout->set_text("M"); // representative glyph for metrics
    int layout_ascent = 0;
    {
      Pango::Rectangle ink, logical;
      layout->get_extents(ink, logical);
      // logical.y is the offset from baseline to top (negative), in Pango units
      layout_ascent = PANGO_PIXELS(-logical.get_y());
    }

    auto draw_at = [&](const Gtk::TextBuffer::iterator &it, const char *glyph) {
      Gdk::Rectangle r;
      m_text_view.get_iter_location(it, r);
      int wx = 0, wy = 0;
      m_text_view.buffer_to_window_coords(Gtk::TextWindowType::WIDGET,
                                          r.get_x(), r.get_y(), wx, wy);
      layout->set_text(glyph);
      // wy is the top of the line cell; add ascent to reach the baseline
      cr->move_to(wx, wy + layout_ascent);
      layout->show_in_cairo_context(cr);
    };

    while (!iter.is_end()) {
      Gdk::Rectangle lr;
      m_text_view.get_iter_location(iter, lr);
      if (lr.get_y() > vis.get_y() + vis.get_height())
        break;

      gunichar ch = iter.get_char();
      switch (ch) {
      case 0x0020:
        draw_at(iter, "\xc2\xb7");
        break; // · space
      case 0x0009:
        draw_at(iter, "\xe2\x86\x92");
        break; // → tab
      case 0x000A:
        draw_at(iter, "\xc2\xb6");
        break; // ¶ newline
      case 0x00A0:
        draw_at(iter, "\xe2\x8e\xb5");
        break; // ⎵ nbsp
      case 0x00AD:
        draw_at(iter, "\xe2\x80\x90");
        break; // ‐ soft hyphen
      case 0x2009:
        draw_at(iter, "\xe2\x80\xa2");
        break; // • thin space
      case 0x200A:
        draw_at(iter, "\xe2\x80\xa2");
        break; // • hair space
      case 0x200B:
        draw_at(iter, "\xc2\xb0");
        break; // ° zwsp
      case 0x200C:
        draw_at(iter, "|");
        break; // | zwnj
      case 0x200D:
        draw_at(iter, "\xe2\x8b\x88");
        break; // ⋈ zwj
      case 0x200E:
        draw_at(iter, "\xe2\x80\xba");
        break; // › lrm
      case 0x200F:
        draw_at(iter, "\xe2\x80\xb9");
        break; // ‹ rlm
      case 0x2011:
        draw_at(iter, "\xe2\x80\x90");
        break; // ‐ nb hyphen
      case 0x2060:
        draw_at(iter, "\xc2\xb0");
        break; // ° wj
      case 0xFEFF:
        draw_at(iter, "\xc2\xb0");
        break; // ° bom
      default:
        break;
      }
      if (!iter.forward_char())
        break;
    }
  });
  m_scroll_overlay.add_overlay(m_invis_overlay);

  // Empty-state hint — shown when no node is loaded in Write/Joined mode
  m_write_placeholder.set_text(
      "Select an item in the Sidebar to begin writing.\n"
      "Double-click a scene or chapter to open it here.");
  m_write_placeholder.add_css_class("board-placeholder");
  m_write_placeholder.set_justify(Gtk::Justification::CENTER);
  m_write_placeholder.set_valign(Gtk::Align::CENTER);
  m_write_placeholder.set_halign(Gtk::Align::CENTER);
  m_write_placeholder.set_can_target(false);
  m_write_placeholder.set_visible(false);
  m_scroll_overlay.add_overlay(m_write_placeholder);

  m_view_stack.add(m_scroll_overlay, "write");

  // ── s41 — Form view (the inversion) ─────────────────────────────────────────
  // A Character/Place draws its template form here, as the Editor document. The
  // form card mirrors the write paper card (folio-paper, centred, 680px). The
  // relation provider and the "Edit fields…" door are wired ONCE (the form's rows
  // are rebuilt per object by populate(), but these sinks are set-once).
  m_form_card.set_orientation(Gtk::Orientation::VERTICAL);
  m_form_card.add_css_class("folio-paper");
  m_form_card.set_margin_top(28);
  m_form_card.set_margin_bottom(28);
  m_form_card.set_margin_start(28);
  m_form_card.set_margin_end(28);
  m_form_card.set_hexpand(false);
  m_form_card.set_halign(Gtk::Align::CENTER);
  m_form_card.set_size_request(680, -1);
  m_object_form.set_margin_top(16);
  m_object_form.set_margin_bottom(20);
  m_object_form.set_margin_start(20);
  m_object_form.set_margin_end(20);
  m_form_card.append(m_object_form);

  // Relation picker candidate source — resolved LIVE against the store on each
  // call (rebuilt before each populate; never a held snapshot). Empty type = any.
  m_object_form.set_relation_provider([this](const std::string& target_type) {
    std::vector<Folio::FieldChoice> out;
    const Folio::ObjectStore& store = m_model.object_store();
    for (const Folio::Object* o : store.objects_of_type(target_type))
      out.push_back({ o->iid, Folio::object_display_name(*o) });
    return out;
  });
  // Backlink source (s44 — the relief): incoming_edges resolved to display rows,
  // one per (source object, relation field) pair pointing at `iid`. Computed live
  // against the store on each populate; never a stored reverse index.
  m_object_form.set_backlink_provider([this](const std::string& iid) {
    std::vector<Folio::ObjectForm::Backlink> out;
    const Folio::ObjectStore& store = m_model.object_store();
    for (const Folio::Object* src : store.incoming_edges(iid)) {
      const Folio::Template* st = store.find_template(src->type);
      if (!st) continue;
      const std::string label = Folio::object_display_name(*src);
      for (const auto& f : st->fields) {
        if (!Folio::field_type_is_relation(f.type)) continue;
        if (!src->has_value(f.id)) continue;
        const Folio::json& v = src->values.at(f.id);
        bool hit = false;
        if (v.is_string()) hit = (v.get<std::string>() == iid);
        else if (v.is_array())
          for (const auto& e : v)
            if (e.is_string() && e.get<std::string>() == iid) { hit = true; break; }
        if (hit) out.push_back({ src->iid, label, f.label });
      }
    }
    return out;
  });

  m_form_scroll.set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
  m_form_scroll.set_vexpand(true);
  m_form_scroll.set_hexpand(true);
  m_form_scroll.set_child(m_form_card);
  m_view_stack.add(m_form_scroll, "form");

  // Heading outline navigator (WritingMode::Outline within Editor view)
  m_houtline_scroll.set_policy(Gtk::PolicyType::NEVER,
                               Gtk::PolicyType::AUTOMATIC);
  m_houtline_scroll.set_vexpand(true);
  m_houtline_scroll.set_hexpand(true);
  m_houtline_box.add_css_class("heading-outline-box");
  m_houtline_scroll.set_child(m_houtline_box);
  m_view_stack.add(m_houtline_scroll, "heading-outline");

  // Multi-selection placeholder
  m_multi_placeholder_box.set_vexpand(true);
  m_multi_placeholder_box.set_hexpand(true);
  m_multi_placeholder_box.set_valign(Gtk::Align::CENTER);
  m_multi_placeholder_box.set_halign(Gtk::Align::CENTER);
  auto *multi_icon = Gtk::make_managed<Gtk::Label>("⊞");
  multi_icon->set_markup("<span size='xx-large' alpha='40%'>⊞</span>");
  m_multi_placeholder_label.set_justify(Gtk::Justification::CENTER);
  m_multi_placeholder_label.add_css_class("board-placeholder");
  m_multi_placeholder_box.append(*multi_icon);
  m_multi_placeholder_box.append(m_multi_placeholder_label);
  m_view_stack.add(m_multi_placeholder_box, "multi");

  // Outline view
  m_outline_scroll.set_policy(Gtk::PolicyType::NEVER,
                              Gtk::PolicyType::AUTOMATIC);
  m_outline_scroll.set_vexpand(true);
  m_outline_scroll.set_policy(Gtk::PolicyType::AUTOMATIC,
                              Gtk::PolicyType::AUTOMATIC);
  m_outline_scroll.set_vexpand(true);
  m_outline_scroll.set_hexpand(true);
  m_outline_grid.add_css_class("outline-grid");
  m_outline_scroll.set_child(m_outline_grid);

  // Marquee overlay sits on top of the scroll window
  m_outline_overlay.set_child(m_outline_scroll);
  m_marquee_layer.set_hexpand(true);
  m_marquee_layer.set_vexpand(true);
  m_marquee_layer.set_can_target(false); // pass-through for normal input
  m_marquee_layer.set_draw_func(
      [this](const Cairo::RefPtr<Cairo::Context> &cr, int /*w*/, int /*h*/) {
        if (!m_marquee_active)
          return;
        double x = std::min(m_marquee_x0, m_marquee_x1);
        double y = std::min(m_marquee_y0, m_marquee_y1);
        double rw = std::abs(m_marquee_x1 - m_marquee_x0);
        double rh = std::abs(m_marquee_y1 - m_marquee_y0);
        cr->set_source_rgba(0.3, 0.5, 1.0, 0.15);
        cr->rectangle(x, y, rw, rh);
        cr->fill();
        cr->set_source_rgba(0.3, 0.5, 1.0, 0.60);
        cr->set_line_width(1.0);
        cr->rectangle(x + 0.5, y + 0.5, rw - 1, rh - 1);
        cr->stroke();
      });
  m_outline_overlay.add_overlay(m_marquee_layer);

  // Marquee gesture on the overlay
  auto marquee_drag = Gtk::GestureDrag::create();
  marquee_drag->set_button(GDK_BUTTON_PRIMARY);
  marquee_drag->signal_drag_begin().connect([this](double x, double y) {
    // Store start position but don't activate yet — wait for first movement
    m_marquee_active = false;
    m_marquee_x0 = m_marquee_x1 = x;
    m_marquee_y0 = m_marquee_y1 = y;
  });
  marquee_drag->signal_drag_update().connect([this](double dx, double dy) {
    // Only activate marquee after moving at least 4px
    if (!m_marquee_active && (std::abs(dx) < 4.0 && std::abs(dy) < 4.0))
      return;
    m_marquee_active = true;
    m_marquee_x1 = m_marquee_x0 + dx;
    m_marquee_y1 = m_marquee_y0 + dy;
    m_marquee_layer.queue_draw();
    // Hit test rows
    double ry = std::min(m_marquee_y0, m_marquee_y1);
    double rh = std::abs(m_marquee_y1 - m_marquee_y0);
    // Adjust for scroll position
    auto vadj = m_outline_scroll.get_vadjustment();
    double scroll_y = vadj ? vadj->get_value() : 0;
    for (size_t i = 0; i < m_grid_row_y.size() && i < m_grid_selected.size();
         ++i) {
      double row_top = m_grid_row_y[i] - scroll_y;
      double row_bot = row_top + m_grid_row_h;
      bool hit = (row_bot >= ry) && (row_top <= ry + rh);
      if (m_grid_rows[i]) // skip group headers
        m_grid_selected[i] = hit;
    }
    rebuild_outline();
  });
  marquee_drag->signal_drag_end().connect([this](double, double) {
    m_marquee_active = false;
    m_marquee_layer.queue_draw();
  });
  m_outline_overlay.add_controller(marquee_drag);

  // Empty-state hint — shown in Grid mode when nothing is selected
  m_grid_placeholder.set_text(
      "Select items in the Sidebar to view them in the grid.\n"
      "Ctrl+click to mix Groups, Scenes, Characters and Places.");
  m_grid_placeholder.add_css_class("board-placeholder");
  m_grid_placeholder.set_justify(Gtk::Justification::CENTER);
  m_grid_placeholder.set_valign(Gtk::Align::CENTER);
  m_grid_placeholder.set_halign(Gtk::Align::CENTER);
  m_grid_placeholder.set_can_target(false);
  m_grid_placeholder.set_visible(false);
  m_outline_overlay.add_overlay(m_grid_placeholder);

  m_view_stack.add(m_outline_overlay, "outline");

  // Board view
  m_board_scroll.set_vexpand(true);
  m_board_scroll.set_hexpand(true);
  m_board_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);

  m_board_flow = Gtk::make_managed<Gtk::FlowBox>();
  m_board_flow->set_margin_top(24);
  m_board_flow->set_margin_start(24);
  m_board_flow->set_margin_end(24);
  m_board_flow->set_margin_bottom(24);
  m_board_flow->set_row_spacing(16);
  m_board_flow->set_column_spacing(16);
  m_board_flow->set_max_children_per_line(4);
  m_board_flow->set_min_children_per_line(1);
  m_board_flow->set_selection_mode(Gtk::SelectionMode::NONE);

  m_board_placeholder.set_text(
      "Select items in the Sidebar to view them as cards here.\n"
      "Ctrl+click to mix Groups, Scenes, Characters and Places.");
  m_board_placeholder.add_css_class("board-placeholder");
  m_board_placeholder.set_justify(Gtk::Justification::CENTER);
  m_board_placeholder.set_valign(Gtk::Align::CENTER);
  m_board_placeholder.set_halign(Gtk::Align::CENTER);

  m_board_scroll.set_child(*m_board_flow);
  m_board_overlay.set_vexpand(true);
  m_board_overlay.set_hexpand(true);
  m_board_overlay.set_child(m_board_scroll);
  m_board_overlay.add_overlay(m_board_placeholder);
  m_board_flow->set_visible(false);
  m_board_placeholder.set_visible(true);

  m_view_stack.add(m_board_overlay, "board");

  // ── s48 — Map view (the fourth lens) ────────────────────────────────────────
  // A whole-graph projection, hosted like Board. The canvas reads the model on
  // each entry (set_view_mode(Map) → rebuild). A node click forwards through the
  // Editor's map-open hook so MainWindow can switch to Write + select it.
  m_map_canvas.set_open_callback([this](const std::string& iid) {
    if (m_on_map_open) m_on_map_open(iid);
  });
  m_map_canvas.set_create_callback([this](double wx, double wy) -> std::string {
    return m_on_map_create ? m_on_map_create(wx, wy) : std::string();
  });
  m_view_stack.add(m_map_canvas, "map");

  // ── s51 — the owned Mind Map document surface ───────────────────────────────
  // A Reference whose form is "Mind Map" shows THIS in place of the ObjectForm
  // (routed in set_editor_mode / set_view_mode). The canvas reads/writes a CMMDoc;
  // its persist callback serialises straight into the host node's body cell, and
  // its open callback navigates to an Anchor's target through the shared map-open
  // path. Object + name providers resolve against the live object store.
  m_cmm_canvas.set_open_callback([this](const std::string &iid) {
    if (m_on_map_open) m_on_map_open(iid);
  });
  m_cmm_canvas.set_persist_callback([this](const std::string &cmm) {
    if (BinderNode *n = m_model.find_node_by_iid(m_cmm_iid)) {
      n->content = cmm;
      m_model.mark_modified();
    }
  });
  m_cmm_canvas.set_rename_callback([this](const std::string &name) {
    BinderNode *n = m_model.find_node_by_iid(m_cmm_iid);
    if (!n) return;
    n->title = name;
    m_model.mark_modified();
    m_title_label.set_text(name.empty() ? "Unnamed" : name);   // editor header
    if (m_on_meta_changed) m_on_meta_changed(n);               // sidebar row, etc.
  });
  m_cmm_canvas.set_objects_provider([this]() {
    std::vector<Folio::CustomMindMapCanvas::ObjOption> out;
    auto group_for = [](Section s) -> const char * {
      switch (s) {
        case Section::Manuscript: return "Manuscript";
        case Section::Characters: return "Characters";
        case Section::Places:     return "Places";
        case Section::References:  return "References";
        default:                  return "";
      }
    };
    // Every anchorable node across the binder (Parts/Chapters/Scenes, Character
    // groups + characters, Place groups + places, References) — grouped by section
    // and indented by tree depth. Templates/Trash and the host map are excluded.
    for (const DocumentModel::NodeRef &nr : m_model.collect_all_nodes()) {
      if (!nr.node) continue;
      if (nr.section == Section::Templates) continue;
      const BinderKind k = nr.node->kind;
      if (k != BinderKind::Scene && k != BinderKind::Group &&
          k != BinderKind::Character && k != BinderKind::Place &&
          k != BinderKind::Reference)
        continue;
      if (nr.node->iid == m_cmm_iid) continue;        // a map never anchors itself
      Folio::CustomMindMapCanvas::ObjOption o;
      o.iid   = nr.node->iid;
      o.name  = nr.node->title.empty() ? std::string("Untitled") : nr.node->title;
      o.group = group_for(nr.section);
      o.depth = nr.path.empty() ? 0 : (int)nr.path.size() - 1;
      out.push_back(std::move(o));
    }
    return out;
  });
  m_cmm_canvas.set_name_provider([this](const std::string &iid) -> std::string {
    if (const BinderNode *n = m_model.find_node_by_iid(iid))
      return n->title.empty() ? iid : n->title;
    return iid;
  });
  m_cmm_canvas.set_color_provider([this](const std::string &iid) -> int {
    if (const BinderNode *n = m_model.find_node_by_iid(iid))
      return n->color_idx;          // an Anchor inherits its target's label colour
    return 0;
  });
  m_view_stack.add(m_cmm_canvas, "cmm");

  // ── s54 — the journal as an owned instrument ────────────────────────────────
  // A journal Reference shows this surface in Write mode; it owns its buffer +
  // serializer and persists straight into the host node's body cell through this
  // callback, keyed by the iid it was loaded with (mirrors the MM canvas).
  m_journal_surface.set_persist_callback(
      [this](const std::string &iid, const std::string &html) {
        if (BinderNode *n = m_model.find_node_by_iid(iid)) {
          n->content = html;
          m_model.mark_modified();
          if (n == m_current_node)
            update_word_count();
        }
      });
  m_view_stack.add(m_journal_surface, "journal");

  // Extra menu is rebuilt on each right-click via rebuild_extra_menu().

  append(m_view_stack);
  append(m_find_revealer); // find bar sits between editor and footer
}

// ─────────────────────────────────────────────────────────────────────────────
// build_footer
// ─────────────────────────────────────────────────────────────────────────────

void Editor::build_footer() {
  m_footer.add_css_class("folio-editor-footer");
  m_footer.set_valign(Gtk::Align::CENTER);

  auto make_stat = [](const std::string &label,
                      Gtk::Label **val_lbl) -> Gtk::Box * {
    auto *box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    auto *lbl = Gtk::make_managed<Gtk::Label>(label);
    lbl->add_css_class("stat-label");
    *val_lbl = Gtk::make_managed<Gtk::Label>("—");
    (*val_lbl)->add_css_class("stat-value");
    box->append(*lbl);
    box->append(**val_lbl);
    return box;
  };

  m_footer.append(*make_stat("Words ", &m_wc_label));
  m_footer.append(*make_stat("  Chars ", &m_chars_label));
  m_footer.append(*make_stat("  Read ", &m_read_label));
  if (m_wc_label)
    m_wc_label->set_tooltip_text("Word count for this item");
  if (m_chars_label)
    m_chars_label->set_tooltip_text("Character count including spaces");
  if (m_read_label)
    m_read_label->set_tooltip_text("Estimated reading time at 200 wpm");

  // ── Zoom slider ────────────────────────────────────────────────────────────
  auto *zoom_btn = Gtk::make_managed<Gtk::Button>("");
  zoom_btn->set_icon_name("preferences-system-search-symbolic");
  zoom_btn->add_css_class("fmt-btn");
  zoom_btn->set_tooltip_text("Zoom to 100%");
  auto zoom_adj = Gtk::Adjustment::create(100.0, 50.0, 300.0, 1.0, 10.0);
  m_zoom_scale =
      Gtk::make_managed<Gtk::Scale>(zoom_adj, Gtk::Orientation::HORIZONTAL);
  m_zoom_scale->set_size_request(120, -1);
  m_zoom_scale->set_draw_value(false); // we draw our own label
  m_zoom_scale->set_has_origin(true);  // filled track from origin (100%)
  m_zoom_scale->set_valign(Gtk::Align::CENTER);
  m_zoom_scale->add_css_class("zoom-scale");
  m_zoom_scale->set_tooltip_text("Zoom — drag to scale editor type size.");
  // Snap marks at 50, 100, 150, 200, 300
  m_zoom_scale->add_mark(50, Gtk::PositionType::BOTTOM, "");
  m_zoom_scale->add_mark(100, Gtk::PositionType::BOTTOM, "");
  m_zoom_scale->add_mark(150, Gtk::PositionType::BOTTOM, "");
  m_zoom_scale->add_mark(200, Gtk::PositionType::BOTTOM, "");
  m_zoom_scale->add_mark(300, Gtk::PositionType::BOTTOM, "");

  // ── Zoom percent entry + static "%" label ─────────────────────────────────
  m_zoom_pct_entry = Gtk::make_managed<Gtk::Entry>();
  m_zoom_pct_entry->set_text("100");
  m_zoom_pct_entry->set_width_chars(3);
  m_zoom_pct_entry->set_max_width_chars(3);
  m_zoom_pct_entry->set_max_length(3);
  m_zoom_pct_entry->set_input_purpose(Gtk::InputPurpose::DIGITS);
  m_zoom_pct_entry->set_alignment(1.0f); // right-align digits
  m_zoom_pct_entry->add_css_class("zoom-pct-entry");
  m_zoom_pct_entry->set_tooltip_text("Type a zoom % (50–300) and press Enter");

  // Reject non-digit characters as they are typed
  m_zoom_pct_entry->signal_insert_text().connect(
      [this](const Glib::ustring &text, int * /*pos*/) {
        for (gunichar c : text) {
          if (!g_unichar_isdigit(c)) {
            // Block the insert by replacing the buffer with current digits only
            Glib::signal_idle().connect_once([this]() {
              if (!m_zoom_pct_entry)
                return;
              std::string cur = m_zoom_pct_entry->get_text();
              std::string digits;
              for (char ch : cur)
                if (ch >= '0' && ch <= '9')
                  digits += ch;
              if (digits != cur)
                m_zoom_pct_entry->set_text(digits);
            });
            break;
          }
        }
      },
      false);

  // Commit on Enter
  m_zoom_pct_entry->signal_activate().connect([this]() {
    std::string txt = m_zoom_pct_entry->get_text();
    try {
      int v = std::clamp(std::stoi(txt), 50, 300);
      m_zoom_scale->set_value((double)v);
      m_zoom_pct_entry->set_text(std::to_string(v));
    } catch (...) {
      // Restore current value on bad input
      m_zoom_pct_entry->set_text(
          std::to_string((int)std::round(m_zoom_scale->get_value())));
    }
    m_text_view.grab_focus();
  });

  // Commit on focus-out
  auto focus_ctrl = Gtk::EventControllerFocus::create();
  focus_ctrl->signal_leave().connect([this]() {
    std::string txt = m_zoom_pct_entry->get_text();
    try {
      int v = std::clamp(std::stoi(txt), 50, 300);
      m_zoom_scale->set_value((double)v);
      m_zoom_pct_entry->set_text(std::to_string(v));
    } catch (...) {
      m_zoom_pct_entry->set_text(
          std::to_string((int)std::round(m_zoom_scale->get_value())));
    }
  });
  m_zoom_pct_entry->add_controller(focus_ctrl);

  auto *zoom_pct_sign = Gtk::make_managed<Gtk::Label>("%");
  zoom_pct_sign->add_css_class("zoom-pct-sign");

  auto *zoom_pct_box =
      Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
  zoom_pct_box->append(*m_zoom_pct_entry);
  zoom_pct_box->append(*zoom_pct_sign);

  // ── Zoom context menu ────────────────────────────────────────────────────
  auto set_zoom = [this](double pct) { m_zoom_scale->set_value(pct); };

  auto *zoom_menu = Gtk::make_managed<Gtk::Popover>();
  zoom_menu->set_has_arrow(false);
  auto *menu_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
  menu_box->set_margin_top(6);
  menu_box->set_margin_bottom(6);
  menu_box->set_margin_start(4);
  menu_box->set_margin_end(4);

  auto make_menu_btn = [&](const std::string &label, double pct) {
    auto *btn = Gtk::make_managed<Gtk::Button>(label);
    btn->add_css_class("zoom-menu-btn");
    btn->set_has_frame(false);
    btn->signal_clicked().connect([set_zoom, pct, zoom_menu]() {
      set_zoom(pct);
      zoom_menu->popdown();
    });

    // Set zoom button to set zoom to 100%
    zoom_btn->signal_clicked().connect([set_zoom]() { set_zoom(100.0); });

    menu_box->append(*btn);
  };

  make_menu_btn("50%", 50);
  make_menu_btn("75%", 75);
  make_menu_btn("100% — Reset", 100);
  make_menu_btn("125%", 125);
  make_menu_btn("150%", 150);
  make_menu_btn("175%", 175);
  make_menu_btn("200%", 200);
  make_menu_btn("300%", 300);

  zoom_menu->set_child(*menu_box);
  zoom_menu->set_parent(*m_zoom_scale);

  auto gc_right = Gtk::GestureClick::create();
  gc_right->set_button(3);
  gc_right->signal_pressed().connect([zoom_menu](int, double x, double y) {
    Gdk::Rectangle rect((int)x, (int)y, 1, 1);
    zoom_menu->set_pointing_to(rect);
    zoom_menu->popup();
  });
  m_zoom_scale->add_controller(gc_right);

  m_zoom_scale->signal_value_changed().connect([this]() {
    double v = m_zoom_scale->get_value();
    m_zoom_pct_entry->set_text(std::to_string((int)std::round(v)));
    if (m_updating_font_controls)
      return; // called from apply_font_prefs — skip
    m_zoom_factor = v / 100.0;
    if (m_in_focus) {
      m_focus_zoom_factor = m_zoom_factor;
      m_prefs.focus_zoom_pct = (int)std::round(v);
    } else {
      m_prefs.editor_zoom_pct = (int)std::round(v);
    }
    m_prefs.save();
    apply_base_font_tag();
    apply_zoom_to_font_tags();
  });

  // Double-click resets to 100%
  auto gc_dbl = Gtk::GestureClick::create();
  gc_dbl->set_button(1);
  gc_dbl->signal_pressed().connect([this](int n_press, double, double) {
    if (n_press == 2) {
      m_zoom_scale->set_value(100.0);
    }
  });
  m_zoom_scale->add_controller(gc_dbl);

  auto *spacer = Gtk::make_managed<Gtk::Box>();
  spacer->set_hexpand(true);
  m_footer.append(*spacer);

  // Wrap zoom controls in a tight group — no internal gaps
  auto *zoom_group =
      Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
  zoom_group->add_css_class("zoom-group");
  zoom_group->append(*zoom_btn);
  zoom_group->append(*m_zoom_scale);
  zoom_group->append(*zoom_pct_box);
  m_footer.append(*zoom_group);

  append(m_footer);
}

// ─────────────────────────────────────────────────────────────────────────────
// build_find_bar
// ─────────────────────────────────────────────────────────────────────────────

void Editor::build_find_bar() {
  m_find_revealer.set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);
  m_find_revealer.set_transition_duration(120);
  m_find_revealer.set_reveal_child(false);
  m_find_bar.add_css_class("find-bar");

  // ── Search row ────────────────────────────────────────────────────────────
  m_find_row.set_margin_start(8);
  m_find_row.set_margin_end(8);
  m_find_row.set_margin_top(4);
  m_find_row.set_margin_bottom(4);

  m_find_entry = Gtk::make_managed<Gtk::Entry>();
  m_find_entry->set_placeholder_text("Find…");
  m_find_entry->set_hexpand(true);
  m_find_entry->signal_changed().connect([this] { find_update(); });
  auto find_kc = Gtk::EventControllerKey::create();
  find_kc->signal_key_pressed().connect(
      [this](guint kv, guint, Gdk::ModifierType mods) -> bool {
        if (kv == GDK_KEY_Return || kv == GDK_KEY_KP_Enter) {
          bool shift =
              (mods & Gdk::ModifierType::SHIFT_MASK) != Gdk::ModifierType{};
          find_navigate(shift ? -1 : 1);
          return true;
        }
        if (kv == GDK_KEY_Escape) {
          close_find();
          return true;
        }
        return false;
      },
      false);
  m_find_entry->add_controller(find_kc);

  m_find_prev_btn = Gtk::make_managed<Gtk::Button>();
  m_find_prev_btn->set_icon_name("go-up-symbolic");
  m_find_prev_btn->set_tooltip_text("Previous match (Shift+Enter)");
  m_find_prev_btn->add_css_class("flat");
  m_find_prev_btn->signal_clicked().connect([this] { find_navigate(-1); });

  m_find_next_btn = Gtk::make_managed<Gtk::Button>();
  m_find_next_btn->set_icon_name("go-down-symbolic");
  m_find_next_btn->set_tooltip_text("Next match (Enter)");
  m_find_next_btn->add_css_class("flat");
  m_find_next_btn->signal_clicked().connect([this] { find_navigate(1); });

  m_find_count_lbl = Gtk::make_managed<Gtk::Label>("");
  m_find_count_lbl->add_css_class("dim-label");
  m_find_count_lbl->set_width_chars(8);
  m_find_count_lbl->set_halign(Gtk::Align::START);

  m_find_case_btn = Gtk::make_managed<Gtk::CheckButton>("Aa");
  m_find_case_btn->set_tooltip_text("Case sensitive");
  m_find_case_btn->signal_toggled().connect([this] { find_update(); });

  m_find_word_btn = Gtk::make_managed<Gtk::CheckButton>("\\b");
  m_find_word_btn->set_tooltip_text("Whole word");
  m_find_word_btn->signal_toggled().connect([this] { find_update(); });

  m_find_regex_btn = Gtk::make_managed<Gtk::CheckButton>(".*");
  m_find_regex_btn->set_tooltip_text("Regular expression");
  m_find_regex_btn->signal_toggled().connect([this] { find_update(); });

  m_find_replace_toggle = Gtk::make_managed<Gtk::ToggleButton>("Replace");
  m_find_replace_toggle->add_css_class("flat");
  m_find_replace_toggle->set_tooltip_text("Show replace field (Ctrl+H)");
  m_find_replace_toggle->signal_toggled().connect([this] {
    m_replace_revealer.set_reveal_child(m_find_replace_toggle->get_active());
  });

  m_find_close_btn = Gtk::make_managed<Gtk::Button>();
  m_find_close_btn->set_icon_name("window-close-symbolic");
  m_find_close_btn->add_css_class("flat");
  m_find_close_btn->set_tooltip_text("Close (Escape)");
  m_find_close_btn->signal_clicked().connect([this] { close_find(); });

  m_find_row.append(*m_find_replace_toggle);
  m_find_row.append(*m_find_entry);
  m_find_row.append(*m_find_prev_btn);
  m_find_row.append(*m_find_next_btn);
  m_find_row.append(*m_find_count_lbl);
  m_find_row.append(*m_find_case_btn);
  m_find_row.append(*m_find_word_btn);
  m_find_row.append(*m_find_regex_btn);
  m_find_row.append(*m_find_close_btn);

  // ── Replace row ───────────────────────────────────────────────────────────
  m_replace_revealer.set_transition_type(
      Gtk::RevealerTransitionType::SLIDE_DOWN);
  m_replace_revealer.set_transition_duration(100);
  m_replace_revealer.set_reveal_child(false);

  m_replace_row.set_margin_start(8);
  m_replace_row.set_margin_end(8);
  m_replace_row.set_margin_bottom(4);

  m_replace_entry = Gtk::make_managed<Gtk::Entry>();
  m_replace_entry->set_placeholder_text(
      "Replace with…  ($1 $2 … for backrefs)");
  m_replace_entry->set_hexpand(true);
  auto rep_kc = Gtk::EventControllerKey::create();
  rep_kc->signal_key_pressed().connect(
      [this](guint kv, guint, Gdk::ModifierType) -> bool {
        if (kv == GDK_KEY_Return || kv == GDK_KEY_KP_Enter) {
          find_replace_current();
          return true;
        }
        if (kv == GDK_KEY_Escape) {
          close_find();
          return true;
        }
        return false;
      },
      false);
  m_replace_entry->add_controller(rep_kc);

  m_replace_one_btn = Gtk::make_managed<Gtk::Button>("Replace");
  m_replace_one_btn->set_tooltip_text("Replace current match");
  m_replace_one_btn->signal_clicked().connect(
      [this] { find_replace_current(); });

  m_replace_all_btn = Gtk::make_managed<Gtk::Button>("Replace All");
  m_replace_all_btn->set_tooltip_text("Replace all matches in this scene");
  m_replace_all_btn->signal_clicked().connect([this] { find_replace_all(); });

  m_replace_row.append(*m_replace_entry);
  m_replace_row.append(*m_replace_one_btn);
  m_replace_row.append(*m_replace_all_btn);
  m_replace_revealer.set_child(m_replace_row);

  // ── Assemble ──────────────────────────────────────────────────────────────
  auto *sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
  m_find_bar.append(*sep);
  m_find_bar.append(m_find_row);
  m_find_bar.append(m_replace_revealer);
  m_find_revealer.set_child(m_find_bar);
}

void Editor::update_header_toggle_icon() {
  bool shown = m_header_revealer.get_reveal_child();
  m_header_toggle.set_label(shown ? "▾  Title & Path" : "▸  Title & Path");
}

// ─────────────────────────────────────────────────────────────────────────────
// rebuild_extra_menu
// Builds a Gio::Menu + SimpleActionGroup and shows them via a fresh
// Gtk::PopoverMenu parented to m_text_view.  Standard GTK4 pattern.
// ─────────────────────────────────────────────────────────────────────────────

void Editor::rebuild_extra_menu(double x, double y) {
  // Build a Gio::Menu with context-sensitive items and hand it to the
  // TextView via set_extra_menu. GTK merges it with the built-in
  // cut/copy/paste menu and handles all popup positioning itself.

  auto menu = Gio::Menu::create();

  // ── Spell suggestions ────────────────────────────────────────────────────
  int buf_x = 0, buf_y = 0;
  m_text_view.window_to_buffer_coords(Gtk::TextWindowType::TEXT, (int)x, (int)y,
                                      buf_x, buf_y);
  Gtk::TextBuffer::iterator click_iter;
  int trailing = 0;
  m_text_view.get_iter_at_position(click_iter, trailing, buf_x, buf_y);

  auto error_tag = m_buffer->get_tag_table()->lookup("spell-error");
  bool on_error = error_tag && click_iter.has_tag(error_tag);
  std::string misspelled_word;
  int ws_off = -1, we_off = -1;

  if (on_error && m_highlighter) {
    auto ws = click_iter, we = click_iter;
    while (!ws.is_start()) {
      auto prev = ws;
      if (!prev.backward_char())
        break;
      gunichar c = prev.get_char();
      if (!g_unichar_isalpha(c) && c != '\'' && c != 0x2019 && c != '-')
        break;
      ws = prev;
    }
    while (!we.is_end()) {
      gunichar c = we.get_char();
      if (!g_unichar_isalpha(c) && c != '\'' && c != 0x2019 && c != '-')
        break;
      we.forward_char();
    }
    misspelled_word = std::string(m_buffer->get_text(ws, we));
    ws_off = ws.get_offset();
    we_off = we.get_offset();

    auto spell_sec = Gio::Menu::create();
    auto suggs = m_highlighter->get_suggestions(misspelled_word);
    int shown = 0;
    for (const auto &sug : suggs) {
      if (shown >= 6)
        break;
      std::string aname = "spell-sug-" + std::to_string(shown);
      spell_sec->append(sug, "tv." + aname);
      ++shown;
    }
    if (shown == 0)
      spell_sec->append("No Suggestions", "tv.spell-noop");
    spell_sec->append("Ignore Word", "tv.spell-ignore");
    spell_sec->append("Add to Dictionary", "tv.spell-add");
    menu->append_section({}, spell_sec);
  }

  // ── Annotations ──────────────────────────────────────────────────────────
  bool in_ann = false;
  {
    int bx, by;
    m_text_view.window_to_buffer_coords(Gtk::TextWindowType::WIDGET, (int)x,
                                        (int)y, bx, by);
    Gtk::TextBuffer::iterator ann_iter;
    int tr = 0;
    m_text_view.get_iter_at_position(ann_iter, tr, bx, by);
    for (auto &tag : ann_iter.get_tags())
      if (tag->property_name().get_value().substr(0, 4) == "ann:") {
        in_ann = true;
        break;
      }
  }
  bool had_sel = (m_rc_sel_start >= 0 && m_rc_sel_end > m_rc_sel_start);
  {
    auto ann_sec = Gio::Menu::create();
    if (in_ann)
      ann_sec->append("Edit Annotation\u2026", "tv.ann-edit");
    else
      ann_sec->append("Add Annotation\u2026", "tv.ann-add");
    menu->append_section({}, ann_sec);
  }

  // ── Outline ───────────────────────────────────────────────────────────────
  if (current_outline_level() > 0) {
    auto ol_sec = Gio::Menu::create();
    ol_sec->append("Remove Outline Level", "tv.outline-remove");
    menu->append_section({}, ol_sec);
  }

  // ── Insert ────────────────────────────────────────────────────────────────
  {
    auto ins_sec = Gio::Menu::create();
    ins_sec->append("Insert Character\u2026", "tv.insert-char");
    menu->append_section({}, ins_sec);
  }

  // ── Links ─────────────────────────────────────────────────────────────────
  {
    auto link_sec = Gio::Menu::create();
    // Detect if cursor is on a link tag or anchor tag
    bool on_link = false;
    bool on_anchor = false;
    {
      int bx, by;
      m_text_view.window_to_buffer_coords(Gtk::TextWindowType::WIDGET, (int)x,
                                          (int)y, bx, by);
      Gtk::TextBuffer::iterator li;
      int tr = 0;
      m_text_view.get_iter_at_position(li, tr, bx, by);
      on_link = (bool)link_tag_at(li);
      // Check paragraph start for anchor: tag
      int line = li.get_line();
      auto ls = m_buffer->get_iter_at_line(line);
      for (auto &t : ls.get_tags()) {
        std::string tn = t->property_name().get_value();
        if (tn.size() > 7 && tn.substr(0, 7) == "anchor:") {
          on_anchor = true;
          break;
        }
      }
    }
    if (on_link) {
      link_sec->append("Follow Link", "tv.link-follow");
      link_sec->append("Edit Link\u2026", "tv.link-edit");
      link_sec->append("Remove Link", "tv.link-remove");
    } else {
      link_sec->append("Insert Link\u2026", "tv.link-insert");
      if (on_anchor)
        link_sec->append("Remove Anchor", "tv.link-remove-anchor");
      else
        link_sec->append("Set Anchor Here", "tv.link-set-anchor");
    }
    // Back-trace available via the amber diamond in the left gutter
    menu->append_section({}, link_sec);
  }

  // ── Split ─────────────────────────────────────────────────────────────────
  if (m_current_node && !binder_kind_is_group(m_current_node->kind)) {
    auto split_sec = Gio::Menu::create();
    split_sec->append("Split Here", "tv.split-here");
    split_sec->append("Split on Separator\u2026", "tv.split-sep");
    menu->append_section({}, split_sec);
  }

  // ── Wire action group on the TextView ────────────────────────────────────
  // Use a fresh SimpleActionGroup each time so old lambdas don't linger.
  auto ag = Gio::SimpleActionGroup::create();

  auto add = [&](const std::string &name, std::function<void()> fn,
                 bool enabled = true) {
    auto a = Gio::SimpleAction::create(name);
    a->set_enabled(enabled);
    a->signal_activate().connect([fn](const Glib::VariantBase &) { fn(); });
    ag->add_action(a);
  };

  // Spell
  if (!misspelled_word.empty() && m_highlighter) {
    auto suggs = m_highlighter->get_suggestions(misspelled_word);
    int shown = 0;
    for (const auto &sug : suggs) {
      if (shown >= 6)
        break;
      const std::string s = sug;
      add("spell-sug-" + std::to_string(shown), [this, s, ws_off, we_off]() {
        auto ws = m_buffer->get_iter_at_offset(ws_off);
        auto we = m_buffer->get_iter_at_offset(we_off);
        m_buffer->erase(ws, we);
        m_buffer->insert(m_buffer->get_iter_at_offset(ws_off), s);
      });
      ++shown;
    }
    add("spell-noop", []() {}, false);
    const std::string wc = misspelled_word;
    add("spell-ignore", [this, wc]() { spell_ignore_word(wc); });
    add("spell-add", [this, wc]() { spell_add_to_dict(wc); });
  }

  // Annotations
  add(
      "ann-edit",
      [this]() { show_annotation_popover(m_last_rc_x, m_last_rc_y); }, in_ann);
  add(
      "ann-add",
      [this]() { show_annotation_popover(m_last_rc_x, m_last_rc_y); }, had_sel);

  // Outline
  add("outline-remove", [this]() { replace_line_indicator(0); });

  // Insert char
  add("insert-char", [this]() {
    if (!m_char_picker) {
      m_char_picker = std::make_unique<UnicodePickerPopover>(&m_text_view);
      auto *root = dynamic_cast<Gtk::Window *>(get_root());
      if (root)
        m_char_picker->set_parent(*root);
    }
    double wx = 0, wy = 0;
    auto *root = dynamic_cast<Gtk::Window *>(get_root());
    if (root)
      m_text_view.translate_coordinates(*root, m_text_view.get_width() / 2,
                                        m_text_view.get_height() / 2, wx, wy);
    m_char_picker->set_pointing_to(Gdk::Rectangle((int)wx, (int)wy, 1, 1));
    m_char_picker->popup();
  });

  // Split
  add("split-here", [this]() { split_at_cursor(); });
  add("split-sep", [this]() {
    std::string joined;
    for (auto &s : m_prefs.split_separators) {
      if (!joined.empty())
        joined += ",";
      joined += s;
    }
    if (!joined.empty())
      split_on_separator(joined);
  });

  // Links — get click iter for follow/remove/backtrace
  {
    int bx, by;
    m_text_view.window_to_buffer_coords(Gtk::TextWindowType::WIDGET, (int)x,
                                        (int)y, bx, by);
    Gtk::TextBuffer::iterator li;
    int tr = 0;
    m_text_view.get_iter_at_position(li, tr, bx, by);

    add("link-follow", [this, li]() mutable { follow_link_at(li); });
    add("link-remove", [this]() { remove_link_at_cursor(); });
    add("link-insert", [this]() { open_link_picker(); });
    add("link-set-anchor", [this]() { set_anchor_at_cursor(); });
    add("link-remove-anchor", [this]() { remove_anchor_at_cursor(); });
    add("link-edit", [this]() { open_link_picker(); }); // re-opens picker

    // Back-trace: navigate directly if one source, show picker if multiple
    add("link-backtrace", [this]() { do_backtrace(); });
  }

  // Replace the action group and extra menu on the text view
  m_text_view.insert_action_group("tv", ag);
  gtk_text_view_set_extra_menu(m_text_view.gobj(), G_MENU_MODEL(menu->gobj()));
}

// ─────────────────────────────────────────────────────────────────────────────
// rebuild_style_dropdown
// Repopulates the toolbar dropdown from m_prefs.text_styles.
// Index 0 is always the placeholder "(Style…)".
// ─────────────────────────────────────────────────────────────────────────────

void Editor::rebuild_style_dropdown() {
  if (!m_style_dropdown)
    return;

  auto model = Gtk::StringList::create({});
  model->append("Style…"); // index 0 — placeholder

  for (const auto &ts : m_prefs.text_styles) {
    std::string label = (ts.kind == "paragraph" ? "¶ " : "T ") + ts.name;
    model->append(label);
  }

  m_inhibit_style_dd = true;
  m_style_dropdown->set_model(model);
  m_style_dropdown->set_selected(0);
  m_inhibit_style_dd = false;
}

}  // namespace Folio
