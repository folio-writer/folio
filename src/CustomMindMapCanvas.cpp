// ─────────────────────────────────────────────────────────────────────────────
// CustomMindMapCanvas.cpp — the owned mind map, GTK side (s51). See header.
//
// THIN by contract: the only maths here asks the pure unit — world_to_screen /
// screen_to_world / hit_test (MindMap.cpp) place and pick; cmm_add_text /
// cmm_add_anchor / cmm_add_edge / cmm_stamp_frame / cmm_add_subject (CustomMind-
// Map.cpp) own the model. This file paints the CMMDoc, forwards input, and
// persists the document after each mutation. No layout rule, no second store.
// ─────────────────────────────────────────────────────────────────────────────
#include "CustomMindMapCanvas.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <unordered_map>

#include "FolioPrefs.hpp"
#include "DocumentModel.hpp"   // s89 — current_kind() / find_node_by_iid for anchor glyphs
#include "Iid.hpp"

namespace Folio {

namespace {
constexpr double kNodeR     = 18.0;   // world-radius of a node glyph at zoom 1
constexpr double kHitR      = 20.0;   // screen-px pick radius
constexpr double kZoomMin   = 0.10;
constexpr double kZoomMax   = 4.00;
constexpr double kFitMargin = 80.0;
constexpr double kGridWorld = 80.0;
constexpr double kFrameRing = 170.0;  // stamp fan radius (world units)

Gdk::RGBA themed(Gtk::Widget& w, const char* name, const char* fallback) {
    Gdk::RGBA c;
    if (w.get_style_context()->lookup_color(name, c)) return c;
    c.set(fallback);
    return c;
}

// FNV-1a, matching the deterministic-jitter idiom used elsewhere — a stable
// category→palette index so a given label always wears the same colour.
unsigned fnv1a(const std::string& s) {
    unsigned h = 2166136261u;
    for (unsigned char ch : s) { h ^= ch; h *= 16777619u; }
    return h;
}
}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
CustomMindMapCanvas::CustomMindMapCanvas(DocumentModel& model, FolioPrefs& prefs)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 0), m_model(model), m_prefs(prefs) {
    set_hexpand(true);
    set_vexpand(true);

    // ── Title (the map's name = the host fragment's title) ────────────────────
    m_title_entry.add_css_class("cmm-title");
    m_title_entry.add_css_class("title-3");
    m_title_entry.set_has_frame(false);
    m_title_entry.set_placeholder_text("Untitled map");
    m_title_entry.set_margin_top(10);
    m_title_entry.set_margin_start(14);
    m_title_entry.set_margin_end(14);
    m_title_entry.signal_changed().connect([this]() {
        if (m_title_loading || !m_attached) return;
        const std::string name = m_title_entry.get_text();
        m_doc.name = name;
        if (m_on_rename) m_on_rename(name);     // → node->title + chrome refresh
        persist();
    });
    append(m_title_entry);

    // ── About header (the subjects chip-row) ─────────────────────────────────
    m_about_row.add_css_class("cmm-about");
    m_about_row.set_margin_top(8);
    m_about_row.set_margin_bottom(4);
    m_about_row.set_margin_start(14);
    m_about_row.set_margin_end(14);
    m_about_label.set_text("About");
    m_about_label.add_css_class("dim-label");
    m_about_label.set_valign(Gtk::Align::CENTER);
    m_about_row.append(m_about_label);
    m_chip_box.set_valign(Gtk::Align::CENTER);
    m_about_row.append(m_chip_box);
    m_subject_add.set_label("\uFF0B");                 // ＋
    m_subject_add.set_tooltip_text("Add a subject this map is about");
    m_subject_add.add_css_class("flat");
    m_subject_add.set_valign(Gtk::Align::CENTER);
    m_subject_add.signal_clicked().connect([this]() {
        if (!m_attached) return;
        open_object_picker(/*for_subject=*/true);
    });
    m_about_row.append(m_subject_add);
    append(m_about_row);

    // ── Drawing surface ───────────────────────────────────────────────────────
    m_area.set_hexpand(true);
    m_area.set_vexpand(true);
    m_area.set_can_focus(true);
    m_area.set_focusable(true);
    m_area.set_draw_func(sigc::mem_fun(*this, &CustomMindMapCanvas::draw));
    m_area.signal_resize().connect([this](int w, int /*h*/) {
        if (m_fit_pending && w > 0 && !m_placements.empty()) {
            zoom_to_fit();
            m_fit_pending = false;
        }
    });

    m_overlay.set_child(m_area);
    m_overlay.set_hexpand(true);
    m_overlay.set_vexpand(true);

    build_tools();

    m_empty_hint.set_text("An empty canvas.\nDrop a text node, anchor a "
                          "character or place, then draw links between them.");
    m_empty_hint.set_justify(Gtk::Justification::CENTER);
    m_empty_hint.add_css_class("board-placeholder");
    m_empty_hint.set_halign(Gtk::Align::CENTER);
    m_empty_hint.set_valign(Gtk::Align::CENTER);
    m_empty_hint.set_can_target(false);
    m_empty_hint.set_visible(false);
    m_overlay.add_overlay(m_empty_hint);

    append(m_overlay);

    // ── Pointer tracking + hover + link rubber-band ──────────────────────────
    auto motion = Gtk::EventControllerMotion::create();
    motion->signal_enter().connect([this](double, double) {
        // Focus the canvas when the pointer is over it, so tool/zoom keys land here
        // rather than in the title entry (which GTK focuses first on view open).
        if (!m_area.has_focus()) m_area.grab_focus();
    });
    motion->signal_motion().connect([this](double x, double y) {
        m_ptr_x = x; m_ptr_y = y;
        if (!m_link_from.empty()) { m_link_x = x; m_link_y = y; m_area.queue_draw(); return; }
        if (m_panning || m_moving || m_marquee) return;   // don't hover mid-gesture
        const std::string hit = hit_test(m_placements, m_vp, x, y, kHitR);
        if (hit != m_hover_id) { m_hover_id = hit; m_area.queue_draw(); }
    });
    motion->signal_leave().connect([this]() {
        if (!m_hover_id.empty()) { m_hover_id.clear(); m_area.queue_draw(); }
    });
    m_area.add_controller(motion);

    // ── Keyboard ──────────────────────────────────────────────────────────────
    // Controller on the canvas BOX (not just the DrawingArea) so the keys fire
    // whenever focus is anywhere in the canvas — but skip while the user is typing
    // in a field (the title, a picker search, a node's editor), so letters type.
    auto key = Gtk::EventControllerKey::create();
    key->signal_key_pressed().connect(
        [this](guint kv, guint, Gdk::ModifierType) {
            if (auto* win = dynamic_cast<Gtk::Window*>(get_root())) {
                Gtk::Widget* foc = win->get_focus();
                if (foc && (dynamic_cast<Gtk::Editable*>(foc) ||
                            dynamic_cast<Gtk::TextView*>(foc)))
                    return false;                       // typing — let the field have it
            }
            const double cx = m_area.get_width() / 2.0, cy = m_area.get_height() / 2.0;
            const double step = 70.0;
            switch (kv) {
                case GDK_KEY_space:                       m_space_held = true; return true;
                case GDK_KEY_Escape:
                    if (!m_link_from.empty()) { m_link_from.clear(); m_area.queue_draw(); }
                    if (!m_sel.empty())       { m_sel.clear(); refresh_selection_ui(); }
                    set_tool(Tool::Select);
                    return true;
                case GDK_KEY_Delete:
                case GDK_KEY_KP_Delete:
                case GDK_KEY_BackSpace:
                    if (!m_sel.empty()) { delete_selection(); return true; }
                    return false;
                case GDK_KEY_plus: case GDK_KEY_equal:
                case GDK_KEY_KP_Add:                       zoom_about(1.15, cx, cy); return true;
                case GDK_KEY_minus: case GDK_KEY_underscore:
                case GDK_KEY_KP_Subtract:                  zoom_about(1.0 / 1.15, cx, cy); return true;
                case GDK_KEY_0: case GDK_KEY_KP_0:
                case GDK_KEY_f: case GDK_KEY_F:            zoom_to_fit(); m_area.queue_draw(); return true;
                // Tool hotkeys (muscle memory): S Select · T Text · A Anchor · L Link.
                case GDK_KEY_s: case GDK_KEY_S:            set_tool(Tool::Select);    return true;
                case GDK_KEY_t: case GDK_KEY_T:            set_tool(Tool::PlaceText); return true;
                case GDK_KEY_l: case GDK_KEY_L:            set_tool(Tool::Link);      return true;
                case GDK_KEY_a: case GDK_KEY_A:
                    if (m_attached && m_tool != Tool::PlaceAnchor) open_object_picker(/*for_subject=*/false);
                    return true;
                case GDK_KEY_Left:  m_vp.pan_x += step; m_area.queue_draw(); return true;
                case GDK_KEY_Right: m_vp.pan_x -= step; m_area.queue_draw(); return true;
                case GDK_KEY_Up:    m_vp.pan_y += step; m_area.queue_draw(); return true;
                case GDK_KEY_Down:  m_vp.pan_y -= step; m_area.queue_draw(); return true;
                default: return false;
            }
        }, false);
    key->signal_key_released().connect(
        [this](guint kv, guint, Gdk::ModifierType) {
            if (kv == GDK_KEY_space) m_space_held = false;
        });
    add_controller(key);                                  // on the canvas Box
    m_area.signal_map().connect([this]() { m_area.grab_focus(); });

    // ── Ctrl-scroll zoom; plain/shift scroll = pan ───────────────────────────
    auto scroll = Gtk::EventControllerScroll::create();
    scroll->set_flags(Gtk::EventControllerScroll::Flags::BOTH_AXES);
    scroll->signal_scroll().connect(
        [this, scroll](double dx, double dy) {
            const bool ctrl =
                (scroll->get_current_event_state() & Gdk::ModifierType::CONTROL_MASK)
                != Gdk::ModifierType{};
            if (ctrl) {
                const double factor = (dy < 0.0) ? 1.10 : (dy > 0.0 ? 1.0 / 1.10 : 1.0);
                if (factor != 1.0) zoom_about(factor, m_ptr_x, m_ptr_y);
            } else {
                const bool shift =
                    (scroll->get_current_event_state() & Gdk::ModifierType::SHIFT_MASK)
                    != Gdk::ModifierType{};
                const double step = 48.0;
                if (shift) m_vp.pan_x -= dy * step;
                else     { m_vp.pan_x -= dx * step; m_vp.pan_y -= dy * step; }
                m_area.queue_draw();
            }
            return true;
        }, false);
    m_area.add_controller(scroll);

    // ── Middle-drag pan ───────────────────────────────────────────────────────
    auto pan_mid = Gtk::GestureDrag::create();
    pan_mid->set_button(GDK_BUTTON_MIDDLE);
    pan_mid->signal_drag_begin().connect([this](double, double) {
        m_panning = true; m_pan_base_x = m_vp.pan_x; m_pan_base_y = m_vp.pan_y;
    });
    pan_mid->signal_drag_update().connect([this](double ox, double oy) {
        m_vp.pan_x = m_pan_base_x + ox; m_vp.pan_y = m_pan_base_y + oy;
        m_area.queue_draw();
    });
    pan_mid->signal_drag_end().connect([this](double, double) { m_panning = false; });
    m_area.add_controller(pan_mid);

    // ── Primary drag: Link draws a connection; Select moves the selection or
    //    rubber-band-picks; space/empty pans. ───────────────────────────────────
    auto drag = Gtk::GestureDrag::create();
    drag->set_button(GDK_BUTTON_PRIMARY);
    drag->signal_drag_begin().connect([this](double sx, double sy) {
        m_drag_sx = sx; m_drag_sy = sy;
        m_drag_committed = false;
        m_link_from.clear();
        m_moving = false;
        m_marquee = false;
        m_panning = false;
    });
    drag->signal_drag_update().connect([this, drag](double ox, double oy) {
        if (!m_drag_committed) {
            if (std::hypot(ox, oy) < 8.0) return;     // jitter, not a drag
            m_drag_committed = true;
            m_did_pan = true;                          // suppress the click
            const std::string hit = m_space_held ? std::string()
                                  : hit_test(m_placements, m_vp, m_drag_sx, m_drag_sy, kHitR);
            const bool extend =
                (drag->get_current_event_state() & Gdk::ModifierType::SHIFT_MASK)
                != Gdk::ModifierType{};
            if (m_space_held) {                        // explicit pan
                m_panning = true; m_pan_base_x = m_vp.pan_x; m_pan_base_y = m_vp.pan_y;
            } else if (m_tool == Tool::Link && !hit.empty()) {
                m_link_from = hit;                     // begin a connection
            } else if (m_tool == Tool::Select && !hit.empty()) {
                // Press on a node: if it isn't already selected, make it the lone
                // selection; then lift the WHOLE selection to move together.
                if (m_sel.find(hit) == m_sel.end()) {
                    m_sel.clear(); m_sel.insert(hit); refresh_selection_ui();
                }
                m_move_base.clear();
                for (const CMMNode& n : m_doc.nodes)
                    if (m_sel.count(n.id)) m_move_base[n.id] = WorldPt{ n.x, n.y };
                m_moving = true;
            } else if (m_tool == Tool::Select) {       // empty press → marquee
                m_marquee = true;
                m_marquee_add = extend;                // Shift/Ctrl → add to selection
                m_sel_base = extend ? m_sel : std::set<std::string>{};
                m_marq_x0 = m_marq_x1 = m_drag_sx;
                m_marq_y0 = m_marq_y1 = m_drag_sy;
            } else {                                   // a place tool dragging → pan
                m_panning = true; m_pan_base_x = m_vp.pan_x; m_pan_base_y = m_vp.pan_y;
            }
        }
        if (!m_link_from.empty()) {
            m_link_x = m_drag_sx + ox; m_link_y = m_drag_sy + oy;
            m_area.queue_draw();
        } else if (m_moving) {
            const double z = (m_vp.zoom != 0.0) ? m_vp.zoom : 1.0;
            const double dwx = ox / z, dwy = oy / z;
            for (auto& n : m_doc.nodes) {
                auto it = m_move_base.find(n.id);
                if (it != m_move_base.end()) { n.x = it->second.x + dwx; n.y = it->second.y + dwy; }
            }
            recompute();
        } else if (m_marquee) {
            m_marq_x1 = m_drag_sx + ox; m_marq_y1 = m_drag_sy + oy;
            // Live selection: the base (kept on Shift/Ctrl) unioned with every node
            // whose centre falls inside the rectangle.
            const double lo_x = std::min(m_marq_x0, m_marq_x1), hi_x = std::max(m_marq_x0, m_marq_x1);
            const double lo_y = std::min(m_marq_y0, m_marq_y1), hi_y = std::max(m_marq_y0, m_marq_y1);
            m_sel = m_sel_base;
            for (const auto& p : m_placements) {
                const ScreenPt s = world_to_screen(m_vp, p.x, p.y);
                if (s.x >= lo_x && s.x <= hi_x && s.y >= lo_y && s.y <= hi_y) m_sel.insert(p.iid);
            }
            refresh_selection_ui();
        } else if (m_panning) {
            m_vp.pan_x = m_pan_base_x + ox; m_vp.pan_y = m_pan_base_y + oy;
            m_area.queue_draw();
        }
    });
    drag->signal_drag_end().connect([this](double, double) {
        if (!m_link_from.empty()) {                    // complete a connection?
            const std::string to = hit_test(m_placements, m_vp, m_link_x, m_link_y, kHitR);
            const std::string from = m_link_from;
            m_link_from.clear();
            if (!to.empty() && to != from)
                open_category_popover(from, to, m_link_x, m_link_y);
            else
                set_tool(Tool::Select);                // missed → done, return home
            m_area.queue_draw();
        } else if (m_moving) {
            m_moving = false; m_move_base.clear();
            persist();                                 // a move settled — save x/y
        } else if (m_marquee) {
            m_marquee = false; m_area.queue_draw();    // selection already set live
        }
        m_panning = false; m_drag_committed = false;
    });
    m_area.add_controller(drag);

    // ── Primary click: place / select / toggle / open ────────────────────────
    auto click = Gtk::GestureClick::create();
    click->set_button(GDK_BUTTON_PRIMARY);
    click->signal_released().connect([this, click](int n_press, double x, double y) {
        m_area.grab_focus();
        if (m_did_pan) { m_did_pan = false; return; }   // a drag, not a click
        const auto state = click->get_current_event_state();
        const bool ctrl  = (state & Gdk::ModifierType::CONTROL_MASK) != Gdk::ModifierType{};
        const bool shift = (state & Gdk::ModifierType::SHIFT_MASK)   != Gdk::ModifierType{};

        // Ctrl-click zooms IN about the click point; Ctrl+Shift zooms OUT (matches
        // the lens). Ctrl owns zoom, so Shift alone stays free for multi-select.
        if (ctrl) {
            const WorldPt wp = screen_to_world(m_vp, x, y);
            m_vp.zoom = std::clamp(m_vp.zoom * (shift ? 1.0 / 1.5 : 1.5), kZoomMin, kZoomMax);
            m_vp.pan_x = m_area.get_width()  / 2.0 - wp.x * m_vp.zoom;
            m_vp.pan_y = m_area.get_height() / 2.0 - wp.y * m_vp.zoom;
            m_area.queue_draw();
            return;
        }
        const bool extend = shift;                       // multi-select uses Shift only

        const WorldPt    w   = screen_to_world(m_vp, x, y);
        const std::string hit = hit_test(m_placements, m_vp, x, y, kHitR);

        // Armed placement tools drop on the next click, anywhere (then go home).
        if (m_tool == Tool::PlaceText) {
            const std::string id = cmm_add_text(m_doc, "New idea", w.x, w.y);
            set_tool(Tool::Select);
            m_empty_hint.set_visible(false);
            recompute(); persist();
            const ScreenPt sp = world_to_screen(m_vp, w.x, w.y);
            open_text_editor(id, sp.x, sp.y);
            return;
        }
        if (m_tool == Tool::PlaceAnchor) {
            if (!m_pending_anchor_iid.empty()) {
                cmm_add_anchor(m_doc, m_pending_anchor_iid, m_pending_anchor_name, w.x, w.y);
                m_pending_anchor_iid.clear(); m_pending_anchor_name.clear();
                m_empty_hint.set_visible(false);
                recompute(); persist();
            }
            set_tool(Tool::Select);
            return;
        }
        if (m_tool == Tool::Link) {                     // links are a drag; a click clears
            if (!extend && !m_sel.empty()) { m_sel.clear(); refresh_selection_ui(); }
            return;
        }

        // Select tool.
        if (!hit.empty()) {
            if (n_press >= 2) {                         // open / edit
                const CMMNode* n = node_at_id(hit);
                if (n && n->kind == CMMNodeKind::Anchor && !n->iid.empty()) {
                    if (m_on_open) m_on_open(n->iid);
                } else {
                    open_text_editor(hit, x, y);
                }
                return;
            }
            if (extend) {                               // shift/ctrl toggles membership
                if (m_sel.count(hit)) m_sel.erase(hit); else m_sel.insert(hit);
            } else {                                    // plain click → lone selection
                m_sel.clear(); m_sel.insert(hit);
            }
            refresh_selection_ui();
            return;
        }

        // Empty canvas.
        if (n_press == 2) {                             // double-click drops a text node
            const std::string id = cmm_add_text(m_doc, "New idea", w.x, w.y);
            m_empty_hint.set_visible(false);
            recompute(); persist();
            open_text_editor(id, x, y);
        } else if (!extend && !m_sel.empty()) {         // plain click clears selection
            m_sel.clear(); refresh_selection_ui();
        }
    });
    m_area.add_controller(click);
}

