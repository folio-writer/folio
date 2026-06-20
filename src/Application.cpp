#include "Application.hpp"
#include "MainWindow.hpp"
#include "FolioLog.hpp"
#include <glibmm/fileutils.h>
#include <giomm/resource.h>
#include <gtkmm/icontheme.h>
#include <gdkmm/display.h>

// The resource bundle is generated as C (src/folio-resources.c) and exports
// this with C linkage. We forward-declare it with C linkage rather than
// #include the generated folio-resources.h, because that header pulls in
// <gio/gio.h>; wrapping it in extern "C" would (wrongly) give C linkage to
// the C++ template machinery gio transitively includes.
extern "C" GResource *resources_get_resource(void);

namespace Folio {

DocumentModel* Application::s_document = nullptr;

DocumentModel& Application::document() {
  // The owner sets s_document in its ctor before any window or lens exists, so
  // a null here means a caller reached the channel before the app was built —
  // a programming error, not a runtime condition. Deref directly; a crash names
  // the bug rather than masking it behind a silent fallback.
  return *s_document;
}

Glib::RefPtr<Application> Application::create() {
  return Glib::make_refptr_for_instance<Application>(new Application());
}

Application::Application()
    : Gtk::Application("com.folio.writingstudio",
                       Gio::Application::Flags::NONE) {
  s_document = &m_model;   // owner sets the one channel; borrowers read
  m_prefs.load();
  m_model.reset();
}

void Application::on_activate() {
  // Register compiled-in GLib resources (icons) into the process resource set,
  // then point the default icon theme at our gresource prefix. After this,
  // set_icon_name("folio-foo-symbolic") resolves
  //   /com/folio/app/icons/scalable/apps/folio-foo-symbolic.svg
  // straight out of the binary — works installed, moved, or in a sandbox, with
  // no /proc/self/exe directory-walking. Adding an icon = drop the .svg in
  // resources/icons/ + a line in resources.xml. (Prefix must match
  // resources.xml; GTK's lookup requires the .../scalable/apps tail.)
  g_resources_register(resources_get_resource());
  auto icon_theme = Gtk::IconTheme::get_for_display(Gdk::Display::get_default());
  icon_theme->add_resource_path("/com/folio/app/icons");
  Gtk::Window::set_default_icon_name("folio-brand-symbolic");
  LOG_INFO("Registered gresource icons; theme has folio-binder-symbolic: {}",
           icon_theme->has_icon("folio-binder-symbolic"));

  auto *win = new MainWindow(m_model, m_prefs);
  add_window(*win);
  win->set_visible(true);

  // Reopen last file after the window is fully constructed and wired,
  // so open_path can populate the editor, inspector, and project pane.
  if (m_prefs.reopen_last_file && !m_prefs.recent_files.empty()) {
    std::string last = m_prefs.recent_files.front();  // copy, not reference
    if (Glib::file_test(last, Glib::FileTest::EXISTS)) {
      win->open_path(last);
    }
  }
}

} // namespace Folio