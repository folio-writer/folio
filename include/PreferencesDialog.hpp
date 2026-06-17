#pragma once
#include <gtkmm.h>
#include <vector>
#include <string>
#include "FolioPrefs.hpp"
#include "SpellChecker.hpp"
#include "UnicodePickerPopover.hpp"

namespace Folio {

class PreferencesDialog : public Gtk::Window {
public:
    PreferencesDialog(Gtk::Window& parent, FolioPrefs& prefs);
    ~PreferencesDialog();  // unparents m_ac_pair_picker before window closes

    // Call after construction to provide the document template names
    // so the defaults page can show a "Default Template" picker.
    void set_template_list(std::vector<std::string> names) {
        m_template_names = std::move(names);
    }
    bool was_changed() const { return m_changed; }

    // Jump directly to a named page — switches stack AND selects the nav row
    void navigate_to_page(const std::string& page_id) {
        static const char* ids[] = {
            "typography","editor","appearance","tags","saving","defaults",
            "pomodoro","editing","headings","screenplay"
        };
        for (int i = 0; i < 10; ++i) {
            if (ids[i] == page_id) {
                auto* row = m_nav_list.get_row_at_index(i);
                if (row) m_nav_list.select_row(*row);
                return;
            }
        }
        m_stack.set_visible_child(page_id);
    }

private:
    void apply_and_close();

    Gtk::Widget* build_page_typography();
    Gtk::Widget* build_page_editor();
    Gtk::Widget* build_page_appearance();
    Gtk::Widget* build_page_tags();
    Gtk::Widget* build_page_saving();
    Gtk::Widget* build_page_defaults();
    Gtk::Widget* build_page_pomodoro();
    Gtk::Widget* build_page_editing();
    Gtk::Widget* build_page_headings();
    Gtk::Widget* build_page_screenplay();

    Gtk::Box* make_section(const std::string& title, Gtk::ListBox*& out_lb);
    void append_row(Gtk::ListBox* lb, const std::string& label,
                    const std::string& sub, Gtk::Widget& control);
    // Builds a compact sub-table for one NodeDefaults inside a section
    Gtk::Box* build_defaults_block(const std::string& heading,
                                   NodeDefaults& nd,
                                   bool show_word_target,
                                   bool show_export,
                                   bool show_role = false);

    void rebuild_status_list();
    void rebuild_tags_list();
    void rebuild_roles_list();
    void rebuild_genres_list();

    FolioPrefs&   m_prefs;
    Gtk::Window&  m_parent_win;
    bool          m_changed = false;

    Gtk::Stack    m_stack;
    Gtk::ListBox  m_nav_list;

    // Typography
    Gtk::DropDown*   m_dd_editor_font = nullptr;
    Gtk::SpinButton  m_spin_editor_font_size;
    Gtk::DropDown*   m_dd_ui_font     = nullptr;
    Gtk::SpinButton  m_spin_ui_font_size;
    Gtk::SpinButton  m_spin_line_spacing;
    Gtk::DropDown*   m_dd_serif_font  = nullptr;
    Gtk::DropDown*   m_dd_sans_font   = nullptr;
    Gtk::DropDown*   m_dd_mono_font   = nullptr;

    // Font family names populated at build time
    std::vector<std::string> m_font_families;

    // Editor
    Gtk::SpinButton  m_spin_width_chars;
    Gtk::Switch      m_sw_typewriter;

    // Appearance
    Gtk::DropDown*   m_dd_theme = nullptr;
    Gtk::Switch      m_sw_wc;
    Gtk::Switch      m_sw_rt;
    Gtk::Switch      m_sw_para;

    // Saving
    Gtk::Switch      m_sw_autosave;
    Gtk::SpinButton  m_spin_autosave_interval;
    Gtk::Switch      m_sw_save_on_close;
    Gtk::SpinButton  m_spin_goal;
    Gtk::SpinButton  m_spin_max_recent;

    // Backup
    Gtk::Switch      m_sw_backup;
    Gtk::SpinButton  m_spin_backup_interval;
    Gtk::SpinButton  m_spin_backup_max;
    Gtk::Entry       m_entry_backup_dir;
    Glib::RefPtr<Gtk::FileDialog> m_backup_dir_dialog; // kept alive across async

    // Status — working vector of StatusDef (name + color), flushed to prefs.statuses on apply
    std::vector<StatusDef> m_working_statuses;
    Gtk::Box*                m_status_list_box = nullptr;

    // Character roles — working vector, flushed to prefs.character_roles on apply
    std::vector<std::string> m_working_roles;
    Gtk::Box*                m_roles_list_box  = nullptr;

    // Genres — working vector, flushed to prefs.genres on apply
    std::vector<std::string> m_working_genres;
    Gtk::Box*                m_genres_list_box = nullptr;

    // Tags — working vector, flushed to prefs.tag_colors on apply
    std::vector<TagColor>    m_working_tags;
    Gtk::Box*                m_tags_list_box   = nullptr;

    // Node creation defaults — working copies flushed on apply
    NodeDefaults m_wd_scene;
    NodeDefaults m_wd_group;
    NodeDefaults m_wd_character;
    NodeDefaults m_wd_char_group;
    NodeDefaults m_wd_place;
    NodeDefaults m_wd_place_group;
    NodeDefaults m_wd_reference;
    NodeDefaults m_wd_template;
    std::vector<std::string> m_template_names; // doc template titles for picker

    // Pomodoro
    Gtk::SpinButton  m_spin_pomo_focus;
    Gtk::SpinButton  m_spin_pomo_short;
    Gtk::SpinButton  m_spin_pomo_long;
    Gtk::SpinButton  m_spin_pomo_sessions;
    Gtk::Switch      m_sw_pomo_auto_start;
    Gtk::Switch      m_sw_pomo_headerbar;
    Gtk::ColorButton m_btn_pomo_focus_color;
    Gtk::ColorButton m_btn_pomo_short_color;
    Gtk::ColorButton m_btn_pomo_long_color;
    Gtk::ColorButton m_btn_pomo_pip_color;

    // Editing — spell check
    Gtk::Switch        m_sw_spell_enabled;
    Gtk::DropDown*     m_dd_spell_language        = nullptr;
    Gtk::DropDown*     m_dd_spell_underline_style = nullptr;
    Gtk::ColorButton   m_btn_spell_color;
    Gtk::Switch        m_sw_spell_bold;
    Gtk::Switch        m_sw_spell_bg_tint;
    Gtk::ColorButton   m_btn_spell_bg_color;
    Gtk::ListBox*      m_spell_bg_color_row       = nullptr; // shown/hidden with tint switch

    // Editing — text substitution
    Gtk::Switch  m_sw_sub_smart_quotes;
    Gtk::Switch  m_sw_sub_em_dash;
    Gtk::Switch  m_sw_sub_ellipsis;
    Gtk::Switch  m_sw_sub_autocorrect;

    // Editing — splitting
    Gtk::Box*    m_sep_list_box  = nullptr;
    std::vector<std::string> m_working_separators;
    void         rebuild_sep_list();

    // Autocorrect pair list — working copy flushed to prefs on apply
    std::vector<std::pair<std::string,std::string>> m_working_ac_pairs;
    Gtk::Box*                          m_ac_pairs_box   = nullptr;
    std::unique_ptr<UnicodePickerPopover> m_ac_pair_picker; // unparented in dtor
    void                               rebuild_ac_pairs_list();

    // Language list populated at build time
    std::vector<std::string> m_spell_languages;
};

} // namespace Folio
