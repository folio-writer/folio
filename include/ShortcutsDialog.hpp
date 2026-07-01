#pragma once
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/grid.h>
#include <gtkmm/label.h>
#include <gtkmm/notebook.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/separator.h>
#include <gtkmm/window.h>

#include <string>

#include "Shortcuts.hpp"

namespace Folio {

// s98 — Keyboard & mouse reference window.
//
// Replaces the deprecated GtkShortcutsWindow (deprecated GTK 4.18; Gtk::Dialog is
// deprecated too, 4.10) with a hand-rolled Gtk::Window — the same non-deprecated
// pattern Curvz settled on. A Notebook of two tabs (Keyboard, Mouse & Modifiers),
// each a ScrolledWindow over a Grid.
//
// The content is NOT hand-listed here: both tabs are rendered by walking the pure
// shortcut_registry() (Shortcuts.hpp), which is also what MainWindow wires the
// accelerators from — one source of truth, so the dialog can never drift from the
// live accel table. Non-modal + hide_on_close, so MainWindow builds it once.
class ShortcutsDialog : public Gtk::Window {
public:
  ShortcutsDialog();
  void show(Gtk::Window& parent);

private:
  // Build one tab by walking the registry for `tab`, emitting a heading whenever
  // the section changes. Returns the scroll widget for append_page.
  Gtk::Widget& build_tab(Gtk::Grid& grid, Gtk::ScrolledWindow& scroll,
                         ShortcutTab tab);

  // ── Grid helpers (return the next free row) ───────────────────────────
  int add_heading(Gtk::Grid& grid, const std::string& title, int row);
  int add_row(Gtk::Grid& grid, const std::string& keys, const std::string& desc,
              int row);
  int add_spacer(Gtk::Grid& grid, int row);

  // ── Widgets ───────────────────────────────────────────────────────────
  Gtk::Box      m_root{Gtk::Orientation::VERTICAL};
  Gtk::Notebook m_notebook;
  Gtk::Button   m_btn_close{"Close"};

  Gtk::ScrolledWindow m_keyboard_scroll;
  Gtk::ScrolledWindow m_mouse_scroll;
  Gtk::Grid           m_keyboard_grid;
  Gtk::Grid           m_mouse_grid;
};

}  // namespace Folio
