// ─────────────────────────────────────────────────────────────────────────────
// ResearchCard.cpp — the owned Research capture surface. See ResearchCard.hpp.
// ─────────────────────────────────────────────────────────────────────────────
#include "ResearchCard.hpp"

namespace Folio {

ResearchCard::ResearchCard() : Gtk::Box(Gtk::Orientation::VERTICAL, 0) {
  set_name("research-card");
  set_hexpand(true);
  set_vexpand(true);
  add_css_class("research-card");

  // A centred column so the card reads as a card, not a full-bleed panel.
  auto* column = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
  column->set_halign(Gtk::Align::CENTER);
  column->set_hexpand(false);
  column->set_size_request(560, -1);
  column->set_margin_top(28);
  column->set_margin_bottom(28);
  column->set_margin_start(24);
  column->set_margin_end(24);

  // ── UNCAPTURED pane: URL entry + Capture button + status ───────────────────
  m_capture_pane = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
  m_capture_pane->set_name("research-capture-pane");

  auto* cap_heading = Gtk::make_managed<Gtk::Label>("Capture a web page");
  cap_heading->add_css_class("paper-title");
  cap_heading->set_halign(Gtk::Align::START);
  cap_heading->set_xalign(0.0f);
  cap_heading->set_wrap(true);
  m_capture_pane->append(*cap_heading);

  auto* cap_hint = Gtk::make_managed<Gtk::Label>(
      "Paste a URL. The page is saved as a single self-contained file inside "
      "this project, so it stays readable offline even if the source goes away.");
  cap_hint->add_css_class("dim-label");
  cap_hint->set_halign(Gtk::Align::START);
  cap_hint->set_xalign(0.0f);
  cap_hint->set_wrap(true);
  m_capture_pane->append(*cap_hint);

  m_url_entry.set_name("research-url-entry");
  m_url_entry.set_placeholder_text("https://example.com/article");
  m_url_entry.set_hexpand(true);
  m_url_entry.signal_activate().connect(
      sigc::mem_fun(*this, &ResearchCard::on_capture_clicked));
  m_capture_pane->append(m_url_entry);

  m_capture_btn.set_name("research-capture-btn");
  m_capture_btn.set_label("Capture page");
  m_capture_btn.add_css_class("suggested-action");
  m_capture_btn.set_halign(Gtk::Align::START);
  m_capture_btn.signal_clicked().connect(
      sigc::mem_fun(*this, &ResearchCard::on_capture_clicked));
  m_capture_pane->append(m_capture_btn);

  m_capture_status.set_name("research-capture-status");
  m_capture_status.add_css_class("dim-label");
  m_capture_status.set_halign(Gtk::Align::START);
  m_capture_status.set_xalign(0.0f);
  m_capture_status.set_wrap(true);
  m_capture_status.set_visible(false);
  m_capture_pane->append(m_capture_status);

  column->append(*m_capture_pane);

  // ── CAPTURED pane: title / source / editable summary / open ────────────────
  m_display_pane = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 12);
  m_display_pane->set_name("research-display-pane");

  m_title_label.set_name("research-card-title");
  m_title_label.add_css_class("paper-title");
  m_title_label.set_halign(Gtk::Align::START);
  m_title_label.set_xalign(0.0f);
  m_title_label.set_wrap(true);
  m_title_label.set_hexpand(true);
  m_display_pane->append(m_title_label);

  m_url_label.set_name("research-card-url");
  m_url_label.add_css_class("dim-label");
  m_url_label.set_halign(Gtk::Align::START);
  m_url_label.set_xalign(0.0f);
  m_url_label.set_wrap(true);
  m_url_label.set_selectable(true);
  m_display_pane->append(m_url_label);

  m_summary_heading.set_name("research-card-summary-heading");
  m_summary_heading.add_css_class("dim-label");
  m_summary_heading.set_halign(Gtk::Align::START);
  m_summary_heading.set_xalign(0.0f);
  m_summary_heading.set_text("Summary");
  m_summary_heading.set_margin_top(8);
  m_display_pane->append(m_summary_heading);

