#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — CompileFormatDialog.hpp  (s18)
//
// The PDF compile-format editor: create / edit / duplicate / delete named
// CompileFormats, persisted in FolioPrefs (GROUP_COMPILE_FORMATS) via the pure
// CompileFormatIO seam. Opened from the ExportDialog PDF panel's "Edit formats…"
// button; on_formats_changed lets the picker rebuild + re-select afterwards.
//
// Built-ins (novel / manuscript / screenplay) are READ-ONLY — all edit controls
// go insensitive when one is selected; Duplicate makes an editable copy. The
// permission matrix (validated as a truth table before coding, s18):
//
//   selected      | edit | rename | delete | duplicate | new
//   built-in      |  no  |   no   |   no   |    yes    | yes
//   custom        | yes  |  yes   |  yes   |    yes    | yes
//
// The ElementMap (body + 9 headings + 6 screenplay elements = 16 TextFormats)
// is edited through ONE shared TextFormat control set plus a "slot" dropdown
// that swaps which entry is bound. Switching the slot loads that entry into the
// controls; editing a control writes back to the active slot. m_inhibit guards
// the load so the write-back doesn't fire while loading (same pattern as
// StyleManagerDialog).
// ─────────────────────────────────────────────────────────────────────────────

#include <functional>
#include <gtkmm.h>
#include <string>
#include <vector>

#include "CompileFormat.hpp"
#include "FolioPrefs.hpp"

namespace Folio {

class CompileFormatDialog : public Gtk::Window {
public:
    CompileFormatDialog(Gtk::Window& parent, FolioPrefs& prefs);
    ~CompileFormatDialog() = default;

    // Fired after any change to the format set (add / edit / rename / delete).
    // ExportDialog connects this to rebuild the PDF picker. The argument is the
    // name to try to re-select (the format that was just being edited), or ""
    // if nothing in particular.
    std::function<void(const std::string& select_name)> on_formats_changed;

private:
    FolioPrefs& m_prefs;

    // m_working is the live edit buffer for the selected CUSTOM format. We edit
    // a copy and commit to m_prefs.custom_compile_formats on each change so the
    // list and persistence stay in sync (matches StyleManager's commit-on-edit).
    // m_sel_idx indexes into all_compile_formats() (builtins first, then customs).
    std::vector<CompileFormat> m_all;         // snapshot rebuilt from prefs
    int  m_sel_idx  = -1;
    bool m_inhibit  = false;                  // block widget signals during load
    int  m_slot_idx = 0;                      // active ElementMap slot in the editor

    bool selected_is_builtin() const;
    int  custom_index_for(int all_idx) const; // → index into custom_compile_formats, or -1
    CompileFormat* working();                 // editable selected custom, or nullptr

    // ── Layout ────────────────────────────────────────────────────────────────
    Gtk::Box m_root{Gtk::Orientation::HORIZONTAL, 0};

    // Left: format list + actions
    Gtk::Box            m_left{Gtk::Orientation::VERTICAL, 0};
    Gtk::ListBox        m_list;
    Gtk::ScrolledWindow m_list_scroll;
    Gtk::Box            m_list_actions{Gtk::Orientation::HORIZONTAL, 4};
    Gtk::Button         m_btn_new;
    Gtk::Button         m_btn_dup;
    Gtk::Button         m_btn_del;

    // Right: editor (Stack: "empty" placeholder | "editor")
    Gtk::Stack          m_stack;
    Gtk::Label          m_empty_lbl;
    Gtk::ScrolledWindow m_editor_scroll;
    Gtk::Box            m_editor{Gtk::Orientation::VERTICAL, 14};

    Gtk::Label          m_readonly_note;      // shown for built-ins

    // Identity
    Gtk::Entry          m_name_entry;
    Gtk::CheckButton    m_radio_formal;       // mode
    Gtk::CheckButton    m_radio_adaptable;
    Gtk::CheckButton    m_chk_hyphenate;
    Gtk::CheckButton    m_chk_pb_top;

    // Page
    Gtk::DropDown*      m_paper_dd      = nullptr;
    Gtk::DropDown*      m_orient_dd     = nullptr;
    Gtk::SpinButton*    m_cw_spin       = nullptr;   // custom width  (pt)
    Gtk::SpinButton*    m_ch_spin       = nullptr;   // custom height (pt)
    Gtk::Box*           m_custom_dim_row = nullptr;  // sensitivity follows paper==Custom
    Gtk::SpinButton*    m_mi_spin       = nullptr;   // margins (pt)
    Gtk::SpinButton*    m_mo_spin       = nullptr;
    Gtk::SpinButton*    m_mt_spin       = nullptr;
    Gtk::SpinButton*    m_mb_spin       = nullptr;
    Gtk::CheckButton    m_chk_mirror;
    Gtk::Label*         m_mi_lbl        = nullptr;    // "Inner"/"Left" flips with mirror
    Gtk::Label*         m_mo_lbl        = nullptr;    // "Outer"/"Right"

    // Furniture
    Gtk::CheckButton    m_chk_title_page;
    Gtk::CheckButton    m_chk_restart;
    Gtk::CheckButton    m_chk_header;
    Gtk::Entry          m_hdr_l, m_hdr_c, m_hdr_r;
    Gtk::CheckButton    m_chk_footer;
    Gtk::Entry          m_ftr_l, m_ftr_c, m_ftr_r;

    // Element map — slot selector + one shared TextFormat control set
    Gtk::DropDown*      m_slot_dd       = nullptr;
    Gtk::Entry          m_tf_font;                   // "" = inherit
    Gtk::SpinButton*    m_tf_size       = nullptr;   // 0 = inherit
    Gtk::ToggleButton   m_tf_bold, m_tf_italic, m_tf_underline;
    Gtk::DropDown*      m_tf_align_dd   = nullptr;
    Gtk::DropDown*      m_tf_case_dd    = nullptr;
    Gtk::SpinButton*    m_tf_ls         = nullptr;   // line spacing (0 = inherit)
    Gtk::SpinButton*    m_tf_above      = nullptr;
    Gtk::SpinButton*    m_tf_below      = nullptr;
    Gtk::SpinButton*    m_tf_il         = nullptr;
    Gtk::SpinButton*    m_tf_ir         = nullptr;
    Gtk::SpinButton*    m_tf_fl         = nullptr;
    Gtk::CheckButton    m_tf_color_chk;               // checked = colour set
    Gtk::ColorDialogButton* m_tf_color  = nullptr;

    // ── Build / behaviour ─────────────────────────────────────────────────────
    void build_ui();
    Gtk::Widget* section(const std::string& title);
    Gtk::Widget* row(const std::string& label, Gtk::Widget& w);

    void rebuild_list(const std::string& select_name = "");
    Gtk::ListBoxRow* make_list_row(const CompileFormat& f);
    Gtk::Widget*     make_list_row_child(const CompileFormat& f);
    void select_all_index(int all_idx);

    void load_format_to_editor();   // m_all[m_sel_idx] → controls
    void load_slot_to_tf();         // active slot → TextFormat controls
    void commit_tf_from_controls(); // TextFormat controls → active slot of working()
    void commit_format_from_controls(); // page/furniture/identity → working()
    void persist();                 // save prefs + fire on_formats_changed

    void update_sensitivity();      // read-only gating + paper/mirror dependents

    TextFormat* active_slot_tf(CompileFormat& f);

    // Connect-all signal wiring (called once); each handler is a no-op while
    // m_inhibit is true.
    void wire_signals();
};

} // namespace Folio
