// ─────────────────────────────────────────────────────────────────────────────
// Folio — EditorRuler.cpp
// Visual design:
//   • Page edges  — draggable vertical boundary between gray/white zones
//   • Margins     — white ▲ at bottom (pointing up into the margin band)
//   • Indents     — white ▼ at top (pointing down into the text area)
//   • Right indent— white ▼ at top, right side
// ─────────────────────────────────────────────────────────────────────────────

#include "EditorRuler.hpp"
#include <cairomm/context.h>
#include <gdkmm/rgba.h>
#include <gtkmm/eventcontrollermotion.h>
#include <gtkmm/tooltip.h>
#include <algorithm>
#include <cmath>

namespace Folio {

EditorRuler::EditorRuler(FolioPrefs& prefs) : m_prefs(prefs) {
    set_size_request(-1, HEIGHT);
    set_hexpand(true);
    add_css_class("editor-ruler");
    set_draw_func([this](const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
        on_draw(cr, w, h);
    });
    m_drag = Gtk::GestureDrag::create();
    m_drag->set_button(GDK_BUTTON_PRIMARY);
    m_drag->signal_drag_begin().connect([this](double x, double y) { on_drag_begin(x, y); });
    m_drag->signal_drag_update().connect([this](double dx, double dy) { on_drag_update(dx, dy); });
    m_drag->signal_drag_end().connect([this](double dx, double dy) { on_drag_end(dx, dy); });
    add_controller(m_drag);
    m_click = Gtk::GestureClick::create();
    m_click->set_button(GDK_BUTTON_PRIMARY);
    m_click->signal_pressed().connect([this](int n, double x, double y) { on_click(n, x, y); });
    add_controller(m_click);

    // Motion controller — tracks hover position for visual feedback
    m_motion = Gtk::EventControllerMotion::create();
    m_motion->signal_motion().connect([this](double x, double y) {
        int tab_idx = -1;
        auto target = hit_test(x, y, &tab_idx);
        if (target != m_hover_target || tab_idx != m_hover_tab_idx) {
            m_hover_target  = target;
            m_hover_tab_idx = tab_idx;
            queue_draw();
        }
    });
    m_motion->signal_leave().connect([this]() {
        if (m_hover_target != DragTarget::None) {
            m_hover_target  = DragTarget::None;
            m_hover_tab_idx = -1;
            queue_draw();
        }
    });
    add_controller(m_motion);
    set_has_tooltip(true);
    signal_query_tooltip().connect([this](int x, int y, bool /*kb*/,
                                          const Glib::RefPtr<Gtk::Tooltip>& tip) -> bool {
        int tab_idx = -1;
        auto target = hit_test((double)x, (double)y, &tab_idx);
        std::string text;
        switch (target) {
        case DragTarget::PageLeft:
        case DragTarget::PageRight:
            text = "Drag to set page width";
            break;
        case DragTarget::FirstIndent:
            text = "First-line indent — drag to set paragraph first-line offset";
            break;
        case DragTarget::LeftIndent:
            text = "Left indent — drag to set paragraph left body indent";
            break;
        case DragTarget::RightIndent:
            text = "Right indent — drag to set paragraph right indent";
            break;
        case DragTarget::TabStop:
            if (tab_idx >= 0 && tab_idx < (int)m_prefs.tab_stops.size()) {
                const auto& ts = m_prefs.tab_stops[tab_idx];
                RulerUnit u = unit();
                double val = RulerUnits::px_to_unit(
                    RulerUnits::pt_to_px(ts.position_pt), u);
                char buf[64];
                std::snprintf(buf, sizeof(buf), "Tab stop  %.2f %s  (%s)  — drag off ruler to remove",
                    val, RulerUnits::display_label(u).c_str(), ts.type.c_str());
                text = buf;
            }
            break;
        default:
            if ((double)x > TYPE_W) {
                // In text area — hint about clicking to add tab stop
                text = "Click to add a " + m_prefs.ruler_tab_type + " tab stop  |  "
                       "Click tab type selector (left) to change type";
            } else {
                text = "Tab type: " + m_prefs.ruler_tab_type + "  —  click to cycle L → R → C → D";
            }
            break;
        }
        if (!text.empty()) {
            tip->set_text(text);
            return true;
        }
        return false;
    }, false);
}

void EditorRuler::sync_geometry(int viewport_w, int page_px, int margin_px,
                                 int first_indent_px, int left_indent_px,
                                 int right_indent_px) {
    m_viewport_w      = viewport_w;
    m_page_px         = page_px;
    m_margin_px       = margin_px;
    m_first_indent_px = first_indent_px;
    m_left_indent_px  = left_indent_px;
    m_right_indent_px = right_indent_px;
    m_page_left_x     = (viewport_w - page_px) / 2;
    queue_draw();
}

// ─── Coordinate helpers ───────────────────────────────────────────────────────

double EditorRuler::page_off_to_ruler_x(double off) const {
    return TYPE_W + m_page_left_x + off;
}
double EditorRuler::ruler_x_to_page_off(double rx) const {
    return rx - TYPE_W - m_page_left_x;
}

// ─── Tab type selector ────────────────────────────────────────────────────────

const char* EditorRuler::current_type_label() const {
    const std::string& t = m_prefs.ruler_tab_type;
    if (t == "right")   return "R";
    if (t == "center")  return "C";
    if (t == "decimal") return "D";
    return "L";
}
void EditorRuler::cycle_tab_type() {
    const std::string& t = m_prefs.ruler_tab_type;
    if      (t == "left")    m_prefs.ruler_tab_type = "right";
    else if (t == "right")   m_prefs.ruler_tab_type = "center";
    else if (t == "center")  m_prefs.ruler_tab_type = "decimal";
    else                     m_prefs.ruler_tab_type = "left";
    try { m_prefs.save(); } catch (...) {}
    queue_draw();
}

// ─── Draw ─────────────────────────────────────────────────────────────────────

void EditorRuler::on_draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
    draw_background(cr, w, h);
    draw_ticks(cr, w, h);
    draw_tab_stops(cr, h);
    draw_handles(cr, h);
    draw_type_selector(cr, h);
}