// ── Floating OSD toolbar ──────────────────────────────────────────────────────
void CustomMindMapCanvas::build_tools() {
    m_tools.add_css_class("osd");
    m_tools.add_css_class("toolbar");
    m_tools.set_halign(Gtk::Align::CENTER);
    m_tools.set_valign(Gtk::Align::END);
    m_tools.set_margin_bottom(14);
    m_tools.set_can_focus(false);

    // Underline the hotkey letter on each tool so the key is discoverable at rest
    // (markup underline, always visible — not a mnemonic that hides until Alt).
    auto set_keylabel = [](Gtk::ToggleButton& b, const char* markup) -> Gtk::Label* {
        auto* l = Gtk::make_managed<Gtk::Label>();
        l->set_markup(markup);
        b.set_child(*l);
        return l;
    };
    m_tl_select = set_keylabel(m_t_select, "<u>S</u>elect");
    m_t_select.set_tooltip_text("Select & move nodes — S   (drag to marquee; Del removes)");
    m_tl_text = set_keylabel(m_t_text, "<u>T</u>ext");
    m_t_text.set_tooltip_text("Drop a text node — T   (then click the canvas)");
    m_tl_anchor = set_keylabel(m_t_anchor, "<u>A</u>nchor");
    m_t_anchor.set_tooltip_text("Anchor a scene / character / place — A");
    m_tl_link = set_keylabel(m_t_link, "<u>L</u>ink");
    m_t_link.set_tooltip_text("Drag node \u2192 node to connect them — L");
    m_t_frame.set_label("Frame \u25BE");
    m_t_frame.set_tooltip_text("Stamp a labelled frame (Five W\u2019s\u2026)");
    m_fit_btn.set_label("\u2922");
    m_fit_btn.set_tooltip_text("Fit map to view — F");

    for (Gtk::Widget* b : { (Gtk::Widget*)&m_t_select, (Gtk::Widget*)&m_t_text,
                            (Gtk::Widget*)&m_t_anchor, (Gtk::Widget*)&m_t_link,
                            (Gtk::Widget*)&m_t_frame,  (Gtk::Widget*)&m_fit_btn })
        b->set_can_focus(false);

    // Tools are a manual radio group: drive selection from clicked() (not toggled,
    // to avoid the GTK4 toggle re-entrancy trap), and mirror state via set_tool().
    // Each authoring tool is one-shot — it returns HOME to Select once it acts.
    m_t_select.signal_clicked().connect([this]() { set_tool(Tool::Select); });
    m_t_text.signal_clicked().connect([this]() {
        set_tool(m_tool == Tool::PlaceText ? Tool::Select : Tool::PlaceText);
    });
    m_t_anchor.signal_clicked().connect([this]() {
        if (m_tool == Tool::PlaceAnchor) { set_tool(Tool::Select); return; }
        if (!m_attached) { set_tool(Tool::Select); return; }
        // Pick the object first; the picker arms PlaceAnchor on selection.
        open_object_picker(/*for_subject=*/false);
    });
    m_t_link.signal_clicked().connect([this]() {
        set_tool(m_tool == Tool::Link ? Tool::Select : Tool::Link);
    });
    m_t_frame.signal_clicked().connect([this]() { open_frame_menu(); });
    m_fit_btn.signal_clicked().connect([this]() { zoom_to_fit(); m_area.queue_draw(); });

    m_tools.append(m_t_select);
    m_tools.append(m_t_text);
    m_tools.append(m_t_anchor);
    m_tools.append(m_t_link);
    m_tools.append(m_t_frame);
    auto* sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL);
    m_tools.append(*sep);
    m_tools.append(m_fit_btn);
    m_overlay.add_overlay(m_tools);

    // Top-centre mode banner — names the armed tool (the toggle's active styling is
    // unreliable in this GTK build, so the mode must read somewhere certain).
    m_mode_hint.add_css_class("osd");
    m_mode_hint.set_halign(Gtk::Align::CENTER);
    m_mode_hint.set_valign(Gtk::Align::START);
    m_mode_hint.set_margin_top(12);
    m_mode_hint.set_can_target(false);
    m_mode_hint.set_visible(false);
    m_overlay.add_overlay(m_mode_hint);

    // Bottom-left selection badge — an unmistakable read-out of how many nodes are
    // selected (alongside the bright ring each one wears).
    m_sel_badge.add_css_class("osd");
    m_sel_badge.set_halign(Gtk::Align::START);
    m_sel_badge.set_valign(Gtk::Align::START);
    m_sel_badge.set_margin_top(12);
    m_sel_badge.set_margin_start(12);
    m_sel_badge.set_can_target(false);
    m_sel_badge.set_visible(false);
    m_overlay.add_overlay(m_sel_badge);

    set_tool(Tool::Select);
}

