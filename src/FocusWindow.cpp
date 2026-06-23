// ─────────────────────────────────────────────────────────────────────────────
// FocusWindow.cpp — distraction-free writing as a separate window on the shared
// editor buffer. See FocusWindow.hpp for the architecture and the bug it
// dissolves.
//
// THE INVARIANT THAT KEEPS THE EDITOR UNTOUCHED: every look property applied
// here is VIEW-LEVEL, never buffer-level. Font/size/color/zoom go through a
// CssProvider on THIS view's style context; line spacing uses the view's
// set_pixels_*_lines setters; width/margins/padding are this view's own
// margins and its container's width. The shared buffer carries only text +
// tags, which both views read identically — so changing the focus look writes
// to nothing the editor can see. (Line spacing as a *buffer tag* would leak;
// the view-level setter does not. That distinction is the whole reason this is
// safe.)
// ─────────────────────────────────────────────────────────────────────────────
#include "FocusWindow.hpp"
#include "Editor.hpp"
#include "FolioLog.hpp"
#include "Iid.hpp"
#include "ColorPicker.hpp"
#include "color/Color.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <pango/pango.h>   // s46 — PANGO_PIXELS for the invisibles overlay metrics

namespace Folio {

FocusWindow::FocusWindow(DocumentModel& model, FolioPrefs& prefs, Editor& editor)
    : m_model(model), m_prefs(prefs), m_editor(editor) {
    set_name("focus-window");
    set_title("Focus");
    set_default_size(1100, 800);

    build_view();
    build_view_chrome();  // s46 — line-number + invisibles overlays (above text, below chrome)
    build_drawer();
    build_switcher();
    build_link_picker();   // s46 — focus-owned link picker overlay (top of the overlay stack)
    build_toast();         // s46 — transient confirmation pill (topmost; snapshot/link feedback)
    wire_keys();

    set_child(m_overlay);

    // s45 — keep the caret on the typewriter rail in THIS view. Focus owns its own
    // view, so the editor's scroll_to_cursor_center never moves it; we track the
    // shared buffer's insert mark + edits and re-rail when focus is the live
    // surface. Guarded by is_active() so edits made while the editor is in front
    // (focus hidden) don't fight the editor's own scrolling.
    if (auto buf = m_editor.shared_buffer()) {
        buf->signal_mark_set().connect(
            [this](const Gtk::TextBuffer::iterator&,
                   const Glib::RefPtr<Gtk::TextBuffer::Mark>& mark) {
                if (!mark) return;
                if (mark->get_name() == "insert" &&
                    m_prefs.focus_typewriter_mode && is_active())
                    queue_scroll_to_rail();
            });
        buf->signal_changed().connect([this]() {
            if (m_prefs.focus_typewriter_mode && is_active())
                queue_scroll_to_rail();
        });
    }

    // One-time window signals (NOT in present_focus — that would re-connect on
    // every reopen). On map, (re)apply geometry now that a real width exists; on
    // close, flush through the editor and hide (the owning unique_ptr keeps us
    // alive for reuse).
    signal_map().connect([this]() {
        apply_focus_geometry();
        apply_typewriter_padding();
        m_view.grab_focus();
        if (m_prefs.focus_typewriter_mode) queue_scroll_to_rail();
    });
    signal_close_request().connect([this]() -> bool {
        m_editor.save_current();
        // Restore the editor's pre-focus body size + zoom — focus size is its
        // own, and leaving focus must put the editor back exactly as it was.
        if (m_saved_size > 0)
            m_editor.set_body_display(m_saved_size, m_saved_zoom);
        if (m_on_closed) m_on_closed();
        set_visible(false);
        return true;
    }, false);

    // s45 — load any persisted backdrop onto the layer stack (this window is built
    // once and reused, so a one-time apply is enough; the sliders update live and
    // set_backdrop re-applies on a path change).
    apply_backdrop();
    apply_panel_color();
    apply_text_color();
}

FocusWindow::~FocusWindow() {
    // Cancel any pending debounced work so a timeout can't fire on a dead window.
    m_save_conn.disconnect();
    m_size_conn.disconnect();
    m_toast_conn.disconnect();
}

// ── Construction ─────────────────────────────────────────────────────────────
void FocusWindow::build_view() {
    m_view.set_name("focus-view");
    m_view.set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
    m_view.set_editable(true);
    m_view.set_cursor_visible(true);
    // Bind to the editor's live buffer — created once, reused per node, so this
    // binding holds across every load_node without rebinding.
    m_view.set_buffer(m_editor.shared_buffer());

    m_scroll.set_name("focus-scroll");
    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_scroll.set_child(m_view);
    m_scroll.set_hexpand(true);
    m_scroll.set_vexpand(true);

    m_overlay.set_name("focus-overlay");

    // s45 — backdrop layer stack. The photo is the overlay's CHILD (bottom); the
    // dim scrim and the text-column panel are overlays added BEFORE the text, so
    // the z-order is photo → dim → panel → text. The text view + scroll go
    // transparent (via the #focus-window.backdrop CSS class) only while a backdrop
    // is live, so with no backdrop the surface looks exactly as it did before.
    m_bg_pic = Gtk::make_managed<Gtk::Picture>();
    m_bg_pic->set_name("focus-backdrop-image");
    m_bg_pic->set_hexpand(true);
    m_bg_pic->set_vexpand(true);
    m_bg_pic->set_can_shrink(true);
    m_bg_pic->set_content_fit(Gtk::ContentFit::COVER);  // fill the viewport (crop overflow)
    m_overlay.set_child(*m_bg_pic);

    m_bg_dim = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
    m_bg_dim->set_name("focus-backdrop-dim");
    m_bg_dim->add_css_class("focus-backdrop-dim");
    m_bg_dim->set_hexpand(true);
    m_bg_dim->set_vexpand(true);
    m_bg_dim->set_can_target(false);   // never eat clicks meant for the text
    m_bg_dim->set_visible(false);
    m_overlay.add_overlay(*m_bg_dim);

    // s45 — text-column card. A Cairo DrawingArea (not a CSS box) so its left/right
    // edges can FEATHER into the photo instead of cutting a hard seam, and so its
    // fill colour can be set live by the coming panel-colour picker. Full-height
    // band; the panel-opacity knob is the widget's opacity over the drawn fill.
    m_bg_panel = Gtk::make_managed<Gtk::DrawingArea>();
    m_bg_panel->set_name("focus-backdrop-panel");
    m_bg_panel->set_halign(Gtk::Align::CENTER);  // width set in apply_focus_geometry
    m_bg_panel->set_valign(Gtk::Align::FILL);
    m_bg_panel->set_vexpand(true);
    m_bg_panel->set_can_target(false);
    m_bg_panel->set_visible(false);
    m_bg_panel->set_draw_func([this](const Cairo::RefPtr<Cairo::Context>& cr,
                                     int w, int h) {
        if (w <= 0 || h <= 0) return;
        // Feather the left/right edges over a fixed band (clamped to half width).
        // The text is inset past this band (apply_focus_geometry) so glyphs never
        // sit on the fade. Top/bottom run off-screen, so only the sides feather.
        const double feather = std::min(48.0, w / 2.0);
        const double fx = feather / static_cast<double>(w);
        const double r = m_panel_color.get_red();
        const double g = m_panel_color.get_green();
        const double b = m_panel_color.get_blue();
        const double a = m_panel_color.get_alpha();
        auto grad = Cairo::LinearGradient::create(0, 0, w, 0);
        grad->add_color_stop_rgba(0.0,        r, g, b, 0.0);
        grad->add_color_stop_rgba(fx,         r, g, b, a);
        grad->add_color_stop_rgba(1.0 - fx,   r, g, b, a);
        grad->add_color_stop_rgba(1.0,        r, g, b, 0.0);
        cr->set_source(grad);
        cr->rectangle(0, 0, w, h);
        cr->fill();
    });
    m_overlay.add_overlay(*m_bg_panel);

    // Text sits above the backdrop layers.
    m_overlay.add_overlay(m_scroll);
    // The photo (overlay child) has no natural size when empty, so size the overlay
    // to the text scroll instead — otherwise an empty backdrop could collapse it.
    m_overlay.set_measure_overlay(m_scroll, true);

    // Reapply on viewport size change. With horizontal policy NEVER the
    // hadjustment page_size == the scroll window's allocated width, which does
    // NOT change when we set inner view margins, so this cannot recurse.
    if (auto h = m_scroll.get_hadjustment())
        h->signal_changed().connect([this]() { apply_focus_geometry(); });
    if (auto v = m_scroll.get_vadjustment())
        v->signal_changed().connect([this]() { apply_typewriter_padding(); });
}

// ── View-chrome overlays (s46) ───────────────────────────────────────────────
// Line numbers and invisible-char marks. The editor draws these with sibling
// widgets (a gutter packed beside the text, an overlay in its scroll overlay);
// focus has neither, so it draws its OWN overlay DrawingAreas over m_view, using
// the same GtkTextView geometry the editor's versions use (compute_bounds to
// translate into the overlay, get_visible_rect + buffer_to_window_coords to walk
// visible lines). can_target(false) so they never eat clicks. Both are per-view,
// gated on the focus-only prefs; they redraw on edit / scroll / resize.
void FocusWindow::build_view_chrome() {
    // Invisible-char overlay — a faithful port of the editor's m_invis_overlay,
    // reading m_view instead of the editor's text view, sized to the focus body
    // font (focus drives set_body_display at zoom 1.0, so no zoom factor here).
    m_invis_overlay = Gtk::make_managed<Gtk::DrawingArea>();
    m_invis_overlay->set_name("focus-invis-overlay");
    m_invis_overlay->set_can_target(false);
    m_invis_overlay->set_hexpand(true);
    m_invis_overlay->set_vexpand(true);
    m_invis_overlay->set_halign(Gtk::Align::FILL);
    m_invis_overlay->set_valign(Gtk::Align::FILL);
    m_invis_overlay->set_visible(m_prefs.focus_show_invisibles);
    m_invis_overlay->set_draw_func([this](const Cairo::RefPtr<Cairo::Context>& cr,
                                          int, int) {
        cr->save();
        cr->set_operator(Cairo::Context::Operator::CLEAR);
        cr->paint();
        cr->restore();
        auto buf = m_view.get_buffer();
        if (!buf || !m_prefs.focus_show_invisibles) return;

        graphene_rect_t vb;
        if (!gtk_widget_compute_bounds(GTK_WIDGET(m_view.gobj()),
                                       GTK_WIDGET(m_invis_overlay->gobj()), &vb))
            return;
        cr->translate(vb.origin.x, vb.origin.y);
        cr->set_source_rgba(0.42, 0.66, 0.98, 0.85);   // dim link-blue (focus is dark)

        Gdk::Rectangle vis;
        m_view.get_visible_rect(vis);
        int base = std::clamp(m_editor.body_font_size(), 6, 72);
        auto layout = m_view.create_pango_layout("");
        Pango::FontDescription fd("sans " + std::to_string(base));
        layout->set_font_description(fd);

        Gtk::TextBuffer::iterator iter;
        int dummy = 0;
        m_view.get_line_at_y(iter, vis.get_y(), dummy);
        iter.set_line_offset(0);

        layout->set_text("M");
        int ascent = 0;
        { Pango::Rectangle ink, lg; layout->get_extents(ink, lg);
          ascent = PANGO_PIXELS(-lg.get_y()); }

        auto draw_at = [&](const Gtk::TextBuffer::iterator& it, const char* glyph) {
            Gdk::Rectangle r;
            m_view.get_iter_location(it, r);
            int wx = 0, wy = 0;
            m_view.buffer_to_window_coords(Gtk::TextWindowType::WIDGET,
                                           r.get_x(), r.get_y(), wx, wy);
            layout->set_text(glyph);
            cr->move_to(wx, wy + ascent);
            layout->show_in_cairo_context(cr);
        };

        while (!iter.is_end()) {
            Gdk::Rectangle lr;
            m_view.get_iter_location(iter, lr);
            if (lr.get_y() > vis.get_y() + vis.get_height()) break;
            switch (iter.get_char()) {
                case 0x0020: draw_at(iter, "\xc2\xb7");     break;  // · space
                case 0x0009: draw_at(iter, "\xe2\x86\x92"); break;  // → tab
                case 0x000A: draw_at(iter, "\xc2\xb6");     break;  // ¶ newline
                case 0x00A0: draw_at(iter, "\xe2\x8e\xb5"); break;  // ⎵ nbsp
                case 0x00AD: draw_at(iter, "\xe2\x80\x90"); break;  // ‐ soft hyphen
                case 0x2009: draw_at(iter, "\xe2\x80\xa2"); break;  // • thin space
                case 0x200B: draw_at(iter, "\xc2\xb0");     break;  // ° zwsp
                case 0x2011: draw_at(iter, "\xe2\x80\x90"); break;  // ‐ nb hyphen
                case 0x2060: draw_at(iter, "\xc2\xb0");     break;  // ° wj
                case 0xFEFF: draw_at(iter, "\xc2\xb0");     break;  // ° bom
                default: break;
            }
            if (!iter.forward_char()) break;
        }
    });
    m_overlay.add_overlay(*m_invis_overlay);

    // Line-number overlay — the editor's gutter is a sibling strip; focus has a
    // centred column with wide margins, so numbers are drawn right-aligned just
    // left of the column (buffer x=0 → window x is the column's left edge).
    m_ln_overlay = Gtk::make_managed<Gtk::DrawingArea>();
    m_ln_overlay->set_name("focus-linenum-overlay");
    m_ln_overlay->set_can_target(false);
    m_ln_overlay->set_hexpand(true);
    m_ln_overlay->set_vexpand(true);
    m_ln_overlay->set_halign(Gtk::Align::FILL);
    m_ln_overlay->set_valign(Gtk::Align::FILL);
    m_ln_overlay->set_visible(m_prefs.focus_show_line_numbers);
    m_ln_overlay->set_draw_func([this](const Cairo::RefPtr<Cairo::Context>& cr,
                                       int, int) {
        cr->save();
        cr->set_operator(Cairo::Context::Operator::CLEAR);
        cr->paint();
        cr->restore();
        auto buf = m_view.get_buffer();
        if (!buf || !m_prefs.focus_show_line_numbers) return;

        graphene_rect_t vb;
        if (!gtk_widget_compute_bounds(GTK_WIDGET(m_view.gobj()),
                                       GTK_WIDGET(m_ln_overlay->gobj()), &vb))
            return;
        cr->translate(vb.origin.x, vb.origin.y);
        cr->set_source_rgba(0.80, 0.84, 0.96, 0.42);

        int base = std::clamp(m_editor.body_font_size(), 6, 72);
        int num_pt = std::max(8, (int)std::round(base * 0.62));
        auto layout = m_view.create_pango_layout("");
        Pango::FontDescription fd("monospace " + std::to_string(num_pt));
        layout->set_font_description(fd);

        // The text column starts at m_view's left margin (authoritative — buffer
        // x=0 → window x came back near zero here, which floated the numbers to the
        // screen edge). Anchor off the margin so the numbers hug the text column.
        const double col_left = (double)m_view.get_left_margin();

        Gdk::Rectangle vis;
        m_view.get_visible_rect(vis);
        Gtk::TextBuffer::iterator iter;
        int dummy = 0;
        m_view.get_line_at_y(iter, vis.get_y(), dummy);
        iter.set_line_offset(0);

        while (!iter.is_end()) {
            Gdk::Rectangle r;
            m_view.get_iter_location(iter, r);
            if (r.get_y() > vis.get_y() + vis.get_height()) break;
            int wx = 0, wy = 0;   // only wy needed (scroll-aware line top)
            m_view.buffer_to_window_coords(Gtk::TextWindowType::WIDGET,
                                           0, r.get_y(), wx, wy);
            int ln = iter.get_line();
            layout->set_text(std::to_string(ln + 1));
            int lw = 0, lh = 0;
            layout->get_pixel_size(lw, lh);
            double draw_x = col_left - 10.0 - (double)lw;  // right-aligned, just left of the text
            if (draw_x < 2.0) draw_x = 2.0;
            double draw_y = (double)wy + ((double)r.get_height() - (double)lh) / 2.0;
            cr->move_to(draw_x, draw_y);
            layout->show_in_cairo_context(cr);
            if (!iter.forward_line()) break;
        }
    });
    m_overlay.add_overlay(*m_ln_overlay);

    // Redraw on edit / scroll / resize.
    if (auto b = m_view.get_buffer())
        b->signal_changed().connect([this]() { queue_chrome_draw(); });
    if (auto v = m_scroll.get_vadjustment()) {
        v->signal_value_changed().connect([this]() { queue_chrome_draw(); });
        v->signal_changed().connect([this]() { queue_chrome_draw(); });
    }
    if (auto h = m_scroll.get_hadjustment())
        h->signal_changed().connect([this]() { queue_chrome_draw(); });
}

void FocusWindow::apply_view_chrome() {
    if (m_ln_overlay) {
        m_ln_overlay->set_visible(m_prefs.focus_show_line_numbers);
        m_ln_overlay->queue_draw();
    }
    if (m_invis_overlay) {
        m_invis_overlay->set_visible(m_prefs.focus_show_invisibles);
        m_invis_overlay->queue_draw();
    }
}

void FocusWindow::queue_chrome_draw() {
    if (m_ln_overlay && m_ln_overlay->get_visible())       m_ln_overlay->queue_draw();
    if (m_invis_overlay && m_invis_overlay->get_visible()) m_invis_overlay->queue_draw();
}

// ── Transient confirmation toast (s46) ───────────────────────────────────────
// The editor's own snapshot/confirmation toast lands on the hidden surface behind
// focus, so focus flashes its own bottom-centre pill. Created last (topmost) and
// never eats clicks. (s46: the old top-left tool strip was retired — spell is now
// a View toggle and snapshot/link are drawer actions; only this feedback pill and
// the scene breadcrumb + Exit remain as non-drawer chrome.)
void FocusWindow::build_toast() {
    m_toast = Gtk::make_managed<Gtk::Label>("");
    m_toast->set_name("focus-toast");
    m_toast->add_css_class("focus-toast");
    m_toast->set_halign(Gtk::Align::CENTER);
    m_toast->set_valign(Gtk::Align::END);
    m_toast->set_margin_bottom(48);
    m_toast->set_can_target(false);
    m_overlay.add_overlay(*m_toast);
}

// Show a brief bottom-centre confirmation, then fade it out. The .show class is
// driven by CSS opacity transition; a one-shot timer pulls it back after a beat.
// Re-firing restarts the timer so back-to-back snapshots don't blink.
void FocusWindow::flash_toast(const std::string& msg) {
    if (!m_toast) return;
    m_toast->set_text(msg);
    m_toast->add_css_class("show");
    m_toast_conn.disconnect();
    m_toast_conn = Glib::signal_timeout().connect([this]() {
        if (m_toast) m_toast->remove_css_class("show");
        return false;   // one-shot
    }, 1600);
}

void FocusWindow::build_drawer() {
    // ── Drawer panel content ──────────────────────────────────────────────────
    auto* panel = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    panel->set_name("focus-drawer");
    panel->add_css_class("focus-drawer");
    panel->set_size_request(320, -1);
    panel->set_vexpand(true);

    // Header — title + a visible close chevron (the "push the drawer back" door).
    auto* hdr = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    hdr->add_css_class("focus-drawer-header");
    auto* title = Gtk::make_managed<Gtk::Label>("Focus Settings");
    title->add_css_class("focus-drawer-title");
    title->set_halign(Gtk::Align::START);
    title->set_hexpand(true);
    auto* close_btn = Gtk::make_managed<Gtk::Button>("‹");
    close_btn->add_css_class("flat");
    close_btn->add_css_class("focus-drawer-close");
    close_btn->set_tooltip_text("Close settings");
    close_btn->signal_clicked().connect([this]() { close_drawer(); });
    hdr->append(*title);
    hdr->append(*close_btn);
    panel->append(*hdr);

    // Scrollable body — groups stack vertically and scroll if they outgrow height.
    auto* body = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    body->add_css_class("focus-drawer-body");
    auto* sc = Gtk::make_managed<Gtk::ScrolledWindow>();
    sc->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    sc->set_vexpand(true);
    sc->set_child(*body);
    panel->append(*sc);

    // A section header (small-caps group label).
    auto section = [](Gtk::Box* parent, const std::string& t) {
        auto* h = Gtk::make_managed<Gtk::Label>(t);
        h->add_css_class("focus-section-header");
        h->set_halign(Gtk::Align::START);
        parent->append(*h);
    };
    // A labeled slider row with a live numeric readout. round_digits snaps the drag
    // to clean steps (precision). Returns {scale, value} so the caller can format the
    // readout and (for the rail) gate the scale.
    auto row = [](Gtk::Box* parent, const std::string& name,
                  const Glib::RefPtr<Gtk::Adjustment>& adj, int round_digits)
                   -> std::pair<Gtk::Scale*, Gtk::Label*> {
        auto* r = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        r->add_css_class("focus-setting-row");
        auto* top = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto* nm = Gtk::make_managed<Gtk::Label>(name);
        nm->add_css_class("focus-setting-label");
        nm->set_halign(Gtk::Align::START);
        nm->set_hexpand(true);
        auto* val = Gtk::make_managed<Gtk::Label>("");
        val->add_css_class("focus-setting-value");
        val->set_halign(Gtk::Align::END);
        top->append(*nm);
        top->append(*val);
        auto* scale = Gtk::make_managed<Gtk::Scale>(adj, Gtk::Orientation::HORIZONTAL);
        scale->add_css_class("focus-setting-scale");
        scale->add_css_class("focus-width-scale");   // reuse the slider look
        scale->set_draw_value(false);
        scale->set_round_digits(round_digits);       // snap the drag to clean steps
        scale->set_hexpand(true);
        r->append(*top);
        r->append(*scale);
        parent->append(*r);
        return {scale, val};
    };

    // ── PAGE ──────────────────────────────────────────────────────────────────
    section(body, "Page");
    auto w_adj = Gtk::Adjustment::create(
        std::clamp(m_prefs.focus_page_width_pct, 15, 100), 15.0, 100.0, 1.0, 5.0, 0.0);
    Gtk::Label* w_val = row(body, "Column width", w_adj, 0).second;
    auto w_fmt = [w_val, w_adj]() {
        w_val->set_text(std::to_string((int)std::round(w_adj->get_value())) + "%");
    };
    w_fmt();
    w_adj->signal_value_changed().connect([this, w_adj, w_fmt]() {
        m_prefs.focus_page_width_pct =
            std::clamp((int)std::round(w_adj->get_value()), 15, 100);
        w_fmt();
        apply_focus_geometry();   // cheap (margin + panel-width sets) — stays live
        schedule_save();
    });

    // ── TYPOGRAPHY ──────────────────────────────────────────────────────────────
    section(body, "Typography");
    int init_sz = m_prefs.focus_font_size > 0 ? m_prefs.focus_font_size
                                              : m_prefs.editor_font_size;
    m_size_adj = Gtk::Adjustment::create(
        std::clamp(init_sz, 6, 72), 6.0, 72.0, 1.0, 2.0, 0.0);
    Gtk::Label* sz_val = row(body, "Text size", m_size_adj, 0).second;
    auto sz_fmt = [sz_val, this]() {
        sz_val->set_text(std::to_string((int)std::round(m_size_adj->get_value())) + " pt");
    };
    sz_fmt();
    m_size_adj->signal_value_changed().connect([this, sz_fmt]() {
        int v = std::clamp((int)std::round(m_size_adj->get_value()), 6, 72);
        m_prefs.focus_font_size = v;
        sz_fmt();                 // readout tracks live
        schedule_size_apply();    // heavy re-tag waits for the drag to settle
        schedule_save();
    });

    double init_ls = m_prefs.focus_line_spacing > 0.0 ? m_prefs.focus_line_spacing
                                                       : m_prefs.line_spacing;
    auto ls_adj = Gtk::Adjustment::create(std::clamp(init_ls, 0.5, 4.0),
                                          0.5, 4.0, 0.1, 0.5, 0.0);
    Gtk::Label* ls_val = row(body, "Line spacing", ls_adj, 1).second;
    auto ls_fmt = [ls_val, ls_adj]() {
        char b[16]; std::snprintf(b, sizeof b, "%.1f×", ls_adj->get_value());
        ls_val->set_text(b);
    };
    ls_fmt();
    ls_adj->signal_value_changed().connect([this, ls_adj, ls_fmt]() {
        m_prefs.focus_line_spacing =
            std::round(std::clamp(ls_adj->get_value(), 0.5, 4.0) * 10.0) / 10.0;
        ls_fmt();
        queue_apply_look();   // coalesce the relayout to one apply per idle
        schedule_save();
    });

    // Typewriter — a labeled switch, with the rail position as a row below it that
    // is only sensitive while the mode is on.
    auto* tw_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    tw_row->add_css_class("focus-setting-row");
    auto* tw_lbl = Gtk::make_managed<Gtk::Label>("Typewriter mode");
    tw_lbl->add_css_class("focus-setting-label");
    tw_lbl->set_halign(Gtk::Align::START);
    tw_lbl->set_hexpand(true);
    m_tw_switch = Gtk::make_managed<Gtk::Switch>();
    m_tw_switch->set_valign(Gtk::Align::CENTER);
    m_tw_switch->set_active(m_prefs.focus_typewriter_mode);
    tw_row->append(*tw_lbl);
    tw_row->append(*m_tw_switch);
    body->append(*tw_row);

    auto rail_adj = Gtk::Adjustment::create(focus_typewriter_pos(),
                                            0.15, 0.85, 0.01, 0.05, 0.0);
    auto rail = row(body, "Rail position", rail_adj, 2);
    m_rail_scale = rail.first;
    Gtk::Label* rail_val = rail.second;
    auto rail_fmt = [rail_val, rail_adj]() {
        rail_val->set_text(std::to_string((int)std::round(rail_adj->get_value() * 100)) + "%");
    };
    rail_fmt();
    if (m_rail_scale) m_rail_scale->set_sensitive(m_prefs.focus_typewriter_mode);
    rail_adj->signal_value_changed().connect([this, rail_adj, rail_fmt]() {
        m_prefs.typewriter_position = rail_adj->get_value();
        rail_fmt();
        if (m_prefs.focus_typewriter_mode) {
            apply_typewriter_padding();
            queue_scroll_to_rail();
        }
        schedule_save();
    });
    m_tw_switch->property_active().signal_changed().connect([this]() {
        if (m_tw_guard) return;
        toggle_typewriter();
    });

    // ── VIEW ────────────────────────────────────────────────────────────────────
    // Show/hide chrome on the writing surface. Line numbers + invisibles draw on
    // focus's own overlays (focus-only prefs); annotations + hyperlinks toggle the
    // SHARED buffer-tag visuals through the editor's refresh (so they also reflect
    // in the editor — a tag's look is one value across both views).
    section(body, "View");
    auto view_switch = [&](const std::string& name, bool initial,
                           std::function<void(bool)> on_toggle) -> Gtk::Switch* {
        auto* r = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        r->add_css_class("focus-setting-row");
        auto* lbl = Gtk::make_managed<Gtk::Label>(name);
        lbl->add_css_class("focus-setting-label");
        lbl->set_halign(Gtk::Align::START);
        lbl->set_hexpand(true);
        auto* sw = Gtk::make_managed<Gtk::Switch>();
        sw->set_valign(Gtk::Align::CENTER);
        sw->set_active(initial);
        sw->property_active().signal_changed().connect([this, sw, on_toggle]() {
            if (m_view_guard) return;
            on_toggle(sw->get_active());
        });
        r->append(*lbl);
        r->append(*sw);
        body->append(*r);
        return sw;
    };
    m_spell_sw = view_switch("Spell check", m_prefs.spell_check_enabled,
        [this](bool on) {
            m_prefs.spell_check_enabled = on;
            m_editor.apply_editing_prefs();   // (dis)connect highlighter + re-check; marks live in m_view
            schedule_save();
        });
    m_ann_switch = view_switch("Annotations", m_prefs.show_annotations,
        [this](bool on) {
            m_prefs.show_annotations = on; m_editor.refresh_annotation_visibility();
            schedule_save();
        });
    m_links_switch = view_switch("Hyperlinks", m_prefs.show_links,
        [this](bool on) {
            m_prefs.show_links = on; m_editor.refresh_link_visibility();
            schedule_save();
        });
    m_ln_switch = view_switch("Line numbers", m_prefs.focus_show_line_numbers,
        [this](bool on) {
            m_prefs.focus_show_line_numbers = on; apply_view_chrome(); schedule_save();
        });
    m_invis_switch = view_switch("Invisible characters", m_prefs.focus_show_invisibles,
        [this](bool on) {
            m_prefs.focus_show_invisibles = on; apply_view_chrome(); schedule_save();
        });

    // ── TOOLS ───────────────────────────────────────────────────────────────────
    // Actions (not toggles): one-shot operations on the current scene. Each closes
    // the drawer so the writer lands back on the page (snapshot flashes the toast;
    // link opens its picker, which needs the drawer/scrim out of the way). Ctrl+K
    // still opens the link picker directly without the drawer.
    section(body, "Tools");
    auto* snap_btn = Gtk::make_managed<Gtk::Button>("Save snapshot");
    snap_btn->set_name("focus-snapshot-action");
    snap_btn->add_css_class("flat");
    snap_btn->add_css_class("focus-drawer-action");
    snap_btn->signal_clicked().connect([this]() {
        close_drawer();
        m_editor.snapshot_current("Manual snapshot");
        flash_toast("\xF0\x9F\x93\xB7  Snapshot saved");   // 📷 — matches the editor toast
        m_view.grab_focus();
    });
    body->append(*snap_btn);

    auto* link_btn = Gtk::make_managed<Gtk::Button>("Insert link…");
    link_btn->set_name("focus-link-action");
    link_btn->add_css_class("flat");
    link_btn->add_css_class("focus-drawer-action");
    link_btn->set_tooltip_text("Link to another scene or page  (Ctrl+K)");
    link_btn->signal_clicked().connect([this]() {
        close_drawer();
        open_link_picker();
    });
    body->append(*link_btn);

    // ── BACKDROP ──────────────────────────────────────────────────────────────
    section(body, "Backdrop");
    m_bg_btn = Gtk::make_managed<Gtk::Button>("Choose image…");
    m_bg_btn->set_name("focus-backdrop-btn");
    m_bg_btn->add_css_class("flat");
    m_bg_btn->add_css_class("focus-drawer-action");
    m_bg_btn->signal_clicked().connect([this]() { open_backdrop_picker(); });
    body->append(*m_bg_btn);

    // Dim + Panel + Clear live together so they can be shown only when a backdrop
    // is loaded (update_backdrop_controls toggles this group's visibility).
    m_bg_slider_grp = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);

