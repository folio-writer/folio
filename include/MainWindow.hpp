#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — MainWindow.hpp
// ─────────────────────────────────────────────────────────────────────────────
#include "DocumentModel.hpp"
#include "FolioPrefs.hpp"
#include "BackupManager.hpp"
#include "PreferencesDialog.hpp"
#include "SnapshotDialog.hpp"
#include "ExportDialog.hpp"
#include "ImportDialog.hpp"
#include "PatternDialog.hpp"   // s24 — Layer 3: pattern input dialog
#include "PrintDialog.hpp"
#include "ReportEngine.hpp"
#include "SearchDialog.hpp"
#include "EditorTabBar.hpp"
#include "PomodoroDialog.hpp"
#include "FocusWindow.hpp"
#include <giomm/settings.h>
#include <gtkmm.h>

namespace Folio {

class Sidebar;
class EditorTabBar;
class Editor;
class Inspector;

class MainWindow : public Gtk::ApplicationWindow {
public:
  explicit MainWindow(DocumentModel &model, FolioPrefs &prefs);
  void open_path(std::string path);

protected:
  bool on_close_request() override;

private:
  // ── Model / prefs ─────────────────────────────────────────────────────────
  DocumentModel &m_model;
  FolioPrefs &m_prefs;

  // ── Panels ────────────────────────────────────────────────────────────────
  std::unique_ptr<Sidebar> m_sidebar;
  std::unique_ptr<EditorTabBar> m_timeline;
  std::unique_ptr<Editor> m_editor;
  std::unique_ptr<Inspector> m_inspector;
  std::unique_ptr<FocusWindow> m_focus_window;   // distraction-free window (lazy)

  // ── Layout ────────────────────────────────────────────────────────────────
  Gtk::Paned m_paned_left;  // sidebar | (center+inspector)
  Gtk::Paned m_paned_right; // center  | inspector
  Gtk::Box m_center_box;    // timeline + editor (VERTICAL)

  bool m_show_binder = true;
  bool m_show_inspector = true;
  bool m_restoring_tabs   = false; // load/restore in progress — suppress selection side-effects
  bool m_navigating_link  = false; // suppresses on_tab_activated during navigate_to_link
  bool m_broadcasting     = false; // s20: inside apply_selection's set_active — suppress
                                   // the on_node_changed → apply_selection recursion only
  void apply_layout_state();

  // ── Header bar ────────────────────────────────────────────────────────────
  Gtk::HeaderBar m_headerbar;

  // Layout panel toggles
  Gtk::Box m_layout_toggle_box;
  Gtk::ToggleButton m_btn_binder;
  Gtk::ToggleButton m_btn_inspector_toggle;

  // View mode toggles
  Gtk::Box      m_view_toggle_box;
  Gtk::DropDown m_view_mode_dd;
  bool          m_inhibit_view_dd = false;

  // Title
  Gtk::Box m_title_box;
  Gtk::Label m_title_label;
  Gtk::Label m_subtitle_label;

  // Hamburger menu
  Gtk::MenuButton m_menu_btn;
  Gtk::MenuButton m_tools_btn;
  Glib::RefPtr<Gio::Menu> m_recent_menu;

  void setup_headerbar();
  void rebuild_recent_menu();

  // ── CSS ───────────────────────────────────────────────────────────────────
  Glib::RefPtr<Gtk::CssProvider> m_css_provider;
  Glib::RefPtr<Gio::Settings> m_gsettings;
  bool m_dark_mode = true;
  bool m_applying_theme = false;

  void setup_css();
  void apply_theme(bool dark);

  // ── Setup ─────────────────────────────────────────────────────────────────
  void setup_layout();
  void setup_actions();
  void wire_callbacks();

  // ── Callback handlers ─────────────────────────────────────────────────────
  void on_node_opened(Section section, const std::vector<int>& path);
  void on_node_changed(BinderNode* node);
  void on_tab_activated(const OpenTab &tab);
  void navigate_to_link(const std::string& target_iid, const std::string& anchor_id);
  void on_tab_closed(const OpenTab &tab);
  void on_meta_changed(BinderNode* node);

