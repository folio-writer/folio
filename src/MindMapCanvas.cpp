// ─────────────────────────────────────────────────────────────────────────────
// MindMapCanvas.cpp — the fourth lens, GTK side (s48 slice 1). See header.
//
// THIN by contract: the only maths here is asking the pure unit. reflow() places,
// world_to_screen() transforms, hit_test() picks. This file paints the result and
// forwards input. If a geometry decision appears to be needed here, it belongs in
// MindMap.cpp instead.
// ─────────────────────────────────────────────────────────────────────────────
#include "MindMapCanvas.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <tuple>
#include <unordered_map>

#include "DocumentModel.hpp"
#include "FolioPrefs.hpp"
#include "Iid.hpp"

namespace Folio {

namespace {
constexpr double kNodeR      = 16.0;   // world-radius of a node glyph at zoom 1
constexpr double kHitR       = 18.0;   // screen-px pick radius (stays clickable at any zoom)
constexpr double kZoomMin    = 0.10;
constexpr double kZoomMax    = 4.00;
constexpr double kFitMargin  = 64.0;   // px breathing room when framing all
constexpr double kGridWorld  = 80.0;   // dot-grid spacing in world units

// ── Synthetic hub nodes (the centre cloud) ───────────────────────────────────
// The map needs a centre the manuscript and the cast/world hang off, but the
// project has no single "Project" binder node — so the canvas MINTS hubs. Their
// iids start with \x01 (never a real iid: real ones are "scn_…", "chr_…", …), so
// they are easy to tell apart, are NOT navigable (clicking a hub opens nothing),
// and are pure layout scaffolding — never written back to the model.
const std::string kHubProject    = "\x01hub_project";
const std::string kHubCharacters = "\x01hub_characters";
const std::string kHubPlaces     = "\x01hub_places";
const std::string kHubReferences = "\x01hub_references";

inline bool is_hub(const std::string& iid) {
    return !iid.empty() && iid[0] == '\x01';
}

// Pull a palette colour, falling back to a sensible literal if the theme lacks it.
Gdk::RGBA themed(Gtk::Widget& w, const char* name, const char* fallback) {
    Gdk::RGBA c;
    if (w.get_style_context()->lookup_color(name, c)) return c;
    c.set(fallback);
    return c;
}

// A node's tint by kind (shape already carries kind; colour reinforces it).
const char* kind_color_token(IidKind k) {
    switch (k) {
        case IidKind::Scene:     return "accent";
        case IidKind::Character: return "col_mauve";
        case IidKind::Place:     return "col_peach";
        default:                 return "col_green";   // Reference / scrap
    }
}
}  // namespace

MindMapCanvas::MindMapCanvas(DocumentModel& model, FolioPrefs& prefs)
    : Gtk::Box(Gtk::Orientation::VERTICAL, 0), m_model(model), m_prefs(prefs) {
    set_hexpand(true);
    set_vexpand(true);

    // Default lens: the balloon (nested radial) layout — Project ▸ Part ▸ Chapter
    // ▸ Scene as clouds-within-clouds, the structural view. (lane.kind and the
    // hand-arranging free-flow become selectable once the rule-toggle UI lands; in
    // the balloon view the structure owns position, so drag/create settle back to
    // their cluster — arranging by hand is a free-flow activity.)
    m_rules.push_back({ "pos.balloon", RuleRole::Position, /*enabled=*/true, "" });

    // ── Drawing surface ──────────────────────────────────────────────────────
    m_area.set_hexpand(true);
    m_area.set_vexpand(true);
    m_area.set_can_focus(true);
    m_area.set_focusable(true);
    m_area.set_draw_func(sigc::mem_fun(*this, &MindMapCanvas::draw));
    m_area.signal_resize().connect([this](int w, int /*h*/) {
        if (m_fit_pending && w > 0 && !m_placements.empty()) {
            zoom_to_fit();
            m_fit_pending = false;
        }
    });

    m_overlay.set_child(m_area);
    m_overlay.set_hexpand(true);
    m_overlay.set_vexpand(true);

    // ── Zoom-to-fit button (the "I'm lost" recovery) ─────────────────────────
    m_fit_btn.set_label("\u2922");           // ⤢
    m_fit_btn.set_tooltip_text("Fit map to view");
    m_fit_btn.add_css_class("osd");
    m_fit_btn.set_halign(Gtk::Align::END);
    m_fit_btn.set_valign(Gtk::Align::START);
    m_fit_btn.set_margin_top(10);
    m_fit_btn.set_margin_end(10);
    m_fit_btn.set_can_focus(false);
    m_fit_btn.signal_clicked().connect([this] { zoom_to_fit(); m_area.queue_draw(); });
    m_overlay.add_overlay(m_fit_btn);

    // ── Empty-state hint ─────────────────────────────────────────────────────
    m_empty_hint.set_text("Nothing to map yet.\nScenes, characters, places and "
                          "references appear here as you write.");
    m_empty_hint.set_justify(Gtk::Justification::CENTER);
    m_empty_hint.add_css_class("board-placeholder");
    m_empty_hint.set_halign(Gtk::Align::CENTER);
    m_empty_hint.set_valign(Gtk::Align::CENTER);
    m_empty_hint.set_can_target(false);
    m_empty_hint.set_visible(false);
    m_overlay.add_overlay(m_empty_hint);

    append(m_overlay);

    // ── Pointer tracking + hover ─────────────────────────────────────────────
    auto motion = Gtk::EventControllerMotion::create();
    motion->signal_motion().connect([this](double x, double y) {
        m_ptr_x = x; m_ptr_y = y;
        if (m_panning || !m_drag_iid.empty()) return;     // don't hover mid-gesture
        const std::string hit = hit_test(m_placements, m_vp, x, y, kHitR);
        if (hit != m_hover_iid) { m_hover_iid = hit; m_area.queue_draw(); }
    });
    motion->signal_leave().connect([this]() {
        if (!m_hover_iid.empty()) { m_hover_iid.clear(); m_area.queue_draw(); }
    });
    m_area.add_controller(motion);

    // ── Keyboard (no mouse needed): +/- zoom, 0 fit, arrows pan, space = pan ─
    auto key = Gtk::EventControllerKey::create();
    key->signal_key_pressed().connect(
        [this](guint kv, guint, Gdk::ModifierType) {
            const double cx = m_area.get_width() / 2.0, cy = m_area.get_height() / 2.0;
            const double step = 70.0;
            switch (kv) {
                case GDK_KEY_space:                       m_space_held = true; return true;
                case GDK_KEY_plus: case GDK_KEY_equal:
                case GDK_KEY_KP_Add:                       zoom_about(1.15, cx, cy); return true;
                case GDK_KEY_minus: case GDK_KEY_underscore:
                case GDK_KEY_KP_Subtract:                  zoom_about(1.0 / 1.15, cx, cy); return true;
                case GDK_KEY_0: case GDK_KEY_KP_0:
                case GDK_KEY_f: case GDK_KEY_F:            zoom_to_fit(); m_area.queue_draw(); return true;
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
    m_area.add_controller(key);

    // Take keyboard focus when the map becomes visible, so the keys work without
    // a click first (important on a trackpad/keyboard-only setup).
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
                if (shift) m_vp.pan_x -= dy * step;   // shift → horizontal
                else     { m_vp.pan_x -= dx * step; m_vp.pan_y -= dy * step; }
                m_area.queue_draw();
            }
            return true;
        }, false);
    m_area.add_controller(scroll);

    // ── Middle-drag pan (always) ─────────────────────────────────────────────
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

    // ── Primary drag: grab a node to MOVE it, else pan the canvas ────────────
    // A drag only COMMITS after the finger has actually moved past a threshold —
    // so a click or a trackpad double-tap (which always jitters a pixel or two) is
    // never mistaken for a drag and suppressed. Below the threshold nothing moves
    // and the click passes through to open the node.
    auto drag = Gtk::GestureDrag::create();
    drag->set_button(GDK_BUTTON_PRIMARY);
    drag->signal_drag_begin().connect([this](double sx, double sy) {
        m_drag_sx = sx; m_drag_sy = sy;
        m_drag_committed = false;
        m_drag_iid.clear();
        m_panning = false;
    });
    drag->signal_drag_update().connect([this](double ox, double oy) {
        if (!m_drag_committed) {
            if (std::hypot(ox, oy) < 8.0) return;       // jitter, not a drag → let the click happen
            m_drag_committed = true;
            m_did_pan = true;                            // now it's a real drag → suppress the click
            const std::string hit = m_space_held ? std::string()
                                  : hit_test(m_placements, m_vp, m_drag_sx, m_drag_sy, kHitR);
            if (!hit.empty()) {                          // MOVE: lift + pin the grabbed node
                for (const auto& p : m_placements)
                    if (p.iid == hit) { m_move_base_wx = p.x; m_move_base_wy = p.y; break; }
                m_drag_iid = hit;
            } else {                                     // PAN
                m_panning = true;
                m_pan_base_x = m_vp.pan_x; m_pan_base_y = m_vp.pan_y;
            }
        }
        if (!m_drag_iid.empty()) {
            const double z = (m_vp.zoom != 0.0) ? m_vp.zoom : 1.0;
            const double wx = m_move_base_wx + ox / z;
            const double wy = m_move_base_wy + oy / z;
            for (auto& it : m_items)
                if (it.iid == m_drag_iid) { it.x = wx; it.y = wy; it.pinned = true; break; }
            recompute();
        } else if (m_panning) {
            m_vp.pan_x = m_pan_base_x + ox; m_vp.pan_y = m_pan_base_y + oy;
            m_area.queue_draw();
        }
    });
    drag->signal_drag_end().connect([this](double, double) {
        m_panning = false; m_drag_iid.clear(); m_drag_committed = false;
    });
    m_area.add_controller(drag);

    // ── Primary click → open a node; double-click empty → author a Reference ──
    auto click = Gtk::GestureClick::create();
    click->set_button(GDK_BUTTON_PRIMARY);
    click->signal_released().connect([this, click](int n_press, double x, double y) {
        m_area.grab_focus();                    // so space-pan keys reach us next
        if (m_did_pan) { m_did_pan = false; return; }   // a drag, not a click
        const bool ctrl =
            (click->get_current_event_state() & Gdk::ModifierType::CONTROL_MASK) != Gdk::ModifierType{};
        if (ctrl) {
            // Centre the clicked region in the view; Ctrl = zoom in, +Shift = zoom out.
            const bool shift =
                (click->get_current_event_state() & Gdk::ModifierType::SHIFT_MASK) != Gdk::ModifierType{};
            const WorldPt wp = screen_to_world(m_vp, x, y);
            m_vp.zoom = std::clamp(m_vp.zoom * (shift ? 1.0 / 1.5 : 1.5), kZoomMin, kZoomMax);
            m_vp.pan_x = m_area.get_width() / 2.0 - wp.x * m_vp.zoom;
            m_vp.pan_y = m_area.get_height() / 2.0 - wp.y * m_vp.zoom;
            m_area.queue_draw();
            return;
        }
        const std::string iid = hit_test(m_placements, m_vp, x, y, kHitR);
        if (!iid.empty()) {
            if (n_press >= 2 && !is_hub(iid) && m_on_open) m_on_open(iid);   // double-click opens
            return;
        }
        // Empty space: a double-click authors a Reference fragment THERE.
        if (n_press == 2 && m_on_create) {
            const WorldPt w = screen_to_world(m_vp, x, y);
            const std::string new_iid = m_on_create(w.x, w.y);
            if (!new_iid.empty()) {
                m_items.push_back(MindMapItem{ new_iid, w.x, w.y, /*pinned=*/true, false, "" });
                m_empty_hint.set_visible(false);
                recompute();
            }
        }
    });
    m_area.add_controller(click);
}

// ── Build the lens from the model (truth → projection) ───────────────────────
// Reconciling: a node already on the map KEEPS its authored position + pin (so
// hand-arrangement survives a view-switch within the session); a node new to the
// map enters at the origin; a node gone from the binder drops off. Every rebuild
// also re-reads CONTAINMENT (parent_iid) from the binder path, so the balloon
// layout always reflects the current tree. Full disk persistence of the layout is
// a later slice — this keeps placement sticky in-session.
void MindMapCanvas::rebuild() {
    std::unordered_map<std::string, MindMapItem> prev;
    prev.reserve(m_items.size());
    for (const MindMapItem& it : m_items) prev[it.iid] = it;

    const std::vector<DocumentModel::NodeRef> nodes = m_model.collect_all_nodes();

    // (section,path) → iid, to resolve each node's PARENT (the node one level up in
    // the same section). This is how containment reaches the pure balloon layout.
    auto key = [](int sec, const std::vector<int>& path) {
        std::string k = std::to_string(sec);
        for (int i : path) { k += ':'; k += std::to_string(i); }
        return k;
    };
    std::unordered_map<std::string, std::string> path_iid;
    path_iid.reserve(nodes.size());
    for (const DocumentModel::NodeRef& nr : nodes)
        if (nr.node) path_iid[key(static_cast<int>(nr.section), nr.path)] = nr.node->iid;

    auto included = [](IidKind k) {
        return k == IidKind::Scene || k == IidKind::Character || k == IidKind::Place ||
               k == IidKind::Reference || k == IidKind::Group;   // Group = Part/Chapter hubs
    };
    // A section's centre hub — the cloud its top-level nodes hang under.
    auto section_hub = [](Section s) -> std::string {
        switch (s) {
            case Section::Characters: return kHubCharacters;
            case Section::Places:     return kHubPlaces;
            case Section::References:  return kHubReferences;
            case Section::Manuscript: return kHubProject;   // Parts hang straight off the centre
            default:                  return std::string();
        }
    };

    std::vector<MindMapItem> next;
    bool used_chars = false, used_places = false, used_refs = false;
    for (const DocumentModel::NodeRef& nr : nodes) {
        if (!nr.node) continue;
        if (nr.section == Section::Templates) continue;     // schema, not story — off the map
        const std::string& iid = nr.node->iid;
        if (!included(iid_kind_of(iid))) continue;

        // Parent = the node one level up in the same section; a top-level node hangs
        // under its section's centre hub (so characters/places cluster in the middle
        // and Parts ring the Project hub).
        std::string parent;
        if (nr.path.size() > 1) {
            std::vector<int> pp(nr.path.begin(), nr.path.end() - 1);
            auto it = path_iid.find(key(static_cast<int>(nr.section), pp));
            if (it != path_iid.end()) parent = it->second;
        }
        if (parent.empty()) {
            parent = section_hub(nr.section);
            if (nr.section == Section::Characters) used_chars = true;
            else if (nr.section == Section::Places) used_places = true;
            else if (nr.section == Section::References) used_refs = true;
        }

        auto p = prev.find(iid);
        if (p != prev.end()) {
            MindMapItem m = p->second;       // keep authored layout/pin
            m.parent_iid = parent;           // refresh containment
            next.push_back(std::move(m));
        } else {
            next.push_back(MindMapItem{ iid, 0.0, 0.0, false, false, parent });
        }
    }

    // The centre cloud: the Project hub (single root, centred) owns the cast/world
    // hubs. Only emit a kind-hub when that section actually has nodes (no empty
    // clouds). Parts already parent straight to the Project hub above.
    next.push_back(MindMapItem{ kHubProject, 0.0, 0.0, false, false, "" });
    if (used_chars)  next.push_back(MindMapItem{ kHubCharacters, 0.0, 0.0, false, false, kHubProject });
    if (used_places) next.push_back(MindMapItem{ kHubPlaces,     0.0, 0.0, false, false, kHubProject });
    if (used_refs)   next.push_back(MindMapItem{ kHubReferences, 0.0, 0.0, false, false, kHubProject });

    m_items = std::move(next);

    // ── Effective colours: scene = its KP/label colour; chapter/part = the BLEND
    // (average) of its descendant scenes' colours; entity = its own label colour. ──
    m_color.clear();
    std::unordered_map<std::string, std::vector<std::string>> kids;
    for (const MindMapItem& it : m_items)
        if (!it.parent_iid.empty()) kids[it.parent_iid].push_back(it.iid);
    // own colours (scenes + characters/places/references with a label colour set)
    for (const MindMapItem& it : m_items) {
        if (is_hub(it.iid)) continue;
        if (const BinderNode* n = m_model.find_node_by_iid(it.iid)) {
            if (n->color_idx > 0) {
                const std::string hex = m_prefs.color_hex_for_idx(n->color_idx);
                if (!hex.empty()) { Gdk::RGBA c; c.set(hex); m_color[it.iid] = c; }
            }
        }
    }
    // blend: post-order average of descendant SCENE colours → assign to containers
    std::function<std::tuple<double, double, double, int>(const std::string&)> blend =
        [&](const std::string& id) -> std::tuple<double, double, double, int> {
            double r = 0, g = 0, b = 0; int n = 0;
            if (iid_kind_of(id) == IidKind::Scene) {
                auto c = m_color.find(id);
                if (c != m_color.end()) {
                    r += c->second.get_red(); g += c->second.get_green();
                    b += c->second.get_blue(); n = 1;
                }
            }
            auto it = kids.find(id);
            if (it != kids.end())
                for (const std::string& c : it->second) {
                    auto [rr, gg, bb, nn] = blend(c);
                    r += rr; g += gg; b += bb; n += nn;
                }
            // assign the blend to non-scene containers that actually gathered scenes
            if (n > 0 && !is_hub(id) && iid_kind_of(id) != IidKind::Scene) {
                Gdk::RGBA c; c.set_rgba(r / n, g / n, b / n, 1.0);
                m_color[id] = c;
            }
            return { r, g, b, n };
        };
    blend(kHubProject);   // single root: walks the whole tree

    // Descendant lists for cloud hulls (container → self + all descendants).
    m_descendants.clear();
    std::function<std::vector<std::string>(const std::string&)> collect =
        [&](const std::string& id) -> std::vector<std::string> {
            std::vector<std::string> out{ id };
            auto it = kids.find(id);
            if (it != kids.end()) {
                for (const std::string& c : it->second) {
                    std::vector<std::string> sub = collect(c);
                    out.insert(out.end(), sub.begin(), sub.end());
                }
                m_descendants[id] = out;   // only nodes that actually contain others
            }
            return out;
        };
    collect(kHubProject);

    // Edges are READ, never owned — fresh from the s20 link + s44 relation indices.
    m_edges = StoryGraph::edges_from_backlinks(m_model);

    bool any_real = false;                     // the Project hub always exists; ignore it
    for (const MindMapItem& it : m_items) if (!is_hub(it.iid)) { any_real = true; break; }
    m_empty_hint.set_visible(!any_real);
    m_fit_pending = true;                      // re-frame on next allocation/draw
    recompute();
}

void MindMapCanvas::recompute() {
    m_placements = MindMapLayout::reflow(m_items, m_rules, m_edges);
    m_area.queue_draw();
}

// ── Paint ────────────────────────────────────────────────────────────────────
void MindMapCanvas::draw(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
    // Deferred first-fit: if a rebuild happened before the area had a size.
    if (m_fit_pending && w > 0 && h > 0 && !m_placements.empty()) {
        zoom_to_fit();
        m_fit_pending = false;
    }

    const Gdk::RGBA bg  = themed(m_area, "adw_bg2", "#181825");
    const Gdk::RGBA fg  = themed(m_area, "tx1",     "#cdd6f4");
    const Gdk::RGBA dim = themed(m_area, "tx4",     "#5a5d75");

    // Background.
    cr->set_source_rgb(bg.get_red(), bg.get_green(), bg.get_blue());
    cr->paint();

    // Dot grid — gives the infinite-sheet feel and reads pan/zoom at a glance.
    const double gstep = kGridWorld * m_vp.zoom;
    if (gstep >= 14.0) {
        cr->set_source_rgba(dim.get_red(), dim.get_green(), dim.get_blue(), 0.35);
        const double x0 = std::fmod(m_vp.pan_x, gstep);
        const double y0 = std::fmod(m_vp.pan_y, gstep);
        for (double x = x0; x < w; x += gstep)
            for (double y = y0; y < h; y += gstep) {
                cr->arc(x, y, 1.0, 0.0, 2.0 * M_PI);
                cr->fill();
            }
    }

    // Screen positions for every node placement (for edge endpoints + drawing).
    std::unordered_map<std::string, ScreenPt> at;
    at.reserve(m_placements.size());
    for (const auto& p : m_placements)
        if (!p.field_chip && !p.iid.empty())
            at[p.iid] = world_to_screen(m_vp, p.x, p.y);

    // ── Cloud hulls: a soft translucent boundary around each container's whole
    // subtree, so a cluster reads as ONE cloud. Drawn outermost-first (more
    // descendants = bigger/under), tinted by the cluster's blend colour. This is
    // what turns spoked dots into nested clouds. ──
    {
        std::vector<const std::string*> hubs;
        hubs.reserve(m_descendants.size());
        for (const auto& kv : m_descendants) hubs.push_back(&kv.first);
        std::sort(hubs.begin(), hubs.end(), [this](const std::string* a, const std::string* b) {
            return m_descendants[*a].size() > m_descendants[*b].size();   // outer first
        });
        for (const std::string* idp : hubs) {
            const std::vector<std::string>& desc = m_descendants[*idp];
            double cx = 0, cy = 0; int n = 0;
            for (const std::string& d : desc) {
                auto f = at.find(d);
                if (f == at.end()) continue;
                cx += f->second.x; cy += f->second.y; ++n;
            }
            if (n < 2) continue;
            cx /= n; cy /= n;
            double rad = 0;
            for (const std::string& d : desc) {
                auto f = at.find(d);
                if (f == at.end()) continue;
                rad = std::max(rad, std::hypot(f->second.x - cx, f->second.y - cy));
            }
            rad += std::clamp(kNodeR * m_vp.zoom, 8.0, 30.0) + 16.0;   // clear the glyphs
            Gdk::RGBA col = themed(m_area, "tx3", "#9196b4");
            auto mc = m_color.find(*idp);
            if (mc != m_color.end()) col = mc->second;
            cr->set_source_rgba(col.get_red(), col.get_green(), col.get_blue(), 0.07);
            cr->arc(cx, cy, rad, 0.0, 2.0 * M_PI);
            cr->fill();
            cr->set_source_rgba(col.get_red(), col.get_green(), col.get_blue(), 0.22);
            cr->set_line_width(1.0);
            cr->arc(cx, cy, rad, 0.0, 2.0 * M_PI);
            cr->stroke();
        }
    }

    // ── Containment spokes (the clock hands): a soft line from each node to its
    // parent, UNDER the relationship edges and nodes. A scene→chapter spoke wears
    // the scene's KP colour; a chapter→part spoke the chapter's blend — so the
    // structure colour-threads from the leaves inward. ──
    const Gdk::RGBA spoke_fallback = themed(m_area, "tx4", "#5a5d75");
    cr->set_line_width(1.0);
    cr->unset_dash();
    for (const MindMapItem& it : m_items) {
        if (it.parent_iid.empty()) continue;
        auto cp = at.find(it.iid); auto pp = at.find(it.parent_iid);
        if (cp == at.end() || pp == at.end()) continue;
        Gdk::RGBA col = spoke_fallback;
        auto mc = m_color.find(it.iid);
        if (mc != m_color.end()) col = mc->second;
        cr->set_source_rgba(col.get_red(), col.get_green(), col.get_blue(), 0.45);
        cr->move_to(pp->second.x, pp->second.y);
        cr->line_to(cp->second.x, cp->second.y);
        cr->stroke();
    }

    // ── Edges (read, not owned): a line per typed edge; directed kinds tipped ──
    // Each line takes the colour of its OWNER — the entity end (character / place /
    // reference), else the source. So a character tinted orange paints every line
    // to the scenes it's in orange; the web reads by who owns each thread. Falls
    // back to a neutral tx3 when the owner has no label colour set.
    const Gdk::RGBA edge_c = themed(m_area, "tx3", "#9196b4");
    auto is_entity = [](IidKind k) {
        return k == IidKind::Character || k == IidKind::Place || k == IidKind::Reference;
    };
    cr->set_line_width(1.4);
    for (const StoryEdge& e : m_edges) {
        auto a = at.find(e.from_iid); auto b = at.find(e.to_iid);
        if (a == at.end() || b == at.end()) continue;       // endpoint not on map

        const bool fe = is_entity(iid_kind_of(e.from_iid));
        const bool te = is_entity(iid_kind_of(e.to_iid));
        const std::string& owner = (te && !fe) ? e.to_iid : e.from_iid;
        Gdk::RGBA col = edge_c;
        if (const BinderNode* n = m_model.find_node_by_iid(owner)) {
            const std::string hex = m_prefs.color_hex_for_idx(n->color_idx);
            if (!hex.empty()) col.set(hex);
        }

        // Soft, thin, gently BOWED — a whispering web under the structure, not
        // crossing slashes. On hover, the hovered node's threads brighten and the
        // rest of the web fades, so you read one node's connections cleanly.
        const bool hov     = !m_hover_iid.empty();
        const bool touches = hov && (e.from_iid == m_hover_iid || e.to_iid == m_hover_iid);
        double alpha;
        if (hov) alpha = touches ? 0.95 : 0.05;
        else     alpha = (e.kind == EdgeKind::Foreshadow) ? 0.6 : 0.32;
        cr->set_source_rgba(col.get_red(), col.get_green(), col.get_blue(), alpha);
        cr->set_line_width(touches ? 1.8 : 1.0);
        if (e.kind == EdgeKind::Foreshadow) {
            const double dash = 5.0; std::vector<double> d{dash, dash};
            cr->set_dash(d, 0.0);
        } else cr->unset_dash();
        const double ax = a->second.x, ay = a->second.y;
        const double bx = b->second.x, by = b->second.y;
        const double ex = bx - ax, ey = by - ay;
        const double elen = std::hypot(ex, ey);
        const double bow = elen * 0.14;
        const double mx = (ax + bx) / 2.0 - (elen > 1e-6 ? ey / elen : 0.0) * bow;
        const double my = (ay + by) / 2.0 + (elen > 1e-6 ? ex / elen : 0.0) * bow;
        cr->move_to(ax, ay);
        cr->curve_to(mx, my, mx, my, bx, by);          // quadratic-ish bow
        cr->stroke();

        // Arrowhead at the target for DIRECTED kinds (Reference is headless),
        // pointed along the curve's tangent (from the control point into the target).
        if (e.kind != EdgeKind::Reference) {
            const double dx = bx - mx, dy = by - my;
            const double len = std::hypot(dx, dy);
            if (len > 1.0) {
                const double ux = dx / len, uy = dy / len;
                const double r  = std::clamp(kNodeR * m_vp.zoom, 7.0, 30.0);
                const double tx = bx - ux * r, ty = by - uy * r;   // edge of target
                const double ah = 7.0, aw = 4.0;
                cr->unset_dash();
                cr->move_to(tx, ty);
                cr->line_to(tx - ux * ah + (-uy) * aw, ty - uy * ah + (ux) * aw);
                cr->line_to(tx - ux * ah - (-uy) * aw, ty - uy * ah - (ux) * aw);
                cr->close_path();
                cr->fill();
            }
        }
    }
    cr->unset_dash();

    // ── Nodes ────────────────────────────────────────────────────────────────
    const double r = std::clamp(kNodeR * m_vp.zoom, 7.0, 30.0);
    const bool   show_labels = r > 9.5;
    for (const auto& p : m_placements) {
        if (p.field_chip || p.iid.empty()) continue;
        const ScreenPt s = world_to_screen(m_vp, p.x, p.y);
        if (s.x < -r * 4 || s.x > w + r * 4 || s.y < -r * 4 || s.y > h + r * 4)
            continue;   // cull off-screen

        const IidKind  k = iid_kind_of(p.iid);
        Gdk::RGBA tint;
        auto cit = m_color.find(p.iid);
        if (cit != m_color.end()) tint = cit->second;          // KP / blend / label colour
        else tint = themed(m_area, is_hub(p.iid) ? "tx3" : kind_color_token(k), "#5bc8af");

        // Fill (tinted, soft) + stroke (full tint).
        glyph_path(cr, p.glyph, s.x, s.y, r);
        cr->set_source_rgba(tint.get_red(), tint.get_green(), tint.get_blue(), 0.22);
        cr->fill_preserve();
        cr->set_source_rgb(tint.get_red(), tint.get_green(), tint.get_blue());
        cr->set_line_width(1.6);
        cr->stroke();

        if (show_labels) {
            const std::string title = node_title(p.iid);
            auto layout = m_area.create_pango_layout(title);
            Pango::FontDescription fd("sans 9");
            layout->set_font_description(fd);
            layout->set_ellipsize(Pango::EllipsizeMode::END);
            layout->set_width(static_cast<int>(140 * Pango::SCALE));
            layout->set_alignment(Pango::Alignment::CENTER);
            int tw = 0, th = 0; layout->get_pixel_size(tw, th);
            cr->set_source_rgb(fg.get_red(), fg.get_green(), fg.get_blue());
            cr->move_to(s.x - tw / 2.0, s.y + r + 3.0);
            layout->show_in_cairo_context(cr);
        }
    }

    draw_hover_card(cr, w, h);   // floating metadata, on top of everything
}

// ── Hover metadata card — the floating read-out for the node under the cursor ─
// A small flat card anchored beside the hovered node: title, kind · status, the
// KP/label colour name, and the synopsis (wrapped). Hubs show a cluster summary.
// Read-only for now; editable-in-place is the next slice (it'll grow into a
// popover with the same fields). Painted last so it sits over the web and glyphs.
void MindMapCanvas::draw_hover_card(const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
    if (m_hover_iid.empty()) return;
    auto a = std::find_if(m_placements.begin(), m_placements.end(),
                          [&](const MindMapLayout::Placement& p) { return p.iid == m_hover_iid; });
    if (a == m_placements.end()) return;
    const ScreenPt s = world_to_screen(m_vp, a->x, a->y);

    // ── Gather the lines ──
    std::string title = node_title(m_hover_iid);
    std::string sub, accent, body;
    const IidKind k = iid_kind_of(m_hover_iid);
    if (is_hub(m_hover_iid)) {
        auto d = m_descendants.find(m_hover_iid);
        const int n = (d != m_descendants.end()) ? std::max(0, static_cast<int>(d->second.size()) - 1) : 0;
        sub = std::to_string(n) + (n == 1 ? " item" : " items");
    } else if (const BinderNode* nd = m_model.find_node_by_iid(m_hover_iid)) {
        const char* kind = (k == IidKind::Scene) ? "Scene"
                         : (k == IidKind::Character) ? "Character"
                         : (k == IidKind::Place) ? "Place"
                         : (k == IidKind::Reference) ? "Reference"
                         : (k == IidKind::Group) ? "Group" : "Node";
        const char* st = "";
        switch (nd->status) {
            case NodeStatus::RoughDraft: st = " · Rough draft"; break;
            case NodeStatus::InProgress: st = " · In progress"; break;
            case NodeStatus::Polished:   st = " · Polished";    break;
            case NodeStatus::Skip:       st = " · Skip";        break;
            default: break;
        }
        sub = std::string(kind) + st;
        if (nd->color_idx > 0) accent = m_prefs.color_name_for_idx(nd->color_idx);
        body = nd->synopsis;
    }

    // ── Build layouts (measure first) ──
    const double pad = 12.0, maxw = 240.0;
    auto mk = [&](const std::string& text, const char* font) {
        auto l = m_area.create_pango_layout(text);
        Pango::FontDescription fd(font); l->set_font_description(fd);
        l->set_width(static_cast<int>(maxw * Pango::SCALE));
        l->set_wrap(Pango::WrapMode::WORD_CHAR);
        l->set_ellipsize(Pango::EllipsizeMode::END);
        return l;
    };
    auto lt = mk(title, "sans bold 11");
    auto ls = sub.empty()    ? Glib::RefPtr<Pango::Layout>() : mk(sub, "sans 9");
    auto la = accent.empty() ? Glib::RefPtr<Pango::Layout>() : mk(accent, "sans 9");
    auto lb = body.empty()   ? Glib::RefPtr<Pango::Layout>() : mk(body, "sans 9");

    int tw, th, cw = 0, ch = 0;
    auto acc = [&](const Glib::RefPtr<Pango::Layout>& l, double gap) {
        if (!l) return; l->get_pixel_size(tw, th); cw = std::max(cw, tw); ch += th + (int)gap;
    };
    acc(lt, 4); acc(ls, 4); acc(la, 4); acc(lb, 0);
    const double cardw = std::min(maxw, (double)cw) + pad * 2;
    const double cardh = ch + pad * 2;

    // ── Anchor beside the node, flipping to stay on screen ──
    const double r = std::clamp(kNodeR * m_vp.zoom, 7.0, 30.0);
    double cx = s.x + r + 12.0, cy = s.y - cardh / 2.0;
    if (cx + cardw > w - 8) cx = s.x - r - 12.0 - cardw;     // flip left
    cy = std::clamp(cy, 8.0, std::max(8.0, h - cardh - 8.0));

    // ── Card surface ──
    const Gdk::RGBA bg = themed(m_area, "adw_surface", "#242436");
    const Gdk::RGBA br = themed(m_area, "accent_border", "#5bc8af");
    const Gdk::RGBA t1 = themed(m_area, "tx1", "#cdd6f4");
    const Gdk::RGBA t2 = themed(m_area, "tx3", "#9196b4");
    const double rad = 10.0;
    cr->begin_new_sub_path();
    cr->arc(cx + cardw - rad, cy + rad,         rad, -M_PI / 2, 0);
    cr->arc(cx + cardw - rad, cy + cardh - rad, rad, 0, M_PI / 2);
    cr->arc(cx + rad,         cy + cardh - rad, rad, M_PI / 2, M_PI);
    cr->arc(cx + rad,         cy + rad,         rad, M_PI, 3 * M_PI / 2);
    cr->close_path();
    cr->set_source_rgba(bg.get_red(), bg.get_green(), bg.get_blue(), 0.97);
    cr->fill_preserve();
    cr->set_source_rgba(br.get_red(), br.get_green(), br.get_blue(), 0.6);
    cr->set_line_width(1.0);
    cr->stroke();

    // ── Text lines ──
    double y = cy + pad;
    auto put = [&](const Glib::RefPtr<Pango::Layout>& l, const Gdk::RGBA& c, double gap) {
        if (!l) return;
        l->get_pixel_size(tw, th);
        cr->set_source_rgb(c.get_red(), c.get_green(), c.get_blue());
        cr->move_to(cx + pad, y);
        l->show_in_cairo_context(cr);
        y += th + gap;
    };
    put(lt, t1, 4);
    put(ls, t2, 4);
    if (la) {   // accent line gets a swatch dot
        auto cit = m_color.find(m_hover_iid);
        Gdk::RGBA sw = (cit != m_color.end()) ? cit->second : t2;
        la->get_pixel_size(tw, th);
        cr->set_source_rgb(sw.get_red(), sw.get_green(), sw.get_blue());
        cr->arc(cx + pad + 4, y + th / 2.0, 4, 0, 2 * M_PI); cr->fill();
        cr->move_to(cx + pad + 14, y);
        la->show_in_cairo_context(cr);
        y += th + 4;
    }
    put(lb, t2, 0);
}

// ── Glyph shapes — the small derived vocabulary (the Scapple lesson) ─────────
void MindMapCanvas::glyph_path(const Cairo::RefPtr<Cairo::Context>& cr,
                               MapGlyph g, double cx, double cy, double r) {
    cr->begin_new_sub_path();   // detach from any leftover current point (e.g. a prior label)
    switch (g) {
        case MapGlyph::Circle:                              // Character
            cr->arc(cx, cy, r, 0.0, 2.0 * M_PI);
            cr->close_path();
            break;
        case MapGlyph::Pin: {                               // Place — rounded teardrop
            cr->arc(cx, cy - r * 0.15, r * 0.85, 0.0, 2.0 * M_PI);
            cr->close_path();
            cr->move_to(cx - r * 0.45, cy + r * 0.45);
            cr->line_to(cx,            cy + r * 1.05);
            cr->line_to(cx + r * 0.45, cy + r * 0.45);
            cr->close_path();
            break;
        }
        case MapGlyph::Clip: {                              // Reference source — folded corner
            const double f = r * 0.5;
            cr->move_to(cx - r, cy - r);
            cr->line_to(cx + r - f, cy - r);
            cr->line_to(cx + r, cy - r + f);
            cr->line_to(cx + r, cy + r);
            cr->line_to(cx - r, cy + r);
            cr->close_path();
            break;
        }
        case MapGlyph::Thumb:                               // Image — frame
        case MapGlyph::Card:                                // Note / loose scrap
        case MapGlyph::Square:                              // Scene
        default: {
            const double rad = (g == MapGlyph::Square) ? r * 0.18 : r * 0.32;
            const double x = cx - r, y = cy - r, wdt = 2 * r, hgt = 2 * r;
            cr->arc(x + wdt - rad, y + rad,       rad, -M_PI / 2, 0.0);
            cr->arc(x + wdt - rad, y + hgt - rad, rad, 0.0,       M_PI / 2);
            cr->arc(x + rad,       y + hgt - rad, rad, M_PI / 2,  M_PI);
            cr->arc(x + rad,       y + rad,       rad, M_PI,      3 * M_PI / 2);
            cr->close_path();
            break;
        }
    }
}

// ── Viewport helpers (transforms come from the pure unit) ────────────────────
void MindMapCanvas::zoom_about(double factor, double sx, double sy) {
    const WorldPt before = screen_to_world(m_vp, sx, sy);
    m_vp.zoom = std::clamp(m_vp.zoom * factor, kZoomMin, kZoomMax);
    // Re-pin: keep the same world point under the cursor (screen = world*zoom+pan).
    m_vp.pan_x = sx - before.x * m_vp.zoom;
    m_vp.pan_y = sy - before.y * m_vp.zoom;
    m_area.queue_draw();
}

void MindMapCanvas::zoom_to_fit() {
    // World bounding box over node placements (chips excluded).
    bool any = false;
    double minx = 0, miny = 0, maxx = 0, maxy = 0;
    for (const auto& p : m_placements) {
        if (p.field_chip || p.iid.empty()) continue;
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
    m_vp.pan_x = w / 2.0 - cx * zoom;     // centre the bbox in the widget
    m_vp.pan_y = h / 2.0 - cy * zoom;
    m_area.queue_draw();
}

std::string MindMapCanvas::node_title(const std::string& iid) const {
    if (iid == kHubProject)
        return m_model.project_title.empty() ? std::string("Project") : m_model.project_title;
    if (iid == kHubCharacters) return "Characters";
    if (iid == kHubPlaces)     return "Places";
    if (iid == kHubReferences) return "References";
    if (const BinderNode* n = m_model.find_node_by_iid(iid))
        return n->title.empty() ? iid : n->title;
    return iid;
}

}  // namespace Folio
