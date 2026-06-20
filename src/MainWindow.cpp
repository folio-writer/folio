// ─────────────────────────────────────────────────────────────────────────────
// Folio — MainWindow.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "MainWindow.hpp"
#include "color_utils.hpp"
#include "Editor.hpp"
#include "EditorTabBar.hpp"
#include "ExportDialog.hpp"
#include "ImportDialog.hpp"
#include "Inspector.hpp"
#include "Module.hpp"            // s23 — built-in modules
#include "ModuleIO.hpp"          // s23 — keypoint spectrum palette
#include "ModulePlanner.hpp"    // s23 — scaffold planner
#include "ModuleMaterializer.hpp" // s23 — plan -> BinderNodes
#include "PomodoroDialog.hpp"
#include "PrintDialog.hpp"
#include "ReportEngine.hpp"
#include "SearchDialog.hpp"
#include "Sidebar.hpp"
#include "SnapshotDialog.hpp"
#include <FolioLog.hpp>
#include <css.hpp>
#include <ctime>
#include <giomm/simpleaction.h>
#include <gtk/gtk.h>
#include <gtkmm/aboutdialog.h>
#include <gtkmm/filechoosernative.h>
#include <gtkmm/gestureclick.h>
#include <iomanip>
#include <set>
#include <sstream>
#include <unistd.h>

