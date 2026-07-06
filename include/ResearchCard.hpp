#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// ResearchCard.hpp — the owned surface for a Research capture (s113).
//
// A Reference whose form is "Research" (template_id == kResearchTemplateId) shows
// this surface in Write mode, mirroring JournalSurface / GallerySurface. It has
// TWO states, toggled by whether the node has been captured yet (url set):
//
//   • UNCAPTURED — a URL entry + "Capture page" button + status line. The user
//     pastes a URL and captures; the card fires the capture callback (host runs
//     monolith + writes assets/<iid>.html), then reloads into the captured state.
//   • CAPTURED — the page Title, the source URL, an EDITABLE Summary (persists to
//     the node's synopsis on edit), and an "Open saved page in browser" button.
//
// The surface is bundle-layout- and engine-IGNORANT: it never shells out and
// never touches disk. It fires callbacks keyed by the node iid; the host
// (Editor) resolves iid -> assets/<iid>.html, runs the capture engine, and
// launches the saved file via Gio::AppInfo::launch_default_for_uri.
// ─────────────────────────────────────────────────────────────────────────────

#include <functional>
#include <string>

#include <gtkmm.h>

namespace Folio {

class ResearchCard : public Gtk::Box {
public:
  ResearchCard();

  // Load a capture into the card. An empty `url` means "not captured yet" ->
  // the capture pane; a non-empty `url` -> the display pane. `summary` seeds the
  // editable field (echoed back through persist on edit).
  void load(const std::string& iid, const std::string& title,
            const std::string& url, const std::string& summary);
  void clear();

  // Rename nudge (the card owns its title label).
  void set_title(const std::string& title);

  // The host reports a failed capture: re-enable the capture pane + show why.
  void capture_failed(const std::string& message);

  // Summary edits (captured state), keyed by the loaded iid.
  using PersistCallback =
      std::function<void(const std::string& iid, const std::string& summary)>;
  void set_persist_callback(PersistCallback cb) { m_on_persist = std::move(cb); }

  // "Open saved page" (captured state) fires this with the loaded iid.
  using OpenCallback = std::function<void(const std::string& iid)>;
  void set_open_callback(OpenCallback cb) { m_on_open = std::move(cb); }

  // "Capture page" (uncaptured state) fires this with the loaded iid + the typed
  // URL. The host runs the engine and reloads the card (or calls capture_failed).
  using CaptureCallback =
      std::function<void(const std::string& iid, const std::string& url)>;
  void set_capture_callback(CaptureCallback cb) { m_on_capture = std::move(cb); }

private:
  void show_captured(bool captured);
  void on_capture_clicked();
  void on_summary_changed();
  void on_open_clicked();

  std::string m_iid;
  bool        m_loading = false;   // guards programmatic fills from persisting

  // Uncaptured pane.
  Gtk::Box*   m_capture_pane = nullptr;   // managed
  Gtk::Entry  m_url_entry;
  Gtk::Button m_capture_btn;
  Gtk::Label  m_capture_status;

  // Captured pane.
  Gtk::Box*     m_display_pane = nullptr;  // managed
  Gtk::Label    m_title_label;
  Gtk::Label    m_url_label;
  Gtk::Label    m_summary_heading;
  Gtk::TextView m_summary_view;
  Gtk::Button   m_open_btn;

  PersistCallback m_on_persist;
  OpenCallback    m_on_open;
  CaptureCallback m_on_capture;
};

} // namespace Folio
