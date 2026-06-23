#pragma once
#include <string>
#include <array>
#include <vector>
#include <algorithm>
#include <giomm.h>
#include <glibmm.h>

#include "CompileFormat.hpp"   // custom PDF compile formats (s18)

namespace Folio {

// ─── StatusDef ────────────────────────────────────────────────────────────────
struct StatusDef {
    std::string name;      // e.g. "Rough Draft"
    std::string color_hex; // e.g. "#f9e2af" — dot color in sidebar
};

// ─── TagColor ─────────────────────────────────────────────────────────────────
struct TagColor {
    std::string name;   // e.g. "teal"
    std::string hex;    // e.g. "#5bc8af"
};

// ─── TabStop ──────────────────────────────────────────────────────────────────
struct TabStop {
    double      position_pt = 0.0;              // position in typographic points
    std::string type        = "left";           // "left" | "right" | "center" | "decimal"
};


// A named text style saved in preferences.
// kind: "paragraph" clears and replaces the whole paragraph's tags.
//       "character" clears and replaces only the current selection's tags.
// Unset / inherit fields: font_family="", font_size=0, line_height=0.0,
//                         justification="", fg_color="", bg_color="".
struct TextStyle {
    std::string kind          = "paragraph"; // "paragraph" | "character"
    std::string name          = "Unnamed";
    std::string font_family;                 // "" = inherit
    int         font_size     = 0;           // 0  = inherit
    bool        bold          = false;
    bool        italic        = false;
    bool        underline     = false;
    std::string justification;               // "" | "left" | "center" | "right" | "full"
    std::string fg_color;                    // "" | "#rrggbb"
    std::string bg_color;                    // "" | "#rrggbb"
    double      line_height   = 0.0;         // 0  = inherit
};

// ─── NodeDefaults ─────────────────────────────────────────────────────────────
// Template applied when a new node or group is created in a given section.
struct NodeDefaults {
    std::string title;
    std::string status_name;
    std::string role_name;       // characters only
    int         color_idx  = 0;
    bool        include_in_export = true;
    int         word_target = 0;

    // Default template auto-apply
    std::string template_name;              // title of template to apply (empty = none)
    bool        template_copy_title        = false;
    bool        template_copy_color        = false;
    bool        template_copy_status       = false;
    bool        template_copy_word_target  = false;
};

// ─── PomodoroPrefs ────────────────────────────────────────────────────────────
// All durations stored in minutes for human-readable prefs; PomodoroTimer
// converts to seconds when it reads them.
struct PomodoroPrefs {
    int  focus_min            = 25;
    int  short_break_min      = 5;
    int  long_break_min       = 15;
    int  sessions_before_long = 4;
    bool auto_start           = false;
    bool show_in_headerbar    = true;