namespace Folio {

// ─────────────────────────────────────────────────────────────────────────────
// Dark-mode detection helper
// ─────────────────────────────────────────────────────────────────────────────
static bool detect_dark_mode() {
  try {
    auto gs = Gio::Settings::create("org.gnome.desktop.interface");
    if (gs) {
      Glib::ustring scheme = gs->get_string("color-scheme");
      if (scheme == "prefer-dark")
        return true;
      if (scheme == "prefer-light")
        return false;
    }
  } catch (...) {
  }
  auto s = Gtk::Settings::get_default();
  if (s) {
    bool dark = false;
    s->get_property("gtk-application-prefer-dark-theme", dark);
    if (dark)
      return true;
    Glib::ustring theme;
    s->get_property("gtk-theme-name", theme);
    if (theme.lowercase().find("dark") != Glib::ustring::npos)
      return true;
  }
  return false;
}

// Collect every node in a tree recursively — used to give the Inspector a
// flat list of all nodes in a JV selection regardless of expand state.
static void flat_all_descendants(BinderNode *node,
                                 std::vector<BinderNode *> &out) {
  if (!node)
    return;
  out.push_back(node);
  for (auto &child : node->children)
    flat_all_descendants(&child, out);
}

// Build a flat deduplicated node list for the Inspector from a sidebar item
// list that may contain both a group and its descendants.
// Only processes root-level items (skips items whose ancestor is also present).
// s20: items are iid-keyed; resolve to node + current path up front (the
// ancestor-dedup still needs positional context, derived at this edge).
static std::vector<BinderNode *>
inspector_nodes_from_items(DocumentModel &model,
                           const std::vector<BoardItem> &items) {
  std::set<BinderNode *> item_nodes;
  for (const auto &item : items) {
    BinderNode *n = model.find_node_by_iid(item.iid);
    if (n) item_nodes.insert(n);
  }
  std::vector<BinderNode *> out;
  for (const auto &item : items) {
    BinderNode *n = model.find_node_by_iid(item.iid);
    if (!n) continue;
    bool has_ancestor = false;
    auto path = model.path_for_iid(item.section, item.iid);
    while (path.size() > 1) {
      path.pop_back();
      BinderNode *anc = model.node_at(item.section, path);
      if (anc && item_nodes.count(anc)) { has_ancestor = true; break; }
    }
    if (!has_ancestor)
      flat_all_descendants(n, out);
  }
  return out;
}
// ─────────────────────────────────────────────────────────────────────────────
MainWindow::MainWindow(DocumentModel &model, FolioPrefs &prefs)
    : Gtk::ApplicationWindow(), m_model(model), m_prefs(prefs),
      m_paned_left(Gtk::Orientation::HORIZONTAL),
      m_paned_right(Gtk::Orientation::HORIZONTAL),
      m_center_box(Gtk::Orientation::VERTICAL),
      m_title_box(Gtk::Orientation::VERTICAL) {
  // Restore saved window dimensions
  set_default_size(m_prefs.window_width, m_prefs.window_height);
  if (m_prefs.window_maximized)
    maximize();
  set_title("Folio");

  // Restore panel visibility state from prefs
  m_show_binder = m_prefs.binder_visible;
  m_show_inspector = m_prefs.inspector_visible;

  setup_css();
  setup_headerbar();
  setup_layout();
  setup_actions();
  wire_callbacks();
  apply_layout_state(); // apply restored binder/inspector visibility
}

bool MainWindow::on_close_request() {
  // Save window geometry
  m_prefs.window_maximized = is_maximized();
  if (!m_prefs.window_maximized) {
    m_prefs.window_width = get_width();
    m_prefs.window_height = get_height();
  }

  // Save panel visibility and paned split positions.
  m_prefs.binder_visible = m_show_binder;
  m_prefs.inspector_visible = m_show_inspector;
  m_prefs.binder_width = m_paned_left.get_position();
  m_prefs.paned_right_pos = m_paned_right.get_position();

  // Save sidebar disclosure state
  if (m_sidebar) {
    m_prefs.sidebar_sec_manuscript_expanded =
        m_sidebar->sec_manuscript_expanded();
    m_prefs.sidebar_sec_characters_expanded =
        m_sidebar->sec_characters_expanded();
    m_prefs.sidebar_sec_places_expanded = m_sidebar->sec_places_expanded();
    m_prefs.sidebar_sec_references_expanded =
        m_sidebar->sec_references_expanded();
    m_prefs.sidebar_sec_templates_expanded =
        m_sidebar->sec_templates_expanded();
    m_prefs.sidebar_sec_trash_expanded = m_sidebar->sec_trash_expanded();
    m_prefs.sidebar_pomo_tile_expanded = m_sidebar->pomo_tile_expanded();
    m_prefs.sidebar_session_tile_expanded = m_sidebar->session_tile_expanded();
  }
  if (m_inspector)
    m_prefs.inspector_progress_expanded = m_inspector->progress_expanded();

  try {
    m_prefs.save();
  } catch (...) {
  }

  // Stop autosave timer — we're closing
  stop_autosave_timer();

  // Always attempt a backup on close if we have a saved project,
  // regardless of whether there are unsaved changes — the project file
  // on disk is current (either just saved, or unmodified since last save).
  if (!m_model.current_path.empty())
    do_backup_on_close();

  // Not modified — just close
  if (!m_model.is_modified || m_closing)
    return false;

  // save_on_close + has a path → silent save, then close
  if (m_prefs.save_on_close && !m_model.current_path.empty()) {
    try {
      snapshot_open_tabs();
      m_model.save();
      do_backup_on_close();
    } catch (...) {
    }
    return false; // saved — allow close
  }

  // save_on_close + NO path → prompt Save As, then close; or Discard, or Cancel
  if (m_prefs.save_on_close && m_model.current_path.empty()) {
    auto dlg = Gtk::AlertDialog::create("Save before closing?");
    dlg->set_detail(
        "This project has never been saved. Save it now or discard changes?");
    dlg->set_modal(true);
    dlg->set_buttons({"Cancel", "Discard", "Save As…"});
    dlg->set_cancel_button(0);
    dlg->set_default_button(2);
    dlg->choose(*this, [this, dlg](Glib::RefPtr<Gio::AsyncResult> &r) mutable {
      int resp = 0;
      try {
        resp = dlg->choose_finish(r);
      } catch (...) {
      }
      if (resp == 1) { // Discard
        m_closing = true;
        close();
      } else if (resp == 2) { // Save As
        action_save_as();
        // After save_as, if current_path is set the file was saved — close
        Glib::signal_idle().connect_once([this]() {
          if (!m_model.current_path.empty()) {
            do_backup_on_close();
            m_closing = true;
            close();
          }
        });
      }
      // resp == 0 → Cancel: do nothing, stay open
    });
    return true; // inhibit until dialog resolves
  }

  // save_on_close is OFF → show standard "Cancel / Save / Discard" dialog
  {
    auto dlg = Gtk::AlertDialog::create("Unsaved changes");
    dlg->set_detail("You have unsaved changes. What would you like to do?");
    dlg->set_modal(true);
    dlg->set_buttons({"Cancel", "Discard", "Save"});
    dlg->set_cancel_button(0);
    dlg->set_default_button(2);
    dlg->choose(*this, [this, dlg](Glib::RefPtr<Gio::AsyncResult> &r) mutable {
      int resp = 0;
      try {
        resp = dlg->choose_finish(r);
      } catch (...) {
      }
      if (resp == 1) { // Discard
        m_closing = true;
        close();
      } else if (resp == 2) { // Save
        action_save();        // handles path-empty → Save As internally
        Glib::signal_idle().connect_once([this]() {
          if (!m_model.is_modified) {
            do_backup_on_close();
            m_closing = true;
            close();
          }
        });
      }
      // resp == 0 → Cancel
    });
    return true;
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// CSS
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::setup_css() {
  m_css_provider = Gtk::CssProvider::create();
  Gtk::StyleContext::add_provider_for_display(
      Gdk::Display::get_default(), m_css_provider,
      GTK_STYLE_PROVIDER_PRIORITY_USER + 1);

  bool dark = (m_prefs.theme == "dark")    ? true
              : (m_prefs.theme == "light") ? false
                                           : detect_dark_mode();
  apply_theme(dark);

  try {
    auto gs = Gio::Settings::create("org.gnome.desktop.interface");
    if (gs) {
      gs->signal_changed().connect([this, gs](const Glib::ustring &key) {
        if (key == "color-scheme") {
          bool dark = gs->get_string("color-scheme") == "prefer-dark";
          if (dark != m_dark_mode)
            apply_theme(dark);
        }
      });
      m_gsettings = gs;
    }
  } catch (...) {
  }

  auto gtkset = Gtk::Settings::get_default();
  if (gtkset) {
    gtkset->property_gtk_application_prefer_dark_theme()
        .signal_changed()
        .connect([this]() { apply_theme(detect_dark_mode()); });
  }
}

void MainWindow::apply_theme(bool dark) {
  if (m_applying_theme)
    return;
  m_applying_theme = true;
  m_dark_mode = dark;
  auto gs = Gtk::Settings::get_default();
  if (gs)
    gs->set_property("gtk-application-prefer-dark-theme", dark);
  std::string css = (dark ? FOLIO_CSS_DARK : FOLIO_CSS_LIGHT);
  css += FOLIO_CSS_SHARED;
  if (!m_prefs.ui_font.empty()) {
    css += "\n* { font-family: \"" + m_prefs.ui_font +
           "\", \"Cantarell\", sans-serif; }\n";
    css += "* { font-size: " + std::to_string(m_prefs.ui_font_size) + "px; }\n";
  }
  // Inject pip color override so pomo-dot-done always uses the user's chosen
  // color
  if (!m_prefs.pomodoro.pip_color.empty()) {
    const std::string &pc = m_prefs.pomodoro.pip_color;
    // Build a simple rgba() string from the hex for the alpha variant
    // (shared helper; fallback teal 91,200,175).
    css += "\n.pomo-dot-done   { background-color: " + pc + "; }\n";
    css += ".pomo-dot-active { background-color: " +
           Folio::color::to_css_rgba(pc, 0.45,
                                     Folio::color::rgba(91 / 255.0, 200 / 255.0,
                                                        175 / 255.0)) +
           "; border: 1px solid " + pc + "; }\n";
  }
  m_css_provider->load_from_data(css);
  m_applying_theme = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Header bar
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::setup_headerbar() {
  // ── Layout panel toggles ──────────────────────────────────────────────────
  m_layout_toggle_box.add_css_class("view-toggle-group");
  m_layout_toggle_box.set_orientation(Gtk::Orientation::HORIZONTAL);
  m_layout_toggle_box.set_valign(Gtk::Align::CENTER);

  // s6: system-icon placeholders for the panel toggles. Curvz will supply
  // folio-binder-symbolic / folio-inspector-symbolic later.
  m_btn_binder.set_icon_name("folio-binder-symbolic");
  m_btn_binder.set_tooltip_text("Show/hide Sidebar");
  m_btn_binder.set_active(m_prefs.binder_visible);
  m_btn_inspector_toggle.set_icon_name("folio-inspector-symbolic");
  m_btn_inspector_toggle.set_tooltip_text("Show/hide Inspector");
  m_btn_inspector_toggle.set_active(m_prefs.inspector_visible);

  m_btn_binder.signal_toggled().connect([this]() {
    m_show_binder = m_btn_binder.get_active();
    m_prefs.binder_visible = m_show_binder;
    apply_layout_state();
  });
  m_btn_inspector_toggle.signal_toggled().connect([this]() {
    m_show_inspector = m_btn_inspector_toggle.get_active();
    m_prefs.inspector_visible = m_show_inspector;
    apply_layout_state();
  });
  m_layout_toggle_box.append(m_btn_binder);
  m_layout_toggle_box.append(m_btn_inspector_toggle);

  // ── View mode dropdown ────────────────────────────────────────────────────
  m_view_toggle_box.set_orientation(Gtk::Orientation::HORIZONTAL);
  m_view_toggle_box.set_valign(Gtk::Align::CENTER);

  // List indices: 0=Write 1=Board 2=Grid
  // Logical mode indices passed to switch_view_mode: 0 1 2
  const std::array<guint, 3> LOGICAL_TO_LIST = {0, 1, 2};
  const std::array<int, 3> LIST_TO_LOGICAL = {0, 1, 2};

  auto view_items = Gtk::StringList::create({
      "✎  Write        Ctrl+Alt+W",  // 0
      "⊞  Board        Ctrl+Alt+B",  // 1
      "≡  Grid          Ctrl+Alt+G", // 2
  });
  m_view_mode_dd.set_model(view_items);
  m_view_mode_dd.set_selected(0);
  m_view_mode_dd.set_tooltip_text("View mode");
  m_view_mode_dd.add_css_class("view-mode-dropdown");

  // ── Custom list factory ───────────────────────────────────────────────────
  auto factory = Gtk::SignalListItemFactory::create();

  factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem> &li) {
    auto *box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    box->set_name("view-dd-row");
    li->set_child(*box);
  });

  factory->signal_bind().connect([](const Glib::RefPtr<Gtk::ListItem> &li) {
    auto *box = dynamic_cast<Gtk::Box *>(li->get_child());
    if (!box)
      return;
    while (auto *c = box->get_first_child())
      box->remove(*c);
    auto *item = dynamic_cast<Gtk::StringObject *>(li->get_item().get());
    if (!item)
      return;
    auto *lbl = Gtk::make_managed<Gtk::Label>(item->get_string());
    lbl->set_halign(Gtk::Align::START);
    lbl->set_use_markup(false);
    box->append(*lbl);
    li->set_selectable(true);
    li->set_activatable(true);
  });

  // Button face factory — shows icon + name only (no hotkey)
  static const char *const FACE_LABELS[] = {
      "✎  Write", "⊞  Board", "≡  Grid",
  };
  auto btn_factory = Gtk::SignalListItemFactory::create();
  btn_factory->signal_setup().connect(
      [](const Glib::RefPtr<Gtk::ListItem> &li) {
        auto *lbl = Gtk::make_managed<Gtk::Label>();
        lbl->set_halign(Gtk::Align::START);
        li->set_child(*lbl);
      });
  btn_factory->signal_bind().connect([](const Glib::RefPtr<Gtk::ListItem> &li) {
    auto *lbl = dynamic_cast<Gtk::Label *>(li->get_child());
    if (!lbl)
      return;
    guint pos = li->get_position();
    if (pos < 3)
      lbl->set_text(FACE_LABELS[pos]);
  });

  m_view_mode_dd.set_factory(btn_factory);
  m_view_mode_dd.set_list_factory(factory);

  // Helper to switch view mode (logical index 0-2)
  auto switch_view_mode = [this, LOGICAL_TO_LIST](guint logical) {
    if (m_inhibit_view_dd)
      return;
    m_inhibit_view_dd = true;
    m_view_mode_dd.set_selected(LOGICAL_TO_LIST[logical]);
    m_inhibit_view_dd = false;

    if (!m_editor)
      return;
    switch (logical) {
    case 0: // Write (handles both single and multi via apply_selection)
      m_editor->set_view_mode(Editor::ViewMode::Write);
      if (m_sidebar)
        m_sidebar->set_allow_cross_category(true);
      // Re-apply current selection so Write mode reflects it immediately.
      // sync_sidebar=false — sidebar already has correct state, don't clobber it.
      if (m_sidebar) {
        auto items = m_sidebar->get_board_selection();
        apply_selection(std::vector<BoardItem>(items.begin(), items.end()), false);
      }
      break;
    case 1: // Board
      m_editor->set_view_mode(Editor::ViewMode::Board);
      if (m_sidebar)
        m_sidebar->set_allow_cross_category(false);
      break;
    case 2: // Grid
      m_editor->set_view_mode(Editor::ViewMode::Outline);
      if (m_sidebar)
        m_sidebar->set_allow_cross_category(true);
      break;
    }
  };

  m_view_mode_dd.property_selected().signal_changed().connect(
      [this, switch_view_mode, LIST_TO_LOGICAL]() {
        if (m_inhibit_view_dd)
          return;
        guint list_idx = m_view_mode_dd.get_selected();
        if (list_idx >= LIST_TO_LOGICAL.size())
          return;
        int logical = LIST_TO_LOGICAL[list_idx];
        if (logical >= 0)
          switch_view_mode((guint)logical);
      });

  m_view_toggle_box.append(m_view_mode_dd);

  // ── View mode accelerators ────────────────────────────────────────────────
  auto add_view_accel = [this, switch_view_mode](guint keyval, guint idx) {
    auto ctrl = Gtk::ShortcutController::create();
    ctrl->set_scope(Gtk::ShortcutScope::GLOBAL);
    auto trigger = Gtk::KeyvalTrigger::create(
        keyval, Gdk::ModifierType::CONTROL_MASK | Gdk::ModifierType::ALT_MASK);
    auto action = Gtk::CallbackAction::create(
        [switch_view_mode, idx](Gtk::Widget &,
                                      const Glib::VariantBase &) {
          switch_view_mode(idx);
          return true;
        });
    ctrl->add_shortcut(Gtk::Shortcut::create(trigger, action));
    add_controller(ctrl);
  };

  add_view_accel(GDK_KEY_w, 0);
  add_view_accel(GDK_KEY_b, 1);
  add_view_accel(GDK_KEY_g, 2);

  // ── Expand/collapse all groups hotkeys ────────────────────────────────────
  auto add_expand_accel = [this](guint keyval, bool expand) {
    auto ctrl = Gtk::ShortcutController::create();
    ctrl->set_scope(Gtk::ShortcutScope::GLOBAL);
    auto trigger = Gtk::KeyvalTrigger::create(
        keyval, Gdk::ModifierType::CONTROL_MASK | Gdk::ModifierType::ALT_MASK);
    auto action = Gtk::CallbackAction::create(
        [this, expand](Gtk::Widget &, const Glib::VariantBase &) {
          if (!m_sidebar) return true;
          for (auto sec : {Section::Manuscript, Section::Characters,
                           Section::Places, Section::References,
                           Section::Templates}) {
            if (expand)
              m_sidebar->expand_all_in_section(sec);
            else
              m_sidebar->collapse_all_in_section(sec);
          }
          return true;
        });
    ctrl->add_shortcut(Gtk::Shortcut::create(trigger, action));
    add_controller(ctrl);
  };
  add_expand_accel(GDK_KEY_e, true);   // Ctrl+Alt+E — expand all groups
  add_expand_accel(GDK_KEY_k, false);  // Ctrl+Alt+K — collapse all groups

  // ── Title / subtitle ──────────────────────────────────────────────────────
  m_title_label.set_text(m_model.project_title);
  m_title_label.add_css_class("folio-title");
  m_title_label.set_halign(Gtk::Align::CENTER);
  m_subtitle_label.set_text("Novel · " + std::to_string(m_model.total_words()) +
                            " words");
  m_subtitle_label.add_css_class("folio-subtitle");
  m_subtitle_label.set_halign(Gtk::Align::CENTER);
  m_title_box.set_spacing(0);
  m_title_box.set_halign(Gtk::Align::CENTER);
  m_title_box.append(m_title_label);
  m_title_box.append(m_subtitle_label);
  m_title_box.add_css_class("title-box");

  // ── Hamburger menu ────────────────────────────────────────────────────────
  auto make_item = [](const Glib::ustring &label, const Glib::ustring &action,
                      const Glib::ustring &accel = "") {
    auto item = Gio::MenuItem::create(label, action);
    if (!accel.empty())
      item->set_attribute_value("accel",
                                Glib::Variant<Glib::ustring>::create(accel));
    return item;
  };

  auto file_menu = Gio::Menu::create();

  // ── Section 1: Project file operations ───────────────────────────────────
  auto project_sec = Gio::Menu::create();
  project_sec->append_item(make_item("New Project", "win.new", "<Ctrl>n"));
  project_sec->append_item(make_item("New from Pattern…", "win.new-from-pattern", ""));
  project_sec->append_item(make_item("Open…", "win.open", "<Ctrl>o"));

  m_recent_menu = Gio::Menu::create();
  project_sec->append_submenu("Open Recent", m_recent_menu);

  project_sec->append_item(make_item("Save", "win.save", "<Ctrl>s"));
  project_sec->append_item(
      make_item("Save As…", "win.save-as", "<Ctrl><Shift>s"));
  project_sec->append_item(make_item("Close Window", "win.quit", "<Ctrl>w"));
  file_menu->append_section(project_sec);

  // ── Share submenu: Import / Export / Print / Report ───────────────────────
  auto share_sub = Gio::Menu::create();
  share_sub->append_item(make_item("Import…", "win.import", "<Ctrl><Shift>i"));
  share_sub->append_item(make_item("Export…", "win.export", "<Ctrl>e"));
  share_sub->append_item(make_item("Print…", "win.print", "<Ctrl>p"));
  share_sub->append_item(make_item("Save Report…", "win.save-report", ""));

  auto share_wrapper = Gio::Menu::create();
  share_wrapper->append_submenu("Share", share_sub);
  file_menu->append_section(share_wrapper);

  // ── Section 1b: Search ────────────────────────────────────────────────────
  auto edit_sec = Gio::Menu::create();
  edit_sec->append_item(
      make_item("Search & Replace…", "win.search", "<Ctrl><Shift>g"));
  file_menu->append_section(edit_sec);

  // ── Section 2: View submenu ───────────────────────────────────────────────
  auto view_sub = Gio::Menu::create();
  view_sub->append_item(
      make_item("Toggle Sidebar", "win.toggle-binder", "<Ctrl><Shift>b"));
  view_sub->append_item(
      make_item("Toggle Inspector", "win.toggle-inspector", "<Ctrl><Shift>i"));
  view_sub->append_item(
      make_item("Focus Mode", "win.focus-mode", "<Ctrl><Shift>f"));
  view_sub->append_item(
      make_item("Close All Tabs", "win.close-all-tabs", "<Ctrl><Shift>w"));

  auto view_wrapper = Gio::Menu::create();
  view_wrapper->append_submenu("View", view_sub);
  file_menu->append_section(view_wrapper);

  // ── Section 3: Navigate submenu ───────────────────────────────────────────
  auto nav_sub = Gio::Menu::create();

  // Timeline tab navigation
  auto nav_tabs = Gio::Menu::create();
  nav_tabs->append_item(
      make_item("Previous Tab", "win.timeline-prev", "<Alt>Left"));
  nav_tabs->append_item(
      make_item("Next Tab", "win.timeline-next", "<Alt>Right"));
  nav_sub->append_section(nav_tabs);

  // Inspector tab navigation
  auto nav_inspector = Gio::Menu::create();
  nav_inspector->append_item(
      make_item("Project tab", "win.inspector-project", "<Alt>p"));
  nav_inspector->append_item(
      make_item("Metadata tab", "win.inspector-metadata", "<Alt>m"));
  nav_inspector->append_item(
      make_item("Notes tab", "win.inspector-notes", "<Alt>n"));
  nav_inspector->append_item(
      make_item("Snapshots tab", "win.inspector-snapshots", "<Alt>s"));
  nav_sub->append_section(nav_inspector);

  auto nav_wrapper = Gio::Menu::create();
  nav_wrapper->append_submenu("Navigate", nav_sub);
  file_menu->append_section(nav_wrapper);

  // ── Section 4: Binder submenu ─────────────────────────────────────────────
  auto binder_sub = Gio::Menu::create();

  auto binder_add = Gio::Menu::create();
  binder_add->append_item(
      make_item("Add Scene", "win.add-scene", "<Ctrl><Alt>s"));
  binder_add->append_item(
      make_item("Add Character", "win.add-character", "<Ctrl><Alt>c"));
  binder_add->append_item(
      make_item("Add Place", "win.add-place", "<Ctrl><Alt>p"));
  binder_add->append_item(
      make_item("Add Reference", "win.add-reference", "<Ctrl><Alt>r"));
  binder_add->append_item(
      make_item("Add Template", "win.add-template", "<Ctrl><Alt>t"));
  binder_sub->append_section(binder_add);

  auto binder_groups = Gio::Menu::create();
  binder_groups->append_item(make_item("Add Group (Manuscript)",
                                       "win.add-group-manuscript",
                                       "<Ctrl><Alt><Shift>s"));
  binder_groups->append_item(make_item("Add Group (Characters)",
                                       "win.add-group-characters",
                                       "<Ctrl><Alt><Shift>c"));
  binder_groups->append_item(make_item(
      "Add Group (Places)", "win.add-group-places", "<Ctrl><Alt><Shift>p"));
  binder_sub->append_section(binder_groups);

  auto binder_wrapper = Gio::Menu::create();
  binder_wrapper->append_submenu("Sidebar", binder_sub);
  file_menu->append_section(binder_wrapper);

  // ── Section 5: Tools submenu ─────────────────────────────────────────────
  auto tools_sub = Gio::Menu::create();

  auto tools_writing = Gio::Menu::create();
  tools_writing->append_item(
      make_item("Pomodoro Timer", "win.pomodoro", "<Ctrl><Shift>p"));
  tools_writing->append_item(
      make_item("Batch Snapshot…", "win.batch-snapshot", "<Ctrl><Shift>t"));
  tools_sub->append_section(tools_writing);

  auto tools_config = Gio::Menu::create();
  tools_config->append_item(
      make_item("Keyboard Shortcuts", "win.shortcuts", "<Ctrl>question"));
  tools_config->append_item(
      make_item("Preferences…", "win.preferences", "<Ctrl>comma"));
  tools_sub->append_section(tools_config);

  auto tools_wrapper = Gio::Menu::create();
  tools_wrapper->append_submenu("Tools", tools_sub);
  file_menu->append_section(tools_wrapper);

  // ── Section 6: Help ──────────────────────────────────────────────────────
  auto help_sec = Gio::Menu::create();
  help_sec->append_item(make_item("About Folio", "win.about", ""));
  file_menu->append_section(help_sec);

  // ── Section 7: Quit ──────────────────────────────────────────────────────
  auto quit_sec = Gio::Menu::create();
  quit_sec->append_item(make_item("Quit", "win.quit", "<Ctrl>q"));
  file_menu->append_section(quit_sec);

  m_menu_btn.set_icon_name("folio-hamburger-symbolic");
  m_menu_btn.add_css_class("icon-btn");
  m_menu_btn.set_tooltip_text("Menu");
  m_menu_btn.set_always_show_arrow(false);

  // Use GTK_POPOVER_MENU_NESTED (GTK 4.14+) so submenus fly out beside their
  // parent item instead of replacing the popover content as nested pages.
#if GTK_CHECK_VERSION(4, 14, 0)
  {
    auto *popover = gtk_popover_menu_new_from_model_full(
        G_MENU_MODEL(file_menu->gobj()), GTK_POPOVER_MENU_NESTED);
    m_menu_btn.set_popover(*Glib::wrap(GTK_POPOVER(popover)));
  }
#else
  m_menu_btn.set_menu_model(file_menu);
#endif

  // ── Pomodoro headerbar pill ───────────────────────────────────────────────
  // Shown on the right when the timer is running (if pref enabled).
  m_pomo_hdr_box.set_orientation(Gtk::Orientation::HORIZONTAL);
  m_pomo_hdr_box.set_spacing(6);
  m_pomo_hdr_box.set_valign(Gtk::Align::CENTER);
  m_pomo_hdr_box.add_css_class("pomo-hdr-pill");
  m_pomo_hdr_box.set_visible(false);
  m_pomo_hdr_box.set_tooltip_text(
      "Pomodoro timer — click to open the timer window");

  // Clicking the pill opens the dialog
  auto gc_pill = Gtk::GestureClick::create();
  gc_pill->set_button(1);
  gc_pill->signal_pressed().connect(
      [this](int, double, double) { action_pomodoro(); });
  m_pomo_hdr_box.add_controller(gc_pill);
  m_pomo_hdr_box.set_cursor(Gdk::Cursor::create("pointer"));

  m_pomo_hdr_phase.set_text("Focus");
  m_pomo_hdr_phase.add_css_class("pomo-hdr-phase");
  m_pomo_hdr_time.set_text("25:00");
  m_pomo_hdr_time.add_css_class("pomo-hdr-time");

  // Small dot separator
  auto *dot_sep = Gtk::make_managed<Gtk::Label>("·");
  dot_sep->add_css_class("pomo-hdr-sep");

  m_pomo_hdr_box.append(m_pomo_hdr_phase);
  m_pomo_hdr_box.append(*dot_sep);
  m_pomo_hdr_box.append(m_pomo_hdr_time);

  // ── Tools menu ────────────────────────────────────────────────────────────
  // Alphabetical list of manager dialogs with system icons.
  // Icon names use the "verb-icon" attribute so they render in PopoverMenu.
  {
    auto make_tool_item = [](const Glib::ustring& label,
                             const Glib::ustring& action,
                             const Glib::ustring& icon) {
      auto item = Gio::MenuItem::create(label, action);
      item->set_attribute_value(
          "verb-icon", Glib::Variant<Glib::ustring>::create(icon));
      return item;
    };

    auto tools_menu = Gio::Menu::create();
    tools_menu->append_item(make_tool_item(
        "Annotation Report…",  "win.tool-annotation-report",
        "document-properties-symbolic"));
    tools_menu->append_item(make_tool_item(
        "Barcode…",            "win.tool-barcode",
        "view-barcode-symbolic"));
    tools_menu->append_item(make_tool_item(
        "Pomodoro Timer…",     "win.pomodoro",
        "alarm-symbolic"));
    tools_menu->append_item(make_tool_item(
        "Project Goals…",      "win.tool-project-goals",
        "trophy-symbolic"));
    tools_menu->append_item(make_tool_item(
        "Rulers…",             "win.tool-rulers",
        "ruler-symbolic"));
    tools_menu->append_item(make_tool_item(
        "Screenplay Help…",    "win.tool-screenplay-help",
        "help-contents-symbolic"));
    tools_menu->append_item(make_tool_item(
        "Snapshots…",          "win.batch-snapshot",
        "camera-symbolic"));
    tools_menu->append_item(make_tool_item(
        "Style Manager…",      "win.tool-style-manager",
        "applications-graphics-symbolic"));

    m_tools_btn.set_icon_name("folio-tools-symbolic");
    m_tools_btn.add_css_class("icon-btn");
    m_tools_btn.set_tooltip_text("Tools");
    m_tools_btn.set_always_show_arrow(false);
    m_tools_btn.set_menu_model(tools_menu);
  }

  // ── Assemble header bar ───────────────────────────────────────────────────
  m_headerbar.pack_start(m_menu_btn);
  m_headerbar.pack_start(m_tools_btn);
  m_headerbar.pack_start(m_layout_toggle_box);
  m_headerbar.pack_start(m_view_toggle_box);
  m_headerbar.pack_end(m_pomo_hdr_box);
  m_headerbar.set_title_widget(m_title_box);

  set_titlebar(m_headerbar);

  rebuild_recent_menu();
}

void MainWindow::rebuild_recent_menu() {
  while (m_recent_menu->get_n_items() > 0)
    m_recent_menu->remove(0);
  if (m_prefs.recent_files.empty()) {
    m_recent_menu->append("(none)", "");
  } else {
    for (int i = 0; i < (int)m_prefs.recent_files.size(); ++i) {
      std::string label = Glib::path_get_basename(m_prefs.recent_files[i]);
      m_recent_menu->append(label, "win.open-recent-" + std::to_string(i));
    }
    // Separator + clear action
    auto clear_sec = Gio::Menu::create();
    clear_sec->append("Clear Recent Files", "win.clear-recent");
    m_recent_menu->append_section(clear_sec);
  }
}

// ─────────────────────────────────────────────────────────────────────────────
// Layout
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::setup_layout() {
  m_sidebar = std::make_unique<Sidebar>(m_model, m_prefs);
  m_timeline = std::make_unique<EditorTabBar>(m_model, m_prefs);
  m_editor = std::make_unique<Editor>(m_model, m_prefs);
  m_editor->apply_font_prefs(m_prefs);
  m_inspector = std::make_unique<Inspector>(m_model, m_prefs);

  m_center_box.append(*m_timeline);
  m_center_box.append(*m_editor);
  m_editor->set_vexpand(true);

  m_paned_right.set_start_child(m_center_box);
  m_paned_right.set_end_child(*m_inspector);
  m_paned_right.set_position(m_prefs.paned_right_pos);
  m_paned_right.set_resize_start_child(true);
  m_paned_right.set_resize_end_child(false);
  m_paned_right.set_shrink_start_child(false);
  m_paned_right.set_shrink_end_child(false);
  // Track position live so hide/reshow restores the correct split
  m_paned_right.property_position().signal_changed().connect([this]() {
    if (m_show_inspector)
      m_prefs.paned_right_pos = m_paned_right.get_position();
  });

  m_paned_left.set_start_child(*m_sidebar);
  m_paned_left.set_end_child(m_paned_right);
  m_paned_left.set_position(m_prefs.binder_width);
  m_paned_left.set_resize_start_child(false);
  m_paned_left.set_resize_end_child(true);
  m_paned_left.set_shrink_start_child(false);
  m_paned_left.set_shrink_end_child(false);
  m_paned_left.property_position().signal_changed().connect([this]() {
    if (m_show_binder)
      m_prefs.binder_width = m_paned_left.get_position();
  });

  set_child(m_paned_left);

  if (m_sidebar)
    m_sidebar->rebuild();
}

void MainWindow::apply_layout_state() {
  if (m_sidebar)
    m_sidebar->set_visible(m_show_binder);
  if (m_inspector)
    m_inspector->set_visible(m_show_inspector);

  // When a panel is reshown, explicitly restore the paned divider position.
  // GTK does not automatically reclaim the correct split after a child is
  // hidden and reshown — the editor stays at its expanded width otherwise.
  Glib::signal_idle().connect_once([this]() {
    if (m_show_inspector)
      m_paned_right.set_position(m_prefs.paned_right_pos);
    if (m_show_binder)
      m_paned_left.set_position(m_prefs.binder_width);
    // Second pass after layout settles
    Glib::signal_timeout().connect_once(
        [this]() {
          if (m_show_inspector)
            m_paned_right.set_position(m_prefs.paned_right_pos);
          if (m_show_binder)
            m_paned_left.set_position(m_prefs.binder_width);
          if (m_editor)
            m_editor->sync_ruler();
        },
        50);
  });

  if (m_editor)
    Glib::signal_idle().connect_once([this]() {
      if (m_editor)
        m_editor->sync_ruler();
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// snapshot_open_tabs — capture the live timeline into model.open_tabs, keyed by
// the stable iid (s20). A tab whose node no longer resolves is dropped. Called
// from every save path; previously this loop was copy-pasted at each one.
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::snapshot_open_tabs() {
  if (!m_timeline)
    return;
  m_model.open_tabs.clear();
  for (const auto &t : m_timeline->tabs()) {
    const BinderNode *n = m_model.node_at(t.section, t.path);
    if (!n)
      continue;
    DocumentModel::SavedTab st;
    st.section = t.section;
    st.iid     = n->iid;
    m_model.open_tabs.push_back(std::move(st));
  }
  m_model.timeline_active_idx = m_timeline->active_idx();
}

// ─────────────────────────────────────────────────────────────────────────────
// apply_selection — single broadcast for all selection changes
// Every trigger (sidebar click, tab click, app restore, ESC) routes here.
//
// s20: items are iid-keyed (BoardItem::iid). Nodes resolve via find_node_by_iid;
// a path is derived (path_for_iid) only where a GTK surface needs a row index.
// Re-entrancy uses two distinct guards (s20, was one overloaded flag):
//   • m_restoring_tabs — load/restore is in progress; suppress side-effects
//     entirely (early return here, and in on_node_changed / on_tab_activated).
//   • m_broadcasting    — we are inside our own set_active() call; suppress only
//     the on_node_changed → apply_selection recursion it would otherwise cause.
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::apply_selection(std::vector<BoardItem> items, bool sync_sidebar) {
  if (m_restoring_tabs)
    return;

  m_multi_selected = items.size() > 1;

  // ── Sidebar highlight — only sync when trigger is external ────────────────
  if (sync_sidebar && m_sidebar) {
    if (items.empty())
      m_sidebar->set_active(Section::Manuscript, std::string{});
    else if (items.size() == 1)
      m_sidebar->set_active(items[0].section, items[0].iid);
    else
      m_sidebar->set_selection(items); // restore full multi-selection
  }

  // ── Empty selection: all modes show hint ──────────────────────────────────
  if (items.empty()) {
    if (m_editor) {
      m_editor->exit_joined(); // save + clear JV state if active
      m_editor->load_empty();
    }
    if (m_inspector)
      m_inspector->load_empty();
    return;
  }

  // ── Board + Grid always follow the full selection ─────────────────────────
  if (m_editor) {
    m_editor->show_board(items);
    m_editor->show_grid(items);
  }

  // ── Single item ───────────────────────────────────────────────────────────
  if (items.size() == 1) {
    BinderNode *node = m_model.find_node_by_iid(items[0].iid);
    auto path = m_model.path_for_iid(items[0].section, items[0].iid);
    m_broadcasting = true;
    m_model.set_active(items[0].section, path);
    m_broadcasting = false;

    if (m_editor && m_editor->view_mode() == Editor::ViewMode::Write) {
      // Exit JV mode if currently active (switching from multi to single)
      if (m_editor->joined_segment_count() > 0)
        m_editor->exit_joined();
      m_editor->load_node(node);
    }

    if (m_inspector) {
      m_inspector->load_node(node);
      m_inspector->focus_meta_tab();
    }
    update_title_bar();
    return;
  }

  // ── Multi-item ────────────────────────────────────────────────────────────

  // Write mode: stitch all selected nodes together (JV behaviour)
  // Collect leaf nodes only — groups are containers, not writable documents.
  // Use a set to deduplicate (a leaf selected directly + its parent group
  // both selected would otherwise add it twice). The dedup set is keyed by
  // node pointer for this single synchronous pass (no mutation occurs here);
  // selection identity across mutations is the iid, carried by `items`.
  if (m_editor && m_editor->view_mode() == Editor::ViewMode::Write) {
    std::vector<BinderNode *> jv_nodes;
    std::set<BinderNode *> seen;

    std::set<BinderNode *> selected_set;
    for (const auto &item : items) {
      BinderNode *n = m_model.find_node_by_iid(item.iid);
      if (n) selected_set.insert(n);
    }

    std::function<void(BinderNode *, bool)>
      collect_jv = [&](BinderNode *n, bool force_recurse) {
      if (!n || seen.count(n)) return;
      seen.insert(n);
      if (binder_kind_is_group(n->kind)) {
        jv_nodes.push_back(n);
        for (auto &child : n->children) {
          bool child_selected = selected_set.count(&child) > 0;
          if (force_recurse || child_selected)
            collect_jv(&child, force_recurse);
        }
      } else {
        jv_nodes.push_back(n);
      }
    };

    for (const auto &item : items) {
      BinderNode *n = m_model.find_node_by_iid(item.iid);
      if (!n || seen.count(n)) continue;
      auto path = m_model.path_for_iid(item.section, item.iid);
      bool is_closed = !m_sidebar->is_node_expanded(item.section, path);
      collect_jv(n, is_closed);
    }

    if (!jv_nodes.empty())
      m_editor->reload_joined(jv_nodes);
    else
      m_editor->load_empty();
  }

  // Inspector: aggregate view for multi
  if (m_inspector)
    m_inspector->load_joined_nodes(inspector_nodes_from_items(m_model, items));

  update_title_bar();
}

// ─────────────────────────────────────────────────────────────────────────────
// Wire callbacks
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::wire_callbacks() {
  // Model → editor + inspector when active node changes (DnD moves etc.)
  m_model.on_node_changed = [this](BinderNode *node) { on_node_changed(node); };

  // Sidebar: double-click → open tab + apply selection
  m_sidebar->set_node_opened_callback(
      [this](Section section, std::vector<int> path) {
        on_node_opened(section, path);
      });

  // Sidebar: nodes moved (DnD or keyboard) → update timeline tab paths.
  // s20: no selection re-apply here. Selection identity is the iid, which the
  // move does not change, and the sidebar's own drop tail broadcasts the final
  // selection state after the move — so a re-apply from here was redundant
  // (it was always overridden by the sidebar's trailing fire_board_selection).
  // The timeline keeps positional tabs internally, so it still needs the
  // old→new path remap.
  m_sidebar->set_nodes_moved_callback(
      [this](Section section,
             std::vector<std::pair<std::vector<int>, std::vector<int>>> moves) {
        if (!m_timeline)
          return;
        for (const auto &mv : moves)
          m_timeline->notify_node_moved(section, mv.first, mv.second);
      });

  // Sidebar: any selection change → apply_selection broadcasts to all modes
  m_sidebar->set_board_selection_callback([this](std::vector<BoardItem> items) {
    apply_selection(std::move(items), false); // sidebar already has correct state
  });

  // Sidebar: single-click fires node_selected → already handled by
  // board_selection_callback above (which fires after). No duplicate load needed.
  m_sidebar->set_node_selected_callback([](Section section,
                                               std::vector<int> path) {
    (void)section; (void)path; // handled by board_selection_callback
  });

  m_sidebar->set_split_node_callback(
      [this](Section section, std::vector<int> path) {
        action_split_node(section, path);
      });
  m_sidebar->set_combine_nodes_callback(
      [this](Section section, std::vector<std::vector<int>> paths) {
        action_combine_nodes(section, std::move(paths));
      });
  m_sidebar->set_global_search_callback([this](const std::string &query) {
    action_search();
    if (m_search_dialog)
      m_search_dialog->set_query(query);
  });

  // Timeline: tab click → apply_selection for that node
  m_timeline->set_tab_activated_callback(
      [this](const OpenTab &tab) { on_tab_activated(tab); });
  m_timeline->set_tab_closed_callback(
      [this](const OpenTab &tab) { on_tab_closed(tab); });

  // Inspector: metadata changed → refresh sidebar + timeline chip + titlebar
  m_inspector->set_meta_changed_callback(
      [this](BinderNode *node) { on_meta_changed(node); });

  // Inspector: content replaced (snapshot restore) → reload Editor buffer
  m_inspector->set_content_changed_callback([this](BinderNode *node) {
    if (m_editor && node)
      m_editor->load_node(node);
  });

  // Inspector: toast notification → show in Editor overlay
  m_inspector->set_toast_callback([this](const std::string &msg) {
    if (m_editor)
      m_editor->show_toast(msg);
  });

  // Inspector progress footer — save on toggle, restore on startup
  m_inspector->set_progress_disclosure_callback([this](bool expanded) {
    m_prefs.inspector_progress_expanded = expanded;
    try {
      m_prefs.save();
    } catch (...) {
    }
  });
  m_inspector->set_progress_expanded(m_prefs.inspector_progress_expanded);

  // ── Annotation wiring ────────────────────────────────────────────────────
  // Editor → Inspector: annotations changed (add/edit)
  m_editor->on_annotations_changed = [this]() {
    m_inspector->notify_annotations_changed();
  };

  // ── Internal hyperlink wiring ─────────────────────────────────────────────
  m_editor->set_on_follow_link(
      [this](const std::string &target_iid, const std::string &anchor_id) {
        navigate_to_link(target_iid, anchor_id);
      });

  // ── Split wiring ──────────────────────────────────────────────────────────
  m_editor->on_split_requested = [this](BinderNode *original,
                                        std::vector<std::string> new_chunks) {
    if (!original || new_chunks.empty())
      return;

    // Find path of the original node by its stable ID
    int orig_id = original->id;
    auto orig_path = m_model.path_for_id(Section::Manuscript, orig_id);
    if (orig_path.empty())
      return;

    // Parent path = all but last element; index after original = last+1
    std::vector<int> parent_path(orig_path.begin(), orig_path.end() - 1);
    int insert_after = orig_path.back();

    // ── Collect all existing titles in the manuscript (flat) ─────────────────
    std::set<std::string> used_titles;
    std::function<void(const std::vector<BinderNode> &)> collect_titles =
        [&](const std::vector<BinderNode> &nodes) {
          for (auto &n : nodes) {
            used_titles.insert(n.title);
            collect_titles(n.children);
          }
        };
    collect_titles(m_model.root(Section::Manuscript));

    // Return a title that does not exist in used_titles.
    // If `base` is already unique, return it as-is.
    // Otherwise try "base (2)", "base (3)", … until one is free.
    auto unique_title = [&](const std::string &base) -> std::string {
      if (!used_titles.count(base))
        return base;
      for (int n = 2;; ++n) {
        std::string candidate = base + " (" + std::to_string(n) + ")";
        if (!used_titles.count(candidate))
          return candidate;
      }
    };

    // Insert new scenes in order, moving each into position after the previous
    int prev_idx = insert_after;
    for (size_t i = 0; i < new_chunks.size(); ++i) {
      // New scenes numbered from (2) up using the original node's title.
      std::string title =
          unique_title(original->title + " (" + std::to_string(i + 2) + ")");
      used_titles.insert(title);

      // Add new leaf at parent level
      auto new_path = m_model.add_leaf(Section::Manuscript, parent_path, title);

      // Set its content
      auto *new_node = m_model.node_at(Section::Manuscript, new_path);
      if (new_node) {
        new_node->content = new_chunks[i];
        new_node->content_modified = true;
      }

      // Move it into position immediately after the last placed scene
      auto cur_new = m_model.path_for_id(Section::Manuscript,
                                         new_node ? new_node->id : -1);
      if (!cur_new.empty() && new_node)
        m_model.move_node(Section::Manuscript, cur_new, parent_path,
                          prev_idx + 1);

      // Track last placed position for next sibling
      if (new_node) {
        auto final_path =
            m_model.path_for_id(Section::Manuscript, new_node->id);
        if (!final_path.empty())
          prev_idx = final_path.back();
      }
    }

    m_model.mark_modified();
    if (m_sidebar)
      m_sidebar->rebuild();
    update_title_bar();
  };
  // Inspector → Editor: scroll to annotation when card clicked
  m_inspector->on_scroll_to_annotation = [this](int id) {
    if (m_editor)
      m_editor->scroll_to_annotation(id);
  };
  // Inspector → Editor: delete annotation from card
  m_inspector->on_delete_annotation = [this](int id) {
    if (m_editor)
      m_editor->remove_annotation(id);
  };
  m_inspector->on_edit_annotation_text = [this](int id,
                                                const std::string &text) {
    if (!m_editor)
      return;
    // Find kind/color from current node annotations for the edit call
    auto *node = m_model.active_node();
    if (!node)
      return;
    for (const auto &a : node->annotations) {
      if (a.id == id) {
        m_editor->edit_annotation(id, text, a.kind, a.color_hex);
        return;
      }
    }
  };
  m_inspector->on_delete_annotation_from_node = [this](BinderNode *node, int id) {
    if (m_editor)
      m_editor->remove_annotation_from_node(node, id);
  };
  m_inspector->on_edit_annotation_on_node = [this](BinderNode *node, int id,
                                                    const std::string &text,
                                                    const std::string &kind,
                                                    const std::string &color_hex) {
    if (m_editor)
      m_editor->edit_annotation_on_node(node, id, text, kind, color_hex);
  };

  // Inspector template picker button
  m_editor->set_template_picker_callback([this](Gtk::Widget *anchor) {
    if (!m_sidebar)
      return;
    m_sidebar->show_template_picker(anchor, [this](const BinderNode &tpl) {
      // Apply template to the currently active node
      BinderNode *node = m_model.active_node();
      if (!node)
        return;
      node->content = tpl.content;
      node->title = tpl.title;
      node->color_idx = tpl.color_idx;
      node->status = tpl.status;
      if (tpl.word_target > 0)
        node->word_target = tpl.word_target;
      m_model.mark_modified();
      if (m_editor)
        m_editor->load_node(node);
      if (m_inspector)
        m_inspector->load_node(node);
      if (m_sidebar)
        m_sidebar->rebuild();
    });
  });

  // Editor: focus mode
  m_editor->set_focus_mode_callback([this](bool entering) {
    if (entering) {
      fullscreen();
      m_headerbar.set_visible(false);
      if (m_timeline)
        m_timeline->set_visible(false);
      if (m_sidebar)
        m_sidebar->set_visible(false);
      if (m_inspector)
        m_inspector->set_visible(false);
    } else {
      unfullscreen();
      m_headerbar.set_visible(true);
      if (m_timeline)
        m_timeline->set_visible(true);
      apply_layout_state();
    }
  });
  m_editor->set_session_changed_callback([this]() {
    if (m_sidebar)
      m_sidebar->refresh_session();
  });
  m_editor->set_snapshot_saved_callback([this]() {
    if (m_inspector)
      m_inspector->refresh_history();
  });

  // Pomodoro tile: clicking "Open Timer ↗" shows the floating dialog
  m_sidebar->set_pomodoro_tile_clicked_callback([this]() {
    action_pomodoro(); // present() the window
  });

  // Pomodoro tile: settings gear → open prefs at Pomodoro page
  m_sidebar->set_pomodoro_tile_settings_callback([this]() {
    action_preferences();
    Glib::signal_idle().connect_once([this]() {
      if (m_prefs_dialog)
        m_prefs_dialog->navigate_to_page("pomodoro");
    });
  });

  // Pomodoro tile: play/pause button toggles timer WITHOUT showing dialog
  m_sidebar->set_pomodoro_tile_play_pause_callback([this]() {
    ensure_pomodoro_timer(); // create if needed, no present()
    m_pomodoro_dialog->toggle_timer();
    push_pomodoro_to_sidebar();
  });

  // Sidebar disclosure state — save to prefs immediately on any toggle
  m_sidebar->set_disclosure_changed_callback([this]() {
    m_prefs.sidebar_sec_manuscript_expanded =
        m_sidebar->sec_manuscript_expanded();
    m_prefs.sidebar_sec_characters_expanded =
        m_sidebar->sec_characters_expanded();
    m_prefs.sidebar_sec_places_expanded = m_sidebar->sec_places_expanded();
    m_prefs.sidebar_sec_references_expanded =
        m_sidebar->sec_references_expanded();
    m_prefs.sidebar_sec_templates_expanded =
        m_sidebar->sec_templates_expanded();
    m_prefs.sidebar_sec_trash_expanded = m_sidebar->sec_trash_expanded();
    m_prefs.sidebar_pomo_tile_expanded = m_sidebar->pomo_tile_expanded();
    m_prefs.sidebar_session_tile_expanded = m_sidebar->session_tile_expanded();
    try {
      m_prefs.save();
    } catch (...) {
    }
  });

  // Start the autosave timer
  start_autosave_timer();

  // Seed the tile with the correct prefs-derived idle state on startup
  push_pomodoro_to_sidebar();

  // Restore sidebar disclosure state from prefs
  if (m_sidebar)
    m_sidebar->apply_disclosure_state(m_prefs.sidebar_sec_manuscript_expanded,
                                      m_prefs.sidebar_sec_characters_expanded,
                                      m_prefs.sidebar_sec_places_expanded,
                                      m_prefs.sidebar_pomo_tile_expanded,
                                      m_prefs.sidebar_session_tile_expanded,
                                      m_prefs.sidebar_sec_references_expanded,
                                      m_prefs.sidebar_sec_templates_expanded,
                                      m_prefs.sidebar_sec_trash_expanded);
}

// ─────────────────────────────────────────────────────────────────────────────
// Callback handlers
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::on_node_opened(Section section, const std::vector<int> &path) {
  if (m_timeline) {
    m_restoring_tabs = true;   // suppress on_tab_activated during open_node
    m_timeline->open_node(section, path);
    m_restoring_tabs = false;
  }
  BinderNode *n = m_model.node_at(section, path);
  if (n)
    apply_selection({BoardItem::make(section, n->iid)});
}

void MainWindow::on_node_changed(BinderNode *node) {
  if (m_restoring_tabs || m_broadcasting)
    return;
  if (!node) {
    apply_selection({});
    return;
  }
  apply_selection({BoardItem::make(m_model.active_section, node->iid)});
}

void MainWindow::on_tab_activated(const OpenTab &tab) {
  if (m_restoring_tabs)
    return;
  if (m_navigating_link)
    return;
  if (tab.path.empty()) {
    apply_selection({});
    return;
  }
  BinderNode *n = m_model.node_at(tab.section, tab.path);
  if (n)
    apply_selection({BoardItem::make(tab.section, n->iid)});
  else
    apply_selection({});
}

void MainWindow::on_tab_closed(const OpenTab & /*tab*/) {
  // Timeline fires on_tab_activated with empty sentinel when needed
}

// ─────────────────────────────────────────────────────────────────────────────
// navigate_to_link — follow an internal hyperlink to target node + anchor
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::navigate_to_link(const std::string &target_iid,
                                  const std::string &anchor_id) {
  // Find the target node across all sections by its stable iid
  BinderNode *target = m_model.find_node_by_iid(target_iid);
  if (!target) {
    LOG_WARN("navigate_to_link: node {} not found", target_iid);
    if (m_editor)
      m_editor->show_toast("Link target no longer exists.");
    return;
  }

  // Find its path so we can open it in the timeline
  std::vector<int> path;
  Section section = Section::Manuscript;
  for (auto s : {Section::Manuscript, Section::Characters, Section::Places,
                 Section::References, Section::Templates}) {
    path = m_model.path_for_iid(s, target_iid);
    if (!path.empty()) {
      section = s;
      break;
    }
  }
  if (path.empty()) {
    LOG_WARN("navigate_to_link: path for node {} not found", target_iid);
    return;
  }

  LOG_INFO("navigate_to_link: node={} anchor='{}' path size={}", target_iid,
           anchor_id, path.size());

  // Open the node in the timeline (opens a tab if not already open).
  // Guard with m_navigating_link so on_tab_activated doesn't fire a premature
  // load_node while the buffer still contains the previous node's content —
  // that would cause save_current() to overwrite the source node with the
  // wrong HTML, destroying any links it contains.
  // set_active below triggers on_node_changed → the single correct load_node.
  m_navigating_link = true;
  if (m_timeline)
    m_timeline->open_node(section, path);
  m_navigating_link = false;

  // set_active triggers on_node_changed → load_node (the one correct call)
  m_model.set_active(section, path);

  // After load_node completes (deferred), scroll to anchor and highlight
  if (!anchor_id.empty()) {
    Glib::signal_idle().connect_once([this, anchor_id]() {
      if (!m_editor)
        return;
      m_editor->scroll_to_anchor(anchor_id);
    });
  }
}

void MainWindow::on_meta_changed(BinderNode *node) {
  if (m_sidebar)
    m_sidebar->rebuild();
  if (m_editor)
    m_editor->update_word_count();
  if (m_editor)
    m_editor->refresh_chapter_tag();
  if (node && m_timeline)
    m_timeline->refresh_tab_title(m_model.active_section, m_model.active_path);
  update_title_bar();
}

// ─────────────────────────────────────────────────────────────────────────────
// Title bar
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::update_title_bar() {
  std::string title = m_model.project_title;
  if (m_model.is_modified)
    title += " •";
  m_title_label.set_text(title);

  // In multi-node (joined) writing, show segment count indicator
  if (m_editor && m_editor->joined_segment_count() > 0) {
    int n = (int)m_editor->joined_segment_count();
    std::string sub =
        "Novel · " + std::to_string(m_model.total_words()) + " words";
    sub += "  —  ⧉ " + std::to_string(n) + " scenes";
    m_subtitle_label.set_text(sub);
    return;
  }

  std::string sub =
      "Novel · " + std::to_string(m_model.total_words()) + " words";
  if (!m_model.current_path.empty()) {
    auto pos = m_model.current_path.rfind('/');
    sub += "  —  " + ((pos == std::string::npos)
                          ? m_model.current_path
                          : m_model.current_path.substr(pos + 1));
  }
  m_subtitle_label.set_text(sub);
}

// ─────────────────────────────────────────────────────────────────────────────
// File operations
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::show_error(const std::string &msg) {
  auto dlg = Gtk::AlertDialog::create(msg);
  dlg->set_modal(true);
  dlg->set_buttons({"OK"});
  dlg->set_default_button(0);
  dlg->set_cancel_button(0);
  dlg->choose(*this, [dlg](Glib::RefPtr<Gio::AsyncResult> &r) {
    try {
      dlg->choose_finish(r);
    } catch (...) {
    }
  });
}

void MainWindow::confirm_discard_async(std::function<void()> then) {
  if (!m_model.is_modified) {
    then();
    return;
  }
  auto dlg = Gtk::AlertDialog::create("Unsaved changes");
  dlg->set_detail("You have unsaved changes. Discard them and continue?");
  dlg->set_modal(true);
  dlg->set_buttons({"Cancel", "Discard"});
  dlg->set_cancel_button(0);
  dlg->set_default_button(0);
  dlg->choose(*this, [dlg, then = std::move(then)](
                         Glib::RefPtr<Gio::AsyncResult> &r) mutable {
    int response = 0;
    try {
      response = dlg->choose_finish(r);
    } catch (...) {
    }
    if (response == 1)
      then();
  });
}

void MainWindow::action_new() {
  confirm_discard_async([this]() {
    if (m_timeline)
      m_timeline->clear();
    m_model = DocumentModel::new_project();
    m_model.daily_target = m_prefs.daily_word_goal;
    m_model.on_node_changed = [this](BinderNode *n) { on_node_changed(n); };
    if (m_sidebar) {
      m_sidebar->rebuild();
      m_sidebar->refresh_session();
    }
    if (m_inspector)
      m_inspector->load_project();
    apply_selection({});
    update_title_bar();
  });
}

void MainWindow::action_new_from_pattern() {
  confirm_discard_async([this]() {
    // s24 (Layer 3): gather the author's inputs in a dialog, then scaffold.
    // confirm_discard already cleared the way, so applying is unconditional.
    m_pattern_dialog = std::make_unique<PatternDialog>(*this);

    m_pattern_dialog->set_apply_callback([this](const Module& mod, const PlanInputs& in) {
      if (m_timeline)
        m_timeline->clear();
      m_model = DocumentModel::new_project();
      m_model.root(Section::Manuscript).clear();   // drop new_project's seed Part I/Chapter 1
      m_model.daily_target = m_prefs.daily_word_goal;
      m_model.on_node_changed = [this](BinderNode *n) { on_node_changed(n); };

      // Scaffold the manuscript from the AUTHORED arc (s29 — the dialog's Key
      // Points editor produced `mod`; the built-in is only its starting seed).
      // s23: install the KP spectrum palette so tag and colour are one entry —
      // each KP's color_idx (= its order) lands on a swatch named after it.
      {
        auto pal = ModuleIO::keypoint_palette(mod);
        m_prefs.tag_colors.clear();
        for (auto& e : pal) m_prefs.tag_colors.push_back({e.first, e.second});
        m_prefs.save();   // persist so the KP swatches join the app colour list
      }
      ScaffoldPlan plan = ModulePlanner::plan(mod, in);
      ModuleMaterializer::materialize(m_model, plan);

      if (m_sidebar) {
        m_sidebar->rebuild();
        m_sidebar->refresh_session();
      }
      if (m_inspector) {
        m_inspector->refresh_prefs_dropdowns(); // s23: pick up the new KP palette
        m_inspector->load_project();
      }
      apply_selection({});
      update_title_bar();
    });

    m_pattern_dialog->signal_hide().connect([this]() {
      Glib::signal_idle().connect_once([this]() { m_pattern_dialog.reset(); });
    });
    m_pattern_dialog->present();
  });
}

void MainWindow::action_open() {
  confirm_discard_async([this]() {
    // s19: a v5 project is a .folio BUNDLE DIRECTORY, so Open selects a folder.
    // load_from() detects bundle-vs-legacy; a non-bundle folder is rejected
    // cleanly via show_error. (Folder choosers don't apply name patterns, so no
    // *.folio filter here — the user picks the .folio bundle directory.)
    auto dlg = Gtk::FileChooserNative::create("Open Folio Project", *this,
                                              Gtk::FileChooser::Action::SELECT_FOLDER,
                                              "Open", "Cancel");
    dlg->signal_response().connect([this, dlg](int response) {
      if (response != Gtk::ResponseType::ACCEPT)
        return;
      auto file = dlg->get_file();
      if (!file)
        return;
      open_path(file->get_path());
    });
    dlg->show();
  });
}

void MainWindow::open_path(std::string path) {
  if (m_editor)
    m_editor->load_empty();
  if (m_inspector)
    m_inspector->load_empty();
  if (m_timeline)
    m_timeline->clear();
  try {
    m_model.load_from(path);
  } catch (const std::exception &ex) {
    show_error(std::string("Failed to open:\n") + ex.what());
    return;
  }
  m_model.on_node_changed = [this](BinderNode *n) { on_node_changed(n); };
  if (m_model.daily_target > 0) {
    m_prefs.daily_word_goal = m_model.daily_target;
    try {
      m_prefs.save();
    } catch (...) {
    }
  }
  if (m_sidebar) {
    m_restoring_tabs = true;
    m_sidebar->rebuild();
    m_restoring_tabs = false;
    m_sidebar->refresh_session();
  }
  if (m_inspector)
    m_inspector->load_project();

  // Restore timeline tabs visually — no auto-activation. Tabs persist by iid
  // (s20); resolve each to its current path for the positionally-addressed
  // timeline. A tab whose node no longer exists is silently dropped.
  if (m_timeline && !m_model.open_tabs.empty()) {
    m_restoring_tabs = true;
    for (const auto &t : m_model.open_tabs) {
      auto path = m_model.path_for_iid(t.section, t.iid);
      if (!path.empty())
        m_timeline->open_node(t.section, path);
    }
    m_restoring_tabs = false;
  }

  // Restore selection state — broadcasts to all modes via apply_selection
  if (!m_model.sidebar_selected_iid.empty())
    apply_selection({BoardItem::make(m_model.sidebar_selected_section,
                                    m_model.sidebar_selected_iid)});
  else
    apply_selection({});

  m_prefs.push_recent(path);
  m_prefs.save();
  rebuild_recent_menu();
  update_title_bar();
  m_prefs.push_recent(path);
  m_prefs.save();
  rebuild_recent_menu();
  update_title_bar();
}

void MainWindow::action_save() {
  // Snapshot current timeline state into the model before writing to disk
  snapshot_open_tabs();
  // Snapshot sidebar selection (first item if any)
  if (m_sidebar) {
    auto sel = m_sidebar->get_board_selection();
    if (!sel.empty()) {
      m_model.sidebar_selected_section = sel.front().section;
      m_model.sidebar_selected_iid     = sel.front().iid;
    } else {
      m_model.sidebar_selected_iid.clear();
    }
  }
  if (m_model.current_path.empty()) {
    action_save_as();
    return;
  }
  try {
    m_model.save();
    m_prefs.push_recent(m_model.current_path);
    m_prefs.save();
    rebuild_recent_menu();
    update_title_bar();
  } catch (const std::exception &ex) {
    show_error(std::string("Failed to save:\n") + ex.what());
  }
}

void MainWindow::action_save_as() {
  auto dlg = Gtk::FileChooserNative::create("Save Folio Project", *this,
                                            Gtk::FileChooser::Action::SAVE,
                                            "Save", "Cancel");
  auto filter = Gtk::FileFilter::create();
  filter->set_name("Folio projects (*.folio)");
  filter->add_pattern("*.folio");
  dlg->add_filter(filter);
  dlg->set_current_name(m_model.project_title + ".folio");
  dlg->signal_response().connect([this, dlg](int response) {
    if (response != Gtk::ResponseType::ACCEPT)
      return;
    try {
      auto file = dlg->get_file();
      if (!file)
        return;
      std::string path = file->get_path();
      if (path.size() < 6 || path.substr(path.size() - 6) != ".folio")
        path += ".folio";
      // Snapshot timeline state before writing
      snapshot_open_tabs();
      m_model.save_to(path);
      m_prefs.push_recent(path);
      m_prefs.save();
      rebuild_recent_menu();
      update_title_bar();
    } catch (const std::exception &ex) {
      show_error(std::string("Failed to save:\n") + ex.what());
    }
  });
  dlg->show();
}

// ─────────────────────────────────────────────────────────────────────────────
// Actions
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::setup_actions() {
  auto add = [this](const std::string &name, auto fn) {
    auto a = Gio::SimpleAction::create(name);
    a->signal_activate().connect([fn](const Glib::VariantBase &) { fn(); });
    add_action(a);
  };

  add("new", [this]() { action_new(); });
  add("new-from-pattern", [this]() { action_new_from_pattern(); });
  add("open", [this]() { action_open(); });
  add("save", [this]() { action_save(); });
  add("save-as", [this]() { action_save_as(); });
  add("quit", [this]() {
    confirm_discard_async([this]() {
      m_closing = true;
      close();
    });
  });

  add("export", [this]() { action_export(); });
  add("import", [this]() { action_import(); });
  add("print", [this]() { action_print(); });
  add("save-report", [this]() { action_save_report(); });
  add("search", [this]() { action_search(); });

  add("preferences", [this]() { action_preferences(); });
  add("pomodoro", [this]() { action_pomodoro(); });

  // Inspector tab hotkeys
  add("inspector-project", [this]() {
    if (m_inspector)
      m_inspector->navigate_to_tab(0);
  });
  add("inspector-metadata", [this]() {
    if (m_inspector)
      m_inspector->navigate_to_tab(1);
  });
  add("inspector-notes", [this]() {
    if (m_inspector)
      m_inspector->navigate_to_tab(2);
  });
  add("inspector-snapshots", [this]() {
    if (m_inspector)
      m_inspector->navigate_to_tab(3);
  });

  // Timeline navigation
  add("timeline-prev", [this]() {
    if (m_timeline)
      m_timeline->navigate_prev();
  });
  add("timeline-next", [this]() {
    if (m_timeline)
      m_timeline->navigate_next();
  });
  add("batch-snapshot", [this]() {
    m_snapshot_dialog = std::make_unique<SnapshotDialog>(*this, m_model);
    m_snapshot_dialog->set_saved_callback([this]() {
      if (m_inspector)
        m_inspector->refresh_history();
    });
    m_snapshot_dialog->signal_hide().connect(
        [this]() { m_snapshot_dialog.reset(); });
    m_snapshot_dialog->present();
  });

  // ── Tools-menu actions ────────────────────────────────────────────────────
  add("tool-annotation-report", [this]() {
    if (m_inspector) m_inspector->open_annotation_report();
  });
  add("tool-barcode", [this]() {
    if (m_inspector) m_inspector->open_barcode();
  });
  add("tool-project-goals", [this]() {
    if (m_inspector) m_inspector->open_project_goals();
  });
  add("tool-rulers", [this]() {
    if (m_editor) m_editor->open_ruler_manager();
  });
  add("tool-screenplay-help", [this]() {
    if (m_editor) m_editor->open_screenplay_help();
  });
  add("tool-style-manager", [this]() {
    if (m_editor) m_editor->open_style_manager();
  });

  add("focus-mode", [this]() {
    if (!m_editor) return;
    // Distraction-free is now a separate fullscreen window sharing the editor's
    // live buffer — the editor's own geometry/typography is never swapped, so
    // there is nothing to restore on return (and nothing to corrupt).
    if (!m_focus_window)
      m_focus_window =
          std::make_unique<FocusWindow>(m_model, m_prefs, *m_editor);
    m_focus_window->present_focus(m_editor->current_node());
  });
  add("toggle-binder",
      [this]() { m_btn_binder.set_active(!m_btn_binder.get_active()); });
  add("toggle-inspector", [this]() {
    m_btn_inspector_toggle.set_active(!m_btn_inspector_toggle.get_active());
  });
  add("shortcuts", [this]() { show_shortcuts_window(); });
  add("about", [this]() { show_about_dialog(); });
  add("close-all-tabs", [this]() {
    if (m_timeline)
      m_timeline->close_all_tabs();
  });
  add("add-scene", [this]() {
    if (m_sidebar)
      m_sidebar->add_scene_to_active();
  });
  add("add-character", [this]() {
    if (m_sidebar)
      m_sidebar->add_character_to_active();
  });
  add("add-place", [this]() {
    if (m_sidebar)
      m_sidebar->add_place_to_active();
  });
  add("add-reference", [this]() {
    if (m_sidebar)
      m_sidebar->add_reference_to_active();
  });
  add("add-template", [this]() {
    if (m_sidebar)
      m_sidebar->add_template_to_active();
  });
  add("add-group-manuscript", [this]() {
    if (m_sidebar)
      m_sidebar->add_group_to_manuscript();
  });
  add("add-group-characters", [this]() {
    if (m_sidebar)
      m_sidebar->add_group_to_characters();
  });
  add("add-group-places", [this]() {
    if (m_sidebar)
      m_sidebar->add_group_to_places();
  });

  // set_accels_for_action needs get_application() to be non-null, which it
  // isn't during construction (add_window hasn't been called yet).
  // Defer until realize when the window is fully attached to the application.
  signal_realize().connect([this]() {
    auto app = get_application();
    if (!app)
      return;
    app->set_accels_for_action("win.new", {"<Ctrl>n"});
    app->set_accels_for_action("win.open", {"<Ctrl>o"});
    app->set_accels_for_action("win.save", {"<Ctrl>s"});
    app->set_accels_for_action("win.save-as", {"<Ctrl><Shift>s"});
    app->set_accels_for_action("win.export", {"<Ctrl>e"});
    app->set_accels_for_action("win.print", {"<Ctrl>p"});
    app->set_accels_for_action("win.search", {"<Ctrl><Shift>g"});
    app->set_accels_for_action("win.preferences", {"<Ctrl>comma"});
    app->set_accels_for_action("win.pomodoro", {"<Ctrl><Shift>p"});
    app->set_accels_for_action("win.batch-snapshot", {"<Ctrl><Shift>t"});
    app->set_accels_for_action("win.quit", {"<Ctrl>q", "<Ctrl>w"});
    app->set_accels_for_action("win.focus-mode", {"<Ctrl><Shift>f"});
    app->set_accels_for_action("win.toggle-binder", {"<Ctrl><Shift>b"});
    app->set_accels_for_action("win.toggle-inspector", {"<Ctrl><Shift>i"});
    app->set_accels_for_action("win.close-all-tabs", {"<Ctrl><Shift>w"});
    app->set_accels_for_action("win.timeline-prev",
                               {"<Alt>Left", "<Ctrl><Shift>Tab"});
    app->set_accels_for_action("win.timeline-next",
                               {"<Alt>Right", "<Ctrl>Tab"});
    app->set_accels_for_action("win.inspector-project", {"<Alt>p"});
    app->set_accels_for_action("win.inspector-metadata", {"<Alt>m"});
    app->set_accels_for_action("win.inspector-notes", {"<Alt>n"});
    app->set_accels_for_action("win.inspector-snapshots", {"<Alt>s"});
    app->set_accels_for_action("win.add-scene", {"<Ctrl><Alt>s"});
    app->set_accels_for_action("win.add-character", {"<Ctrl><Alt>c"});
    app->set_accels_for_action("win.add-place", {"<Ctrl><Alt>p"});
    app->set_accels_for_action("win.add-reference", {"<Ctrl><Alt>r"});
    app->set_accels_for_action("win.add-template", {"<Ctrl><Alt>t"});
    app->set_accels_for_action("win.add-group-manuscript",
                               {"<Ctrl><Alt><Shift>s"});
    app->set_accels_for_action("win.add-group-characters",
                               {"<Ctrl><Alt><Shift>c"});
    app->set_accels_for_action("win.add-group-places", {"<Ctrl><Alt><Shift>p"});
    app->set_accels_for_action("win.shortcuts", {"<Ctrl>question"});
  });

  // Open Recent slots
  for (int i = 0; i < FolioPrefs::MAX_RECENT; ++i) {
    auto a = Gio::SimpleAction::create("open-recent-" + std::to_string(i));
    a->signal_activate().connect([this, i](const Glib::VariantBase &) {
      if (i >= (int)m_prefs.recent_files.size())
        return;
      confirm_discard_async([this, i]() {
        if (i < (int)m_prefs.recent_files.size())
          open_path(m_prefs.recent_files[i]);
      });
    });
    add_action(a);
  }

  // Clear recent files
  add("clear-recent", [this]() {
    m_prefs.clear_recent();
    try {
      m_prefs.save();
    } catch (...) {
    }
    rebuild_recent_menu();
  });
}

// ─────────────────────────────────────────────────────────────────────────────
// ─────────────────────────────────────────────────────────────────────────────
// Export dialog
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::action_export() {
  m_export_dialog = std::make_unique<ExportDialog>(*this, m_model, m_prefs);
  m_export_dialog->signal_hide().connect([this]() {
    Glib::signal_idle().connect_once([this]() { m_export_dialog.reset(); });
  });
  m_export_dialog->present();
}

// ─────────────────────────────────────────────────────────────────────────────
// Print dialog
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::action_print() {
  m_print_dialog = std::make_unique<PrintDialog>(*this, m_model, m_prefs);
  m_print_dialog->signal_hide().connect([this]() {
    Glib::signal_idle().connect_once([this]() { m_print_dialog.reset(); });
  });
  m_print_dialog->present();
}

// ─────────────────────────────────────────────────────────────────────────────
// Save Report as HTML
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::action_save_report() {
  std::string html = ReportEngine::render_html(
      ReportEngine::generate(m_model, m_prefs), false);

  // Sanitise filename
  std::string base =
      m_model.project_title.empty() ? "report" : m_model.project_title;
  std::string safe;
  for (unsigned char c : base)
    safe += (std::isalnum(c) || c == '-' || c == '_') ? (char)c : '_';

  auto filter = Gtk::FileFilter::create();
  filter->set_name("HTML Document");
  filter->add_pattern("*.html");
  auto filters = Gio::ListStore<Gtk::FileFilter>::create();
  filters->append(filter);

  // Store as member so the RefPtr stays alive through the async callback
  m_report_file_dialog = Gtk::FileDialog::create();
  m_report_file_dialog->set_title("Save Report As…");
  m_report_file_dialog->set_initial_name(safe + "-report.html");
  m_report_file_dialog->set_filters(filters);

  m_report_file_dialog->save(*this, [this, html](
                                        Glib::RefPtr<Gio::AsyncResult> &res) {
    Glib::RefPtr<Gio::File> file;
    try {
      file = m_report_file_dialog->save_finish(res);
    } catch (const Gtk::DialogError &e) {
      // User dismissed — not an error
      m_report_file_dialog.reset();
      return;
    } catch (const Glib::Error &e) {
      // Show error in a simple dialog
      auto err_dlg = Gtk::AlertDialog::create(e.what());
      err_dlg->show(*this);
      m_report_file_dialog.reset();
      return;
    }

    if (!file) {
      m_report_file_dialog.reset();
      return;
    }

    std::string path = file->get_path();
    FILE *f = fopen(path.c_str(), "wb");
    if (!f) {
      auto err_dlg = Gtk::AlertDialog::create("Could not write to: " + path);
      err_dlg->show(*this);
    } else {
      fwrite(html.data(), 1, html.size(), f);
      fclose(f);
    }
    m_report_file_dialog.reset();
  });
}

// ─────────────────────────────────────────────────────────────────────────────
// Import
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::action_import() {
  m_import_dialog = std::make_unique<ImportDialog>(*this, m_model);

  m_import_dialog->set_import_callback([this](std::vector<ImportNode> nodes) {
    // Insert nodes into the manuscript root.
    // depth==0 nodes are added at top level; depth==1 nodes go inside the
    // last group that was added at depth 0.
    std::vector<int> last_group_path;

    for (auto &nd : nodes) {
      if (nd.is_group) {
        // Add a Group at top level (or at depth, if nested — currently max 1
        // deep)
        std::vector<int> parent = (nd.depth > 0 && !last_group_path.empty())
                                      ? last_group_path
                                      : std::vector<int>{};
        auto path = m_model.add_group(Section::Manuscript, parent, nd.title);
        if (nd.depth == 0)
          last_group_path = path;
        // Groups carry no prose content in import (synopsis only)
      } else {
        std::vector<int> parent = (nd.depth > 0 && !last_group_path.empty())
                                      ? last_group_path
                                      : std::vector<int>{};
        auto path = m_model.add_leaf(Section::Manuscript, parent, nd.title);
        auto *node = m_model.node_at(Section::Manuscript, path);
        if (node) {
          node->content = nd.html;
          node->content_modified = true;
        }
      }
    }

    m_model.mark_modified();
    if (m_sidebar)
      m_sidebar->rebuild();
    update_title_bar();
  });

  m_import_dialog->signal_hide().connect([this]() {
    Glib::signal_idle().connect_once([this]() { m_import_dialog.reset(); });
  });
  m_import_dialog->present();
}

// ─────────────────────────────────────────────────────────────────────────────
// Search & Replace dialog
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::action_search() {
  if (!m_search_dialog) {
    m_search_dialog = std::make_unique<SearchDialog>(*this, m_model);

    // Open node in editor when user clicks a result
    m_search_dialog->set_open_node_callback(
        [this](Section section, std::vector<int> path) {
          m_model.set_active(section, path);
          BinderNode *node = m_model.node_at(section, path);
          if (m_sidebar && node)
            m_sidebar->set_active(section, node->iid);
          if (m_editor && node)
            m_editor->load_node(node);
          if (m_inspector && node)
            m_inspector->load_node(node);
        });

    // Notify editor when a node's content changes via Replace All
    m_search_dialog->set_node_changed_callback(
        [this](Section section, std::vector<int> path, std::string /*new_html*/) {
          // If editor is currently showing this node, reload it
          if (m_model.active_section == section &&
              m_model.active_path == path) {
            BinderNode *node = m_model.node_at(section, path);
            if (m_editor && node) {
              m_editor->load_empty();
              m_editor->load_node(node);
            }
          }
          update_title_bar();
        });

    m_search_dialog->signal_hide().connect([this]() {
      Glib::signal_idle().connect_once([this]() { m_search_dialog.reset(); });
    });
  }
  m_search_dialog->present();
}

// ─────────────────────────────────────────────────────────────────────────────
// Binder split — called from sidebar context menu
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::action_split_node(Section section,
                                   const std::vector<int> &path) {
  BinderNode *node = m_model.node_at(section, path);
  if (!node || binder_kind_is_group(node->kind))
    return;
  if (m_prefs.split_separators.empty())
    return;

  // Build pattern set
  std::set<std::string> patterns(m_prefs.split_separators.begin(),
                                 m_prefs.split_separators.end());

  // Capture everything by value before any mutation
  const std::string html = node->content;
  const std::string orig_title = node->title;
  const int orig_id = node->id;

  // Split into <p>…</p> blocks
  std::vector<std::string> paras;
  {
    size_t pos = 0;
    while (pos < html.size()) {
      size_t open = html.find("<p>", pos);
      if (open == std::string::npos)
        break;
      size_t close = html.find("</p>", open);
      if (close == std::string::npos)
        break;
      paras.push_back(html.substr(open, close + 4 - open));
      pos = close + 4;
    }
  }
  if (paras.size() < 2)
    return;

  // Plain text of a paragraph (strip tags + trim)
  auto para_text = [](const std::string &p) {
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
    size_t a = out.find_first_not_of(" \t\r\n");
    if (a == std::string::npos)
      return std::string{};
    size_t b = out.find_last_not_of(" \t\r\n");
    return out.substr(a, b - a + 1);
  };

  // Collect chunks
  std::vector<std::vector<std::string>> chunks;
  chunks.push_back({});
  for (auto &p : paras) {
    if (patterns.count(para_text(p)))
      chunks.push_back({});
    else
      chunks.back().push_back(p);
  }
  if (chunks.size() < 2)
    return;

  // Build first chunk HTML
  std::string first_html;
  for (auto &p : chunks[0])
    first_html += p;
  if (first_html.empty())
    first_html = "<p></p>";

  // Clear the editor BEFORE mutations so save_current() inside load_node
  // doesn't write the stale buffer through a dangling m_current_node pointer.
  if (m_editor)
    m_editor->load_empty();

  // Write trimmed content into original node — raw pointer still valid here
  // (no mutations yet)
  node->content = first_html;
  node->content_modified = true;

  // Collect used titles for uniqueness
  std::set<std::string> used_titles;
  std::function<void(const std::vector<BinderNode> &)> collect_titles =
      [&](const std::vector<BinderNode> &nodes) {
        for (auto &n : nodes) {
          used_titles.insert(n.title);
          collect_titles(n.children);
        }
      };
  collect_titles(m_model.root(Section::Manuscript));

  auto unique_title = [&](const std::string &base) -> std::string {
    if (!used_titles.count(base))
      return base;
    for (int k = 2;; ++k) {
      std::string c = base + " (" + std::to_string(k) + ")";
      if (!used_titles.count(c))
        return c;
    }
  };

  std::vector<int> parent_path(path.begin(), path.end() - 1);
  int prev_idx = path.back();

  for (size_t i = 1; i < chunks.size(); ++i) {
    std::string ch;
    for (auto &p : chunks[i])
      ch += p;
    if (ch.empty())
      ch = "<p></p>";

    // New scenes are numbered from 2 up using the original title.
    // Original node keeps its title unchanged.
    std::string title =
        unique_title(orig_title + " (" + std::to_string(i + 1) + ")");
    used_titles.insert(title);

    auto new_path = m_model.add_leaf(section, parent_path, title);
    auto *new_node = m_model.node_at(section, new_path);
    if (new_node) {
      new_node->content = ch;
      new_node->content_modified = true;
    }

    // Move new node to position after the last placed node.
    // prev_idx tracks the last inserted sibling (starts at orig position).
    auto cur_new = m_model.path_for_id(section, new_node ? new_node->id : -1);
    if (!cur_new.empty() && new_node)
      m_model.move_node(section, cur_new, parent_path, prev_idx + 1);
    // Update prev_idx to this node's final position for the next iteration
    if (new_node) {
      auto fp = m_model.path_for_id(section, new_node->id);
      if (!fp.empty())
        prev_idx = fp.back();
    }
  }

  // Reload editor with the trimmed first node only if it was active —
  // avoids redundant reloads during multi-scene split from sidebar.
  {
    auto p = m_model.path_for_id(section, orig_id);
    auto *n = m_model.node_at(section, p);
    if (m_editor && n && m_model.active_section == section &&
        m_model.active_path == path) {
      m_model.set_active(section, p);
      m_editor->load_node(n);
      if (m_inspector)
        m_inspector->load_node(n);
    }
  }

  m_model.mark_modified();
  if (m_sidebar)
    m_sidebar->rebuild();
  update_title_bar();
}

// ─────────────────────────────────────────────────────────────────────────────
// Binder combine — called from sidebar multi-select context menu
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::action_combine_nodes(Section section,
                                      std::vector<std::vector<int>> paths) {
  if (paths.size() < 2)
    return;

  // Sort paths into document order
  std::sort(paths.begin(), paths.end());

  // Validate all nodes up-front; capture IDs and content by value
  // before any mutation (trash_node/erase invalidates raw pointers).
  struct NodeSnap {
    int id;
    std::string content;
  };
  std::vector<NodeSnap> snaps;
  for (auto &p : paths) {
    BinderNode *n = m_model.node_at(section, p);
    if (!n || binder_kind_is_group(n->kind))
      return;
    snaps.push_back({n->id, n->content});
  }

  // Build separator HTML paragraph
  std::string sep_html = "<p></p>";
  if (!m_prefs.split_separators.empty()) {
    std::string s = m_prefs.split_separators[0];
    std::string esc;
    for (unsigned char c : s) {
      if (c == '&')
        esc += "&amp;";
      else if (c == '<')
        esc += "&lt;";
      else if (c == '>')
        esc += "&gt;";
      else
        esc += static_cast<char>(c);
    }
    sep_html = "<p>" + esc + "</p>";
  }

  // Build combined content from captured snapshots
  std::string combined = snaps[0].content;
  for (size_t i = 1; i < snaps.size(); ++i) {
    combined += sep_html;
    combined += snaps[i].content;
  }

  // Clear the editor BEFORE any mutations so save_current() inside load_node
  // doesn't write the stale buffer back through a dangling m_current_node
  // pointer (trash_node erases from the vector, invalidating all raw pointers).
  if (m_editor)
    m_editor->load_empty();

  // Write combined content into first node — re-fetch by stable ID
  {
    auto p = m_model.path_for_id(section, snaps[0].id);
    auto *n = m_model.node_at(section, p);
    if (!n)
      return;
    n->content = combined;
    n->content_modified = true;
  }

  // Trash all nodes except the first, re-fetching each path by ID
  // because earlier erases shift indices.
  for (size_t i = 1; i < snaps.size(); ++i) {
    auto p = m_model.path_for_id(section, snaps[i].id);
    if (!p.empty())
      m_model.trash_node(section, p);
  }

  // Reload editor with the combined first node
  {
    auto p = m_model.path_for_id(section, snaps[0].id);
    auto *n = m_model.node_at(section, p);
    if (m_editor && n) {
      m_model.set_active(section, p);
      m_editor->load_node(n);
      if (m_inspector)
        m_inspector->load_node(n);
    }
  }

  m_model.mark_modified();
  if (m_sidebar)
    m_sidebar->rebuild();
  update_title_bar();
}

// ─────────────────────────────────────────────────────────────────────────────
// Preferences dialog
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::action_preferences() {
  if (!m_prefs_dialog) {
    m_prefs_dialog = std::make_unique<PreferencesDialog>(*this, m_prefs);
    // Provide document + global template names for the default-template picker
    {
      std::vector<std::string> tpl_names;
      std::function<void(const std::vector<BinderNode> &)> collect;
      collect = [&](const std::vector<BinderNode> &nodes) {
        for (const auto &n : nodes) {
          if (n.kind == BinderKind::Template && !n.title.empty())
            tpl_names.push_back(n.title);
          if (!n.children.empty())
            collect(n.children);
        }
      };
      collect(m_model.root(Section::Templates));
      // Also include global templates (prefixed so user can distinguish)
      for (const auto &n : m_prefs.global_templates_get())
        if (!n.title.empty())
          tpl_names.push_back("[Global] " + n.title);
      m_prefs_dialog->set_template_list(std::move(tpl_names));
    }
    m_prefs_dialog->signal_hide().connect([this]() {
      bool changed = m_prefs_dialog && m_prefs_dialog->was_changed();
      m_prefs_dialog.reset(); // Rebuild fresh next open so working copies
                              // reflect latest prefs
      if (!changed)
        return;
      m_model.daily_target = m_prefs.daily_word_goal;
      m_model.mark_modified();
      if (m_inspector)
        m_inspector->refresh_prefs_dropdowns();
      if (m_inspector)
        m_inspector->refresh_project_tab();
      if (m_sidebar)
        m_sidebar->rebuild();
      if (m_sidebar)
        m_sidebar->refresh_session();
      if (m_editor) {
        m_editor->apply_font_prefs(m_prefs);
        m_editor->apply_editing_prefs();
      }
      // Trim recent list if max_recent_files was reduced
      if ((int)m_prefs.recent_files.size() > m_prefs.max_recent_files)
        m_prefs.recent_files.resize(m_prefs.max_recent_files);
      rebuild_recent_menu();
      // If the Pomodoro dialog is open, sync its timer to the new durations
      // and repaint the ring with any new phase colours.
      if (m_pomodoro_dialog) {
        m_pomodoro_dialog->apply_prefs();
        m_pomodoro_dialog
            ->refresh_display(); // repaint ring + banner with new colors
      }
      // Reapply theme immediately so pip color override in apply_theme() takes
      // effect right away — the 50ms timeout below handles other theme changes.
      apply_theme(m_dark_mode);
      // Always push to sidebar/headerbar: durations and colors may have changed
      // even if the floating dialog isn't visible.
      push_pomodoro_to_sidebar();
      // Restart autosave timer with new interval/enabled settings
      start_autosave_timer();
      // Reset backup timer so a new interval starts from now
      m_backup_manager.reset_timer();
      Glib::signal_timeout().connect_once(
          [this]() {
            if (m_prefs.theme == "dark")
              apply_theme(true);
            else if (m_prefs.theme == "light")
              apply_theme(false);
            else
              apply_theme(detect_dark_mode());
          },
          50);
    });
  }
  m_prefs_dialog->present();
}

// ─────────────────────────────────────────────────────────────────────────────
// Keyboard shortcuts window
// ─────────────────────────────────────────────────────────────────────────────
void MainWindow::show_shortcuts_window() {
  static const char *UI = R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<interface>
  <object class="GtkShortcutsWindow" id="shortcuts_window">
    <property name="modal">true</property>
    <child>
      <object class="GtkShortcutsSection">
        <property name="title">Project</property>
        <property name="section-name">project</property>
        <child>
          <object class="GtkShortcutsGroup">
            <property name="title">File</property>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">New Project</property>
              <property name="accelerator">&lt;Ctrl&gt;n</property>
            </object></child>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Open…</property>
              <property name="accelerator">&lt;Ctrl&gt;o</property>
            </object></child>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Close Window</property>
              <property name="accelerator">&lt;Ctrl&gt;w</property>
            </object></child>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Save</property>
              <property name="accelerator">&lt;Ctrl&gt;s</property>
            </object></child>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Save As…</property>
              <property name="accelerator">&lt;Ctrl&gt;&lt;Shift&gt;s</property>
            </object></child>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Export…</property>
              <property name="accelerator">&lt;Ctrl&gt;e</property>
            </object></child>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Preferences…</property>
              <property name="accelerator">&lt;Ctrl&gt;comma</property>
            </object></child>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Keyboard Shortcuts</property>
              <property name="accelerator">&lt;Ctrl&gt;question</property>
            </object></child>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Quit</property>
              <property name="accelerator">&lt;Ctrl&gt;q</property>
            </object></child>
          </object>
        </child>
      </object>
    </child>
    <child>
      <object class="GtkShortcutsSection">
        <property name="title">Writing</property>
        <property name="section-name">writing</property>
        <child>
          <object class="GtkShortcutsGroup">
            <property name="title">Formatting</property>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Bold</property>
              <property name="accelerator">&lt;Ctrl&gt;b</property>
            </object></child>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Italic</property>
              <property name="accelerator">&lt;Ctrl&gt;i</property>
            </object></child>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Underline</property>
              <property name="accelerator">&lt;Ctrl&gt;u</property>
            </object></child>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Strikethrough</property>
              <property name="accelerator">&lt;Ctrl&gt;&lt;Shift&gt;s</property>
            </object></child>
          </object>
        </child>
        <child>
          <object class="GtkShortcutsGroup">
            <property name="title">View</property>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Focus Mode</property>
              <property name="accelerator">&lt;Ctrl&gt;&lt;Shift&gt;f</property>
            </object></child>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Toggle Binder</property>
              <property name="accelerator">&lt;Ctrl&gt;&lt;Shift&gt;b</property>
            </object></child>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Toggle Inspector</property>
              <property name="accelerator">&lt;Ctrl&gt;&lt;Shift&gt;i</property>
            </object></child>
          </object>
        </child>
        <child>
          <object class="GtkShortcutsGroup">
            <property name="title">Timeline</property>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Close All Tabs</property>
              <property name="accelerator">&lt;Ctrl&gt;&lt;Shift&gt;w</property>
            </object></child>
          </object>
        </child>
        <child>
          <object class="GtkShortcutsGroup">
            <property name="title">Special Characters</property>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Word Joiner (zero-width no-break)</property>
              <property name="accelerator">&lt;Ctrl&gt;space</property>
            </object></child>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Non-breaking Space</property>
              <property name="accelerator">&lt;Ctrl&gt;&lt;Shift&gt;space</property>
            </object></child>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Non-breaking Hyphen</property>
              <property name="accelerator">&lt;Ctrl&gt;&lt;Shift&gt;minus</property>
            </object></child>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Soft Hyphen</property>
              <property name="accelerator">&lt;Ctrl&gt;minus</property>
            </object></child>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Zero-width Space</property>
              <property name="accelerator">&lt;Ctrl&gt;&lt;Shift&gt;z</property>
            </object></child>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Thin Space</property>
              <property name="accelerator">&lt;Ctrl&gt;&lt;Shift&gt;t</property>
            </object></child>
          </object>
        </child>
        <child>
          <object class="GtkShortcutsGroup">
            <property name="title">Screenplay</property>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Format Reference</property>
              <property name="accelerator">&lt;Ctrl&gt;&lt;Shift&gt;h</property>
            </object></child>
          </object>
        </child>
      </object>
    </child>
    <child>
      <object class="GtkShortcutsSection">
        <property name="title">Binder</property>
        <property name="section-name">binder</property>
        <child>
          <object class="GtkShortcutsGroup">
            <property name="title">Add Items</property>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Add Scene</property>
              <property name="accelerator">&lt;Ctrl&gt;&lt;Alt&gt;s</property>
            </object></child>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Add Character</property>
              <property name="accelerator">&lt;Ctrl&gt;&lt;Alt&gt;c</property>
            </object></child>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Add Place</property>
              <property name="accelerator">&lt;Ctrl&gt;&lt;Alt&gt;p</property>
            </object></child>
          </object>
        </child>
        <child>
          <object class="GtkShortcutsGroup">
            <property name="title">Add Groups</property>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Add Group (Manuscript)</property>
              <property name="accelerator">&lt;Ctrl&gt;&lt;Alt&gt;&lt;Shift&gt;s</property>
            </object></child>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Add Group (Characters)</property>
              <property name="accelerator">&lt;Ctrl&gt;&lt;Alt&gt;&lt;Shift&gt;c</property>
            </object></child>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Add Group (Places)</property>
              <property name="accelerator">&lt;Ctrl&gt;&lt;Alt&gt;&lt;Shift&gt;p</property>
            </object></child>
          </object>
        </child>
        <child>
          <object class="GtkShortcutsGroup">
            <property name="title">Navigation</property>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Move Up / Down</property>
              <property name="accelerator">Up Down</property>
            </object></child>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Enter Group</property>
              <property name="accelerator">&lt;Shift&gt;Right</property>
            </object></child>
            <child><object class="GtkShortcutsShortcut">
              <property name="title">Exit / Collapse Group</property>
              <property name="accelerator">&lt;Shift&gt;Left</property>
            </object></child>
          </object>
        </child>
      </object>
    </child>
  </object>
</interface>
)XML";
  auto builder = Gtk::Builder::create_from_string(UI);
  auto *sw = builder->get_widget<Gtk::ShortcutsWindow>("shortcuts_window");
  sw->set_transient_for(*this);
  sw->present();
}

void MainWindow::show_about_dialog() {
  auto *dlg = Gtk::make_managed<Gtk::AboutDialog>();
  dlg->set_transient_for(*this);
  dlg->set_modal(true);

  dlg->set_program_name("Folio");
  dlg->set_version("0.1.0");
  dlg->set_comments("A focused writing studio for novelists, screenwriters,\n"
                    "and storytellers of every kind.");
  dlg->set_copyright("© 2026 Folio Contributors");
  dlg->set_license_type(Gtk::License::MIT_X11);
  dlg->set_website("https://github.com/folio-writer/folio");
  dlg->set_website_label("View on GitHub");

  dlg->set_authors(
      {"Scott Combs",
       "Portion of the code in this project is adapted from or used "
       "by the following:\n"
       "Copyright © 2013-2026 Niels Lohmann"
       "https://github.com/nlohmann/json?tab=readme-ov-file#license"});

  // Logo — resolved from the compiled-in gresource icon theme (no disk access).
  // folio-brand-symbolic themes light/dark via currentColor; the AboutDialog
  // renders it from the icon name registered in Application::on_activate.
  dlg->set_logo_icon_name("folio-brand-symbolic");

  dlg->present();
}

// ─────────────────────────────────────────────────────────────────────────────
// Pomodoro — internal helpers
// ─────────────────────────────────────────────────────────────────────────────

// Ensures the timer object exists and callbacks are wired.
// Does NOT present the window. Safe to call repeatedly.
void MainWindow::ensure_pomodoro_timer() {
  if (m_pomodoro_dialog)
    return;

  m_pomodoro_dialog = std::make_unique<PomodoroDialog>(*this, m_prefs);

  // Phase change → in-app notification + tile refresh
  m_pomodoro_dialog->signal_phase_changed().connect(
      [this](PomodoroPhase finished, PomodoroPhase next) {
        send_phase_notification(finished, next);
        push_pomodoro_to_sidebar();
      });

  // Every tick → refresh sidebar tile + headerbar pill
  m_pomodoro_dialog->set_tick_callback(
      [this]() { push_pomodoro_to_sidebar(); });

  // Settings button → open PreferencesDialog at the Pomodoro page
  m_pomodoro_dialog->set_open_prefs_callback([this]() {
    action_preferences();
    Glib::signal_idle().connect_once([this]() {
      if (m_prefs_dialog)
        m_prefs_dialog->navigate_to_page("pomodoro");
    });
  });

  // On close-request: hide the window instead of destroying it.
  // The timer keeps ticking in the background.
  m_pomodoro_dialog->signal_close_request().connect(
      [this]() -> bool {
        m_pomodoro_dialog->hide();
        push_pomodoro_to_sidebar(); // refresh pill after hide
        return true;                // suppress default destroy
      },
      false);

  push_pomodoro_to_sidebar();
}

// ─────────────────────────────────────────────────────────────────────────────
// Pomodoro — public actions
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::action_pomodoro() {
  ensure_pomodoro_timer();
  m_pomodoro_dialog->present();
}

void MainWindow::send_phase_notification(PomodoroPhase finished,
                                         PomodoroPhase next) {
  // Work cycle is complete when the last Focus session ends → LongBreak begins.
  bool work_cycle_done =
      (finished == PomodoroPhase::Focus && next == PomodoroPhase::LongBreak);

  // Log the phase that just completed naturally (full duration)
  if (m_pomodoro_dialog)
    log_pomodoro_phase(phase_label(finished),
                       m_pomodoro_dialog->timer_total_sec(),
                       /*completed=*/true);

  std::string title, body;
  std::vector<Glib::ustring> buttons;

  if (work_cycle_done) {
    title = "🎉 Work Cycle Complete!";
    body = "You finished all your focus sessions. Time for a well-earned long "
           "break.";
    buttons = {"Start Long Break", "Stop"};
  } else {
    switch (next) {
    case PomodoroPhase::Focus:
      title = "Back to Work ✏️";
      body = "Break is over — ready to focus?";
      buttons = {"Start Focus", "Stop"};
      break;
    case PomodoroPhase::ShortBreak:
      title = "Short Break 🍵";
      body = "Good work! Take 5 minutes.";
      buttons = {"Start Break", "Stop"};
      break;
    case PomodoroPhase::LongBreak:
      title = "Long Break 🌿";
      body = "Take a longer rest.";
      buttons = {"Start Break", "Stop"};
      break;
    }
  }

  auto dlg = Gtk::AlertDialog::create(title);
  dlg->set_detail(body);
  dlg->set_modal(true);
  dlg->set_buttons(buttons);
  dlg->set_default_button(0);
  dlg->set_cancel_button(1);

  dlg->choose(*this, [this, dlg, next](Glib::RefPtr<Gio::AsyncResult> &result) {
    int btn = -1;
    try {
      btn = dlg->choose_finish(result);
    } catch (...) {
    }
    if (btn == 1) {
      // Stop: log next phase as stopped at 0 elapsed, then pause and reset
      log_pomodoro_phase(phase_label(next), 0, /*completed=*/false);
      if (m_pomodoro_dialog) {
        m_pomodoro_dialog->pause_timer();
        m_pomodoro_dialog->reset_phase();
      }
    } else {
      // Start / Continue: begin the next phase clock immediately
      if (m_pomodoro_dialog)
        m_pomodoro_dialog->start_timer();
    }
    push_pomodoro_to_sidebar();
  });

  // Desktop notification (non-blocking secondary alert)
  if (auto app = get_application()) {
    auto noti = Gio::Notification::create(title);
    noti->set_body(body);
    noti->set_priority(Gio::Notification::Priority::HIGH);
    noti->set_default_action("app.activate");
    app->send_notification("folio.pomodoro.phase", noti);
  }
}

void MainWindow::log_pomodoro_phase(const std::string &phase, int elapsed_sec,
                                    bool completed) {
  if (!m_model.current_path.empty()) {
    // Derive today's date
    std::time_t t = std::time(nullptr);
    std::tm *tm_now = std::localtime(&t);
    char buf[12];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", tm_now);

    PomodoroRecord rec;
    rec.date = buf;
    rec.phase = phase;
    rec.duration_sec = elapsed_sec;
    rec.completed = completed;
    m_model.pomodoro_log.push_back(rec);
    m_model.mark_modified();
  }
}

void MainWindow::push_pomodoro_to_sidebar() {
  if (!m_sidebar)
    return;
  if (!m_pomodoro_dialog) {
    m_sidebar->refresh_pomodoro_tile(0.0, m_prefs.pomodoro.focus_min * 60,
                                     false, "Focus", 0,
                                     m_prefs.pomodoro.sessions_before_long);
    update_pomodoro_headerbar();
    return;
  }
  m_sidebar->refresh_pomodoro_tile(
      m_pomodoro_dialog->timer_progress(),
      m_pomodoro_dialog->timer_remaining_sec(),
      m_pomodoro_dialog->timer_running(),
      m_pomodoro_dialog->timer_phase_label(),
      m_pomodoro_dialog->timer_session_in_cycle(),
      m_pomodoro_dialog->timer_sessions_before_long());
  update_pomodoro_headerbar();
}

void MainWindow::update_pomodoro_headerbar() {
  // Show pill only when: pref is on AND the dialog exists AND the timer
  // has been started at least once (elapsed > 0 or currently running).
  bool timer_active =
      m_pomodoro_dialog && (m_pomodoro_dialog->timer_running() ||
                            m_pomodoro_dialog->timer_elapsed_sec() > 0);
  bool show = m_prefs.pomodoro.show_in_headerbar && timer_active;

  m_pomo_hdr_box.set_visible(show);
  if (!show)
    return;

  bool running = m_pomodoro_dialog->timer_running();
  std::string full_phase = m_pomodoro_dialog->timer_phase_label();

  // Phase label: shorten long names, prefix ⏸ when paused
  std::string phase_display = full_phase;
  if (phase_display == "Short Break")
    phase_display = "Break";
  if (!running)
    phase_display = "⏸ " + phase_display;
  m_pomo_hdr_phase.set_text(phase_display);

  // Phase colour class
  m_pomo_hdr_box.remove_css_class("pomo-hdr-focus");
  m_pomo_hdr_box.remove_css_class("pomo-hdr-short");
  m_pomo_hdr_box.remove_css_class("pomo-hdr-long");
  m_pomo_hdr_box.remove_css_class("pomo-hdr-paused");
  if (!running) {
    m_pomo_hdr_box.add_css_class("pomo-hdr-paused");
  } else if (full_phase == "Focus") {
    m_pomo_hdr_box.add_css_class("pomo-hdr-focus");
  } else if (full_phase == "Short Break") {
    m_pomo_hdr_box.add_css_class("pomo-hdr-short");
  } else {
    m_pomo_hdr_box.add_css_class("pomo-hdr-long");
  }

  // Time countdown
  int rem = m_pomodoro_dialog->timer_remaining_sec();
  int mm = rem / 60, ss = rem % 60;
  std::ostringstream ts;
  ts << std::setfill('0') << std::setw(2) << mm << ':' << std::setfill('0')
     << std::setw(2) << ss;
  m_pomo_hdr_time.set_text(ts.str());
}

// ─────────────────────────────────────────────────────────────────────────────
// Auto-save
// ─────────────────────────────────────────────────────────────────────────────

void MainWindow::start_autosave_timer() {
  stop_autosave_timer();
  if (!m_prefs.auto_save || m_prefs.auto_save_interval_min < 1)
    return;

  m_autosave_secs_left = m_prefs.auto_save_interval_min * 60;
  m_autosave_warning_shown = false;

  // 1-second tick so we can show a live countdown warning
  m_autosave_conn = Glib::signal_timeout().connect(
      [this]() -> bool {
        // User disabled autosave mid-session — cancel
        if (!m_prefs.auto_save) {
          if (m_editor)
            m_editor->show_toast("");
          return false;
        }

        --m_autosave_secs_left;

        // Backup manager ticks every second independently of autosave state
        m_backup_manager.tick(1);

        // If there's nothing to save right now, keep the counter ticking but
        // suppress the warning toast. This lets the interval run continuously
        // whether or not the file is dirty — when dirty work accumulates it
        // will be caught by whichever tick fires while is_modified is true.
        bool can_save = !m_model.current_path.empty() && m_model.is_modified;

        if (m_autosave_secs_left > 0) {
          // Show/update countdown warning in the last AUTOSAVE_WARN_SECS
          // seconds only when there's actually something to save
          if (can_save && m_autosave_secs_left <= AUTOSAVE_WARN_SECS) {
            m_autosave_warning_shown = true;
            int s = m_autosave_secs_left;
            std::string msg = "Auto-saving in " + std::to_string(s) +
                              (s == 1 ? " second…" : " seconds…");
            if (m_editor)
              m_editor->show_toast(msg);
          }
          return true;
        }

        // Counter reached zero
        if (can_save)
          do_autosave();

        // Reset for next cycle regardless
        m_autosave_secs_left = m_prefs.auto_save_interval_min * 60;
        m_autosave_warning_shown = false;
        return true; // keep ticking
      },
      1000);
}

void MainWindow::stop_autosave_timer() {
  if (m_autosave_conn.connected())
    m_autosave_conn.disconnect();
  m_autosave_secs_left = 0;
  m_autosave_warning_shown = false;
}

void MainWindow::do_autosave() {
  if (m_model.current_path.empty() || !m_model.is_modified)
    return;
  try {
    snapshot_open_tabs();
    m_model.save();
    update_title_bar();
    if (m_editor)
      m_editor->show_toast("Auto-saved ✓");
  } catch (...) {
    if (m_editor)
      m_editor->show_toast("Auto-save failed");
  }
}

void MainWindow::do_backup_on_close() {
  // The project file has already been saved by the close handler before
  // this is called — we just need to compress and rotate.
  m_backup_manager.on_close();
}

} // namespace Folio