void EditorRuler::draw_background(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
    auto style = get_style_context();
    Gdk::RGBA fg = style->get_color();
    bool dark = (fg.get_red() + fg.get_green() + fg.get_blue()) > 1.5f;

    // Inactive zone (outside page)
    if (dark) cr->set_source_rgba(0.12, 0.12, 0.14, 1.0);
    else      cr->set_source_rgba(0.72, 0.72, 0.74, 1.0);
    cr->rectangle(TYPE_W, 0, w - TYPE_W, h);
    cr->fill();

    // Page zone (margin bands — slightly lighter than inactive)
    double page_l = page_off_to_ruler_x(0);
    double page_r = page_off_to_ruler_x(m_page_px);
    if (dark) cr->set_source_rgba(0.20, 0.20, 0.23, 1.0);
    else      cr->set_source_rgba(0.84, 0.84, 0.86, 1.0);
    if (page_r > page_l) {
        cr->rectangle(page_l, 0, page_r - page_l, h);
        cr->fill();
    }

    // Text area (inside margins + indent — lightest)
    double text_l = page_off_to_ruler_x(m_margin_px + m_left_indent_px);
    double text_r = page_off_to_ruler_x(m_page_px - m_margin_px - m_right_indent_px);
    if (text_r > text_l) {
        if (dark) cr->set_source_rgba(0.26, 0.26, 0.30, 1.0);
        else      cr->set_source_rgba(0.97, 0.97, 0.98, 1.0);
        cr->rectangle(text_l, 0, text_r - text_l, h);
        cr->fill();
    }

    // Type selector
    if (dark) cr->set_source_rgba(0.14, 0.14, 0.17, 1.0);
    else      cr->set_source_rgba(0.78, 0.78, 0.80, 1.0);
    cr->rectangle(0, 0, TYPE_W, h);
    cr->fill();
    cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 0.20);
    cr->set_line_width(1.0);
    cr->move_to(TYPE_W - 0.5, 0); cr->line_to(TYPE_W - 0.5, h); cr->stroke();

    // Bottom border
    cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 0.15);
    cr->move_to(0, h - 0.5); cr->line_to(w, h - 0.5); cr->stroke();
}

