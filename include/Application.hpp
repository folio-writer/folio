#pragma once
#include <gtkmm.h>
#include "DocumentModel.hpp"
#include "FolioPrefs.hpp"

namespace Folio {

class Application : public Gtk::Application {
public:
    static Glib::RefPtr<Application> create();

    FolioPrefs& prefs() { return m_prefs; }

    // s22: the one channel to the source of truth (DESIGN §4.7a). The owner sets
    // the handle once at construction; everyone else reads. Folio is project-wide
    // — one project, never two — so this is a plain document(), no active_document()
    // hedging. The macOS NSDocument sibling to prefs() that was missing. Code that
    // does not already hold an injected DocumentModel& (the coming lenses, created
    // lazily) reaches the model here instead of demanding a threaded reference.
    // The pure seams never call this — they are fed their data as parameters.
    static DocumentModel& document();

protected:
    Application();
    void on_activate() override;

private:
    DocumentModel m_model;
    FolioPrefs    m_prefs;

    // Set in the ctor to &m_model; the backing store for document(). Owner sets,
    // borrowers read. Single-project, single-window, single instance.
    static DocumentModel* s_document;
};

} // namespace Folio
