// ─────────────────────────────────────────────────────────────────────────────
// Folio — EditorRuler.hpp
// ─────────────────────────────────────────────────────────────────────────────

#pragma once

#include "FolioPrefs.hpp"
#include "RulerUnits.hpp"
#include <gtkmm/drawingarea.h>
#include <gtkmm/eventcontrollermotion.h>
#include <gtkmm/gesturedrag.h>
#include <gtkmm/gestureclick.h>
#include <sigc++/signal.h>
#include <vector>

namespace Folio {

class EditorRuler : public Gtk::DrawingArea {
public:
    explicit EditorRuler(FolioPrefs& prefs);

    void sync_geometry(int viewport_w, int page_px, int margin_px,
                       int first_indent_px, int left_indent_px,
                       int right_indent_px);

    // Signals
    sigc::signal<void(int)>         signal_page_width_changed;   // new pct (15-100)
    sigc::signal<void(int)>         signal_margin_changed;       // new margin px
    sigc::signal<void(int)>         signal_first_indent_changed; // px
    sigc::signal<void(int)>         signal_left_indent_changed;  // px
    sigc::signal<void(int)>         signal_right_indent_changed; // px
    sigc::signal<void(TabStop)>     signal_tab_stop_added;
    sigc::signal<void(int,TabStop)> signal_tab_stop_moved;
    sigc::signal<void(int)>         signal_tab_stop_removed;

    static constexpr int HEIGHT  = 28;
    static constexpr int TYPE_W  = 26;
    static constexpr int HANDLE_R = 7;

private:
    void on_draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
    void draw_background   (const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
    void draw_ticks        (const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);
    void draw_handles      (const Cairo::RefPtr<Cairo::Context>& cr, int h);
    void draw_tab_stops    (const Cairo::RefPtr<Cairo::Context>& cr, int h);
    void draw_type_selector(const Cairo::RefPtr<Cairo::Context>& cr, int h);

    double page_off_to_ruler_x(double off) const;
    double ruler_x_to_page_off(double rx)  const;

    int m_viewport_w      = 800;
    int m_page_px         = 520;
    int m_margin_px       = 64;
    int m_first_indent_px = 32;
    int m_left_indent_px  = 0;
    int m_right_indent_px = 0;
    int m_page_left_x     = 0;

    enum class DragTarget {
        None,
        PageLeft, PageRight,
        FirstIndent, LeftIndent, RightIndent,
        TabStop
    };
    DragTarget m_drag_target     = DragTarget::None;
    int        m_drag_tab_idx    = -1;
    double     m_drag_orig_first  = 0;
    double     m_drag_orig_left   = 0;
    double     m_drag_orig_right  = 0;
    double     m_drag_orig_margin = 0;
    double     m_drag_orig_page_px = 0;
    double     m_drag_orig_tab    = 0;

    // Hover state for visual feedback
    DragTarget m_hover_target    = DragTarget::None;
    int        m_hover_tab_idx   = -1;

    Glib::RefPtr<Gtk::GestureDrag>           m_drag;
    Glib::RefPtr<Gtk::GestureClick>          m_click;
    Glib::RefPtr<Gtk::EventControllerMotion> m_motion;

    void on_drag_begin (double x, double y);
    void on_drag_update(double dx, double dy);
    void on_drag_end   (double dx, double dy);
    void on_click      (int n, double x, double y);

    DragTarget hit_test(double rx, double ry, int* tab_idx = nullptr) const;

    const char* current_type_label() const;
    void        cycle_tab_type();

    FolioPrefs& m_prefs;
    RulerUnit   unit() const { return RulerUnits::from_string(m_prefs.ruler_unit); }
};

} // namespace Folio
