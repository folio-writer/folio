#include "PreferencesDialog.hpp"
#include "color_utils.hpp"
#include "BackupManager.hpp"
#include "SpellChecker.hpp"
#include "UnicodePickerPopover.hpp"
#include <gdkmm/rgba.h>
#include <pango/pangocairo.h>
#include <sstream>
#include <iomanip>
#include <cmath>
#include <algorithm>

namespace Folio {

// ─── CSS ──────────────────────────────────────────────────────────────────────
static const char* PREFS_CSS = R"CSS(
.prefs-sidebar {
    background-color: @adw_surface;
    border-right: 1px solid @border_strong;
    min-width: 160px;
}
.prefs-sidebar > listbox { background-color: transparent; }
.prefs-sidebar > listbox > row {
    background-color: transparent;
    border-radius: 8px;
    padding: 8px 12px;
    min-height: 36px;
    margin: 2px 6px;
    border-bottom: none;
}
.prefs-sidebar > listbox > row:hover    { background-color: @adw_overlay; }
.prefs-sidebar > listbox > row:selected { background-color: @accent_dim; }
.prefs-sidebar > listbox > row:selected label { color: @accent; }
.prefs-nav-label { font-size: 13px; font-weight: 600; color: @tx2; }
.prefs-page { background-color: @adw_bg; padding: 24px 28px; }
.prefs-page-title { font-size: 18px; font-weight: 700; color: @tx1; margin-bottom: 16px; }
.prefs-reset-btn {
    background-color: transparent;
    border: 1px solid @border_subtle;
    border-radius: 8px;
    color: @tx3; font-size: 12px; padding: 4px 12px;
    min-height: 0; box-shadow: none; margin-top: 8px;
}
.prefs-reset-btn:hover { background-color: @adw_overlay; color: @tx1; }
.dyn-row {
    background-color: @adw_surface;
    border-radius: 8px;
    padding: 6px 10px;
    margin-bottom: 4px;
}
.dyn-add-btn {
    background-color: transparent;
    border: 1px dashed @border_subtle;
    border-radius: 8px;
    color: @tx3; font-size: 12px;
    padding: 4px 14px; min-height: 0; box-shadow: none;
    margin-top: 6px;
}
.dyn-add-btn:hover { background-color: @adw_overlay; color: @tx1; }
.dyn-del-btn {
    background-color: transparent; border: none; box-shadow: none;
    color: @tx3; padding: 0 6px; min-height: 0; font-size: 14px;
    min-width: 0;
}
.dyn-del-btn:hover { color: @error_color; }
)CSS";

