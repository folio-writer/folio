#include "ShortcutsDialog.hpp"

namespace Folio {

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────
ShortcutsDialog::ShortcutsDialog() {
  set_name("shortcuts-dialog");
  set_title("Keyboard Shortcuts");
  set_modal(false);
  set_resizable(true);
  set_default_size(640, 720);
  set_hide_on_close(true);   // built once by MainWindow, reused on every open

  m_notebook.set_name("shortcuts-notebook");
  m_notebook.set_expand(true);
  m_notebook.append_page(
      build_tab(m_keyboard_grid, m_keyboard_scroll, ShortcutTab::Keyboard),
      "Keyboard");
  m_notebook.append_page(
      build_tab(m_mouse_grid, m_mouse_scroll, ShortcutTab::Mouse),
      "Mouse & Modifiers");

  m_btn_close.set_name("shortcuts-close");
  m_btn_close.set_halign(Gtk::Align::END);
  m_btn_close.set_margin(8);
  m_btn_close.signal_clicked().connect([this]() { set_visible(false); });

  m_root.append(m_notebook);
  m_root.append(m_btn_close);
  set_child(m_root);
}

void ShortcutsDialog::show(Gtk::Window& parent) {
  set_transient_for(parent);
  present();
}

// ─────────────────────────────────────────────────────────────────────────────
// Grid helpers
// ─────────────────────────────────────────────────────────────────────────────
int ShortcutsDialog::add_heading(Gtk::Grid& grid, const std::string& title,
                                 int row) {
  auto* lbl = Gtk::make_managed<Gtk::Label>(title);
  lbl->set_halign(Gtk::Align::START);
  lbl->set_margin_top(12);
  lbl->set_margin_bottom(2);
  lbl->add_css_class("heading");
  grid.attach(*lbl, 0, row, 2, 1);
  auto* sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
  sep->set_margin_bottom(4);
  grid.attach(*sep, 0, row + 1, 2, 1);
  return row + 2;
}

int ShortcutsDialog::add_row(Gtk::Grid& grid, const std::string& keys,
                             const std::string& desc, int row) {
  auto* key_lbl = Gtk::make_managed<Gtk::Label>(keys);
  key_lbl->set_halign(Gtk::Align::START);
  key_lbl->set_valign(Gtk::Align::START);
  key_lbl->set_margin_start(8);
  key_lbl->set_margin_end(16);
  key_lbl->set_margin_top(2);
  key_lbl->set_margin_bottom(2);
  key_lbl->add_css_class("monospace");

  auto* desc_lbl = Gtk::make_managed<Gtk::Label>(desc);
  desc_lbl->set_halign(Gtk::Align::START);
  desc_lbl->set_xalign(0.0f);
  desc_lbl->set_margin_top(2);
  desc_lbl->set_margin_bottom(2);

  grid.attach(*key_lbl, 0, row);
  grid.attach(*desc_lbl, 1, row);
  return row + 1;
}

int ShortcutsDialog::add_spacer(Gtk::Grid& grid, int row) {
  auto* lbl = Gtk::make_managed<Gtk::Label>("");
  lbl->set_margin_top(4);
  grid.attach(*lbl, 0, row, 2, 1);
  return row + 1;
}


// ─────────────────────────────────────────────────────────────────────────────
// Tab builder — walk the registry for this tab, heading per section change
// ─────────────────────────────────────────────────────────────────────────────
Gtk::Widget& ShortcutsDialog::build_tab(Gtk::Grid& grid,
                                        Gtk::ScrolledWindow& scroll,
                                        ShortcutTab tab) {
  grid.set_margin(12);
  grid.set_column_spacing(8);
  grid.set_row_spacing(0);
  grid.set_column_homogeneous(false);

  int r = 0;
  std::string cur_section;
  bool first = true;
  for (const auto& s : shortcut_registry()) {
    if (s.tab != tab) continue;
    if (first || s.section != cur_section) {
      if (!first) r = add_spacer(grid, r);
      r = add_heading(grid, s.section, r);
      cur_section = s.section;
      first = false;
    }
    r = add_row(grid, s.display_keys(), s.description, r);
  }

  scroll.set_child(grid);
  scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  scroll.set_expand(true);
  return scroll;
}

}  // namespace Folio
