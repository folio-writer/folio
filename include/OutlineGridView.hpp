// ─────────────────────────────────────────────────────────────────────────────
// Folio — OutlineGridView.hpp
// Spreadsheet-style metadata grid replacing the old outline view.
// Columns: ● | Title | Status | POV | Words | Target | Include | Synopsis
// Groups appear as full-width section headers.
// Click a column header to select it for batch editing.
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include "DocumentModel.hpp"
#include "FolioPrefs.hpp"
#include <gtkmm/box.h>
#include <gtkmm/grid.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/label.h>
#include <gtkmm/entry.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/button.h>
#include <gtkmm/revealer.h>
#include <gtkmm/separator.h>
#include <functional>
#include <vector>

namespace Folio {

class OutlineGridView : public Gtk::Box {
public:
    explicit OutlineGridView(DocumentModel& model, FolioPrefs& prefs);

    void rebuild();  // called when switching to outline view or model changes

    // Called when any cell edit changes metadata
    std::function<void(BinderNode*)> on_meta_changed;

private:
    // ── Column indices ────────────────────────────────────────────────────────
    enum Col { COL_DOT=0, COL_TITLE, COL_STATUS, COL_POV,
               COL_WORDS, COL_TARGET, COL_INCLUDE, COL_SYNOPSIS,
               COL_COUNT };
    static const char* col_label(Col c);
    static bool col_batchable(Col c); // can this column be batch-set?

    // ── Row record ────────────────────────────────────────────────────────────
    struct RowInfo {
        BinderNode* node   = nullptr;
        int         grid_row = 0;    // row index in m_grid
        bool        is_group = false;
    };

    void build_header_row();
    void append_group_row(const BinderNode& node, int depth, int& grid_row);
    void append_scene_row(BinderNode* node, int depth, int& grid_row);
    Gtk::Widget* make_cell(BinderNode* node, Col col);

    void select_column(Col col);
    void clear_column_selection();
    void apply_batch(Col col);
    void show_batch_bar(Col col);
    void hide_batch_bar();

    DocumentModel& m_model;
    FolioPrefs&    m_prefs;

    // ── Layout ────────────────────────────────────────────────────────────────
    Gtk::Box            m_outer;        // VERTICAL: batch_revealer + scroll
    Gtk::Revealer       m_batch_rev;
    Gtk::Box            m_batch_bar;
    Gtk::ScrolledWindow m_scroll;
    Gtk::Grid           m_grid;

    // Batch bar widgets (recreated per-column)
    Gtk::Label*    m_batch_label   = nullptr;
    Gtk::Widget*   m_batch_widget  = nullptr; // entry/dropdown/check
    Gtk::Button*   m_batch_apply   = nullptr;
    Gtk::Button*   m_batch_cancel  = nullptr;

    // State
    Col  m_selected_col   = COL_COUNT; // COL_COUNT = none
    std::vector<RowInfo> m_rows;
    std::vector<Gtk::Label*> m_col_headers; // for highlight
};

} // namespace Folio
