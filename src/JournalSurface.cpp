// ─────────────────────────────────────────────────────────────────────────────
// JournalSurface.cpp — the deck-log journal surface (s55 rebuild). Plain GTK
// widgets + CSS; no cairo painting or hit-testing. See JournalSurface.hpp.
// ─────────────────────────────────────────────────────────────────────────────

#include "JournalSurface.hpp"

#include "Journal.hpp" // j_first_line / j_starred / j_strip_star / j_year_month / j_weekday

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <string>

namespace Folio {
namespace {

// "YYYY-MM-DDTHH:MM:SS" for right now (local wall clock).
std::string now_iso() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_r(&t, &tm);
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
  return buf;
}

// "TUE JUN 24, 2026 · 10:42:07 PM" (with seconds) for the live, ticking clock.
std::string now_human() {
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_r(&t, &tm);
  char buf[64];
  std::strftime(buf, sizeof(buf), "%a %b %d, %Y \u00b7 %I:%M:%S %p", &tm);
  return buf;
}

// "MON JUN 23, 2026 · 10:05 PM" from an iso stamp; raw iso on any parse trouble.
std::string fmt_stamp(const std::string &iso) {
  int y = 0, mo = 0;
  if (!j_year_month(iso, y, mo) || iso.size() < 10)
    return iso;
  int day = 0;
  try {
    day = std::stoi(iso.substr(8, 2));
  } catch (...) {
    return iso;
  }
  static const char *wd[] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
  static const char *mn[] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                             "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
  char head[64];
  std::snprintf(head, sizeof(head), "%s %s %d, %04d", wd[j_weekday(y, mo, day) % 7],
                mn[(mo - 1) % 12], day, y);
  std::string out = head;
  if (iso.size() >= 16 && iso[10] == 'T') {
    try {
      int hh = std::stoi(iso.substr(11, 2));
      int mm = std::stoi(iso.substr(14, 2));
      const char *ap = hh < 12 ? "AM" : "PM";
      int h12 = hh % 12;
      if (h12 == 0) h12 = 12;
      char tail[32];
      std::snprintf(tail, sizeof(tail), " \u00b7 %d:%02d %s", h12, mm, ap);
      out += tail;
    } catch (...) {
    }
  }
  return out;
}

bool blank(const std::string &s) {
  for (unsigned char c : s)
    if (!std::isspace(c))
      return false;
  return true;
}

// ASCII-lowercased copy, for case-insensitive substring search.
std::string lc(const std::string &s) {
  std::string o = s;
  for (char &c : o)
    c = (char)std::tolower((unsigned char)c);
  return o;
}

// The body with its first (Title) line dropped, whitespace-collapsed: the card
// excerpt. Empty for a title-only entry.
std::string excerpt_of(const std::string &text) {
  std::size_t i = 0;
  bool dropped_title = false;
  std::string out;
  while (i < text.size()) {
    std::size_t nl = text.find('\n', i);
    std::string line =
        text.substr(i, nl == std::string::npos ? std::string::npos : nl - i);
    std::size_t a = line.find_first_not_of(" \t\r");
    std::size_t b = line.find_last_not_of(" \t\r");
    std::string trimmed = (a == std::string::npos) ? "" : line.substr(a, b - a + 1);
    if (!dropped_title) {
      if (!trimmed.empty())
        dropped_title = true; // this was the Title line
    } else if (!trimmed.empty()) {
      if (!out.empty())
        out += ' ';
      out += trimmed;
    }
    if (nl == std::string::npos)
      break;
    i = nl + 1;
  }
  return out;
}

} // namespace