    // Phase ring / banner colours — hex strings matching tag_colors palette
    std::string focus_color       = "#5bc8af";  // default accent teal
    std::string short_break_color = "#a6e3a1";  // green
    std::string long_break_color  = "#cba6f7";  // mauve
    std::string pip_color         = "#5bc8af";  // session dot (pip) fill color
};

// ─── HeadingStyle ─────────────────────────────────────────────────────────────
// Visual style for one outline level.
// marker:    "1"=Arabic "A"=UC-Alpha "a"=LC-Alpha "I"=UC-Roman "i"=LC-Roman ""=none
// separator: string appended after each level's counter, default "."
struct HeadingStyle {
    int    font_size_pt   = 0;     // 0 = auto (editor_font_size × scale)
    double font_scale     = 1.0;   // multiplier when font_size_pt == 0
    bool   bold           = true;
    bool   italic         = false;
    std::string color_hex;         // "" = inherit editor colour
    int    space_above_px = 12;    // extra space above this heading paragraph
    int    space_below_px = 4;     // extra space below
    std::string marker    = "";    // numbering marker character
    std::string separator = ".";   // separator after each counter
};

inline constexpr int MAX_OUTLINE_LEVELS = 9;

// ─── Heading presets ──────────────────────────────────────────────────────────
struct HeadingPreset {
    const char* name;
    const char* markers[MAX_OUTLINE_LEVELS];
    const char* separators[MAX_OUTLINE_LEVELS];
};

inline constexpr HeadingPreset HEADING_PRESETS[] = {
    { "Legal",    {"I","A","1","a","i","A","a","1","i"}, {".",".",".",".",".",".",".",".","."}},
    { "Numeric",  {"1","1","1","1","1","1","1","1","1"}, {".",".",".",".",".",".",".",".","."}},
    { "Academic", {"1","a","i","A","I","a","i","1","a"}, {".",".",".",".",".",".",".",".","."}},
    { "Simple",   {"1","2","3","4","5","6","7","8","9"}, {".",".",".",".",".",".",".",".","."}},
    { "Alpha",    {"A","B","C","D","E","F","G","H","I"}, {".",".",".",".",".",".",".",".","."}},
    { "None",     {"", "", "", "", "", "", "", "", ""},   {".",".",".",".",".",".",".",".","."}},
};
inline constexpr int HEADING_PRESET_COUNT   = 6;
inline constexpr int HEADING_PRESET_DEFAULT = 0; // Legal

// ─── FolioPrefs ──────────────────────────────────────────────────────────────
// Persistent user preferences stored in
//   ~/.config/folio/preferences.ini
// using GLib's GKeyFile format (standard for Linux desktop apps).
//
// Call load() once at startup; call save() after any change.
// All members are plain value types — no GTK widgets.

class FolioPrefs {
public:
    // ── Typography ────────────────────────────────────────────────────────────
    std::string editor_font        = "Lora";
    int         editor_font_size   = 16;     // pt
    std::string ui_font            = "Cantarell";
    int         ui_font_size       = 13;     // pt
    double      line_spacing       = 1.9;
    bool        first_line_indent         = true;
    int         first_line_indent_px      = 32;
    int         paragraph_spacing_px      = 0;    // extra px above each paragraph

    // ── Style font defaults ───────────────────────────────────────────────────
    // Used as the base font for built-in styles. Built-in styles are seeded
    // from these on first run (when text_styles is empty).
    std::string serif_font         = "JansonText";   // fallback: Times New Roman
    std::string sans_font          = "Cantarell";    // fallback: Liberation Sans
    std::string mono_font          = "Courier New";  // fallback: Liberation Mono

    // ── Headings & Outline ────────────────────────────────────────────────────
    // heading_styles[0..8] — styles for up to MAX_OUTLINE_LEVELS levels.
    // Only heading_styles[0..outline_levels-1] are active.
    int outline_levels = 3;  // how many levels are active (3–MAX_OUTLINE_LEVELS)
    std::array<HeadingStyle, MAX_OUTLINE_LEVELS> heading_styles = {{
        { 0, 1.80, true,  false, "", 16, 6, "I", "." }, // level 1
        { 0, 1.40, true,  false, "", 12, 4, "A", "." }, // level 2
        { 0, 1.15, true,  true,  "", 8,  2, "1", "." }, // level 3
        { 0, 1.00, false, false, "", 4,  1, "a", "." }, // level 4
        { 0, 1.00, false, true,  "", 4,  1, "i", "." }, // level 5
        { 0, 1.00, false, false, "", 4,  1, "A", ")" }, // level 6
        { 0, 1.00, false, true,  "", 4,  1, "a", ")" }, // level 7
        { 0, 1.00, false, false, "", 4,  1, "1", ")" }, // level 8
        { 0, 1.00, false, true,  "", 4,  1, "i", ")" }, // level 9
    }};

    // ── Screenplay ────────────────────────────────────────────────────────────
    // Tab-key cycle order for screenplay elements.
    // Each entry is one of: "scene","action","character","parenthetical","dialogue","transition"
    // Default: scene→action→character→dialogue (loop), Tab from character→parenthetical→dialogue
    std::vector<std::string> screenplay_tab_cycle = {
        "scene", "action", "character", "dialogue"
    };

