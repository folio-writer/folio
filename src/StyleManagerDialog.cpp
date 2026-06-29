// ─────────────────────────────────────────────────────────────────────────────
// Folio — StyleManagerDialog.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "StyleManagerDialog.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <pangomm.h>

#include "RulerUnits.hpp"

namespace Folio {

StyleManagerDialog::StyleManagerDialog(Gtk::Window &parent, FolioPrefs &prefs)
    : Gtk::Window(), m_prefs(prefs) {
  set_title("Style Manager");
  set_transient_for(parent);
  set_modal(true);
  set_default_size(700, 640);
  set_resizable(true);
  add_css_class("folio-style-manager");
  // Group existing styles into Paragraph/Character sections. If this reorders
  // the (shared) vector, the editor dropdown — which maps index → style — is now
  // stale; flush a rebuild when the dialog closes.
  if (regroup_styles())
    m_styles_dirty = true;
  signal_close_request().connect(
      [this]() -> bool {
        if (m_styles_dirty)
          notify_styles_changed();
        return false;
      },
      false);
  build_ui();
  rebuild_style_list();
  if (!m_prefs.text_styles.empty())
    select_row(0);
}

void StyleManagerDialog::build_ui() {
  auto *root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
  set_child(*root);

  // ── Left panel ────────────────────────────────────────────────────────
  m_left_panel.set_size_request(200, -1);
  m_left_panel.add_css_class("style-manager-left");

  auto *list_header =
      Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
  list_header->add_css_class("style-manager-list-header");
  auto *lh_lbl = Gtk::make_managed<Gtk::Label>("Styles");
  lh_lbl->add_css_class("style-manager-section-title");
  lh_lbl->set_hexpand(true);
  lh_lbl->set_xalign(0.0f);
  list_header->append(*lh_lbl);
  m_left_panel.append(*list_header);

  m_style_list.add_css_class("style-manager-listbox");
  m_style_list.set_selection_mode(Gtk::SelectionMode::SINGLE);

  // Section headers: "Paragraph styles" / "Character styles". The vector is kept
  // grouped by kind (regroup_styles), so a header is drawn before the first row
  // and wherever the kind changes.
  m_style_list.set_header_func(
      [this](Gtk::ListBoxRow *row, Gtk::ListBoxRow *before) {
        int i = row->get_index();
        if (i < 0 || i >= (int)m_prefs.text_styles.size()) {
          gtk_list_box_row_set_header(row->gobj(), nullptr);
          return;
        }
        const std::string &kind = m_prefs.text_styles[(size_t)i].kind;
        bool need = (before == nullptr);
        if (!need) {
          int bi = before->get_index();
          if (bi < 0 || bi >= (int)m_prefs.text_styles.size() ||
              m_prefs.text_styles[(size_t)bi].kind != kind)
            need = true;
        }
        if (need) {
          auto *hdr = Gtk::make_managed<Gtk::Label>(
              kind == "paragraph" ? "Paragraph styles" : "Character styles");
          hdr->set_xalign(0.0f);
          hdr->add_css_class("style-section-header");
          hdr->set_margin_start(12);
          hdr->set_margin_top(8);
          hdr->set_margin_bottom(2);
          row->set_header(*hdr);
        } else {
          gtk_list_box_row_set_header(row->gobj(), nullptr);
        }
      });

  // Section-header + drag/drop feedback styling (registered once for the
  // display). Drop indicator is an inset top/bottom rule; the dragged row dims.
  if (!m_dnd_css) {
    m_dnd_css = Gtk::CssProvider::create();
    try {
      m_dnd_css->load_from_data(
          ".style-section-header{font-weight:700;opacity:0.55;font-size:0.82em;"
          "letter-spacing:0.04em;}"
          ".style-row-grip{opacity:0.38;padding:0 2px;font-size:13px;}"
          ".style-row-grip:hover{opacity:0.8;}"
          ".style-row-dragging{opacity:0.4;}"
          ".style-row-drop-before{box-shadow:inset 0 2px 0 0 #4a90d9;}"
          ".style-row-drop-after{box-shadow:inset 0 -2px 0 0 #4a90d9;}");
    } catch (...) {
    }
    if (auto disp = Gdk::Display::get_default())
      Gtk::StyleContext::add_provider_for_display(
          disp, m_dnd_css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
  }

  m_style_list.signal_row_selected().connect([this](Gtk::ListBoxRow *row) {
    if (!row)
      return;
    int idx = row->get_index();
    if (idx >= 0 && idx < (int)m_prefs.text_styles.size())
      load_style_to_editor(idx);
  });
  m_list_scroll.set_child(m_style_list);
  m_list_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  m_list_scroll.set_vexpand(true);
  m_left_panel.append(m_list_scroll);

  m_list_actions.set_margin_top(6);
  m_list_actions.set_margin_bottom(6);
  m_list_actions.set_margin_start(8);
  m_list_actions.set_margin_end(8);
  m_list_actions.add_css_class("style-manager-actions");

  m_btn_add_para.set_label("+ Paragraph");
  m_btn_add_para.add_css_class("pill-btn");
  m_btn_add_para.set_hexpand(true);
  m_btn_add_para.set_tooltip_text("Add a new paragraph style");
  m_btn_add_para.signal_clicked().connect([this]() {
    TextStyle ts;
    ts.kind = "paragraph";
    ts.name = "Paragraph " + std::to_string(m_prefs.text_styles.size() + 1);
    // Insert at the end of the paragraph group so the vector stays grouped
    // (paragraphs first, characters after).
    int nPara = 0;
    for (const auto &s : m_prefs.text_styles)
      if (s.kind == "paragraph")
        ++nPara;
    m_prefs.text_styles.insert(m_prefs.text_styles.begin() + nPara, ts);
    try {
      m_prefs.save();
    } catch (...) {
    }
    rebuild_style_list();
    select_row(nPara);
    notify_styles_changed();
  });

  m_btn_add_char.set_label("+ Character");
  m_btn_add_char.add_css_class("pill-btn");
  m_btn_add_char.set_hexpand(true);
  m_btn_add_char.set_tooltip_text("Add a new character style");
  m_btn_add_char.signal_clicked().connect([this]() {
    TextStyle ts;
    ts.kind = "character";
    ts.name = "Character " + std::to_string(m_prefs.text_styles.size() + 1);
    m_prefs.text_styles.push_back(ts);
    try {
      m_prefs.save();
    } catch (...) {
    }
    rebuild_style_list();
    select_row((int)m_prefs.text_styles.size() - 1);
    notify_styles_changed();
  });

  m_btn_delete.set_icon_name("user-trash-symbolic");
  m_btn_delete.add_css_class("pill-btn");
  m_btn_delete.set_tooltip_text("Delete selected style");
  m_btn_delete.signal_clicked().connect([this]() {
    if (m_selected_idx < 0 || m_selected_idx >= (int)m_prefs.text_styles.size())
      return;
    m_prefs.text_styles.erase(m_prefs.text_styles.begin() + m_selected_idx);
    try {
      m_prefs.save();
    } catch (...) {
    }
    int new_sel = std::min(m_selected_idx, (int)m_prefs.text_styles.size() - 1);
    m_selected_idx = -1;
    rebuild_style_list();
    if (new_sel >= 0)
      select_row(new_sel);
    else
      m_editor_stack.set_visible_child("empty");
    notify_styles_changed();
  });

  auto *add_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
  add_box->set_hexpand(true);
  add_box->append(m_btn_add_para);
  add_box->append(m_btn_add_char);
  m_list_actions.append(*add_box);
  m_list_actions.append(m_btn_delete);
  m_left_panel.append(m_list_actions);

  // Reset to defaults button — below the actions bar
  m_btn_reset.set_label("Reset to Defaults");
  m_btn_reset.add_css_class("flat");
  m_btn_reset.set_margin_start(8);
  m_btn_reset.set_margin_end(8);
  m_btn_reset.set_margin_bottom(8);
  m_btn_reset.set_tooltip_text("Replace all styles with the built-in defaults");
  m_btn_reset.signal_clicked().connect([this]() {
    m_prefs.text_styles = m_prefs.default_styles();
    regroup_styles();
    try { m_prefs.save(); } catch (...) {}
    m_selected_idx = -1;
    rebuild_style_list();
    m_editor_stack.set_visible_child("empty");
    if (!m_prefs.text_styles.empty())
      select_row(0);
    notify_styles_changed();
  });
  m_left_panel.append(m_btn_reset);

  root->append(m_left_panel);
  root->append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL));