JournalSurface::JournalSurface() : Gtk::Box(Gtk::Orientation::VERTICAL, 0) {
  set_hexpand(true);
  set_vexpand(true);
  add_css_class("journal-surface");

  // ── header: the journal's title + a search field (scroll-to-match) ──
  m_title.add_css_class("paper-title");
  m_title.set_halign(Gtk::Align::START);
  m_title.set_hexpand(true);
  m_title.set_text("Journal");

  m_search.set_placeholder_text("Search this journal");
  m_search.set_halign(Gtk::Align::END);
  m_search.set_size_request(220, -1);
  m_search.signal_search_changed().connect([this]() { run_search(/*reset=*/true); });
  m_search.signal_activate().connect([this]() { run_search(/*reset=*/false); });

  m_header.set_margin_start(16);
  m_header.set_margin_end(16);
  m_header.set_margin_top(12);
  m_header.set_margin_bottom(8);
  m_header.append(m_title);
  m_header.append(m_search);
  append(m_header);

  // ── calendar disclosure ──
  m_cal_toggle.set_label("\u25B8  Calendar"); // ▸ collapsed
  m_cal_toggle.add_css_class("flat");
  m_cal_summary.add_css_class("dim-label");
  m_cal_summary.set_halign(Gtk::Align::START);
  m_cal_summary.set_hexpand(true);
  m_cal_row.set_margin_start(16);
  m_cal_row.set_margin_end(16);
  m_cal_row.set_margin_bottom(6);
  m_cal_row.append(m_cal_toggle);
  m_cal_row.append(m_cal_summary);
  append(m_cal_row);

  m_cal_grid.add_css_class("journal-cal");
  m_cal_grid.set_row_spacing(2);
  m_cal_grid.set_column_spacing(2);
  m_cal_grid.set_column_homogeneous(true);
  m_cal_grid.set_margin_start(16);
  m_cal_grid.set_margin_end(16);
  m_cal_grid.set_margin_bottom(8);
  m_cal_revealer.set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);
  m_cal_revealer.set_transition_duration(120);
  m_cal_revealer.set_reveal_child(false);
  m_cal_revealer.set_child(m_cal_grid);
  append(m_cal_revealer);

  m_cal_toggle.signal_toggled().connect([this]() {
    const bool on = m_cal_toggle.get_active();
    m_cal_toggle.set_label(on ? "\u25BE  Calendar" : "\u25B8  Calendar"); // ▾/▸
    m_cal_revealer.set_reveal_child(on);
    if (on)
      build_calendar();
  });

  // ── pinned draft ──
  m_draft_card.add_css_class("journal-draft");
  m_draft_card.set_margin_start(16);
  m_draft_card.set_margin_end(16);
  m_draft_card.set_margin_bottom(8);

  m_now_badge.set_text("NOW");
  m_now_badge.add_css_class("journal-now-badge");
  m_now_badge.set_valign(Gtk::Align::CENTER);

  m_clock.add_css_class("journal-dt");
  m_clock.set_halign(Gtk::Align::START);
  m_clock.set_hexpand(true);

  m_accept_btn.set_icon_name("folio-accept-symbolic");
  m_accept_btn.add_css_class("flat");
  m_accept_btn.set_tooltip_text("Accept this entry — lock it as record (Ctrl+J)");
  m_accept_btn.signal_clicked().connect(sigc::mem_fun(*this, &JournalSurface::do_accept));

  m_draft_del_btn.set_icon_name("folio-delete-symbolic");
  m_draft_del_btn.add_css_class("flat");
  m_draft_del_btn.set_tooltip_text("Discard this draft");
  m_draft_del_btn.signal_clicked().connect(
      sigc::mem_fun(*this, &JournalSurface::delete_draft));

  m_draft_head.append(m_now_badge);
  m_draft_head.append(m_clock);
  m_draft_head.append(m_accept_btn);
  m_draft_head.append(m_draft_del_btn);
  m_draft_card.append(m_draft_head);

  m_prompt.set_text("what's up?");
  m_prompt.add_css_class("journal-prompt");
  m_prompt.set_halign(Gtk::Align::START);
  m_draft_card.append(m_prompt);

  m_draft_buf = Gtk::TextBuffer::create();
  m_draft_view.set_buffer(m_draft_buf);
  m_draft_view.set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
  m_draft_view.set_top_margin(2);
  m_draft_view.set_left_margin(2);
  m_draft_view.set_right_margin(2);
  m_draft_view.add_css_class("journal-draft-view");
  m_draft_buf->signal_changed().connect(
      sigc::mem_fun(*this, &JournalSurface::on_draft_changed));

  m_draft_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  m_draft_scroll.set_min_content_height(110); // stable draft editor; scrolls within
  m_draft_scroll.set_child(m_draft_view);
  m_draft_card.append(m_draft_scroll);

  append(m_draft_card);

  // ── accepted records (scrolling) ──
  m_column.set_margin_start(16);
  m_column.set_margin_end(16);
  m_column.set_margin_top(4);
  m_column.set_margin_bottom(24);
  m_scroller.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
  m_scroller.set_hexpand(true);
  m_scroller.set_vexpand(true);
  m_scroller.set_child(m_column);
  append(m_scroller);

  // ── the ticking clock ──
  m_tick = Glib::signal_timeout().connect(
      [this]() {
        update_clock();
        return true;
      },
      1000);

  update_clock();
  refresh_draft_state();
}