  // sync_sidebar=false when called from sidebar (it already has correct state)
  // sync_sidebar=true when called from tab click, app restore, etc.
  void apply_selection(std::vector<BoardItem> items, bool sync_sidebar = true);

  // s20: snapshot the live timeline tabs into the model, persisted by stable
  // iid (was an inline positional copy duplicated across every save path).
  void snapshot_open_tabs();

  // ── File operations ───────────────────────────────────────────────────────
  void action_new();
  void action_new_from_pattern();   // s23 — scaffold the manuscript from a module
  void action_open_sample();        // s48 — load the built-in demo project (eval data)
  void action_open();
  void action_save();
  void action_save_as();
  void confirm_discard_async(std::function<void()> then);
  void show_error(const std::string &msg);

  // ── Title bar ─────────────────────────────────────────────────────────────
  void update_title_bar();

  // ── Preferences dialog ────────────────────────────────────────────────────
  void action_preferences();
  void action_export();
  void action_import();
  void action_search();
  void action_print();
  void action_save_report();
  void action_split_node(Section section, const std::vector<int>& path);
  void action_combine_nodes(Section section, std::vector<std::vector<int>> paths);
  void action_convert_node_kind(Section section, const std::vector<int>& path); // s89
  std::unique_ptr<PreferencesDialog> m_prefs_dialog;
  std::vector<std::string> m_tag_ids_before_prefs;  // s81: KP swatch-id order snapshot for remap
  std::unique_ptr<SnapshotDialog>    m_snapshot_dialog;
  std::unique_ptr<ExportDialog>      m_export_dialog;
  std::unique_ptr<ImportDialog>      m_import_dialog;
  std::unique_ptr<PatternDialog>     m_pattern_dialog;   // s24 — Layer 3
  std::unique_ptr<SearchDialog>      m_search_dialog;
  std::unique_ptr<PrintDialog>       m_print_dialog;
  Glib::RefPtr<Gtk::FileDialog>      m_report_file_dialog; // kept alive across async save

  // ── Pomodoro ──────────────────────────────────────────────────────────────
  // Headerbar pill — shown when timer is running and pref is set
  Gtk::Box    m_pomo_hdr_box;       // pill container packed into headerbar
  Gtk::Label  m_pomo_hdr_phase;     // "Focus" / "Break"
  Gtk::Label  m_pomo_hdr_time;      // "24:07"
  std::unique_ptr<PomodoroDialog>    m_pomodoro_dialog;
  void action_pomodoro();
  void ensure_pomodoro_timer();   // create timer + wire callbacks without presenting
  void send_phase_notification(PomodoroPhase finished, PomodoroPhase next);
  void push_pomodoro_to_sidebar();
  void update_pomodoro_headerbar();
  void log_pomodoro_phase(const std::string& phase, int elapsed_sec, bool completed);

  // ── Shortcuts window ──────────────────────────────────────────────────────
  void show_shortcuts_window();

  // ── About dialog ──────────────────────────────────────────────────────────
  void show_about_dialog();

  // True when binder has >1 item selected — suppresses single-item callbacks
  bool m_multi_selected = false;
  bool m_closing        = false;

  // ── Auto-save ─────────────────────────────────────────────────────────────
  sigc::connection m_autosave_conn;     // 1-second tick
  int              m_autosave_secs_left = 0; // countdown in seconds
  bool             m_autosave_warning_shown = false; // toast shown flag
  static constexpr int AUTOSAVE_WARN_SECS = 10; // warn this many secs before save

  void start_autosave_timer();
  void stop_autosave_timer();
  void do_autosave();             // perform save + show toast

  // ── Backup ────────────────────────────────────────────────────────────────
  BackupManager m_backup_manager{m_prefs, m_model};
  void do_backup_on_close();    // call after final save succeeds at close
};

} // namespace Folio