  // ── Right panel ───────────────────────────────────────────────────────
  m_right_panel.set_hexpand(true);
  m_right_panel.add_css_class("style-manager-right");

  auto *empty_lbl = Gtk::make_managed<Gtk::Label>("Select or create a style");
  empty_lbl->add_css_class("dim-label");
  empty_lbl->set_valign(Gtk::Align::CENTER);
  empty_lbl->set_halign(Gtk::Align::CENTER);
  empty_lbl->set_vexpand(true);
  empty_lbl->set_hexpand(true);
  m_editor_stack.add(*empty_lbl, "empty");

  auto *form_scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
  form_scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  form_scroll->set_vexpand(true);
  form_scroll->set_child(m_editor_pane);
  m_editor_pane.set_margin_top(16);
  m_editor_pane.set_margin_bottom(16);
  m_editor_pane.set_margin_start(20);
  m_editor_pane.set_margin_end(20);

  auto make_lbl = [](const std::string &t) -> Gtk::Label * {
    auto *l = Gtk::make_managed<Gtk::Label>(t);
    l->set_xalign(0.0f);
    l->set_width_chars(12);
    l->add_css_class("stat-label");
    return l;
  };
  auto make_hrow = [](int sp = 8) -> Gtk::Box * {
    auto *r = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, sp);
    r->set_valign(Gtk::Align::CENTER);
    return r;
  };

  // Name
  auto *name_row = make_hrow();
  name_row->append(*make_lbl("Name"));
  m_name_entry.set_hexpand(true);
  m_name_entry.set_placeholder_text("Style name...");
  m_name_entry.signal_changed().connect([this]() { update_preview(); });
  name_row->append(m_name_entry);
  m_editor_pane.append(*name_row);
  m_editor_pane.append(
      *Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL));

  // Font family
  {
    PangoFontMap *fm = pango_cairo_font_map_get_default();
    PangoFontFamily **families = nullptr;
    int n = 0;
    pango_font_map_list_families(fm, &families, &n);
    m_font_names.clear();
    m_font_names.emplace_back("");
    for (int fi = 0; fi < n; ++fi)
      m_font_names.emplace_back(pango_font_family_get_name(families[fi]));
    g_free(families);
    std::sort(m_font_names.begin() + 1, m_font_names.end());
  }
  auto font_model = Gtk::StringList::create({});
  font_model->append("(inherit)");
  for (size_t fi = 1; fi < m_font_names.size(); ++fi)
    font_model->append(m_font_names[fi]);
  m_font_dd = Gtk::make_managed<Gtk::DropDown>(font_model);
  m_font_dd->set_enable_search(true);
  m_font_dd->set_hexpand(true);
  m_font_dd->set_selected(0);
  m_font_dd->property_selected().signal_changed().connect([this]() {
    if (!m_inhibit)
      update_preview();
  });
  auto *font_row = make_hrow();
  font_row->append(*make_lbl("Font"));
  font_row->append(*m_font_dd);
  m_editor_pane.append(*font_row);

  // Size
  m_size_spin.set_adjustment(Gtk::Adjustment::create(0, 0, 96, 1, 4));
  m_size_spin.set_digits(0);
  m_size_spin.set_numeric(true);
  m_size_spin.set_snap_to_ticks(true);
  m_size_spin.set_width_chars(4);
  m_size_spin.set_tooltip_text("Font size in pt  (0 = inherit)");
  m_size_spin.signal_value_changed().connect([this]() {
    if (!m_inhibit)
      update_preview();
  });
  auto *size_row = make_hrow();
  size_row->append(*make_lbl("Size (pt)"));
  size_row->append(m_size_spin);
  auto *size_hint = Gtk::make_managed<Gtk::Label>("0 = inherit");
  size_hint->add_css_class("stat-label");
  size_row->append(*size_hint);
  m_editor_pane.append(*size_row);

  // Bold / italic / underline
  m_btn_bold.set_label("B");
  m_btn_bold.add_css_class("fmt-btn");
  m_btn_bold.signal_toggled().connect([this]() {
    if (!m_inhibit)
      update_preview();
  });
  m_btn_italic.set_label("I");
  m_btn_italic.add_css_class("fmt-btn");
  m_btn_italic.signal_toggled().connect([this]() {
    if (!m_inhibit)
      update_preview();
  });
  m_btn_underline.set_label("U");
  m_btn_underline.add_css_class("fmt-btn");
  m_btn_underline.signal_toggled().connect([this]() {
    if (!m_inhibit)
      update_preview();
  });
  auto *style_row = make_hrow(2);
  style_row->append(*make_lbl("Style"));
  auto *style_grp =
      Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 2);
  style_grp->add_css_class("view-toggle-group");
  style_grp->append(m_btn_bold);
  style_grp->append(m_btn_italic);
  style_grp->append(m_btn_underline);
  style_row->append(*style_grp);
  m_editor_pane.append(*style_row);

  // Justification (paragraph only)
  m_just_left.set_label("Left");
  m_just_left.set_tooltip_text("Align left");
  m_just_center.set_label("Center");
  m_just_center.set_tooltip_text("Center");
  m_just_right.set_label("Right");
  m_just_right.set_tooltip_text("Right");
  m_just_full.set_label("Justify");
  m_just_full.set_tooltip_text("Justify");
  m_just_center.set_group(m_just_left);
  m_just_right.set_group(m_just_left);
  m_just_full.set_group(m_just_left);
  m_just_left.set_active(true);
  for (auto *b : {&m_just_left, &m_just_center, &m_just_right, &m_just_full}) {
    b->add_css_class("fmt-btn");
    m_just_box.append(*b);
  }
  m_just_box.add_css_class("view-toggle-group");
  m_just_left.signal_toggled().connect([this]() {
    if (!m_inhibit)
      update_preview();
  });
  m_just_row = make_hrow();
  m_just_row->append(*make_lbl("Align"));
  m_just_row->append(m_just_box);
  m_editor_pane.append(*m_just_row);

  // ── Colour rows ───────────────────────────────────────────────────────
  // Each row:  [checkbox "Text color"]  [■ swatch]  [⊘ icon = transparent]
  //
  // Checkbox UNCHECKED  → colour is "default / inherit" — nothing saved.
  //                       Swatch is insensitive (dimmed).
  // Checkbox CHECKED    → a specific colour is saved.
  //                       Swatch is sensitive; click to change colour.
  // Transparent button  → saves a fully-transparent colour tag.
  //                       This HIDES text or clears a highlight visually
  //                       while keeping a real tag in the buffer.
  //                       Tooltip explains this distinction.
  {
    // Text colour
    m_chk_fg.set_label("Text color");
    m_chk_fg.signal_toggled().connect([this]() {
      if (m_inhibit)
        return;
      m_fg_set = m_chk_fg.get_active();
      if (m_color_btn)
        m_color_btn->set_sensitive(m_fg_set);
      update_preview();
    });

    auto fg_dlg = Gtk::ColorDialog::create();
    fg_dlg->set_with_alpha(false);
    fg_dlg->set_title("Text color");
    m_color_btn = Gtk::make_managed<Gtk::ColorDialogButton>(fg_dlg);
    m_color_btn->set_sensitive(false);
    m_color_btn->set_tooltip_text("Choose a text colour");
    m_color_btn->property_rgba().signal_changed().connect([this]() {
      if (!m_inhibit) {
        m_fg_transparent = false; // real colour chosen, clear transparent flag
        update_preview();
      }
    });

    m_btn_fg_trans = Gtk::make_managed<Gtk::Button>();
    m_btn_fg_trans->set_icon_name("window-close-symbolic");
    m_btn_fg_trans->add_css_class("color-trans-btn");
    m_btn_fg_trans->set_tooltip_text(
        "Apply transparent text colour.\nThe text becomes invisible but a "
        "colour tag remains in the\nbuffer. Use this to intentionally hide "
        "text or override an\ninherited colour with full transparency.");
    m_btn_fg_trans->signal_clicked().connect([this]() {
      // Store the special sentinel value for transparent
      m_fg_set = true;
      m_chk_fg.set_active(true);
      if (m_color_btn) {
        Gdk::RGBA t;
        t.set_rgba(0, 0, 0, 0);
        m_inhibit = true;
        m_color_btn->set_rgba(t);
        m_inhibit = false;
      }
      // Mark as transparent in state
      m_fg_transparent = true;
      update_preview();
    });

    auto *fg_row = make_hrow(6);
    fg_row->append(m_chk_fg);
    fg_row->append(*m_color_btn);
    fg_row->append(*m_btn_fg_trans);
    m_editor_pane.append(*fg_row);
  }
  {
    // Background colour
    m_chk_bg.set_label("Background");
    m_chk_bg.signal_toggled().connect([this]() {
      if (m_inhibit)
        return;
      m_bg_set = m_chk_bg.get_active();
      if (m_bg_btn)
        m_bg_btn->set_sensitive(m_bg_set);
      update_preview();
    });

    auto bg_dlg = Gtk::ColorDialog::create();
    bg_dlg->set_with_alpha(false);
    bg_dlg->set_title("Background / highlight color");
    m_bg_btn = Gtk::make_managed<Gtk::ColorDialogButton>(bg_dlg);
    m_bg_btn->set_sensitive(false);
    m_bg_btn->set_tooltip_text("Choose a background / highlight colour");
    m_bg_btn->property_rgba().signal_changed().connect([this]() {
      if (!m_inhibit) {
        m_bg_transparent = false; // real colour chosen, clear transparent flag
        update_preview();
      }
    });

    m_btn_bg_trans = Gtk::make_managed<Gtk::Button>();
    m_btn_bg_trans->set_icon_name("window-close-symbolic");
    m_btn_bg_trans->add_css_class("color-trans-btn");
    m_btn_bg_trans->set_tooltip_text(
        "Apply transparent background colour.\nRemoves any visible highlight "
        "while keeping a colour tag in\nthe buffer. Useful for overriding an "
        "inherited background with\nfull transparency.");
    m_btn_bg_trans->signal_clicked().connect([this]() {
      m_bg_set = true;
      m_chk_bg.set_active(true);
      if (m_bg_btn) {
        Gdk::RGBA t;
        t.set_rgba(0, 0, 0, 0);
        m_inhibit = true;
        m_bg_btn->set_rgba(t);
        m_inhibit = false;
      }
      m_bg_transparent = true;
      update_preview();
    });

    auto *bg_row = make_hrow(6);
    bg_row->append(m_chk_bg);
    bg_row->append(*m_bg_btn);
    bg_row->append(*m_btn_bg_trans);
    m_editor_pane.append(*bg_row);
  }

  // Line height (paragraph only)
  m_lh_spin = Gtk::make_managed<Gtk::SpinButton>();
  m_lh_spin->set_adjustment(Gtk::Adjustment::create(0.0, 0.0, 5.0, 0.1, 0.5));
  m_lh_spin->set_digits(1);
  m_lh_spin->set_numeric(true);
  m_lh_spin->set_width_chars(4);
  m_lh_spin->set_tooltip_text("Line height multiplier  (0 = inherit)");
  m_lh_spin->signal_value_changed().connect([this]() {
    if (!m_inhibit)
      update_preview();
  });
  m_lh_row = make_hrow();
  m_lh_row->append(*make_lbl("Line height"));
  m_lh_row->append(*m_lh_spin);
  auto *lh_hint = Gtk::make_managed<Gtk::Label>("0 = inherit");
  lh_hint->add_css_class("stat-label");
  m_lh_row->append(*lh_hint);
  m_editor_pane.append(*m_lh_row);

  // Space above (paragraph only) — s88
  m_space_above_spin = Gtk::make_managed<Gtk::SpinButton>();
  m_space_above_spin->set_adjustment(Gtk::Adjustment::create(0, 0, 144, 1, 6));
  m_space_above_spin->set_digits(0);
  m_space_above_spin->set_numeric(true);
  m_space_above_spin->set_width_chars(4);
  m_space_above_spin->set_tooltip_text("Extra space above the paragraph, in pt");
  m_space_above_spin->signal_value_changed().connect([this]() {
    if (!m_inhibit)
      update_preview();
  });
  m_space_above_row = make_hrow();
  m_space_above_row->append(*make_lbl("Space above"));
  m_space_above_row->append(*m_space_above_spin);
  {
    auto *u = Gtk::make_managed<Gtk::Label>("pt");
    u->add_css_class("stat-label");
    m_space_above_row->append(*u);
  }
  m_editor_pane.append(*m_space_above_row);

  // Space below (paragraph only) — s88
  m_space_below_spin = Gtk::make_managed<Gtk::SpinButton>();
  m_space_below_spin->set_adjustment(Gtk::Adjustment::create(0, 0, 144, 1, 6));
  m_space_below_spin->set_digits(0);
  m_space_below_spin->set_numeric(true);
  m_space_below_spin->set_width_chars(4);
  m_space_below_spin->set_tooltip_text("Extra space below the paragraph, in pt");
  m_space_below_spin->signal_value_changed().connect([this]() {
    if (!m_inhibit)
      update_preview();
  });
  m_space_below_row = make_hrow();
  m_space_below_row->append(*make_lbl("Space below"));
  m_space_below_row->append(*m_space_below_spin);
  {
    auto *u = Gtk::make_managed<Gtk::Label>("pt");
    u->add_css_class("stat-label");
    m_space_below_row->append(*u);
  }
  m_editor_pane.append(*m_space_below_row);

  // First-line indent (paragraph only, tri-state) — s88
  m_indent_spin = Gtk::make_managed<Gtk::SpinButton>();
  m_indent_spin->set_adjustment(Gtk::Adjustment::create(0, 0, 144, 1, 6));
  m_indent_spin->set_digits(0);
  m_indent_spin->set_numeric(true);
  m_indent_spin->set_width_chars(4);
  m_indent_spin->set_tooltip_text(
      "First-line indent, in pt  (0 = none). Used when 'Inherit global' is off");
  m_indent_spin->signal_value_changed().connect([this]() {
    if (!m_inhibit)
      update_preview();
  });
  m_indent_inherit.set_label("Inherit global");
  m_indent_inherit.set_tooltip_text(
      "Use the global first-line indent preference for this style");
  m_indent_inherit.signal_toggled().connect([this]() {
    if (m_indent_spin)
      m_indent_spin->set_sensitive(!m_indent_inherit.get_active());
    if (!m_inhibit)
      update_preview();
  });
  m_indent_row = make_hrow();
  m_indent_row->append(*make_lbl("First-line indent"));
  m_indent_row->append(m_indent_inherit);
  m_indent_row->append(*m_indent_spin);
  {
    auto *u = Gtk::make_managed<Gtk::Label>("pt");
    u->add_css_class("stat-label");
    m_indent_row->append(*u);
  }
  m_editor_pane.append(*m_indent_row);

  // Preview
  m_editor_pane.append(
      *Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL));
  auto *prev_lbl = Gtk::make_managed<Gtk::Label>("Preview");
  prev_lbl->set_xalign(0.0f);
  prev_lbl->add_css_class("stat-label");
  m_editor_pane.append(*prev_lbl);
  m_preview.set_text("The quick brown fox jumps over the lazy dog.");
  m_preview.set_wrap(true);
  m_preview.set_xalign(0.0f);
  m_preview.set_margin_top(4);
  m_preview.set_margin_bottom(8);
  m_preview.add_css_class("style-manager-preview");
  m_editor_pane.append(m_preview);

  // Save button
  auto *btn_row = make_hrow();
  btn_row->set_halign(Gtk::Align::END);
  m_btn_save.set_label("Save Style");
  m_btn_save.add_css_class("pill-btn");
  m_btn_save.add_css_class("pill-btn-primary");
  m_btn_save.signal_clicked().connect([this]() { save_editor_to_style(); });
  btn_row->append(m_btn_save);
  m_editor_pane.append(*btn_row);

  m_editor_stack.add(*form_scroll, "editor");
  m_editor_stack.set_visible_child("empty");
  m_editor_stack.set_hexpand(true);
  m_editor_stack.set_vexpand(true);
  m_right_panel.append(m_editor_stack);
  root->append(m_right_panel);
}