    // ── Editor geometry ───────────────────────────────────────────────────────
    int  typewriter_width_chars    = 72;     // chars before soft-wrap
    bool typewriter_mode           = false;  // keep cursor centred vertically
    double typewriter_position     = 0.42;   // s44 — rail fraction from top (0.30–0.55)
    bool focus_mode_dim            = true;   // dim non-active paragraph in focus
    int  editor_page_width_pct     = 65;     // page width as % of editor window (15–100)
    int  editor_page_margin_px     = 64;     // legacy — kept for migration
    int  editor_left_margin_px     = 64;     // left margin inside the page (px)
    int  editor_right_margin_px    = 64;     // right margin inside the page (px)
    bool editor_margins_linked     = true;   // left/right move together
    int  focus_page_width_pct      = 80;     // page width in focus mode (15–100)
    bool focus_typewriter_mode     = false;  // typewriter mode independent in focus
    bool focus_show_line_numbers   = false;  // s46 — focus-only line numbers (opt-in; editor gutter independent)
    bool focus_show_invisibles     = false;  // s46 — focus-only invisible-char marks (opt-in)
    int  focus_zoom_pct            = 100;    // zoom level in focus mode (50–300)
    int  focus_page_margin_px      = 64;     // page margin in focus mode
    std::string focus_font         = "";     // font family in focus ("" = use editor font)
    int         focus_font_size    = 0;      // font size in focus (0 = use editor size)
    double      focus_line_spacing = 0.0;   // line spacing in focus (0 = use editor spacing)
    std::string focus_text_color   = "";     // text color override in focus ("" = none)
    // s45 — focus backdrop (global pref, external path; projects link, never embed —
    // a gather/archive step is the future packager). dim = darkness of the scrim over
    // the whole photo (0=none); panel_opacity = alpha of the text-column backing card
    // (1=solid card, photo only in the margins; ~0.6–0.85 = frosted, photo as atmosphere).
    std::string focus_background_path = "";   // external image path ("" = no backdrop)
    double      focus_background_dim   = 0.35; // 0.0–0.9 scrim darkness over the photo
    double      focus_panel_opacity    = 0.78; // 0.0–1.0 text-column backing alpha
    std::string focus_panel_color      = "#1e1e2e"; // s45 — card fill colour (hex)
    int  editor_zoom_pct           = 100;    // zoom level (50–300%)
    bool editor_header_visible     = true;   // show title/path header in editor

    // ── Ruler ─────────────────────────────────────────────────────────────────
    bool        show_ruler          = false;
    std::string ruler_unit          = "cm";    // "mm"|"cm"|"inch"|"pt"|"pc"
    std::string ruler_tab_type      = "left";  // current click-to-add type
    std::vector<TabStop> tab_stops;            // tab stops with position+type

    // ── Window geometry ───────────────────────────────────────────────────────
    int  window_width      = 1280;
    int  window_height     = 800;
    bool window_maximized  = false;

    // ── Panel visibility & width state ───────────────────────────────────────
    bool binder_visible    = true;
    bool inspector_visible = true;
    int  binder_width      = 260;
    int  paned_right_pos   = 1000;
    int  notes_anno_pane_pos = 300; // split between notes and annotations

    // ── Sidebar disclosure state ──────────────────────────────────────────────
    bool sidebar_sec_manuscript_expanded = true;
    bool sidebar_sec_characters_expanded = true;
    bool sidebar_sec_places_expanded     = true;
    bool sidebar_sec_references_expanded = true;
    bool sidebar_sec_templates_expanded  = true;
    bool sidebar_sec_trash_expanded      = true;
    bool sidebar_pomo_tile_expanded      = true;
    bool sidebar_session_tile_expanded   = true;
    bool inspector_progress_expanded     = true;
    bool inspector_proj_project_expanded     = true;
    bool inspector_proj_synopsis_expanded    = true;
    bool inspector_proj_publication_expanded = true;
    bool inspector_proj_cover_expanded       = true;
    bool inspector_proj_goals_expanded       = true;
    // Metadata tab — node
    bool inspector_meta_node_identity_expanded = true;
    bool inspector_meta_node_synopsis_expanded = true;
    bool inspector_meta_node_status_expanded   = true;
    bool inspector_meta_node_label_expanded    = true;
    bool inspector_meta_node_scene_expanded    = true;
    // Metadata tab — character
    bool inspector_meta_char_identity_expanded = true;
    bool inspector_meta_char_tagline_expanded  = true;
    bool inspector_meta_char_colour_expanded   = true;
    // Metadata tab — place
    bool inspector_meta_place_identity_expanded    = true;
    bool inspector_meta_place_description_expanded = true;
    bool inspector_meta_place_colour_expanded      = true;
    // Metadata tab — reference
    bool inspector_meta_ref_reference_expanded = true;
    bool inspector_meta_ref_notes_expanded     = true;