    auto dim_adj = Gtk::Adjustment::create(
        std::clamp(m_prefs.focus_background_dim, 0.0, 0.9), 0.0, 0.9, 0.01, 0.1, 0.0);
    Gtk::Label* dim_val = row(m_bg_slider_grp, "Dim photo", dim_adj, 2).second;
    auto dim_fmt = [dim_val, dim_adj]() {
        dim_val->set_text(std::to_string((int)std::round(dim_adj->get_value() * 100)) + "%");
    };
    dim_fmt();
    dim_adj->signal_value_changed().connect([this, dim_adj, dim_fmt]() {
        m_prefs.focus_background_dim = std::clamp(dim_adj->get_value(), 0.0, 0.9);
        dim_fmt();
        if (m_bg_dim) m_bg_dim->set_opacity(m_prefs.focus_background_dim);  // cheap — live
        schedule_save();
    });

    auto pan_adj = Gtk::Adjustment::create(
        std::clamp(m_prefs.focus_panel_opacity, 0.0, 1.0), 0.0, 1.0, 0.01, 0.1, 0.0);
    Gtk::Label* pan_val = row(m_bg_slider_grp, "Panel opacity", pan_adj, 2).second;
    auto pan_fmt = [pan_val, pan_adj]() {
        pan_val->set_text(std::to_string((int)std::round(pan_adj->get_value() * 100)) + "%");
    };
    pan_fmt();
    pan_adj->signal_value_changed().connect([this, pan_adj, pan_fmt]() {
        m_prefs.focus_panel_opacity = std::clamp(pan_adj->get_value(), 0.0, 1.0);
        pan_fmt();
        if (m_bg_panel) m_bg_panel->set_opacity(m_prefs.focus_panel_opacity);  // cheap — live
        schedule_save();
    });