static void ensure_prefs_css() {
    static bool done = false;
    if (done) return; done = true;
    auto css = Gtk::CssProvider::create();
    css->load_from_data(PREFS_CSS);
    Gtk::StyleContext::add_provider_for_display(
        Gdk::Display::get_default(), css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}

// ─── Colour helpers ───────────────────────────────────────────────────────────
static std::string rgba_to_hex(const Gdk::RGBA& c) {
    return Folio::color::to_hex(c);
}
static Gdk::RGBA hex_to_rgba(const std::string& hex) {
    Gdk::RGBA c; c.set(hex); return c;
}

// ─── Constructor ─────────────────────────────────────────────────────────────
PreferencesDialog::PreferencesDialog(Gtk::Window& parent, FolioPrefs& prefs)
    : m_prefs(prefs), m_parent_win(parent)
{
    ensure_prefs_css();
    set_transient_for(parent);
    set_modal(true);
    set_default_size(760, 600);
    set_title("Preferences");

    auto* hbar = Gtk::make_managed<Gtk::HeaderBar>();
    hbar->set_show_title_buttons(true);
    auto* htitle = Gtk::make_managed<Gtk::Label>();
    htitle->set_markup("<b>Preferences</b>");
    hbar->set_title_widget(*htitle);
    set_titlebar(*hbar);

    signal_close_request().connect([this]() -> bool {
        apply_and_close(); return true;
    }, false);

    auto* root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    set_child(*root);

    // Sidebar
    auto* sidebar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    sidebar->add_css_class("prefs-sidebar");
    sidebar->set_vexpand(true);
    root->append(*sidebar);
    m_nav_list.set_selection_mode(Gtk::SelectionMode::SINGLE);
    m_nav_list.set_vexpand(true);
    sidebar->append(m_nav_list);

    // Stack
    m_stack.set_hexpand(true);
    m_stack.set_vexpand(true);
    m_stack.set_transition_type(Gtk::StackTransitionType::CROSSFADE);
    m_stack.set_transition_duration(100);
    root->append(m_stack);

    struct P { const char* id; const char* label; };
    static const P pages[] = {
        {"typography", "Typography"},
        {"editor",     "Editor"},
        {"appearance", "Appearance"},
        {"tags",       "Status & Tags"},
        {"saving",     "Saving"},
        {"defaults",   "New Item Defaults"},
        {"pomodoro",   "Pomodoro"},
        {"editing",    "Editing"},
        {"headings",   "Outlining"},
        {"screenplay", "Screenplay"},
    };
    using B = Gtk::Widget* (PreferencesDialog::*)();
    static const B builders[] = {
        &PreferencesDialog::build_page_typography,
        &PreferencesDialog::build_page_editor,
        &PreferencesDialog::build_page_appearance,
        &PreferencesDialog::build_page_tags,
        &PreferencesDialog::build_page_saving,
        &PreferencesDialog::build_page_defaults,
        &PreferencesDialog::build_page_pomodoro,
        &PreferencesDialog::build_page_editing,
        &PreferencesDialog::build_page_headings,
        &PreferencesDialog::build_page_screenplay,
    };
    for (int i = 0; i < 10; ++i) {
        auto* page = (this->*builders[i])();
        m_stack.add(*page, pages[i].id, pages[i].label);
        auto* rb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
        auto* lbl = Gtk::make_managed<Gtk::Label>(pages[i].label);
        lbl->add_css_class("prefs-nav-label");
        lbl->set_halign(Gtk::Align::START);
        lbl->set_hexpand(true);
        rb->append(*lbl);
        auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_child(*rb);
        m_nav_list.append(*row);
    }

    signal_show().connect([this]{ m_changed = false; });
    m_nav_list.select_row(*m_nav_list.get_row_at_index(0));
    m_nav_list.signal_row_selected().connect([this](Gtk::ListBoxRow* row) {
        if (!row) return;
        static const char* ids[] = {
            "typography","editor","appearance","tags","saving","defaults","pomodoro","editing","headings","screenplay"
        };
        int idx = row->get_index();
        if (idx >= 0 && idx < 10) m_stack.set_visible_child(ids[idx]);
    });
}

PreferencesDialog::~PreferencesDialog() {
    // Explicitly unparent the picker popover before the window is destroyed.
    // GTK requires popovers to be unparented before their parent window closes;
    // failing to do so produces a "Finalizing window but it still has children"
    // warning and a subsequent "grabbing popup with non-top-most parent" crash.
    if (m_ac_pair_picker) {
        m_ac_pair_picker->popdown();
        m_ac_pair_picker->unparent();
        m_ac_pair_picker.reset();
    }
}

// ─── Apply & close ────────────────────────────────────────────────────────────
void PreferencesDialog::apply_and_close() {
    // Typography — read font names from dropdowns
    if (m_dd_editor_font) {
        guint sel = m_dd_editor_font->get_selected();
        if (sel < m_font_families.size())
            m_prefs.editor_font = m_font_families[sel];
    }
    m_prefs.editor_font_size  = (int)m_spin_editor_font_size.get_value();
    if (m_dd_ui_font) {
        guint sel = m_dd_ui_font->get_selected();
        if (sel < m_font_families.size())
            m_prefs.ui_font = m_font_families[sel];
    }
    m_prefs.ui_font_size      = (int)m_spin_ui_font_size.get_value();
    m_prefs.line_spacing      = m_spin_line_spacing.get_value();
    if (m_dd_serif_font) {
        guint sel = m_dd_serif_font->get_selected();
        if (sel < m_font_families.size())
            m_prefs.serif_font = m_font_families[sel];
    }
    if (m_dd_sans_font) {
        guint sel = m_dd_sans_font->get_selected();
        if (sel < m_font_families.size())
            m_prefs.sans_font = m_font_families[sel];
    }
    if (m_dd_mono_font) {
        guint sel = m_dd_mono_font->get_selected();
        if (sel < m_font_families.size())
            m_prefs.mono_font = m_font_families[sel];
    }

    m_prefs.typewriter_width_chars = (int)m_spin_width_chars.get_value();
    m_prefs.typewriter_mode        = m_sw_typewriter.get_active();

    if (m_dd_theme) {
        guint sel = m_dd_theme->get_selected();
        m_prefs.theme = (sel == 1) ? "dark" : (sel == 2) ? "light" : "system";
    }
    m_prefs.show_word_count      = m_sw_wc.get_active();
    m_prefs.show_reading_time    = m_sw_rt.get_active();
    m_prefs.show_paragraph_marks = m_sw_para.get_active();

    m_prefs.auto_save              = m_sw_autosave.get_active();
    m_prefs.auto_save_interval_min = (int)m_spin_autosave_interval.get_value();
    m_prefs.save_on_close          = m_sw_save_on_close.get_active();
    m_prefs.daily_word_goal        = (int)m_spin_goal.get_value();
    m_prefs.max_recent_files       = (int)m_spin_max_recent.get_value();

    m_prefs.backup_enabled        = m_sw_backup.get_active();
    m_prefs.backup_interval_hours = (int)m_spin_backup_interval.get_value();
    m_prefs.backup_max_count      = (int)m_spin_backup_max.get_value();
    m_prefs.backup_dir            = m_entry_backup_dir.get_text();

    // Statuses
    m_prefs.statuses.clear();
    for (auto& s : m_working_statuses)
        if (!s.name.empty()) m_prefs.statuses.push_back(s);

    // Character roles
    m_prefs.character_roles.clear();
    for (auto& r : m_working_roles)
        if (!r.empty()) m_prefs.character_roles.push_back(r);

    // Genres
    m_prefs.genres.clear();
    for (auto& g : m_working_genres)
        if (!g.empty()) m_prefs.genres.push_back(g);

    // Tags
    m_prefs.tag_colors = m_working_tags;

    // Node defaults — working copies flushed directly
    m_prefs.scene_defaults       = m_wd_scene;
    m_prefs.group_defaults       = m_wd_group;
    m_prefs.character_defaults   = m_wd_character;
    m_prefs.char_group_defaults  = m_wd_char_group;
    m_prefs.place_defaults       = m_wd_place;
    m_prefs.place_group_defaults = m_wd_place_group;
    m_prefs.reference_defaults   = m_wd_reference;
    m_prefs.template_defaults    = m_wd_template;

    // Pomodoro
    m_prefs.pomodoro.focus_min            = (int)m_spin_pomo_focus.get_value();
    m_prefs.pomodoro.short_break_min      = (int)m_spin_pomo_short.get_value();
    m_prefs.pomodoro.long_break_min       = (int)m_spin_pomo_long.get_value();
    m_prefs.pomodoro.sessions_before_long = (int)m_spin_pomo_sessions.get_value();
    m_prefs.pomodoro.auto_start           = m_sw_pomo_auto_start.get_active();
    m_prefs.pomodoro.show_in_headerbar    = m_sw_pomo_headerbar.get_active();

    // Editing — spell check
    m_prefs.spell_check_enabled    = m_sw_spell_enabled.get_active();
    m_prefs.spell_underline_bold   = m_sw_spell_bold.get_active();
    m_prefs.spell_background_tint  = m_sw_spell_bg_tint.get_active();
    m_prefs.spell_underline_color  = rgba_to_hex(m_btn_spell_color.get_rgba());
    m_prefs.spell_background_color = rgba_to_hex(m_btn_spell_bg_color.get_rgba());
    if (m_dd_spell_underline_style) {
        static const char* styles[] = {"wavy", "single", "double"};
        guint idx = m_dd_spell_underline_style->get_selected();
        if (idx < 3) m_prefs.spell_underline_style = styles[idx];
    }
    if (m_dd_spell_language && !m_spell_languages.empty()) {
        guint idx = m_dd_spell_language->get_selected();
        m_prefs.spell_language = (idx == 0) ? "" : (idx - 1 < m_spell_languages.size() ? m_spell_languages[idx - 1] : "");
    }

    // Editing — text substitution
    m_prefs.sub_smart_quotes  = m_sw_sub_smart_quotes.get_active();
    m_prefs.sub_em_dash       = m_sw_sub_em_dash.get_active();
    m_prefs.sub_ellipsis      = m_sw_sub_ellipsis.get_active();
    m_prefs.sub_autocorrect   = m_sw_sub_autocorrect.get_active();
    m_prefs.autocorrect_pairs = m_working_ac_pairs;

    // Flush separator entry text from live widgets — avoids dangling-pointer
    // signal_changed handlers during teardown.
    if (m_sep_list_box) {
        m_working_separators.clear();
        auto* child = m_sep_list_box->get_first_child();
        while (child) {
            // Each data row is a Gtk::Box whose first child is a Gtk::Entry.
            // The last child is the "+ Add Separator" button — skip it.
            if (auto* row_box = dynamic_cast<Gtk::Box*>(child)) {
                if (auto* entry = dynamic_cast<Gtk::Entry*>(row_box->get_first_child())) {
                    std::string val = std::string(entry->get_text());
                    if (!val.empty()) m_working_separators.push_back(val);
                }
            }
            child = child->get_next_sibling();
        }
        if (m_working_separators.empty()) m_working_separators = {"---"};
    }
    m_prefs.split_separators = m_working_separators;

    try { m_prefs.save(); } catch (...) {}
    m_changed = true;
    hide();
}

// ─── Layout helpers ───────────────────────────────────────────────────────────
Gtk::Box* PreferencesDialog::make_section(const std::string& title, Gtk::ListBox*& out_lb) {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    box->set_margin_bottom(24);
    auto* lbl = Gtk::make_managed<Gtk::Label>(title);
    lbl->add_css_class("pref-group-title");
    lbl->set_halign(Gtk::Align::START);
    box->append(*lbl);
    out_lb = Gtk::make_managed<Gtk::ListBox>();
    out_lb->add_css_class("pref-listbox");
    out_lb->set_selection_mode(Gtk::SelectionMode::NONE);
    box->append(*out_lb);
    return box;
}

void PreferencesDialog::append_row(Gtk::ListBox* lb,
                                   const std::string& label,
                                   const std::string& sub,
                                   Gtk::Widget& control) {
    auto* rb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
    rb->set_margin_start(4); rb->set_margin_end(8);
    rb->set_margin_top(2);   rb->set_margin_bottom(2);

    auto* left = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    left->set_hexpand(true); left->set_valign(Gtk::Align::CENTER);
    auto* lbl = Gtk::make_managed<Gtk::Label>(label);
    lbl->add_css_class("pref-row-label"); lbl->set_halign(Gtk::Align::START);
    left->append(*lbl);
    if (!sub.empty()) {
        auto* sl = Gtk::make_managed<Gtk::Label>(sub);
        sl->add_css_class("pref-row-sub"); sl->set_halign(Gtk::Align::START);
        left->append(*sl);
    }
    rb->append(*left);
    control.set_valign(Gtk::Align::CENTER);
    control.set_halign(Gtk::Align::END);
    rb->append(control);

    auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
    row->set_child(*rb); row->set_activatable(false);
    lb->append(*row);
}

static void setup_spin(Gtk::SpinButton& spin,
                       double value, double lo, double hi,
                       double step, double page, int digits) {
    auto adj = Gtk::Adjustment::create(value, lo, hi, step, page);
    spin.set_adjustment(adj);
    spin.set_value(value);
    spin.set_digits(digits);
    spin.set_numeric(true);
    spin.set_snap_to_ticks(true);
}

// ─── Page: Typography ─────────────────────────────────────────────────────────
Gtk::Widget* PreferencesDialog::build_page_typography() {
    // Enumerate system font families once
    if (m_font_families.empty()) {
        PangoFontMap* font_map = pango_cairo_font_map_get_default();
        PangoContext* ctx = pango_font_map_create_context(font_map);
        PangoFontFamily** families = nullptr;
        int n_families = 0;
        pango_context_list_families(ctx, &families, &n_families);
        for (int i = 0; i < n_families; ++i)
            m_font_families.push_back(pango_font_family_get_name(families[i]));
        g_free(families);
        g_object_unref(ctx);
        std::sort(m_font_families.begin(), m_font_families.end(),
            [](const std::string& a, const std::string& b){
                std::string al = a, bl = b;
                std::transform(al.begin(),al.end(),al.begin(),::tolower);
                std::transform(bl.begin(),bl.end(),bl.begin(),::tolower);
                return al < bl;
            });
    }

    // Build StringList for the font dropdowns
    auto font_sl = Gtk::StringList::create({});
    guint editor_sel = 0, ui_sel = 0;
    guint serif_sel = 0, sans_sel = 0, mono_sel = 0;
    for (guint i = 0; i < (guint)m_font_families.size(); ++i) {
        font_sl->append(m_font_families[i]);
        if (m_font_families[i] == m_prefs.editor_font) editor_sel = i;
        if (m_font_families[i] == m_prefs.ui_font)     ui_sel     = i;
        if (m_font_families[i] == m_prefs.serif_font)  serif_sel  = i;
        if (m_font_families[i] == m_prefs.sans_font)   sans_sel   = i;
        if (m_font_families[i] == m_prefs.mono_font)   mono_sel   = i;
    }

    // The two dropdowns share the same model (safe — StringList is ref-counted)
    auto font_sl2 = Gtk::StringList::create({});
    for (auto& f : m_font_families) font_sl2->append(f);

    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    auto* page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    page->add_css_class("prefs-page"); scroll->set_child(*page);

    auto* tl = Gtk::make_managed<Gtk::Label>("Typography");
    tl->add_css_class("prefs-page-title"); tl->set_halign(Gtk::Align::START);
    page->append(*tl);

    Gtk::ListBox* lb = nullptr;
    page->append(*make_section("Editor / Body Text", lb));
    m_dd_editor_font = Gtk::make_managed<Gtk::DropDown>(font_sl);
    m_dd_editor_font->set_selected(editor_sel);
    m_dd_editor_font->set_expression(
        Gtk::PropertyExpression<Glib::ustring>::create(GTK_TYPE_STRING_OBJECT, "string"));
    m_dd_editor_font->set_enable_search(true);
    append_row(lb, "Font Family", "Serif font used in the writing area", *m_dd_editor_font);
    m_spin_editor_font_size.set_width_chars(5);
    setup_spin(m_spin_editor_font_size, m_prefs.editor_font_size, 8, 48, 1, 4, 0);
    append_row(lb, "Font Size", "Points (8–48 pt)", m_spin_editor_font_size);
    m_spin_line_spacing.set_width_chars(5);
    setup_spin(m_spin_line_spacing, m_prefs.line_spacing, 1.0, 3.0, 0.1, 0.5, 1);
    append_row(lb, "Line Spacing", "Multiplier (1.0–3.0)", m_spin_line_spacing);

    Gtk::ListBox* lb_ui = nullptr;
    page->append(*make_section("Interface", lb_ui));
    m_dd_ui_font = Gtk::make_managed<Gtk::DropDown>(font_sl2);
    m_dd_ui_font->set_selected(ui_sel);
    m_dd_ui_font->set_expression(
        Gtk::PropertyExpression<Glib::ustring>::create(GTK_TYPE_STRING_OBJECT, "string"));
    m_dd_ui_font->set_enable_search(true);
    append_row(lb_ui, "UI Font Family", "Font for panels, labels, and menus", *m_dd_ui_font);
    m_spin_ui_font_size.set_width_chars(5);
    setup_spin(m_spin_ui_font_size, m_prefs.ui_font_size, 8, 24, 1, 2, 0);
    append_row(lb_ui, "UI Font Size", "Points (8–24 pt)", m_spin_ui_font_size);

    // Style font defaults — used by the built-in and user-defined style set
    auto font_sl3 = Gtk::StringList::create({});
    auto font_sl4 = Gtk::StringList::create({});
    auto font_sl5 = Gtk::StringList::create({});
    for (auto& f : m_font_families) {
        font_sl3->append(f);
        font_sl4->append(f);
        font_sl5->append(f);
    }

    Gtk::ListBox* lb_sf = nullptr;
    page->append(*make_section("Style Font Defaults", lb_sf));

    m_dd_serif_font = Gtk::make_managed<Gtk::DropDown>(font_sl3);
    m_dd_serif_font->set_selected(serif_sel);
    m_dd_serif_font->set_expression(
        Gtk::PropertyExpression<Glib::ustring>::create(GTK_TYPE_STRING_OBJECT, "string"));
    m_dd_serif_font->set_enable_search(true);
    append_row(lb_sf, "Serif Font", "Used by Body Text, Headings and prose styles", *m_dd_serif_font);

    m_dd_sans_font = Gtk::make_managed<Gtk::DropDown>(font_sl4);
    m_dd_sans_font->set_selected(sans_sel);
    m_dd_sans_font->set_expression(
        Gtk::PropertyExpression<Glib::ustring>::create(GTK_TYPE_STRING_OBJECT, "string"));
    m_dd_sans_font->set_enable_search(true);
    append_row(lb_sf, "Sans-Serif Font", "Used by Small Caps and sans-serif styles", *m_dd_sans_font);

    m_dd_mono_font = Gtk::make_managed<Gtk::DropDown>(font_sl5);
    m_dd_mono_font->set_selected(mono_sel);
    m_dd_mono_font->set_expression(
        Gtk::PropertyExpression<Glib::ustring>::create(GTK_TYPE_STRING_OBJECT, "string"));
    m_dd_mono_font->set_enable_search(true);
    append_row(lb_sf, "Monospace Font", "Used by Code style and screenplay elements", *m_dd_mono_font);

    return scroll;
}

// ─── Page: Editor ─────────────────────────────────────────────────────────────
Gtk::Widget* PreferencesDialog::build_page_editor() {
    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    auto* page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    page->add_css_class("prefs-page"); scroll->set_child(*page);

    auto* tl = Gtk::make_managed<Gtk::Label>("Editor");
    tl->add_css_class("prefs-page-title"); tl->set_halign(Gtk::Align::START);
    page->append(*tl);

    Gtk::ListBox* lb = nullptr;
    page->append(*make_section("Layout", lb));
    m_spin_width_chars.set_width_chars(5);
    setup_spin(m_spin_width_chars, m_prefs.typewriter_width_chars, 40, 140, 1, 10, 0);
    append_row(lb, "Column Width", "Characters before soft wrap (40–140)", m_spin_width_chars);
    m_sw_typewriter.set_active(m_prefs.typewriter_mode);
    append_row(lb, "Typewriter Mode", "Keep the active line centred vertically", m_sw_typewriter);

    return scroll;
}

// ─── Page: Appearance ─────────────────────────────────────────────────────────
Gtk::Widget* PreferencesDialog::build_page_appearance() {
    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    auto* page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    page->add_css_class("prefs-page"); scroll->set_child(*page);

    auto* tl = Gtk::make_managed<Gtk::Label>("Appearance");
    tl->add_css_class("prefs-page-title"); tl->set_halign(Gtk::Align::START);
    page->append(*tl);

    Gtk::ListBox* lb_t = nullptr;
    page->append(*make_section("Colour Scheme", lb_t));
    auto sm = Gtk::StringList::create({});
    sm->append("Follow system"); sm->append("Always dark"); sm->append("Always light");
    m_dd_theme = Gtk::make_managed<Gtk::DropDown>(sm);
    guint ti = (m_prefs.theme == "dark") ? 1 : (m_prefs.theme == "light") ? 2 : 0;
    m_dd_theme->set_selected(ti);
    append_row(lb_t, "Theme", "Override the system dark/light preference", *m_dd_theme);

    Gtk::ListBox* lb_s = nullptr;
    page->append(*make_section("Status Bar", lb_s));
    m_sw_wc.set_active(m_prefs.show_word_count);
    append_row(lb_s, "Show Word Count", "", m_sw_wc);
    m_sw_rt.set_active(m_prefs.show_reading_time);
    append_row(lb_s, "Show Reading Time", "Estimated time at 200 wpm", m_sw_rt);
    m_sw_para.set_active(m_prefs.show_paragraph_marks);
    append_row(lb_s, "Show Paragraph Marks", "Visible ¶ at each paragraph end", m_sw_para);

    return scroll;
}

// ─── Page: Status & Tags ──────────────────────────────────────────────────────

void PreferencesDialog::rebuild_status_list() {
    if (!m_status_list_box) return;
    while (auto* c = m_status_list_box->get_first_child())
        m_status_list_box->remove(*c);

    for (int i = 0; i < (int)m_working_statuses.size(); ++i) {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        row->add_css_class("dyn-row");

        // Color picker
        auto* color_btn = Gtk::make_managed<Gtk::ColorButton>();
        std::string hex = m_working_statuses[i].color_hex;
        if (hex.empty()) hex = "#888888";
        color_btn->set_rgba(hex_to_rgba(hex));
        color_btn->set_use_alpha(false);
        color_btn->set_size_request(32, 32);
        color_btn->set_tooltip_text("Dot colour for this status");
        int idx = i;
        color_btn->signal_color_set().connect([this, idx, color_btn] {
            if (idx < (int)m_working_statuses.size())
                m_working_statuses[idx].color_hex = rgba_to_hex(color_btn->get_rgba());
        });
        row->append(*color_btn);

        auto* name_entry = Gtk::make_managed<Gtk::Entry>();
        name_entry->set_text(m_working_statuses[i].name);
        name_entry->set_hexpand(true);
        name_entry->set_placeholder_text("Status name…");
        name_entry->signal_changed().connect([this, idx, name_entry] {
            if (idx < (int)m_working_statuses.size())
                m_working_statuses[idx].name = name_entry->get_text();
        });
        row->append(*name_entry);

        auto* del_btn = Gtk::make_managed<Gtk::Button>("✕");
        del_btn->add_css_class("dyn-del-btn");
        del_btn->set_tooltip_text("Remove this status");
        del_btn->signal_clicked().connect([this, idx] {
            if (idx < (int)m_working_statuses.size()) {
                m_working_statuses.erase(m_working_statuses.begin() + idx);
                rebuild_status_list();
            }
        });
        row->append(*del_btn);

        m_status_list_box->append(*row);
    }
}

void PreferencesDialog::rebuild_roles_list() {
    if (!m_roles_list_box) return;
    while (auto* c = m_roles_list_box->get_first_child())
        m_roles_list_box->remove(*c);

    for (int i = 0; i < (int)m_working_roles.size(); ++i) {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        row->add_css_class("dyn-row");

        auto* entry = Gtk::make_managed<Gtk::Entry>();
        entry->set_text(m_working_roles[i]);
        entry->set_hexpand(true);
        entry->set_placeholder_text("Role name…");
        int idx = i;
        entry->signal_changed().connect([this, idx, entry] {
            if (idx < (int)m_working_roles.size())
                m_working_roles[idx] = entry->get_text();
        });
        row->append(*entry);

        auto* del_btn = Gtk::make_managed<Gtk::Button>("✕");
        del_btn->add_css_class("dyn-del-btn");
        del_btn->set_tooltip_text("Remove this role");
        del_btn->signal_clicked().connect([this, idx] {
            if (idx < (int)m_working_roles.size()) {
                m_working_roles.erase(m_working_roles.begin() + idx);
                rebuild_roles_list();
            }
        });
        row->append(*del_btn);

        m_roles_list_box->append(*row);
    }
}

void PreferencesDialog::rebuild_genres_list() {
    if (!m_genres_list_box) return;
    while (auto* c = m_genres_list_box->get_first_child())
        m_genres_list_box->remove(*c);

    for (int i = 0; i < (int)m_working_genres.size(); ++i) {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        row->add_css_class("dyn-row");

        auto* entry = Gtk::make_managed<Gtk::Entry>();
        entry->set_text(m_working_genres[i]);
        entry->set_hexpand(true);
        entry->set_placeholder_text("Genre name…");
        int idx = i;
        entry->signal_changed().connect([this, idx, entry] {
            if (idx < (int)m_working_genres.size())
                m_working_genres[idx] = entry->get_text();
        });
        row->append(*entry);

        auto* del_btn = Gtk::make_managed<Gtk::Button>("✕");
        del_btn->add_css_class("dyn-del-btn");
        del_btn->set_tooltip_text("Remove this genre");
        del_btn->signal_clicked().connect([this, idx] {
            if (idx < (int)m_working_genres.size()) {
                m_working_genres.erase(m_working_genres.begin() + idx);
                rebuild_genres_list();
            }
        });
        row->append(*del_btn);

        m_genres_list_box->append(*row);
    }
}

void PreferencesDialog::rebuild_tags_list() {
    if (!m_tags_list_box) return;
    while (auto* c = m_tags_list_box->get_first_child())
        m_tags_list_box->remove(*c);

    for (int i = 0; i < (int)m_working_tags.size(); ++i) {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        row->add_css_class("dyn-row");

        auto* color_btn = Gtk::make_managed<Gtk::ColorButton>();
        color_btn->set_rgba(hex_to_rgba(m_working_tags[i].hex));
        color_btn->set_use_alpha(false);
        color_btn->set_size_request(32, 32);
        int idx = i;
        color_btn->signal_color_set().connect([this, idx, color_btn] {
            if (idx < (int)m_working_tags.size())
                m_working_tags[idx].hex = rgba_to_hex(color_btn->get_rgba());
        });
        row->append(*color_btn);

        auto* name_entry = Gtk::make_managed<Gtk::Entry>();
        name_entry->set_text(m_working_tags[i].name);
        name_entry->set_hexpand(true);
        name_entry->set_placeholder_text("Tag name…");
        name_entry->signal_changed().connect([this, idx, name_entry] {
            if (idx < (int)m_working_tags.size())
                m_working_tags[idx].name = name_entry->get_text();
        });
        row->append(*name_entry);

        auto* del_btn = Gtk::make_managed<Gtk::Button>("✕");
        del_btn->add_css_class("dyn-del-btn");
        del_btn->set_tooltip_text("Remove this tag");
        del_btn->signal_clicked().connect([this, idx] {
            if (idx < (int)m_working_tags.size()) {
                m_working_tags.erase(m_working_tags.begin() + idx);
                rebuild_tags_list();
            }
        });
        row->append(*del_btn);

        m_tags_list_box->append(*row);
    }
}

Gtk::Widget* PreferencesDialog::build_page_tags() {
    // Populate working copies from prefs
    m_working_statuses.clear();
    for (auto& s : m_prefs.statuses)
        m_working_statuses.push_back(s);
    m_working_roles   = m_prefs.character_roles;
    m_working_genres  = m_prefs.genres;
    m_working_tags    = m_prefs.tag_colors;

    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    auto* page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    page->add_css_class("prefs-page"); scroll->set_child(*page);

    auto* tl = Gtk::make_managed<Gtk::Label>("Status & Tags");
    tl->add_css_class("prefs-page-title"); tl->set_halign(Gtk::Align::START);
    page->append(*tl);

    // ── Status section (first) ────────────────────────────────────────────────
    {
        auto* sec = Gtk::make_managed<Gtk::Label>("Scene Status Indicators");
        sec->add_css_class("pref-group-title"); sec->set_halign(Gtk::Align::START);
        sec->set_margin_bottom(4);
        page->append(*sec);

        auto* sub = Gtk::make_managed<Gtk::Label>(
            "These appear in the Status dropdown on the Metadata panel. "
            "Add, remove, or rename freely.");
        sub->add_css_class("pref-row-sub"); sub->set_halign(Gtk::Align::START);
        sub->set_wrap(true); sub->set_xalign(0.0f); sub->set_margin_bottom(10);
        page->append(*sub);

        m_status_list_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        page->append(*m_status_list_box);
        rebuild_status_list();

        auto* add_btn = Gtk::make_managed<Gtk::Button>("+ Add Status");
        add_btn->add_css_class("dyn-add-btn"); add_btn->set_halign(Gtk::Align::START);
        add_btn->signal_clicked().connect([this] {
            m_working_statuses.push_back({"New Status", "#888888"});
            rebuild_status_list();
        });
        page->append(*add_btn);

        auto* rst = Gtk::make_managed<Gtk::Button>("Reset to defaults");
        rst->add_css_class("prefs-reset-btn"); rst->set_halign(Gtk::Align::START);
        rst->signal_clicked().connect([this] {
            m_working_statuses = {
                {"Rough Draft",  "#f9e2af"},
                {"In Progress",  "#89b4fa"},
                {"Polished",     "#a6e3a1"},
                {"Skip",         "#6c7086"},
            };
            rebuild_status_list();
        });
        page->append(*rst);
    }

    // Spacer
    auto* sp1 = Gtk::make_managed<Gtk::Box>();
    sp1->set_size_request(-1, 28); page->append(*sp1);

    // ── Character Roles section ───────────────────────────────────────────────
    {
        auto* sec = Gtk::make_managed<Gtk::Label>("Character Roles");
        sec->add_css_class("pref-group-title"); sec->set_halign(Gtk::Align::START);
        sec->set_margin_bottom(4);
        page->append(*sec);

        auto* sub = Gtk::make_managed<Gtk::Label>(
            "Roles available in the Character role dropdown. Add, remove, or rename freely.");
        sub->add_css_class("pref-row-sub"); sub->set_halign(Gtk::Align::START);
        sub->set_wrap(true); sub->set_xalign(0.0f); sub->set_margin_bottom(10);
        page->append(*sub);

        m_roles_list_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        page->append(*m_roles_list_box);
        rebuild_roles_list();

        auto* add_btn = Gtk::make_managed<Gtk::Button>("+ Add Role");
        add_btn->add_css_class("dyn-add-btn"); add_btn->set_halign(Gtk::Align::START);
        add_btn->signal_clicked().connect([this] {
            m_working_roles.push_back("New Role");
            rebuild_roles_list();
        });
        page->append(*add_btn);

        auto* rst2 = Gtk::make_managed<Gtk::Button>("Reset to defaults");
        rst2->add_css_class("prefs-reset-btn"); rst2->set_halign(Gtk::Align::START);
        rst2->signal_clicked().connect([this] {
            m_working_roles = {"Protagonist", "Antagonist", "Supporting", "Minor"};
            rebuild_roles_list();
        });
        page->append(*rst2);
    }

    // Spacer
    auto* sp2 = Gtk::make_managed<Gtk::Box>();
    sp2->set_size_request(-1, 28); page->append(*sp2);

    // ── Genres section ────────────────────────────────────────────────────────
    {
        auto* sec = Gtk::make_managed<Gtk::Label>("Genres");
        sec->add_css_class("pref-group-title"); sec->set_halign(Gtk::Align::START);
        sec->set_margin_bottom(4);
        page->append(*sec);

        auto* sub = Gtk::make_managed<Gtk::Label>(
            "Genres available in the Project genre dropdown. Add, remove, or rename freely.");
        sub->add_css_class("pref-row-sub"); sub->set_halign(Gtk::Align::START);
        sub->set_wrap(true); sub->set_xalign(0.0f); sub->set_margin_bottom(10);
        page->append(*sub);

        m_genres_list_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        page->append(*m_genres_list_box);
        rebuild_genres_list();

        auto* add_btn = Gtk::make_managed<Gtk::Button>("+ Add Genre");
        add_btn->add_css_class("dyn-add-btn"); add_btn->set_halign(Gtk::Align::START);
        add_btn->signal_clicked().connect([this] {
            m_working_genres.push_back("New Genre");
            rebuild_genres_list();
        });
        page->append(*add_btn);

        auto* rst3 = Gtk::make_managed<Gtk::Button>("Reset to defaults");
        rst3->add_css_class("prefs-reset-btn"); rst3->set_halign(Gtk::Align::START);
        rst3->signal_clicked().connect([this] {
            m_working_genres = FolioPrefs{}.genres;
            rebuild_genres_list();
        });
        page->append(*rst3);
    }

    // Spacer
    auto* sp = Gtk::make_managed<Gtk::Box>();
    sp->set_size_request(-1, 28); page->append(*sp);

    // ── Tags section (second) ─────────────────────────────────────────────────
    {
        auto* sec = Gtk::make_managed<Gtk::Label>("Label Colours");
        sec->add_css_class("pref-group-title"); sec->set_halign(Gtk::Align::START);
        sec->set_margin_bottom(4);
        page->append(*sec);

        auto* sub = Gtk::make_managed<Gtk::Label>(
            "Colour tags used to label scenes, characters, and places.");
        sub->add_css_class("pref-row-sub"); sub->set_halign(Gtk::Align::START);
        sub->set_wrap(true); sub->set_xalign(0.0f); sub->set_margin_bottom(10);
        page->append(*sub);

        m_tags_list_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
        page->append(*m_tags_list_box);
        rebuild_tags_list();

        auto* add_btn = Gtk::make_managed<Gtk::Button>("+ Add Colour");
        add_btn->add_css_class("dyn-add-btn"); add_btn->set_halign(Gtk::Align::START);
        add_btn->signal_clicked().connect([this] {
            m_working_tags.push_back({"new", "#888888"});
            rebuild_tags_list();
        });
        page->append(*add_btn);

        auto* rst = Gtk::make_managed<Gtk::Button>("Reset to defaults");
        rst->add_css_class("prefs-reset-btn"); rst->set_halign(Gtk::Align::START);
        rst->signal_clicked().connect([this] {
            m_working_tags = FolioPrefs{}.tag_colors;
            rebuild_tags_list();
        });
        page->append(*rst);
    }

    return scroll;
}

// ─── Page: Saving ─────────────────────────────────────────────────────────────
Gtk::Widget* PreferencesDialog::build_page_saving() {
    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    auto* page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    page->add_css_class("prefs-page"); scroll->set_child(*page);

    auto* tl = Gtk::make_managed<Gtk::Label>("Saving");
    tl->add_css_class("prefs-page-title"); tl->set_halign(Gtk::Align::START);
    page->append(*tl);

    Gtk::ListBox* lb_as = nullptr;
    page->append(*make_section("Auto-Save", lb_as));
    m_sw_autosave.set_active(m_prefs.auto_save);
    append_row(lb_as, "Enable Auto-Save", "Automatically save to disk at regular intervals", m_sw_autosave);
    m_spin_autosave_interval.set_width_chars(5);
    setup_spin(m_spin_autosave_interval, m_prefs.auto_save_interval_min, 1, 360, 1, 5, 0);
    append_row(lb_as, "Interval (minutes)", "Minutes between auto-saves (1–360)", m_spin_autosave_interval);
    m_sw_save_on_close.set_active(m_prefs.save_on_close);
    append_row(lb_as, "Save on Close", "Automatically save when the window is closed", m_sw_save_on_close);

    Gtk::ListBox* lb_ss = nullptr;
    page->append(*make_section("Writing Session", lb_ss));
    m_spin_goal.set_width_chars(7);
    setup_spin(m_spin_goal, m_prefs.daily_word_goal, 0, 50000, 100, 500, 0);
    append_row(lb_ss, "Daily Word Goal",
               "Words to write per session (0 = disabled)", m_spin_goal);

    Gtk::ListBox* lb_r = nullptr;
    page->append(*make_section("Recent Files", lb_r));
    m_spin_max_recent.set_width_chars(4);
    setup_spin(m_spin_max_recent, m_prefs.max_recent_files, 1, FolioPrefs::MAX_RECENT, 1, 5, 0);
    append_row(lb_r, "Maximum Recent Files",
               "How many files appear in Open Recent (1–" +
               std::to_string(FolioPrefs::MAX_RECENT) + ")",
               m_spin_max_recent);

    // ── Backup ────────────────────────────────────────────────────────────────
    Gtk::ListBox* lb_bk = nullptr;
    page->append(*make_section("Backup", lb_bk));

    m_sw_backup.set_active(m_prefs.backup_enabled);
    append_row(lb_bk, "Enable Backups",
               "Write compressed backups on a timer and when closing",
               m_sw_backup);

    m_spin_backup_interval.set_width_chars(4);
    setup_spin(m_spin_backup_interval, m_prefs.backup_interval_hours, 0, 24, 1, 1, 0);
    append_row(lb_bk, "Backup Interval (hours)",
               "Hours between timed backups (0 = on close only)",
               m_spin_backup_interval);

    m_spin_backup_max.set_width_chars(4);
    setup_spin(m_spin_backup_max, m_prefs.backup_max_count, 3, 50, 1, 5, 0);
    append_row(lb_bk, "Maximum Backups",
               "Oldest backups are removed once this limit is reached (3–50)",
               m_spin_backup_max);

    m_entry_backup_dir.set_text(m_prefs.backup_dir);
    m_entry_backup_dir.set_placeholder_text(
        BackupManager::resolve_backup_dir(""));
    m_entry_backup_dir.set_hexpand(true);

    // Row: entry + Browse… button + Reset button in an HBox
    auto* dir_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    dir_box->set_hexpand(true);
    dir_box->append(m_entry_backup_dir);

    auto* btn_browse = Gtk::make_managed<Gtk::Button>("Browse…");
    btn_browse->signal_clicked().connect([this]() {
        m_backup_dir_dialog = Gtk::FileDialog::create();
        m_backup_dir_dialog->set_title("Choose Backup Folder");
        m_backup_dir_dialog->set_modal(true);

        // Pre-open at current entry value, or the default dir
        std::string start = m_entry_backup_dir.get_text();
        if (start.empty())
            start = BackupManager::resolve_backup_dir("");
        auto init_folder = Gio::File::create_for_path(start);
        m_backup_dir_dialog->set_initial_folder(init_folder);

        m_backup_dir_dialog->select_folder(
            m_parent_win,
            [this](Glib::RefPtr<Gio::AsyncResult>& res) {
                try {
                    auto folder = m_backup_dir_dialog->select_folder_finish(res);
                    if (folder)
                        m_entry_backup_dir.set_text(folder->get_path());
                } catch (...) {
                    // User cancelled — do nothing
                }
                m_backup_dir_dialog.reset();
            });
    });
    dir_box->append(*btn_browse);

    auto* btn_reset = Gtk::make_managed<Gtk::Button>("Reset");
    btn_reset->set_tooltip_text("Reset to default location");
    btn_reset->signal_clicked().connect([this]() {
        m_entry_backup_dir.set_text("");
    });
    dir_box->append(*btn_reset);

    append_row(lb_bk, "Backup Location",
               "Folder where backups are stored (leave blank for default)",
               *dir_box);

    return scroll;
}

// ─── Page: New Item Defaults ──────────────────────────────────────────────────

Gtk::Box* PreferencesDialog::build_defaults_block(const std::string& heading,
                                                   NodeDefaults& nd,
                                                   bool show_word_target,
                                                   bool show_export,
                                                   bool show_role) {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    box->set_margin_bottom(16);

    // Sub-heading
    auto* lbl = Gtk::make_managed<Gtk::Label>(heading);
    lbl->add_css_class("pref-group-title");
    lbl->set_halign(Gtk::Align::START);
    lbl->set_margin_bottom(6);
    box->append(*lbl);

    Gtk::ListBox* lb = nullptr;
    auto* sec = make_section("", lb);
    box->append(*sec);

    // ── Title ─────────────────────────────────────────────────────────────────
    auto* title_entry = Gtk::make_managed<Gtk::Entry>();
    title_entry->set_text(nd.title);
    title_entry->set_placeholder_text("(use built-in default)");
    title_entry->set_hexpand(true);
    title_entry->signal_changed().connect([&nd, title_entry]() {
        nd.title = title_entry->get_text();
    });
    append_row(lb, "Default Title", "Leave blank to use the built-in name", *title_entry);

    // ── Status dropdown ───────────────────────────────────────────────────────
    // Entries: "(none)" + each working status name
    auto status_model = Gtk::StringList::create({});
    status_model->append("(none)");
    for (auto& s : m_working_statuses)
        if (!s.name.empty()) status_model->append(s.name);

    auto* status_dd = Gtk::make_managed<Gtk::DropDown>(status_model);
    status_dd->set_hexpand(true);

    // Select the entry matching nd.status_name
    guint status_sel = 0;
    for (guint i = 1; i < status_model->get_n_items(); ++i) {
        if (status_model->get_string(i) == Glib::ustring(nd.status_name)) { status_sel = i; break; }
    }
    status_dd->set_selected(status_sel);

    status_dd->property_selected().signal_changed().connect([&nd, status_dd, status_model]() {
        guint sel = status_dd->get_selected();
        nd.status_name = (sel == 0) ? "" : status_model->get_string(sel);
    });
    append_row(lb, "Default Status", "", *status_dd);

    // ── Role dropdown (characters only) ───────────────────────────────────────
    if (show_role) {
        auto role_model = Gtk::StringList::create({});
        role_model->append("—");
        for (auto& r : m_working_roles) role_model->append(r);

        auto* role_dd = Gtk::make_managed<Gtk::DropDown>(role_model);
        role_dd->set_hexpand(true);

        guint role_sel = 0;
        for (guint i = 1; i < role_model->get_n_items(); ++i) {
            if (role_model->get_string(i) == Glib::ustring(nd.role_name)) { role_sel = i; break; }
        }
        role_dd->set_selected(role_sel);

        role_dd->property_selected().signal_changed().connect([&nd, role_dd, role_model]() {
            guint sel = role_dd->get_selected();
            nd.role_name = (sel == 0) ? "" : role_model->get_string(sel);
        });
        append_row(lb, "Default Role", "", *role_dd);
    }

    // ── Label dropdown (color chip + name) ────────────────────────────────────
    // Build a StringList with "None" + each tag color name; use index as color_idx.
    auto color_model = Gtk::StringList::create({});
    color_model->append("None");
    for (auto& tc : m_working_tags)
        color_model->append(tc.name);

    auto color_factory = Gtk::SignalListItemFactory::create();
    color_factory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        row->set_margin_start(4); row->set_margin_end(4);
        row->set_margin_top(2);   row->set_margin_bottom(2);
        auto* chip = Gtk::make_managed<Gtk::DrawingArea>();
        chip->set_size_request(14, 14);
        chip->set_valign(Gtk::Align::CENTER);
        chip->set_halign(Gtk::Align::CENTER);
        auto* name_lbl = Gtk::make_managed<Gtk::Label>("");
        name_lbl->set_halign(Gtk::Align::START);
        row->append(*chip);
        row->append(*name_lbl);
        item->set_child(*row);
    });

    color_factory->signal_bind().connect(
        [this](const Glib::RefPtr<Gtk::ListItem>& item) {
            guint pos = item->get_position();
            auto* row = dynamic_cast<Gtk::Box*>(item->get_child());
            if (!row) return;
            auto* chip     = dynamic_cast<Gtk::DrawingArea*>(row->get_first_child());
            auto* name_lbl = dynamic_cast<Gtk::Label*>(chip ? chip->get_next_sibling() : nullptr);
            if (!chip || !name_lbl) return;

            if (pos == 0) {
                name_lbl->set_text("None");
                chip->set_draw_func([](const Cairo::RefPtr<Cairo::Context>& cr,
                                       int w, int h) {
                    cr->set_source_rgba(0.5, 0.5, 0.5, 0.3);
                    cr->rectangle(0, 0, w, h);
                    cr->fill();
                });
            } else {
                int idx = (int)pos - 1;
                if (idx < (int)m_working_tags.size()) {
                    name_lbl->set_text(m_working_tags[idx].name);
                    // Parse hex color for drawing
                    std::string hex = m_working_tags[idx].hex;
                    double r = 0.5, g = 0.5, b = 0.5;
                    if (hex.size() == 7 && hex[0] == '#') {
                        auto hx = [&](int o) {
                            return std::stoi(hex.substr(o, 2), nullptr, 16) / 255.0;
                        };
                        r = hx(1); g = hx(3); b = hx(5);
                    }
                    chip->set_draw_func([r, g, b](const Cairo::RefPtr<Cairo::Context>& cr,
                                                   int w, int h) {
                        cr->set_source_rgb(r, g, b);
                        // Rounded rect
                        double radius = 3.0;
                        cr->move_to(radius, 0);
                        cr->line_to(w - radius, 0);
                        cr->arc(w - radius, radius, radius, -M_PI/2, 0);
                        cr->line_to(w, h - radius);
                        cr->arc(w - radius, h - radius, radius, 0, M_PI/2);
                        cr->line_to(radius, h);
                        cr->arc(radius, h - radius, radius, M_PI/2, M_PI);
                        cr->line_to(0, radius);
                        cr->arc(radius, radius, radius, M_PI, 3*M_PI/2);
                        cr->close_path();
                        cr->fill();
                    });
                }
            }
        });

    auto* color_dd = Gtk::make_managed<Gtk::DropDown>(color_model);
    color_dd->set_factory(color_factory);
    color_dd->set_hexpand(true);
    // nd.color_idx: 0=None, 1-based → dropdown index matches directly
    guint color_sel = (nd.color_idx >= 0 && nd.color_idx <= (int)m_working_tags.size())
                      ? (guint)nd.color_idx : 0;
    color_dd->set_selected(color_sel);

    color_dd->property_selected().signal_changed().connect([&nd, color_dd]() {
        nd.color_idx = (int)color_dd->get_selected();
    });
    append_row(lb, "Default Label", "", *color_dd);

    // ── Include in Export ─────────────────────────────────────────────────────
    if (show_export) {
        auto* export_sw = Gtk::make_managed<Gtk::Switch>();
        export_sw->set_active(nd.include_in_export);
        export_sw->property_active().signal_changed().connect([&nd, export_sw]() {
            nd.include_in_export = export_sw->get_active();
        });
        append_row(lb, "Include in Export", "", *export_sw);
    }

    // ── Word Target ───────────────────────────────────────────────────────────
    if (show_word_target) {
        auto* wt_spin = Gtk::make_managed<Gtk::SpinButton>();
        setup_spin(*wt_spin, nd.word_target, 0, 100000, 100, 1000, 0);
        wt_spin->set_width_chars(7);
        wt_spin->signal_value_changed().connect([&nd, wt_spin]() {
            nd.word_target = (int)wt_spin->get_value();
        });
        append_row(lb, "Word Target", "0 = no target", *wt_spin);
    }

    // ── Default Template ──────────────────────────────────────────────────────
    // Only show if there are templates available
    if (!m_template_names.empty()) {
        Gtk::ListBox* tpl_lb = nullptr;
        auto* tpl_sec = make_section("Default Template", tpl_lb);
        box->append(*tpl_sec);

        // Template picker dropdown
        auto tpl_model = Gtk::StringList::create({"(none)"});
        for (const auto& name : m_template_names)
            tpl_model->append(name);

        auto* tpl_dd = Gtk::make_managed<Gtk::DropDown>(tpl_model);
        tpl_dd->set_hexpand(true);

        // Select current
        guint tpl_sel = 0;
        for (guint i = 1; i < tpl_model->get_n_items(); ++i) {
            if (std::string(tpl_model->get_string(i)) == nd.template_name) {
                tpl_sel = i; break;
            }
        }
        tpl_dd->set_selected(tpl_sel);
        tpl_dd->property_selected().signal_changed().connect([&nd, tpl_dd, tpl_model]() {
            guint sel = tpl_dd->get_selected();
            nd.template_name = (sel == 0) ? "" : std::string(tpl_model->get_string(sel));
        });
        append_row(tpl_lb, "Template", "Applied when a new item is created", *tpl_dd);

        // Override toggles — which fields to copy from the template
        auto make_tpl_sw = [&](bool& flag, const char* label) {
            auto* sw = Gtk::make_managed<Gtk::Switch>();
            sw->set_active(flag);
            sw->property_active().signal_changed().connect([&flag, sw]() {
                flag = sw->get_active();
            });
            append_row(tpl_lb, label, "", *sw);
        };
        make_tpl_sw(nd.template_copy_title,       "Copy Title");
        make_tpl_sw(nd.template_copy_color,       "Copy Color");
        make_tpl_sw(nd.template_copy_status,      "Copy Status");
        make_tpl_sw(nd.template_copy_word_target, "Copy Word Target");
    }

    return box;
}