void CustomMindMapCanvas::update_mode_hint() {
    std::string t;
    switch (m_tool) {
        case Tool::PlaceText:   t = "Click the canvas to place a text node"; break;
        case Tool::PlaceAnchor:
            t = m_pending_anchor_name.empty()
                  ? "Click the canvas to place the anchor"
                  : "Click to place: " + m_pending_anchor_name;
            break;
        case Tool::Link:        t = "Drag from one node to another to link them"; break;
        case Tool::Select:      t = ""; break;
    }
    m_mode_hint.set_text(t);
    m_mode_hint.set_visible(!t.empty());
}

void CustomMindMapCanvas::set_tool(Tool t) {
    m_tool = t;
    if (t != Tool::PlaceAnchor) { m_pending_anchor_iid.clear(); m_pending_anchor_name.clear(); }
    // Mirror the active tool onto the buttons via direct label pointers. The active
    // marker is a bold bullet + a teal pill behind the text (Pango markup) — both
    // text-level, so they render regardless of the button theme.
    struct TB { Gtk::ToggleButton* b; Gtk::Label* l; Tool t; const char* mk; };
    for (TB tb : { TB{ &m_t_select, m_tl_select, Tool::Select,      "<u>S</u>elect" },
                   TB{ &m_t_text,   m_tl_text,   Tool::PlaceText,   "<u>T</u>ext"   },
                   TB{ &m_t_anchor, m_tl_anchor, Tool::PlaceAnchor, "<u>A</u>nchor" },
                   TB{ &m_t_link,   m_tl_link,   Tool::Link,        "<u>L</u>ink"   } }) {
        const bool on = (tb.t == t);
        if (tb.b->get_active() != on) tb.b->set_active(on);
        if (!tb.l) continue;
        if (on)
            tb.l->set_markup(std::string("<span background=\"#5bc8af\" foreground=\"#11111b\">"
                             "<b>\u2009\u25CF\u2009") + tb.mk + "\u2009</b></span>");
        else
            tb.l->set_markup(tb.mk);
    }
    update_mode_hint();
}