    // s45 — Panel colour + Text colour. Each is a click-to-open swatch backed by the
    // ported OKLCH ColorPicker; signal_changed recolors live and persists the hex.
    auto color_row = [this](Gtk::Box* parent, const std::string& name,
                            std::function<std::string()> get_hex,
                            std::function<void(const std::string&)> set_hex) {
        auto* r = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        r->add_css_class("focus-setting-row");
        auto* lbl = Gtk::make_managed<Gtk::Label>(name);
        lbl->add_css_class("focus-setting-label");
        lbl->set_halign(Gtk::Align::START);
        lbl->set_hexpand(true);

        auto* swatch = Gtk::make_managed<Gtk::DrawingArea>();
        swatch->add_css_class("focus-color-swatch");
        swatch->set_size_request(48, 22);
        swatch->set_draw_func([get_hex](const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
            Folio::color::Color c = Folio::color::Color::black();
            if (auto pc = Folio::color::from_hex(get_hex())) c = *pc;
            const double rad = 5.0;
            cr->begin_new_sub_path();
            cr->arc(w - rad, rad,     rad, -M_PI / 2, 0);
            cr->arc(w - rad, h - rad, rad, 0,          M_PI / 2);
            cr->arc(rad,     h - rad, rad, M_PI / 2,   M_PI);
            cr->arc(rad,     rad,     rad, M_PI,       3 * M_PI / 2);
            cr->close_path();
            cr->set_source_rgb(c.r, c.g, c.b);
            cr->fill_preserve();
            cr->set_source_rgba(1, 1, 1, 0.18);
            cr->set_line_width(1.0);
            cr->stroke();
        });

        auto* picker = Gtk::make_managed<Folio::ColorPicker>();
        picker->set_with_alpha(false);
        auto* pop = Gtk::make_managed<Gtk::Popover>();
        pop->set_child(*picker);
        // Re-seed the picker from the current value each time it opens.
        pop->signal_map().connect([picker, get_hex]() {
            if (auto pc = Folio::color::from_hex(get_hex())) picker->set_initial(*pc);
        });
        picker->signal_changed().connect(
            [this, swatch, set_hex](Folio::color::Color c) {
                set_hex(Folio::color::to_hex(c));
                swatch->queue_draw();
                schedule_save();
            });

        auto* mb = Gtk::make_managed<Gtk::MenuButton>();
        mb->add_css_class("focus-color-mb");
        mb->set_child(*swatch);
        mb->set_popover(*pop);

        r->append(*lbl);
        r->append(*mb);
        parent->append(*r);
    };