Gtk::Widget* PreferencesDialog::build_page_defaults() {
    // Initialise working copies
    m_wd_scene       = m_prefs.scene_defaults;
    m_wd_group       = m_prefs.group_defaults;
    m_wd_character   = m_prefs.character_defaults;
    m_wd_char_group  = m_prefs.char_group_defaults;
    m_wd_place       = m_prefs.place_defaults;
    m_wd_place_group = m_prefs.place_group_defaults;
    m_wd_reference   = m_prefs.reference_defaults;
    m_wd_template    = m_prefs.template_defaults;

    // Ensure status/tag working copies are populated (pages built in order,
    // but guard here in case order ever changes)
    if (m_working_statuses.empty()) {
        for (auto& s : m_prefs.statuses)
            m_working_statuses.push_back(s);
    }
    if (m_working_roles.empty())
        m_working_roles = m_prefs.character_roles;
    if (m_working_genres.empty())
        m_working_genres = m_prefs.genres;
    if (m_working_tags.empty())
        m_working_tags = m_prefs.tag_colors;

    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    auto* page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    page->add_css_class("prefs-page"); scroll->set_child(*page);

    auto* tl = Gtk::make_managed<Gtk::Label>("New Item Defaults");
    tl->add_css_class("prefs-page-title"); tl->set_halign(Gtk::Align::START);
    page->append(*tl);

    auto* sub = Gtk::make_managed<Gtk::Label>(
        "These values are applied automatically whenever a new scene, group, character, or place is created.");
    sub->add_css_class("pref-row-sub"); sub->set_halign(Gtk::Align::START);
    sub->set_wrap(true); sub->set_xalign(0.0f); sub->set_margin_bottom(20);
    page->append(*sub);

    // ── Manuscript ────────────────────────────────────────────────────────────
    auto* ms_lbl = Gtk::make_managed<Gtk::Label>("Manuscript");
    ms_lbl->add_css_class("prefs-page-title");
    ms_lbl->set_halign(Gtk::Align::START);
    ms_lbl->set_margin_bottom(4);
    page->append(*ms_lbl);

    page->append(*build_defaults_block("Scene (item)",  m_wd_scene, true,  true));
    page->append(*build_defaults_block("Group",         m_wd_group, false, true));

    auto* sep1 = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
    sep1->set_margin_top(4); sep1->set_margin_bottom(20);
    page->append(*sep1);

    // ── Characters ────────────────────────────────────────────────────────────
    auto* ch_lbl = Gtk::make_managed<Gtk::Label>("Characters");
    ch_lbl->add_css_class("prefs-page-title");
    ch_lbl->set_halign(Gtk::Align::START);
    ch_lbl->set_margin_bottom(4);
    page->append(*ch_lbl);

    page->append(*build_defaults_block("Character (item)", m_wd_character,  false, false, true));
    page->append(*build_defaults_block("Group",            m_wd_char_group, false, false, false));

    auto* sep2 = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
    sep2->set_margin_top(4); sep2->set_margin_bottom(20);
    page->append(*sep2);

    // ── Places ────────────────────────────────────────────────────────────────
    auto* pl_lbl = Gtk::make_managed<Gtk::Label>("Places");
    pl_lbl->add_css_class("prefs-page-title");
    pl_lbl->set_halign(Gtk::Align::START);
    pl_lbl->set_margin_bottom(4);
    page->append(*pl_lbl);

    page->append(*build_defaults_block("Place (item)", m_wd_place,       false, false));
    page->append(*build_defaults_block("Group",        m_wd_place_group, false, false));

    // ── References ────────────────────────────────────────────────────────────
    auto* ref_lbl = Gtk::make_managed<Gtk::Label>("References");
    ref_lbl->add_css_class("prefs-page-title");
    ref_lbl->set_halign(Gtk::Align::START);
    ref_lbl->set_margin_top(8);
    ref_lbl->set_margin_bottom(4);
    page->append(*ref_lbl);
    page->append(*build_defaults_block("Reference (item)", m_wd_reference, false, false));

    // ── Templates ─────────────────────────────────────────────────────────────
    auto* tpl_lbl = Gtk::make_managed<Gtk::Label>("Templates");
    tpl_lbl->add_css_class("prefs-page-title");
    tpl_lbl->set_halign(Gtk::Align::START);
    tpl_lbl->set_margin_top(8);
    tpl_lbl->set_margin_bottom(4);
    page->append(*tpl_lbl);
    page->append(*build_defaults_block("Template (item)", m_wd_template, false, false));

    return scroll;
}

