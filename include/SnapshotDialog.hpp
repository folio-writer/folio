#pragma once
#include <gtkmm.h>
#include <functional>
#include <vector>
#include <string>
#include "DocumentModel.hpp"

namespace Folio {

// ─────────────────────────────────────────────────────────────────────────────
// SnapshotDialog — batch snapshot manager
//
// Uses Gtk::ListBox with native filter/sort functions — no manual widget
// reordering, no segfaults.
// ─────────────────────────────────────────────────────────────────────────────

class SnapshotDialog : public Gtk::Window {
public:
    using SavedCallback = std::function<void()>;

    SnapshotDialog(Gtk::Window& parent, DocumentModel& model);
    void set_saved_callback(SavedCallback cb) { m_on_saved = std::move(cb); }

private:
    // ── Per-row data attached to each ListBoxRow ──────────────────────────────
    struct RowData {
        DocumentModel::NodeRef  ref;
        Gtk::Switch*            sw             = nullptr;
        Gtk::Label*             mod_lbl        = nullptr;
        Gtk::Label*             last_lbl       = nullptr;
        Gtk::Label*             snap_count_lbl = nullptr;
        Gtk::ListBoxRow*        list_row       = nullptr;
    };

    // ── Widgets ───────────────────────────────────────────────────────────────
    Gtk::Box            m_root      { Gtk::Orientation::VERTICAL,   0 };
    Gtk::Box            m_toolbar   { Gtk::Orientation::HORIZONTAL, 8 };
    Gtk::Box            m_sets_bar  { Gtk::Orientation::HORIZONTAL, 8 };
    Gtk::Box            m_filter_bar{ Gtk::Orientation::HORIZONTAL, 8 };
    Gtk::Box            m_header_row{ Gtk::Orientation::HORIZONTAL, 0 };
    Gtk::Overlay        m_overlay;          // wraps m_scroll for toast overlay
    Gtk::ScrolledWindow m_scroll;
    Gtk::Revealer       m_toast_revealer;
    Gtk::Label          m_toast_label;
    sigc::connection    m_toast_timer;
    Gtk::ListBox        m_list;
    Gtk::Box            m_footer    { Gtk::Orientation::HORIZONTAL, 8 };

    // Toolbar
    Gtk::Button         m_btn_modified;
    Gtk::Button         m_btn_all;
    Gtk::Button         m_btn_none;

    // Sets bar
    Gtk::ComboBoxText*  m_set_combo     = nullptr;
    [[maybe_unused]] Gtk::Label*         m_set_count_lbl = nullptr;
    Gtk::Button*        m_del_btn       = nullptr;

    // Filter bar
    Gtk::SearchEntry*   m_search        = nullptr;
    Gtk::ComboBoxText*  m_filter_combo  = nullptr;
    Gtk::ComboBoxText*  m_sort_combo    = nullptr;

    // Footer
    Gtk::Entry          m_name_entry;
    Gtk::Button         m_btn_save;
    Gtk::Label          m_status_lbl;

    // ── State ─────────────────────────────────────────────────────────────────
    DocumentModel&           m_model;
    SavedCallback            m_on_saved;
    std::vector<RowData>     m_rows;

    // Column widths — shared between header and data rows
    static constexpr int W_DOT     = 28;
    static constexpr int W_KIND    = 80;
    static constexpr int W_SECTION = 96;
    static constexpr int W_WORDS   = 52;
    static constexpr int W_SNAPS   = 48;
    static constexpr int W_LAST    = 160;
    static constexpr int W_SWITCH  = 58;
    static constexpr int ROW_H     = 32;

    // ── Build ─────────────────────────────────────────────────────────────────
    void build_toolbar();
    void build_sets_bar();
    void build_filter_bar();
    void build_header_row();
    void build_list();
    void build_footer();

    Gtk::ListBoxRow* make_list_row(RowData& rd);

    // ── Filter / Sort (called by ListBox callbacks) ───────────────────────────
    bool   row_filter (Gtk::ListBoxRow* row);
    int    row_sort   (Gtk::ListBoxRow* a, Gtk::ListBoxRow* b);
    void   invalidate ();          // re-run filter+sort

    // ── Actions ───────────────────────────────────────────────────────────────
    void select_modified();
    void select_all();
    void select_none();
    void save_snapshots();
    void delete_snapshot_set(const std::string& name);
    void refresh_sets_bar();
    void update_status();
    void show_toast(const std::string& message);

    // ── Helpers ───────────────────────────────────────────────────────────────
    static RowData*    row_data_of(Gtk::ListBoxRow* r);
    static std::string kind_label(BinderKind k);
    static std::string section_label(Section s);
    static std::string last_snapshot_str(const BinderNode& n);
};

} // namespace Folio