    color_row(m_bg_slider_grp, "Panel colour",
        [this]() { return m_prefs.focus_panel_color; },
        [this](const std::string& hex) {
            m_prefs.focus_panel_color = hex; apply_panel_color();
        });
    color_row(m_bg_slider_grp, "Text colour",
        [this]() {
            return m_prefs.focus_text_color.empty() ? std::string("#cdd6f4")
                                                    : m_prefs.focus_text_color;
        },
        [this](const std::string& hex) {
            m_prefs.focus_text_color = hex; apply_text_color();
        });

    m_bg_clear = Gtk::make_managed<Gtk::Button>("Remove backdrop");
    m_bg_clear->set_name("focus-backdrop-clear");
    m_bg_clear->add_css_class("flat");
    m_bg_clear->add_css_class("focus-drawer-action");
    m_bg_clear->signal_clicked().connect([this]() { set_backdrop(""); });
    m_bg_slider_grp->append(*m_bg_clear);
    body->append(*m_bg_slider_grp);

    // ── Revealer (the slide animation) ────────────────────────────────────────
    m_drawer = Gtk::make_managed<Gtk::Revealer>();
    m_drawer->set_transition_type(Gtk::RevealerTransitionType::SLIDE_RIGHT);
    m_drawer->set_transition_duration(240);
    m_drawer->set_halign(Gtk::Align::START);
    m_drawer->set_valign(Gtk::Align::FILL);
    m_drawer->set_child(*panel);
    m_drawer->set_reveal_child(false);