// Remove every selected node and its incident edges, then clear the selection.
void CustomMindMapCanvas::delete_selection() {
    if (m_sel.empty()) return;
    for (const std::string& id : m_sel) cmm_remove_node(m_doc, id);
    m_sel.clear();
    m_hover_id.clear();
    recompute(); persist();
    refresh_selection_ui();
}

// The selection read-out: a "N selected" badge + a redraw so the rings repaint.
void CustomMindMapCanvas::refresh_selection_ui() {
    const std::size_t n = m_sel.size();
    if (n == 0)      m_sel_badge.set_text("");
    else if (n == 1) m_sel_badge.set_text("1 selected");
    else             m_sel_badge.set_text(std::to_string(n) + " selected");
    m_sel_badge.set_visible(n > 0);
    m_area.queue_draw();
}

// Owned colour → a paint tint. A set color_idx wins; an Anchor with none inherits
// its target object's label colour (the provider); otherwise the kind default.
Gdk::RGBA CustomMindMapCanvas::node_tint(const CMMNode& n) {
    int idx = n.color_idx;
    if (idx <= 0 && n.kind == CMMNodeKind::Anchor && m_color_of && !n.iid.empty())
        idx = m_color_of(n.iid);
    if (idx > 0) {
        const std::string hex = m_prefs.color_hex_for_idx(idx);
        if (!hex.empty()) { Gdk::RGBA c; c.set(hex); return c; }
    }
    return (n.kind == CMMNodeKind::Anchor)
         ? themed(m_area, "accent",   "#5bc8af")
         : themed(m_area, "col_green", "#a6e3a1");
}

// ── Subjects chip-row ─────────────────────────────────────────────────────────
void CustomMindMapCanvas::rebuild_chips() {
    Gtk::Widget* c = m_chip_box.get_first_child();
    while (c) { Gtk::Widget* nx = c->get_next_sibling(); m_chip_box.remove(*c); c = nx; }

    if (m_doc.subject_iids.empty()) {
        auto* none = Gtk::make_managed<Gtk::Label>("\u2014 not about a specific object");
        none->add_css_class("dim-label");
        m_chip_box.append(*none);
        return;
    }
    for (const std::string& iid : m_doc.subject_iids) {
        auto* chip = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
        chip->add_css_class("cmm-chip");
        auto* lab = Gtk::make_managed<Gtk::Label>(display_name(iid));
        chip->append(*lab);
        auto* x = Gtk::make_managed<Gtk::Button>();
        x->set_label("\u00D7");
        x->add_css_class("flat");
        x->set_tooltip_text("Remove subject");
        const std::string captured = iid;
        x->signal_clicked().connect([this, captured]() {
            cmm_remove_subject(m_doc, captured);
            rebuild_chips(); persist();
        });
        chip->append(*x);
        m_chip_box.append(*chip);
    }
}

// ── Document lifecycle ────────────────────────────────────────────────────────
void CustomMindMapCanvas::set_document(const CMMDoc& doc) {
    m_doc = doc;
    m_vp = m_doc.viewport;
    m_attached = true;
    m_sel.clear(); m_hover_id.clear(); m_link_from.clear();
    set_tool(Tool::Select);
    refresh_selection_ui();
    m_title_loading = true;
    m_title_entry.set_text(m_doc.name);
    m_title_loading = false;
    rebuild_chips();
    m_fit_pending = true;
    recompute();
}

