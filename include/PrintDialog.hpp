#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — PrintDialog.hpp
// ─────────────────────────────────────────────────────────────────────────────
#include "DocumentModel.hpp"
#include "Exporter.hpp"
#include "FolioPrefs.hpp"
#include "ReportEngine.hpp"
#include "SearchEngine.hpp"
#include <gtkmm.h>
#include <vector>

namespace Folio {

class PrintDialog : public Gtk::Window {
public:
    PrintDialog(Gtk::Window& parent, DocumentModel& model, FolioPrefs& prefs);

private:
    DocumentModel& m_model;
    FolioPrefs&    m_prefs;

    // ── Layout ────────────────────────────────────────────────────────────────
    Gtk::Box        m_root{Gtk::Orientation::VERTICAL};
    Gtk::Paned      m_paned{Gtk::Orientation::HORIZONTAL};

    // ── Mode selector (top of left panel) ─────────────────────────────────────
    Gtk::Box         m_mode_bar{Gtk::Orientation::HORIZONTAL, 0};
    Gtk::CheckButton m_mode_manuscript;   // radio: print selected scenes
    Gtk::CheckButton m_mode_report;       // radio: print project report
    void on_mode_changed();

    // ── Scene checklist (left panel) — identical to ExportDialog ─────────────
    Gtk::Box        m_left{Gtk::Orientation::VERTICAL};
    Gtk::Box        m_list_toolbar{Gtk::Orientation::HORIZONTAL, 6};
    Gtk::Button     m_btn_all;
    Gtk::Button     m_btn_none;
    Gtk::Label      m_sel_count_lbl;
    Gtk::ScrolledWindow m_list_scroll;
    Gtk::Box        m_list_box{Gtk::Orientation::VERTICAL, 0};

    struct SceneRow {
        const BinderNode* node     = nullptr;
        bool              is_group = false;
        int               depth    = 0;
        Gtk::CheckButton* check    = nullptr;
    };
    std::vector<SceneRow> m_rows;

    void build_scene_list();
    void add_nodes_recursive(const std::vector<BinderNode>& nodes, int depth);
    void update_sel_count();
    std::vector<Exporter::SourceNode> collect_selected_nodes() const;

    // ── Settings (right panel) ────────────────────────────────────────────────
    Gtk::Box            m_right{Gtk::Orientation::VERTICAL, 0};
    Gtk::ScrolledWindow m_settings_scroll;
    Gtk::Box            m_settings_box{Gtk::Orientation::VERTICAL, 16};

    // Format
    Gtk::CheckButton m_radio_asis;
    Gtk::CheckButton m_radio_normalise;

    // Groups
    Gtk::DropDown*   m_group_heading_dd = nullptr;
    Gtk::Entry       m_sep_entry;

    // Layout
    Gtk::CheckButton m_chk_page_break;   // page break between groups
    Gtk::CheckButton m_chk_header;       // header: title + author
    Gtk::CheckButton m_chk_footer;       // footer: page N of M

    // Paper
    Gtk::DropDown*   m_paper_dd         = nullptr;
    Gtk::DropDown*   m_orientation_dd   = nullptr;

    void build_settings();
    Gtk::Widget* make_section(const std::string& title);
    Gtk::Widget* make_row(const std::string& label, Gtk::Widget& widget);

    // ── Report-mode settings (shown instead of manuscript settings) ────────────
    Gtk::Box         m_report_settings_box{Gtk::Orientation::VERTICAL, 16};
    Gtk::CheckButton m_chk_report_dark;   // dark mode for report

    void build_report_settings();

    // ── Footer ────────────────────────────────────────────────────────────────
    Gtk::Box    m_footer{Gtk::Orientation::HORIZONTAL, 8};
    Gtk::Button m_btn_print;
    Gtk::Button m_btn_cancel;
    Gtk::Label  m_status_lbl;

    void build_footer();
    void on_print();

    // ── Print internals ───────────────────────────────────────────────────────
    struct PagedPara {
        std::string text;
        bool        is_heading = false;
        bool        page_break = false; // force break before this para
        // Chart fields — only used when is_chart == true
        bool        is_chart   = false;
        double      chart_h    = 0.0;  // reserved height in points
        std::vector<DailyRecord> chart_history;
        int         chart_target = 0;  // project word target for target line
    };
    std::vector<std::vector<PagedPara>> m_pages; // [page][para]

    void   paginate(const Glib::RefPtr<Gtk::PrintContext>& ctx,
                    const std::vector<Exporter::SourceNode>& nodes,
                    Gtk::PrintOperation& op);
    void   paginate_report(const Glib::RefPtr<Gtk::PrintContext>& ctx,
                            const ReportData& data,
                            Gtk::PrintOperation& op);
    void   draw_page(const Glib::RefPtr<Gtk::PrintContext>& ctx,
                     int page_nr,
                     const std::string& title,
                     const std::string& author,
                     int total_pages);
};

} // namespace Folio