    // ── Scrim (click-away catcher; transparent, present only while open) ──────
    auto* scrim = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL);
    scrim->set_name("focus-drawer-scrim");
    scrim->set_hexpand(true);
    scrim->set_vexpand(true);
    scrim->set_visible(false);
    {
        auto click = Gtk::GestureClick::create();
        click->signal_released().connect(
            [this](int, double, double) { close_drawer(); });
        scrim->add_controller(click);
    }
    m_drawer_scrim = scrim;

    // ── Pull tab (always visible; the door you can see) ───────────────────────
    auto* tab = Gtk::make_managed<Gtk::Button>("⚙");
    tab->set_name("focus-drawer-tab");
    tab->add_css_class("focus-drawer-tab");
    tab->set_tooltip_text("Focus settings");
    tab->set_halign(Gtk::Align::START);
    tab->set_valign(Gtk::Align::CENTER);
    tab->signal_clicked().connect([this]() { toggle_drawer(); });
    m_drawer_tab = tab;

    // Overlay order: scrim (bottom) → tab → drawer (top), so the open drawer covers
    // the tab and the scrim catches everything outside the drawer.
    m_overlay.add_overlay(*scrim);
    m_overlay.add_overlay(*tab);
    m_overlay.add_overlay(*m_drawer);

    // Exit affordance, top-right (unchanged — the right edge is the way out).
    auto* exit_btn = Gtk::make_managed<Gtk::Button>("✕  Exit Focus");
    exit_btn->set_name("focus-exit-btn");
    exit_btn->add_css_class("focus-exit-btn");
    exit_btn->set_halign(Gtk::Align::END);
    exit_btn->set_valign(Gtk::Align::START);
    exit_btn->set_margin_top(12);
    exit_btn->set_margin_end(16);
    exit_btn->signal_clicked().connect([this]() { close(); });
    m_overlay.add_overlay(*exit_btn);
}

void FocusWindow::open_drawer() {
    if (m_drawer_scrim) m_drawer_scrim->set_visible(true);
    if (m_drawer) m_drawer->set_reveal_child(true);
}

void FocusWindow::close_drawer() {
    if (m_drawer) m_drawer->set_reveal_child(false);
    if (m_drawer_scrim) m_drawer_scrim->set_visible(false);
    m_view.grab_focus();
}

void FocusWindow::toggle_drawer() {
    if (m_drawer && m_drawer->get_reveal_child()) close_drawer();
    else open_drawer();
}

void FocusWindow::build_switcher() {
    // A type-to-filter scene list, centred, hidden until invoked. v1 filters on
    // scene title in reading order; widening to synopsis text is a follow-on.
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    box->set_name("focus-switcher");
    box->add_css_class("focus-switcher");
    box->set_halign(Gtk::Align::CENTER);
    box->set_valign(Gtk::Align::CENTER);
    box->set_size_request(420, -1);

    m_switch_entry = Gtk::make_managed<Gtk::SearchEntry>();
    m_switch_entry->set_name("focus-switcher-entry");
    m_switch_entry->set_placeholder_text("Jump to scene…");

    m_switch_list = Gtk::make_managed<Gtk::ListBox>();
    m_switch_list->set_name("focus-switcher-list");
    m_switch_list->set_selection_mode(Gtk::SelectionMode::SINGLE);

    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroll->set_min_content_height(280);
    scroll->set_child(*m_switch_list);

    box->append(*m_switch_entry);
    box->append(*scroll);

    m_switch_entry->signal_search_changed().connect(
        [this]() { repopulate_switcher(); });
    m_switch_entry->signal_activate().connect(
        [this]() { activate_switch_row(m_switch_list->get_selected_row()); });
    m_switch_list->signal_row_activated().connect(
        [this](Gtk::ListBoxRow* row) { activate_switch_row(row); });
    // SearchEntry consumes Escape itself (stop-search), so the window-level Esc
    // handler never sees it — hide the switcher from here instead.
    m_switch_entry->signal_stop_search().connect([this]() {
        if (m_switcher) m_switcher->set_visible(false);
        m_view.grab_focus();
    });

    m_switcher = box;
    m_overlay.add_overlay(*box);
    box->set_visible(false);

    // s45 — visible scene navigation breadcrumb (top-centre): ‹ [Current scene] ›.
    // The title is a button that opens the switcher; the arrows step prev/next. This
    // is the visible door for what used to be keyboard-only (Ctrl+P / Ctrl+[ /]).
    auto* nav = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 2);
    nav->set_name("focus-navbar");
    nav->add_css_class("focus-navbar");
    nav->set_halign(Gtk::Align::CENTER);
    nav->set_valign(Gtk::Align::START);
    nav->set_margin_top(12);

    auto* prev = Gtk::make_managed<Gtk::Button>("‹");
    prev->add_css_class("focus-nav-arrow");
    prev->set_tooltip_text("Previous scene");
    prev->signal_clicked().connect([this]() { goto_relative(-1); });

    m_nav_title = Gtk::make_managed<Gtk::Button>("");
    m_nav_title->add_css_class("focus-nav-title");
    m_nav_title->set_tooltip_text("Jump to scene…");
    m_nav_title->signal_clicked().connect([this]() { open_switcher(); });

    auto* next = Gtk::make_managed<Gtk::Button>("›");
    next->add_css_class("focus-nav-arrow");
    next->set_tooltip_text("Next scene");
    next->signal_clicked().connect([this]() { goto_relative(+1); });

    nav->append(*prev);
    nav->append(*m_nav_title);
    nav->append(*next);
    m_navbar = nav;
    m_overlay.add_overlay(*nav);
    update_navbar();
}

