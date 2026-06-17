// ─────────────────────────────────────────────────────────────────────────────
// Folio — ProjectGoalDialog.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "ProjectGoalDialog.hpp"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <set>
#include <sstream>

namespace Folio {

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

std::string ProjectGoalDialog::today_str() {
    std::time_t t = std::time(nullptr);
    std::tm* tm_now = std::localtime(&t);
    char buf[12];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", tm_now);
    return buf;
}

std::tm ProjectGoalDialog::parse_date(const std::string& s) {
    std::tm t{};
    if (s.size() < 10) return t;
    std::istringstream ss(s);
    ss >> std::get_time(&t, "%Y-%m-%d");
    if (ss.fail()) t.tm_year = 0;
    return t;
}

int ProjectGoalDialog::days_until_due() const {
    const std::string due = m_model.due_date;
    if (due.empty()) return -1;
    std::tm t_due  = parse_date(due);
    if (t_due.tm_year == 0) return -1;
    std::tm t_now  = parse_date(today_str());
    std::time_t ts_due = std::mktime(&t_due);
    std::time_t ts_now = std::mktime(&t_now);
    double diff = std::difftime(ts_due, ts_now);
    return (int)std::round(diff / 86400.0);
}

int ProjectGoalDialog::words_written_in_history() const {
    int total = 0;
    for (const auto& r : m_model.daily_history) total += r.words;
    return total;
}

double ProjectGoalDialog::ideal_on_day(int day_idx, int total_days, int target) const {
    if (total_days <= 0) return target;
    return target * ((double)day_idx / total_days);
}

int ProjectGoalDialog::ensure_today_record() {
    std::string today = today_str();
    for (int i = 0; i < (int)m_model.daily_history.size(); ++i)
        if (m_model.daily_history[i].date == today) return i;
    m_model.daily_history.push_back({today, 0});
    return (int)m_model.daily_history.size() - 1;
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

ProjectGoalDialog::ProjectGoalDialog(Gtk::Window& parent,
                                     DocumentModel& model,
                                     FolioPrefs& prefs)
    : Gtk::Window()
    , m_model(model)
    , m_prefs(prefs)
{
    set_transient_for(parent);
    set_modal(false);   // non-modal so user can keep writing
    set_default_size(560, 620);
    set_resizable(true);

    // Header bar
    m_headerbar.set_title_widget(*Gtk::make_managed<Gtk::Label>("Project Goal"));
    set_titlebar(m_headerbar);

    // Root scroll
    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroll->set_vexpand(true);

    m_root.set_margin_top(16);
    m_root.set_margin_start(20);
    m_root.set_margin_end(20);
    m_root.set_margin_bottom(20);
    m_root.set_spacing(20);

    build_settings_card();
    build_stats_area();

    // Chart
    m_chart.set_size_request(-1, 200);
    m_chart.set_vexpand(false);
    m_chart.set_draw_func([this](const Cairo::RefPtr<Cairo::Context>& cr, int w, int h){
        draw_chart(cr, w, h);
    });
    m_root.append(m_chart);

    scroll->set_child(m_root);
    set_child(*scroll);

    refresh();
}

// ─────────────────────────────────────────────────────────────────────────────
// Settings card
// ─────────────────────────────────────────────────────────────────────────────

void ProjectGoalDialog::build_settings_card() {
    // Card container
    auto* card = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    card->add_css_class("proj-goal-card");

    // Section title
    auto* title = Gtk::make_managed<Gtk::Label>("Target Settings");
    title->add_css_class("pref-group-title");
    title->set_halign(Gtk::Align::START);
    title->set_margin_bottom(8);
    m_root.append(*title);

    // Listbox with pref-row style
    auto* lb = Gtk::make_managed<Gtk::ListBox>();
    lb->set_selection_mode(Gtk::SelectionMode::NONE);
    lb->add_css_class("pref-listbox");

    // Helper: make a row with label + widget
    auto row = [](const std::string& lbl_text, Gtk::Widget& w) {
        auto* r  = Gtk::make_managed<Gtk::ListBoxRow>();
        auto* rb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        rb->set_margin_start(12); rb->set_margin_end(12);
        rb->set_margin_top(10);  rb->set_margin_bottom(10);
        auto* l = Gtk::make_managed<Gtk::Label>(lbl_text);
        l->add_css_class("pref-row-label");
        l->set_hexpand(true); l->set_halign(Gtk::Align::START);
        w.set_halign(Gtk::Align::END);
        rb->append(*l); rb->append(w);
        r->set_child(*rb); return r;
    };

    // Project word target
    m_spin_target.set_range(0, 2000000);
    m_spin_target.set_increments(1000, 10000);
    m_spin_target.set_value(m_model.project_word_target > 0
                             ? m_model.project_word_target : 80000);
    m_spin_target.set_digits(0);
    m_spin_target.set_size_request(120, -1);
    m_spin_target.signal_value_changed().connect([this] {
        m_model.project_word_target = (int)m_spin_target.get_value();
        m_model.mark_modified();
        update_computed_labels();
        update_status_chip();
        m_chart.queue_draw();
    });
    lb->append(*row("Project word target", m_spin_target));

    // Daily goal
    m_spin_daily.set_range(0, 10000);
    m_spin_daily.set_increments(100, 500);
    m_spin_daily.set_value(m_prefs.daily_word_goal > 0
                            ? m_prefs.daily_word_goal : 1000);
    m_spin_daily.set_digits(0);
    m_spin_daily.set_size_request(120, -1);
    m_spin_daily.signal_value_changed().connect([this] {
        m_prefs.daily_word_goal = (int)m_spin_daily.get_value();
        m_model.daily_target    = m_prefs.daily_word_goal;
        m_model.mark_modified();
        try { m_prefs.save(); } catch (...) {}
        update_computed_labels();
        update_status_chip();
        m_chart.queue_draw();
    });
    lb->append(*row("Daily session goal (words)", m_spin_daily));

    // Due date entry
    m_date_entry.set_placeholder_text("YYYY-MM-DD  (e.g. 2025-12-31)");
    m_date_entry.set_text(m_model.due_date);
    m_date_entry.set_size_request(160, -1);
    m_date_entry.signal_changed().connect([this] {
        m_model.due_date = std::string(m_date_entry.get_text());
        m_model.mark_modified();
        update_computed_labels();
        update_status_chip();
        m_chart.queue_draw();
    });
    lb->append(*row("Due date", m_date_entry));

    // Required words/day (computed, read-only)
    auto* req_row = Gtk::make_managed<Gtk::ListBoxRow>();
    {
        auto* rb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        rb->set_margin_start(12); rb->set_margin_end(12);
        rb->set_margin_top(10);  rb->set_margin_bottom(10);
        auto* l = Gtk::make_managed<Gtk::Label>("Required pace (words/day)");
        l->add_css_class("pref-row-label");
        l->set_hexpand(true); l->set_halign(Gtk::Align::START);
        m_required_lbl.add_css_class("proj-computed-lbl");
        m_required_lbl.set_halign(Gtk::Align::END);
        rb->append(*l); rb->append(m_required_lbl);
        req_row->set_child(*rb);
    }
    lb->append(*req_row);

    // Days left (computed)
    auto* days_row = Gtk::make_managed<Gtk::ListBoxRow>();
    {
        auto* rb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        rb->set_margin_start(12); rb->set_margin_end(12);
        rb->set_margin_top(10);  rb->set_margin_bottom(10);
        auto* l = Gtk::make_managed<Gtk::Label>("Days remaining");
        l->add_css_class("pref-row-label");
        l->set_hexpand(true); l->set_halign(Gtk::Align::START);
        m_days_left_lbl.add_css_class("proj-computed-lbl");
        m_days_left_lbl.set_halign(Gtk::Align::END);
        rb->append(*l); rb->append(m_days_left_lbl);
        days_row->set_child(*rb);
    }
    lb->append(*days_row);

    m_root.append(*lb);
}

// ─────────────────────────────────────────────────────────────────────────────
// Stats area (status chip + stat tiles)
// ─────────────────────────────────────────────────────────────────────────────

void ProjectGoalDialog::build_stats_area() {
    // Status chip row
    auto* chip_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    chip_row->set_halign(Gtk::Align::START);
    m_status_chip.add_css_class("badge-chip");
    m_status_chip.set_text("—");
    chip_row->append(m_status_chip);

    auto* prog_title = Gtk::make_managed<Gtk::Label>("Progress");
    prog_title->add_css_class("pref-group-title");
    prog_title->set_halign(Gtk::Align::START);
    prog_title->set_hexpand(true);

    auto* title_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    title_row->append(*prog_title);
    title_row->append(*chip_row);
    m_root.append(*title_row);

    // Stat tiles grid (2 × 4)
    auto* grid = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    grid->set_homogeneous(true);

    auto make_tile = [](const std::string& label_text, Gtk::Label& value_lbl) {
        auto* tile = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        tile->add_css_class("proj-stat-tile");
        auto* vl = Gtk::make_managed<Gtk::Label>(label_text);
        vl->add_css_class("proj-stat-tile-label");
        vl->set_halign(Gtk::Align::START);
        value_lbl.add_css_class("proj-stat-tile-value");
        value_lbl.set_halign(Gtk::Align::START);
        tile->append(*vl);
        tile->append(value_lbl);
        return tile;
    };

    grid->append(*make_tile("Written",          m_stat_written));
    grid->append(*make_tile("Remaining",        m_stat_remaining));
    grid->append(*make_tile("Today (session)",  m_stat_today_session));
    grid->append(*make_tile("Manuscript total", m_stat_total_wc));

    m_root.append(*grid);

    // Second row of stat tiles
    auto* grid2 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    grid2->set_homogeneous(true);
    grid2->append(*make_tile("Daily pace needed", m_stat_pace));
    grid2->append(*make_tile("Days left",         m_stat_days));
    m_root.append(*grid2);

    // Chart title
    auto* chart_title = Gtk::make_managed<Gtk::Label>("Burnup Chart");
    chart_title->add_css_class("pref-group-title");
    chart_title->set_halign(Gtk::Align::START);
    m_root.append(*chart_title);

    // ── Pomodoro activity stats ───────────────────────────────────────────────
    auto* pomo_title = Gtk::make_managed<Gtk::Label>("Pomodoro Activity");
    pomo_title->add_css_class("pref-group-title");
    pomo_title->set_halign(Gtk::Align::START);
    pomo_title->set_margin_top(8);
    m_root.append(*pomo_title);

    auto* pomo_grid = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    pomo_grid->set_homogeneous(true);
    pomo_grid->append(*make_tile("Focus today",      m_stat_pomo_focus_today));
    pomo_grid->append(*make_tile("Sessions (total)", m_stat_pomo_sessions_total));
    pomo_grid->append(*make_tile("Focus time (total)", m_stat_pomo_focus_total));
    pomo_grid->append(*make_tile("Focus streak",     m_stat_pomo_streak));
    m_root.append(*pomo_grid);
}

// ─────────────────────────────────────────────────────────────────────────────
// refresh() — updates all labels and redraws chart
// ─────────────────────────────────────────────────────────────────────────────

void ProjectGoalDialog::refresh() {
    // Sync today's record with current session words
    int today_idx = ensure_today_record();
    // Update today's word count to the current session
    // We store total manuscript words for today's record, not just session delta,
    // so the chart shows absolute cumulative progress.
    // For simplicity, today's entry = max of what's already there and session words.
    int sess = m_model.session_words;
    if (m_model.daily_history[today_idx].words < sess)
        m_model.daily_history[today_idx].words = sess;

    update_computed_labels();
    update_status_chip();

    int written   = words_written_in_history();
    int target    = m_model.project_word_target;
    int remaining = std::max(0, target - written);
    int total_wc  = m_model.total_words();
    int days      = days_until_due();

    m_stat_written.set_text(std::to_string(written));
    m_stat_remaining.set_text(std::to_string(remaining));
    m_stat_today_session.set_text(std::to_string(sess) + " today");
    m_stat_total_wc.set_text(std::to_string(total_wc) + " words");

    // Pace needed
    if (days > 0 && remaining > 0) {
        int needed = (int)std::ceil((double)remaining / days);
        m_stat_pace.set_text(std::to_string(needed) + " / day");
    } else if (remaining == 0) {
        m_stat_pace.set_text("Done! 🎉");
    } else {
        m_stat_pace.set_text("—");
    }

    m_stat_days.set_text(days >= 0 ? std::to_string(days) + " days" : "overdue");

    // ── Pomodoro activity stats ───────────────────────────────────────────────
    {
        std::string today = today_str();

        int focus_sec_today    = 0;
        int focus_sessions_all = 0;
        int focus_sec_all      = 0;

        // Collect all dates that had at least one completed focus session
        // to compute the streak (consecutive days ending today).
        std::set<std::string> focus_days;

        for (const auto& r : m_model.pomodoro_log) {
            if (r.phase == "Focus" && r.completed) {
                focus_sessions_all++;
                focus_sec_all += r.duration_sec;
                if (r.date == today)
                    focus_sec_today += r.duration_sec;
                focus_days.insert(r.date);
            }
        }

        // Focus today — show as "Xm" or "Xh Ym"
        auto fmt_mins = [](int secs) -> std::string {
            int m = secs / 60;
            if (m >= 60) {
                int h = m / 60; int rm = m % 60;
                return std::to_string(h) + "h " + std::to_string(rm) + "m";
            }
            return std::to_string(m) + "m";
        };
        m_stat_pomo_focus_today.set_text(
            focus_sec_today > 0 ? fmt_mins(focus_sec_today) : "—");
        m_stat_pomo_sessions_total.set_text(std::to_string(focus_sessions_all));
        m_stat_pomo_focus_total.set_text(
            focus_sec_all > 0 ? fmt_mins(focus_sec_all) : "—");

        // Streak: count consecutive calendar days ending on today (or yesterday)
        int streak = 0;
        if (!focus_days.empty()) {
            // Walk backwards from today
            std::time_t ts = std::time(nullptr);
            for (int d = 0; d < 365; ++d) {
                std::tm* tm_d = std::localtime(&ts);
                char buf[12];
                std::strftime(buf, sizeof(buf), "%Y-%m-%d", tm_d);
                if (focus_days.count(std::string(buf))) {
                    ++streak;
                    ts -= 86400; // step back one day
                } else if (d == 0) {
                    // No focus today yet — check yesterday as streak base
                    ts -= 86400;
                    continue;
                } else {
                    break;
                }
            }
        }
        m_stat_pomo_streak.set_text(streak > 0
            ? std::to_string(streak) + (streak == 1 ? " day" : " days")
            : "—");
    }

    m_chart.queue_draw();
}

void ProjectGoalDialog::update_computed_labels() {
    int target  = m_model.project_word_target;
    int written = words_written_in_history();
    int days    = days_until_due();
    int remaining = std::max(0, target - written);

    if (days > 0 && remaining > 0) {
        int req = (int)std::ceil((double)remaining / days);
        m_required_lbl.set_text(std::to_string(req) + " words/day");
    } else if (remaining == 0) {
        m_required_lbl.set_text("Target reached ✓");
    } else if (days == 0) {
        m_required_lbl.set_text("Due today!");
    } else if (days < 0) {
        m_required_lbl.set_text("Overdue");
    } else {
        m_required_lbl.set_text("—");
    }

    m_days_left_lbl.set_text(days >= 0 ? std::to_string(days) + " days" : "overdue");
}

void ProjectGoalDialog::update_status_chip() {
    int target  = m_model.project_word_target;
    int written = words_written_in_history();
    int days    = days_until_due();

    // Remove all status CSS classes first
    m_status_chip.remove_css_class("proj-chip-ahead");
    m_status_chip.remove_css_class("proj-chip-on-track");
    m_status_chip.remove_css_class("proj-chip-lagging");
    m_status_chip.remove_css_class("proj-chip-done");
    m_status_chip.remove_css_class("proj-chip-overdue");

    if (target <= 0) {
        m_status_chip.set_text("No target set");
        return;
    }
    if (written >= target) {
        m_status_chip.set_text("✓ Target reached");
        m_status_chip.add_css_class("proj-chip-done");
        return;
    }
    if (days < 0) {
        m_status_chip.set_text("⚠ Overdue");
        m_status_chip.add_css_class("proj-chip-overdue");
        return;
    }
    if (days == 0) {
        m_status_chip.set_text("⚡ Due today");
        m_status_chip.add_css_class("proj-chip-lagging");
        return;
    }

    // Total project days (rough estimate: from day 1 of history to due date)
    int total_days = (int)m_model.daily_history.size() + days;
    if (total_days < 1) total_days = 1;

    // Ideal cumulative words at this point in time
    int elapsed = total_days - days;
    double ideal = ideal_on_day(elapsed, total_days, target);

    double ratio = (ideal > 0) ? (double)written / ideal : 1.0;

    if (ratio >= 1.1) {
        m_status_chip.set_text("↑ Ahead of pace");
        m_status_chip.add_css_class("proj-chip-ahead");
    } else if (ratio >= 0.9) {
        m_status_chip.set_text("◎ On track");
        m_status_chip.add_css_class("proj-chip-on-track");
    } else if (ratio >= 0.6) {
        m_status_chip.set_text("↓ Slightly behind");
        m_status_chip.add_css_class("proj-chip-lagging");
    } else {
        m_status_chip.set_text("⚠ Lagging");
        m_status_chip.add_css_class("proj-chip-overdue");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Chart drawing
// ─────────────────────────────────────────────────────────────────────────────

void ProjectGoalDialog::draw_chart(const Cairo::RefPtr<Cairo::Context>& cr,
                                    int w, int h)
{
    // Margins
    const double ml = 52, mr = 16, mt = 12, mb = 36;
    double cw = w - ml - mr;
    double ch = h - mt - mb;
    if (cw <= 0 || ch <= 0) return;

    const auto& history = m_model.daily_history;
    int n = (int)history.size();
    int target = m_model.project_word_target;
    int days_left = days_until_due();

    // Total span = history + remaining days (at least show 7 days)
    int total_span = std::max(n + std::max(0, days_left), 7);

    // ── Background ────────────────────────────────────────────────────────────
    cr->set_source_rgba(1, 1, 1, 0.03);
    cr->rectangle(ml, mt, cw, ch);
    cr->fill();

    // ── Grid lines ────────────────────────────────────────────────────────────
    cr->set_line_width(1.0);
    cr->set_source_rgba(1, 1, 1, 0.07);
    int y_steps = 4;
    for (int i = 0; i <= y_steps; ++i) {
        double yy = mt + ch - ch * i / y_steps;
        cr->move_to(ml, yy); cr->line_to(ml + cw, yy);
        cr->stroke();

        // Y axis label
        if (target > 0) {
            int label_val = (int)std::round((double)target * i / y_steps);
            std::string lbl;
            if (label_val >= 1000)
                lbl = std::to_string(label_val / 1000) + "k";
            else
                lbl = std::to_string(label_val);
            cr->set_source_rgba(1, 1, 1, 0.35);
            cr->select_font_face("monospace", Cairo::ToyFontFace::Slant::NORMAL,
                                  Cairo::ToyFontFace::Weight::NORMAL);
            cr->set_font_size(9);
            Cairo::TextExtents te;
            cr->get_text_extents(lbl, te);
            cr->move_to(ml - te.width - 5, yy + te.height / 2.0);
            cr->show_text(lbl);
        }
        cr->set_source_rgba(1, 1, 1, 0.07);
    }

    // ── X axis labels (every ~7 days) ─────────────────────────────────────────
    cr->set_source_rgba(1, 1, 1, 0.35);
    cr->set_font_size(9);
    for (int i = 0; i < (int)history.size(); i += std::max(1, (int)history.size() / 6)) {
        double x = ml + cw * (double)i / total_span;
        std::string lbl = history[i].date.size() >= 10
                          ? history[i].date.substr(5) : ""; // MM-DD
        if (lbl.empty()) continue;
        Cairo::TextExtents te;
        cr->get_text_extents(lbl, te);
        cr->move_to(x - te.width / 2.0, mt + ch + 14);
        cr->show_text(lbl);
    }

    // ── Ideal pace line ───────────────────────────────────────────────────────
    if (target > 0 && total_span > 1) {
        cr->set_source_rgba(0.55, 0.78, 0.95, 0.55);  // sky-blue
        cr->set_line_width(1.5);
        std::vector<double> dashes = {4.0, 4.0};
        cr->set_dash(dashes, 0);
        cr->move_to(ml, mt + ch); // start at bottom left (0 words)
        cr->line_to(ml + cw, mt);  // end at top right (target)
        cr->stroke();
        cr->set_dash(std::vector<double>{}, 0);  // reset dash
    }

    // ── Cumulative progress bars ──────────────────────────────────────────────
    if (n > 0) {
        // Compute cumulative sums
        std::vector<int> cumulative(n);
        cumulative[0] = history[0].words;
        for (int i = 1; i < n; ++i)
            cumulative[i] = cumulative[i-1] + history[i].words;

        double bar_w = std::max(2.0, cw / total_span - 1.0);
        double y_scale = (target > 0) ? ch / (double)target : 1.0;

        for (int i = 0; i < n; ++i) {
            double x  = ml + cw * (double)i / total_span;
            double bh = std::min(ch, cumulative[i] * y_scale);
            double y  = mt + ch - bh;

            // Colour: ahead = teal, behind = peach/warn
            if (target > 0) {
                double ideal_frac = (double)(i + 1) / total_span;
                double actual_frac = (double)cumulative[i] / target;
                if (actual_frac >= ideal_frac)
                    cr->set_source_rgba(0.36, 0.78, 0.69, 0.7);   // teal (ahead)
                else
                    cr->set_source_rgba(0.98, 0.70, 0.53, 0.7);   // peach (behind)
            } else {
                cr->set_source_rgba(0.36, 0.78, 0.69, 0.7);
            }

            cr->rectangle(x, y, bar_w, bh);
            cr->fill();

            // Today marker
            if (i == n - 1) {
                cr->set_source_rgba(1, 1, 1, 0.9);
                cr->set_line_width(1.0);
                cr->move_to(x + bar_w / 2.0, mt);
                cr->line_to(x + bar_w / 2.0, mt + ch);
                cr->stroke();
            }
        }
    }

    // ── Axes ──────────────────────────────────────────────────────────────────
    cr->set_source_rgba(1, 1, 1, 0.18);
    cr->set_line_width(1.0);
    // Y axis
    cr->move_to(ml, mt); cr->line_to(ml, mt + ch); cr->stroke();
    // X axis
    cr->move_to(ml, mt + ch); cr->line_to(ml + cw, mt + ch); cr->stroke();

    // ── Target line label ─────────────────────────────────────────────────────
    if (target > 0) {
        cr->set_source_rgba(0.55, 0.78, 0.95, 0.7);
        cr->set_font_size(9);
        std::string t_lbl = std::to_string(target / 1000) + "k target";
        cr->move_to(ml + 4, mt + 10);
        cr->show_text(t_lbl);
    }
}

} // namespace Folio