// ─── Page: Pomodoro ───────────────────────────────────────────────────────────
Gtk::Widget* PreferencesDialog::build_page_pomodoro() {
    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    auto* page = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    page->add_css_class("prefs-page");
    scroll->set_child(*page);

    auto* tl = Gtk::make_managed<Gtk::Label>("Pomodoro");
    tl->add_css_class("prefs-page-title");
    tl->set_halign(Gtk::Align::START);
    page->append(*tl);

    // ── Durations ─────────────────────────────────────────────────────────────
    Gtk::ListBox* lb_dur = nullptr;
    page->append(*make_section("Durations", lb_dur));

    setup_spin(m_spin_pomo_focus,    m_prefs.pomodoro.focus_min,       1, 120, 1, 5, 0);
    m_spin_pomo_focus.set_width_chars(4);
    append_row(lb_dur, "Focus", "Minutes per focus session (1–120)", m_spin_pomo_focus);

    setup_spin(m_spin_pomo_short,    m_prefs.pomodoro.short_break_min, 1,  60, 1, 5, 0);
    m_spin_pomo_short.set_width_chars(4);
    append_row(lb_dur, "Short Break", "Minutes for the short break (1–60)", m_spin_pomo_short);

    setup_spin(m_spin_pomo_long,     m_prefs.pomodoro.long_break_min,  1,  60, 1, 5, 0);
    m_spin_pomo_long.set_width_chars(4);
    append_row(lb_dur, "Long Break",  "Minutes for the long break (1–60)",  m_spin_pomo_long);

    // ── Cycle ─────────────────────────────────────────────────────────────────
    Gtk::ListBox* lb_cyc = nullptr;
    page->append(*make_section("Cycle", lb_cyc));

    setup_spin(m_spin_pomo_sessions, m_prefs.pomodoro.sessions_before_long, 1, 12, 1, 1, 0);
    m_spin_pomo_sessions.set_width_chars(4);
    append_row(lb_cyc, "Sessions per Cycle",
               "Focus sessions before a long break (1–12)", m_spin_pomo_sessions);

    // ── Behaviour ─────────────────────────────────────────────────────────────
    Gtk::ListBox* lb_beh = nullptr;
    page->append(*make_section("Behaviour", lb_beh));

    m_sw_pomo_auto_start.set_active(m_prefs.pomodoro.auto_start);
    append_row(lb_beh, "Auto-Start Phases",
               "Automatically begin the next phase without waiting", m_sw_pomo_auto_start);

    m_sw_pomo_headerbar.set_active(m_prefs.pomodoro.show_in_headerbar);
    append_row(lb_beh, "Show in Header Bar",
               "Display phase and countdown in the title bar while running", m_sw_pomo_headerbar);

    // ── Phase colours ─────────────────────────────────────────────────────────
    Gtk::ListBox* lb_col = nullptr;
    page->append(*make_section("Phase Colours", lb_col));

    // Focus colour
    m_btn_pomo_focus_color.set_rgba(hex_to_rgba(m_prefs.pomodoro.focus_color));
    m_btn_pomo_focus_color.set_use_alpha(false);
    m_btn_pomo_focus_color.set_size_request(48, 32);
    m_btn_pomo_focus_color.set_tooltip_text("Ring and banner colour for Focus sessions");
    m_btn_pomo_focus_color.signal_color_set().connect([this]() {
        m_prefs.pomodoro.focus_color = rgba_to_hex(m_btn_pomo_focus_color.get_rgba());
    });
    append_row(lb_col, "Focus", "Colour for focus session ring and banner",
               m_btn_pomo_focus_color);

    // Short break colour
    m_btn_pomo_short_color.set_rgba(hex_to_rgba(m_prefs.pomodoro.short_break_color));
    m_btn_pomo_short_color.set_use_alpha(false);
    m_btn_pomo_short_color.set_size_request(48, 32);
    m_btn_pomo_short_color.set_tooltip_text("Ring and banner colour for short breaks");
    m_btn_pomo_short_color.signal_color_set().connect([this]() {
        m_prefs.pomodoro.short_break_color = rgba_to_hex(m_btn_pomo_short_color.get_rgba());
    });
    append_row(lb_col, "Short Break", "Colour for short break ring and banner",
               m_btn_pomo_short_color);

    // Long break colour
    m_btn_pomo_long_color.set_rgba(hex_to_rgba(m_prefs.pomodoro.long_break_color));
    m_btn_pomo_long_color.set_use_alpha(false);
    m_btn_pomo_long_color.set_size_request(48, 32);
    m_btn_pomo_long_color.set_tooltip_text("Ring and banner colour for long breaks");
    m_btn_pomo_long_color.signal_color_set().connect([this]() {
        m_prefs.pomodoro.long_break_color = rgba_to_hex(m_btn_pomo_long_color.get_rgba());
    });
    append_row(lb_col, "Long Break", "Colour for long break ring and banner",
               m_btn_pomo_long_color);

    // Pip (session dot) colour
    m_btn_pomo_pip_color.set_rgba(hex_to_rgba(m_prefs.pomodoro.pip_color));
    m_btn_pomo_pip_color.set_use_alpha(false);
    m_btn_pomo_pip_color.set_size_request(48, 32);
    m_btn_pomo_pip_color.set_tooltip_text("Fill colour for completed session dots");
    m_btn_pomo_pip_color.signal_color_set().connect([this]() {
        m_prefs.pomodoro.pip_color = rgba_to_hex(m_btn_pomo_pip_color.get_rgba());
    });
    append_row(lb_col, "Session Pip", "Colour for completed session indicator dots",
               m_btn_pomo_pip_color);

    return scroll;
}