// Rebuild the visible rows from m_scenes filtered by the entry text. The row at
// display index i corresponds to the i-th node that passes the same filter — so
// activate_switch_row recomputes the identical filter to resolve index → node.
void FocusWindow::repopulate_switcher() {
    if (!m_switch_list) return;
    while (auto* row = m_switch_list->get_row_at_index(0))
        m_switch_list->remove(*row);

    std::string q = m_switch_entry ? std::string(m_switch_entry->get_text()) : "";
    std::transform(q.begin(), q.end(), q.begin(), ::tolower);

    for (auto* n : m_scenes) {
        std::string t = n->title.empty() ? "(untitled)" : n->title;
        std::string lt = t;
        std::transform(lt.begin(), lt.end(), lt.begin(), ::tolower);
        if (!q.empty() && lt.find(q) == std::string::npos) continue;

        auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_name(Folio::widget_name("focus-switch-row", n->iid));
        auto* lbl = Gtk::make_managed<Gtk::Label>(
            (n->kind == BinderKind::Group ? "▸ " : "    ") + t);
        lbl->set_xalign(0.0f);
        lbl->set_margin_top(4);   lbl->set_margin_bottom(4);
        lbl->set_margin_start(8); lbl->set_margin_end(8);
        row->set_child(*lbl);
        m_switch_list->append(*row);
    }
    if (auto* first = m_switch_list->get_row_at_index(0))
        m_switch_list->select_row(*first);
}

void FocusWindow::activate_switch_row(Gtk::ListBoxRow* row) {
    if (!row) return;
    std::string q = m_switch_entry ? std::string(m_switch_entry->get_text()) : "";
    std::transform(q.begin(), q.end(), q.begin(), ::tolower);

    std::vector<BinderNode*> filtered;
    for (auto* n : m_scenes) {
        std::string lt = n->title.empty() ? "(untitled)" : n->title;
        std::transform(lt.begin(), lt.end(), lt.begin(), ::tolower);
        if (q.empty() || lt.find(q) != std::string::npos) filtered.push_back(n);
    }
    int idx = row->get_index();
    if (idx >= 0 && idx < (int)filtered.size()) {
        goto_node(filtered[idx]);
        if (m_switcher) m_switcher->set_visible(false);
        m_view.grab_focus();
    }
}

void FocusWindow::wire_keys() {
    auto key = Gtk::EventControllerKey::create();
    key->signal_key_pressed().connect(
        [this](guint keyval, guint, Gdk::ModifierType mods) -> bool {
            const bool ctrl =
                (mods & Gdk::ModifierType::CONTROL_MASK) == Gdk::ModifierType::CONTROL_MASK;
            if (keyval == GDK_KEY_Escape) {
                if (m_link_picker && m_link_picker->get_visible()) {
                    m_link_picker->set_visible(false);
                    m_view.grab_focus();
                } else if (m_switcher && m_switcher->get_visible()) {
                    m_switcher->set_visible(false);
                    m_view.grab_focus();
                } else if (m_drawer && m_drawer->get_reveal_child()) {
                    close_drawer();
                } else {
                    close();
                }
                return true;
            }
            if (ctrl && (keyval == GDK_KEY_comma)) { toggle_drawer(); return true; }
            if (ctrl && (keyval == GDK_KEY_p || keyval == GDK_KEY_P)) {
                open_switcher();
                return true;
            }
            if (ctrl && (keyval == GDK_KEY_k || keyval == GDK_KEY_K)) {
                open_link_picker();
                return true;
            }
            if (ctrl && (keyval == GDK_KEY_bracketright)) { goto_relative(+1); return true; }
            if (ctrl && (keyval == GDK_KEY_bracketleft))  { goto_relative(-1); return true; }
            return false;
        },
        false);
    add_controller(key);
}

// ── Look (view-level only) ───────────────────────────────────────────────────
void FocusWindow::apply_focus_look() {
    // Body font size is NOT set here — GtkTextView ignores CSS font-size for body
    // text, so size is the shared base tag, driven via the Size control →
    // Editor::set_body_display (snapshot on enter, restore on exit). What remains
    // here is genuinely view-level:

    // Line spacing → view-level pixels (not a buffer tag → editor view unaffected).
    double mult = m_prefs.focus_line_spacing > 0.0 ? m_prefs.focus_line_spacing
                                                    : m_prefs.line_spacing;
    int base = std::clamp(m_editor.body_font_size(), 6, 72);
    int extra = std::max(0, (int)std::round(base * (mult - 1.0)));
    m_view.set_pixels_inside_wrap(extra);
    m_view.set_pixels_below_lines(extra);

    apply_focus_geometry();
    apply_typewriter_padding();
}

void FocusWindow::apply_focus_geometry() {
    // Page width as a % of the viewport: clamp the view's content with side
    // margins so the measure is comfortable. All on m_view; the editor's
    // geometry is a different widget and is never read or written here.
    int vw = m_scroll.get_width();
    if (vw < 1) vw = get_width();
    int pct = std::clamp(m_prefs.focus_page_width_pct, 15, 100);
    int side = vw > 0 ? std::max(0, (vw - vw * pct / 100) / 2) : m_prefs.focus_page_margin_px;
    int col  = vw > 0 ? std::max(1, vw - 2 * side) : 0;   // the card / column width

    // s45 — inset the text inside the card so glyphs sit with breathing room and
    // clear the feathered edge band (≈48px). Clamp so a narrow column keeps text.
    int inset = std::clamp((col - 220) / 2, 0, 56);
    m_view.set_left_margin(side + inset);
    m_view.set_right_margin(side + inset);

    // The backdrop card backs the whole column (text breathes inside it).
    if (m_bg_panel && col > 0) m_bg_panel->set_size_request(col, -1);
}

void FocusWindow::apply_typewriter_padding() {
    if (m_prefs.focus_typewriter_mode) {
        // s45 — runway sized to the shared rail fraction so the caret line can reach
        // exactly that position at the FIRST line (top runway = pos·vp) and the LAST
        // line (bottom runway = (1−pos)·vp). Mirrors Editor::apply_typewriter_padding.
        int h = m_scroll.get_height();
        if (h < 1) h = get_height();
        if (h < 1) h = 640;
        double pos = focus_typewriter_pos();
        m_view.set_top_margin(static_cast<int>(h * pos));
        m_view.set_bottom_margin(static_cast<int>(h * (1.0 - pos)));
    } else {
        m_view.set_top_margin(24);
        m_view.set_bottom_margin(96);
    }
}

// s45 — the shared rail fraction (0 = top, 0.5 = centre), clamped to the same sane
// band the editor uses so the caret is never pinned to the very edge.
double FocusWindow::focus_typewriter_pos() const {
    double p = m_prefs.typewriter_position;
    if (p < 0.15) p = 0.15;
    if (p > 0.85) p = 0.85;
    return p;
}

// Scroll m_view so the caret line sits at the rail fraction of the viewport. Focus
// owns its own view inside m_scroll's viewport, so the buffer-coordinate Y of the
// caret (top_margin + iter_y) is its absolute Y within the scrollable content — no
// paper-card nesting to unwind like the editor has.
void FocusWindow::scroll_to_rail() {
    auto vadj = m_scroll.get_vadjustment();
    if (!vadj) return;
    double vp = vadj->get_page_size();
    if (vp < 1) return;
    auto buf = m_view.get_buffer();
    if (!buf) return;

    Gtk::TextBuffer::iterator cur = buf->get_iter_at_mark(buf->get_insert());
    Gdk::Rectangle r;
    m_view.get_iter_location(cur, r);

    double caret_y = static_cast<double>(m_view.get_top_margin()) +
                     static_cast<double>(r.get_y()) + r.get_height() / 2.0;
    double target = caret_y - vp * focus_typewriter_pos();

    double lo = vadj->get_lower();
    double hi = vadj->get_upper() - vp;
    if (hi < lo) return;
    target = std::clamp(target, lo, hi);
    vadj->set_value(target);
}

