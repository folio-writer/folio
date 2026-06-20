#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — ModuleBoard.hpp   (s28 — the mixing board, render-first)
//
// A read-only Cairo render of a Module's Key-Point arc as the "mixing board"
// (DESIGN — the Arc Editor, §2). Each KP is a channel strip:
//   • width  = weight        — x-laid by cumulative weight (the channel's mass)
//   • colour = position      — spectrum_hex(order/(n-1)); the palette IS the arc
//   • round dot  = frenetic  — pacing baseline fader height
//   • diamond dot = arc      — story-tension fader height
//   • two smoothed bezier curves thread the dots (frenetic / arc)
//   • KP order numbers (top) + weight numbers (bottom); high/mid/low guides
//
// s28 shipped this render-only. s29 makes the energy faders LIVE: drag a round
// (frenetic) or diamond (arc) dot to set that KP's value; the curve re-threads
// live and signal_value_changed fires so the owner writes it back to its arc.
// Right-click a dot for a precise "Set value…" entry. Weight (divider drag) and
// curve breakpoints are later slices. Structural chrome → plain GTK name.
// ─────────────────────────────────────────────────────────────────────────────

#include "Module.hpp"
#include <gtkmm/drawingarea.h>
#include <gtkmm/gesturedrag.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/eventcontrollermotion.h>
#include <gtkmm/popover.h>
#include <gtkmm/spinbutton.h>
#include <gtkmm/label.h>
#include <cairomm/context.h>
#include <utility>
#include <vector>

namespace Folio {

class ModuleBoard : public Gtk::DrawingArea {
public:
    ModuleBoard();

    // Swap the rendered arc (s29/s30 reuse: editor live-edit, library preview).
    void set_module(const Module& m);
    const Module& module() const { return m_module; }

    // s29 — fired while an energy fader is dragged or set, so the owner can write
    // the value back into its authoritative arc: (kp_index, is_arc, value 0..1).
    // is_arc=false → frenetic (round fader); true → arc (diamond fader).
    sigc::signal<void(int, bool, double)>& signal_value_changed() {
        return m_sig_changed;
    }

    // s29 — fired while a divider is dragged (weights changed); the owner pulls
    // the new per-KP weights back from module(). Pinned KPs stay single-scene and
    // rigid — the stretch transfers between the nearest UNPINNED KPs on each side.
    sigc::signal<void()>& signal_weight_changed() { return m_sig_weight; }

    // The bottom number per channel: actual scenes the planner assigns to each KP
    // (owner computes from a plan and pushes them in). Empty → show raw weight.
    void set_scene_counts(const std::vector<int>& counts);

    // Chapter sizes (scenes per chapter, in told-line order) for the chapter band
    // drawn under the channels. Owner pushes them from a plan; empty → no band.
    void set_chapters(const std::vector<int>& chapter_sizes);

private:
    Module m_module;

    // Flattened KP list in arc order (rebuilt on set_module). Mutable now — the
    // faders write energy values straight into these KPs (s29).
    std::vector<KeyPoint*> m_kps;
    void reflatten();

    void draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);

    // ── Interaction (s29) ─────────────────────────────────────────────────────
    sigc::signal<void(int, bool, double)> m_sig_changed;
    sigc::signal<void()> m_sig_weight;

    enum class Fader { None, Frenetic, Arc };

    // Geometry cached at draw time so the event handlers hit-test the SAME
    // coordinates the render drew (no second copy of the layout math).
    struct Layout { double x0{}, y0{}, x1{}, y1{}, Hp{}; bool valid = false; };
    Layout m_layout;
    std::vector<double> m_cx;                               // per-KP center x
    std::vector<double> m_edge;                             // channel x-boundaries
    std::vector<std::pair<double,double>> m_fren_pt, m_arc_pt;
    std::vector<int>    m_scene_counts;                     // bottom number (if set)
    std::vector<int>    m_chapters;                          // chapter band (if set)

    int    m_grab_kp    = -1;
    Fader  m_grab_fader = Fader::None;
    double m_grab_start_y = 0.0;
    double m_drag_value   = 0.0;     // live value while dragging (the readout)
    int    m_hover_kp    = -1;
    Fader  m_hover_fader = Fader::None;

    // Divider drag (weight = scenes-per-KP). A drag transfers weight between the
    // nearest UNPINNED KP on each side (m_grab_la / m_grab_ra); pinned KPs in
    // between keep their single-scene weight and slide rigidly.
    int    m_grab_div   = -1;        // the grabbed boundary (left KP index)
    int    m_grab_la    = -1, m_grab_ra = -1;   // left / right unpinned actors
    int    m_hover_div  = -1;
    double m_grab_div_w0 = 0.0, m_grab_div_w1 = 0.0, m_grab_div_tot = 0.0;

    Glib::RefPtr<Gtk::GestureDrag>           m_drag;
    Glib::RefPtr<Gtk::GestureClick>          m_rclick;
    Glib::RefPtr<Gtk::EventControllerMotion> m_motion;

    // Reusable "Set value…" popover (created once, reused — codebase popover
    // discipline). m_set_kp/m_set_arc remember which fader it targets.
    Gtk::Popover*    m_set_pop   = nullptr;
    Gtk::SpinButton* m_set_spin  = nullptr;
    Gtk::Label*      m_set_label = nullptr;
    int              m_set_kp   = -1;
    bool             m_set_arc  = false;
    bool             m_set_guard = false;   // suppress the spin echo while loading

    std::pair<int, Fader> hit_test(double px, double py) const;
    int    hit_divider(double px, double py) const;
    double value_from_y(double abs_y) const;   // inverse of the fader geometry
    void   write_fader(int kp, Fader f, double v);

    void on_drag_begin(double sx, double sy);
    void on_drag_update(double ox, double oy);
    void on_drag_end(double ox, double oy);
    void on_motion(double x, double y);
    void on_leave();
    void on_secondary(int n_press, double x, double y);
};

} // namespace Folio