    // ── Appearance ────────────────────────────────────────────────────────────
    std::string theme              = "system"; // "system" | "dark" | "light"
    bool        show_word_count    = true;
    bool        show_reading_time  = false;
    bool        show_paragraph_marks = false;
    bool        show_line_numbers    = false;
    bool        show_annotations     = true;
    bool        show_links           = true;
    bool        show_invisibles      = false;

    // ── Auto-save ─────────────────────────────────────────────────────────────
    bool auto_save                 = true;
    int  auto_save_interval_min    = 5;    // minutes between saves (1–360)
    bool save_on_close             = true; // auto-save when window is closed

    // ── Backup ────────────────────────────────────────────────────────────────
    bool        backup_enabled        = true;
    int         backup_interval_hours = 1;   // hours between timed backups (1–24; 0 = close-only)
    int         backup_max_count      = 10;  // max backup files to keep (3–50)
    std::string backup_dir            = "";  // empty = use default XDG path

    // ── Status indicators (dynamic — user can add/remove/edit) ───────────────
    std::vector<StatusDef> statuses = {
        {"Rough Draft",  "#f9e2af"},   // yellow
        {"In Progress",  "#89b4fa"},   // blue
        {"Polished",     "#a6e3a1"},   // green
        {"Skip",         "#6c7086"},   // muted
    };

    // ── Character roles (dynamic — user can add/remove/edit) ─────────────────
    std::vector<std::string> character_roles = {
        "Protagonist", "Antagonist", "Supporting", "Minor"
    };

    // ── Genres (dynamic — user can add/remove/edit) ───────────────────────────
    std::vector<std::string> genres = {
        // Fiction
        "Literary Fiction", "Commercial Fiction", "Historical Fiction",
        "Science Fiction", "Fantasy", "Epic Fantasy", "Urban Fantasy",
        "Horror", "Thriller", "Psychological Thriller", "Mystery",
        "Crime", "Detective", "Noir", "Cozy Mystery",
        "Romance", "Contemporary Romance", "Paranormal Romance",
        "Adventure", "Action & Adventure", "Western",
        "Dystopian", "Post-Apocalyptic", "Steampunk", "Cyberpunk",
        "Magical Realism", "Fairy Tale", "Mythology",
        "Young Adult", "Middle Grade", "Children's",
        "Graphic Novel", "Humour", "Satire",
        // Non-fiction
        "Memoir", "Autobiography", "Biography",
        "True Crime", "Narrative Non-Fiction",
        "Self-Help", "Personal Development",
        "History", "Politics", "Philosophy",
        "Science & Nature", "Travel Writing", "Essays"
    };

    // ── Tag colours (dynamic — user can add/remove/edit) ─────────────────────
    std::vector<TagColor> tag_colors = {
        {"teal",   "#5bc8af"},
        {"yellow", "#f9e2af"},
        {"red",    "#f38ba8"},
        {"green",  "#a6e3a1"},
        {"mauve",  "#cba6f7"},
        {"peach",  "#fab387"},
        {"sky",    "#89dceb"},
    };

    // ── Text styles (dynamic — user can add/remove/edit) ─────────────────────
    std::vector<TextStyle> text_styles;   // persisted in GROUP_STYLES
    // Returns the built-in starter set seeded from serif/sans/mono font prefs.
    // Called automatically after load() if text_styles is empty.
    std::vector<TextStyle> default_styles() const;