void EditorRuler::draw_type_selector(const Cairo::RefPtr<Cairo::Context>& cr, int h) {
    auto style = get_style_context();
    Gdk::RGBA fg = style->get_color();
    cr->select_font_face("sans-serif", Cairo::ToyFontFace::Slant::NORMAL,
                          Cairo::ToyFontFace::Weight::BOLD);
    cr->set_font_size(11.0);
    const char* lbl = current_type_label();
    Cairo::TextExtents te;
    cr->get_text_extents(lbl, te);
    double lx = (TYPE_W - te.width) / 2.0 - te.x_bearing;
    double ly = (h - te.height) / 2.0 - te.y_bearing;
    cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 0.80);
    cr->move_to(lx, ly);
    cr->show_text(lbl);
}

void EditorRuler::draw_ticks(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
    auto style = get_style_context();
    Gdk::RGBA fg = style->get_color();
    RulerUnit u = unit();
    double major_interval = RulerUnits::major_tick_interval(u);
    int    subdivisions   = RulerUnits::minor_subdivisions(u);
    double minor_interval = major_interval / subdivisions;
    double origin_x       = page_off_to_ruler_x(0);

    cr->select_font_face("sans-serif", Cairo::ToyFontFace::Slant::NORMAL,
                          Cairo::ToyFontFace::Weight::NORMAL);
    cr->set_font_size(9.0);

    double first_major = std::floor(RulerUnits::px_to_unit(TYPE_W - origin_x, u)
                                    / major_interval) * major_interval;
    double last_major  = std::ceil (RulerUnits::px_to_unit(w     - origin_x, u)
                                    / major_interval) * major_interval;

    // Minor ticks
    cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 0.30);
    cr->set_line_width(1.0);
    for (double val = first_major; val <= last_major; val += minor_interval) {
        double rx = origin_x + RulerUnits::unit_to_px(val, u);
        if (rx < TYPE_W || rx > w) continue;
        double rem = std::fmod(std::abs(val), major_interval);
        if (rem < 1e-6 || (major_interval - rem) < 1e-6) continue;
        int tick_h = (h - 4) / 3;
        cr->move_to(std::round(rx) + 0.5, h - 2);
        cr->line_to(std::round(rx) + 0.5, h - 2 - tick_h);
    }
    cr->stroke();

    // Major ticks + labels
    for (double val = first_major; val <= last_major; val += major_interval) {
        double rx = origin_x + RulerUnits::unit_to_px(val, u);
        if (rx < TYPE_W || rx > w) continue;
        cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 0.55);
        cr->set_line_width(1.0);
        int tick_h = (h - 4) / 2;
        cr->move_to(std::round(rx) + 0.5, h - 2);
        cr->line_to(std::round(rx) + 0.5, h - 2 - tick_h);
        cr->stroke();
        if (std::fmod(std::abs(val), major_interval * 2) < 1e-6 ||
            u == RulerUnit::Inch || u == RulerUnit::Pc) {
            std::string lbl = RulerUnits::format_label(val, u);
            Cairo::TextExtents te;
            cr->get_text_extents(lbl, te);
            double lx = std::round(rx) - te.width / 2.0 - te.x_bearing;
            if (lx > TYPE_W + 2 && lx + te.width < w - 2) {
                cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), 0.50);
                cr->move_to(lx, h - tick_h - 3);
                cr->show_text(lbl);
            }
        }
    }
}

// ─── draw_handles ─────────────────────────────────────────────────────────────
// Page edges  : thin vertical bar spanning full ruler height (draggable)
// Margins     : white ▲ at bottom (pointing up)
// First indent: white ▼ at top (pointing down) — left side
// Right indent: white ▼ at top (pointing down) — right side
// ─────────────────────────────────────────────────────────────────────────────

