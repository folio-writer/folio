// ─────────────────────────────────────────────────────────────────────────────
// Folio — RulerManagerDialog.hpp
// Fine-tune ruler values numerically with unit conversion.
// Shows margin, indent, and tab stop controls in the user's chosen unit.
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "FolioPrefs.hpp"
#include "RulerUnits.hpp"
#include <gtkmm/window.h>
#include <gtkmm/box.h>
#include <gtkmm/button.h>
#include <gtkmm/togglebutton.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/label.h>
#include <gtkmm/dropdown.h>
#include <gtkmm/listbox.h>
#include <gtkmm/scrolledwindow.h>
#include <gtkmm/separator.h>
#include <functional>

namespace Folio {

class RulerManagerDialog : public Gtk::Window {
public:
    explicit RulerManagerDialog(Gtk::Window& parent, FolioPrefs& prefs);

    // Refresh displayed values from prefs (call when geometry changes externally)
    void refresh();

    std::function<void()> on_geometry_changed;
    std::function<void()> on_indent_changed;
    std::function<void()> on_tab_stops_changed;
    std::function<void()> on_spacing_changed;

private:
    void build();
    void rebuild_tab_list();
    void update_unit_labels();
    void update_margin_link();   // sync right spin to left when linked

    double margin_to_unit(int px) const {
        return RulerUnits::px_to_unit(px, current_unit());
    }
    double indent_to_unit() const {
        return RulerUnits::px_to_unit(m_prefs.first_line_indent_px, current_unit());
    }
    int unit_to_px(double v) const {
        return std::max(0, (int)std::round(RulerUnits::unit_to_px(v, current_unit())));
    }
    RulerUnit current_unit() const {
        return RulerUnits::from_string(m_prefs.ruler_unit);
    }

    FolioPrefs& m_prefs;

    Gtk::DropDown*   m_unit_dd          = nullptr;
    Gtk::SpinButton* m_page_width_spin  = nullptr;
    Gtk::SpinButton* m_left_margin_spin = nullptr;
    Gtk::SpinButton* m_right_margin_spin= nullptr;
    Gtk::ToggleButton* m_margins_lock   = nullptr;
    Gtk::Label*      m_left_margin_unit = nullptr;
    Gtk::Label*      m_right_margin_unit= nullptr;
    Gtk::SpinButton* m_indent_spin      = nullptr;
    Gtk::Label*      m_indent_unit      = nullptr;
    Gtk::SpinButton* m_para_spacing_spin= nullptr; // paragraph spacing (px)
    Gtk::ListBox*    m_tab_list         = nullptr;
    Gtk::Button*     m_add_tab_btn      = nullptr;

    bool m_loading = false;

    static const char* UNIT_STRINGS[];
    static const char* UNIT_LABELS[];
};

} // namespace Folio
