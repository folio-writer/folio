// ─────────────────────────────────────────────────────────────────────────────
// Folio — ModuleBoard.cpp   (s28 — render the mixing board, read-only)
//
// Cairo port of the ruler-validated reference (board_ref): channels x-laid by
// cumulative weight, filled by positional spectrum colour; round=frenetic /
// diamond=arc dots at fader height; two Catmull-Rom bezier curves through the
// dots; order + weight numbers; high/mid/low guides. No interaction (s29).
// ─────────────────────────────────────────────────────────────────────────────
#include "ModuleBoard.hpp"
#include "ModuleIO.hpp"     // ModuleIO::spectrum_hex — the proven positional ramp
#include "color_utils.hpp"  // Folio::color::hex_to_rgb01

#include <gtkmm/box.h>
#include <gtkmm/label.h>
#include <gdkmm/cursor.h>
#include <gdkmm/rectangle.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace Folio {

namespace {
// Geometry — logical px, ported verbatim from board_ref (do not drift these
// without re-rendering the ruler reference).
constexpr double L_GUT = 46, R_PAD = 14, T_PAD = 18, B_PAD = 52;

// Curve / dot inks (round=frenetic burgundy, diamond=arc light sky blue).
struct Rgb { double r, g, b; };
constexpr Rgb FREN { 0x8C/255.0, 0x1D/255.0, 0x40/255.0 };
constexpr Rgb ARC  { 0x4F/255.0, 0xC3/255.0, 0xF7/255.0 };

inline Rgb spectrum_rgb(double t) {
    auto [r, g, b] = Folio::color::hex_to_rgb01(ModuleIO::spectrum_hex(t),
                                                0.5, 0.5, 0.5);
    return { r, g, b };
}

// Catmull-Rom through pts → cubic-bezier path on cr (endpoints clamped).
void catmull_path(const Cairo::RefPtr<Cairo::Context>& cr,
                  const std::vector<std::pair<double,double>>& p) {
    if (p.empty()) return;
    cr->move_to(p[0].first, p[0].second);
    const int m = static_cast<int>(p.size());
    for (int i = 0; i < m - 1; ++i) {
        const auto& p0 = p[i > 0 ? i - 1 : 0];
        const auto& p1 = p[i];
        const auto& p2 = p[i + 1];
        const auto& p3 = p[i + 2 < m ? i + 2 : m - 1];
        const double c1x = p1.first  + (p2.first  - p0.first)  / 6.0;
        const double c1y = p1.second + (p2.second - p0.second) / 6.0;
        const double c2x = p2.first  - (p3.first  - p1.first)  / 6.0;
        const double c2y = p2.second - (p3.second - p1.second) / 6.0;
        cr->curve_to(c1x, c1y, c2x, c2y, p2.first, p2.second);
    }
}

std::string weight_label(double w) {
    char buf[16];
    if (std::abs(w - std::lround(w)) < 1e-6)
        std::snprintf(buf, sizeof(buf), "%ld", std::lround(w));
    else
        std::snprintf(buf, sizeof(buf), "%.1f", w);
    return buf;
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────────

ModuleBoard::ModuleBoard() {
    set_name("module-board");
    set_content_height(252);
    set_hexpand(true);
    m_module = built_in_folio_keypoints();
    reflatten();
    set_draw_func(sigc::mem_fun(*this, &ModuleBoard::draw));

    // ── Interaction (s29) ─────────────────────────────────────────────────────
    m_drag = Gtk::GestureDrag::create();
    m_drag->set_button(GDK_BUTTON_PRIMARY);
    m_drag->signal_drag_begin().connect(sigc::mem_fun(*this, &ModuleBoard::on_drag_begin));
    m_drag->signal_drag_update().connect(sigc::mem_fun(*this, &ModuleBoard::on_drag_update));
    m_drag->signal_drag_end().connect(sigc::mem_fun(*this, &ModuleBoard::on_drag_end));
    add_controller(m_drag);

    m_rclick = Gtk::GestureClick::create();
    m_rclick->set_button(GDK_BUTTON_SECONDARY);
    m_rclick->signal_pressed().connect(sigc::mem_fun(*this, &ModuleBoard::on_secondary));
    add_controller(m_rclick);

    m_motion = Gtk::EventControllerMotion::create();
    m_motion->signal_motion().connect(sigc::mem_fun(*this, &ModuleBoard::on_motion));
    m_motion->signal_leave().connect(sigc::mem_fun(*this, &ModuleBoard::on_leave));
    add_controller(m_motion);
}

void ModuleBoard::set_module(const Module& m) {
    m_module = m;
    reflatten();
    m_grab_kp = m_hover_kp = -1;
    m_grab_div = m_hover_div = -1;
    m_grab_fader = m_hover_fader = Fader::None;
    m_scene_counts.clear();   // owner re-pushes counts after re-planning
    m_chapters.clear();
    queue_draw();
}

void ModuleBoard::set_scene_counts(const std::vector<int>& counts) {
    m_scene_counts = counts;
    queue_draw();
}

void ModuleBoard::set_chapters(const std::vector<int>& chapter_sizes) {
    m_chapters = chapter_sizes;
    queue_draw();
}

void ModuleBoard::reflatten() {
    m_kps.clear();
    for (auto& act : m_module.craft.acts)
        for (auto& k : act.kps) m_kps.push_back(&k);
}

// ─────────────────────────────────────────────────────────────────────────────

void ModuleBoard::draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
    const int n = static_cast<int>(m_kps.size());
    m_layout.valid = false;   // set true only once geometry is computed + cached

    // Dark panel (matches the Inspector card surface).
    cr->set_source_rgb(0.118, 0.118, 0.137);
    cr->rectangle(0, 0, w, h);
    cr->fill();
    if (n == 0) return;

    const double x0 = L_GUT, y0 = T_PAD;
    const double x1 = w - R_PAD, y1 = h - B_PAD;
    const double Wp = x1 - x0, Hp = y1 - y0;
    if (Wp <= 4 || Hp <= 4) return;   // too small to render meaningfully

    cr->select_font_face("sans", Cairo::ToyFontFace::Slant::NORMAL,
                         Cairo::ToyFontFace::Weight::NORMAL);

    // ── Channel x-edges by cumulative weight ──────────────────────────────────
    double tot = 0.0;
    for (auto* k : m_kps) tot += std::max(0.0001, k->weight);
    std::vector<double> edge(n + 1);
    edge[0] = x0;
    double cum = 0.0;
    for (int i = 0; i < n; ++i) {
        cum += std::max(0.0001, m_kps[i]->weight);
        edge[i + 1] = x0 + Wp * cum / tot;
    }
    m_edge = edge;   // cache for divider hit-testing

    // ── Channels — fill = spectrum at positional t = i/(n-1) ──────────────────
    for (int i = 0; i < n; ++i) {
        const double t = (n > 1) ? static_cast<double>(i) / (n - 1) : 0.0;
        const Rgb c = spectrum_rgb(t);
        cr->set_source_rgba(c.r, c.g, c.b, 0.72);   // s28: vivid fill (was 0.24, read muddy)
        cr->rectangle(edge[i], y0, edge[i + 1] - edge[i], Hp);
        cr->fill();
    }
    cr->set_source_rgba(1, 1, 1, 0.06);
    cr->set_line_width(1);
    for (int i = 1; i < n; ++i) {
        cr->move_to(edge[i], y0);
        cr->line_to(edge[i], y1);
        cr->stroke();
    }
    // Active divider (hovered or grabbed) gets a bright handle so weight-drag is
    // discoverable. Divider d sits at edge[d+1], between KP d and KP d+1.
    {
        const int active = (m_grab_div >= 0) ? m_grab_div
                         : (m_grab_kp < 0 ? m_hover_div : -1);
        if (active >= 0 && active + 1 <= n - 1) {
            const double ex = edge[active + 1];
            cr->set_source_rgba(1, 1, 1, 0.85);
            cr->set_line_width(2);
            cr->move_to(ex, y0);
            cr->line_to(ex, y1);
            cr->stroke();
            // little grip ticks
            cr->set_line_width(1);
            for (double gy : { y0 + Hp * 0.5 - 5, y0 + Hp * 0.5 + 5 }) {
                cr->move_to(ex - 2, gy);
                cr->line_to(ex + 2, gy);
                cr->stroke();
            }
        }
    }

    // ── Guide lines high / mid / low ──────────────────────────────────────────
    cr->set_font_size(9);
    const std::pair<double, const char*> guides[] = {
        {1.0, "high"}, {0.5, "mid"}, {0.0, "low"}};
    for (const auto& [val, name] : guides) {
        const double gy = y1 - val * Hp;
        cr->set_source_rgba(1, 1, 1, 0.14);
        cr->set_line_width(1);
        std::vector<double> dash{2, 3};
        cr->set_dash(dash, 0);
        cr->move_to(x0, gy);
        cr->line_to(x1, gy);
        cr->stroke();
        cr->unset_dash();
        cr->set_source_rgba(1, 1, 1, 0.40);
        Cairo::TextExtents ext;
        cr->get_text_extents(name, ext);
        cr->move_to(x0 - 6 - ext.width, gy + 3);
        cr->show_text(name);
    }

    // ── Fader points ──────────────────────────────────────────────────────────
    std::vector<std::pair<double,double>> fren_pts(n), arc_pts(n);
    std::vector<double> cx(n);
    for (int i = 0; i < n; ++i) {
        cx[i] = (edge[i] + edge[i + 1]) / 2.0;
        const double f = std::clamp(m_kps[i]->frenetic, 0.0, 1.0);
        const double a = std::clamp(m_kps[i]->arc,      0.0, 1.0);
        fren_pts[i] = { cx[i], y1 - f * Hp };
        arc_pts[i]  = { cx[i], y1 - a * Hp };
    }
    // Cache so the event handlers hit-test exactly what was drawn.
    m_layout  = { x0, y0, x1, y1, Hp, true };
    m_cx      = cx;
    m_fren_pt = fren_pts;
    m_arc_pt  = arc_pts;

    // ── Curves: white halo under, colour over (arc first, frenetic on top) ────
    cr->set_line_join(Cairo::Context::LineJoin::ROUND);
    auto draw_curve = [&](const std::vector<std::pair<double,double>>& pts, Rgb c) {
        catmull_path(cr, pts);
        cr->set_source_rgba(1, 1, 1, 0.5);
        cr->set_line_width(3.5);
        cr->stroke();
        catmull_path(cr, pts);
        cr->set_source_rgb(c.r, c.g, c.b);
        cr->set_line_width(1.8);
        cr->stroke();
    };
    draw_curve(arc_pts,  ARC);
    draw_curve(fren_pts, FREN);

    // ── Dots: round = frenetic, diamond = arc (hot = hovered/grabbed) ─────────
    auto dot_round = [&](double x, double y, Rgb c, bool hot) {
        const double rr = hot ? 5.0 : 3.4;
        cr->set_source_rgb(c.r, c.g, c.b);
        cr->arc(x, y, rr, 0, 2 * M_PI);
        cr->fill();
        cr->set_source_rgba(1, 1, 1, hot ? 1.0 : 0.85);
        cr->set_line_width(hot ? 1.6 : 1.0);
        cr->arc(x, y, rr, 0, 2 * M_PI);
        cr->stroke();
    };
    auto dot_diamond = [&](double x, double y, Rgb c, bool hot) {
        const double r = hot ? 5.4 : 3.8;
        auto path = [&] {
            cr->move_to(x, y - r); cr->line_to(x + r, y);
            cr->line_to(x, y + r); cr->line_to(x - r, y); cr->close_path();
        };
        path(); cr->set_source_rgb(c.r, c.g, c.b); cr->fill();
        path(); cr->set_source_rgba(1, 1, 1, hot ? 1.0 : 0.85);
        cr->set_line_width(hot ? 1.6 : 1.0); cr->stroke();
    };
    auto is_hot = [&](int i, Fader f) {
        return (m_grab_kp == i && m_grab_fader == f) ||
               (m_grab_kp < 0 && m_hover_kp == i && m_hover_fader == f);
    };
    for (int i = 0; i < n; ++i) {
        dot_diamond(arc_pts[i].first, arc_pts[i].second,  ARC,  is_hot(i, Fader::Arc));
        dot_round  (fren_pts[i].first, fren_pts[i].second, FREN, is_hot(i, Fader::Frenetic));
    }

    // ── Order numbers (top) + scene/weight numbers (bottom) + pin glyph ───────
    auto draw_pin = [&](double gx, double gy) {
        cr->set_source_rgba(1, 1, 1, 0.95);
        cr->arc(gx, gy, 2.6, 0, 2 * M_PI);
        cr->fill();
        cr->move_to(gx - 2.3, gy + 1.2);
        cr->line_to(gx, gy + 5.0);
        cr->line_to(gx + 2.3, gy + 1.2);
        cr->close_path();
        cr->fill();
        cr->set_source_rgba(0.1, 0.1, 0.12, 0.9);   // pinhole
        cr->arc(gx, gy, 1.0, 0, 2 * M_PI);
        cr->fill();
    };
    cr->set_font_size(9);
    for (int i = 0; i < n; ++i) {
        const std::string ord = std::to_string(m_kps[i]->order);
        Cairo::TextExtents e1;
        cr->get_text_extents(ord, e1);
        cr->set_source_rgba(1, 1, 1, 0.75);
        cr->move_to(cx[i] - e1.width / 2, y0 - 5);
        cr->show_text(ord);

        // Pinned (fixed_scenes>0): one extended hinge scene, exempt from weight.
        if (m_kps[i]->fixed_scenes > 0)
            draw_pin(cx[i], y0 + 8);

        const std::string wl = (static_cast<int>(m_scene_counts.size()) == n)
                                   ? std::to_string(m_scene_counts[i])
                                   : weight_label(m_kps[i]->weight);
        Cairo::TextExtents e2;
        cr->get_text_extents(wl, e2);
        cr->set_source_rgba(1, 1, 1, 0.45);
        cr->move_to(cx[i] - e2.width / 2, y1 + 13);
        cr->show_text(wl);
    }

    // ── Chapter band — chapters mapped onto the channel axis via per-KP scene
    //    distribution, so a chapter that ends on a pin ends at that channel edge.
    if (static_cast<int>(m_scene_counts.size()) == n && !m_chapters.empty()) {
        double totsc = 0;
        for (int c : m_scene_counts) totsc += c;
        if (totsc > 0) {
            // Map a told-line scene position (0..totsc) to an x on the channel axis.
            auto x_at = [&](double pos) -> double {
                double cumc = 0;
                for (int i = 0; i < n; ++i) {
                    const double c = (i < static_cast<int>(m_scene_counts.size()))
                                         ? m_scene_counts[i] : 0;
                    if (pos <= cumc + c || i == n - 1) {
                        if (c <= 0) return edge[i];
                        return edge[i] + ((pos - cumc) / c) * (edge[i + 1] - edge[i]);
                    }
                    cumc += c;
                }
                return x1;
            };
            const double band_y = y1 + 20, band_h = 14;
            cr->set_font_size(9);
            double start = 0;
            for (size_t ci = 0; ci < m_chapters.size(); ++ci) {
                const double lx = x_at(start);
                const double rx = x_at(start + m_chapters[ci]);
                start += m_chapters[ci];
                const double midy = band_y + band_h * 0.78;
                cr->set_source_rgba(1, 1, 1, (ci % 2) ? 0.05 : 0.10);
                cr->rectangle(lx, band_y, rx - lx, band_h);
                cr->fill();
                cr->set_source_rgba(1, 1, 1, 0.55);
                cr->set_line_width(1);
                cr->move_to(lx + 0.5, band_y + 3); cr->line_to(lx + 0.5, midy); cr->stroke();
                cr->move_to(rx - 0.5, band_y + 3); cr->line_to(rx - 0.5, midy); cr->stroke();
                cr->move_to(lx + 0.5, midy);       cr->line_to(rx - 0.5, midy); cr->stroke();
                std::string lbl = "Ch " + std::to_string(ci + 1);
                Cairo::TextExtents ce;
                cr->get_text_extents(lbl, ce);
                if (ce.width + 6 > rx - lx) {       // too narrow for "Ch N" → just N
                    lbl = std::to_string(ci + 1);
                    cr->get_text_extents(lbl, ce);
                }
                if (ce.width + 2 <= rx - lx) {
                    cr->set_source_rgba(1, 1, 1, 0.80);
                    cr->move_to((lx + rx) / 2 - ce.width / 2, band_y + 9);
                    cr->show_text(lbl);
                }
            }
        }
    }

    // ── Legend (frenetic ● / arc ◆) ───────────────────────────────────────────
    const double ly = h - 9, lx = x0;
    cr->set_source_rgb(FREN.r, FREN.g, FREN.b);
    cr->arc(lx + 4, ly - 3, 3.4, 0, 2 * M_PI);
    cr->fill();
    cr->set_source_rgba(1, 1, 1, 0.6);
    cr->move_to(lx + 12, ly);
    cr->show_text("frenetic");
    const double lx2 = lx + 70, r = 3.8;
    cr->set_source_rgb(ARC.r, ARC.g, ARC.b);
    cr->move_to(lx2 + 4, ly - 3 - r); cr->line_to(lx2 + 4 + r, ly - 3);
    cr->line_to(lx2 + 4, ly - 3 + r); cr->line_to(lx2 + 4 - r, ly - 3);
    cr->close_path(); cr->fill();
    cr->set_source_rgba(1, 1, 1, 0.6);
    cr->move_to(lx2 + 12, ly);
    cr->show_text("arc");

    // ── Live value readout while a fader is grabbed ───────────────────────────
    if (m_grab_kp >= 0 && m_grab_kp < n) {
        const auto& pt = (m_grab_fader == Fader::Arc)
                             ? m_arc_pt[m_grab_kp] : m_fren_pt[m_grab_kp];
        char buf[8];
        std::snprintf(buf, sizeof(buf), "%.2f", m_drag_value);
        cr->set_font_size(11);
        Cairo::TextExtents te;
        cr->get_text_extents(buf, te);
        double bx = pt.first - te.width / 2 - 4;
        double by = pt.second - 22;
        if (by < y0) by = pt.second + 10;        // flip below if near the top
        if (bx < x0) bx = x0;
        if (bx + te.width + 8 > x1) bx = x1 - te.width - 8;
        cr->set_source_rgba(0, 0, 0, 0.72);
        cr->rectangle(bx, by, te.width + 8, 16);
        cr->fill();
        cr->set_source_rgba(1, 1, 1, 0.95);
        cr->move_to(bx + 4, by + 12);
        cr->show_text(buf);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Interaction (s29) — drag the energy faders
// ─────────────────────────────────────────────────────────────────────────────

std::pair<int, ModuleBoard::Fader> ModuleBoard::hit_test(double px, double py) const {
    if (!m_layout.valid) return { -1, Fader::None };
    const double R2 = 10.0 * 10.0;   // ~10px grab radius
    int    best_kp = -1;
    Fader  best_f  = Fader::None;
    double best_d  = R2;
    const int n = static_cast<int>(m_kps.size());
    for (int i = 0; i < n; ++i) {
        if (i < static_cast<int>(m_fren_pt.size())) {
            const double dx = px - m_fren_pt[i].first, dy = py - m_fren_pt[i].second;
            const double d = dx * dx + dy * dy;
            if (d <= best_d) { best_d = d; best_kp = i; best_f = Fader::Frenetic; }
        }
        if (i < static_cast<int>(m_arc_pt.size())) {
            const double dx = px - m_arc_pt[i].first, dy = py - m_arc_pt[i].second;
            const double d = dx * dx + dy * dy;
            if (d <= best_d) { best_d = d; best_kp = i; best_f = Fader::Arc; }
        }
    }
    return { best_kp, best_f };
}

double ModuleBoard::value_from_y(double abs_y) const {
    if (!m_layout.valid || m_layout.Hp <= 0) return 0.0;
    double v = (m_layout.y1 - abs_y) / m_layout.Hp;
    return std::clamp(v, 0.0, 1.0);
}

// A divider is the boundary between KP d and KP d+1, at m_edge[d+1]. Returns the
// left KP index d, or -1. Only inside the plot's vertical band.
int ModuleBoard::hit_divider(double px, double py) const {
    if (!m_layout.valid) return -1;
    if (py < m_layout.y0 - 2 || py > m_layout.y1 + 2) return -1;
    const int n = static_cast<int>(m_kps.size());
    const double tol = 4.0;
    int best = -1;
    double best_d = tol;
    for (int d = 0; d + 1 < n; ++d) {
        if (d + 1 >= static_cast<int>(m_edge.size())) break;
        const double dist = std::abs(px - m_edge[d + 1]);
        if (dist <= best_d) { best_d = dist; best = d; }
    }
    return best;
}

void ModuleBoard::write_fader(int kp, Fader f, double v) {
    if (kp < 0 || kp >= static_cast<int>(m_kps.size()) || f == Fader::None) return;
    if (f == Fader::Arc) m_kps[kp]->arc = v;
    else                 m_kps[kp]->frenetic = v;
    m_drag_value = v;
    queue_draw();
    m_sig_changed.emit(kp, f == Fader::Arc, v);   // owner writes back to its arc
}

void ModuleBoard::on_drag_begin(double sx, double sy) {
    // Faders are the more specific target (small dots at channel centres); try
    // them first, then fall back to the channel dividers.
    auto [kp, f] = hit_test(sx, sy);
    if (kp >= 0) {
        m_grab_kp = kp;
        m_grab_fader = f;
        m_grab_start_y = sy;
        m_drag_value = (f == Fader::Arc) ? m_kps[kp]->arc : m_kps[kp]->frenetic;
        queue_draw();
        return;
    }
    const int d = hit_divider(sx, sy);
    if (d >= 0) {
        const int n = static_cast<int>(m_kps.size());
        // Nearest UNPINNED KP on each side: pinned KPs are rigid spacers, so the
        // stretch flows past them to the first flexible neighbour.
        int la = -1;
        for (int j = d;     j >= 0; --j) if (m_kps[j]->fixed_scenes == 0) { la = j; break; }
        int ra = -1;
        for (int j = d + 1; j < n;  ++j) if (m_kps[j]->fixed_scenes == 0) { ra = j; break; }
        m_grab_div = d;
        m_grab_la = la;
        m_grab_ra = ra;
        m_grab_div_w0 = (la >= 0) ? m_kps[la]->weight : 0.0;
        m_grab_div_w1 = (ra >= 0) ? m_kps[ra]->weight : 0.0;
        m_grab_div_tot = 0.0;
        for (auto* k : m_kps) m_grab_div_tot += std::max(0.0001, k->weight);
        queue_draw();
    }
}

void ModuleBoard::on_drag_update(double ox, double oy) {
    if (m_grab_kp >= 0) {
        write_fader(m_grab_kp, m_grab_fader, value_from_y(m_grab_start_y + oy));
        return;
    }
    if (m_grab_div >= 0 && m_layout.valid) {
        if (m_grab_la < 0 || m_grab_ra < 0) return;   // no flexible KP on a side
        const double Wp = m_layout.x1 - m_layout.x0;
        if (Wp <= 0) return;
        const double pair = m_grab_div_w0 + m_grab_div_w1;
        const double kMin = 0.5;                       // a channel never collapses
        double dW = (ox / Wp) * m_grab_div_tot;        // px → weight along the axis
        double w0 = std::clamp(m_grab_div_w0 + dW, kMin, pair - kMin);
        double w1 = pair - w0;
        m_kps[m_grab_la]->weight = w0;
        m_kps[m_grab_ra]->weight = w1;
        queue_draw();
        m_sig_weight.emit();                           // owner re-plans + counts
    }
}

void ModuleBoard::on_drag_end(double /*ox*/, double /*oy*/) {
    m_grab_kp = -1;
    m_grab_fader = Fader::None;
    m_grab_div = -1;
    m_grab_la = m_grab_ra = -1;
    queue_draw();
}

void ModuleBoard::on_motion(double x, double y) {
    if (m_grab_kp >= 0 || m_grab_div >= 0) return;   // a drag owns the cursor
    auto [kp, f] = hit_test(x, y);
    const int d = (kp < 0) ? hit_divider(x, y) : -1;
    if (kp != m_hover_kp || f != m_hover_fader || d != m_hover_div) {
        m_hover_kp = kp;
        m_hover_fader = f;
        m_hover_div = d;
        const char* cur = (kp >= 0) ? "ns-resize" : (d >= 0 ? "ew-resize" : nullptr);
        set_cursor(cur ? Gdk::Cursor::create(cur) : Glib::RefPtr<Gdk::Cursor>());
        queue_draw();
    }
}

void ModuleBoard::on_leave() {
    if (m_hover_kp != -1 || m_hover_fader != Fader::None || m_hover_div != -1) {
        m_hover_kp = -1;
        m_hover_fader = Fader::None;
        m_hover_div = -1;
        set_cursor(Glib::RefPtr<Gdk::Cursor>());
        queue_draw();
    }
}

void ModuleBoard::on_secondary(int /*n_press*/, double x, double y) {
    auto [kp, f] = hit_test(x, y);
    if (kp < 0) return;
    m_set_kp  = kp;
    m_set_arc = (f == Fader::Arc);

    // Build the popover once; reuse it on every right-click.
    if (!m_set_pop) {
        m_set_pop = Gtk::make_managed<Gtk::Popover>();
        m_set_pop->set_parent(*this);
        m_set_pop->set_autohide(true);
        m_set_pop->set_name("module-board-setval");

        auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
        box->set_margin_start(8); box->set_margin_end(8);
        box->set_margin_top(8);   box->set_margin_bottom(8);

        m_set_label = Gtk::make_managed<Gtk::Label>("");
        m_set_label->add_css_class("dim-label");
        m_set_label->set_halign(Gtk::Align::START);
        box->append(*m_set_label);

        m_set_spin = Gtk::make_managed<Gtk::SpinButton>();
        m_set_spin->set_range(0.0, 1.0);
        m_set_spin->set_increments(0.01, 0.10);
        m_set_spin->set_digits(2);
        m_set_spin->signal_value_changed().connect([this]{
            if (m_set_guard || m_set_kp < 0) return;
            write_fader(m_set_kp, m_set_arc ? Fader::Arc : Fader::Frenetic,
                        m_set_spin->get_value());
        });
        box->append(*m_set_spin);
        m_set_pop->set_child(*box);
    }

    const auto& pt = m_set_arc ? m_arc_pt[kp] : m_fren_pt[kp];
    m_set_label->set_text(m_set_arc ? "Arc tension" : "Frenetic pacing");
    m_set_guard = true;   // loading the current value must not echo back
    m_set_spin->set_value(m_set_arc ? m_kps[kp]->arc : m_kps[kp]->frenetic);
    m_set_guard = false;
    Gdk::Rectangle rect(static_cast<int>(pt.first) - 1,
                        static_cast<int>(pt.second) - 1, 2, 2);
    m_set_pop->set_pointing_to(rect);
    m_set_pop->popup();
    m_set_spin->grab_focus();
}

} // namespace Folio