// ─────────────────────────────────────────────────────────────────────────────
// build_page_editing — Spell Check + Text Substitution
// ─────────────────────────────────────────────────────────────────────────────

void PreferencesDialog::rebuild_ac_pairs_list() {
    if (!m_ac_pairs_box) return;

    while (auto* child = m_ac_pairs_box->get_first_child())
        m_ac_pairs_box->remove(*child);

    // Helper: build one editable row
    auto make_row = [this](int i) {
        auto* row_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        row_box->set_margin_top(4);
        row_box->set_margin_bottom(4);
        row_box->set_margin_start(8);
        row_box->set_margin_end(8);

        auto* from_entry = Gtk::make_managed<Gtk::Entry>();
        from_entry->set_text(m_working_ac_pairs[i].first);
        from_entry->set_placeholder_text("From");
        from_entry->set_hexpand(true);
        from_entry->signal_changed().connect([this, from_entry, i]() {
            if (i < (int)m_working_ac_pairs.size())
                m_working_ac_pairs[i].first = std::string(from_entry->get_text());
        });

        auto* arrow = Gtk::make_managed<Gtk::Label>("\xe2\x86\x92"); // →
        arrow->set_margin_start(4);
        arrow->set_margin_end(4);

        auto* to_entry = Gtk::make_managed<Gtk::Entry>();
        to_entry->set_text(m_working_ac_pairs[i].second);
        to_entry->set_placeholder_text("To  (right-click for ∞ picker)");
        to_entry->set_hexpand(true);
        to_entry->signal_changed().connect([this, to_entry, i]() {
            if (i < (int)m_working_ac_pairs.size())
                m_working_ac_pairs[i].second = std::string(to_entry->get_text());
        });

        auto rc = Gtk::GestureClick::create();
        rc->set_button(GDK_BUTTON_SECONDARY);
        rc->signal_pressed().connect([this, to_entry](int, double x, double y) {
            if (!m_ac_pair_picker) return;
            m_ac_pair_picker->set_target(to_entry);
            double wx = 0, wy = 0;
            to_entry->translate_coordinates(*this, x, y, wx, wy);
            Gdk::Rectangle rect((int)wx, (int)wy, 1, 1);
            m_ac_pair_picker->set_pointing_to(rect);
            m_ac_pair_picker->popup();
        });
        to_entry->add_controller(rc);

        auto* del_btn = Gtk::make_managed<Gtk::Button>();
        del_btn->set_icon_name("list-remove-symbolic");
        del_btn->add_css_class("flat");
        del_btn->set_tooltip_text("Remove this pair");
        del_btn->signal_clicked().connect([this, i]() {
            if (i < (int)m_working_ac_pairs.size()) {
                m_working_ac_pairs.erase(m_working_ac_pairs.begin() + i);
                rebuild_ac_pairs_list();
            }
        });

        row_box->append(*from_entry);
        row_box->append(*arrow);
        row_box->append(*to_entry);
        row_box->append(*del_btn);
        return row_box;
    };

    // Helper: section divider label
    auto make_group_label = [](const std::string& text) {
        auto* lbl = Gtk::make_managed<Gtk::Label>(text);
        lbl->add_css_class("pref-group-title");
        lbl->set_halign(Gtk::Align::START);
        lbl->set_margin_top(10);
        lbl->set_margin_bottom(2);
        lbl->set_margin_start(8);
        return lbl;
    };

    // Partition pairs into word corrections vs special characters.
    // Special char pairs are those whose "from" starts and ends with < >.
    // (e.g. <nbsp>, <wj>) or are typographic symbols (---, --, (c), (r), (tm))
    auto is_special = [](const std::string& from) -> bool {
        if (from.size() >= 3 && from.front() == '<' && from.back() == '>') return true;
        if (from == "---" || from == "--") return true;
        if (from == "(c)" || from == "(r)" || from == "(tm)") return true;
        return false;
    };

    bool has_word = false, has_special = false;
    for (auto& p : m_working_ac_pairs) {
        if (is_special(p.first)) has_special = true;
        else has_word = true;
    }

    if (has_word) {
        m_ac_pairs_box->append(*make_group_label("Word Corrections"));
        for (int i = 0; i < (int)m_working_ac_pairs.size(); ++i)
            if (!is_special(m_working_ac_pairs[i].first))
                m_ac_pairs_box->append(*make_row(i));
    }

    if (has_special) {
        m_ac_pairs_box->append(*make_group_label(
            "Special Characters  \xe2\x80\x94  type the shortcut to insert"));
        for (int i = 0; i < (int)m_working_ac_pairs.size(); ++i)
            if (is_special(m_working_ac_pairs[i].first))
                m_ac_pairs_box->append(*make_row(i));
    }

    // Bottom button row: Add Pair + Reset to defaults
    auto* btn_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    btn_row->set_margin_top(6);
    btn_row->set_margin_start(8);
    btn_row->set_margin_bottom(4);

    auto* add_btn = Gtk::make_managed<Gtk::Button>("+ Add Pair");
    add_btn->add_css_class("flat");
    add_btn->signal_clicked().connect([this]() {
        m_working_ac_pairs.push_back({"", ""});
        rebuild_ac_pairs_list();
    });

    auto* reset_btn = Gtk::make_managed<Gtk::Button>("Reset to defaults");
    reset_btn->add_css_class("flat");
    reset_btn->add_css_class("prefs-reset-btn");
    reset_btn->signal_clicked().connect([this]() {
        m_working_ac_pairs.clear();
        for (auto& p : FolioPrefs{}.autocorrect_pairs)
            m_working_ac_pairs.push_back(p);
        rebuild_ac_pairs_list();
    });

    btn_row->append(*add_btn);
    btn_row->append(*reset_btn);
    m_ac_pairs_box->append(*btn_row);
}

