#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — SearchDialog.hpp  (VSCode-style)
// ─────────────────────────────────────────────────────────────────────────────
#include "DocumentModel.hpp"
#include "SearchEngine.hpp"
#include <functional>
#include <gtkmm.h>

namespace Folio {

class SearchDialog : public Gtk::Window {
public:
    using OpenNodeCallback    = std::function<void(Section, std::vector<int>)>;
    using NodeChangedCallback = std::function<void(Section, std::vector<int>, std::string)>;

    SearchDialog(Gtk::Window& parent, DocumentModel& model);
    ~SearchDialog() override;

    void set_open_node_callback(OpenNodeCallback cb)     { m_on_open    = std::move(cb); }
    void set_node_changed_callback(NodeChangedCallback cb){ m_on_changed = std::move(cb); }

    void set_query(const std::string& q);
    void refresh();

private:
    DocumentModel&      m_model;
    OpenNodeCallback    m_on_open;
    NodeChangedCallback m_on_changed;

    std::vector<SearchResult> m_results;
    int                       m_total_matches = 0;

    // ── Root ──────────────────────────────────────────────────────────────────
    Gtk::Box m_root{Gtk::Orientation::VERTICAL, 0};

    // ── Input area ────────────────────────────────────────────────────────────
    Gtk::Entry*        m_query_entry    = nullptr;
    Gtk::Entry*        m_replace_entry  = nullptr;
    Gtk::ToggleButton* m_replace_toggle = nullptr;
    Gtk::Revealer      m_replace_revealer;
    Gtk::Label*        m_count_lbl      = nullptr;

    // Inline field option toggles
    Gtk::ToggleButton* m_case_btn  = nullptr;
    Gtk::ToggleButton* m_word_btn  = nullptr;
    Gtk::ToggleButton* m_regex_btn = nullptr;

    Gtk::Button* m_replace_all_btn = nullptr;

    // ── Scope bar ─────────────────────────────────────────────────────────────
    // ── Scope popover ─────────────────────────────────────────────────────────
    Gtk::MenuButton*   m_scope_btn     = nullptr; // "Scope ▾"
    Gtk::Label*        m_scope_lbl     = nullptr; // summary on the button
    Gtk::Popover*      m_scope_popover = nullptr;

    // Section checkboxes
    Gtk::CheckButton*  m_ck_ms    = nullptr;
    Gtk::CheckButton*  m_ck_ch    = nullptr;
    Gtk::CheckButton*  m_ck_pl    = nullptr;
    Gtk::CheckButton*  m_ck_ref   = nullptr;
    Gtk::CheckButton*  m_ck_tpl   = nullptr;
    Gtk::CheckButton*  m_ck_all_sec = nullptr; // "All sections" master

    // Field checkboxes
    Gtk::CheckButton*  m_ck_title = nullptr;
    Gtk::CheckButton*  m_ck_body  = nullptr;
    Gtk::CheckButton*  m_ck_syn   = nullptr;
    Gtk::CheckButton*  m_ck_notes = nullptr;
    Gtk::CheckButton*  m_ck_desc  = nullptr;
    Gtk::CheckButton*  m_ck_all_fld = nullptr; // "All fields" master

    // ── Results ───────────────────────────────────────────────────────────────
    Gtk::ScrolledWindow m_results_scroll;
    Gtk::Box            m_results_box{Gtk::Orientation::VERTICAL, 0};
    Gtk::Label*         m_empty_lbl = nullptr;

    // Debounce
    sigc::connection m_debounce_conn;

    // ── Build ─────────────────────────────────────────────────────────────────
    void build_ui();
    void build_input_area();
    void build_scope_bar();      // now builds a single "Scope ▾" button
    void build_results_area();
    void update_scope_label();   // refreshes summary text on scope button

    Gtk::ToggleButton* make_field_toggle(const char* label, const char* tip);

    // ── Logic ─────────────────────────────────────────────────────────────────
    SearchOptions  current_opts() const;
    void           schedule_search();
    void           run_search();
    void           populate_results();
    void           do_replace_all();
    Gtk::Widget*   make_result_group(const SearchResult& r, int idx);
};

} // namespace Folio