// Defer the rail scroll to idle so it runs after the line layout / margin relayout
// has settled (get_iter_location is stale immediately after an insert). Deduped so
// a burst of edits collapses to one scroll.
void FocusWindow::queue_scroll_to_rail() {
    if (m_rail_queued) return;
    m_rail_queued = true;
    Glib::signal_idle().connect_once([this]() {
        m_rail_queued = false;
        if (m_prefs.focus_typewriter_mode) scroll_to_rail();
    });
}

void FocusWindow::toggle_typewriter() {
    bool on = m_tw_switch ? m_tw_switch->get_active() : !m_prefs.focus_typewriter_mode;
    m_prefs.focus_typewriter_mode = on;
    if (m_rail_scale) m_rail_scale->set_sensitive(on);
    schedule_save();
    apply_typewriter_padding();
    if (on) queue_scroll_to_rail();
}

// Debounced prefs.save() — every call restarts a single timer; the write lands once
// after the drag settles, so dragging a slider no longer writes to disk per tick.
void FocusWindow::schedule_save() {
    m_save_conn.disconnect();
    m_save_conn = Glib::signal_timeout().connect([this]() {
        try { m_prefs.save(); } catch (...) {}
        return false;   // one-shot
    }, 600);
}

// Idle-coalesced line-spacing relayout — a burst of drag ticks collapses to one
// apply per idle, keeping the slider live without the relayout storm.
void FocusWindow::queue_apply_look() {
    if (m_look_queued) return;
    m_look_queued = true;
    Glib::signal_idle().connect_once([this]() {
        m_look_queued = false;
        apply_focus_look();
    });
}

// Debounced size re-tag — set_body_display re-tags the whole buffer, and there is no
// cheap preview, so it applies ~140ms after the last change (i.e. when the drag
// settles) rather than every frame. The readout still tracks live.
void FocusWindow::schedule_size_apply() {
    m_size_conn.disconnect();
    m_size_conn = Glib::signal_timeout().connect([this]() {
        int v = std::clamp(m_prefs.focus_font_size, 6, 72);
        m_editor.set_body_display(v, 1.0);   // focus size, zoom neutralised
        if (m_prefs.focus_typewriter_mode) queue_scroll_to_rail();
        return false;   // one-shot
    }, 140);
}

// ── Backdrop (s45) ───────────────────────────────────────────────────────────
void FocusWindow::open_backdrop_picker() {
    auto dlg = Gtk::FileChooserNative::create(
        "Choose Backdrop", *this, Gtk::FileChooser::Action::OPEN, "Open", "Cancel");
    auto filter = Gtk::FileFilter::create();
    filter->set_name("Images (PNG, JPG, WebP)");
    filter->add_mime_type("image/png");
    filter->add_mime_type("image/jpeg");
    filter->add_mime_type("image/webp");
    dlg->add_filter(filter);
    dlg->signal_response().connect([this, dlg](int response) {
        if (response != Gtk::ResponseType::ACCEPT) return;
        auto file = dlg->get_file();
        if (!file) return;
        std::string path = file->get_path();
        if (!path.empty()) set_backdrop(path);
    });
    dlg->show();
}

// Set or clear the backdrop path, persist it, and reapply the layer stack. The
// path is stored as an EXTERNAL reference (projects link, never embed) — link-death
// is the writer's risk until a gather/archive step exists.
void FocusWindow::set_backdrop(const std::string& path) {
    m_prefs.focus_background_path = path;
    try { m_prefs.save(); } catch (...) {}
    apply_backdrop();
}

// Push the path/dim/panel prefs onto the three layers. A live backdrop turns the
// text + scroll transparent (the .backdrop window class) so the panel becomes the
// reading surface; clearing it restores the plain dark focus surface unchanged.
void FocusWindow::apply_backdrop() {
    bool active = false;
    const std::string& path = m_prefs.focus_background_path;
    if (!path.empty() && m_bg_pic) {
        try {
            auto pix = Gdk::Pixbuf::create_from_file(path);
            m_bg_pic->set_paintable(Gdk::Texture::create_for_pixbuf(pix));
            active = true;
        } catch (...) {
            // Unloadable path (moved/deleted external file) — fall back to no backdrop
            // but KEEP the stored path so a remount restores it.
            if (m_bg_pic) m_bg_pic->set_paintable(Glib::RefPtr<Gdk::Paintable>{});
            active = false;
        }
    } else if (m_bg_pic) {
        m_bg_pic->set_paintable(Glib::RefPtr<Gdk::Paintable>{});
    }

    if (m_bg_dim) {
        m_bg_dim->set_opacity(std::clamp(m_prefs.focus_background_dim, 0.0, 0.9));
        m_bg_dim->set_visible(active);
    }
    if (m_bg_panel) {
        m_bg_panel->set_opacity(std::clamp(m_prefs.focus_panel_opacity, 0.0, 1.0));
        m_bg_panel->set_visible(active);
    }
    if (active) add_css_class("backdrop");
    else        remove_css_class("backdrop");

    apply_focus_geometry();      // size the panel to the text column
    update_backdrop_controls();
}

void FocusWindow::update_backdrop_controls() {
    bool active = !m_prefs.focus_background_path.empty();
    if (m_bg_btn)        m_bg_btn->set_label(active ? "Change…" : "Backdrop…");
    if (m_bg_slider_grp) m_bg_slider_grp->set_visible(active);
}

// s45 — parse the panel-colour hex into m_panel_color and repaint the card. The
// panel-opacity knob still scales the whole drawn fill on top of this colour.
void FocusWindow::apply_panel_color() {
    Gdk::RGBA c;
    if (!m_prefs.focus_panel_color.empty() && c.set(m_prefs.focus_panel_color))
        m_panel_color = c;
    if (m_bg_panel) m_bg_panel->queue_draw();
}

// s45 — apply the focus text colour. The app stylesheet sets `* { color: @tx1 }`
// display-wide at USER+1, which beats a per-widget provider; so this override is
// also display-wide, scoped to #focus-view, at a clearly higher priority. GtkTextView
// caches the text foreground, so we nudge a repaint after reloading the rule.
void FocusWindow::apply_text_color() {
    if (!m_text_css) {
        m_text_css = Gtk::CssProvider::create();
        if (auto dpy = Gdk::Display::get_default())
            Gtk::StyleContext::add_provider_for_display(
                dpy, m_text_css, GTK_STYLE_PROVIDER_PRIORITY_USER + 20);
    }
    const std::string& hex = m_prefs.focus_text_color;
    if (hex.empty())
        m_text_css->load_from_data("#focus-view, #focus-view text {}");
    else
        m_text_css->load_from_data(
            "#focus-view, #focus-view text { color: " + hex + "; }");
    m_view.queue_draw();
}

// ── Navigation ───────────────────────────────────────────────────────────────
void FocusWindow::rebuild_scene_list() {
    m_scenes = m_model.manuscript_in_reading_order();
}

void FocusWindow::goto_node(BinderNode* node) {
    if (!node) return;
    // Delegate to the editor's single load/save path: it flushes the current
    // node and loads the next into the shared buffer, which this view shows.
    m_editor.load_node(node);
    m_current = node;
    set_title("Focus — " + (node->title.empty() ? std::string("(untitled)")
                                                 : node->title));
    update_navbar();
    LOG_DEBUG("focus goto {} ({})", node->iid,
              node->title.empty() ? "(untitled)" : node->title);
}

// Refresh the visible breadcrumb from the current node + its position in the
// reading-order list (so it can read "3 / 12 · Chapter One"). Scenes only.
void FocusWindow::update_navbar() {
    if (!m_nav_title) return;
    BinderNode* cur = m_current ? m_current : m_editor.current_node();
    std::string label = cur ? (cur->title.empty() ? "(untitled)" : cur->title)
                            : "No scene";
    if (cur && !m_scenes.empty()) {
        auto it = std::find(m_scenes.begin(), m_scenes.end(), cur);
        if (it != m_scenes.end()) {
            int pos = (int)std::distance(m_scenes.begin(), it) + 1;
            label = std::to_string(pos) + " / " +
                    std::to_string((int)m_scenes.size()) + "   " + label;
        }
    }
    m_nav_title->set_label(label);
}

void FocusWindow::goto_relative(int delta) {
    if (m_scenes.empty()) rebuild_scene_list();
    if (m_scenes.empty()) return;
    BinderNode* cur = m_current ? m_current : m_editor.current_node();
    int idx = 0;
    auto it = std::find(m_scenes.begin(), m_scenes.end(), cur);
    if (it != m_scenes.end()) idx = (int)std::distance(m_scenes.begin(), it);
    int next = std::clamp(idx + delta, 0, (int)m_scenes.size() - 1);
    if (next != idx || cur == nullptr) goto_node(m_scenes[next]);
}