void EditorRuler::draw_handles(const Cairo::RefPtr<Cairo::Context>& cr, int h) {
    double page_l  = page_off_to_ruler_x(0);
    double page_r  = page_off_to_ruler_x(m_page_px);

    // ── Page edge bars ────────────────────────────────────────────────────────
    for (auto [px, target] : std::initializer_list<std::pair<double,DragTarget>>{
             {page_l, DragTarget::PageLeft}, {page_r, DragTarget::PageRight}}) {
        bool hovered = (m_hover_target == target);
        cr->set_source_rgba(0.55, 0.65, 0.85, hovered ? 1.0 : 0.70);
        cr->set_line_width(hovered ? 3.0 : 2.0);
        cr->move_to(std::round(px) + 0.5, 0);
        cr->line_to(std::round(px) + 0.5, h);
        cr->stroke();
    }

    // ── Indent handles — tip-to-tip design ────────────────────────────────────
    // Body indent ▲: base at bottom of ruler, tip pointing upward to mid-ruler
    // First-line ▼: base at top of ruler, tip pointing downward to mid-ruler
    // Tips nearly meet in the middle for a classic Word-style indent handle.

    const double mid    = h / 2.0;       // midpoint of ruler height
    const double tip_y  = mid + 1;       // ▲ tip position (just past center)
    const double tipd_y = mid - 1;       // ▼ tip position (just past center)
    const double base_h = h - 3;         // bottom base y for ▲
    const double base_t = 2;             // top base y for ▼

    auto draw_tri = [&](double x, double base_y, double tip_y2,
                        bool hovered, bool is_first_line) {
        double half = hovered ? 6.5 : 5.5;
        // Fill
        cr->set_source_rgba(1.0, 1.0, 1.0, hovered ? 1.0 : 0.90);
        cr->move_to(x - half, base_y);
        cr->line_to(x + half, base_y);
        cr->line_to(x,        tip_y2);
        cr->close_path(); cr->fill();
        // Outline
        cr->set_source_rgba(0.0, 0.0, 0.0, hovered ? 0.45 : 0.20);
        cr->set_line_width(0.75);
        cr->move_to(x - half, base_y);
        cr->line_to(x + half, base_y);
        cr->line_to(x,        tip_y2);
        cr->close_path(); cr->stroke();
        (void)is_first_line;
    };

    // Left body indent ▲ — base at bottom, tip up
    double left_x  = page_off_to_ruler_x(m_margin_px + m_left_indent_px);
    draw_tri(left_x, base_h, tip_y, m_hover_target == DragTarget::LeftIndent, false);

    // First-line indent ▼ — base at top, tip down, aligned with left body indent x
    double first_x = page_off_to_ruler_x(m_margin_px + m_left_indent_px + m_first_indent_px);
    draw_tri(first_x, base_t, tipd_y, m_hover_target == DragTarget::FirstIndent, true);

    // Right indent ▲ — base at bottom, tip up
    double right_x = page_off_to_ruler_x(m_page_px - m_margin_px - m_right_indent_px);
    draw_tri(right_x, base_h, tip_y, m_hover_target == DragTarget::RightIndent, false);

    // Faint connector line at the midpoint showing text width zone
    cr->set_source_rgba(1.0, 1.0, 1.0, 0.12);
    cr->set_line_width(1.0);
    cr->move_to(left_x,  mid);
    cr->line_to(right_x, mid);
    cr->stroke();
}