    // ── Custom PDF compile formats (s18 — user-editable, persisted) ──────────
    // The three code-seeded presets (builtin_compile_formats()) are NOT stored
    // here — only user-created/edited formats are. all_compile_formats() returns
    // the builtins followed by these, which is what the PDF picker shows.
    std::vector<CompileFormat> custom_compile_formats;   // persisted in GROUP_COMPILE_FORMATS
    std::vector<CompileFormat> all_compile_formats() const;

    // ── Pomodoro ──────────────────────────────────────────────────────────────
    PomodoroPrefs pomodoro;

    // ── Session goal ─────────────────────────────────────────────────────────
    int  daily_word_goal           = 1000;

    // ── Node creation defaults ────────────────────────────────────────────────
    NodeDefaults scene_defaults;
    NodeDefaults group_defaults;
    NodeDefaults character_defaults;
    NodeDefaults char_group_defaults;
    NodeDefaults place_defaults;
    NodeDefaults place_group_defaults;
    NodeDefaults reference_defaults;
    NodeDefaults template_defaults;

    // App-wide templates — serialised as a JSON array string.
    // Use global_templates_get/set (defined in FolioPrefs.cpp) to
    // convert to/from std::vector<BinderNode> without a circular include.
    std::string global_templates_json; // raw JSON array, empty = none

    // ── Recent files & startup ────────────────────────────────────────────────
    static constexpr int MAX_RECENT = 20;   // hard upper bound for storage slots
    int  max_recent_files           = 10;   // user-configurable (1–MAX_RECENT)
    bool reopen_last_file           = true;
    std::vector<std::string> recent_files;   // most-recent first
    std::string last_export_folder;          // remembered across sessions
    std::vector<std::string> split_separators = {"---", "* * *"}; // scene split patterns

    // Prepends path to recent_files, removing duplicates, capping at max_recent_files
    void push_recent(const std::string& path) {
        recent_files.erase(
            std::remove(recent_files.begin(), recent_files.end(), path),
            recent_files.end());
        recent_files.insert(recent_files.begin(), path);
        if ((int)recent_files.size() > max_recent_files)
            recent_files.resize(max_recent_files);
    }

    // Clears all recent files
    void clear_recent() {
        recent_files.clear();
    }

    // ── Color index helpers ───────────────────────────────────────────────────
    // color_idx: 0 = None, 1..N = tag_colors[idx-1]. Returns "" if None/OOB.
    std::string color_hex_for_idx(int idx) const {
        if (idx <= 0 || idx > (int)tag_colors.size()) return "";
        return tag_colors[idx - 1].hex;
    }
    std::string color_name_for_idx(int idx) const {
        if (idx <= 0 || idx > (int)tag_colors.size()) return "";
        return tag_colors[idx - 1].name;
    }
    // Find 1-based index for a legacy color name (e.g. "teal"). Returns 0 if not found.
    int color_idx_for_name(const std::string& name) const {
        std::string lo = name;
        std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);
        for (int i = 0; i < (int)tag_colors.size(); ++i) {
            std::string t = tag_colors[i].name;
            std::transform(t.begin(), t.end(), t.begin(), ::tolower);
            if (t == lo) return i + 1;
        }
        return 0;
    }

    // ── Status color helper ──────────────────────────────────────────────────
    // Returns the hex color for a status name, or a neutral fallback if not found.
    std::string status_color_for_name(const std::string& name) const {
        for (const auto& s : statuses)
            if (s.name == name) return s.color_hex.empty() ? "#6c7086" : s.color_hex;
        return "#6c7086";
    }

    // ── Spell check ───────────────────────────────────────────────────────────
    bool        spell_check_enabled     = true;
    std::string spell_language          = "";      // "" = follow system locale
    // Error appearance — what the user sees under misspelled words
    std::string spell_underline_color   = "#e06c75"; // default red-ish
    bool        spell_underline_bold    = false;     // thicker line
    bool        spell_background_tint   = false;     // subtle background highlight
    std::string spell_background_color  = "#3d2a2a"; // used only when tint is on
    // Underline style: "single" | "double" | "wavy"
    std::string spell_underline_style   = "wavy";

    // ── Text substitution ─────────────────────────────────────────────────────
    bool sub_smart_quotes       = true;   // " " → " "   ' ' → ' '
    bool sub_em_dash            = true;   // -- → —
    bool sub_ellipsis           = true;   // ... → …
    bool sub_autocorrect        = true;   // user-defined pairs (see below)