void CustomMindMapCanvas::load_string(const std::string& iid, const std::string& body) {
    CMMDoc d;
    bool parsed = false;
    if (!body.empty()) {
        try { d = cmm_from_string(body); parsed = true; } catch (...) { parsed = false; }
    }
    if (!parsed) { d = CMMDoc{}; }
    d.id = iid;                                  // the host fragment owns identity
    set_document(d);
}

void CustomMindMapCanvas::clear_document() {
    m_doc = CMMDoc{};
    m_attached = false;
    m_placements.clear();
    rebuild_chips();
    m_area.queue_draw();
}

// ── Pure-seam plumbing ────────────────────────────────────────────────────────
void CustomMindMapCanvas::recompute() {
    m_placements.clear();
    m_placements.reserve(m_doc.nodes.size());
    for (const CMMNode& n : m_doc.nodes) {
        MindMapLayout::Placement p;
        p.iid   = n.id;                          // doc-local id is the hit key here
        p.x = n.x; p.y = n.y;
        p.glyph = cmm_node_glyph(n);
        // s89 — an Anchor borrows its target's shape; resolve the target's CURRENT
        // role from the model so a converted Scene↔Group target shows the right
        // glyph (cmm_node_glyph reads the iid prefix, which is the birth kind).
        if (n.kind == CMMNodeKind::Anchor && !n.iid.empty())
            p.glyph = map_glyph_for(current_kind(m_model, n.iid));
        m_placements.push_back(p);
    }
    m_empty_hint.set_visible(m_attached && m_doc.nodes.empty());
    m_area.queue_draw();
}

void CustomMindMapCanvas::persist() {
    m_doc.viewport = m_vp;                        // keep the lens position with the doc
    if (m_on_persist) m_on_persist(cmm_to_string(m_doc, /*pretty=*/false));
}

const CMMNode* CustomMindMapCanvas::node_at_id(const std::string& id) const {
    return cmm_find_node(m_doc, id);
}

std::string CustomMindMapCanvas::display_name(const std::string& iid) const {
    if (m_name_of) { std::string n = m_name_of(iid); if (!n.empty()) return n; }
    return iid;
}

Gdk::RGBA CustomMindMapCanvas::category_color(const std::string& category) {
    if (category.empty()) return themed(m_area, "tx3", "#9196b4");
    const int n = std::max(1, (int)m_prefs.tag_colors.size());
    const int idx = (int)(fnv1a(category) % (unsigned)n) + 1;     // 1-based palette
    Gdk::RGBA c;
    const std::string hex = m_prefs.color_hex_for_idx(idx);
    c.set(hex.empty() ? "#9196b4" : hex);
    return c;
}

// ── Viewport helpers (transforms from the pure unit) ──────────────────────────
void CustomMindMapCanvas::zoom_about(double factor, double sx, double sy) {
    const WorldPt before = screen_to_world(m_vp, sx, sy);
    m_vp.zoom = std::clamp(m_vp.zoom * factor, kZoomMin, kZoomMax);
    m_vp.pan_x = sx - before.x * m_vp.zoom;
    m_vp.pan_y = sy - before.y * m_vp.zoom;
    m_area.queue_draw();
}

void CustomMindMapCanvas::zoom_to_fit() {
    bool any = false;
    double minx = 0, miny = 0, maxx = 0, maxy = 0;
    for (const auto& p : m_placements) {
        if (!any) { minx = maxx = p.x; miny = maxy = p.y; any = true; }
        minx = std::min(minx, p.x); maxx = std::max(maxx, p.x);
        miny = std::min(miny, p.y); maxy = std::max(maxy, p.y);
    }
    const double w = std::max(1, m_area.get_width());
    const double h = std::max(1, m_area.get_height());
    if (!any) { m_vp = MapViewport{ w / 2.0, h / 2.0, 1.0 }; m_area.queue_draw(); return; }
    const double bw = std::max(1.0, maxx - minx);
    const double bh = std::max(1.0, maxy - miny);
    double zoom = std::min((w - 2 * kFitMargin) / bw, (h - 2 * kFitMargin) / bh);
    zoom = std::clamp(zoom, kZoomMin, kZoomMax);
    const double cx = (minx + maxx) / 2.0, cy = (miny + maxy) / 2.0;
    m_vp.zoom  = zoom;
    m_vp.pan_x = w / 2.0 - cx * zoom;
    m_vp.pan_y = h / 2.0 - cy * zoom;
    m_area.queue_draw();
}