void StyleManagerDialog::rebuild_style_list() {
  while (auto *row = m_style_list.get_row_at_index(0))
    m_style_list.remove(*row);
  for (int i = 0; i < (int)m_prefs.text_styles.size(); ++i)
    m_style_list.append(*make_style_row(m_prefs.text_styles[(size_t)i], i));
}

bool StyleManagerDialog::regroup_styles() {
  // Detect interleaving: a paragraph style appearing after any character style
  // means the vector is not grouped. Stable-partition only if needed so the
  // common (already-grouped) case is a no-op and never flags the editor stale.
  bool seen_char = false, needs = false;
  for (const auto &s : m_prefs.text_styles) {
    if (s.kind == "character")
      seen_char = true;
    else if (seen_char) {
      needs = true;
      break;
    }
  }
  if (needs)
    std::stable_partition(
        m_prefs.text_styles.begin(), m_prefs.text_styles.end(),
        [](const TextStyle &s) { return s.kind == "paragraph"; });
  return needs;
}

void StyleManagerDialog::notify_styles_changed() {
  m_styles_dirty = false;
  if (on_styles_changed)
    on_styles_changed();
}

void StyleManagerDialog::reorder_style(int src, int dst, bool after) {
  int n = (int)m_prefs.text_styles.size();
  if (src < 0 || src >= n || dst < 0 || dst >= n)
    return;
  // Same-section only — never let a paragraph land among character styles.
  if (m_prefs.text_styles[(size_t)src].kind !=
      m_prefs.text_styles[(size_t)dst].kind)
    return;
  int target = after ? dst + 1 : dst; // desired slot in current indexing
  if (src == target || (after && src == dst))
    return;
  TextStyle moved = m_prefs.text_styles[(size_t)src];
  m_prefs.text_styles.erase(m_prefs.text_styles.begin() + src);
  if (src < target)
    --target; // erase shifted everything after src down by one
  if (target < 0)
    target = 0;
  if (target > (int)m_prefs.text_styles.size())
    target = (int)m_prefs.text_styles.size();
  m_prefs.text_styles.insert(m_prefs.text_styles.begin() + target, moved);
  // A same-kind move within a grouped vector can't break grouping.
  try {
    m_prefs.save();
  } catch (...) {
  }
  rebuild_style_list();
  select_row(target);
  notify_styles_changed();
}