JournalSurface::~JournalSurface() { m_tick.disconnect(); }

void JournalSurface::set_title(const std::string &title) {
  m_title.set_text(title.empty() ? "Journal" : title);
}

void JournalSurface::load(const std::string &iid, const std::string &title,
                          const std::string &body) {
  m_loading = true;
  m_iid = iid;
  set_title(title);
  m_log = JournalLog::from_json(body);
  m_draft_buf->set_text(m_log.draft().text); // restores an in-progress draft
  m_loading = false;

  cal_set_default_month();
  rebuild_accepted();
  update_clock();
  refresh_draft_state();
  Glib::signal_idle().connect_once([this]() { m_draft_view.grab_focus(); });
}

void JournalSurface::clear() {
  m_loading = true;
  m_iid.clear();
  m_log = JournalLog{};
  m_draft_buf->set_text("");
  m_loading = false;
  rebuild_accepted();
  update_clock();
  refresh_draft_state();
}

void JournalSurface::on_draft_changed() {
  if (m_loading)
    return;
  const std::string text = m_draft_buf->get_text(m_draft_buf->begin(), m_draft_buf->end());
  // First real (non-whitespace) input stamps the start time, once and forever.
  if (!blank(text) && !m_log.draft().committed()) {
    m_log.commit_draft(now_iso());
    update_clock();
  }
  m_log.draft().text = text;
  refresh_draft_state();
  persist();
}

void JournalSurface::do_accept() { // Ctrl+J / the ✓ button
  if (!m_log.draft().committed())
    return; // nothing to accept; the log never locks an empty record
  m_log.draft().text = m_draft_buf->get_text(m_draft_buf->begin(), m_draft_buf->end());
  m_log.accept_draft();
  m_loading = true;
  m_draft_buf->set_text(""); // the fresh draft is blank
  m_loading = false;
  rebuild_accepted();
  update_clock();
  refresh_draft_state();
  persist();
  m_draft_view.grab_focus();
}

void JournalSurface::delete_draft() { // the draft's ✕ — clears, restarts clock
  m_log.soft_delete(m_log.draft_index());
  m_loading = true;
  m_draft_buf->set_text("");
  m_loading = false;
  update_clock();
  refresh_draft_state();
  persist();
  m_draft_view.grab_focus();
}

void JournalSurface::stamp_new_entry() { do_accept(); }      // Ctrl+J
void JournalSurface::resume_caret() { m_draft_view.grab_focus(); } // Ctrl+Shift+J

void JournalSurface::update_clock() {
  if (m_log.draft().committed()) {
    m_now_badge.set_visible(false);
    m_clock.set_text(fmt_stamp(m_log.draft().iso_dt)); // frozen, true start time
  } else {
    m_now_badge.set_visible(true);
    m_clock.set_text(now_human()); // ticking current time, beside the NOW pill
  }
}

void JournalSurface::refresh_draft_state() {
  const bool committed = m_log.draft().committed();
  m_accept_btn.set_sensitive(committed); // only a committed draft can be accepted
  m_prompt.set_visible(blank(m_draft_buf->get_text(m_draft_buf->begin(), m_draft_buf->end())));
}

void JournalSurface::clear_column() {
  m_cards.clear();
  m_match = -1;
  while (Gtk::Widget *c = m_column.get_first_child())
    m_column.remove(*c);
}

void JournalSurface::rebuild_accepted() {
  clear_column();
  for (std::size_t idx : m_log.accepted_view()) { // newest-first, deleted excluded
    Gtk::Widget *card = make_accepted_card(idx);
    m_cards.push_back({idx, card});
    m_column.append(*card);
  }
  if (m_cal_toggle.get_active()) // keep the dots in step with accepts/deletes
    build_calendar();
}