// ── Paint ─────────────────────────────────────────────────────────────────────
void CustomMindMapCanvas::draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
    if (m_fit_pending && w > 0 && h > 0 && !m_placements.empty()) {
        zoom_to_fit(); m_fit_pending = false;
    }

    const Gdk::RGBA bg  = themed(m_area, "adw_bg2", "#181825");
    const Gdk::RGBA fg  = themed(m_area, "tx1",     "#cdd6f4");
    const Gdk::RGBA dim = themed(m_area, "tx4",     "#5a5d75");

    cr->set_source_rgb(bg.get_red(), bg.get_green(), bg.get_blue());
    cr->paint();

    // Dot grid (the infinite-sheet feel).
    const double gstep = kGridWorld * m_vp.zoom;
    if (gstep >= 14.0) {
        cr->set_source_rgba(dim.get_red(), dim.get_green(), dim.get_blue(), 0.35);
        const double x0 = std::fmod(m_vp.pan_x, gstep);
        const double y0 = std::fmod(m_vp.pan_y, gstep);
        for (double x = x0; x < w; x += gstep)
            for (double y = y0; y < h; y += gstep) { cr->arc(x, y, 1.0, 0.0, 2.0 * M_PI); cr->fill(); }
    }

    // Screen positions for every node (edges + nodes draw from this).
    std::unordered_map<std::string, ScreenPt> at;
    at.reserve(m_placements.size());
    for (const auto& p : m_placements) at[p.iid] = world_to_screen(m_vp, p.x, p.y);

    const double r = std::clamp(kNodeR * m_vp.zoom, 7.0, 32.0);
    const bool   show_labels = r > 9.0;

    // ── Owned edges — a bowed line per edge, coloured by its category ─────────
    for (const CMMEdge& e : m_doc.edges) {
        auto a = at.find(e.from); auto b = at.find(e.to);
        if (a == at.end() || b == at.end()) continue;
        const Gdk::RGBA col = category_color(e.category);

        const bool hov     = !m_hover_id.empty();
        const bool touches = hov && (e.from == m_hover_id || e.to == m_hover_id);
        double alpha = hov ? (touches ? 0.95 : 0.06) : (e.category.empty() ? 0.45 : 0.72);

        const double ax = a->second.x, ay = a->second.y;
        const double bx = b->second.x, by = b->second.y;
        const double ex = bx - ax, ey = by - ay;
        const double elen = std::hypot(ex, ey);
        const double bow = elen * 0.12;
        const double mx = (ax + bx) / 2.0 - (elen > 1e-6 ? ey / elen : 0.0) * bow;
        const double my = (ay + by) / 2.0 + (elen > 1e-6 ? ex / elen : 0.0) * bow;

        cr->set_source_rgba(col.get_red(), col.get_green(), col.get_blue(), alpha);
        cr->set_line_width(touches ? 2.0 : (e.category.empty() ? 1.0 : 1.6));
        cr->unset_dash();
        cr->move_to(ax, ay);
        cr->curve_to(mx, my, mx, my, bx, by);
        cr->stroke();

        // Arrowhead for a directed edge (an Anchor link reads toward truth).
        if (e.directed) {
            const double dx = bx - mx, dy = by - my, len = std::hypot(dx, dy);
            if (len > 1.0) {
                const double ux = dx / len, uy = dy / len;
                const double tx = bx - ux * r, ty = by - uy * r;
                const double ah = 8.0, aw = 4.5;
                cr->move_to(tx, ty);
                cr->line_to(tx - ux * ah + (-uy) * aw, ty - uy * ah + (ux) * aw);
                cr->line_to(tx - ux * ah - (-uy) * aw, ty - uy * ah - (ux) * aw);
                cr->close_path();
                cr->fill();
            }
        }

        // Category chip at the midpoint (only when it earns the space).
        if (!e.category.empty() && show_labels && (!hov || touches)) {
            auto cl = m_area.create_pango_layout(e.category);
            Pango::FontDescription fd("sans 8"); cl->set_font_description(fd);
            int tw = 0, th = 0; cl->get_pixel_size(tw, th);
            const double pad = 4.0, cw = tw + pad * 2, chh = th + pad;
            const double bxm = mx - cw / 2.0, bym = my - chh / 2.0;
            cr->set_source_rgba(bg.get_red(), bg.get_green(), bg.get_blue(), 0.9);
            cr->rectangle(bxm, bym, cw, chh); cr->fill();
            cr->set_source_rgba(col.get_red(), col.get_green(), col.get_blue(), 0.9);
            cr->move_to(bxm + pad, bym + pad / 2.0);
            cl->show_in_cairo_context(cr);
        }
    }

    // Link rubber-band (the connection being drawn).
    if (!m_link_from.empty()) {
        auto f = at.find(m_link_from);
        if (f != at.end()) {
            const Gdk::RGBA ac = themed(m_area, "accent", "#5bc8af");
            cr->set_source_rgba(ac.get_red(), ac.get_green(), ac.get_blue(), 0.85);
            cr->set_line_width(1.6);
            std::vector<double> d{4.0, 4.0}; cr->set_dash(d, 0.0);
            cr->move_to(f->second.x, f->second.y);
            cr->line_to(m_link_x, m_link_y);
            cr->stroke();
            cr->unset_dash();
        }
    }

    // ── Nodes ──────────────────────────────────────────────────────────────────
    for (const auto& p : m_placements) {
        const CMMNode* n = node_at_id(p.iid);
        if (!n) continue;
        const ScreenPt s = at[p.iid];
        if (s.x < -r * 4 || s.x > w + r * 4 || s.y < -r * 4 || s.y > h + r * 4) continue;

        const bool selected = m_sel.count(p.iid) > 0;
        Gdk::RGBA tint = node_tint(*n);
        const Gdk::RGBA ac = themed(m_area, "accent", "#5bc8af");

        // Selected nodes wear an obvious bright halo behind the glyph.
        if (selected) {
            cr->set_source_rgba(ac.get_red(), ac.get_green(), ac.get_blue(), 0.22);
            cr->arc(s.x, s.y, r + 9.0, 0.0, 2.0 * M_PI);
            cr->fill();
        }

        glyph_path(cr, p.glyph, s.x, s.y, r);
        cr->set_source_rgba(tint.get_red(), tint.get_green(), tint.get_blue(),
                            selected ? 0.34 : 0.20);
        cr->fill_preserve();
        cr->set_source_rgb(tint.get_red(), tint.get_green(), tint.get_blue());
        cr->set_line_width(selected ? 2.8 : 1.6);
        cr->stroke();

        // Bright accent ring on top — the unmistakable selection mark.
        if (selected) {
            cr->set_source_rgb(ac.get_red(), ac.get_green(), ac.get_blue());
            cr->set_line_width(2.0);
            cr->arc(s.x, s.y, r + 5.0, 0.0, 2.0 * M_PI);
            cr->stroke();
        }

        if (show_labels) {
            std::string title = n->title;
            if (title.empty())
                title = (n->kind == CMMNodeKind::Anchor) ? display_name(n->iid) : "Untitled";
            auto layout = m_area.create_pango_layout(title);
            Pango::FontDescription fd("sans 9"); layout->set_font_description(fd);
            layout->set_ellipsize(Pango::EllipsizeMode::END);
            layout->set_width(static_cast<int>(150 * Pango::SCALE));
            layout->set_alignment(Pango::Alignment::CENTER);
            int tw = 0, th = 0; layout->get_pixel_size(tw, th);
            const double dimv = (!m_hover_id.empty() && p.iid != m_hover_id) ? 0.5 : 1.0;
            cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), dimv);
            cr->move_to(s.x - tw / 2.0, s.y + r + 3.0);
            layout->show_in_cairo_context(cr);

            // The THOUGHT — a dimmer wrapped excerpt beneath the title, the prose
            // the author expounds under the label. Shown for a text node that has
            // a body and isn't collapsed, when there's room to read it. Capped to a
            // few lines (ellipsised); double-click opens the full editor.
            if (n->kind == CMMNodeKind::Text && !n->collapsed &&
                !n->body.empty() && m_vp.zoom > 0.55) {
                auto bl = m_area.create_pango_layout(n->body);
                Pango::FontDescription bfd("sans 8"); bl->set_font_description(bfd);
                bl->set_width(static_cast<int>(168 * Pango::SCALE));
                bl->set_wrap(Pango::WrapMode::WORD_CHAR);
                bl->set_ellipsize(Pango::EllipsizeMode::END);
                bl->set_height(-3);                       // up to 3 lines
                bl->set_alignment(Pango::Alignment::CENTER);
                int bw = 0, bh = 0; bl->get_pixel_size(bw, bh);
                (void)bh;
                cr->set_source_rgba(fg.get_red(), fg.get_green(), fg.get_blue(), dimv * 0.62);
                cr->move_to(s.x - bw / 2.0, s.y + r + 3.0 + th + 2.0);
                bl->show_in_cairo_context(cr);
            }
        }
    }

    // ── Marquee rubber-band (the selection rectangle) ─────────────────────────
    if (m_marquee) {
        const double x = std::min(m_marq_x0, m_marq_x1), y = std::min(m_marq_y0, m_marq_y1);
        const double rw = std::abs(m_marq_x1 - m_marq_x0), rh = std::abs(m_marq_y1 - m_marq_y0);
        const Gdk::RGBA ac = themed(m_area, "accent", "#5bc8af");
        cr->set_source_rgba(ac.get_red(), ac.get_green(), ac.get_blue(), 0.12);
        cr->rectangle(x, y, rw, rh); cr->fill();
        cr->set_source_rgba(ac.get_red(), ac.get_green(), ac.get_blue(), 0.7);
        cr->set_line_width(1.0);
        cr->rectangle(x + 0.5, y + 0.5, rw - 1, rh - 1); cr->stroke();
    }
}

