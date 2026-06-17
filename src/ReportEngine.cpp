// ─────────────────────────────────────────────────────────────────────────────
// Folio — ReportEngine.cpp
// Pure logic, zero GTK dependency.
// ─────────────────────────────────────────────────────────────────────────────
#include "ReportEngine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>

namespace Folio {

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Date helpers
// ─────────────────────────────────────────────────────────────────────────────
std::string ReportEngine::today_iso() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm* tm_ptr = std::localtime(&t);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d", tm_ptr);
    return buf;
}

// Returns days from today to iso_date (positive = future, negative = past).
// Returns 0 if iso_date is empty or unparseable.
int ReportEngine::days_between_today(const std::string& iso_date) {
    if (iso_date.size() < 10) return 0;
    std::tm target{};
    std::istringstream ss(iso_date);
    ss >> std::get_time(&target, "%Y-%m-%d");
    if (ss.fail()) return 0;
    target.tm_hour = 12; // noon to avoid DST edge
    std::time_t t_target = std::mktime(&target);

    auto now = std::chrono::system_clock::now();
    std::time_t t_now = std::chrono::system_clock::to_time_t(now);

    double diff_sec = std::difftime(t_target, t_now);
    return static_cast<int>(std::round(diff_sec / 86400.0));
}

// ─────────────────────────────────────────────────────────────────────────────
// Streak calculations  (history assumed sorted ascending by date)
// ─────────────────────────────────────────────────────────────────────────────
int ReportEngine::compute_current_streak(const std::vector<DailyRecord>& history) {
    if (history.empty()) return 0;
    std::string today = today_iso();
    int streak = 0;
    // Walk backwards from the end
    for (int i = (int)history.size() - 1; i >= 0; --i) {
        if (history[i].words <= 0) continue;
        // Compute expected date for this position in the streak
        // We accept today or yesterday as the anchor
        if (streak == 0) {
            if (history[i].date != today) {
                // Check if it's yesterday
                int d = days_between_today(history[i].date);
                if (d < -1) break; // gap already
            }
        }
        ++streak;
        if (i == 0) break;
        // Check consecutive with previous
        int diff = days_between_today(history[i - 1].date)
                 - days_between_today(history[i].date);
        if (diff != 1) break; // non-consecutive
    }
    return streak;
}