Gtk::Widget *JournalSurface::make_accepted_card(std::size_t index) {
  const JLogEntry &e = m_log.all()[index];

  auto *card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
  card->add_css_class("journal-card");

  // header row: DT label + a hover-revealed delete
  auto *head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
  auto *dt = Gtk::make_managed<Gtk::Label>(fmt_stamp(e.iso_dt));
  dt->add_css_class("journal-dt");
  dt->set_halign(Gtk::Align::START);
  dt->set_hexpand(true);

  auto *del = Gtk::make_managed<Gtk::Button>();
  del->set_icon_name("folio-delete-symbolic");
  del->add_css_class("flat");
  del->add_css_class("journal-card-del");
  del->set_tooltip_text("Delete this entry");
  del->signal_clicked().connect([this, index]() {
    m_log.soft_delete(index); // indices are stable (soft delete / append only)
    rebuild_accepted();
    persist();
  });
  head->append(*dt);
  head->append(*del);
  card->append(*head);

  // Title (bold, first line; a leading ★ marks a favourite)
  const std::string first = j_first_line(e.text);
  std::string title = j_strip_star(first);
  if (title.empty())
    title = "(untitled)";
  auto *title_lbl = Gtk::make_managed<Gtk::Label>(
      (j_starred(first) ? "\u2605  " : "") + title);
  title_lbl->add_css_class("journal-title");
  title_lbl->set_halign(Gtk::Align::START);
  title_lbl->set_wrap(true);
  title_lbl->set_xalign(0.0f);
  card->append(*title_lbl);

  // Excerpt (muted, the body past the Title)
  const std::string ex = excerpt_of(e.text);
  if (!ex.empty()) {
    auto *ex_lbl = Gtk::make_managed<Gtk::Label>(ex);
    ex_lbl->add_css_class("journal-excerpt");
    ex_lbl->set_halign(Gtk::Align::START);
    ex_lbl->set_wrap(true);
    ex_lbl->set_xalign(0.0f);
    ex_lbl->set_lines(2);
    ex_lbl->set_ellipsize(Pango::EllipsizeMode::END);
    card->append(*ex_lbl);
  }
  return card;
}

void JournalSurface::persist() {
  if (m_loading || m_iid.empty() || !m_on_persist)
    return;
  m_on_persist(m_iid, m_log.to_json());
}

void JournalSurface::run_search(bool reset) {
  // Scroll-to-match: never hides a record. Clears the previous flag, finds the
  // next accepted card whose text contains the needle, flags + scrolls to it.
  if (m_match >= 0 && m_match < (int)m_cards.size())
    m_cards[(std::size_t)m_match].second->remove_css_class("journal-match");

  const std::string needle = lc(m_search.get_text());
  if (needle.empty()) {
    m_match = -1;
    return;
  }
  if (m_cards.empty())
    return;

  const int n = (int)m_cards.size();
  const int start = reset ? 0 : (m_match + 1);
  for (int step = 0; step < n; ++step) {
    int i = (start + step) % n; // wrap once through the list
    const JLogEntry &e = m_log.all()[m_cards[(std::size_t)i].first];
    if (lc(e.text).find(needle) != std::string::npos) {
      Gtk::Widget *card = m_cards[(std::size_t)i].second;
      card->add_css_class("journal-match");
      scroll_card_into_view(card);
      m_match = i;
      return;
    }
  }
  m_match = -1; // no match
}

void JournalSurface::scroll_card_into_view(Gtk::Widget *card) {
  auto vadj = m_scroller.get_vadjustment();
  if (!vadj || !card)
    return;
  graphene_rect_t b;
  if (!gtk_widget_compute_bounds(card->gobj(), GTK_WIDGET(m_column.gobj()), &b))
    return;
  const double y = b.origin.y;
  const double h = b.size.height;
  const double page = vadj->get_page_size();
  const double cur = vadj->get_value();
  const double upper = vadj->get_upper();
  if (y < cur)
    vadj->set_value(std::max(0.0, y - 8.0));
  else if (y + h > cur + page)
    vadj->set_value(std::min(y + h - page + 8.0, std::max(0.0, upper - page)));
}

void JournalSurface::scroll_to_entry_index(std::size_t all_index) {
  for (const auto &pr : m_cards)
    if (pr.first == all_index) {
      scroll_card_into_view(pr.second);
      return;
    }
}

void JournalSurface::cal_set_default_month() {
  // Open on the newest accepted record's month; fall back to the current month.
  const auto view = m_log.accepted_view(); // newest-first
  if (!view.empty()) {
    const std::string &iso = m_log.all()[view.front()].iso_dt;
    int y = 0, mo = 0;
    if (j_year_month(iso, y, mo) && iso.size() >= 10) {
      m_cal_year = y;
      m_cal_month = mo;
      try {
        m_cal_selected = std::stoi(iso.substr(8, 2));
      } catch (...) {
        m_cal_selected = -1;
      }
      return;
    }
  }
  std::time_t t = std::time(nullptr);
  std::tm tm{};
  localtime_r(&t, &tm);
  m_cal_year = tm.tm_year + 1900;
  m_cal_month = tm.tm_mon + 1;
  m_cal_selected = -1;
}