// ─────────────────────────────────────────────────────────────────────────────
// rebuild_sep_list
// ─────────────────────────────────────────────────────────────────────────────

void PreferencesDialog::rebuild_sep_list() {
    if (!m_sep_list_box) return;

    while (auto* child = m_sep_list_box->get_first_child())
        m_sep_list_box->remove(*child);

    for (int i = 0; i < (int)m_working_separators.size(); ++i) {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        row->set_margin_top(3);
        row->set_margin_bottom(3);
        row->set_margin_start(8);
        row->set_margin_end(8);

        auto* entry = Gtk::make_managed<Gtk::Entry>();
        entry->set_text(m_working_separators[i]);
        entry->set_placeholder_text("e.g.  ---  or  * * *");
        entry->set_hexpand(true);
        // No signal_changed — values are flushed from the widget tree in apply_and_close.

        auto* del_btn = Gtk::make_managed<Gtk::Button>();
        del_btn->set_icon_name("list-remove-symbolic");
        del_btn->add_css_class("flat");
        del_btn->set_tooltip_text("Remove this separator");
        del_btn->signal_clicked().connect([this, i]() {
            if (i < (int)m_working_separators.size()) {
                m_working_separators.erase(m_working_separators.begin() + i);
                rebuild_sep_list();
            }
        });

        row->append(*entry);
        row->append(*del_btn);
        m_sep_list_box->append(*row);
    }

    auto* add_btn = Gtk::make_managed<Gtk::Button>("+ Add Separator");
    add_btn->add_css_class("flat");
    add_btn->set_margin_top(4);
    add_btn->set_margin_bottom(4);
    add_btn->set_margin_start(8);
    add_btn->set_halign(Gtk::Align::START);
    add_btn->signal_clicked().connect([this]() {
        m_working_separators.push_back("---");
        rebuild_sep_list();
    });
    m_sep_list_box->append(*add_btn);
}