Gtk::ListBoxRow *StyleManagerDialog::make_style_row(const TextStyle &s,
                                                    int idx) {
  auto *row = Gtk::make_managed<Gtk::ListBoxRow>();
  auto *box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  box->set_margin_top(6);
  box->set_margin_bottom(6);
  box->set_margin_start(12);
  box->set_margin_end(8);
  // paragraph sign U+00B6 = 0xC2 0xB6
  const char *pill_lbl = (s.kind == "paragraph") ? "\xc2\xb6" : "T";
  auto *kind_pill = Gtk::make_managed<Gtk::Label>(pill_lbl);
  kind_pill->add_css_class("style-kind-pill");
  kind_pill->add_css_class(s.kind == "paragraph" ? "style-kind-para"
                                                 : "style-kind-char");
  box->append(*kind_pill);
  auto *name_lbl = Gtk::make_managed<Gtk::Label>(s.name);
  name_lbl->set_hexpand(true);
  name_lbl->set_xalign(0.0f);
  box->append(*name_lbl);
  // Drag-handle affordance (house standard: ☰ grip). The whole row is the drag
  // source, so grabbing the grip reorders too; it just signals the capability.
  auto *grip = Gtk::make_managed<Gtk::Label>("\u2630");
  grip->add_css_class("style-row-grip");
  grip->set_tooltip_text("Drag to reorder within its section");
  box->append(*grip);
  row->set_child(*box);

  // ── Drag-and-drop reordering (within the same section) ─────────────────
  // The dragged style carries its current index as an int. Drops are accepted
  // only onto rows of the same kind, so paragraph and character styles stay in
  // their own sections. A click still selects (GTK separates click from drag).
  auto alive = std::make_shared<bool>(true);
  row->signal_destroy().connect([alive] { *alive = false; });

  auto src = Gtk::DragSource::create();
  src->set_actions(Gdk::DragAction::MOVE);
  src->signal_prepare().connect(
      [this, idx](double, double) -> Glib::RefPtr<Gdk::ContentProvider> {
        m_drag_src_idx = idx;
        Glib::Value<int> v;
        v.init(G_TYPE_INT);
        v.set(idx);
        return Gdk::ContentProvider::create(v);
      },
      false);
  src->signal_drag_begin().connect(
      [row, alive](const Glib::RefPtr<Gdk::Drag> &) {
        if (*alive)
          row->add_css_class("style-row-dragging");
      },
      false);
  src->signal_drag_end().connect(
      [this, row, alive](const Glib::RefPtr<Gdk::Drag> &, bool) {
        m_drag_src_idx = -1;
        if (*alive)
          row->remove_css_class("style-row-dragging");
      },
      false);
  row->add_controller(src);

  auto dst = Gtk::DropTarget::create(G_TYPE_INT, Gdk::DragAction::MOVE);
  auto clear_hl = [row, alive] {
    if (!*alive)
      return;
    row->remove_css_class("style-row-drop-before");
    row->remove_css_class("style-row-drop-after");
  };
  dst->signal_motion().connect(
      [this, idx, row, alive, clear_hl](double, double y) -> Gdk::DragAction {
        if (!*alive)
          return Gdk::DragAction{};
        // Same-section only: reject if the dragged style's kind differs.
        if (m_drag_src_idx < 0 ||
            m_drag_src_idx >= (int)m_prefs.text_styles.size() ||
            idx >= (int)m_prefs.text_styles.size() ||
            m_prefs.text_styles[(size_t)m_drag_src_idx].kind !=
                m_prefs.text_styles[(size_t)idx].kind ||
            m_drag_src_idx == idx)
          return Gdk::DragAction{};
        bool after = (y > row->get_height() * 0.5);
        clear_hl();
        row->add_css_class(after ? "style-row-drop-after"
                                 : "style-row-drop-before");
        return Gdk::DragAction::MOVE;
      },
      false);
  dst->signal_leave().connect([clear_hl] { clear_hl(); }, false);
  dst->signal_drop().connect(
      [this, idx, row, alive, clear_hl](const Glib::ValueBase &value, double,
                                        double y) -> bool {
        if (!*alive)
          return false;
        clear_hl();
        const GValue *gv = value.gobj();
        if (!gv || !G_VALUE_HOLDS_INT(gv))
          return false;
        int src_idx = g_value_get_int(gv);
        bool after = (y > row->get_height() * 0.5);
        reorder_style(src_idx, idx, after);
        return true;
      },
      false);
  row->add_controller(dst);

  return row;
}

