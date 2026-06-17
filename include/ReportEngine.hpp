#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — ReportEngine.hpp
// Pure logic, zero GTK dependency.
// Walks DocumentModel → produces ReportData.
// render_html() produces a self-contained HTML file.
// ─────────────────────────────────────────────────────────────────────────────
#include "DocumentModel.hpp"
#include "FolioPrefs.hpp"
#include <map>
#include <string>
#include <vector>

namespace Folio {

// ─────────────────────────────────────────────────────────────────────────────
// ReportData
// ─────────────────────────────────────────────────────────────────────────────
struct ReportData {
    // 1. Project Identity
    std::string title;
    std::string author;
    std::string genre;
    std::string publisher;
    std::string isbn;
    std::string year;
    std::string file_path;
    uintmax_t   file_size_bytes  = 0;
    std::string due_date;
    int         days_remaining   = 0;  // negative = overdue, 0 = none set

    // 2. Word Count & Progress
    int    total_words            = 0;
    int    project_word_target    = 0;
    double pct_complete           = 0.0;
    int    daily_target           = 0;
    int    days_written           = 0;
    int    current_streak         = 0;
    int    longest_streak         = 0;
    double avg_words_per_day      = 0.0;
    int    days_to_completion     = 0;
    std::vector<DailyRecord> daily_history; // for sparkline

    // 3. Manuscript Structure
    int ms_groups                 = 0;
    int ms_scenes                 = 0;
    int ms_max_depth              = 0;
    std::map<NodeStatus, int> scenes_by_status;
    std::map<int, int>        scenes_by_color;   // color_idx → count
    int scenes_with_word_target   = 0;
    int scenes_empty              = 0;
    int scenes_excluded           = 0;
    int scenes_content_modified   = 0;
    std::vector<std::string> empty_scene_titles;
    std::map<std::string, int> pov_breakdown;    // pov name → scene count

    // 4. Snapshots
    int total_snapshots           = 0;
    int nodes_modified_no_snap    = 0;
    std::string most_recent_snapshot_ts;

    // 5. Annotations
    int total_annotations         = 0;
    std::map<std::string, int> annotations_by_kind; // "Writer"/"Editor"/"Proofreader"

    // 6. Notes
    int total_notes               = 0;

    // 7. Characters
    int char_nodes                = 0;
    int char_groups               = 0;
    std::map<std::string, int> chars_by_role;
    int chars_with_image          = 0;
    int chars_empty               = 0;

    // 8. Places
    int place_nodes               = 0;
    int place_groups              = 0;
    int places_with_image         = 0;
    int places_empty              = 0;

    // 9. References
    int ref_nodes                 = 0;
    int refs_with_url             = 0;
    int refs_empty                = 0;

    // 10. Templates
    int template_nodes            = 0;
    std::vector<std::string> template_names;

    // 11. Trash
    int trash_count               = 0;
    int trash_words               = 0;
    std::map<std::string, int> trash_by_origin_section;

    // 12. Timeline
    int open_tabs                 = 0;

    // 13. Backlinks
    int total_links               = 0;
    std::string most_linked_title;

    // 14. Pomodoro
    int    pomo_total_sessions    = 0;
    int    pomo_completed         = 0;
    double pomo_total_focus_hrs   = 0.0;
    double pomo_avg_session_min   = 0.0;
    std::string pomo_best_day;

    // 15. Health indicators
    std::vector<std::string> warnings;   // items needing attention
    std::vector<std::string> positives;  // achievements
};

// ─────────────────────────────────────────────────────────────────────────────
// ReportEngine
// ─────────────────────────────────────────────────────────────────────────────
class ReportEngine {
public:
    static ReportData   generate(const DocumentModel& model,
                                  const FolioPrefs&    prefs);

    static std::string  render_html(const ReportData& data,
                                     bool dark_mode = false);

private:
    // Manuscript walk
    static void walk_manuscript(const std::vector<BinderNode>& nodes,
                                 int depth, ReportData& out);

    // Section walks (characters, places, references, templates, trash)
    static void walk_characters(const std::vector<BinderNode>& nodes,
                                 ReportData& out);
    static void walk_places    (const std::vector<BinderNode>& nodes,
                                 ReportData& out);
    static void walk_references(const std::vector<BinderNode>& nodes,
                                 ReportData& out);
    static void walk_templates (const std::vector<BinderNode>& nodes,
                                 ReportData& out);
    static void walk_trash     (const std::vector<BinderNode>& nodes,
                                 ReportData& out);

    // Writing streaks
    static int compute_current_streak(const std::vector<DailyRecord>& history);
    static int compute_longest_streak(const std::vector<DailyRecord>& history);

    // Date helpers
    static int  days_between_today(const std::string& iso_date); // positive = future
    static std::string today_iso();

    // HTML helpers
    static std::string h(const std::string& s);  // HTML-escape
    static std::string bar(double pct, const std::string& colour);
    static std::string sparkline(const std::vector<DailyRecord>& hist);
    static std::string section_open(const std::string& id,
                                     const std::string& title);
    static std::string section_close();
    static std::string stat_row(const std::string& label,
                                 const std::string& value,
                                 const std::string& note = "");
    static std::string badge(const std::string& text,
                              const std::string& colour);
    static std::string css(bool dark);
};

} // namespace Folio