void FocusWindow::open_switcher() {
    rebuild_scene_list();
    if (!m_switcher) return;
    if (m_switch_entry) m_switch_entry->set_text("");
    repopulate_switcher();
    m_switcher->set_visible(true);
    if (m_switch_entry) m_switch_entry->grab_focus();
}

// ── Link picker (s46) ────────────────────────────────────────────────────────
// Focus's own node picker: the same centred type-to-filter overlay as the scene
// switcher (reusing its panel CSS), but it lists every linkable node across the
// four authored sections and inserts a link at the SHARED cursor through
// Editor::insert_link. It deliberately does NOT use Editor::open_link_picker —
// that popover parents to the editor's window and points into the editor's view,
// which is the wrong (hidden) surface while focus is up.
void FocusWindow::build_link_picker() {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    box->set_name("focus-link-picker");
    box->add_css_class("focus-switcher");   // reuse the switcher panel look
    box->set_halign(Gtk::Align::CENTER);
    box->set_valign(Gtk::Align::CENTER);
    box->set_size_request(440, -1);

    auto* title = Gtk::make_managed<Gtk::Label>("Insert link");
    title->set_name("focus-link-title");
    title->add_css_class("focus-link-title");
    title->set_xalign(0.0f);
    box->append(*title);

    m_link_entry = Gtk::make_managed<Gtk::SearchEntry>();
    m_link_entry->set_name("focus-link-entry");
    m_link_entry->set_placeholder_text("Link to scene, character, place…");

    m_link_list = Gtk::make_managed<Gtk::ListBox>();
    m_link_list->set_name("focus-link-list");
    m_link_list->set_selection_mode(Gtk::SelectionMode::SINGLE);

    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroll->set_min_content_height(300);
    scroll->set_child(*m_link_list);

    box->append(*m_link_entry);
    box->append(*scroll);

    m_link_entry->signal_search_changed().connect(
        [this]() { repopulate_link_picker(); });
    m_link_entry->signal_activate().connect(
        [this]() { activate_link_row(m_link_list->get_selected_row()); });
    m_link_list->signal_row_activated().connect(
        [this](Gtk::ListBoxRow* row) { activate_link_row(row); });
    // SearchEntry eats Escape (stop-search) before the window key handler — hide here.
    m_link_entry->signal_stop_search().connect([this]() {
        if (m_link_picker) m_link_picker->set_visible(false);
        m_view.grab_focus();
    });

    m_link_picker = box;
    m_overlay.add_overlay(*box);
    box->set_visible(false);
}

// Rebuild the linkable-node set from the model (every non-group node across the
// four authored sections, tagged with its section), then show the overlay. Read
// through m_model — the same injected reference focus already uses — never the
// editor's widgets.
void FocusWindow::open_link_picker() {
    if (!m_link_picker) return;
    m_link_entries.clear();
    struct Sec { Section section; const char* label; };
    const Sec secs[] = {
        {Section::Manuscript, "Manuscript"},
        {Section::Characters, "Characters"},
        {Section::Places,     "Places"},
        {Section::References,  "References"},
    };
    std::function<void(const std::vector<BinderNode>&, const char*)> collect =
        [&](const std::vector<BinderNode>& nodes, const char* sec) {
            for (const auto& n : nodes) {
                if (!binder_kind_is_group(n.kind))
                    m_link_entries.push_back(
                        {n.iid, n.title.empty() ? "(untitled)" : n.title, sec});
                collect(n.children, sec);
            }
        };
    for (const auto& s : secs) collect(m_model.root(s.section), s.label);

    if (m_link_entry) m_link_entry->set_text("");
    repopulate_link_picker();
    m_link_picker->set_visible(true);
    if (m_link_entry) m_link_entry->grab_focus();
}

// Refill the list from m_link_entries filtered by the entry text. Row at display
// index i is the i-th entry passing the filter — activate_link_row recomputes the
// same filter to resolve index → entry (the switcher uses the same idiom).
void FocusWindow::repopulate_link_picker() {
    if (!m_link_list) return;
    while (auto* row = m_link_list->get_row_at_index(0))
        m_link_list->remove(*row);

    std::string q = m_link_entry ? std::string(m_link_entry->get_text()) : "";
    std::transform(q.begin(), q.end(), q.begin(), ::tolower);

    for (const auto& e : m_link_entries) {
        std::string lt = e.title;
        std::transform(lt.begin(), lt.end(), lt.begin(), ::tolower);
        if (!q.empty() && lt.find(q) == std::string::npos) continue;

        auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
        row->set_name(Folio::widget_name("focus-link-row", e.iid));
        auto* hb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        hb->set_margin_top(4);   hb->set_margin_bottom(4);
        hb->set_margin_start(8); hb->set_margin_end(8);
        auto* tl = Gtk::make_managed<Gtk::Label>(e.title);
        tl->set_xalign(0.0f); tl->set_hexpand(true);
        auto* sl = Gtk::make_managed<Gtk::Label>(e.section);
        sl->add_css_class("dim-label");
        sl->set_xalign(1.0f);
        hb->append(*tl); hb->append(*sl);
        row->set_child(*hb);
        m_link_list->append(*row);
    }
    if (auto* first = m_link_list->get_row_at_index(0))
        m_link_list->select_row(*first);
}

void FocusWindow::activate_link_row(Gtk::ListBoxRow* row) {
    if (!row) return;
    std::string q = m_link_entry ? std::string(m_link_entry->get_text()) : "";
    std::transform(q.begin(), q.end(), q.begin(), ::tolower);

    std::vector<const LinkEntry*> filtered;
    for (const auto& e : m_link_entries) {
        std::string lt = e.title;
        std::transform(lt.begin(), lt.end(), lt.begin(), ::tolower);
        if (q.empty() || lt.find(q) != std::string::npos) filtered.push_back(&e);
    }
    int idx = row->get_index();
    if (idx < 0 || idx >= (int)filtered.size()) return;
    const LinkEntry* e = filtered[idx];

    // insert_link tags the selection if there is one, else inserts the title as the
    // linked text — both at the shared cursor, so it lands where the writer is in
    // m_view. Empty anchor = link to the node (matches the editor's picker).
    m_editor.insert_link(e->iid, "", e->title);
    if (m_link_picker) m_link_picker->set_visible(false);
    flash_toast("\xF0\x9F\x94\x97  Link inserted");   // 🔗
    m_view.grab_focus();
    LOG_DEBUG("focus insert_link -> {} ({})", e->iid, e->title);
}

// ── Open ─────────────────────────────────────────────────────────────────────
void FocusWindow::present_focus(BinderNode* start) {
    rebuild_scene_list();
    BinderNode* node = start ? start : m_editor.current_node();
    if (!node && !m_scenes.empty()) node = m_scenes.front();
    m_current = node;
    if (node)
        set_title("Focus — " + (node->title.empty() ? std::string("(untitled)")
                                                     : node->title));
    update_navbar();
    // s46 — sync the View toggles from prefs (spell/annotations/links are shared and
    // may have changed in the editor; line-number/invisibles are focus-only) and apply
    // the line-number / invisibles overlays for this session.
    m_view_guard = true;
    if (m_spell_sw)     m_spell_sw->set_active(m_prefs.spell_check_enabled);
    if (m_ann_switch)   m_ann_switch->set_active(m_prefs.show_annotations);
    if (m_links_switch) m_links_switch->set_active(m_prefs.show_links);
    if (m_ln_switch)    m_ln_switch->set_active(m_prefs.focus_show_line_numbers);
    if (m_invis_switch) m_invis_switch->set_active(m_prefs.focus_show_invisibles);
    m_view_guard = false;
    apply_view_chrome();

    apply_focus_look();   // geometry/padding re-fire on signal_map (see ctor)
    // Snapshot the editor's size+zoom so we can restore them on exit, then show
    // focus at ITS OWN size with zoom neutralised (so the size is literal, not
    // editor-size × editor-zoom — the "additive" effect).
    m_saved_size = m_editor.body_font_size();
    m_saved_zoom = m_editor.zoom_factor();
    int focus_sz = m_prefs.focus_font_size > 0 ? m_prefs.focus_font_size
                                               : m_saved_size;
    focus_sz = std::clamp(focus_sz, 6, 72);
    m_editor.set_body_display(focus_sz, 1.0);
    if (m_size_adj) m_size_adj->set_value(focus_sz);
    fullscreen();
    present();
}

}  // namespace Folio
