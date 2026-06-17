#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — ExportDialog.hpp
// ─────────────────────────────────────────────────────────────────────────────
#include "DocumentModel.hpp"
#include "Exporter.hpp"
#include <gtkmm.h>
#include <memory>
#include <vector>

namespace Folio {

class CompileFormatDialog;   // s18 — PDF format editor (opened from PDF panel)

class ExportDialog : public Gtk::Window {
public:
    ExportDialog(Gtk::Window& parent, DocumentModel& model, FolioPrefs& prefs);
    ~ExportDialog();   // defined in .cpp (unique_ptr to incomplete CompileFormatDialog)

private:
    DocumentModel& m_model;
    FolioPrefs&    m_prefs;

    // ── Layout ────────────────────────────────────────────────────────────────
    Gtk::Box        m_root{Gtk::Orientation::VERTICAL};
    Gtk::Paned      m_paned{Gtk::Orientation::HORIZONTAL};

    // ── Scene checklist (left panel) ──────────────────────────────────────────
    Gtk::Box        m_left{Gtk::Orientation::VERTICAL};
    Gtk::Box        m_list_toolbar{Gtk::Orientation::HORIZONTAL, 6};
    Gtk::Button     m_btn_all;
    Gtk::Button     m_btn_none;
    Gtk::Label      m_sel_count_lbl;
    Gtk::ScrolledWindow m_list_scroll;
    Gtk::Box        m_list_box{Gtk::Orientation::VERTICAL, 0};

    struct SceneRow {
        const BinderNode* node   = nullptr;
        bool              is_group = false;
        int               depth   = 0;
        Gtk::CheckButton* check   = nullptr;
    };
    std::vector<SceneRow> m_rows;

    void build_scene_list();
    void add_nodes_recursive(const std::vector<BinderNode>& nodes, int depth);
    void update_sel_count();

    // ── Settings (right panel) ────────────────────────────────────────────────
    Gtk::Box        m_right{Gtk::Orientation::VERTICAL, 0};
    Gtk::ScrolledWindow m_settings_scroll;
    Gtk::Box        m_settings_box{Gtk::Orientation::VERTICAL, 16};

    // Format dropdown
    Gtk::DropDown*   m_format_dd    = nullptr;
    // Index order must match build_settings: DOCX,EPUB,HTML,Markdown,ODT,RTF,TXT,
    // then PDF appended last (index 7 — appended rather than alphabetical so the
    // existing hardcoded indices in build_opts/update_format_sensitivity don't shift).
    void update_format_sensitivity();

    // Per-format settings live in swappable boxes: the format dropdown selects
    // which property set is shown. m_standard_settings holds the controls the
    // text/markup formats share; m_pdf_settings is PDF's own set. Future formats
    // can grow their own boxes the same way.
    Gtk::Box m_standard_settings{Gtk::Orientation::VERTICAL, 16};
    Gtk::Box m_pdf_settings{Gtk::Orientation::VERTICAL, 16};

    // PDF format picker (Novel / Manuscript / Screenplay + user customs) — index
    // maps to FolioPrefs::all_compile_formats() order (builtins then customs).
    // PDF runs the CompileFormat/paginator path, not ExportOptions, so it ignores
    // the standard controls entirely.
    Gtk::DropDown*   m_pdf_format_dd = nullptr;
    Gtk::Button*     m_pdf_edit_btn  = nullptr;   // opens CompileFormatDialog (s18)
    std::unique_ptr<CompileFormatDialog> m_format_editor;   // owned; reset on close
    // Rebuilds the PDF picker from all_compile_formats(); optionally re-selects by
    // name (used after the editor closes). Stores the chosen name for remembering.
    void rebuild_pdf_formats(const std::string& select_name = "");
    void open_format_editor();

    // Output mode
    Gtk::CheckButton m_radio_combined;
    Gtk::CheckButton m_radio_zip;

    // Groups
    Gtk::DropDown    *m_group_heading_dd   = nullptr; // heading style
    Gtk::DropDown    *m_group_prefix_dd    = nullptr; // Chapter/Part/Book/Section
    Gtk::CheckButton  m_chk_group_content;

    // Separators
    Gtk::Entry       m_sep_entry;
    Gtk::CheckButton m_chk_sep_own_line;
    Gtk::CheckButton m_chk_page_break;

    // Flatten
    Gtk::CheckButton m_chk_flatten;
    Gtk::Box         m_flatten_box{Gtk::Orientation::VERTICAL, 8};
    Gtk::Entry       m_flatten_font_entry;
    Gtk::SpinButton* m_flatten_size_spin   = nullptr;
    Gtk::SpinButton* m_flatten_ls_spin     = nullptr;
    Gtk::SpinButton* m_flatten_margin_spin = nullptr;
    Gtk::SpinButton* m_flatten_para_spin   = nullptr;

    // Cover (EPUB only — shown when cover thumbnail exists)
    Gtk::CheckButton m_chk_cover;

    void build_settings();
    Gtk::Widget* make_section(const std::string& title);
    Gtk::Widget* make_row(const std::string& label, Gtk::Widget& widget);

    // ── Footer ────────────────────────────────────────────────────────────────
    Gtk::Box    m_footer{Gtk::Orientation::HORIZONTAL, 8};
    Gtk::Button m_btn_export;
    Gtk::Button m_btn_cancel;
    Gtk::Label  m_status_lbl;

    void build_footer();
    void on_export();
    void on_export_pdf();   // s17 — PDF runs the CompileFormat/paginator path
    ExportOptions build_opts() const;
    std::vector<Exporter::SourceNode> collect_selected_nodes() const;
};

} // namespace Folio
