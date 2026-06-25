#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// JournalSurface.hpp — the deck-log journal surface (s55 rebuild).
//
// The journal is a ship's deck log (see JournalLog.hpp): an append-only list of
// dated, locked records plus one live DRAFT. This surface renders that model:
//
//   ┌ header: the journal's title ──────────────────────────────────────────┐
//   ├ PINNED DRAFT (fixed, top): a ticking clock + "what's up?" prompt, an    │
//   │   editable text area, and ✓ accept / ✕ delete. First keystroke stamps   │
//   │   the start time (frozen forever); ✓ locks it as record.                │
//   ├ scrolling ACCEPTED cards below, NEWEST-FIRST: DT label + Title + excerpt │
//   │   + ✕ delete. Accepted records are read-only — corrections are new      │
//   │   linked entries, never edits.                                          │
//   └─────────────────────────────────────────────────────────────────────────┘
//
// It owns a JournalLog and persists it (JSON) into the host node's body via the
// callback, keyed by iid — same contract as the MM canvas. The Editor interface
// (load / set_title / set_persist_callback / the two hotkey verbs) is unchanged
// from s54, so only this widget changed: JSON now flows through the persist
// channel where prose-HTML used to.
//
// Deferred to the next slice (navigators, per the v2 mock): the calendar + search
// disclosure row, concept-chip editing, the accent spine, and flair. The pure
// layer already supports them; this slice lands the lifecycle surface.
// ─────────────────────────────────────────────────────────────────────────────

#include <functional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <gtkmm.h>

#include "JournalLog.hpp"

namespace Folio {

class JournalSurface : public Gtk::Box {
public:
  JournalSurface();
  ~JournalSurface() override;

  // Open a journal node: parse its stored body (JSON; legacy prose migrates
  // losslessly), render the draft + accepted records, focus the draft. iid is
  // echoed back through the persist callback. Mirrors CMMCanvas::load_string.
  void load(const std::string &iid, const std::string &title,
            const std::string &body);
  void clear();

  // The journal name in the surface header (kept synced with sidebar renames).
  void set_title(const std::string &title);

  // Hotkey verbs (names kept stable for the Editor's key handler):
  //   stamp_new_entry()  ← Ctrl+J        : accept the draft (the "next entry"
  //                                        gesture — locks it, opens a fresh one)
  //   resume_caret()     ← Ctrl+Shift+J  : focus the draft to write
  void stamp_new_entry();
  void resume_caret();

  // Edits flow back to the node body cell as JSON through this — (iid, json).
  using PersistCallback =
      std::function<void(const std::string &iid, const std::string &html)>;
  void set_persist_callback(PersistCallback cb) { m_on_persist = std::move(cb); }

private:
  std::string m_iid;
  JournalLog m_log;
  bool m_loading = false; // guards programmatic buffer fills from persisting

  // ── header ──
  Gtk::Box m_header{Gtk::Orientation::HORIZONTAL, 8};
  Gtk::Label m_title;
  Gtk::SearchEntry m_search; // scroll-to-match (never hides records)

  // ── calendar disclosure (collapsed by default; a navigator, never hides) ──
  Gtk::Box m_cal_row{Gtk::Orientation::HORIZONTAL, 8};
  Gtk::ToggleButton m_cal_toggle;
  Gtk::Label m_cal_summary;
  Gtk::Revealer m_cal_revealer;
  Gtk::Grid m_cal_grid;
  int m_cal_year = 0;       // displayed month
  int m_cal_month = 0;      // 1..12
  int m_cal_selected = -1;  // the "you are here" day in the displayed month, or -1

  // ── pinned draft (persistent widgets — never rebuilt, so the caret survives) ──
  Gtk::Box m_draft_card{Gtk::Orientation::VERTICAL, 6};
  Gtk::Box m_draft_head{Gtk::Orientation::HORIZONTAL, 8};
  Gtk::Label m_now_badge;
  Gtk::Label m_clock;
  Gtk::Button m_accept_btn;
  Gtk::Button m_draft_del_btn;
  Gtk::ScrolledWindow m_draft_scroll;
  Gtk::TextView m_draft_view;
  Glib::RefPtr<Gtk::TextBuffer> m_draft_buf;
  Gtk::Label m_prompt; // "what's up?" ghost, shown when the draft is empty

  // ── accepted records (scrolling, rebuilt newest-first) ──
  Gtk::ScrolledWindow m_scroller;
  Gtk::Box m_column{Gtk::Orientation::VERTICAL, 12};
  // display order (newest-first): the all()-index and its card widget, so search
  // and (later) the calendar can scroll a specific record into view.
  std::vector<std::pair<std::size_t, Gtk::Widget *>> m_cards;
  int m_match = -1; // cursor into m_cards for the current search hit

  sigc::connection m_tick; // 1-second clock pulse
  PersistCallback m_on_persist;

  // ── internals ──
  void on_draft_changed();
  void do_accept();
  void delete_draft();
  void rebuild_accepted();
  Gtk::Widget *make_accepted_card(std::size_t index);
  void clear_column();
  void update_clock();
  void refresh_draft_state();
  void persist();
  // search: scroll-to-match; reset=true starts from the top, else advances.
  void run_search(bool reset);
  void scroll_card_into_view(Gtk::Widget *card);
  // calendar navigator
  void build_calendar();                 // (re)draw the month grid
  void cal_set_default_month();          // newest entry's month, else today
  void scroll_to_entry_index(std::size_t all_index); // find its card + scroll
};

} // namespace Folio