void StyleManagerDialog::select_row(int idx) {
  auto *row = m_style_list.get_row_at_index(idx);
  if (row) {
    m_style_list.select_row(*row);
    load_style_to_editor(idx);
  }
}

void StyleManagerDialog::load_style_to_editor(int idx) {
  if (idx < 0 || idx >= (int)m_prefs.text_styles.size())
    return;
  m_selected_idx = idx;
  const TextStyle &ts = m_prefs.text_styles[idx];
  m_inhibit = true;

  m_name_entry.set_text(ts.name);

  if (m_font_dd) {
    guint sel = 0;
    for (size_t fi = 0; fi < m_font_names.size(); ++fi)
      if (m_font_names[fi] == ts.font_family) {
        sel = (guint)fi;
        break;
      }
    m_font_dd->set_selected(sel);
  }
  m_size_spin.set_value(ts.font_size);
  m_btn_bold.set_active(ts.bold);
  m_btn_italic.set_active(ts.italic);
  m_btn_underline.set_active(ts.underline);

  bool is_para = (ts.kind == "paragraph");
  if (m_just_row)
    m_just_row->set_visible(is_para);
  if (m_lh_row)
    m_lh_row->set_visible(is_para);
  if (m_space_above_row)
    m_space_above_row->set_visible(is_para);
  if (m_space_below_row)
    m_space_below_row->set_visible(is_para);
  if (m_indent_row)
    m_indent_row->set_visible(is_para);
  if (is_para) {
    if (ts.justification == "center")
      m_just_center.set_active(true);
    else if (ts.justification == "right")
      m_just_right.set_active(true);
    else if (ts.justification == "full")
      m_just_full.set_active(true);
    else
      m_just_left.set_active(true);
  }
  if (m_lh_spin)
    m_lh_spin->set_value(ts.line_height);

  // Paragraph spacing + first-line indent (s88). Stored in px (GTK's native
  // spacing unit); shown/edited in pt for consistency with font size + ruler.
  if (m_space_above_spin)
    m_space_above_spin->set_value(
        std::round(RulerUnits::px_to_pt(ts.space_above_px)));
  if (m_space_below_spin)
    m_space_below_spin->set_value(
        std::round(RulerUnits::px_to_pt(ts.space_below_px)));
  {
    bool inherit = (ts.first_line_indent_px < 0);
    m_indent_inherit.set_active(inherit);
    if (m_indent_spin) {
      m_indent_spin->set_value(
          inherit ? 0 : std::round(RulerUnits::px_to_pt(ts.first_line_indent_px)));
      m_indent_spin->set_sensitive(!inherit);
    }
  }

  // Text colour
  m_fg_transparent = (ts.fg_color == "transparent");
  m_fg_set = !ts.fg_color.empty();
  m_chk_fg.set_active(m_fg_set);
  if (m_color_btn) {
    m_color_btn->set_sensitive(m_fg_set);
    Gdk::RGBA rgba;
    if (m_fg_transparent)
      rgba.set_rgba(0, 0, 0, 0);
    else
      rgba.set(m_fg_set ? ts.fg_color : "#cc3333");
    m_color_btn->set_rgba(rgba);
  }

  // Background colour
  m_bg_transparent = (ts.bg_color == "transparent");
  m_bg_set = !ts.bg_color.empty();
  m_chk_bg.set_active(m_bg_set);
  if (m_bg_btn) {
    m_bg_btn->set_sensitive(m_bg_set);
    Gdk::RGBA rgba;
    if (m_bg_transparent)
      rgba.set_rgba(0, 0, 0, 0);
    else
      rgba.set(m_bg_set ? ts.bg_color : "#f9e2af");
    m_bg_btn->set_rgba(rgba);
  }

  m_inhibit = false;
  m_editor_stack.set_visible_child("editor");
  update_preview();
}