int ReportEngine::compute_longest_streak(const std::vector<DailyRecord>& history) {
    if (history.empty()) return 0;
    // Collect dates with nonzero words into a set
    std::vector<std::string> dates;
    for (const auto& r : history)
        if (r.words > 0) dates.push_back(r.date);
    if (dates.empty()) return 0;
    std::sort(dates.begin(), dates.end());
    dates.erase(std::unique(dates.begin(), dates.end()), dates.end());

    int longest = 1, cur = 1;
    for (size_t i = 1; i < dates.size(); ++i) {
        int diff = days_between_today(dates[i - 1]) - days_between_today(dates[i]);
        if (diff == -1) { // consecutive ascending
            ++cur;
            longest = std::max(longest, cur);
        } else {
            cur = 1;
        }
    }
    return longest;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tree walkers
// ─────────────────────────────────────────────────────────────────────────────
void ReportEngine::walk_manuscript(const std::vector<BinderNode>& nodes,
                                    int depth, ReportData& out) {
    for (const auto& n : nodes) {
        if (binder_kind_is_group(n.kind)) {
            ++out.ms_groups;
            out.ms_max_depth = std::max(out.ms_max_depth, depth);
            // Annotations on group preface
            out.total_annotations += (int)n.annotations.size();
            for (const auto& ann : n.annotations)
                out.annotations_by_kind[ann.kind]++;
            out.total_notes += (int)n.notes.size();
            // Snapshots
            out.total_snapshots += (int)n.snapshots.size();
            for (const auto& snap : n.snapshots) {
                if (out.most_recent_snapshot_ts.empty() ||
                    snap.timestamp > out.most_recent_snapshot_ts)
                    out.most_recent_snapshot_ts = snap.timestamp;
            }
            if (!n.children.empty())
                walk_manuscript(n.children, depth + 1, out);
        } else {
            // Scene
            ++out.ms_scenes;
            out.scenes_by_status[n.status]++;
            if (n.color_idx > 0)
                out.scenes_by_color[n.color_idx]++;
            if (n.word_target > 0)
                ++out.scenes_with_word_target;
            if (!n.include_in_export)
                ++out.scenes_excluded;
            if (n.content_modified)
                ++out.scenes_content_modified;

            bool empty = n.word_count() == 0;
            if (empty) {
                ++out.scenes_empty;
                out.empty_scene_titles.push_back(n.title.empty() ? "(Untitled)" : n.title);
            }

            if (!n.pov_character_name.empty())
                out.pov_breakdown[n.pov_character_name]++;

            // Snapshots
            out.total_snapshots += (int)n.snapshots.size();
            for (const auto& snap : n.snapshots) {
                if (out.most_recent_snapshot_ts.empty() ||
                    snap.timestamp > out.most_recent_snapshot_ts)
                    out.most_recent_snapshot_ts = snap.timestamp;
            }
            if (n.content_modified && n.snapshots.empty())
                ++out.nodes_modified_no_snap;

            // Annotations & notes
            out.total_annotations += (int)n.annotations.size();
            for (const auto& ann : n.annotations)
                out.annotations_by_kind[ann.kind]++;
            out.total_notes += (int)n.notes.size();
        }
    }
}

void ReportEngine::walk_characters(const std::vector<BinderNode>& nodes,
                                    ReportData& out) {
    for (const auto& n : nodes) {
        if (binder_kind_is_group(n.kind)) {
            ++out.char_groups;
            walk_characters(n.children, out);
        } else {
            ++out.char_nodes;
            if (!n.role.empty())
                out.chars_by_role[n.role]++;
            if (!n.image_path.empty()) ++out.chars_with_image;
            if (n.word_count() == 0 && n.description.empty()) ++out.chars_empty;
            out.total_notes += (int)n.notes.size();
        }
    }
}

void ReportEngine::walk_places(const std::vector<BinderNode>& nodes,
                                ReportData& out) {
    for (const auto& n : nodes) {
        if (binder_kind_is_group(n.kind)) {
            ++out.place_groups;
            walk_places(n.children, out);
        } else {
            ++out.place_nodes;
            if (!n.image_path.empty()) ++out.places_with_image;
            if (n.word_count() == 0 && n.description.empty()) ++out.places_empty;
            out.total_notes += (int)n.notes.size();
        }
    }
}

void ReportEngine::walk_references(const std::vector<BinderNode>& nodes,
                                    ReportData& out) {
    for (const auto& n : nodes) {
        if (binder_kind_is_group(n.kind)) {
            walk_references(n.children, out);
        } else {
            ++out.ref_nodes;
            if (!n.url.empty()) ++out.refs_with_url;
            if (n.word_count() == 0) ++out.refs_empty;
            out.total_notes += (int)n.notes.size();
        }
    }
}

void ReportEngine::walk_templates(const std::vector<BinderNode>& nodes,
                                   ReportData& out) {
    for (const auto& n : nodes) {
        if (binder_kind_is_group(n.kind)) {
            walk_templates(n.children, out);
        } else {
            ++out.template_nodes;
            out.template_names.push_back(n.title.empty() ? "(Untitled)" : n.title);
        }
    }
}

void ReportEngine::walk_trash(const std::vector<BinderNode>& nodes,
                               ReportData& out) {
    for (const auto& n : nodes) {
        if (binder_kind_is_group(n.kind)) {
            walk_trash(n.children, out);
        } else {
            ++out.trash_count;
            out.trash_words += n.word_count();
            if (!n.trash_origin_section.empty())
                out.trash_by_origin_section[n.trash_origin_section]++;
        }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// generate()
// ─────────────────────────────────────────────────────────────────────────────
ReportData ReportEngine::generate(const DocumentModel& model,
                                   const FolioPrefs&    /*prefs*/) {
    ReportData out;

    // ── 1. Project Identity ────────────────────────────────────────────────────
    out.title      = model.project_title;
    out.author     = model.author;
    out.genre      = model.genre;
    out.publisher  = model.publisher;
    out.isbn       = model.isbn;
    out.year       = model.year;
    out.file_path  = model.current_path;
    out.due_date   = model.due_date;

    if (!model.current_path.empty()) {
        std::error_code ec;
        out.file_size_bytes = fs::file_size(model.current_path, ec);
        if (ec) out.file_size_bytes = 0;
    }

    if (!out.due_date.empty())
        out.days_remaining = days_between_today(out.due_date);

    // ── 2. Word Count & Progress ───────────────────────────────────────────────
    for (const auto& n : model.manuscript)
        out.total_words += n.total_words();

    out.project_word_target = model.project_word_target;
    out.daily_target        = model.daily_target;
    out.daily_history       = model.daily_history;

    if (out.project_word_target > 0)
        out.pct_complete = std::min(100.0,
            100.0 * out.total_words / out.project_word_target);

    out.days_written = 0;
    long long total_hist_words = 0;
    for (const auto& r : model.daily_history) {
        if (r.words > 0) {
            ++out.days_written;
            total_hist_words += r.words;
        }
    }
    if (out.days_written > 0)
        out.avg_words_per_day = static_cast<double>(total_hist_words) / out.days_written;
    else
        out.avg_words_per_day = 0.0;

    int words_remaining = std::max(0, out.project_word_target - out.total_words);
    if (out.avg_words_per_day > 0.5)
        out.days_to_completion = static_cast<int>(
            std::ceil(words_remaining / out.avg_words_per_day));
    else if (out.daily_target > 0)
        out.days_to_completion = static_cast<int>(
            std::ceil((double)words_remaining / out.daily_target));

    out.current_streak = compute_current_streak(model.daily_history);
    out.longest_streak = compute_longest_streak(model.daily_history);

    // ── 3. Manuscript Structure ───────────────────────────────────────────────
    walk_manuscript(model.manuscript, 0, out);

    // ── 4–6. Already accumulated in walk_manuscript ────────────────────────────

    // ── 7. Characters ─────────────────────────────────────────────────────────
    walk_characters(model.characters, out);

    // ── 8. Places ─────────────────────────────────────────────────────────────
    walk_places(model.places, out);

    // ── 9. References ─────────────────────────────────────────────────────────
    walk_references(model.references, out);

    // ── 10. Templates ─────────────────────────────────────────────────────────
    walk_templates(model.templates, out);

    // ── 11. Trash ─────────────────────────────────────────────────────────────
    walk_trash(model.trash, out);

    // ── 12. Timeline ──────────────────────────────────────────────────────────
    out.open_tabs = (int)model.open_tabs.size();

    // ── 13. Backlinks ─────────────────────────────────────────────────────────
    const auto& bl = model.backlinks();
    int most_links = 0;
    for (const auto& [node_iid, entries] : bl) {
        out.total_links += (int)entries.size();
        if ((int)entries.size() > most_links) {
            most_links = (int)entries.size();
            const BinderNode* n = model.find_node_by_iid(node_iid);
            out.most_linked_title = n ? n->title : "";
        }
    }

    // ── 14. Pomodoro ──────────────────────────────────────────────────────────
    std::map<std::string, int>    pomo_sessions_per_day;
    std::map<std::string, double> pomo_focus_per_day;

    for (const auto& rec : model.pomodoro_log) {
        if (rec.phase == "Focus") {
            ++out.pomo_total_sessions;
            if (rec.completed) ++out.pomo_completed;
            double hrs = rec.duration_sec / 3600.0;
            out.pomo_total_focus_hrs += hrs;
            pomo_sessions_per_day[rec.date]++;
            pomo_focus_per_day[rec.date] += rec.duration_sec / 60.0;
        }
    }
    if (out.pomo_total_sessions > 0)
        out.pomo_avg_session_min = (out.pomo_total_focus_hrs * 60.0)
                                    / out.pomo_total_sessions;

    if (!pomo_sessions_per_day.empty()) {
        auto it = std::max_element(pomo_sessions_per_day.begin(),
                                    pomo_sessions_per_day.end(),
                                    [](const auto& a, const auto& b) {
                                        return a.second < b.second;
                                    });
        out.pomo_best_day = it->first;
    }

    // ── 15. Health indicators ──────────────────────────────────────────────────
    if (out.trash_count > 0)
        out.warnings.push_back("Trash contains " + std::to_string(out.trash_count) + " items");
    if (!out.due_date.empty() && out.days_remaining > 0 && out.days_remaining < 14)
        out.warnings.push_back("Due date in " + std::to_string(out.days_remaining) + " days");
    if (!out.due_date.empty() && out.days_remaining < 0)
        out.warnings.push_back("Overdue by " + std::to_string(-out.days_remaining) + " days");
    if (out.nodes_modified_no_snap > 0)
        out.warnings.push_back(std::to_string(out.nodes_modified_no_snap)
            + " scenes modified without snapshot");
    if (out.scenes_empty > 0)
        out.warnings.push_back(std::to_string(out.scenes_empty) + " empty scenes");

    if (out.pct_complete >= 100.0)
        out.positives.push_back("Project word target reached 🎉");
    if (out.current_streak >= 7)
        out.positives.push_back(std::to_string(out.current_streak) + "-day writing streak 🔥");
    if (out.current_streak >= 30)
        out.positives.push_back("30-day writing streak — outstanding! 🏆");
    if (out.total_snapshots > 0)
        out.positives.push_back(std::to_string(out.total_snapshots) + " snapshots — good backup habit");
    if (out.pomo_completed >= 10)
        out.positives.push_back(std::to_string(out.pomo_completed) + " focus sessions completed");

    return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// HTML rendering helpers
// ─────────────────────────────────────────────────────────────────────────────
std::string ReportEngine::h(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        switch (c) {
            case '&':  out += "&amp;";  break;
            case '<':  out += "&lt;";   break;
            case '>':  out += "&gt;";   break;
            case '"':  out += "&quot;"; break;
            case '\'': out += "&#39;";  break;
            default:   out += c;        break;
        }
    }
    return out;
}

std::string ReportEngine::bar(double pct, const std::string& colour) {
    pct = std::max(0.0, std::min(100.0, pct));
    std::ostringstream o;
    o << "<div class='bar-bg'><div class='bar-fill' style='width:"
      << std::fixed << std::setprecision(1) << pct
      << "%;background:" << colour << "'></div></div>";
    return o.str();
}

std::string ReportEngine::sparkline(const std::vector<DailyRecord>& hist) {
    if (hist.empty()) return "<em>No data</em>";

    // Last 30 days
    std::vector<DailyRecord> recent;
    {
        std::string today = today_iso();
        for (const auto& r : hist) {
            int d = days_between_today(r.date);
            if (d >= -29 && d <= 0) recent.push_back(r);
        }
    }
    if (recent.empty()) return "<em>No recent data</em>";

    int max_w = 1;
    for (const auto& r : recent) max_w = std::max(max_w, r.words);

    std::ostringstream o;
    o << "<div class='sparkline'>";
    for (const auto& r : recent) {
        double pct = 100.0 * r.words / max_w;
        int h_px   = std::max(2, (int)std::round(pct * 40.0 / 100.0));
        std::string colour = (r.words == 0) ? "var(--bar-empty)"
                           : (r.words >= max_w * 0.8) ? "var(--bar-hi)" : "var(--bar-lo)";
        o << "<span class='spark-bar' title='" << h(r.date) << ": " << r.words
          << " words' style='height:" << h_px << "px;background:" << colour << "'></span>";
    }
    o << "</div>";
    return o.str();
}

std::string ReportEngine::section_open(const std::string& id,
                                        const std::string& title) {
    return "<details open><summary id='" + h(id) + "'>" + h(title) +
           "</summary><div class='section-body'>";
}

std::string ReportEngine::section_close() {
    return "</div></details>\n";
}

std::string ReportEngine::stat_row(const std::string& label,
                                    const std::string& value,
                                    const std::string& note) {
    std::ostringstream o;
    o << "<div class='stat-row'>"
      << "<span class='stat-label'>" << h(label) << "</span>"
      << "<span class='stat-value'>" << value << "</span>";
    if (!note.empty())
        o << "<span class='stat-note'>" << h(note) << "</span>";
    o << "</div>";
    return o.str();
}

std::string ReportEngine::badge(const std::string& text, const std::string& colour) {
    return "<span class='badge' style='background:" + colour + "'>" + h(text) + "</span>";
}

// ─────────────────────────────────────────────────────────────────────────────
// css()
// ─────────────────────────────────────────────────────────────────────────────
std::string ReportEngine::css(bool dark) {
    return std::string(R"(
:root {
  --bg:        )") + (dark ? "#1e1e2e" : "#f8f8f6") + R"(;
  --surface:   )" + (dark ? "#2a2a3e" : "#ffffff") + R"(;
  --border:    )" + (dark ? "#3a3a5e" : "#e0e0d8") + R"(;
  --text:      )" + (dark ? "#cdd6f4" : "#2c2c2c") + R"(;
  --muted:     )" + (dark ? "#888aaa" : "#888880") + R"(;
  --accent:    )" + (dark ? "#89b4fa" : "#3a7bd5") + R"(;
  --warn:      )" + (dark ? "#f38ba8" : "#c0392b") + R"(;
  --ok:        )" + (dark ? "#a6e3a1" : "#27ae60") + R"(;
  --bar-bg:    )" + (dark ? "#3a3a5e" : "#e8e8e2") + R"(;
  --bar-fill:  )" + (dark ? "#89b4fa" : "#3a7bd5") + R"(;
  --bar-hi:    )" + (dark ? "#a6e3a1" : "#27ae60") + R"(;
  --bar-lo:    )" + (dark ? "#89b4fa" : "#3a7bd5") + R"(;
  --bar-empty: )" + (dark ? "#3a3a5e" : "#e0e0d8") + R"(;
}
* { box-sizing: border-box; margin: 0; padding: 0; }
body {
  font-family: system-ui, 'Cantarell', sans-serif;
  font-size: 14px;
  background: var(--bg);
  color: var(--text);
  max-width: 860px;
  margin: 0 auto;
  padding: 24px 16px 48px;
}
h1 { font-size: 1.6rem; font-weight: 700; margin-bottom: 4px; }
.subtitle { color: var(--muted); font-size: 0.9rem; margin-bottom: 28px; }
details {
  background: var(--surface);
  border: 1px solid var(--border);
  border-radius: 8px;
  margin-bottom: 12px;
  overflow: hidden;
}
summary {
  display: flex;
  align-items: center;
  gap: 8px;
  padding: 12px 16px;
  font-weight: 700;
  font-size: 0.85rem;
  letter-spacing: 0.06em;
  text-transform: uppercase;
  cursor: pointer;
  color: var(--muted);
  user-select: none;
  list-style: none;
}
summary::before {
  content: '▶';
  font-size: 0.7rem;
  transition: transform 0.15s;
}
details[open] summary::before { transform: rotate(90deg); }
.section-body { padding: 4px 16px 14px; }
.stat-row {
  display: flex;
  align-items: baseline;
  gap: 8px;
  padding: 5px 0;
  border-bottom: 1px solid var(--border);
  flex-wrap: wrap;
}
.stat-row:last-child { border-bottom: none; }
.stat-label { flex: 1; color: var(--muted); font-size: 0.85rem; min-width: 160px; }
.stat-value { font-weight: 600; font-size: 0.95rem; }
.stat-note  { color: var(--muted); font-size: 0.8rem; font-style: italic; }
.bar-bg {
  width: 100%;
  height: 8px;
  background: var(--bar-bg);
  border-radius: 4px;
  overflow: hidden;
  margin-top: 6px;
}
.bar-fill {
  height: 100%;
  border-radius: 4px;
  transition: width 0.4s ease;
}
.sparkline {
  display: flex;
  align-items: flex-end;
  gap: 2px;
  height: 44px;
  margin-top: 8px;
  padding: 2px 0;
}
.spark-bar {
  flex: 1;
  border-radius: 2px 2px 0 0;
  min-width: 4px;
  max-width: 16px;
}
.badge {
  display: inline-block;
  padding: 2px 8px;
  border-radius: 12px;
  font-size: 0.78rem;
  font-weight: 600;
  color: #fff;
  margin: 2px 2px 2px 0;
}
.tag-list { display: flex; flex-wrap: wrap; gap: 4px; margin-top: 6px; }
.warn-box, .ok-box {
  border-radius: 6px;
  padding: 10px 14px;
  margin: 6px 0;
  font-size: 0.88rem;
  line-height: 1.6;
}
.warn-box { background: color-mix(in srgb, var(--warn) 12%, transparent);
            border-left: 3px solid var(--warn); color: var(--text); }
