#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — PomodoroDialog.hpp
//
// A non-modal, persistent Pomodoro timer window.  It owns the GLib second-tick
// timeout, draws a Cairo arc-ring progress indicator, shows phase + session
// dots, and exposes a signal so MainWindow can fire desktop notifications on
// phase transitions.
//
// Usage:
//   m_pomodoro_dialog = std::make_unique<PomodoroDialog>(*this, m_prefs);
//   m_pomodoro_dialog->signal_phase_changed().connect([this](auto f, auto n) {
//       send_phase_notification(f, n);
//   });
//   m_pomodoro_dialog->present();
// ─────────────────────────────────────────────────────────────────────────────

#include <gtkmm.h>
#include <sigc++/sigc++.h>
#include <string>

#include "FolioPrefs.hpp"
#include "PomodoroTimer.hpp"

namespace Folio {

class PomodoroDialog : public Gtk::Window {
public:
    explicit PomodoroDialog(Gtk::Window& parent, FolioPrefs& prefs);
    ~PomodoroDialog() override;

    // Emitted when a phase naturally completes (not on manual reset).
    // Arguments: (phase that finished, phase that is starting).
    using PhaseChangedSignal =
        sigc::signal<void(PomodoroPhase finished, PomodoroPhase next)>;
    PhaseChangedSignal& signal_phase_changed() { return m_signal_phase_changed; }

    // Optional callback fired every tick (used to push state to sidebar tile).
    void set_tick_callback(std::function<void()> cb) { m_tick_cb = std::move(cb); }

    // Callback fired when the user clicks the settings button (opens prefs at Pomodoro page)
    void set_open_prefs_callback(std::function<void()> cb) { m_open_prefs_cb = std::move(cb); }

    // Timer controls callable from outside (e.g. sidebar tile play/pause button)
    void toggle_timer() {
        m_timer.toggle();
        if (m_timer.running()) start_tick();
        m_cycle_complete = false;
        full_refresh();
    }
    void start_timer() {
        m_timer.start();
        start_tick();
        m_cycle_complete = false;
        full_refresh();
        if (m_tick_cb) m_tick_cb();
    }
    void pause_timer() {
        m_timer.pause();
        full_refresh();
    }
    void reset_phase() {
        m_timer.reset_phase();
        full_refresh();
    }

    // Timer state accessors for the sidebar tile
    double      timer_progress()            const { return m_timer.progress(); }
    int         timer_remaining_sec()       const { return m_timer.remaining_sec(); }
    int         timer_elapsed_sec()         const { return m_timer.elapsed_sec(); }
    int         timer_total_sec()           const { return m_timer.total_sec(); }
    bool        timer_running()             const { return m_timer.running(); }
    std::string timer_phase_label()         const { return phase_label(m_timer.phase()); }
    int         timer_session_in_cycle()    const { return m_timer.session_in_cycle(); }
    int         timer_sessions_before_long()const { return m_timer.sessions_before_long; }

    // Sync timer durations from prefs (called after PreferencesDialog closes).
    void apply_prefs();
    // Repaint ring + banner with current prefs (call after apply_prefs).
    void refresh_display() { full_refresh(); }

private:
    // ── Data ─────────────────────────────────────────────────────────────────
    FolioPrefs&    m_prefs;
    PomodoroTimer  m_timer;
    bool           m_cycle_complete = false; // true after LongBreak → Focus transition

    // ── GLib timeout ─────────────────────────────────────────────────────────
    sigc::connection m_tick_conn;
    void start_tick();
    void stop_tick();

    // ── Widgets ──────────────────────────────────────────────────────────────
    // Ring drawing area
    Gtk::DrawingArea m_ring;

    // Phase & time labels (overlaid on ring via Gtk::Overlay)
    Gtk::Box*        m_ring_overlay_box = nullptr;
    Gtk::Label*      m_time_label       = nullptr;
    Gtk::Label*      m_phase_label      = nullptr;

    // Phase banner — coloured strip below the ring giving strong state signal
    Gtk::Box*        m_phase_banner      = nullptr;  // the coloured box
    Gtk::Label*      m_phase_banner_icon = nullptr;  // emoji icon
    Gtk::Label*      m_phase_banner_text = nullptr;  // "Focus" / "Short Break" / "Long Break"

    // Session dots  (● filled = completed this cycle)
    Gtk::Box*        m_dots_box         = nullptr;

    // Control buttons
    Gtk::Button*     m_btn_play_pause   = nullptr;
    Gtk::Button*     m_btn_reset        = nullptr;

    // ── Build helpers ────────────────────────────────────────────────────────
    void build_ui();

    // ── Update helpers ───────────────────────────────────────────────────────
    void update_time_label();
    void update_phase_label();
    void update_play_pause_btn();
    void update_dots();
    void update_phase_banner();
    void flash_banner();          // brief CSS pulse on phase transition
    void full_refresh();          // all of the above + ring.queue_draw()

    // ── Ring drawing ─────────────────────────────────────────────────────────
    void draw_ring(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h);

    // ── Signal ───────────────────────────────────────────────────────────────
    PhaseChangedSignal m_signal_phase_changed;
    std::function<void()> m_tick_cb;        // optional — fires each second
    std::function<void()> m_open_prefs_cb;  // optional — opens prefs at Pomodoro page
};

} // namespace Folio