  m_summary_view.set_name("research-card-summary");
  m_summary_view.add_css_class("research-card-summary");
  m_summary_view.set_wrap_mode(Gtk::WrapMode::WORD);
  m_summary_view.set_size_request(-1, 120);
  m_summary_view.set_top_margin(8);
  m_summary_view.set_bottom_margin(8);
  m_summary_view.set_left_margin(8);
  m_summary_view.set_right_margin(8);
  auto* summary_frame = Gtk::make_managed<Gtk::Frame>();
  summary_frame->set_child(m_summary_view);
  m_display_pane->append(*summary_frame);
  if (auto buf = m_summary_view.get_buffer())
    buf->signal_changed().connect(
        sigc::mem_fun(*this, &ResearchCard::on_summary_changed));

  m_open_btn.set_name("research-card-open");
  m_open_btn.set_label("Open saved page in browser");
  m_open_btn.add_css_class("suggested-action");
  m_open_btn.set_halign(Gtk::Align::START);
  m_open_btn.set_margin_top(8);
  m_open_btn.signal_clicked().connect(
      sigc::mem_fun(*this, &ResearchCard::on_open_clicked));
  m_display_pane->append(m_open_btn);

  column->append(*m_display_pane);

  append(*column);
  show_captured(false);
}

void ResearchCard::show_captured(bool captured) {
  if (m_capture_pane) m_capture_pane->set_visible(!captured);
  if (m_display_pane) m_display_pane->set_visible(captured);
}

void ResearchCard::load(const std::string& iid, const std::string& title,
                        const std::string& url, const std::string& summary) {
  m_loading = true;
  m_iid = iid;

  // Uncaptured fields reset.
  m_url_entry.set_text("");
  m_capture_status.set_visible(false);
  m_capture_status.set_text("");
  m_capture_btn.set_sensitive(true);
  m_url_entry.set_sensitive(true);

  // Display fields.
  m_title_label.set_text(title.empty() ? "Untitled capture" : title);
  m_url_label.set_text(url.empty() ? "" : ("Source: " + url));
  m_url_label.set_visible(!url.empty());
  if (auto buf = m_summary_view.get_buffer())
    buf->set_text(summary);

  // A captured node has a source url on record; an empty url = not yet captured.
  show_captured(!url.empty());
  m_loading = false;
}

void ResearchCard::clear() {
  m_loading = true;
  m_iid.clear();
  m_url_entry.set_text("");
  m_capture_status.set_visible(false);
  m_title_label.set_text("");
  m_url_label.set_text("");
  if (auto buf = m_summary_view.get_buffer())
    buf->set_text("");
  show_captured(false);
  m_loading = false;
}

void ResearchCard::set_title(const std::string& title) {
  m_title_label.set_text(title.empty() ? "Untitled capture" : title);
}

void ResearchCard::capture_failed(const std::string& message) {
  m_capture_status.set_text(message);
  m_capture_status.set_visible(true);
  m_capture_btn.set_sensitive(true);
  m_url_entry.set_sensitive(true);
  m_capture_btn.set_label("Capture page");
}

void ResearchCard::on_capture_clicked() {
  if (m_iid.empty() || !m_on_capture)
    return;
  std::string url(m_url_entry.get_text());
  // trim leading/trailing whitespace
  while (!url.empty() && (url.front() == ' ' || url.front() == '\t')) url.erase(url.begin());
  while (!url.empty() && (url.back() == ' ' || url.back() == '\t')) url.pop_back();
  if (url.empty()) {
    m_capture_status.set_text("Enter a URL first.");
    m_capture_status.set_visible(true);
    return;
  }
  // Show a busy state, then fire the capture on a deferred tick so the "Capturing"
  // state paints before the (currently blocking) engine call. iid is captured by
  // value so a mid-flight reload can't retarget it.
  m_capture_status.set_text("Capturing " + url + " …");
  m_capture_status.set_visible(true);
  m_capture_btn.set_sensitive(false);
  m_capture_btn.set_label("Capturing…");
  m_url_entry.set_sensitive(false);
  const std::string iid = m_iid;
  Glib::signal_timeout().connect_once(
      [this, iid, url]() {
        if (m_on_capture) m_on_capture(iid, url);
      },
      30);
}

void ResearchCard::on_summary_changed() {
  if (m_loading || m_iid.empty() || !m_on_persist)
    return;
  std::string text;
  if (auto buf = m_summary_view.get_buffer())
    text = buf->get_text();
  m_on_persist(m_iid, text);
}

void ResearchCard::on_open_clicked() {
  if (m_on_open && !m_iid.empty())
    m_on_open(m_iid);
}

} // namespace Folio