.ok-box   { background: color-mix(in srgb, var(--ok)   12%, transparent);
            border-left: 3px solid var(--ok);   color: var(--text); }
ul.compact { margin: 6px 0 0 18px; }
ul.compact li { font-size: 0.86rem; color: var(--muted); margin: 2px 0; }
.meta-grid {
  display: grid;
  grid-template-columns: 1fr 1fr;
  gap: 4px 16px;
}
@media (max-width: 500px) { .meta-grid { grid-template-columns: 1fr; } }
.toc { display: flex; flex-wrap: wrap; gap: 6px; margin-bottom: 20px; }
.toc a {
  padding: 4px 12px;
  border-radius: 16px;
  background: var(--surface);
  border: 1px solid var(--border);
  color: var(--accent);
  text-decoration: none;
  font-size: 0.82rem;
  font-weight: 500;
}
.toc a:hover { background: var(--border); }
)";
}

// ─────────────────────────────────────────────────────────────────────────────
// render_html()
// ─────────────────────────────────────────────────────────────────────────────
std::string ReportEngine::render_html(const ReportData& d, bool dark_mode) {
    std::ostringstream o;

    // ── Preamble ──────────────────────────────────────────────────────────────
    o << "<!DOCTYPE html>\n<html lang='en'>\n<head>\n"
      << "<meta charset='UTF-8'/>\n"
      << "<meta name='viewport' content='width=device-width,initial-scale=1'/>\n"
      << "<title>" << h(d.title.empty() ? "Folio Project Report" : d.title) << " — Report</title>\n"
      << "<style>" << css(dark_mode) << "</style>\n"
      << "</head>\n<body>\n";

    // Title
    o << "<h1>" << h(d.title.empty() ? "Untitled Project" : d.title) << "</h1>\n";
    o << "<div class='subtitle'>Folio Project Report · Generated " << today_iso() << "</div>\n";

    // ── Health callouts at the top ─────────────────────────────────────────────
    if (!d.warnings.empty()) {
        o << "<div class='warn-box'>⚠️ ";
        for (size_t i = 0; i < d.warnings.size(); ++i) {
            if (i) o << " &nbsp;·&nbsp; ";
            o << h(d.warnings[i]);
        }
        o << "</div>\n";
    }
    if (!d.positives.empty()) {
        o << "<div class='ok-box'>✅ ";
        for (size_t i = 0; i < d.positives.size(); ++i) {
            if (i) o << " &nbsp;·&nbsp; ";
            o << h(d.positives[i]);
        }
        o << "</div>\n";
    }

    // ── TOC ───────────────────────────────────────────────────────────────────
    o << "<div class='toc'>"
      << "<a href='#identity'>Identity</a>"
      << "<a href='#progress'>Progress</a>"
      << "<a href='#structure'>Structure</a>"
      << "<a href='#snapshots'>Snapshots</a>"
      << "<a href='#annotations'>Annotations</a>"
      << "<a href='#characters'>Characters</a>"
      << "<a href='#places'>Places</a>"
      << "<a href='#references'>References</a>"
      << "<a href='#templates'>Templates</a>"
      << "<a href='#trash'>Trash</a>"
      << "<a href='#links'>Links</a>"
      << "<a href='#pomodoro'>Pomodoro</a>"
      << "</div>\n";

    // ─────────────────────────────────────────────────────────────────────────
    // 1. Project Identity
    // ─────────────────────────────────────────────────────────────────────────
    o << section_open("identity", "1 · Project Identity");
    o << "<div class='meta-grid'>";
    o << stat_row("Title",     h(d.title));
    o << stat_row("Author",    h(d.author.empty() ? "—" : d.author));
    o << stat_row("Genre",     h(d.genre.empty() ? "—" : d.genre));
    o << stat_row("Publisher", h(d.publisher.empty() ? "—" : d.publisher));
    o << stat_row("ISBN",      h(d.isbn.empty() ? "—" : d.isbn));
    o << stat_row("Year",      h(d.year.empty() ? "—" : d.year));
    if (!d.file_path.empty()) {
        std::string sz;
        if (d.file_size_bytes >= 1024 * 1024)
            sz = std::to_string(d.file_size_bytes / (1024*1024)) + " MB";
        else if (d.file_size_bytes >= 1024)
            sz = std::to_string(d.file_size_bytes / 1024) + " KB";
        else
            sz = std::to_string(d.file_size_bytes) + " B";
        o << stat_row("File", h(d.file_path), sz);
    }
    if (!d.due_date.empty()) {
        std::string note;
        if (d.days_remaining > 0)
            note = std::to_string(d.days_remaining) + " days remaining";
        else if (d.days_remaining == 0)
            note = "due today";
        else
            note = "overdue by " + std::to_string(-d.days_remaining) + " days";
        o << stat_row("Due date", h(d.due_date), note);
    }
    o << "</div>";
    o << section_close();

    // ─────────────────────────────────────────────────────────────────────────
    // 2. Word Count & Progress
    // ─────────────────────────────────────────────────────────────────────────
    o << section_open("progress", "2 · Word Count & Progress");
    {
        std::string wc_val = std::to_string(d.total_words);
        if (d.project_word_target > 0) {
            std::ostringstream pct_s;
            pct_s << std::fixed << std::setprecision(1) << d.pct_complete;
            wc_val += " / " + std::to_string(d.project_word_target)
                   + " (" + pct_s.str() + "%)";
        }
        o << stat_row("Total words", wc_val);
        if (d.project_word_target > 0)
            o << bar(d.pct_complete, "var(--bar-fill)");

        o << stat_row("Daily target", std::to_string(d.daily_target) + " words/day");

        {
            std::ostringstream avg_s;
            avg_s << std::fixed << std::setprecision(0) << d.avg_words_per_day;
            o << stat_row("Average pace", avg_s.str() + " words/day",
                           std::to_string(d.days_written) + " days with writing");
        }

        o << stat_row("Current streak", std::to_string(d.current_streak) + " days");
        o << stat_row("Longest streak", std::to_string(d.longest_streak) + " days");

        if (d.days_to_completion > 0 && d.pct_complete < 100.0) {
            o << stat_row("Est. days to finish",
                           std::to_string(d.days_to_completion) + " days",
                           "at current pace");
        }

        // Sparkline — last 30 days
        o << "<div class='stat-row'><span class='stat-label'>Last 30 days</span></div>";
        o << sparkline(d.daily_history);
    }
    o << section_close();

    // ─────────────────────────────────────────────────────────────────────────
    // 3. Manuscript Structure
    // ─────────────────────────────────────────────────────────────────────────
    o << section_open("structure", "3 · Manuscript Structure");
    o << stat_row("Groups",     std::to_string(d.ms_groups));
    o << stat_row("Scenes",     std::to_string(d.ms_scenes));
    o << stat_row("Max depth",  std::to_string(d.ms_max_depth) + " levels");
    o << stat_row("Excluded",   std::to_string(d.scenes_excluded));
    o << stat_row("Empty scenes", std::to_string(d.scenes_empty));
    if (!d.empty_scene_titles.empty()) {
        o << "<ul class='compact'>";
        for (size_t i = 0; i < std::min(d.empty_scene_titles.size(), size_t(8)); ++i)
            o << "<li>" << h(d.empty_scene_titles[i]) << "</li>";
        if (d.empty_scene_titles.size() > 8)
            o << "<li>…and " << (d.empty_scene_titles.size() - 8) << " more</li>";
        o << "</ul>";
    }
    o << stat_row("Content modified (no snap)", std::to_string(d.scenes_content_modified));
    o << stat_row("Scenes with word target", std::to_string(d.scenes_with_word_target));

    // Status breakdown
    auto status_label = [](NodeStatus s) -> std::string {
        return node_status_label(s);
    };
    auto status_colour = [](NodeStatus s) -> std::string {
        switch (s) {
            case NodeStatus::RoughDraft:  return "#e67e22";
            case NodeStatus::InProgress:  return "#3498db";
            case NodeStatus::Polished:    return "#27ae60";
            case NodeStatus::Skip:        return "#888";
            default:                      return "#aaa";
        }
    };
    if (!d.scenes_by_status.empty()) {
        o << "<div class='stat-row'><span class='stat-label'>By status</span>"
          << "<span class='stat-value'><div class='tag-list'>";
        for (const auto& [status, count] : d.scenes_by_status)
            o << badge(status_label(status) + " " + std::to_string(count),
                        status_colour(status));
        o << "</div></span></div>";
    }

    // POV breakdown
    if (!d.pov_breakdown.empty()) {
        o << "<div class='stat-row'><span class='stat-label'>POV characters</span>"
          << "<span class='stat-value'><div class='tag-list'>";
        for (const auto& [name, count] : d.pov_breakdown)
            o << badge(h(name) + " " + std::to_string(count), "var(--accent)");
        o << "</div></span></div>";
    }
    o << section_close();

    // ─────────────────────────────────────────────────────────────────────────
    // 4. Snapshots
    // ─────────────────────────────────────────────────────────────────────────
    o << section_open("snapshots", "4 · Snapshots");
    o << stat_row("Total snapshots",       std::to_string(d.total_snapshots));
    o << stat_row("Modified without snap", std::to_string(d.nodes_modified_no_snap));
    if (!d.most_recent_snapshot_ts.empty())
        o << stat_row("Most recent", h(d.most_recent_snapshot_ts));
    o << section_close();

    // ─────────────────────────────────────────────────────────────────────────
    // 5–6. Annotations & Notes
    // ─────────────────────────────────────────────────────────────────────────
    o << section_open("annotations", "5–6 · Annotations & Notes");
    o << stat_row("Total annotations", std::to_string(d.total_annotations));
    if (!d.annotations_by_kind.empty()) {
        for (const auto& [kind, count] : d.annotations_by_kind)
            o << stat_row("  " + kind, std::to_string(count));
    }
    o << stat_row("Total notes", std::to_string(d.total_notes));
    o << section_close();

    // ─────────────────────────────────────────────────────────────────────────
    // 7. Characters
    // ─────────────────────────────────────────────────────────────────────────
    o << section_open("characters", "7 · Characters");
    o << stat_row("Character nodes", std::to_string(d.char_nodes));
    o << stat_row("Groups",          std::to_string(d.char_groups));
    o << stat_row("With image",      std::to_string(d.chars_with_image));
    o << stat_row("Empty",           std::to_string(d.chars_empty));
    if (!d.chars_by_role.empty()) {
        o << "<div class='stat-row'><span class='stat-label'>By role</span>"
          << "<span class='stat-value'><div class='tag-list'>";
        for (const auto& [role, count] : d.chars_by_role)
            o << badge(h(role) + " " + std::to_string(count), "#8e44ad");
        o << "</div></span></div>";
    }
    o << section_close();

    // ─────────────────────────────────────────────────────────────────────────
    // 8. Places
    // ─────────────────────────────────────────────────────────────────────────
    o << section_open("places", "8 · Places");
    o << stat_row("Place nodes", std::to_string(d.place_nodes));
    o << stat_row("Groups",      std::to_string(d.place_groups));
    o << stat_row("With image",  std::to_string(d.places_with_image));
    o << stat_row("Empty",       std::to_string(d.places_empty));
    o << section_close();

    // ─────────────────────────────────────────────────────────────────────────
    // 9. References
    // ─────────────────────────────────────────────────────────────────────────
    o << section_open("references", "9 · References");
    o << stat_row("Reference nodes", std::to_string(d.ref_nodes));
    o << stat_row("With URL",        std::to_string(d.refs_with_url));
    o << stat_row("Empty",           std::to_string(d.refs_empty));
    o << section_close();

    // ─────────────────────────────────────────────────────────────────────────
    // 10. Templates
    // ─────────────────────────────────────────────────────────────────────────
    o << section_open("templates", "10 · Templates");
    o << stat_row("Template nodes", std::to_string(d.template_nodes));
    if (!d.template_names.empty()) {
        o << "<ul class='compact'>";
        for (const auto& name : d.template_names)
            o << "<li>" << h(name) << "</li>";
        o << "</ul>";
    }
    o << section_close();

    // ─────────────────────────────────────────────────────────────────────────
    // 11. Trash
    // ─────────────────────────────────────────────────────────────────────────
    o << section_open("trash", "11 · Trash");
    o << stat_row("Items in trash", std::to_string(d.trash_count));
    o << stat_row("Words in trash", std::to_string(d.trash_words));
    if (!d.trash_by_origin_section.empty()) {
        for (const auto& [sec, count] : d.trash_by_origin_section)
            o << stat_row("  From " + h(sec), std::to_string(count));
    }
    o << section_close();

    // ─────────────────────────────────────────────────────────────────────────
    // 12–13. Timeline & Links
    // ─────────────────────────────────────────────────────────────────────────
    o << section_open("links", "12–13 · Timeline & Backlinks");
    o << stat_row("Open tabs",    std::to_string(d.open_tabs));
    o << stat_row("Total links",  std::to_string(d.total_links));
    if (!d.most_linked_title.empty())
        o << stat_row("Most linked node", h(d.most_linked_title));
    o << section_close();

    // ─────────────────────────────────────────────────────────────────────────
    // 14. Pomodoro
    // ─────────────────────────────────────────────────────────────────────────
    o << section_open("pomodoro", "14 · Pomodoro");
    o << stat_row("Total focus sessions", std::to_string(d.pomo_total_sessions));
    o << stat_row("Completed sessions",   std::to_string(d.pomo_completed));
    {
        std::ostringstream hrs_s, avg_s;
        hrs_s << std::fixed << std::setprecision(1) << d.pomo_total_focus_hrs;
        avg_s << std::fixed << std::setprecision(0) << d.pomo_avg_session_min;
        o << stat_row("Total focus time", hrs_s.str() + " hours");
        o << stat_row("Avg session",      avg_s.str() + " minutes");
    }
    if (!d.pomo_best_day.empty())
        o << stat_row("Best day", h(d.pomo_best_day));
    o << section_close();

    // ── Footer ────────────────────────────────────────────────────────────────
    o << "<p style='text-align:center;margin-top:32px;font-size:0.78rem;color:var(--muted)'>"
      << "Generated by Folio · " << today_iso() << "</p>\n";

    o << "</body>\n</html>\n";
    return o.str();
}

} // namespace Folio
