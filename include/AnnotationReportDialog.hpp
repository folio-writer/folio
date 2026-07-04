// ─────────────────────────────────────────────────────────────────────────────
// Folio — AnnotationReportDialog.hpp
// Read-only report of all annotations across the entire project.
// Groups by scene, shows: kind badge | quoted excerpt | comment | date
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include "DocumentModel.hpp"
#include "FolioPrefs.hpp"
#include <gtkmm/window.h>
#include <gtkmm/box.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/label.h>
#include <gtkmm/separator.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/stringlist.h>
#include <gtkmm/button.h>
#include <gtkmm/checkbutton.h>
#include <gtkmm/image.h>
#include <gtkmm/searchentry.h>
#include <functional>

namespace Folio {

class AnnotationReportDialog : public Gtk::Window {
public:
    AnnotationReportDialog(Gtk::Window& parent, DocumentModel& model, FolioPrefs& prefs);
    void refresh();  // rebuild after annotations change

private:
    void build();
    void rebuild_list();
    void rebuild_source_filter();   // s103 — repopulate the source dropdown from current annotations
    std::string excerpt_from(BinderNode* node, int start, int end) const;

    // Text zoom for the card list (the "data") — shared with the editorial ledger
    // via FolioPrefs::data_view_zoom_pct. Applied as a Pango scale attribute over
    // the CSS-resolved sizes (CSS font-size is pinned per class, so a scale attr
    // is the reliable lever).
    void apply_scale();
    void nudge_scale(double delta);

    DocumentModel& m_model;
    FolioPrefs&    m_prefs;
    double         m_scale = 1.0;

    Gtk::Box            m_vbox;
    Gtk::Box            m_toolbar;
    Gtk::SearchEntry    m_search;
    Gtk::DropDown*      m_filter_kind  = nullptr;
    Gtk::DropDown*      m_filter_source = nullptr;   // s103 — "All / Mine / <editor>"
    Gtk::DropDown*      m_filter_verdict = nullptr;  // s107 — "All / Proposed / Accepted / Declined"
    std::vector<std::string> m_sources;              // s103 — distinct sources, parallel to dropdown items 2..N
    Gtk::DropDown*      m_sort_dd      = nullptr;
    Gtk::CheckButton*   m_show_resolved = nullptr;   // s108 §24 — reveal receded (resolved) notes
    Gtk::Button*        m_export_btn   = nullptr;
    Gtk::Button*        m_zoom_out     = nullptr;   // −
    Gtk::Button*        m_zoom_reset   = nullptr;   // "100%" — click resets
    Gtk::Button*        m_zoom_in      = nullptr;   // +
    Gtk::ScrolledWindow m_scroll;
    Gtk::Box            m_list;        // vertical list of cards
};

} // namespace Folio
