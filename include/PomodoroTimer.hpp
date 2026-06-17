#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — PomodoroTimer.hpp
//
// Pure-logic Pomodoro state machine.  No GTK dependencies — the UI layer
// (PomodoroDialog) owns the GLib timeout and calls tick() every second.
//
// Phase sequence (one full cycle = 4 focus sessions):
//   Focus → Short Break → Focus → Short Break →
//   Focus → Short Break → Focus → Long Break  → (repeat)
//
// All durations are stored as minutes in FolioPrefs and converted to seconds
// here.  The caller receives phase-change notifications via on_phase_changed
// and on_cycle_complete callbacks.
// ─────────────────────────────────────────────────────────────────────────────

#include <functional>
#include <string>
#include <cmath>

namespace Folio {

// ─── Phase ───────────────────────────────────────────────────────────────────
enum class PomodoroPhase {
    Focus,
    ShortBreak,
    LongBreak,
};

inline std::string phase_label(PomodoroPhase p) {
    switch (p) {
        case PomodoroPhase::Focus:      return "Focus";
        case PomodoroPhase::ShortBreak: return "Short Break";
        case PomodoroPhase::LongBreak:  return "Long Break";
    }
    return "";
}

// ─── PomodoroTimer ───────────────────────────────────────────────────────────
class PomodoroTimer {
public:
    // ── Configuration (set before start; readable at any time) ───────────────
    int focus_sec       = 25 * 60;  // seconds
    int short_break_sec = 5  * 60;
    int long_break_sec  = 15 * 60;
    int sessions_before_long = 4;   // focus sessions per cycle

    // ── Callbacks (set once by PomodoroDialog) ────────────────────────────────
    // Fired when a phase boundary is crossed (timer naturally expires).
    std::function<void(PomodoroPhase finished, PomodoroPhase next)> on_phase_changed;
    // Fired each second with updated progress so the UI can redraw.
    std::function<void()> on_tick;

    // ── State accessors (read-only for UI) ───────────────────────────────────
    PomodoroPhase phase()      const { return m_phase; }
    bool          running()    const { return m_running; }
    int           elapsed_sec()const { return m_elapsed; }
    int           total_sec()  const { return phase_duration(m_phase); }
    int           remaining_sec() const {
        return std::max(0, total_sec() - m_elapsed);
    }
    // 0.0 → 1.0 progress through current phase
    double        progress()   const {
        int tot = total_sec();
        return (tot > 0) ? std::min(1.0, (double)m_elapsed / tot) : 0.0;
    }
    // How many focus sessions have completed in this cycle (0–sessions_before_long)
    int           session_in_cycle() const { return m_session_in_cycle; }
    // Total completed focus sessions since timer was created / reset
    int           total_sessions()   const { return m_total_sessions; }

    // ── Control ──────────────────────────────────────────────────────────────
    void start()  { m_running = true; }
    void pause()  { m_running = false; }
    void toggle() { m_running = !m_running; }

    // Reset to beginning of current phase, keep running state.
    void reset_phase() {
        m_elapsed = 0;
        if (on_tick) on_tick();
    }

    // Full reset — back to first Focus session, session counts zeroed.
    void reset_all() {
        m_running           = false;
        m_phase             = PomodoroPhase::Focus;
        m_elapsed           = 0;
        m_session_in_cycle  = 0;
        m_total_sessions    = 0;
        if (on_tick) on_tick();
    }

    // Called by the UI timeout every second while running.
    void tick() {
        if (!m_running) return;
        ++m_elapsed;
        if (on_tick) on_tick();
        if (m_elapsed >= total_sec())
            advance_phase();
    }

private:
    PomodoroPhase m_phase            = PomodoroPhase::Focus;
    bool          m_running          = false;
    int           m_elapsed          = 0;
    int           m_session_in_cycle = 0;   // focus sessions completed this cycle
    int           m_total_sessions   = 0;

    int phase_duration(PomodoroPhase p) const {
        switch (p) {
            case PomodoroPhase::Focus:      return focus_sec;
            case PomodoroPhase::ShortBreak: return short_break_sec;
            case PomodoroPhase::LongBreak:  return long_break_sec;
        }
        return focus_sec;
    }

    void advance_phase() {
        PomodoroPhase finished = m_phase;
        PomodoroPhase next;

        if (m_phase == PomodoroPhase::Focus) {
            ++m_session_in_cycle;
            ++m_total_sessions;
            if (m_session_in_cycle >= sessions_before_long) {
                m_session_in_cycle = 0;
                next = PomodoroPhase::LongBreak;
            } else {
                next = PomodoroPhase::ShortBreak;
            }
        } else {
            // After any break → back to Focus
            next = PomodoroPhase::Focus;
        }

        m_phase   = next;
        m_elapsed = 0;

        if (on_phase_changed) on_phase_changed(finished, next);
    }
};

} // namespace Folio
