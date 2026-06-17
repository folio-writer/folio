// ─────────────────────────────────────────────────────────────────────────────
// Folio — AnnotationReportDialog.hpp
// Read-only report of all annotations across the entire project.
// Groups by scene, shows: kind badge | quoted excerpt | comment | date
// ─────────────────────────────────────────────────────────────────────────────
#pragma once

#include "DocumentModel.hpp"
#include <gtkmm/window.h>
#include <gtkmm/box.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/label.h>
#include <gtkmm/separator.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/stringlist.h>
#include <gtkmm/button.h>
#include <gtkmm/searchentry.h>

namespace Folio {

class AnnotationReportDialog : public Gtk::Window {
public:
    explicit AnnotationReportDialog(Gtk::Window& parent, DocumentModel& model);
    void refresh();  // rebuild after annotations change

private:
    void build();
    void rebuild_list();
    std::string excerpt_from(BinderNode* node, int start, int end) const;

    DocumentModel& m_model;

    Gtk::Box            m_vbox;
    Gtk::Box            m_toolbar;
    Gtk::SearchEntry    m_search;
    Gtk::DropDown*      m_filter_kind  = nullptr;
    Gtk::DropDown*      m_sort_dd      = nullptr;
    Gtk::Button*        m_export_btn   = nullptr;
    Gtk::ScrolledWindow m_scroll;
    Gtk::Box            m_list;        // vertical list of cards
};

} // namespace Folio