// ── Glyph shapes (the small shared vocabulary) ────────────────────────────────
void CustomMindMapCanvas::glyph_path(const Cairo::RefPtr<Cairo::Context>& cr,
                                     MapGlyph g, double cx, double cy, double r) {
    cr->begin_new_sub_path();
    switch (g) {
        case MapGlyph::Circle:
            cr->arc(cx, cy, r, 0.0, 2.0 * M_PI); cr->close_path(); break;
        case MapGlyph::Pin:
            cr->arc(cx, cy - r * 0.15, r * 0.85, 0.0, 2.0 * M_PI); cr->close_path();
            cr->move_to(cx - r * 0.45, cy + r * 0.45);
            cr->line_to(cx,            cy + r * 1.05);
            cr->line_to(cx + r * 0.45, cy + r * 0.45);
            cr->close_path(); break;
        case MapGlyph::Clip: {
            const double f = r * 0.5;
            cr->move_to(cx - r, cy - r);
            cr->line_to(cx + r - f, cy - r);
            cr->line_to(cx + r, cy - r + f);
            cr->line_to(cx + r, cy + r);
            cr->line_to(cx - r, cy + r);
            cr->close_path(); break;
        }
        case MapGlyph::Thumb:
        case MapGlyph::Card:
        case MapGlyph::Square:
        default: {
            const double rad = (g == MapGlyph::Square) ? r * 0.18 : r * 0.32;
            const double x = cx - r, y = cy - r, wdt = 2 * r, hgt = 2 * r;
            cr->arc(x + wdt - rad, y + rad,       rad, -M_PI / 2, 0.0);
            cr->arc(x + wdt - rad, y + hgt - rad, rad, 0.0,       M_PI / 2);
            cr->arc(x + rad,       y + hgt - rad, rad, M_PI / 2,  M_PI);
            cr->arc(x + rad,       y + rad,       rad, M_PI,      3 * M_PI / 2);
            cr->close_path(); break;
        }
    }
}

// ── Popover: pick a project object (Anchor target OR a subject) ───────────────
void CustomMindMapCanvas::open_object_picker(bool for_subject) {
    if (!m_objects) return;
    std::vector<ObjOption> opts = m_objects();

    auto* pop = Gtk::make_managed<Gtk::Popover>();
    pop->set_parent(for_subject ? static_cast<Gtk::Widget&>(m_subject_add)
                                : static_cast<Gtk::Widget&>(m_t_anchor));
    pop->set_autohide(true);

    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    box->set_margin(8);
    auto* search = Gtk::make_managed<Gtk::SearchEntry>();
    search->set_placeholder_text(for_subject ? "Subject\u2026" : "Anchor an object\u2026");
    box->append(*search);

    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroll->set_min_content_height(220);
    scroll->set_min_content_width(240);
    auto* list = Gtk::make_managed<Gtk::ListBox>();
    list->set_selection_mode(Gtk::SelectionMode::NONE);
    scroll->set_child(*list);
    box->append(*scroll);
    pop->set_child(*box);

    // Build rows; a tiny case-insensitive filter on the search text. With no query
    // the list is GROUPED — a non-selectable header per section, members indented
    // by tree depth (Part ▸ Chapter ▸ Scene; Group ▸ member). While filtering, the
    // headers drop away to a flat hit list. The iid is stashed in the row name (the
    // proven link-picker idiom); header rows carry no name and aren't activatable.
    auto populate = [this, list, for_subject, opts](const std::string& q) {
        Gtk::Widget* c = list->get_first_child();
        while (c) { Gtk::Widget* nx = c->get_next_sibling(); list->remove(*c); c = nx; }
        std::string lo = q; std::transform(lo.begin(), lo.end(), lo.begin(), ::tolower);
        const bool filtering = !lo.empty();
        std::string cur_group;
        for (const ObjOption& o : opts) {
            if (for_subject && cmm_has_subject(m_doc, o.iid)) continue;
            std::string nl = o.name; std::transform(nl.begin(), nl.end(), nl.begin(), ::tolower);
            if (filtering && nl.find(lo) == std::string::npos) continue;

            if (!filtering && o.group != cur_group) {       // section header
                cur_group = o.group;
                auto* hrow = Gtk::make_managed<Gtk::ListBoxRow>();
                hrow->set_activatable(false);
                hrow->set_selectable(false);
                auto* hl = Gtk::make_managed<Gtk::Label>(cur_group);
                hl->add_css_class("dim-label");
                hl->set_xalign(0.0); hl->set_margin_top(6);
                hl->set_margin_start(6); hl->set_margin_bottom(2);
                hrow->set_child(*hl);
                list->append(*hrow);
            }

            auto* row = Gtk::make_managed<Gtk::ListBoxRow>();
            auto* lab = Gtk::make_managed<Gtk::Label>(o.name.empty() ? o.iid : o.name);
            lab->set_xalign(0.0);
            lab->set_margin_top(4); lab->set_margin_bottom(4); lab->set_margin_end(6);
            lab->set_margin_start(6 + (filtering ? 0 : o.depth * 14));   // indent the hierarchy
            row->set_child(*lab);
            row->set_name(o.iid);
            list->append(*row);
        }
    };
    populate("");
    search->signal_search_changed().connect([search, populate]() { populate(search->get_text()); });

    list->signal_row_activated().connect([this, for_subject, pop, opts](Gtk::ListBoxRow* row) {
        if (!row) return;
        const std::string iid = row->get_name();
        if (iid.empty()) return;                 // a section header, not a choice
        std::string name = iid;
        for (const ObjOption& o : opts) if (o.iid == iid) { name = o.name; break; }
        pop->popdown();
        if (for_subject) {
            if (cmm_add_subject(m_doc, iid)) { rebuild_chips(); persist(); }
        } else {
            m_pending_anchor_iid = iid; m_pending_anchor_name = name;
            set_tool(Tool::PlaceAnchor);    // next canvas click drops the anchor
        }
    });

    pop->signal_closed().connect([pop]() { pop->unparent(); });
    pop->popup();
    search->grab_focus();
}