void StyleManagerDialog::save_editor_to_style() {
  if (m_selected_idx < 0 || m_selected_idx >= (int)m_prefs.text_styles.size())
    return;
  TextStyle &ts = m_prefs.text_styles[m_selected_idx];

  std::string new_name = m_name_entry.get_text();
  if (!new_name.empty())
    ts.name = new_name;

  if (m_font_dd) {
    guint sel = m_font_dd->get_selected();
    ts.font_family = (sel < m_font_names.size()) ? m_font_names[sel] : "";
  }
  ts.font_size = (int)m_size_spin.get_value();
  ts.bold = m_btn_bold.get_active();
  ts.italic = m_btn_italic.get_active();
  ts.underline = m_btn_underline.get_active();

  if (ts.kind == "paragraph") {
    if (m_just_center.get_active())
      ts.justification = "center";
    else if (m_just_right.get_active())
      ts.justification = "right";
    else if (m_just_full.get_active())
      ts.justification = "full";
    else
      ts.justification = "left";
    ts.line_height = m_lh_spin ? m_lh_spin->get_value() : 0.0;
    ts.space_above_px =
        m_space_above_spin
            ? (int)std::lround(RulerUnits::pt_to_px(m_space_above_spin->get_value()))
            : 0;
    ts.space_below_px =
        m_space_below_spin
            ? (int)std::lround(RulerUnits::pt_to_px(m_space_below_spin->get_value()))
            : 0;
    ts.first_line_indent_px =
        m_indent_inherit.get_active()
            ? -1
            : (m_indent_spin
                   ? (int)std::lround(RulerUnits::pt_to_px(m_indent_spin->get_value()))
                   : 0);
  } else {
    ts.justification = "";
    ts.line_height = 0.0;
    ts.space_above_px = 0;
    ts.space_below_px = 0;
    ts.first_line_indent_px = -1;
  }

  if (m_fg_set && m_color_btn) {
    if (m_fg_transparent) {
      ts.fg_color = "transparent";
    } else {
      Gdk::RGBA c = m_color_btn->get_rgba();
      char buf[16];
      std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", (int)(c.get_red() * 255),
                    (int)(c.get_green() * 255), (int)(c.get_blue() * 255));
      ts.fg_color = buf;
    }
  } else {
    ts.fg_color = "";
  }

  if (m_bg_set && m_bg_btn) {
    if (m_bg_transparent) {
      ts.bg_color = "transparent";
    } else {
      Gdk::RGBA c = m_bg_btn->get_rgba();
      char buf[16];
      std::snprintf(buf, sizeof(buf), "#%02x%02x%02x", (int)(c.get_red() * 255),
                    (int)(c.get_green() * 255), (int)(c.get_blue() * 255));
      ts.bg_color = buf;
    }
  } else {
    ts.bg_color = "";
  }

  try {
    m_prefs.save();
  } catch (...) {
  }
  rebuild_style_list();
  select_row(m_selected_idx);
  notify_styles_changed();
}