void EditorRuler::draw_tab_stops(const Cairo::RefPtr<Cairo::Context>& cr, int h) {
    cr->set_line_width(1.5);
    for (int i = 0; i < (int)m_prefs.tab_stops.size(); ++i) {
        const auto& ts = m_prefs.tab_stops[i];
        double px_off = m_margin_px + m_left_indent_px +
                        RulerUnits::pt_to_px(ts.position_pt);
        double rx = page_off_to_ruler_x(px_off);
        if (rx < TYPE_W || rx > get_width()) continue;
        bool hovered = (m_hover_target == DragTarget::TabStop && m_hover_tab_idx == i);
        double alpha = hovered ? 1.0 : 0.75;
        cr->set_source_rgba(1.0, 1.0, 1.0, alpha);
        cr->set_line_width(hovered ? 2.0 : 1.5);
        if (ts.type == "right") {
            cr->move_to(rx, h - 5); cr->line_to(rx, h - 13);
            cr->line_to(rx - 5, h - 13); cr->stroke();
        } else if (ts.type == "center") {
            cr->move_to(rx, h - 5); cr->line_to(rx, h - 13); cr->stroke();
            cr->move_to(rx - 4, h - 13); cr->line_to(rx + 4, h - 13); cr->stroke();
        } else if (ts.type == "decimal") {
            cr->move_to(rx, h - 5); cr->line_to(rx, h - 13);
            cr->line_to(rx + 5, h - 13); cr->stroke();
            cr->arc(rx + 2, h - 7, 1.5, 0, 2 * M_PI); cr->fill();
        } else { // left
            cr->move_to(rx, h - 5); cr->line_to(rx, h - 13);
            cr->line_to(rx + 5, h - 13); cr->stroke();
        }
    }
}

// ─── Hit testing ──────────────────────────────────────────────────────────────

EditorRuler::DragTarget EditorRuler::hit_test(double rx, double ry, int* tab_idx) const {
    if (tab_idx) *tab_idx = -1;
    if (rx < TYPE_W) return DragTarget::None;

    // Page edges — thin but easy to grab
    double page_l = page_off_to_ruler_x(0);
    double page_r = page_off_to_ruler_x(m_page_px);
    if (std::abs(rx - page_l) <= HANDLE_R) return DragTarget::PageLeft;
    if (std::abs(rx - page_r) <= HANDLE_R) return DragTarget::PageRight;

    // Left body ▲ — base at bottom, hit in bottom half
    {
        double lx = page_off_to_ruler_x(m_margin_px + m_left_indent_px);
        if (std::abs(rx - lx) <= HANDLE_R && ry >= HEIGHT * 0.4)
            return DragTarget::LeftIndent;
    }
    // Right indent ▲ — base at bottom, hit in bottom half
    {
        double rmx = page_off_to_ruler_x(m_page_px - m_margin_px - m_right_indent_px);
        if (std::abs(rx - rmx) <= HANDLE_R && ry >= HEIGHT * 0.4)
            return DragTarget::RightIndent;
    }
    // First-line ▼ — base at top, hit in top half
    {
        double fix = page_off_to_ruler_x(m_margin_px + m_left_indent_px + m_first_indent_px);
        if (std::abs(rx - fix) <= HANDLE_R && ry < HEIGHT * 0.6)
            return DragTarget::FirstIndent;
    }
    // Tab stops
    for (int i = 0; i < (int)m_prefs.tab_stops.size(); ++i) {
        double px_off = m_margin_px + m_left_indent_px +
                        RulerUnits::pt_to_px(m_prefs.tab_stops[i].position_pt);
        double tx = page_off_to_ruler_x(px_off);
        if (std::abs(rx - tx) <= HANDLE_R) {
            if (tab_idx) *tab_idx = i;
            return DragTarget::TabStop;
        }
    }
    return DragTarget::None;
}

// ─── Gesture handlers ─────────────────────────────────────────────────────────

void EditorRuler::on_drag_begin(double x, double y) {
    int tab_idx = -1;
    m_drag_target     = hit_test(x, y, &tab_idx);
    m_drag_tab_idx    = tab_idx;
    m_drag_orig_first = m_first_indent_px;
    m_drag_orig_left  = m_left_indent_px;
    m_drag_orig_right = m_right_indent_px;
    m_drag_orig_margin = m_margin_px;
    m_drag_orig_page_px = m_page_px;
    if (tab_idx >= 0 && tab_idx < (int)m_prefs.tab_stops.size())
        m_drag_orig_tab = m_prefs.tab_stops[tab_idx].position_pt;
}

