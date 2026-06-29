#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — StyleManagerDialog.hpp
//
// Modal dialog for creating / editing / deleting named text styles.
// Styles are persisted in FolioPrefs and applied through Editor::apply_style().
//
// Two kinds:
//   Paragraph — font, size, color, justification, bold/italic, line-height.
//               Applied to all paragraphs touched by the current selection
//               (or the paragraph containing the cursor).  ALL existing tags
//               on the affected range are cleared first.
//   Character — same properties MINUS justification.
//               Applied only to the actual selection.  All existing tags on
//               the selection are cleared first.
// ─────────────────────────────────────────────────────────────────────────────

#include <functional>
#include <gtkmm.h>
#include <string>
#include <vector>

#include "FolioPrefs.hpp"

namespace Folio {

// ─────────────────────────────────────────────────────────────────────────────
// StyleManagerDialog
// ─────────────────────────────────────────────────────────────────────────────

class StyleManagerDialog : public Gtk::Window {
public:
    explicit StyleManagerDialog(Gtk::Window &parent, FolioPrefs &prefs);
    ~StyleManagerDialog() = default;

    // Called whenever the style list changes (add / rename / delete).
    // Connect this to rebuild the Editor toolbar dropdown.
    std::function<void()> on_styles_changed;

private:
    FolioPrefs &m_prefs;

    // ── Left panel — list of styles ───────────────────────────────────────
    Gtk::ListBox        m_style_list;
    Gtk::ScrolledWindow m_list_scroll;
    Gtk::Box            m_left_panel{Gtk::Orientation::VERTICAL, 0};
    Gtk::Box            m_list_actions{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Button         m_btn_add_para;
    Gtk::Button         m_btn_add_char;
    Gtk::Button         m_btn_delete;
    Gtk::Button         m_btn_reset;   // Reset to built-in defaults

    // ── Right panel — editor for selected style ────────────────────────
    Gtk::Stack          m_editor_stack;   // "empty" | "editor"
    Gtk::Box            m_editor_pane{Gtk::Orientation::VERTICAL, 12};
    Gtk::Box            m_right_panel{Gtk::Orientation::VERTICAL, 0};

    // Name
    Gtk::Entry          m_name_entry;

    // Font family dropdown
    Gtk::DropDown      *m_font_dd = nullptr;
    std::vector<std::string> m_font_names;

    // Size spin
    Gtk::SpinButton     m_size_spin;

    // Style toggles
    Gtk::ToggleButton   m_btn_bold;
    Gtk::ToggleButton   m_btn_italic;
    Gtk::ToggleButton   m_btn_underline;

    // Justification (paragraph only)
    Gtk::ToggleButton   m_just_left;
    Gtk::ToggleButton   m_just_center;
    Gtk::ToggleButton   m_just_right;
    Gtk::ToggleButton   m_just_full;
    Gtk::Box            m_just_box{Gtk::Orientation::HORIZONTAL, 2};
    Gtk::Box           *m_just_row = nullptr;   // hidden for character styles

    // Color — checkbox enables the swatch; transparent button applies alpha=0
    Gtk::ColorDialogButton *m_color_btn      = nullptr;
    Gtk::ColorDialogButton *m_bg_btn         = nullptr;
    Gtk::CheckButton        m_chk_fg;                   // checked = colour is set
    Gtk::CheckButton        m_chk_bg;
    Gtk::Button            *m_btn_fg_trans   = nullptr; // apply fully transparent colour
    Gtk::Button            *m_btn_bg_trans   = nullptr;
    bool                    m_fg_set         = false;   // mirrors m_chk_fg state
    bool                    m_bg_set         = false;
    bool                    m_fg_transparent = false;   // saved as "transparent"
    bool                    m_bg_transparent = false;

    // Line height (paragraph only)
    Gtk::SpinButton    *m_lh_spin = nullptr;
    Gtk::Box           *m_lh_row  = nullptr;

    // Paragraph spacing (paragraph only) — s88
    Gtk::SpinButton    *m_space_above_spin = nullptr;
    Gtk::Box           *m_space_above_row  = nullptr;
    Gtk::SpinButton    *m_space_below_spin = nullptr;
    Gtk::Box           *m_space_below_row  = nullptr;

    // First-line indent (paragraph only, tri-state) — s88.
    // m_indent_inherit checked → store -1 (inherit global); otherwise the spin
    // value (0 = explicitly none, >0 = explicit indent).
    Gtk::CheckButton    m_indent_inherit;
    Gtk::SpinButton    *m_indent_spin = nullptr;
    Gtk::Box           *m_indent_row  = nullptr;

    // Preview label
    Gtk::Label          m_preview;
    Glib::RefPtr<Gtk::CssProvider> m_preview_css;

    // Save button
    Gtk::Button         m_btn_save;

    // ── State ─────────────────────────────────────────────────────────────
    int                 m_selected_idx = -1;   // index into m_prefs.text_styles
    bool                m_inhibit = false;     // block change signals during load
    int                 m_drag_src_idx = -1;   // row being dragged (DnD reorder)
    // True when the style order changed (e.g. open-time regroup) but the editor
    // dropdown has not yet been rebuilt to match. Flushed on close.
    bool                m_styles_dirty = false;

    // CSS for section headers + drag/drop feedback (added once, for the display)
    Glib::RefPtr<Gtk::CssProvider> m_dnd_css;

    // ── Helpers ───────────────────────────────────────────────────────────
    void build_ui();
    void rebuild_style_list();
    void load_style_to_editor(int idx);
    void save_editor_to_style();
    void update_preview();
    void select_row(int idx);

    // Group the style vector paragraph-first, character-second (stable). The
    // two sections in the list map onto these two contiguous ranges. Returns
    // true if the order actually changed.
    bool regroup_styles();
    // Move a style to a new position within its own section (DnD reorder).
    void reorder_style(int src, int dst, bool after);
    // Fire on_styles_changed (rebuild the editor dropdown) and clear the dirty
    // flag — the editor maps dropdown index → style, so its order must match.
    void notify_styles_changed();

    Gtk::ListBoxRow *make_style_row(const TextStyle &s, int idx);
};

} // namespace Folio
