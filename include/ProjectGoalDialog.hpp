#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — ProjectGoalDialog.hpp
//
// A two-part dialog:
//
//   ┌─ Target Settings ──────────────────────────────────────────────────────┐
//   │  Project word target   [  80,000  ▲▼]                                  │
//   │  Daily session goal    [   1,000  ▲▼]                                  │
//   │  Due date              [ 2025-12-31  📅 ]                              │
//   │  Required words / day  (computed, read-only)                           │
//   └────────────────────────────────────────────────────────────────────────┘
//   ┌─ Progress ─────────────────────────────────────────────────────────────┐
//   │  Cairo burndown/up chart — daily history bars + ideal pace line        │
//   │  On-track / lagging / ahead indicator chip                              │
//   │  Stats row: written · remaining · pace today · days left               │
//   └────────────────────────────────────────────────────────────────────────┘
//
// DailyRecord (words written that day) is stored inside DocumentModel.
// The dialog is instantiated from Inspector::build_project_tab() and kept
// alive in a unique_ptr; shown via present().
// ─────────────────────────────────────────────────────────────────────────────
#include "DocumentModel.hpp"
#include "FolioPrefs.hpp"
#include <gtkmm.h>
#include <string>
#include <vector>

namespace Folio {

// ─────────────────────────────────────────────────────────────────────────────
class ProjectGoalDialog : public Gtk::Window {
public:
    ProjectGoalDialog(Gtk::Window& parent,
                      DocumentModel& model,
                      FolioPrefs& prefs);

    // Call whenever the document word count or session changes so the chart
    // and stats stay live while the dialog is open.
    void refresh();

private:
    DocumentModel& m_model;
    FolioPrefs&    m_prefs;

    // ── Widgets ───────────────────────────────────────────────────────────────
    Gtk::Box          m_root       { Gtk::Orientation::VERTICAL,   0 };
    Gtk::HeaderBar    m_headerbar;

    // ── Settings card ─────────────────────────────────────────────────────────
    Gtk::Box          m_settings_box { Gtk::Orientation::VERTICAL, 0 };
    Gtk::SpinButton   m_spin_target;      // project word target
    Gtk::SpinButton   m_spin_daily;       // daily session goal
    Gtk::Entry        m_date_entry;       // due date YYYY-MM-DD
    Gtk::Label        m_required_lbl;     // computed words/day needed
    Gtk::Label        m_days_left_lbl;    // days remaining

    void build_settings_card();

    // ── Chart ─────────────────────────────────────────────────────────────────
    Gtk::DrawingArea  m_chart;
    void draw_chart(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);

    // ── Status chip + stats row ───────────────────────────────────────────────
    Gtk::Label        m_status_chip;
    Gtk::Label        m_stat_written;
    Gtk::Label        m_stat_remaining;
    Gtk::Label        m_stat_pace;
    Gtk::Label        m_stat_days;
    Gtk::Label        m_stat_today_session;  // session words today
    Gtk::Label        m_stat_total_wc;       // total manuscript word count

    // Pomodoro activity stats
    Gtk::Label        m_stat_pomo_focus_today;   // focus minutes today
    Gtk::Label        m_stat_pomo_sessions_total; // total completed focus sessions
    Gtk::Label        m_stat_pomo_focus_total;   // total focus minutes all time
    Gtk::Label        m_stat_pomo_streak;         // consecutive days with focus sessions

    void build_stats_area();

    // ── Helpers ───────────────────────────────────────────────────────────────
    // Today's ISO date string "YYYY-MM-DD"
    static std::string today_str();
    // Parse "YYYY-MM-DD" → struct tm (zeroed on failure, year==0)
    static std::tm parse_date(const std::string& s);
    // Calendar days from today to due date (negative = overdue)
    int days_until_due() const;
    // Total words written across all daily records (or total_words if no history)
    int words_written_in_history() const;
    // Ideal cumulative words for a given day index (0 = project start)
    double ideal_on_day(int day_idx, int total_days, int target) const;
    // Find or create today's record; return index into m_model.daily_history
    int ensure_today_record();

    // Update the computed labels (required/day, days left)
    void update_computed_labels();
    // Update status chip colour and text
    void update_status_chip();
};

} // namespace Folio
