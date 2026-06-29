#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — ImportDialog.hpp
// ─────────────────────────────────────────────────────────────────────────────
#include "DocumentModel.hpp"
#include "Importer.hpp"
#include <functional>
#include <gtkmm.h>

namespace Folio {

class ImportDialog : public Gtk::Window {
public:
    // Called after a successful import with the nodes to insert.
    // Section is always Manuscript for now (could be extended).
    using ImportCallback = std::function<void(std::vector<ImportNode>)>;

    ImportDialog(Gtk::Window& parent, DocumentModel& model);

    void set_import_callback(ImportCallback cb) { m_on_import = std::move(cb); }

private:
    [[maybe_unused]] DocumentModel&  m_model;
    ImportCallback  m_on_import;

    // ── Layout ────────────────────────────────────────────────────────────────
    Gtk::Box            m_root{Gtk::Orientation::VERTICAL};
    Gtk::Box            m_body{Gtk::Orientation::HORIZONTAL, 0};
    Gtk::Box            m_footer{Gtk::Orientation::HORIZONTAL, 8};

    // ── Left: file / folder chooser area ─────────────────────────────────────
    Gtk::Box            m_left{Gtk::Orientation::VERTICAL, 0};
    Gtk::Box            m_pick_bar{Gtk::Orientation::HORIZONTAL, 6};
    Gtk::Button         m_btn_add_files;
    Gtk::Button         m_btn_add_folder;
    Gtk::Button         m_btn_clear;
    Gtk::ScrolledWindow m_file_scroll;
    Gtk::Box            m_file_list{Gtk::Orientation::VERTICAL, 0};

    // Paths queued for import
    struct FileEntry {
        std::string  path;
        bool         is_folder = false;
        Gtk::Widget* row       = nullptr;
    };
    std::vector<FileEntry> m_entries;

    void add_file_row(const std::string& path, bool is_folder);
    void remove_entry(size_t idx);
    void refresh_file_list();
    void pick_files();
    void pick_folder();

    // ── Right: options ────────────────────────────────────────────────────────
    Gtk::Box            m_right{Gtk::Orientation::VERTICAL, 0};
    Gtk::ScrolledWindow m_opts_scroll;
    Gtk::Box            m_opts_box{Gtk::Orientation::VERTICAL, 14};

    // Separator
    Gtk::Entry          m_sep_entry;

    // Markdown heading hierarchy
    Gtk::CheckButton    m_chk_md_headings;

    // Title source (dropdown: First line / Filename / Sequential)
    Gtk::DropDown*      m_title_dd = nullptr;

    // Folder-as-group
    Gtk::CheckButton    m_chk_folder_group;
    Gtk::CheckButton    m_chk_libreoffice;   // s89 — high-fidelity docx/rtf via LibreOffice

    void build_options();
    Gtk::Widget* make_section(const std::string& title);
    Gtk::Widget* make_row(const std::string& label, Gtk::Widget& w);
    ImportOptions current_opts() const;

    // ── Preview ───────────────────────────────────────────────────────────────
    Gtk::Label          m_preview_lbl;   // summary line above footer

    void update_preview();

    // ── Footer ────────────────────────────────────────────────────────────────
    Gtk::Button         m_btn_import;
    Gtk::Button         m_btn_cancel;
    Gtk::Label          m_status_lbl;

    void build_footer();
    void on_import();
};

} // namespace Folio