// ── Popover: edit a text node's TITLE and its THOUGHT (body) ──────────────────
// A text node is a title (the label) plus an optional thought (the prose you
// expound under it). The title is the at-a-glance handle; the body is where the
// idea actually lives. Both round-trip in the model; this is where they're authored.
void CustomMindMapCanvas::open_text_editor(const std::string& node_id, double sx, double sy) {
    const CMMNode* n = node_at_id(node_id);
    if (!n) return;
    auto* pop = Gtk::make_managed<Gtk::Popover>();
    pop->set_parent(m_area);
    Gdk::Rectangle rect((int)sx, (int)sy, 1, 1);
    pop->set_pointing_to(rect);
    pop->set_autohide(true);

    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    box->set_margin(8);

    auto* tl = Gtk::make_managed<Gtk::Label>("Title");
    tl->add_css_class("dim-label"); tl->set_xalign(0.0);
    box->append(*tl);
    auto* entry = Gtk::make_managed<Gtk::Entry>();
    entry->set_text(n->title);
    entry->set_width_chars(30);
    box->append(*entry);

    auto* bl = Gtk::make_managed<Gtk::Label>("Thought");
    bl->add_css_class("dim-label"); bl->set_xalign(0.0);
    box->append(*bl);
    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroll->set_min_content_height(110);
    scroll->set_min_content_width(300);
    scroll->add_css_class("frame");
    auto* view = Gtk::make_managed<Gtk::TextView>();
    view->set_wrap_mode(Gtk::WrapMode::WORD_CHAR);
    view->set_top_margin(6); view->set_bottom_margin(6);
    view->set_left_margin(6); view->set_right_margin(6);
    view->get_buffer()->set_text(n->body);
    scroll->set_child(*view);
    box->append(*scroll);

    // Colour — a compact dropdown (condenses the window). "None" clears it (an
    // Anchor then inherits its target's label colour). Each row shows a swatch dot
    // + the palette name; choosing writes immediately so the change previews live.
    auto set_color = [this, node_id](int idx) {
        for (auto& nn : m_doc.nodes) if (nn.id == node_id) { nn.color_idx = idx; break; }
        recompute(); persist();
    };
    std::vector<Glib::ustring> cnames;
    cnames.push_back("No colour");
    for (int i = 1; i <= (int)m_prefs.tag_colors.size(); ++i)
        cnames.push_back(m_prefs.color_name_for_idx(i));
    auto cmodel = Gtk::StringList::create(cnames);

    auto cfactory = Gtk::SignalListItemFactory::create();
    cfactory->signal_setup().connect([](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* lab = Gtk::make_managed<Gtk::Label>();
        lab->set_xalign(0.0);
        item->set_child(*lab);
    });
    cfactory->signal_bind().connect([this](const Glib::RefPtr<Gtk::ListItem>& item) {
        auto* lab = dynamic_cast<Gtk::Label*>(item->get_child());
        if (!lab) return;
        const guint pos = item->get_position();
        if (pos == 0) { lab->set_text("No colour"); return; }
        const std::string hex  = m_prefs.color_hex_for_idx((int)pos);
        const std::string name = m_prefs.color_name_for_idx((int)pos);
        lab->set_markup("<span foreground='" + hex + "'>\u25CF</span>  " + name);
    });

    auto* crow = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto* cl = Gtk::make_managed<Gtk::Label>("Colour");
    cl->add_css_class("dim-label"); cl->set_xalign(0.0);
    crow->append(*cl);
    auto* cdrop = Gtk::make_managed<Gtk::DropDown>();
    cdrop->set_model(cmodel);
    cdrop->set_factory(cfactory);
    cdrop->set_hexpand(true);
    const int csz = (int)m_prefs.tag_colors.size();
    cdrop->set_selected((guint)std::clamp(n->color_idx, 0, csz));
    cdrop->property_selected().signal_changed().connect(
        [cdrop, set_color]() { set_color((int)cdrop->get_selected()); });
    crow->append(*cdrop);
    box->append(*crow);

    // Dismiss row — an explicit close (commit happens on dismiss either way).
    auto* footer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    footer->set_halign(Gtk::Align::END);
    footer->set_margin_top(2);
    auto* done = Gtk::make_managed<Gtk::Button>("Done");
    done->add_css_class("suggested-action");
    done->signal_clicked().connect([pop]() { pop->popdown(); });
    footer->append(*done);
    box->append(*footer);

    pop->set_child(*box);

    // Commit on dismiss (click-away / Esc) so any exit saves both fields. Read the
    // widgets here, write the owned node, then tear the popover down.
    pop->signal_closed().connect([this, node_id, entry, view, pop]() {
        const std::string title = entry->get_text();
        const std::string body  = std::string(view->get_buffer()->get_text());
        for (auto& nn : m_doc.nodes)
            if (nn.id == node_id) { nn.title = title; nn.body = body; break; }
        recompute(); persist();
        pop->unparent();
    });

    // Enter in the title hops down to the thought — newlines belong to the body.
    entry->signal_activate().connect([view]() { view->grab_focus(); });

    pop->popup();
    entry->grab_focus();
}

// ── Popover: choose a category for a new link ─────────────────────────────────
void CustomMindMapCanvas::open_category_popover(const std::string& from_id,
                                                const std::string& to_id,
                                                double sx, double sy) {
    auto* pop = Gtk::make_managed<Gtk::Popover>();
    pop->set_parent(m_area);
    Gdk::Rectangle rect((int)sx, (int)sy, 1, 1);
    pop->set_pointing_to(rect);
    pop->set_autohide(true);

    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
    box->set_margin(8);
    auto* lab = Gtk::make_managed<Gtk::Label>("Link kind");
    lab->add_css_class("dim-label"); lab->set_xalign(0.0);
    box->append(*lab);

    auto* entry = Gtk::make_managed<Gtk::Entry>();
    entry->set_placeholder_text("category (e.g. lore, is-a)\u2026");
    entry->set_width_chars(22);
    box->append(*entry);

    // An Anchor endpoint makes the edge gently directed toward truth.
    const CMMNode* nf = node_at_id(from_id);
    const CMMNode* nt = node_at_id(to_id);
    const bool to_is_anchor   = nt && nt->kind == CMMNodeKind::Anchor;
    const bool from_is_anchor = nf && nf->kind == CMMNodeKind::Anchor;

    auto add_edge = [this, from_id, to_id, to_is_anchor, from_is_anchor, pop]
                    (const std::string& cat) {
        std::string a = from_id, b = to_id;
        bool directed = false;
        if (to_is_anchor ^ from_is_anchor) {        // exactly one anchor → point at it
            directed = true;
            if (from_is_anchor) std::swap(a, b);    // orient toward the anchor
        }
        cmm_add_edge(m_doc, a, b, cat, directed);
        recompute(); persist();
        pop->popdown();
    };

    entry->signal_activate().connect([entry, add_edge]() { add_edge(entry->get_text()); });

    // Recent categories — one tap to reuse (the warm list the model keeps).
    if (!m_doc.recent_categories.empty()) {
        auto* recents = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
        auto* rl = Gtk::make_managed<Gtk::Label>("Recent");
        rl->add_css_class("dim-label"); rl->set_xalign(0.0);
        recents->append(*rl);
        auto* flow = Gtk::make_managed<Gtk::FlowBox>();
        flow->set_selection_mode(Gtk::SelectionMode::NONE);
        flow->set_max_children_per_line(3);
        for (const std::string& cat : m_doc.recent_categories) {
            auto* b = Gtk::make_managed<Gtk::Button>(cat);
            b->add_css_class("flat");
            b->signal_clicked().connect([add_edge, cat]() { add_edge(cat); });
            flow->append(*b);
        }
        recents->append(*flow);
        box->append(*recents);
    }

    auto* assoc = Gtk::make_managed<Gtk::Button>("Associative (no category)");
    assoc->add_css_class("flat");
    assoc->signal_clicked().connect([add_edge]() { add_edge(std::string()); });
    box->append(*assoc);

    pop->set_child(*box);
    pop->signal_closed().connect([pop]() { pop->unparent(); });
    pop->popup();
    entry->grab_focus();
}

// ── Popover: the Frame stamp menu ─────────────────────────────────────────────
void CustomMindMapCanvas::open_frame_menu() {
    if (!m_attached) return;
    auto* pop = Gtk::make_managed<Gtk::Popover>();
    pop->set_parent(m_t_frame);
    pop->set_autohide(true);

    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    box->set_margin(8);

    auto stamp = [this, pop](const MindMapFrame& frame) {
        // Centre the fan on the current view centre, in world units.
        const WorldPt c = screen_to_world(m_vp, m_area.get_width() / 2.0,
                                                 m_area.get_height() / 2.0);
        // One subject set → seed the Anchor centre on it; else a plain "What" node.
        std::string subj, label = "What";
        if (m_doc.subject_iids.size() == 1) {
            subj  = m_doc.subject_iids.front();
            label = display_name(subj);
        }
        cmm_stamp_frame(m_doc, frame, c.x, c.y, kFrameRing, subj, label);
        m_empty_hint.set_visible(false);
        rebuild_chips();              // a seeded subject may have been registered
        recompute(); persist();
        pop->popdown();
        m_fit_pending = true; m_area.queue_draw();
    };

    auto* five = Gtk::make_managed<Gtk::Button>("Five W\u2019s");
    five->add_css_class("flat");
    five->signal_clicked().connect([stamp]() { stamp(five_ws_frame()); });
    box->append(*five);

    auto* hint = Gtk::make_managed<Gtk::Label>(
        m_doc.subject_iids.size() == 1
            ? "Centres on this map\u2019s subject."
            : "Set one subject to centre on it.");
    hint->add_css_class("dim-label");
    hint->set_wrap(true); hint->set_max_width_chars(24); hint->set_xalign(0.0);
    box->append(*hint);

    pop->set_child(*box);
    pop->signal_closed().connect([pop]() { pop->unparent(); });
    pop->popup();
}

}  // namespace Folio
