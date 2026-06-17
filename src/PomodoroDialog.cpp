// ─────────────────────────────────────────────────────────────────────────────
// Folio — PomodoroDialog.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "PomodoroDialog.hpp"
#include "color_utils.hpp"

#include <cairomm/context.h>
#include <cmath>
#include <string>
#include <sstream>
#include <iomanip>

namespace Folio {

// ─── Constants ───────────────────────────────────────────────────────────────
static constexpr int    RING_SIZE       = 200;  // px, drawing area request
static constexpr double RING_THICKNESS  = 14.0; // arc stroke width
static constexpr double TWO_PI          = 2.0 * M_PI;

// ─── RGB colour type ─────────────────────────────────────────────────────────
struct RGB { double r, g, b; };

// ─── Hex → RGB helper ────────────────────────────────────────────────────────
static RGB hex_to_rgb(const std::string& hex) {
    // Delegates to the shared parser; fallback is teal.
    auto [r, g, b] = Folio::color::hex_to_rgb01(hex, 0.357, 0.784, 0.686);
    return { r, g, b };
}

// ─── Phase colours — reads from prefs, falls back to palette defaults ─────────
static RGB phase_colour_arc(PomodoroPhase p, bool dark,
                            const FolioPrefs& prefs) {
    // In dark mode use stored color directly; in light mode darken it slightly
    // by using the dark defaults as a proxy (consistent with original behaviour).
    switch (p) {
        case PomodoroPhase::Focus:
            if (dark) return hex_to_rgb(prefs.pomodoro.focus_color);
            return RGB{0.055, 0.388, 0.408};   // #0e6368 light fallback
        case PomodoroPhase::ShortBreak:
            if (dark) return hex_to_rgb(prefs.pomodoro.short_break_color);
            return RGB{0.141, 0.376, 0.094};   // #246018 light fallback
        case PomodoroPhase::LongBreak:
            if (dark) return hex_to_rgb(prefs.pomodoro.long_break_color);
            return RGB{0.353, 0.122, 0.690};   // #5a1fb0 light fallback
    }
    return hex_to_rgb(prefs.pomodoro.focus_color);
}

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
PomodoroDialog::PomodoroDialog(Gtk::Window& parent, FolioPrefs& prefs)
    : m_prefs(prefs)
{
    set_transient_for(parent);
    set_modal(true);           // modal — blocks main window while open
    set_resizable(false);
    set_title("Pomodoro");
    add_css_class("folio-pomodoro-window");

    // Sync timer durations from prefs
    apply_prefs();

    // Wire timer callbacks
    m_timer.on_tick = [this]() {
        full_refresh();
        if (m_tick_cb) m_tick_cb();
    };
    m_timer.on_phase_changed = [this](PomodoroPhase finished, PomodoroPhase next) {
        // Detect end-of-full-cycle: long break just finished → back to Focus
        bool cycle_complete = (finished == PomodoroPhase::LongBreak
                               && next  == PomodoroPhase::Focus);
        m_cycle_complete = cycle_complete;

        full_refresh();
        flash_banner();
        set_title("Pomodoro — " + phase_label(next));

        if (!m_prefs.pomodoro.auto_start)
            m_timer.pause();

        m_signal_phase_changed.emit(finished, next);
    };

    build_ui();
    full_refresh();
}

PomodoroDialog::~PomodoroDialog() {
    stop_tick();
}

// ─────────────────────────────────────────────────────────────────────────────
// apply_prefs — sync timer durations (call after PreferencesDialog closes)
// ─────────────────────────────────────────────────────────────────────────────
void PomodoroDialog::apply_prefs() {
    m_timer.focus_sec             = m_prefs.pomodoro.focus_min        * 60;
    m_timer.short_break_sec       = m_prefs.pomodoro.short_break_min  * 60;
    m_timer.long_break_sec        = m_prefs.pomodoro.long_break_min   * 60;
    m_timer.sessions_before_long  = m_prefs.pomodoro.sessions_before_long;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tick management
// ─────────────────────────────────────────────────────────────────────────────
void PomodoroDialog::start_tick() {
    if (m_tick_conn.connected()) return;
    m_tick_conn = Glib::signal_timeout().connect_seconds([this]() -> bool {
        m_timer.tick();
        return true; // keep firing
    }, 1);
}

void PomodoroDialog::stop_tick() {
    if (m_tick_conn.connected())
        m_tick_conn.disconnect();
}

// ─────────────────────────────────────────────────────────────────────────────
// build_ui
// ─────────────────────────────────────────────────────────────────────────────
void PomodoroDialog::build_ui() {
    auto* root = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    root->add_css_class("pomo-root");
    set_child(*root);

    // ── Ring + overlay ───────────────────────────────────────────────────────
    auto* ring_overlay = Gtk::make_managed<Gtk::Overlay>();
    ring_overlay->set_halign(Gtk::Align::CENTER);
    ring_overlay->set_margin_top(24);
    ring_overlay->set_margin_bottom(8);

    m_ring.set_size_request(RING_SIZE, RING_SIZE);
    m_ring.set_draw_func([this](const Cairo::RefPtr<Cairo::Context>& cr,
                                int w, int h) { draw_ring(cr, w, h); });
    m_ring.set_tooltip_text("Countdown ring — shrinks counter-clockwise as time passes");
    ring_overlay->set_child(m_ring);

    // Centre labels on top of ring via Overlay
    m_ring_overlay_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    m_ring_overlay_box->set_halign(Gtk::Align::CENTER);
    m_ring_overlay_box->set_valign(Gtk::Align::CENTER);
    m_ring_overlay_box->set_can_target(false); // pass through pointer events

    m_time_label = Gtk::make_managed<Gtk::Label>("25:00");
    m_time_label->add_css_class("pomo-time-label");
    m_time_label->set_tooltip_text("Time remaining in current phase");

    m_phase_label = Gtk::make_managed<Gtk::Label>("Focus");
    m_phase_label->add_css_class("pomo-phase-label");
    m_phase_label->set_tooltip_text("Current phase: Focus, Short Break, or Long Break");

    m_ring_overlay_box->append(*m_time_label);
    m_ring_overlay_box->append(*m_phase_label);
    ring_overlay->add_overlay(*m_ring_overlay_box);
    root->append(*ring_overlay);

    // ── Session dots ─────────────────────────────────────────────────────────
    m_dots_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    m_dots_box->set_halign(Gtk::Align::CENTER);
    m_dots_box->set_margin_bottom(12);
    m_dots_box->set_tooltip_text("Session pips — each dot represents one focus session in the current cycle");
    root->append(*m_dots_box);

    // ── Phase banner — strong visual state indicator ───────────────────────
    m_phase_banner = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    m_phase_banner->set_halign(Gtk::Align::FILL);
    m_phase_banner->set_margin_start(20);
    m_phase_banner->set_margin_end(20);
    m_phase_banner->set_margin_bottom(16);
    m_phase_banner->add_css_class("pomo-phase-banner");
    m_phase_banner->set_tooltip_text("Phase banner — shows current mode and its intent");

    m_phase_banner_icon = Gtk::make_managed<Gtk::Label>("✏️");
    m_phase_banner_icon->add_css_class("pomo-banner-icon");

    m_phase_banner_text = Gtk::make_managed<Gtk::Label>("Focus");
    m_phase_banner_text->add_css_class("pomo-banner-text");
    m_phase_banner_text->set_hexpand(true);
    m_phase_banner_text->set_xalign(0.0f);

    m_phase_banner->append(*m_phase_banner_icon);
    m_phase_banner->append(*m_phase_banner_text);
    root->append(*m_phase_banner);

    // ── Control buttons ───────────────────────────────────────────────────────
    auto* btn_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    btn_row->set_halign(Gtk::Align::CENTER);
    btn_row->set_margin_bottom(12);

    m_btn_reset = Gtk::make_managed<Gtk::Button>();
    m_btn_reset->set_icon_name("view-refresh-symbolic");
    m_btn_reset->add_css_class("pomo-ctrl-btn");
    m_btn_reset->set_tooltip_text("Reset — restart the current phase from zero without advancing the cycle");
    m_btn_reset->signal_clicked().connect([this]() {
        m_timer.reset_phase();
        full_refresh();
    });

    m_btn_play_pause = Gtk::make_managed<Gtk::Button>();
    m_btn_play_pause->add_css_class("pomo-play-btn");
    m_btn_play_pause->set_size_request(56, 56);
    m_btn_play_pause->set_tooltip_text("Start or pause the current phase");
    m_btn_play_pause->signal_clicked().connect([this]() {
        m_timer.toggle();
        if (m_timer.running())
            start_tick();
        update_play_pause_btn();
        m_ring.queue_draw();
    });

    auto* btn_settings = Gtk::make_managed<Gtk::Button>();
    btn_settings->set_icon_name("org.gnome.Settings-system-symbolic");
    btn_settings->add_css_class("pomo-ctrl-btn");
    btn_settings->set_tooltip_text("Open Pomodoro settings");
    btn_settings->signal_clicked().connect([this]() {
        if (m_open_prefs_cb) m_open_prefs_cb();
    });

    btn_row->append(*m_btn_reset);
    btn_row->append(*m_btn_play_pause);
    btn_row->append(*btn_settings);
    root->append(*btn_row);

    // Small bottom padding
    auto* pad = Gtk::make_managed<Gtk::Box>();
    pad->set_size_request(-1, 8);
    root->append(*pad);

    // Start the timeout unconditionally — PomodoroTimer::tick() guards internally
    start_tick();
}

// ─────────────────────────────────────────────────────────────────────────────
// Update helpers
// ─────────────────────────────────────────────────────────────────────────────
void PomodoroDialog::update_time_label() {
    if (!m_time_label) return;
    int rem = m_timer.remaining_sec();
    int m = rem / 60;
    int s = rem % 60;
    std::ostringstream ss;
    ss << std::setfill('0') << std::setw(2) << m
       << ':' << std::setfill('0') << std::setw(2) << s;
    m_time_label->set_text(ss.str());
}

void PomodoroDialog::update_phase_label() {
    if (!m_phase_label) return;
    m_phase_label->set_text(phase_label(m_timer.phase()));
}

void PomodoroDialog::update_play_pause_btn() {
    if (!m_btn_play_pause) return;
    m_btn_play_pause->set_icon_name(
        m_timer.running() ? "media-playback-pause-symbolic"
                          : "media-playback-start-symbolic");
}

void PomodoroDialog::update_dots() {
    if (!m_dots_box) return;

    while (auto* ch = m_dots_box->get_first_child())
        m_dots_box->remove(*ch);

    int  n         = m_timer.sessions_before_long;
    int  completed = m_timer.session_in_cycle();
    bool in_focus  = (m_timer.phase() == PomodoroPhase::Focus);

    for (int i = 0; i < n; ++i) {
        auto* dot = Gtk::make_managed<Gtk::Box>();
        dot->set_size_request(10, 10);
        dot->set_valign(Gtk::Align::CENTER);
        dot->add_css_class("pomo-dot");

        // Per-pip tooltip
        std::string tip = "Session " + std::to_string(i + 1) + " of "
                          + std::to_string(n);
        if (i < completed)
            tip += " — completed ✓";
        else if (i == completed && in_focus)
            tip += " — in progress ▶";
        else
            tip += " — pending";
        dot->set_tooltip_text(tip);

        if (i < completed)
            dot->add_css_class("pomo-dot-done");
        else if (i == completed && in_focus)
            dot->add_css_class("pomo-dot-active");
        else
            dot->add_css_class("pomo-dot-empty");

        m_dots_box->append(*dot);
    }
}

void PomodoroDialog::update_phase_banner() {
    if (!m_phase_banner || !m_phase_banner_icon || !m_phase_banner_text) return;

    m_phase_banner->remove_css_class("pomo-banner-focus");
    m_phase_banner->remove_css_class("pomo-banner-short");
    m_phase_banner->remove_css_class("pomo-banner-long");
    m_phase_banner->remove_css_class("pomo-banner-cycle");

    if (m_cycle_complete) {
        // Full work cycle just finished — show celebration state
        m_phase_banner->add_css_class("pomo-banner-cycle");
        m_phase_banner_icon->set_text("🎉");
        m_phase_banner_text->set_text("Cycle complete — great work!");
        // Clear flag once user starts the new Focus session
        if (m_timer.running() && m_timer.phase() == PomodoroPhase::Focus)
            m_cycle_complete = false;
    } else {
        switch (m_timer.phase()) {
            case PomodoroPhase::Focus:
                m_phase_banner->add_css_class("pomo-banner-focus");
                m_phase_banner_icon->set_text("✏️");
                m_phase_banner_text->set_text("Focus — time to write");
                break;
            case PomodoroPhase::ShortBreak:
                m_phase_banner->add_css_class("pomo-banner-short");
                m_phase_banner_icon->set_text("🍵");
                m_phase_banner_text->set_text("Short break");
                break;
            case PomodoroPhase::LongBreak:
                m_phase_banner->add_css_class("pomo-banner-long");
                m_phase_banner_icon->set_text("🌿");
                m_phase_banner_text->set_text("Long break — well earned");
                break;
        }
    }

    if (m_timer.running())
        m_phase_banner->remove_css_class("pomo-banner-paused");
    else
        m_phase_banner->add_css_class("pomo-banner-paused");
}

void PomodoroDialog::flash_banner() {
    if (!m_phase_banner) return;
    // Add the flash class, then schedule removal after 600 ms
    m_phase_banner->add_css_class("pomo-banner-flash");
    Glib::signal_timeout().connect_once([this]() {
        if (m_phase_banner)
            m_phase_banner->remove_css_class("pomo-banner-flash");
    }, 600);
}

void PomodoroDialog::full_refresh() {
    update_time_label();
    update_phase_label();
    update_play_pause_btn();
    update_dots();
    update_phase_banner();
    m_ring.queue_draw();
}

// ─────────────────────────────────────────────────────────────────────────────
// draw_ring — Cairo arc progress indicator
// ─────────────────────────────────────────────────────────────────────────────
void PomodoroDialog::draw_ring(const Cairo::RefPtr<Cairo::Context>& cr,
                               int w, int h) {
    double cx = w * 0.5;
    double cy = h * 0.5;
    double radius = (std::min(w, h) * 0.5) - RING_THICKNESS - 2.0;

    // Determine dark-mode by probing the window's style context
    bool dark = true;
    {
        auto style = get_style_context();
        if (style) {
            Gdk::RGBA bg;
            if (style->lookup_color("adw_bg", bg)) {
                double lum = 0.299 * bg.get_red()
                           + 0.587 * bg.get_green()
                           + 0.114 * bg.get_blue();
                dark = (lum < 0.5);
            }
        }
    }

    // ── Background track ─────────────────────────────────────────────────────
    cr->set_line_cap(Cairo::Context::LineCap::ROUND);
    cr->set_line_width(RING_THICKNESS);
    cr->arc(cx, cy, radius, -M_PI / 2.0, -M_PI / 2.0 + TWO_PI);
    if (dark)
        cr->set_source_rgba(1.0, 1.0, 1.0, 0.07);
    else
        cr->set_source_rgba(0.0, 0.0, 0.0, 0.09);
    cr->stroke();

    // ── Progress arc — counter-clockwise countdown ────────────────────────────
    double remaining = 1.0 - m_timer.progress();
    if (remaining > 0.001) {
        RGB col = phase_colour_arc(m_timer.phase(), dark, m_prefs);
        cr->set_source_rgb(col.r, col.g, col.b);
        cr->arc(cx, cy, radius,
                -M_PI / 2.0,
                -M_PI / 2.0 + remaining * TWO_PI);
        cr->stroke();
    }

    // ── Pulsing centre glow when running ─────────────────────────────────────
    if (m_timer.running()) {
        RGB col = phase_colour_arc(m_timer.phase(), dark, m_prefs);
        double inner_r = radius - RING_THICKNESS * 0.5 - 4.0;
        if (inner_r > 0) {
            auto pat = Cairo::RadialGradient::create(cx, cy, 0, cx, cy, inner_r);
            pat->add_color_stop_rgba(0.0, col.r, col.g, col.b, 0.08);
            pat->add_color_stop_rgba(1.0, col.r, col.g, col.b, 0.0);
            cr->set_source(pat);
            cr->arc(cx, cy, inner_r, 0, TWO_PI);
            cr->fill();
        }
    }
}

} // namespace Folio