void EditorRuler::on_drag_update(double dx, double /*dy*/) {
    if (m_drag_target == DragTarget::None) return;
    auto ci = [](int v, int lo, int hi) { return std::max(lo, std::min(hi, v)); };

    switch (m_drag_target) {

    case DragTarget::PageLeft:
    case DragTarget::PageRight: {
        // Dragging a page edge changes page width
        // PageLeft drag leftward = page gets wider
        // PageRight drag rightward = page gets wider
        int delta_px = (m_drag_target == DragTarget::PageLeft) ? -(int)std::round(dx)
                                                                :  (int)std::round(dx);
        int new_page_px = ci((int)std::round(m_drag_orig_page_px + delta_px * 2), 100, m_viewport_w - 56);
        if (new_page_px != m_page_px) {
            m_page_px     = new_page_px;
            m_page_left_x = (m_viewport_w - new_page_px) / 2;
            // Convert to percentage and emit
            int pct = ci((int)std::round(new_page_px * 100.0 / m_viewport_w), 15, 100);
            signal_page_width_changed.emit(pct);
            queue_draw();
        }
        break;
    }

    case DragTarget::FirstIndent: {
        int min_fi = -m_left_indent_px;
        int max_fi = m_page_px - m_margin_px * 2 - m_right_indent_px - m_left_indent_px - 4;
        int nv = ci((int)std::round(m_drag_orig_first + dx), min_fi, max_fi);
        if (nv != m_first_indent_px) {
            m_first_indent_px = nv;
            signal_first_indent_changed.emit(nv);
            queue_draw();
        }
        break;
    }

    case DragTarget::LeftIndent: {
        int max_left = m_page_px - m_margin_px * 2 - m_right_indent_px - 4;
        int nv = ci((int)std::round(m_drag_orig_left + dx), 0, max_left);
        if (nv != m_left_indent_px) {
            m_left_indent_px = nv;
            signal_left_indent_changed.emit(nv);
            queue_draw();
        }
        break;
    }

    case DragTarget::RightIndent: {
        int max_ri = m_page_px - m_margin_px * 2 - m_left_indent_px - 4;
        int nv = ci((int)std::round(m_drag_orig_right - dx), 0, max_ri);
        if (nv != m_right_indent_px) {
            m_right_indent_px = nv;
            signal_right_indent_changed.emit(nv);
            queue_draw();
        }
        break;
    }

    case DragTarget::TabStop: {
        if (m_drag_tab_idx < 0 || m_drag_tab_idx >= (int)m_prefs.tab_stops.size()) break;
        double new_px = RulerUnits::pt_to_px(m_drag_orig_tab) + dx;
        new_px = std::max(0.0, std::min(new_px, (double)(m_page_px - m_margin_px * 2)));
        TabStop updated = m_prefs.tab_stops[m_drag_tab_idx];
        updated.position_pt = RulerUnits::px_to_pt(new_px);
        m_prefs.tab_stops[m_drag_tab_idx] = updated;
        signal_tab_stop_moved.emit(m_drag_tab_idx, updated);
        queue_draw();
        break;
    }

    default: break;
    }
}

void EditorRuler::on_drag_end(double /*dx*/, double dy) {
    if (m_drag_target == DragTarget::TabStop && m_drag_tab_idx >= 0)
        if (std::abs(dy) > HEIGHT * 2)
            signal_tab_stop_removed.emit(m_drag_tab_idx);
    m_drag_target  = DragTarget::None;
    m_drag_tab_idx = -1;
}

void EditorRuler::on_click(int n_press, double x, double y) {
    if (n_press != 1) return;
    if (x < TYPE_W) { cycle_tab_type(); return; }
    if (hit_test(x, y) != DragTarget::None) return;
    // Click in text area — add tab stop
    double text_l = page_off_to_ruler_x(m_margin_px + m_left_indent_px);
    double text_r = page_off_to_ruler_x(m_page_px - m_margin_px - m_right_indent_px);
    if (x < text_l || x > text_r) return;
    double px = ruler_x_to_page_off(x) - m_margin_px - m_left_indent_px;
    TabStop ts;
    ts.position_pt = RulerUnits::px_to_pt(px);
    ts.type        = m_prefs.ruler_tab_type;
    signal_tab_stop_added.emit(ts);
}

} // namespace Folio
