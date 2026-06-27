#include "FolioPrefs.hpp"
#include "DocumentModel.hpp"
#include "CompileFormatIO.hpp"   // custom compile-format (de)serialization (s18)
#include "KpPalette.hpp"         // s81 — backfill_swatch_ids (stable swatch ids)
#include "Iid.hpp"               // s81 — make_iid(IidKind::KeyPoint) generator
#include <glibmm/fileutils.h>
#include <glibmm/miscutils.h>
#include <glib.h>
#include <map>
#include <sstream>
#include <stdexcept>

namespace Folio {

std::string FolioPrefs::config_path() {
    const char* xdg = g_get_user_config_dir();
    return Glib::build_filename(xdg, "folio", "preferences.ini");
}

void FolioPrefs::load() {
    std::string path = config_path();
    if (!Glib::file_test(path, Glib::FileTest::EXISTS)) {
        // First run — seed built-in default styles and save
        if (text_styles.empty()) {
            text_styles = default_styles();
            try { save(); } catch (...) {}
        }
        return;
    }

    GKeyFile* kf = g_key_file_new();
    GError* err  = nullptr;
    if (!g_key_file_load_from_file(kf, path.c_str(), G_KEY_FILE_NONE, &err)) {
        if (err) g_error_free(err);
        g_key_file_free(kf);
        return;
    }

    auto str = [&](const char* g, const char* k, const std::string& def) -> std::string {
        char* v = g_key_file_get_string(kf, g, k, nullptr);
        if (!v) return def;
        std::string s(v); g_free(v); return s;
    };
    auto intv = [&](const char* g, const char* k, int def) -> int {
        GError* e2 = nullptr;
        int v = g_key_file_get_integer(kf, g, k, &e2);
        if (e2) { g_error_free(e2); return def; } return v;
    };
    auto dbl = [&](const char* g, const char* k, double def) -> double {
        GError* e2 = nullptr;
        double v = g_key_file_get_double(kf, g, k, &e2);
        if (e2) { g_error_free(e2); return def; } return v;
    };
    auto boolv = [&](const char* g, const char* k, bool def) -> bool {
        GError* e2 = nullptr;
        gboolean v = g_key_file_get_boolean(kf, g, k, &e2);
        if (e2) { g_error_free(e2); return def; } return (bool)v;
    };

    editor_font          = str  (GROUP_TYPOGRAPHY, "editor-font",          editor_font);
    editor_font_size     = intv (GROUP_TYPOGRAPHY, "editor-font-size",     editor_font_size);
    ui_font              = str  (GROUP_TYPOGRAPHY, "ui-font",              ui_font);
    ui_font_size         = intv (GROUP_TYPOGRAPHY, "ui-font-size",         ui_font_size);
    line_spacing         = dbl  (GROUP_TYPOGRAPHY, "line-spacing",         line_spacing);
    first_line_indent    = boolv(GROUP_TYPOGRAPHY, "first-line-indent",    first_line_indent);
    first_line_indent_px = intv (GROUP_TYPOGRAPHY, "first-line-indent-px", first_line_indent_px);
    paragraph_spacing_px = intv (GROUP_TYPOGRAPHY, "paragraph-spacing-px", paragraph_spacing_px);
    serif_font           = str  (GROUP_TYPOGRAPHY, "serif-font",           serif_font);
    sans_font            = str  (GROUP_TYPOGRAPHY, "sans-font",            sans_font);
    mono_font            = str  (GROUP_TYPOGRAPHY, "mono-font",            mono_font);

    // Heading styles — load per-level keys (all MAX_OUTLINE_LEVELS slots)
    outline_levels = intv(GROUP_HEADINGS, "outline-levels", outline_levels);
    outline_levels = std::max(3, std::min(outline_levels, MAX_OUTLINE_LEVELS));
    for (int i = 0; i < MAX_OUTLINE_LEVELS; ++i) {
        auto& hs = heading_styles[i];
        std::string p = "h" + std::to_string(i + 1) + "-";
        hs.font_scale    = dbl  (GROUP_HEADINGS, (p + "font-scale").c_str(),   hs.font_scale);
        hs.font_size_pt  = intv (GROUP_HEADINGS, (p + "font-size-pt").c_str(), hs.font_size_pt);
        hs.bold          = boolv(GROUP_HEADINGS, (p + "bold").c_str(),         hs.bold);
        hs.italic        = boolv(GROUP_HEADINGS, (p + "italic").c_str(),       hs.italic);
        hs.color_hex     = str  (GROUP_HEADINGS, (p + "color").c_str(),        hs.color_hex);
        hs.space_above_px= intv (GROUP_HEADINGS, (p + "space-above").c_str(),  hs.space_above_px);
        hs.space_below_px= intv (GROUP_HEADINGS, (p + "space-below").c_str(),  hs.space_below_px);
        hs.marker        = str  (GROUP_HEADINGS, (p + "marker").c_str(),       hs.marker);
        hs.separator     = str  (GROUP_HEADINGS, (p + "separator").c_str(),    hs.separator);
    }

    // Screenplay tab-cycle order (comma-separated element names)
    {
        char* v = g_key_file_get_string(kf, GROUP_SCREENPLAY, "tab-cycle", nullptr);
        if (v) {
            screenplay_tab_cycle.clear();
            std::string s(v); g_free(v);
            std::string tok;
            for (char c : s) {
                if (c == ',') { if (!tok.empty()) { screenplay_tab_cycle.push_back(tok); tok.clear(); } }
                else tok += c;
            }
            if (!tok.empty()) screenplay_tab_cycle.push_back(tok);
            if (screenplay_tab_cycle.empty())
                screenplay_tab_cycle = {"scene","action","character","dialogue"};
        }
    }

    typewriter_width_chars = intv (GROUP_EDITOR, "typewriter-width-chars", typewriter_width_chars);
    typewriter_mode        = boolv(GROUP_EDITOR, "typewriter-mode",        typewriter_mode);
    typewriter_position    = dbl  (GROUP_EDITOR, "typewriter-position",    typewriter_position);
    focus_mode_dim         = boolv(GROUP_EDITOR, "focus-mode-dim",         focus_mode_dim);
    editor_page_width_pct  = intv (GROUP_EDITOR, "page-width-pct",         editor_page_width_pct);
    editor_page_margin_px  = intv (GROUP_EDITOR, "page-margin-px",         editor_page_margin_px);
    editor_left_margin_px  = intv (GROUP_EDITOR, "left-margin-px",         editor_page_margin_px);
    editor_right_margin_px = intv (GROUP_EDITOR, "right-margin-px",        editor_page_margin_px);
    editor_margins_linked  = boolv(GROUP_EDITOR, "margins-linked",         editor_margins_linked);
  focus_page_width_pct   = intv (GROUP_EDITOR, "focus-page-width-pct",   focus_page_width_pct);
    focus_typewriter_mode  = boolv(GROUP_EDITOR, "focus-typewriter-mode",  focus_typewriter_mode);
    focus_show_line_numbers = boolv(GROUP_EDITOR, "focus-show-line-numbers", focus_show_line_numbers);
    focus_show_invisibles  = boolv(GROUP_EDITOR, "focus-show-invisibles",  focus_show_invisibles);
    focus_zoom_pct         = intv (GROUP_EDITOR, "focus-zoom-pct",         focus_zoom_pct);
    focus_page_margin_px   = intv (GROUP_EDITOR, "focus-page-margin-px",   focus_page_margin_px);
    focus_font           = str  (GROUP_EDITOR, "focus-font",           focus_font.c_str());
    focus_font_size      = intv (GROUP_EDITOR, "focus-font-size",      focus_font_size);
    focus_line_spacing   = dbl  (GROUP_EDITOR, "focus-line-spacing",   focus_line_spacing);
    focus_text_color     = str  (GROUP_EDITOR, "focus-text-color",     focus_text_color.c_str());
    focus_background_path = str  (GROUP_EDITOR, "focus-background-path", focus_background_path.c_str());
    focus_background_dim  = dbl  (GROUP_EDITOR, "focus-background-dim",  focus_background_dim);
    focus_panel_opacity   = dbl  (GROUP_EDITOR, "focus-panel-opacity",   focus_panel_opacity);
    focus_panel_color     = str  (GROUP_EDITOR, "focus-panel-color",     focus_panel_color.c_str());
    editor_zoom_pct        = intv (GROUP_EDITOR, "zoom-pct",               editor_zoom_pct);
    if (editor_zoom_pct < 50)  editor_zoom_pct = 50;
    if (editor_zoom_pct > 300) editor_zoom_pct = 300;
    editor_header_visible  = boolv(GROUP_EDITOR, "header-visible",         editor_header_visible);

    // Gallery image-import prefs (DESIGN_gallery §6). intv/boolv pass the field's
    // own default when the key is absent → older prefs files migrate cleanly.
    gallery_image_max_dim   = intv (GROUP_GALLERY, "image-max-dim",     gallery_image_max_dim);
    gallery_base_long_edge  = intv (GROUP_GALLERY, "base-long-edge",     gallery_base_long_edge);
    gallery_default_detail_tier = intv(GROUP_GALLERY, "default-detail-tier", gallery_default_detail_tier);
    gallery_image_quality   = intv (GROUP_GALLERY, "image-quality",     gallery_image_quality);
    gallery_thumb_max_dim   = intv (GROUP_GALLERY, "thumb-max-dim",     gallery_thumb_max_dim);
    gallery_import_max_mb   = intv (GROUP_GALLERY, "import-max-mb",      gallery_import_max_mb);
    gallery_prefer_lossless = boolv(GROUP_GALLERY, "prefer-lossless",    gallery_prefer_lossless);
    gallery_allow_url_fetch = boolv(GROUP_GALLERY, "allow-url-fetch",    gallery_allow_url_fetch);
    if (gallery_image_max_dim  < 64)  gallery_image_max_dim  = 64;
    if (gallery_base_long_edge < 64)  gallery_base_long_edge = 64;
    if (gallery_base_long_edge > gallery_image_max_dim) gallery_base_long_edge = gallery_image_max_dim;
    if (gallery_default_detail_tier < 1) gallery_default_detail_tier = 1;
    if (gallery_default_detail_tier > 4) gallery_default_detail_tier = 4;
    if (gallery_thumb_max_dim  < 32)  gallery_thumb_max_dim  = 32;
    if (gallery_image_quality  < 1)   gallery_image_quality  = 1;
    if (gallery_image_quality  > 100) gallery_image_quality  = 100;
    if (gallery_import_max_mb  < 1)   gallery_import_max_mb  = 1;

    show_ruler             = boolv(GROUP_EDITOR, "show-ruler",             show_ruler);
    ruler_unit             = str  (GROUP_EDITOR, "ruler-unit",             ruler_unit);
    ruler_tab_type         = str  (GROUP_EDITOR, "ruler-tab-type",         ruler_tab_type);

    // Tab stops — stored as space-separated "position:type" pairs
    {
        char* v = g_key_file_get_string(kf, GROUP_EDITOR, "tab-stops", nullptr);
        if (v) {
            tab_stops.clear();
            std::istringstream ss(v);
            std::string token;
            while (ss >> token) {
                TabStop ts;
                auto colon = token.find(':');
                if (colon != std::string::npos) {
                    ts.position_pt = std::stod(token.substr(0, colon));
                    ts.type        = token.substr(colon + 1);
                } else {
                    // Legacy: plain number with no type
                    ts.position_pt = std::stod(token);
                    ts.type        = "left";
                }
                tab_stops.push_back(ts);
            }
            g_free(v);
        }
    }
    if (editor_page_width_pct < 15)  editor_page_width_pct = 15;
    if (editor_page_width_pct > 100) editor_page_width_pct = 100;

    theme                = str  (GROUP_APPEARANCE, "theme",                theme);
    show_word_count      = boolv(GROUP_APPEARANCE, "show-word-count",      show_word_count);
    show_reading_time    = boolv(GROUP_APPEARANCE, "show-reading-time",    show_reading_time);
    show_paragraph_marks = boolv(GROUP_APPEARANCE, "show-paragraph-marks", show_paragraph_marks);
    show_line_numbers    = boolv(GROUP_APPEARANCE, "show-line-numbers",    show_line_numbers);
    show_annotations     = boolv(GROUP_APPEARANCE, "show-annotations",     show_annotations);
    show_links           = boolv(GROUP_APPEARANCE, "show-links",           show_links);
    show_invisibles      = boolv(GROUP_APPEARANCE, "show-invisibles",      show_invisibles);

    auto_save              = boolv(GROUP_AUTOSAVE, "auto-save",         auto_save);
    auto_save_interval_min = intv (GROUP_AUTOSAVE, "interval-min",      auto_save_interval_min);
    save_on_close          = boolv(GROUP_AUTOSAVE, "save-on-close",     save_on_close);
    if (auto_save_interval_min < 1)   auto_save_interval_min = 1;
    if (auto_save_interval_min > 360) auto_save_interval_min = 360;

    backup_enabled        = boolv(GROUP_AUTOSAVE, "backup-enabled",        backup_enabled);
    backup_interval_hours = intv (GROUP_AUTOSAVE, "backup-interval-hours", backup_interval_hours);
    backup_max_count      = intv (GROUP_AUTOSAVE, "backup-max-count",      backup_max_count);
    backup_dir            = str  (GROUP_AUTOSAVE, "backup-dir",            backup_dir);
    if (backup_interval_hours < 0)  backup_interval_hours = 0;
    if (backup_interval_hours > 24) backup_interval_hours = 24;
    if (backup_max_count < 3)       backup_max_count = 3;
    if (backup_max_count > 50)      backup_max_count = 50;

    // Statuses — dynamic vector
    statuses.clear();
    for (int i = 0; ; ++i) {
        std::string nkey = "status-" + std::to_string(i) + "-name";
        char* v = g_key_file_get_string(kf, GROUP_STATUSES, nkey.c_str(), nullptr);
        if (!v) break;
        StatusDef sd; sd.name = v; g_free(v);
        std::string ckey = "status-" + std::to_string(i) + "-color";
        char* cv = g_key_file_get_string(kf, GROUP_STATUSES, ckey.c_str(), nullptr);
        if (cv) { sd.color_hex = cv; g_free(cv); }
        statuses.push_back(sd);
    }
    if (statuses.empty())
        statuses = {{"Rough Draft","#f9e2af"},{"In Progress","#89b4fa"},{"Polished","#a6e3a1"},{"Skip","#6c7086"}};

    // Character roles — dynamic vector
    character_roles.clear();
    for (int i = 0; ; ++i) {
        std::string key = "role-" + std::to_string(i);
        char* v = g_key_file_get_string(kf, GROUP_ROLES, key.c_str(), nullptr);
        if (!v) break;
        character_roles.push_back(std::string(v));
        g_free(v);
    }
    if (character_roles.empty())
        character_roles = {"Protagonist", "Antagonist", "Supporting", "Minor"};

    // Genres — dynamic vector
    genres.clear();
    for (int i = 0; ; ++i) {
        std::string key = "genre-" + std::to_string(i);
        char* v = g_key_file_get_string(kf, GROUP_GENRES, key.c_str(), nullptr);
        if (!v) break;
        genres.push_back(std::string(v));
        g_free(v);
    }
    if (genres.empty())
        genres = {"Literary Fiction", "Science Fiction", "Fantasy", "Mystery",
                  "Thriller", "Romance", "Horror", "Historical Fiction",
                  "Young Adult", "Memoir", "Non-Fiction"};

    // Tag colours — dynamic vector
    tag_colors.clear();
    for (int i = 0; ; ++i) {
        std::string nk = "tag-" + std::to_string(i) + "-name";
        std::string ck = "tag-" + std::to_string(i) + "-color";
        std::string ik = "tag-" + std::to_string(i) + "-id";   // s81: stable swatch id
        char* nv = g_key_file_get_string(kf, GROUP_TAGS, nk.c_str(), nullptr);
        if (!nv) break;
        char* cv = g_key_file_get_string(kf, GROUP_TAGS, ck.c_str(), nullptr);
        char* iv = g_key_file_get_string(kf, GROUP_TAGS, ik.c_str(), nullptr);
        TagColor tc;
        tc.name = nv; g_free(nv);
        tc.hex  = cv ? std::string(cv) : "#888888"; if (cv) g_free(cv);
        tc.id   = iv ? std::string(iv) : std::string(); if (iv) g_free(iv);  // "" pre-s81
        tag_colors.push_back(tc);
    }
    if (tag_colors.empty())
        tag_colors = {
            {"teal","#5bc8af"},{"yellow","#f9e2af"},{"red","#f38ba8"},
            {"green","#a6e3a1"},{"mauve","#cba6f7"},{"peach","#fab387"},{"sky","#89dceb"}
        };

    // s81: back-fill stable ids for any pre-s81 swatch that lacks one (the field
    // was added this session). Resolving a scene's KP by id (KpPalette) needs
    // every swatch to carry one; idempotent, so a re-save makes it permanent.
    {
        std::vector<KpSwatch> sw;
        sw.reserve(tag_colors.size());
        for (const auto& tc : tag_colors) sw.push_back({tc.id, tc.name, tc.hex});
        if (backfill_swatch_ids(sw, [] { return make_iid(IidKind::KeyPoint); }) > 0)
            for (std::size_t i = 0; i < tag_colors.size(); ++i)
                tag_colors[i].id = sw[i].id;
    }

    daily_word_goal  = intv(GROUP_SESSION, "daily-word-goal", daily_word_goal);

    // ── Editing: spell check ──────────────────────────────────────────────────
    spell_check_enabled    = boolv(GROUP_EDITING, "spell-check-enabled",    spell_check_enabled);
    spell_language         = str  (GROUP_EDITING, "spell-language",         spell_language);
    spell_underline_color  = str  (GROUP_EDITING, "spell-underline-color",  spell_underline_color);
    spell_underline_bold   = boolv(GROUP_EDITING, "spell-underline-bold",   spell_underline_bold);
    spell_background_tint  = boolv(GROUP_EDITING, "spell-background-tint",  spell_background_tint);
    spell_background_color = str  (GROUP_EDITING, "spell-background-color", spell_background_color);
    spell_underline_style  = str  (GROUP_EDITING, "spell-underline-style",  spell_underline_style);

    // ── Editing: text substitution ────────────────────────────────────────────
    sub_smart_quotes  = boolv(GROUP_EDITING, "sub-smart-quotes",  sub_smart_quotes);
    sub_em_dash       = boolv(GROUP_EDITING, "sub-em-dash",       sub_em_dash);
    sub_ellipsis      = boolv(GROUP_EDITING, "sub-ellipsis",      sub_ellipsis);
    sub_autocorrect   = boolv(GROUP_EDITING, "sub-autocorrect",   sub_autocorrect);

    autocorrect_pairs.clear();
    for (int i = 0; ; ++i) {
        std::string from_key = "autocorrect-" + std::to_string(i) + "-from";
        char* from_v = g_key_file_get_string(kf, GROUP_EDITING, from_key.c_str(), nullptr);
        if (!from_v) break;
        std::string to_key = "autocorrect-" + std::to_string(i) + "-to";
        char* to_v = g_key_file_get_string(kf, GROUP_EDITING, to_key.c_str(), nullptr);
        autocorrect_pairs.push_back({std::string(from_v), to_v ? std::string(to_v) : ""});
        g_free(from_v);
        if (to_v) g_free(to_v);
    }
    if (autocorrect_pairs.empty()) {
        autocorrect_pairs = FolioPrefs{}.autocorrect_pairs;
    } else {
        // Merge any new default pairs that aren't already present (one-way migration).
        // Preserves all user customisations; only appends genuinely new "from" keys.
        const auto& defaults = FolioPrefs{}.autocorrect_pairs;
        for (const auto& def : defaults) {
            bool found = false;
            for (const auto& existing : autocorrect_pairs)
                if (existing.first == def.first) { found = true; break; }
            if (!found)
                autocorrect_pairs.push_back(def);
        }
    }
    // Pomodoro
    pomodoro.focus_min            = intv (GROUP_POMODORO, "focus-min",             pomodoro.focus_min);
    pomodoro.short_break_min      = intv (GROUP_POMODORO, "short-break-min",       pomodoro.short_break_min);
    pomodoro.long_break_min       = intv (GROUP_POMODORO, "long-break-min",        pomodoro.long_break_min);
    pomodoro.sessions_before_long = intv (GROUP_POMODORO, "sessions-before-long",  pomodoro.sessions_before_long);
    pomodoro.auto_start           = boolv(GROUP_POMODORO, "auto-start",            pomodoro.auto_start);
    pomodoro.show_in_headerbar    = boolv(GROUP_POMODORO, "show-in-headerbar",     pomodoro.show_in_headerbar);
    pomodoro.focus_color          = str  (GROUP_POMODORO, "focus-color",           pomodoro.focus_color);
    pomodoro.short_break_color    = str  (GROUP_POMODORO, "short-break-color",     pomodoro.short_break_color);
    pomodoro.long_break_color     = str  (GROUP_POMODORO, "long-break-color",      pomodoro.long_break_color);
    pomodoro.pip_color            = str  (GROUP_POMODORO, "pip-color",             pomodoro.pip_color);
    if (pomodoro.focus_min        < 1)  pomodoro.focus_min        = 1;
    if (pomodoro.short_break_min  < 1)  pomodoro.short_break_min  = 1;
    if (pomodoro.long_break_min   < 1)  pomodoro.long_break_min   = 1;
    if (pomodoro.sessions_before_long < 1) pomodoro.sessions_before_long = 1;

    // Text styles — dynamic vector
    text_styles.clear();
    for (int i = 0; ; ++i) {
        std::string pfx = "style-" + std::to_string(i) + "-";
        char* nv = g_key_file_get_string(kf, GROUP_STYLES, (pfx + "name").c_str(), nullptr);
        if (!nv) break;
        TextStyle ts;
        ts.name = nv; g_free(nv);
        auto sv = [&](const char* k) -> std::string {
            char* v = g_key_file_get_string(kf, GROUP_STYLES, (pfx + k).c_str(), nullptr);
            if (!v) return "";
            std::string s(v); g_free(v); return s;
        };
        ts.kind          = sv("kind");           if (ts.kind.empty()) ts.kind = "paragraph";
        ts.font_family   = sv("font-family");
        ts.font_size     = intv(GROUP_STYLES, (pfx + "font-size").c_str(), 0);
        ts.bold          = boolv(GROUP_STYLES, (pfx + "bold").c_str(), false);
        ts.italic        = boolv(GROUP_STYLES, (pfx + "italic").c_str(), false);
        ts.underline     = boolv(GROUP_STYLES, (pfx + "underline").c_str(), false);
        ts.justification = sv("justification");
        ts.fg_color      = sv("fg-color");
        ts.bg_color      = sv("bg-color");
        ts.line_height   = g_key_file_get_double(kf, GROUP_STYLES, (pfx + "line-height").c_str(), nullptr);
        text_styles.push_back(ts);
    }
    // If no styles were saved yet, seed with the built-in defaults and save
    if (text_styles.empty()) {
        text_styles = default_styles();
        try { save(); } catch (...) {}
    }

    // Custom compile formats (s18) — each in its own group "CompileFormat-N".
    // The IO seam owns the key vocabulary; we just read every key in the group
    // into a map and hand it to compile_format_from_kv. Stop at the first
    // missing group (formats are written contiguously from index 0).
    custom_compile_formats.clear();
    for (int i = 0; ; ++i) {
        std::string grp = std::string(GROUP_COMPILE_FORMATS) + "-" + std::to_string(i);
        if (!g_key_file_has_group(kf, grp.c_str())) break;
        gsize n_keys = 0;
        gchar** keys = g_key_file_get_keys(kf, grp.c_str(), &n_keys, nullptr);
        std::map<std::string, std::string> m;
        if (keys) {
            for (gsize k = 0; k < n_keys; ++k) {
                char* v = g_key_file_get_string(kf, grp.c_str(), keys[k], nullptr);
                if (v) { m[keys[k]] = v; g_free(v); }
            }
            g_strfreev(keys);
        }
        if (m.empty()) continue;
        CompileFormat cf = compile_format_from_kv(m);
        cf.builtin = false;   // anything loaded from prefs is, by definition, custom
        custom_compile_formats.push_back(std::move(cf));
    }

    // Node creation defaults
    auto load_nd = [&](const char* prefix, NodeDefaults& nd) {
        std::string p(prefix);
        nd.title             = str  (GROUP_DEFAULTS, (p + "-title").c_str(),       "");
        nd.status_name       = str  (GROUP_DEFAULTS, (p + "-status").c_str(),      "");
        nd.role_name         = str  (GROUP_DEFAULTS, (p + "-role").c_str(),        "");
        nd.color_idx         = intv (GROUP_DEFAULTS, (p + "-color-idx").c_str(),   0);
        nd.include_in_export = boolv(GROUP_DEFAULTS, (p + "-export").c_str(),      true);
        nd.word_target       = intv (GROUP_DEFAULTS, (p + "-word-target").c_str(), 0);
        nd.template_name              = str  (GROUP_DEFAULTS, (p + "-tpl-name").c_str(),         "");
        nd.template_copy_title        = boolv(GROUP_DEFAULTS, (p + "-tpl-copy-title").c_str(),   false);
        nd.template_copy_color        = boolv(GROUP_DEFAULTS, (p + "-tpl-copy-color").c_str(),   false);
        nd.template_copy_status       = boolv(GROUP_DEFAULTS, (p + "-tpl-copy-status").c_str(),  false);
        nd.template_copy_word_target  = boolv(GROUP_DEFAULTS, (p + "-tpl-copy-wt").c_str(),      false);
    };
    load_nd("scene",      scene_defaults);
    load_nd("group",      group_defaults);
    load_nd("character",  character_defaults);
    load_nd("char-group", char_group_defaults);
    load_nd("place",      place_defaults);
    load_nd("place-group",place_group_defaults);
    load_nd("reference",  reference_defaults);
    load_nd("template",   template_defaults);

    reopen_last_file    = boolv(GROUP_STARTUP, "reopen-last-file", reopen_last_file);
    max_recent_files    = intv (GROUP_STARTUP, "max-recent-files", max_recent_files);
    last_export_folder  = str  (GROUP_STARTUP, "last-export-folder", last_export_folder.c_str());
    {
        // Stored as semicolon-joined string; default is "---;* * *"
        std::string joined_default;
        for (auto& s : split_separators) {
            if (!joined_default.empty()) joined_default += ";";
            joined_default += s;
        }
        std::string joined = str(GROUP_STARTUP, "split-separators", joined_default.c_str());
        split_separators.clear();
        std::istringstream ss(joined);
        std::string tok;
        while (std::getline(ss, tok, ';')) {
            size_t a = tok.find_first_not_of(" \t");
            size_t b = tok.find_last_not_of(" \t");
            if (a != std::string::npos && !tok.empty())
                split_separators.push_back(tok.substr(a, b - a + 1));
        }
        if (split_separators.empty()) split_separators = {"---"};
    }
    if (max_recent_files < 1)  max_recent_files = 1;
    if (max_recent_files > MAX_RECENT) max_recent_files = MAX_RECENT;
    recent_files.clear();
    for (int i = 0; i < MAX_RECENT; ++i) {
        std::string key = "recent-" + std::to_string(i);
        char* v = g_key_file_get_string(kf, GROUP_STARTUP, key.c_str(), nullptr);
        if (!v) break;
        std::string p(v); g_free(v);
        if (!p.empty()) recent_files.push_back(p);
    }

    window_width     = intv (GROUP_WINDOW, "width",     window_width);
    window_height    = intv (GROUP_WINDOW, "height",    window_height);
    window_maximized = boolv(GROUP_WINDOW, "maximized", window_maximized);
    binder_visible   = boolv(GROUP_WINDOW, "binder-visible",    binder_visible);
    inspector_visible   = boolv(GROUP_WINDOW, "inspector-visible",     inspector_visible);
    binder_width        = intv (GROUP_WINDOW, "binder-width",          binder_width);
    paned_right_pos     = intv (GROUP_WINDOW, "paned-right-pos",       paned_right_pos);
    notes_anno_pane_pos = intv (GROUP_WINDOW, "notes-anno-pane-pos",   notes_anno_pane_pos);

    sidebar_sec_manuscript_expanded = boolv(GROUP_WINDOW, "sidebar-sec-manuscript", sidebar_sec_manuscript_expanded);
    sidebar_sec_characters_expanded = boolv(GROUP_WINDOW, "sidebar-sec-characters", sidebar_sec_characters_expanded);
    sidebar_sec_places_expanded     = boolv(GROUP_WINDOW, "sidebar-sec-places",     sidebar_sec_places_expanded);
    sidebar_sec_references_expanded = boolv(GROUP_WINDOW, "sidebar-sec-references", sidebar_sec_references_expanded);
    sidebar_sec_templates_expanded  = boolv(GROUP_WINDOW, "sidebar-sec-templates",  sidebar_sec_templates_expanded);
    sidebar_sec_trash_expanded      = boolv(GROUP_WINDOW, "sidebar-sec-trash",      sidebar_sec_trash_expanded);
    sidebar_pomo_tile_expanded      = boolv(GROUP_WINDOW, "sidebar-pomo-tile",      sidebar_pomo_tile_expanded);
    sidebar_session_tile_expanded   = boolv(GROUP_WINDOW, "sidebar-session-tile",   sidebar_session_tile_expanded);
    inspector_progress_expanded          = boolv(GROUP_WINDOW, "inspector-progress",          inspector_progress_expanded);
    inspector_proj_project_expanded      = boolv(GROUP_WINDOW, "inspector-proj-project",      inspector_proj_project_expanded);
    inspector_proj_synopsis_expanded     = boolv(GROUP_WINDOW, "inspector-proj-synopsis",     inspector_proj_synopsis_expanded);
    inspector_proj_publication_expanded  = boolv(GROUP_WINDOW, "inspector-proj-publication",  inspector_proj_publication_expanded);
    inspector_proj_cover_expanded        = boolv(GROUP_WINDOW, "inspector-proj-cover",        inspector_proj_cover_expanded);
    inspector_proj_goals_expanded        = boolv(GROUP_WINDOW, "inspector-proj-goals",        inspector_proj_goals_expanded);
    inspector_meta_node_identity_expanded = boolv(GROUP_WINDOW, "inspector-meta-node-identity", inspector_meta_node_identity_expanded);
    inspector_meta_node_synopsis_expanded = boolv(GROUP_WINDOW, "inspector-meta-node-synopsis", inspector_meta_node_synopsis_expanded);
    inspector_meta_node_status_expanded   = boolv(GROUP_WINDOW, "inspector-meta-node-status",   inspector_meta_node_status_expanded);
    inspector_meta_node_label_expanded    = boolv(GROUP_WINDOW, "inspector-meta-node-label",    inspector_meta_node_label_expanded);
    inspector_meta_node_scene_expanded    = boolv(GROUP_WINDOW, "inspector-meta-node-scene",    inspector_meta_node_scene_expanded);
    inspector_meta_char_identity_expanded = boolv(GROUP_WINDOW, "inspector-meta-char-identity", inspector_meta_char_identity_expanded);
    inspector_meta_char_tagline_expanded  = boolv(GROUP_WINDOW, "inspector-meta-char-tagline",  inspector_meta_char_tagline_expanded);
    inspector_meta_char_colour_expanded   = boolv(GROUP_WINDOW, "inspector-meta-char-colour",   inspector_meta_char_colour_expanded);
    inspector_meta_place_identity_expanded    = boolv(GROUP_WINDOW, "inspector-meta-place-identity",    inspector_meta_place_identity_expanded);
    inspector_meta_place_description_expanded = boolv(GROUP_WINDOW, "inspector-meta-place-description", inspector_meta_place_description_expanded);
    inspector_meta_place_colour_expanded      = boolv(GROUP_WINDOW, "inspector-meta-place-colour",      inspector_meta_place_colour_expanded);
    inspector_meta_ref_reference_expanded = boolv(GROUP_WINDOW, "inspector-meta-ref-reference", inspector_meta_ref_reference_expanded);
    inspector_meta_ref_notes_expanded     = boolv(GROUP_WINDOW, "inspector-meta-ref-notes",     inspector_meta_ref_notes_expanded);
    if (binder_width     < 160)  binder_width     = 160;
    if (binder_width     > 600)  binder_width     = 600;
    if (paned_right_pos  < 200)  paned_right_pos  = 200;
    if (window_width  < 400) window_width  = 400;
    if (window_height < 300) window_height = 300;

    // Global templates — stored as a JSON array string
    global_templates_json.clear();
    if (g_key_file_has_group(kf, GROUP_GLOBAL_TEMPLATES)) {
        char* raw = g_key_file_get_string(kf, GROUP_GLOBAL_TEMPLATES, "templates", nullptr);
        if (raw) { global_templates_json = raw; g_free(raw); }
    }

    g_key_file_free(kf);
}

void FolioPrefs::save() const {
    std::string path = config_path();
    std::string dir  = Glib::path_get_dirname(path);
    g_mkdir_with_parents(dir.c_str(), 0700);

    GKeyFile* kf = g_key_file_new();
    g_key_file_set_comment(kf, nullptr, nullptr,
        " Folio preferences — edited automatically. Format: GLib KeyFile.\n"
        " You may edit this file manually while Folio is not running.", nullptr);

    g_key_file_set_string (kf, GROUP_TYPOGRAPHY, "editor-font",          editor_font.c_str());
    g_key_file_set_integer(kf, GROUP_TYPOGRAPHY, "editor-font-size",     editor_font_size);
    g_key_file_set_string (kf, GROUP_TYPOGRAPHY, "ui-font",              ui_font.c_str());
    g_key_file_set_integer(kf, GROUP_TYPOGRAPHY, "ui-font-size",         ui_font_size);
    g_key_file_set_double (kf, GROUP_TYPOGRAPHY, "line-spacing",         line_spacing);
    g_key_file_set_boolean(kf, GROUP_TYPOGRAPHY, "first-line-indent",    first_line_indent);
    g_key_file_set_integer(kf, GROUP_TYPOGRAPHY, "first-line-indent-px", first_line_indent_px);
    g_key_file_set_integer(kf, GROUP_TYPOGRAPHY, "paragraph-spacing-px", paragraph_spacing_px);
    g_key_file_set_string (kf, GROUP_TYPOGRAPHY, "serif-font",           serif_font.c_str());
    g_key_file_set_string (kf, GROUP_TYPOGRAPHY, "sans-font",            sans_font.c_str());
    g_key_file_set_string (kf, GROUP_TYPOGRAPHY, "mono-font",            mono_font.c_str());

    // Heading styles
    g_key_file_set_integer(kf, GROUP_HEADINGS, "outline-levels", outline_levels);
    for (int i = 0; i < MAX_OUTLINE_LEVELS; ++i) {
        const auto& hs = heading_styles[i];
        std::string p = "h" + std::to_string(i + 1) + "-";
        g_key_file_set_double (kf, GROUP_HEADINGS, (p + "font-scale").c_str(),   hs.font_scale);
        g_key_file_set_integer(kf, GROUP_HEADINGS, (p + "font-size-pt").c_str(), hs.font_size_pt);
        g_key_file_set_boolean(kf, GROUP_HEADINGS, (p + "bold").c_str(),         hs.bold);
        g_key_file_set_boolean(kf, GROUP_HEADINGS, (p + "italic").c_str(),       hs.italic);
        g_key_file_set_string (kf, GROUP_HEADINGS, (p + "color").c_str(),        hs.color_hex.c_str());
        g_key_file_set_integer(kf, GROUP_HEADINGS, (p + "space-above").c_str(),  hs.space_above_px);
        g_key_file_set_integer(kf, GROUP_HEADINGS, (p + "space-below").c_str(),  hs.space_below_px);
        g_key_file_set_string (kf, GROUP_HEADINGS, (p + "marker").c_str(),       hs.marker.c_str());
        g_key_file_set_string (kf, GROUP_HEADINGS, (p + "separator").c_str(),    hs.separator.c_str());
    }

    // Screenplay
    {
        std::string cycle;
        for (size_t i = 0; i < screenplay_tab_cycle.size(); ++i) {
            if (i) cycle += ",";
            cycle += screenplay_tab_cycle[i];
        }
        g_key_file_set_string(kf, GROUP_SCREENPLAY, "tab-cycle", cycle.c_str());
    }

    g_key_file_set_integer(kf, GROUP_EDITOR, "typewriter-width-chars",   typewriter_width_chars);
    g_key_file_set_boolean(kf, GROUP_EDITOR, "typewriter-mode",          typewriter_mode);
    g_key_file_set_double (kf, GROUP_EDITOR, "typewriter-position",      typewriter_position);
    g_key_file_set_boolean(kf, GROUP_EDITOR, "focus-mode-dim",           focus_mode_dim);
    g_key_file_set_integer(kf, GROUP_EDITOR, "page-width-pct",           editor_page_width_pct);
    g_key_file_set_integer(kf, GROUP_EDITOR, "page-margin-px",           editor_page_margin_px);
    g_key_file_set_integer(kf, GROUP_EDITOR, "left-margin-px",           editor_left_margin_px);
    g_key_file_set_integer(kf, GROUP_EDITOR, "right-margin-px",          editor_right_margin_px);
    g_key_file_set_boolean(kf, GROUP_EDITOR, "margins-linked",           editor_margins_linked);
    g_key_file_set_boolean(kf, GROUP_EDITOR, "show-ruler",               show_ruler);
    g_key_file_set_string (kf, GROUP_EDITOR, "ruler-unit",               ruler_unit.c_str());
    g_key_file_set_string (kf, GROUP_EDITOR, "ruler-tab-type",           ruler_tab_type.c_str());
    {
        std::ostringstream ss;
        for (size_t i = 0; i < tab_stops.size(); ++i) {
            if (i) ss << ' ';
            ss << tab_stops[i].position_pt << ':' << tab_stops[i].type;
        }
        g_key_file_set_string(kf, GROUP_EDITOR, "tab-stops", ss.str().c_str());
    }
  g_key_file_set_integer(kf, GROUP_EDITOR, "focus-page-width-pct",     focus_page_width_pct);
    g_key_file_set_boolean(kf, GROUP_EDITOR, "focus-typewriter-mode",  focus_typewriter_mode);
    g_key_file_set_boolean(kf, GROUP_EDITOR, "focus-show-line-numbers", focus_show_line_numbers);
    g_key_file_set_boolean(kf, GROUP_EDITOR, "focus-show-invisibles",  focus_show_invisibles);
    g_key_file_set_integer(kf, GROUP_EDITOR, "focus-zoom-pct",         focus_zoom_pct);
    g_key_file_set_integer(kf, GROUP_EDITOR, "focus-page-margin-px",   focus_page_margin_px);
    g_key_file_set_string (kf, GROUP_EDITOR, "focus-font",           focus_font.c_str());
    g_key_file_set_integer(kf, GROUP_EDITOR, "focus-font-size",      focus_font_size);
    g_key_file_set_double (kf, GROUP_EDITOR, "focus-line-spacing",   focus_line_spacing);
    g_key_file_set_string (kf, GROUP_EDITOR, "focus-text-color",     focus_text_color.c_str());
    g_key_file_set_string (kf, GROUP_EDITOR, "focus-background-path", focus_background_path.c_str());
    g_key_file_set_double (kf, GROUP_EDITOR, "focus-background-dim",  focus_background_dim);
    g_key_file_set_double (kf, GROUP_EDITOR, "focus-panel-opacity",   focus_panel_opacity);
    g_key_file_set_string (kf, GROUP_EDITOR, "focus-panel-color",     focus_panel_color.c_str());
    g_key_file_set_integer(kf, GROUP_EDITOR, "zoom-pct",                 editor_zoom_pct);
    g_key_file_set_boolean(kf, GROUP_EDITOR, "header-visible",           editor_header_visible);

    g_key_file_set_integer(kf, GROUP_GALLERY, "image-max-dim",   gallery_image_max_dim);
    g_key_file_set_integer(kf, GROUP_GALLERY, "base-long-edge",  gallery_base_long_edge);
    g_key_file_set_integer(kf, GROUP_GALLERY, "default-detail-tier", gallery_default_detail_tier);
    g_key_file_set_integer(kf, GROUP_GALLERY, "image-quality",   gallery_image_quality);
    g_key_file_set_integer(kf, GROUP_GALLERY, "thumb-max-dim",   gallery_thumb_max_dim);
    g_key_file_set_integer(kf, GROUP_GALLERY, "import-max-mb",    gallery_import_max_mb);
    g_key_file_set_boolean(kf, GROUP_GALLERY, "prefer-lossless",  gallery_prefer_lossless);
    g_key_file_set_boolean(kf, GROUP_GALLERY, "allow-url-fetch",  gallery_allow_url_fetch);


    g_key_file_set_string (kf, GROUP_APPEARANCE, "theme",                theme.c_str());
    g_key_file_set_boolean(kf, GROUP_APPEARANCE, "show-word-count",      show_word_count);
    g_key_file_set_boolean(kf, GROUP_APPEARANCE, "show-reading-time",    show_reading_time);
    g_key_file_set_boolean(kf, GROUP_APPEARANCE, "show-paragraph-marks", show_paragraph_marks);
    g_key_file_set_boolean(kf, GROUP_APPEARANCE, "show-line-numbers",    show_line_numbers);
    g_key_file_set_boolean(kf, GROUP_APPEARANCE, "show-annotations",     show_annotations);
    g_key_file_set_boolean(kf, GROUP_APPEARANCE, "show-links",           show_links);
    g_key_file_set_boolean(kf, GROUP_APPEARANCE, "show-invisibles",      show_invisibles);

    g_key_file_set_boolean(kf, GROUP_AUTOSAVE, "auto-save",         auto_save);
    g_key_file_set_integer(kf, GROUP_AUTOSAVE, "interval-min",      auto_save_interval_min);
    g_key_file_set_boolean(kf, GROUP_AUTOSAVE, "save-on-close",     save_on_close);
    g_key_file_set_boolean(kf, GROUP_AUTOSAVE, "backup-enabled",        backup_enabled);
    g_key_file_set_integer(kf, GROUP_AUTOSAVE, "backup-interval-hours", backup_interval_hours);
    g_key_file_set_integer(kf, GROUP_AUTOSAVE, "backup-max-count",      backup_max_count);
    g_key_file_set_string (kf, GROUP_AUTOSAVE, "backup-dir",            backup_dir.c_str());

    // Statuses
    for (int i = 0; i < (int)statuses.size(); ++i) {
        std::string nkey = "status-" + std::to_string(i) + "-name";
        std::string ckey = "status-" + std::to_string(i) + "-color";
        g_key_file_set_string(kf, GROUP_STATUSES, nkey.c_str(), statuses[i].name.c_str());
        g_key_file_set_string(kf, GROUP_STATUSES, ckey.c_str(), statuses[i].color_hex.c_str());
    }

    // Character roles
    for (int i = 0; i < (int)character_roles.size(); ++i) {
        std::string key = "role-" + std::to_string(i);
        g_key_file_set_string(kf, GROUP_ROLES, key.c_str(), character_roles[i].c_str());
    }

    // Genres
    for (int i = 0; i < (int)genres.size(); ++i) {
        std::string key = "genre-" + std::to_string(i);
        g_key_file_set_string(kf, GROUP_GENRES, key.c_str(), genres[i].c_str());
    }

    // Tag colours
    for (int i = 0; i < (int)tag_colors.size(); ++i) {
        std::string nk = "tag-" + std::to_string(i) + "-name";
        std::string ck = "tag-" + std::to_string(i) + "-color";
        std::string ik = "tag-" + std::to_string(i) + "-id";   // s81: stable swatch id
        g_key_file_set_string(kf, GROUP_TAGS, nk.c_str(), tag_colors[i].name.c_str());
        g_key_file_set_string(kf, GROUP_TAGS, ck.c_str(), tag_colors[i].hex.c_str());
        g_key_file_set_string(kf, GROUP_TAGS, ik.c_str(), tag_colors[i].id.c_str());
    }

    g_key_file_set_integer(kf, GROUP_SESSION, "daily-word-goal", daily_word_goal);

    // ── Editing: spell check ──────────────────────────────────────────────────
    g_key_file_set_boolean(kf, GROUP_EDITING, "spell-check-enabled",    spell_check_enabled);
    g_key_file_set_string (kf, GROUP_EDITING, "spell-language",         spell_language.c_str());
    g_key_file_set_string (kf, GROUP_EDITING, "spell-underline-color",  spell_underline_color.c_str());
    g_key_file_set_boolean(kf, GROUP_EDITING, "spell-underline-bold",   spell_underline_bold);
    g_key_file_set_boolean(kf, GROUP_EDITING, "spell-background-tint",  spell_background_tint);
    g_key_file_set_string (kf, GROUP_EDITING, "spell-background-color", spell_background_color.c_str());
    g_key_file_set_string (kf, GROUP_EDITING, "spell-underline-style",  spell_underline_style.c_str());

    // ── Editing: text substitution ────────────────────────────────────────────
    g_key_file_set_boolean(kf, GROUP_EDITING, "sub-smart-quotes",  sub_smart_quotes);
    g_key_file_set_boolean(kf, GROUP_EDITING, "sub-em-dash",       sub_em_dash);
    g_key_file_set_boolean(kf, GROUP_EDITING, "sub-ellipsis",      sub_ellipsis);
    g_key_file_set_boolean(kf, GROUP_EDITING, "sub-autocorrect",   sub_autocorrect);
    for (int i = 0; i < (int)autocorrect_pairs.size(); ++i) {
        std::string from_key = "autocorrect-" + std::to_string(i) + "-from";
        std::string to_key   = "autocorrect-" + std::to_string(i) + "-to";
        g_key_file_set_string(kf, GROUP_EDITING, from_key.c_str(), autocorrect_pairs[i].first.c_str());
        g_key_file_set_string(kf, GROUP_EDITING, to_key.c_str(),   autocorrect_pairs[i].second.c_str());
    }

    // Pomodoro
    g_key_file_set_integer(kf, GROUP_POMODORO, "focus-min",             pomodoro.focus_min);
    g_key_file_set_integer(kf, GROUP_POMODORO, "short-break-min",       pomodoro.short_break_min);
    g_key_file_set_integer(kf, GROUP_POMODORO, "long-break-min",        pomodoro.long_break_min);
    g_key_file_set_integer(kf, GROUP_POMODORO, "sessions-before-long",  pomodoro.sessions_before_long);
    g_key_file_set_boolean(kf, GROUP_POMODORO, "auto-start",            pomodoro.auto_start);
    g_key_file_set_boolean(kf, GROUP_POMODORO, "show-in-headerbar",     pomodoro.show_in_headerbar);
    g_key_file_set_string (kf, GROUP_POMODORO, "focus-color",           pomodoro.focus_color.c_str());
    g_key_file_set_string (kf, GROUP_POMODORO, "short-break-color",     pomodoro.short_break_color.c_str());
    g_key_file_set_string (kf, GROUP_POMODORO, "long-break-color",      pomodoro.long_break_color.c_str());
    g_key_file_set_string (kf, GROUP_POMODORO, "pip-color",             pomodoro.pip_color.c_str());

    // Text styles
    for (int i = 0; i < (int)text_styles.size(); ++i) {
        const auto& ts = text_styles[i];
        std::string pfx = "style-" + std::to_string(i) + "-";
        auto sk = [&](const char* k, const std::string& v) {
            g_key_file_set_string(kf, GROUP_STYLES, (pfx+k).c_str(), v.c_str());
        };
        sk("name",          ts.name);
        sk("kind",          ts.kind);
        sk("font-family",   ts.font_family);
        g_key_file_set_integer(kf, GROUP_STYLES, (pfx+"font-size").c_str(),   ts.font_size);
        g_key_file_set_boolean(kf, GROUP_STYLES, (pfx+"bold").c_str(),        ts.bold);
        g_key_file_set_boolean(kf, GROUP_STYLES, (pfx+"italic").c_str(),      ts.italic);
        g_key_file_set_boolean(kf, GROUP_STYLES, (pfx+"underline").c_str(),   ts.underline);
        sk("justification", ts.justification);
        sk("fg-color",      ts.fg_color);
        sk("bg-color",      ts.bg_color);
        g_key_file_set_double(kf, GROUP_STYLES,  (pfx+"line-height").c_str(), ts.line_height);
    }

    // Custom compile formats (s18) — one group "CompileFormat-N" per format.
    // Remove any stale groups first (so a deleted format doesn't linger), then
    // write each format's non-default key/value pairs from the IO seam.
    for (int i = 0; ; ++i) {
        std::string grp = std::string(GROUP_COMPILE_FORMATS) + "-" + std::to_string(i);
        if (!g_key_file_has_group(kf, grp.c_str())) break;
        g_key_file_remove_group(kf, grp.c_str(), nullptr);
    }
    for (int i = 0; i < (int)custom_compile_formats.size(); ++i) {
        std::string grp = std::string(GROUP_COMPILE_FORMATS) + "-" + std::to_string(i);
        for (const auto& kv : compile_format_to_kv(custom_compile_formats[i]))
            g_key_file_set_string(kf, grp.c_str(), kv.first.c_str(), kv.second.c_str());
    }

    // Node creation defaults
    auto save_nd = [&](const char* prefix, const NodeDefaults& nd) {
        std::string p(prefix);
        g_key_file_set_string (kf, GROUP_DEFAULTS, (p + "-title").c_str(),       nd.title.c_str());
        g_key_file_set_string (kf, GROUP_DEFAULTS, (p + "-status").c_str(),      nd.status_name.c_str());
        g_key_file_set_string (kf, GROUP_DEFAULTS, (p + "-role").c_str(),        nd.role_name.c_str());
        g_key_file_set_integer(kf, GROUP_DEFAULTS, (p + "-color-idx").c_str(),   nd.color_idx);
        g_key_file_set_boolean(kf, GROUP_DEFAULTS, (p + "-export").c_str(),      nd.include_in_export);
        g_key_file_set_integer(kf, GROUP_DEFAULTS, (p + "-word-target").c_str(), nd.word_target);
        g_key_file_set_string (kf, GROUP_DEFAULTS, (p + "-tpl-name").c_str(),          nd.template_name.c_str());
        g_key_file_set_boolean(kf, GROUP_DEFAULTS, (p + "-tpl-copy-title").c_str(),    nd.template_copy_title);
        g_key_file_set_boolean(kf, GROUP_DEFAULTS, (p + "-tpl-copy-color").c_str(),    nd.template_copy_color);
        g_key_file_set_boolean(kf, GROUP_DEFAULTS, (p + "-tpl-copy-status").c_str(),   nd.template_copy_status);
        g_key_file_set_boolean(kf, GROUP_DEFAULTS, (p + "-tpl-copy-wt").c_str(),       nd.template_copy_word_target);
    };
    save_nd("scene",      scene_defaults);
    save_nd("group",      group_defaults);
    save_nd("character",  character_defaults);
    save_nd("char-group", char_group_defaults);
    save_nd("place",      place_defaults);
    save_nd("place-group",place_group_defaults);
    save_nd("reference",  reference_defaults);
    save_nd("template",   template_defaults);

    g_key_file_set_boolean(kf, GROUP_STARTUP, "reopen-last-file", reopen_last_file);
    g_key_file_set_integer(kf, GROUP_STARTUP, "max-recent-files", max_recent_files);
    g_key_file_set_string (kf, GROUP_STARTUP, "last-export-folder", last_export_folder.c_str());
    {
        std::string joined;
        for (auto& s : split_separators) {
            if (!joined.empty()) joined += ";";
            joined += s;
        }
        g_key_file_set_string(kf, GROUP_STARTUP, "split-separators", joined.c_str());
    }
    for (int i = 0; i < (int)recent_files.size(); ++i) {
        std::string key = "recent-" + std::to_string(i);
        g_key_file_set_string(kf, GROUP_STARTUP, key.c_str(), recent_files[i].c_str());
    }

    g_key_file_set_integer(kf, GROUP_WINDOW, "width",              window_width);
    g_key_file_set_integer(kf, GROUP_WINDOW, "height",             window_height);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "maximized",          window_maximized);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "binder-visible",     binder_visible);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "inspector-visible",    inspector_visible);
    g_key_file_set_integer(kf, GROUP_WINDOW, "binder-width",         binder_width);
    g_key_file_set_integer(kf, GROUP_WINDOW, "paned-right-pos",      paned_right_pos);
    g_key_file_set_integer(kf, GROUP_WINDOW, "notes-anno-pane-pos",  notes_anno_pane_pos);

    g_key_file_set_boolean(kf, GROUP_WINDOW, "sidebar-sec-manuscript", sidebar_sec_manuscript_expanded);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "sidebar-sec-characters", sidebar_sec_characters_expanded);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "sidebar-sec-places",     sidebar_sec_places_expanded);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "sidebar-sec-references", sidebar_sec_references_expanded);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "sidebar-sec-templates",  sidebar_sec_templates_expanded);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "sidebar-sec-trash",      sidebar_sec_trash_expanded);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "sidebar-pomo-tile",      sidebar_pomo_tile_expanded);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "sidebar-session-tile",   sidebar_session_tile_expanded);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "inspector-progress",          inspector_progress_expanded);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "inspector-proj-project",      inspector_proj_project_expanded);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "inspector-proj-synopsis",     inspector_proj_synopsis_expanded);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "inspector-proj-publication",  inspector_proj_publication_expanded);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "inspector-proj-cover",        inspector_proj_cover_expanded);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "inspector-proj-goals",        inspector_proj_goals_expanded);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "inspector-meta-node-identity", inspector_meta_node_identity_expanded);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "inspector-meta-node-synopsis", inspector_meta_node_synopsis_expanded);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "inspector-meta-node-status",   inspector_meta_node_status_expanded);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "inspector-meta-node-label",    inspector_meta_node_label_expanded);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "inspector-meta-node-scene",    inspector_meta_node_scene_expanded);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "inspector-meta-char-identity", inspector_meta_char_identity_expanded);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "inspector-meta-char-tagline",  inspector_meta_char_tagline_expanded);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "inspector-meta-char-colour",   inspector_meta_char_colour_expanded);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "inspector-meta-place-identity",    inspector_meta_place_identity_expanded);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "inspector-meta-place-description", inspector_meta_place_description_expanded);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "inspector-meta-place-colour",      inspector_meta_place_colour_expanded);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "inspector-meta-ref-reference", inspector_meta_ref_reference_expanded);
    g_key_file_set_boolean(kf, GROUP_WINDOW, "inspector-meta-ref-notes",     inspector_meta_ref_notes_expanded);

    // Global templates
    if (!global_templates_json.empty())
        g_key_file_set_string(kf, GROUP_GLOBAL_TEMPLATES, "templates",
                              global_templates_json.c_str());

    GError* err = nullptr;
    if (!g_key_file_save_to_file(kf, path.c_str(), &err)) {
        std::string msg = err ? err->message : "unknown error";
        if (err) g_error_free(err);
        g_key_file_free(kf);
        throw std::runtime_error("Could not save preferences to " + path + ": " + msg);
    }
    g_key_file_free(kf);
}