Gtk::Widget* PreferencesDialog::build_page_editing() {
    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroll->set_hexpand(true);
    scroll->set_vexpand(true);

    auto* outer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    outer->set_margin_top(24);
    outer->set_margin_bottom(24);
    outer->set_margin_start(32);
    outer->set_margin_end(32);
    scroll->set_child(*outer);

    // ── Spell Check section ───────────────────────────────────────────────────
    Gtk::ListBox* lb_spell = nullptr;
    outer->append(*make_section("Spell Check", lb_spell));

    // Enable toggle
    m_sw_spell_enabled.set_active(m_prefs.spell_check_enabled);
    append_row(lb_spell, "Enable Spell Check",
               "Highlight misspelled words as you type", m_sw_spell_enabled);

    // Language dropdown — populated from actually installed enchant dictionaries
    m_spell_languages = SpellChecker::available_languages();
    // Sort alphabetically for easy browsing
    std::sort(m_spell_languages.begin(), m_spell_languages.end());
    {
        auto lang_model = Gtk::StringList::create({"System default"});
        for (auto& l : m_spell_languages) lang_model->append(l);
        m_dd_spell_language = Gtk::make_managed<Gtk::DropDown>(lang_model);
        m_dd_spell_language->set_enable_search(true);

        // Select current pref
        guint sel = 0;
        if (!m_prefs.spell_language.empty()) {
            for (int i = 0; i < (int)m_spell_languages.size(); ++i) {
                if (m_spell_languages[i] == m_prefs.spell_language) {
                    sel = (guint)(i + 1);
                    break;
                }
            }
        }
        m_dd_spell_language->set_selected(sel);
        append_row(lb_spell, "Language",
                   "Dictionary language for spell checking", *m_dd_spell_language);
    }

    // ── Error Appearance section ──────────────────────────────────────────────
    Gtk::ListBox* lb_appear = nullptr;
    outer->append(*make_section("Error Appearance", lb_appear));

    // Underline style dropdown
    {
        auto style_model = Gtk::StringList::create({"Wavy", "Single", "Double"});
        m_dd_spell_underline_style = Gtk::make_managed<Gtk::DropDown>(style_model);
        guint sel = 0;
        if      (m_prefs.spell_underline_style == "single") sel = 1;
        else if (m_prefs.spell_underline_style == "double") sel = 2;
        m_dd_spell_underline_style->set_selected(sel);
        append_row(lb_appear, "Underline Style",
                   "Shape of the error underline", *m_dd_spell_underline_style);
    }

    // Underline color
    m_btn_spell_color.set_rgba(hex_to_rgba(m_prefs.spell_underline_color));
    m_btn_spell_color.set_use_alpha(false);
    m_btn_spell_color.set_size_request(48, 32);
    m_btn_spell_color.set_tooltip_text("Colour of the error underline");
    append_row(lb_appear, "Underline Color",
               "Color used to mark misspelled words", m_btn_spell_color);

    // Bold (thicker) underline
    m_sw_spell_bold.set_active(m_prefs.spell_underline_bold);
    append_row(lb_appear, "Double Underline",
               "Use a double underline for greater visibility", m_sw_spell_bold);

    // Background tint toggle
    m_sw_spell_bg_tint.set_active(m_prefs.spell_background_tint);
    append_row(lb_appear, "Background Tint",
               "Also highlight the background of misspelled words", m_sw_spell_bg_tint);

    // Background color (shown conditionally below tint toggle)
    m_btn_spell_bg_color.set_rgba(hex_to_rgba(m_prefs.spell_background_color));
    m_btn_spell_bg_color.set_use_alpha(false);
    m_btn_spell_bg_color.set_size_request(48, 32);
    m_btn_spell_bg_color.set_tooltip_text("Background highlight colour for misspelled words");
    // Store row pointer so we can show/hide it
    {
        // We append to lb_appear directly and track visibility
        auto* bg_row_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        bg_row_box->set_margin_top(8);
        bg_row_box->set_margin_bottom(8);
        bg_row_box->set_margin_start(12);
        bg_row_box->set_margin_end(12);
        auto* bg_lbl = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        auto* t = Gtk::make_managed<Gtk::Label>("Background Color");
        t->set_halign(Gtk::Align::START);
        auto* s = Gtk::make_managed<Gtk::Label>("Background highlight colour for misspelled words");
        s->set_halign(Gtk::Align::START);
        s->add_css_class("dim-label");
        bg_lbl->append(*t);
        bg_lbl->append(*s);
        bg_lbl->set_hexpand(true);
        bg_row_box->append(*bg_lbl);
        bg_row_box->append(m_btn_spell_bg_color);
        auto* bg_row = Gtk::make_managed<Gtk::ListBoxRow>();
        bg_row->set_child(*bg_row_box);
        bg_row->set_selectable(false);
        bg_row->set_visible(m_prefs.spell_background_tint);
        lb_appear->append(*bg_row);
        m_spell_bg_color_row = lb_appear; // store lb so we can find the row

        // Toggle visibility when tint switch changes
        m_sw_spell_bg_tint.property_active().signal_changed().connect([bg_row, this]() {
            bg_row->set_visible(m_sw_spell_bg_tint.get_active());
        });
    }

    // ── Text Substitution section ─────────────────────────────────────────────
    Gtk::ListBox* lb_sub = nullptr;
    outer->append(*make_section("Text Substitution", lb_sub));

    m_sw_sub_smart_quotes.set_active(m_prefs.sub_smart_quotes);
    append_row(lb_sub, "Smart Quotes",
               "Replace straight quotes with curly quotes as you type",
               m_sw_sub_smart_quotes);

    m_sw_sub_em_dash.set_active(m_prefs.sub_em_dash);
    append_row(lb_sub, "Em Dash",
               "Replace -- with an em dash (—)",
               m_sw_sub_em_dash);

    m_sw_sub_ellipsis.set_active(m_prefs.sub_ellipsis);
    append_row(lb_sub, "Ellipsis",
               "Replace ... with a typographic ellipsis (…)",
               m_sw_sub_ellipsis);

    m_sw_sub_autocorrect.set_active(m_prefs.sub_autocorrect);
    append_row(lb_sub, "Autocorrect",
               "Apply user-defined word replacements as you type",
               m_sw_sub_autocorrect);

    // ── Autocorrect Pairs section ─────────────────────────────────────────────
    Gtk::ListBox* lb_ac = nullptr;
    outer->append(*make_section("Autocorrect Pairs", lb_ac));

    // Explanation label
    auto* ac_desc = Gtk::make_managed<Gtk::Label>(
        "Word corrections replace as you type. Special character shortcuts "
        "(e.g. <nbsp>, <wj>) insert invisible or typographic characters. "
        "Right-click any \xe2\x80\x9cTo\xe2\x80\x9d field to pick from the Unicode palette.\n"
        "Keyboard shortcuts: Ctrl+Space\xc2\xa0= word joiner  "
        "Ctrl+Shift+Space\xc2\xa0= non-breaking space  "
        "Ctrl+Shift+\xe2\x88\x92\xc2\xa0= non-breaking hyphen  "
        "Ctrl+\xe2\x88\x92\xc2\xa0= soft hyphen  "
        "Ctrl+Shift+Z\xc2\xa0= zero-width space  "
        "Ctrl+Shift+T\xc2\xa0= thin space");
    ac_desc->set_halign(Gtk::Align::START);
    ac_desc->set_wrap(true);
    ac_desc->add_css_class("dim-label");
    ac_desc->set_margin_bottom(8);

    auto* ac_row = Gtk::make_managed<Gtk::ListBoxRow>();
    ac_row->set_selectable(false);
    auto* ac_outer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    ac_outer->set_margin_top(8);
    ac_outer->set_margin_bottom(8);
    ac_outer->set_margin_start(8);
    ac_outer->set_margin_end(8);
    ac_outer->append(*ac_desc);

    m_ac_pairs_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    ac_outer->append(*m_ac_pairs_box);
    ac_row->set_child(*ac_outer);
    lb_ac->append(*ac_row);

    // Populate working copy and render
    m_working_ac_pairs = m_prefs.autocorrect_pairs;
    // Create the shared picker once — owned by unique_ptr, parented to the
    // dialog, retargeted per row.  Unparented explicitly in the destructor.
    if (!m_ac_pair_picker) {
        m_ac_pair_picker = std::make_unique<UnicodePickerPopover>();
        m_ac_pair_picker->set_parent(*this);
    }
    rebuild_ac_pairs_list();

    // ── Document Splitting ────────────────────────────────────────────────────
    Gtk::ListBox* lb_split = nullptr;
    outer->append(*make_section("Document Splitting", lb_split));

    auto* split_desc = Gtk::make_managed<Gtk::Label>(
        "Lines exactly matching any of these patterns will split the scene "
        "into separate scenes when using Split on Separator.");
    split_desc->set_halign(Gtk::Align::START);
    split_desc->set_wrap(true);
    split_desc->add_css_class("dim-label");
    split_desc->set_margin_bottom(8);

    auto* split_row = Gtk::make_managed<Gtk::ListBoxRow>();
    split_row->set_selectable(false);
    auto* split_outer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    split_outer->set_margin_top(8);
    split_outer->set_margin_bottom(8);
    split_outer->set_margin_start(8);
    split_outer->set_margin_end(8);
    split_outer->append(*split_desc);

    m_sep_list_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    split_outer->append(*m_sep_list_box);
    split_row->set_child(*split_outer);
    lb_split->append(*split_row);

    m_working_separators = m_prefs.split_separators;
    rebuild_sep_list();

    return scroll;
}

// ─────────────────────────────────────────────────────────────────────────────
// build_page_headings
// ─────────────────────────────────────────────────────────────────────────────

Gtk::Widget* PreferencesDialog::build_page_headings() {
    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroll->set_hexpand(true);
    scroll->set_vexpand(true);

    auto* outer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    outer->set_margin_top(24);
    outer->set_margin_bottom(24);
    outer->set_margin_start(32);
    outer->set_margin_end(32);
    scroll->set_child(*outer);

    auto* tl = Gtk::make_managed<Gtk::Label>("Outlining");
    tl->add_css_class("prefs-page-title");
    tl->set_halign(Gtk::Align::START);
    outer->append(*tl);

    // ── Global settings ───────────────────────────────────────────────────────
    Gtk::ListBox* lb_global = nullptr;
    outer->append(*make_section("Global", lb_global));

    // Number of levels
    auto* levels_spin = Gtk::make_managed<Gtk::SpinButton>();
    setup_spin(*levels_spin, (double)m_prefs.outline_levels,
               3, MAX_OUTLINE_LEVELS, 1, 1, 0);
    levels_spin->set_width_chars(4);
    levels_spin->signal_value_changed().connect([this, levels_spin]() {
        m_prefs.outline_levels = (int)levels_spin->get_value();
        m_changed = true;
    });
    append_row(lb_global, "Number of levels",
               "Active outline levels (3–9). Higher levels inherit defaults.",
               *levels_spin);

    // Preset dropdown
    auto preset_model = Gtk::StringList::create({});
    for (int p = 0; p < HEADING_PRESET_COUNT; ++p)
        preset_model->append(HEADING_PRESETS[p].name);
    preset_model->append("Custom");

    auto* preset_dd = Gtk::make_managed<Gtk::DropDown>(preset_model);
    preset_dd->set_hexpand(true);

    auto detect_preset = [this]() -> int {
        for (int p = 0; p < HEADING_PRESET_COUNT; ++p) {
            bool match = true;
            for (int i = 0; i < m_prefs.outline_levels; ++i) {
                if (m_prefs.heading_styles[i].marker    != HEADING_PRESETS[p].markers[i] ||
                    m_prefs.heading_styles[i].separator != HEADING_PRESETS[p].separators[i])
                { match = false; break; }
            }
            if (match) return p;
        }
        return HEADING_PRESET_COUNT;
    };
    preset_dd->set_selected((guint)detect_preset());

    auto* reset_btn = Gtk::make_managed<Gtk::Button>("Reset all to defaults");
    reset_btn->add_css_class("flat");

    auto* preset_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    preset_box->set_hexpand(true);
    preset_box->append(*preset_dd);
    preset_box->append(*reset_btn);
    append_row(lb_global, "Preset",
               "Apply a numbering scheme to all levels at once",
               *preset_box);

    // ── Level editor ─────────────────────────────────────────────────────────
    Gtk::ListBox* lb_level = nullptr;
    outer->append(*make_section("Level Settings", lb_level));

    // Level selector dropdown  (Level 1 … Level N)
    auto level_sel_model = Gtk::StringList::create({});
    for (int i = 0; i < m_prefs.outline_levels; ++i)
        level_sel_model->append("Level " + std::to_string(i + 1));
    auto* level_sel_dd = Gtk::make_managed<Gtk::DropDown>(level_sel_model);
    level_sel_dd->set_hexpand(true);
    level_sel_dd->set_selected(0);
    append_row(lb_level, "Edit level",
               "Select which level to configure below", *level_sel_dd);

    // ── Per-level controls (all in lb_level, updated when level changes) ─────
    const char* marker_labels[] = {
        "1, 2, 3  (Arabic)", "A, B, C  (Upper alpha)",
        "a, b, c  (Lower alpha)", "I, II, III  (Upper Roman)",
        "i, ii, iii  (Lower Roman)", "(none)"
    };
    const char* marker_values[] = { "1", "A", "a", "I", "i", "" };

    // Create the controls once, rewire on level change
    auto* scale_spin  = Gtk::make_managed<Gtk::SpinButton>();
    auto* sw_bold     = Gtk::make_managed<Gtk::Switch>();
    auto* sw_italic   = Gtk::make_managed<Gtk::Switch>();
    auto* spin_above  = Gtk::make_managed<Gtk::SpinButton>();
    auto* spin_below  = Gtk::make_managed<Gtk::SpinButton>();
    auto mk_model = Gtk::StringList::create({});
    for (auto* ml : marker_labels) mk_model->append(ml);
    auto* mk_dd = Gtk::make_managed<Gtk::DropDown>(mk_model);
    auto* sep_entry = Gtk::make_managed<Gtk::Entry>();

    setup_spin(*scale_spin, 1.0, 0.5, 4.0, 0.05, 0.1, 2);
    scale_spin->set_width_chars(5);
    setup_spin(*spin_above, 12.0, 0, 80, 1, 4, 0);
    spin_above->set_width_chars(4);
    setup_spin(*spin_below, 4.0, 0, 80, 1, 4, 0);
    spin_below->set_width_chars(4);
    mk_dd->set_hexpand(true);
    sep_entry->set_max_length(3);
    sep_entry->set_width_chars(4);

    append_row(lb_level, "Font scale",   "Size relative to body font",                       *scale_spin);
    append_row(lb_level, "Bold",         "",                                                  *sw_bold);
    append_row(lb_level, "Italic",       "",                                                  *sw_italic);
    append_row(lb_level, "Space above",  "Extra space above this level's lines (px)",         *spin_above);
    append_row(lb_level, "Space below",  "Extra space below this level's lines (px)",         *spin_below);
    append_row(lb_level, "Marker",       "Numbering style shown at this indent level",        *mk_dd);
    append_row(lb_level, "Separator",    "Character after the number, e.g. \".\" or \")\"",  *sep_entry);

    // Signal connections — use shared_ptr<int> to track active level across rewires
    auto cur_level = std::make_shared<int>(0);
    auto updating  = std::make_shared<bool>(false);

    // Load controls from prefs for level i
    auto load_level = [this, cur_level, updating,
                       scale_spin, sw_bold, sw_italic, spin_above, spin_below,
                       mk_dd, sep_entry, marker_values](int i) {
        *updating = true;
        *cur_level = i;
        const auto& hs = m_prefs.heading_styles[i];
        scale_spin->set_value(hs.font_scale);
        sw_bold->set_active(hs.bold);
        sw_italic->set_active(hs.italic);
        spin_above->set_value((double)hs.space_above_px);
        spin_below->set_value((double)hs.space_below_px);
        guint mk_sel = 5;
        for (guint mi = 0; mi < 6; ++mi)
            if (hs.marker == marker_values[mi]) { mk_sel = mi; break; }
        mk_dd->set_selected(mk_sel);
        sep_entry->set_text(hs.separator);
        *updating = false;
    };
    load_level(0);

    // Level selector changes → reload controls
    level_sel_dd->property_selected().signal_changed().connect(
        [load_level, level_sel_dd]() {
            load_level((int)level_sel_dd->get_selected());
        });

    // Each control writes back to prefs[cur_level]
    scale_spin->signal_value_changed().connect([this, cur_level, updating, scale_spin]() {
        if (*updating) return;
        m_prefs.heading_styles[*cur_level].font_scale = scale_spin->get_value();
        m_changed = true;
    });
    sw_bold->property_active().signal_changed().connect([this, cur_level, updating, sw_bold]() {
        if (*updating) return;
        m_prefs.heading_styles[*cur_level].bold = sw_bold->get_active(); m_changed = true;
    });
    sw_italic->property_active().signal_changed().connect([this, cur_level, updating, sw_italic]() {
        if (*updating) return;
        m_prefs.heading_styles[*cur_level].italic = sw_italic->get_active(); m_changed = true;
    });
    spin_above->signal_value_changed().connect([this, cur_level, updating, spin_above]() {
        if (*updating) return;
        m_prefs.heading_styles[*cur_level].space_above_px = (int)spin_above->get_value(); m_changed = true;
    });
    spin_below->signal_value_changed().connect([this, cur_level, updating, spin_below]() {
        if (*updating) return;
        m_prefs.heading_styles[*cur_level].space_below_px = (int)spin_below->get_value(); m_changed = true;
    });
    mk_dd->property_selected().signal_changed().connect(
        [this, cur_level, updating, mk_dd, preset_dd, marker_values]() {
            if (*updating) return;
            guint sel = mk_dd->get_selected();
            if (sel < 6) m_prefs.heading_styles[*cur_level].marker = marker_values[sel];
            preset_dd->set_selected((guint)HEADING_PRESET_COUNT);
            m_changed = true;
        });
    sep_entry->signal_changed().connect(
        [this, cur_level, updating, sep_entry, preset_dd]() {
            if (*updating) return;
            m_prefs.heading_styles[*cur_level].separator = std::string(sep_entry->get_text());
            preset_dd->set_selected((guint)HEADING_PRESET_COUNT);
            m_changed = true;
        });

    // Preset applies to all levels
    preset_dd->property_selected().signal_changed().connect(
        [this, preset_dd, load_level, cur_level]() {
            guint sel = preset_dd->get_selected();
            if (sel >= (guint)HEADING_PRESET_COUNT) return;
            for (int i = 0; i < m_prefs.outline_levels; ++i) {
                m_prefs.heading_styles[i].marker    = HEADING_PRESETS[sel].markers[i];
                m_prefs.heading_styles[i].separator = HEADING_PRESETS[sel].separators[i];
            }
            load_level(*cur_level); // refresh visible controls
            m_changed = true;
        });

    // Reset all: restore factory defaults for all 9 levels
    reset_btn->signal_clicked().connect([this, preset_dd, load_level, cur_level]() {
        // Default preset = Legal for first 3, reasonable for rest
        FolioPrefs defaults;
        for (int i = 0; i < MAX_OUTLINE_LEVELS; ++i)
            m_prefs.heading_styles[i] = defaults.heading_styles[i];
        m_prefs.outline_levels = defaults.outline_levels;
        preset_dd->set_selected(HEADING_PRESET_DEFAULT);
        load_level(*cur_level);
        m_changed = true;
    });

    return scroll;
}


