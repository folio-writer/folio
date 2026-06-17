#pragma once
#include <gtkmm.h>
#include "DocumentModel.hpp"
#include "FolioPrefs.hpp"

namespace Folio {

class Application : public Gtk::Application {
public:
    static Glib::RefPtr<Application> create();

    FolioPrefs& prefs() { return m_prefs; }

protected:
    Application();
    void on_activate() override;

private:
    DocumentModel m_model;
    FolioPrefs    m_prefs;
};

} // namespace Folio