void JournalSurface::build_calendar() {
  if (m_cal_year == 0)
    cal_set_default_month();

  // clear the grid
  while (Gtk::Widget *c = m_cal_grid.get_first_child())
    m_cal_grid.remove(*c);

  // which days this month carry entries (committed, not deleted)
  std::set<std::string> entry_days;
  for (const JLogEntry &e : m_log.all())
    if (e.committed() && !e.deleted)
      entry_days.insert(j_date_key(e.iso_dt));

  static const char *mn[] = {"January", "February", "March",     "April",
                             "May",     "June",     "July",      "August",
                             "September", "October", "November", "December"};

  // row 0 — nav: ◀  Month Year  ▶
  auto *prev = Gtk::make_managed<Gtk::Button>();
  prev->set_label("\u25C2");
  prev->add_css_class("flat");
  prev->signal_clicked().connect([this]() {
    if (--m_cal_month < 1) {
      m_cal_month = 12;
      --m_cal_year;
    }
    build_calendar();
  });
  auto *next = Gtk::make_managed<Gtk::Button>();
  next->set_label("\u25B8");
  next->add_css_class("flat");
  next->signal_clicked().connect([this]() {
    if (++m_cal_month > 12) {
      m_cal_month = 1;
      ++m_cal_year;
    }
    build_calendar();
  });
  char hdr[48];
  std::snprintf(hdr, sizeof(hdr), "%s %d", mn[(m_cal_month - 1) % 12], m_cal_year);
  auto *month_lbl = Gtk::make_managed<Gtk::Label>(hdr);
  month_lbl->add_css_class("journal-cal-month");
  month_lbl->set_hexpand(true);
  m_cal_grid.attach(*prev, 0, 0, 1, 1);
  m_cal_grid.attach(*month_lbl, 1, 0, 5, 1);
  m_cal_grid.attach(*next, 6, 0, 1, 1);

  // row 1 — weekday headers, Monday-start (per the v2 mock)
  static const char *wd[] = {"Mo", "Tu", "We", "Th", "Fr", "Sa", "Su"};
  for (int c = 0; c < 7; ++c) {
    auto *h = Gtk::make_managed<Gtk::Label>(wd[c]);
    h->add_css_class("journal-cal-wd");
    m_cal_grid.attach(*h, c, 1, 1, 1);
  }

  // today, for highlighting
  std::time_t t = std::time(nullptr);
  std::tm now{};
  localtime_r(&t, &now);
  const int ty = now.tm_year + 1900, tm_ = now.tm_mon + 1, td = now.tm_mday;

  const int days = j_days_in_month(m_cal_year, m_cal_month);
  // column of day 1: j_weekday 0=Sun..6=Sat -> Monday-start column
  const int first_col = (j_weekday(m_cal_year, m_cal_month, 1) + 6) % 7;
  int grid_row = 2, col = first_col;
  for (int d = 1; d <= days; ++d) {
    char key[16];
    std::snprintf(key, sizeof(key), "%04d-%02d-%02d", m_cal_year, m_cal_month, d);
    const bool has = entry_days.count(key) > 0;

    auto *day = Gtk::make_managed<Gtk::Button>(std::to_string(d));
    day->add_css_class("flat");
    day->add_css_class("journal-cal-day");
    if (has)
      day->add_css_class("has-entry");
    if (m_cal_year == ty && m_cal_month == tm_ && d == td)
      day->add_css_class("today");
    if (d == m_cal_selected)
      day->add_css_class("selected");
    day->set_sensitive(has); // only days with records navigate

    if (has) {
      const std::string kstr = key;
      day->signal_clicked().connect([this, d, kstr]() {
        m_cal_selected = d;
        // first (chronologically earliest) entry on that day
        for (std::size_t k = 0; k < m_log.all().size(); ++k) {
          const JLogEntry &e = m_log.all()[k];
          if (e.committed() && !e.deleted && j_date_key(e.iso_dt) == kstr) {
            scroll_to_entry_index(k);
            break;
          }
        }
        build_calendar(); // move the "you are here" ring
      });
    }

    m_cal_grid.attach(*day, col, grid_row, 1, 1);
    if (++col > 6) {
      col = 0;
      ++grid_row;
    }
  }

  char sum[64];
  std::snprintf(sum, sizeof(sum), "%zu day%s with entries", entry_days.size(),
                entry_days.size() == 1 ? "" : "s");
  m_cal_summary.set_text(sum);
}

} // namespace Folio