// ─────────────────────────────────────────────────────────────────────────────
// build_page_screenplay
// ─────────────────────────────────────────────────────────────────────────────

Gtk::Widget* PreferencesDialog::build_page_screenplay() {
    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroll->set_hexpand(true);
    scroll->set_vexpand(true);

    auto* outer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    outer->set_margin_top(24);
    outer->set_margin_bottom(24);
    outer->set_margin_start(32);
    outer->set_margin_end(32);
    scroll->set_child(*outer);

    auto* tl = Gtk::make_managed<Gtk::Label>("Screenplay");
    tl->add_css_class("prefs-page-title");
    tl->set_halign(Gtk::Align::START);
    outer->append(*tl);

    // ── Element reference ─────────────────────────────────────────────────────
    Gtk::ListBox* lb_ref = nullptr;
    outer->append(*make_section("Elements", lb_ref));

    // Static info rows — name / keyboard shortcut / what auto-sense detects
    struct ElemInfo {
        const char* name;
        const char* shortcut;
        const char* description;
    };
    static const ElemInfo elems[] = {
        { "Scene Heading", "Tab to cycle",    "Auto-detected when line starts with INT. or EXT." },
        { "Action",        "Tab to cycle",    "Default element; full-width prose." },
        { "Character",     "Tab to cycle",    "Auto-detected: ALL CAPS short line after action/dialogue." },
        { "Parenthetical", "Tab from Character", "Auto-detected: line starts with '('." },
        { "Dialogue",      "Tab to cycle",    "Indented block beneath character cue." },
        { "Transition",    "Tab to cycle",    "Auto-detected: ALL CAPS ending with ':'" },
    };
    for (const auto& e : elems) {
        auto* row_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        row_box->set_margin_top(8);
        row_box->set_margin_bottom(8);
        row_box->set_margin_start(12);
        row_box->set_margin_end(12);

        auto* name_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto* name_lbl = Gtk::make_managed<Gtk::Label>(e.name);
        name_lbl->set_halign(Gtk::Align::START);
        name_lbl->add_css_class("heading");
        auto* key_lbl = Gtk::make_managed<Gtk::Label>(e.shortcut);
        key_lbl->set_halign(Gtk::Align::END);
        key_lbl->set_hexpand(true);
        key_lbl->add_css_class("dim-label");
        name_box->append(*name_lbl);
        name_box->append(*key_lbl);

        auto* desc_lbl = Gtk::make_managed<Gtk::Label>(e.description);
        desc_lbl->set_halign(Gtk::Align::START);
        desc_lbl->add_css_class("dim-label");
        desc_lbl->set_wrap(true);
        desc_lbl->set_xalign(0.0f);

        row_box->append(*name_box);
        row_box->append(*desc_lbl);

        auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_activatable(false);
        row->set_child(*row_box);
        lb_ref->append(*row);
    }

    // ── Tab-cycle order ───────────────────────────────────────────────────────
    Gtk::ListBox* lb_cycle = nullptr;
    outer->append(*make_section("Tab Cycle Order", lb_cycle));

    // Description row
    {
        auto* desc = Gtk::make_managed<Gtk::Label>(
            "Tab advances through these elements in order. "
            "Shift+Tab goes backward. "
            "Tab from Character always goes to Parenthetical.");
        desc->set_wrap(true);
        desc->set_xalign(0.0f);
        desc->set_margin_top(8);
        desc->set_margin_bottom(8);
        desc->set_margin_start(12);
        desc->set_margin_end(12);
        auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_activatable(false);
        row->set_child(*desc);
        lb_cycle->append(*row);
    }

    // All 6 element names available in the cycle
    static const char* all_elements[] = {
        "scene", "action", "character", "parenthetical", "dialogue", "transition"
    };
    static const char* all_labels[] = {
        "Scene Heading", "Action", "Character", "Parenthetical", "Dialogue", "Transition"
    };

    // Build one row per element with Up/Down buttons and a toggle switch.
    // The cycle is a subset of all 6, in user-defined order.
    // We represent it as: for each element, is it in the cycle + what position?

    // Shared state: current ordered list as indices into all_elements
    auto cycle_indices = std::make_shared<std::vector<int>>();
    auto row_switches  = std::make_shared<std::vector<Gtk::Switch*>>();
    auto row_up_btns   = std::make_shared<std::vector<Gtk::Button*>>();
    auto row_dn_btns   = std::make_shared<std::vector<Gtk::Button*>>();
    auto updating_sp   = std::make_shared<bool>(false);

    // Build initial cycle_indices from prefs
    auto rebuild_cycle_indices = [&]() {
        cycle_indices->clear();
        for (const auto& name : m_prefs.screenplay_tab_cycle) {
            for (int i = 0; i < 6; ++i) {
                if (name == all_elements[i]) {
                    cycle_indices->push_back(i);
                    break;
                }
            }
        }
    };
    rebuild_cycle_indices();

    // Write cycle_indices back to prefs
    auto save_cycle = [this, cycle_indices]() {
        m_prefs.screenplay_tab_cycle.clear();
        for (int idx : *cycle_indices)
            m_prefs.screenplay_tab_cycle.push_back(all_elements[idx]);
        m_changed = true;
    };

    // Update button sensitivity (Up disabled for first in cycle, Down for last)
    auto refresh_buttons = [cycle_indices, row_switches, row_up_btns, row_dn_btns,
                            updating_sp]() {
        *updating_sp = true;
        for (int el = 0; el < 6; ++el) {
            // Find position of el in cycle
            int pos = -1;
            for (int p = 0; p < (int)cycle_indices->size(); ++p)
                if ((*cycle_indices)[p] == el) { pos = p; break; }
            bool in_cycle = (pos >= 0);
            (*row_switches)[el]->set_active(in_cycle);
            (*row_up_btns)[el]->set_sensitive(in_cycle && pos > 0);
            (*row_dn_btns)[el]->set_sensitive(in_cycle && pos < (int)cycle_indices->size() - 1);
        }
        *updating_sp = false;
    };

    // Create one row per element
    for (int el = 0; el < 6; ++el) {
        auto* row_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        row_box->set_margin_top(4);
        row_box->set_margin_bottom(4);
        row_box->set_margin_start(12);
        row_box->set_margin_end(12);

        auto* lbl = Gtk::make_managed<Gtk::Label>(all_labels[el]);
        lbl->set_halign(Gtk::Align::START);
        lbl->set_hexpand(true);

        auto* sw = Gtk::make_managed<Gtk::Switch>();
        sw->set_valign(Gtk::Align::CENTER);

        auto* up_btn = Gtk::make_managed<Gtk::Button>();
        up_btn->set_icon_name("go-up-symbolic");
        up_btn->add_css_class("flat");
        up_btn->set_tooltip_text("Move earlier in cycle");

        auto* dn_btn = Gtk::make_managed<Gtk::Button>();
        dn_btn->set_icon_name("go-down-symbolic");
        dn_btn->add_css_class("flat");
        dn_btn->set_tooltip_text("Move later in cycle");

        row_box->append(*lbl);
        row_box->append(*up_btn);
        row_box->append(*dn_btn);
        row_box->append(*sw);

        auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_activatable(false);
        row->set_child(*row_box);
        lb_cycle->append(*row);

        row_switches->push_back(sw);
        row_up_btns->push_back(up_btn);
        row_dn_btns->push_back(dn_btn);

        // Toggle: add/remove from cycle
        sw->property_active().signal_changed().connect(
            [el, sw, cycle_indices, save_cycle, refresh_buttons, updating_sp]() {
                if (*updating_sp) return;
                bool want_in = sw->get_active();
                auto& ci = *cycle_indices;
                bool is_in = std::find(ci.begin(), ci.end(), el) != ci.end();
                if (want_in && !is_in) {
                    ci.push_back(el); // append at end
                } else if (!want_in && is_in) {
                    ci.erase(std::remove(ci.begin(), ci.end(), el), ci.end());
                }
                save_cycle();
                refresh_buttons();
            });

        // Up: swap with previous in cycle
        up_btn->signal_clicked().connect(
            [el, cycle_indices, save_cycle, refresh_buttons]() {
                auto& ci = *cycle_indices;
                for (int p = 1; p < (int)ci.size(); ++p) {
                    if (ci[p] == el) { std::swap(ci[p], ci[p-1]); break; }
                }
                save_cycle();
                refresh_buttons();
            });

        // Down: swap with next in cycle
        dn_btn->signal_clicked().connect(
            [el, cycle_indices, save_cycle, refresh_buttons]() {
                auto& ci = *cycle_indices;
                for (int p = 0; p < (int)ci.size() - 1; ++p) {
                    if (ci[p] == el) { std::swap(ci[p], ci[p+1]); break; }
                }
                save_cycle();
                refresh_buttons();
            });
    }

    refresh_buttons();

    // Reset button
    auto* reset_btn = Gtk::make_managed<Gtk::Button>("Reset to defaults");
    reset_btn->add_css_class("flat");
    reset_btn->set_halign(Gtk::Align::END);
    reset_btn->set_margin_top(8);
    outer->append(*reset_btn);
    reset_btn->signal_clicked().connect(
        [cycle_indices, save_cycle, refresh_buttons]() {
            *cycle_indices = {0, 1, 2, 4}; // scene, action, character, dialogue
            save_cycle();
            refresh_buttons();
        });

    return scroll;
}


} // namespace Folio