std::vector<BinderNode> FolioPrefs::global_templates_get() const {
    std::vector<BinderNode> result;
    if (global_templates_json.empty()) return result;
    try {
        auto arr = json::parse(global_templates_json);
        for (const auto& j : arr) {
            BinderNode n;
            n.from_json(j);
            result.push_back(std::move(n));
        }
    } catch (...) {}
    return result;
}

void FolioPrefs::global_templates_set(const std::vector<BinderNode>& templates) {
    json arr = json::array();
    for (const auto& n : templates)
        arr.push_back(n.to_json());
    global_templates_json = arr.dump();
}

// ─────────────────────────────────────────────────────────────────────────────
// Compile formats — builtins followed by the user's saved customs (s18)
// ─────────────────────────────────────────────────────────────────────────────
std::vector<CompileFormat> FolioPrefs::all_compile_formats() const {
    std::vector<CompileFormat> v = builtin_compile_formats();
    v.insert(v.end(), custom_compile_formats.begin(), custom_compile_formats.end());
    return v;
}

// ─────────────────────────────────────────────────────────────────────────────
// Built-in default style set
// ─────────────────────────────────────────────────────────────────────────────

std::vector<TextStyle> FolioPrefs::default_styles() const {
    const std::string& S = serif_font;   // e.g. "JansonText"
    const std::string& A = sans_font;    // e.g. "Cantarell"
    const std::string& M = mono_font;    // e.g. "Courier New"

    std::vector<TextStyle> v;

    // ── Paragraph styles ─────────────────────────────────────────────────────

    // Body Text — standard prose, first-line indent handled by editor setting
    v.push_back({"paragraph", "Body Text",        S, 12, false, false, false, "left",   "", "", 1.9});

    // Body Text First — opening paragraph of a section, no indent
    v.push_back({"paragraph", "Body Text First",  S, 12, false, false, false, "left",   "", "", 1.9});

    // Block Quote — indented both sides, slightly smaller
    v.push_back({"paragraph", "Block Quote",      S, 11, false, true,  false, "left",   "", "", 1.6});

    // Heading 1 — chapter title, centred, large
    v.push_back({"paragraph", "Heading 1",        S, 24, true,  false, false, "center", "", "", 1.2});

    // Heading 2 — scene / section heading
    v.push_back({"paragraph", "Heading 2",        S, 18, true,  false, false, "left",   "", "", 1.3});

    // Heading 3 — sub-section heading
    v.push_back({"paragraph", "Heading 3",        S, 14, true,  true,  false, "left",   "", "", 1.3});

    // Epigraph — attributed quote at chapter open, italic, right-aligned attribution handled by user
    v.push_back({"paragraph", "Epigraph",         S, 11, false, true,  false, "center", "", "", 1.6});

    // Screenplay: Scene Heading (slug line)
    v.push_back({"paragraph", "Scene Heading",    M, 12, true,  false, false, "left",   "", "", 1.0});

    // Screenplay: Action
    v.push_back({"paragraph", "Action",           M, 12, false, false, false, "left",   "", "", 1.0});

    // Screenplay: Character name (centred)
    v.push_back({"paragraph", "Character",        M, 12, true,  false, false, "center", "", "", 1.0});

    // Screenplay: Parenthetical
    v.push_back({"paragraph", "Parenthetical",    M, 12, false, false, false, "center", "", "", 1.0});

    // Screenplay: Dialogue
    v.push_back({"paragraph", "Dialogue",         M, 12, false, false, false, "left",   "", "", 1.0});

    // Screenplay: Transition (right-aligned)
    v.push_back({"paragraph", "Transition",       M, 12, true,  false, false, "right",  "", "", 1.0});

    // ── Character styles ─────────────────────────────────────────────────────

    // Emphasis — italic
    v.push_back({"character", "Emphasis",         "",  0, false, true,  false, "", "", "", 0.0});

    // Strong — bold
    v.push_back({"character", "Strong",           "",  0, true,  false, false, "", "", "", 0.0});

    // Strong Emphasis — bold italic
    v.push_back({"character", "Strong Emphasis",  "",  0, true,  true,  false, "", "", "", 0.0});

    // Underline
    v.push_back({"character", "Underline",        "",  0, false, false, true,  "", "", "", 0.0});

    // Code — inline monospace
    v.push_back({"character", "Code",             M,  11, false, false, false, "", "", "", 0.0});

    // Small Caps simulation — sans-serif, slightly smaller
    v.push_back({"character", "Small Caps",       A,  10, false, false, false, "", "", "", 0.0});

    return v;
}

} // namespace Folio