    // User-defined autocorrect pairs: replace first → second
    // e.g. { "teh", "the" }  { "iwth", "with" }
    std::vector<std::pair<std::string,std::string>> autocorrect_pairs = {
        // ── Word corrections ──────────────────────────────────────────────────
        {"teh",  "the"},
        {"adn",  "and"},
        {"iwth", "with"},
        {"taht", "that"},
        // ── Typographic characters ────────────────────────────────────────────
        {"(c)",  "\xc2\xa9"},            // (c) → © copyright
        {"(r)",  "\xc2\xae"},            // (r) → ® registered
        {"(tm)", "\xe2\x84\xa2"},        // (tm)→ ™ trademark
        // ── Invisible / special spacing ───────────────────────────────────────
        {"<nbsp>",  "\xc2\xa0"},         // <nbsp>  → non-breaking space (U+00A0)
        {"<nbhy>",  "\xe2\x80\x91"},     // <nbhy>  → non-breaking hyphen (U+2011)
        {"<shy>",   "\xc2\xad"},         // <shy>   → soft hyphen (U+00AD)
        {"<wj>",    "\xe2\x81\xa0"},     // <wj>    → word joiner (U+2060)
        {"<zwsp>",  "\xe2\x80\x8b"},     // <zwsp>  → zero-width space (U+200B)
        {"<zwnj>",  "\xe2\x80\x8c"},     // <zwnj>  → zero-width non-joiner (U+200C)
        {"<zwj>",   "\xe2\x80\x8d"},     // <zwj>   → zero-width joiner (U+200D)
        {"<lrm>",   "\xe2\x80\x8e"},     // <lrm>   → left-to-right mark (U+200E)
        {"<rlm>",   "\xe2\x80\x8f"},     // <rlm>   → right-to-left mark (U+200F)
        {"<thin>",  "\xe2\x80\x89"},     // <thin>  → thin space (U+2009)
        {"<hair>",  "\xe2\x80\x8a"},     // <hair>  → hair space (U+200A)
    };

    // ── I/O ──────────────────────────────────────────────────────────────────
    void load();
    void save() const;

    // Global templates helpers — require DocumentModel.hpp to be included
    // by the caller (to avoid circular dependency in this header).
    // Defined in FolioPrefs.cpp.
    std::vector<struct BinderNode> global_templates_get() const;
    void global_templates_set(const std::vector<struct BinderNode>& templates);

    // Returns the path used:  ~/.config/folio/preferences.ini
    static std::string config_path();

private:
    static constexpr const char* GROUP_SCREENPLAY        = "Screenplay";
    static constexpr const char* GROUP_STYLES           = "TextStyles";
    static constexpr const char* GROUP_COMPILE_FORMATS  = "CompileFormats";
    static constexpr const char* GROUP_POMODORO         = "Pomodoro";
    static constexpr const char* GROUP_TYPOGRAPHY       = "Typography";
    static constexpr const char* GROUP_EDITOR           = "Editor";
    static constexpr const char* GROUP_HEADINGS         = "Headings";
    static constexpr const char* GROUP_APPEARANCE       = "Appearance";
    static constexpr const char* GROUP_AUTOSAVE         = "AutoSave";
    static constexpr const char* GROUP_TAGS             = "TagColors";
    static constexpr const char* GROUP_STATUSES         = "Statuses";
    static constexpr const char* GROUP_ROLES            = "CharacterRoles";
    static constexpr const char* GROUP_GENRES           = "Genres";
    static constexpr const char* GROUP_SESSION          = "Session";
    static constexpr const char* GROUP_STARTUP          = "Startup";
    static constexpr const char* GROUP_DEFAULTS         = "NodeDefaults";
    static constexpr const char* GROUP_GLOBAL_TEMPLATES = "GlobalTemplates";
    static constexpr const char* GROUP_WINDOW           = "Window";
    static constexpr const char* GROUP_EDITING          = "Editing"; // spell + substitution
};

} // namespace Folio