void StyleManagerDialog::update_preview() {
  if (!m_color_btn)
    return;
  if (!m_preview_css) {
    m_preview_css = Gtk::CssProvider::create();
    m_preview.get_style_context()->add_provider(
        m_preview_css, GTK_STYLE_PROVIDER_PRIORITY_USER + 2);
  }

  guint font_sel = m_font_dd ? m_font_dd->get_selected() : 0;
  std::string ff =
      (font_sel < m_font_names.size()) ? m_font_names[font_sel] : "";
  int sz = (int)m_size_spin.get_value();
  bool b = m_btn_bold.get_active();
  bool it = m_btn_italic.get_active();
  bool un = m_btn_underline.get_active();

  std::string css = ".style-manager-preview {";
  if (!ff.empty())
    css += "font-family:'" + ff + "';";
  if (sz > 0)
    css += "font-size:" + std::to_string(sz) + "pt;";
  if (b)
    css += "font-weight:bold;";
  if (it)
    css += "font-style:italic;";
  if (un)
    css += "text-decoration:underline;";
  if (m_fg_set && m_color_btn) {
    if (m_fg_transparent) {
      css += "color:rgba(0,0,0,0);";
    } else {
      Gdk::RGBA c = m_color_btn->get_rgba();
      char buf[64];
      std::snprintf(buf, sizeof(buf), "color:rgb(%d,%d,%d);",
                    (int)(c.get_red() * 255), (int)(c.get_green() * 255),
                    (int)(c.get_blue() * 255));
      css += buf;
    }
  }
  if (m_bg_set && m_bg_btn) {
    if (m_bg_transparent) {
      css += "background-color:rgba(0,0,0,0);";
    } else {
      Gdk::RGBA c = m_bg_btn->get_rgba();
      char buf[64];
      std::snprintf(buf, sizeof(buf), "background-color:rgb(%d,%d,%d);",
                    (int)(c.get_red() * 255), (int)(c.get_green() * 255),
                    (int)(c.get_blue() * 255));
      css += buf;
    }
  }
  css += "}";
  try {
    m_preview_css->load_from_data(css);
  } catch (...) {
  }

  Gtk::Justification just = Gtk::Justification::LEFT;
  if (m_just_center.get_active())
    just = Gtk::Justification::CENTER;
  else if (m_just_right.get_active())
    just = Gtk::Justification::RIGHT;
  else if (m_just_full.get_active())
    just = Gtk::Justification::FILL;
  m_preview.set_justify(just);
}

} // namespace Folio
