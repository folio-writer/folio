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

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <iterator>

namespace Folio {

FocusWindow::FocusWindow(DocumentModel& model, FolioPrefs& prefs, Editor& editor)
    : m_model(model), m_prefs(prefs), m_editor(editor) {
    set_name("focus-window");
    set_title("Focus");
    set_default_size(1100, 800);

    build_view();
    build_control_bar();
    build_switcher();
    wire_keys();

    set_child(m_overlay);

    // One-time window signals (NOT in present_focus — that would re-connect on
    // every reopen). On map, (re)apply geometry now that a real width exists; on
    // close, flush through the editor and hide (the owning unique_ptr keeps us
    // alive for reuse).
    signal_map().connect([this]() {
        apply_focus_geometry();
        apply_typewriter_padding();
        m_view.grab_focus();
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
}

FocusWindow::~FocusWindow() = default;

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
    m_overlay.set_child(m_scroll);

    // Reapply on viewport size change. With horizontal policy NEVER the
    // hadjustment page_size == the scroll window's allocated width, which does
    // NOT change when we set inner view margins, so this cannot recurse.
    if (auto h = m_scroll.get_hadjustment())
        h->signal_changed().connect([this]() { apply_focus_geometry(); });
    if (auto v = m_scroll.get_vadjustment())
        v->signal_changed().connect([this]() { apply_typewriter_padding(); });
}

void FocusWindow::build_control_bar() {
    // Hover-reveal bar at the bottom: width %, zoom, font size, line spacing.
    // Mirrors the old in-editor bar, but every callback drives THIS view only.
    auto* bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 10);
    bar->set_name("focus-control-bar");
    bar->add_css_class("focus-width-bar");
    bar->set_halign(Gtk::Align::CENTER);
    bar->set_valign(Gtk::Align::END);
    bar->set_margin_bottom(16);

    // Page width (% of focus viewport)
    auto* w_lbl = Gtk::make_managed<Gtk::Label>("Width");
    w_lbl->add_css_class("stat-label");
    auto w_adj = Gtk::Adjustment::create(
        std::clamp(m_prefs.focus_page_width_pct, 15, 100), 15.0, 100.0, 1.0, 5.0, 0.0);
    auto* w_scale = Gtk::make_managed<Gtk::Scale>(w_adj, Gtk::Orientation::HORIZONTAL);
    w_scale->set_name("focus-width-scale");
    w_scale->set_size_request(140, -1);
    w_scale->set_draw_value(false);
    w_adj->signal_value_changed().connect([this, w_adj]() {
        m_prefs.focus_page_width_pct =
            std::clamp((int)std::round(w_adj->get_value()), 15, 100);
        apply_focus_geometry();
        try { m_prefs.save(); } catch (...) {}
    });

    auto* sep1 = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL);

    // Font size (pt) — focus's OWN size. There is no per-view body size, so while
    // focus is open we drive the shared base tag at zoom 1.0 (literal size); the
    // editor is occluded behind us and is restored to its snapshot on exit. The
    // value persists in its own pref so focus remembers it across sessions,
    // independent of the editor's size.
    auto* sz_lbl = Gtk::make_managed<Gtk::Label>("Size");
    sz_lbl->add_css_class("stat-label");
    int init_sz = m_prefs.focus_font_size > 0 ? m_prefs.focus_font_size
                                              : m_prefs.editor_font_size;
    m_size_adj = Gtk::Adjustment::create(
        std::clamp(init_sz, 6, 72), 6.0, 72.0, 1.0, 2.0, 0.0);
    auto* sz_scale = Gtk::make_managed<Gtk::Scale>(m_size_adj, Gtk::Orientation::HORIZONTAL);
    sz_scale->set_name("focus-size-scale");
    sz_scale->set_size_request(120, -1);
    sz_scale->set_draw_value(false);
    m_size_adj->signal_value_changed().connect([this]() {
        int v = std::clamp((int)std::round(m_size_adj->get_value()), 6, 72);
        m_prefs.focus_font_size = v;
        m_editor.set_body_display(v, 1.0);   // focus size, zoom neutralised
        try { m_prefs.save(); } catch (...) {}
    });

    auto* sep2 = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL);

    // Line spacing (multiplier → view-level pixels). NOT a buffer tag, so it
    // does not touch the editor's view.
    auto* ls_lbl = Gtk::make_managed<Gtk::Label>("Spacing");
    ls_lbl->add_css_class("stat-label");
    double init_ls = m_prefs.focus_line_spacing > 0.0 ? m_prefs.focus_line_spacing
                                                       : m_prefs.line_spacing;
    auto ls_adj = Gtk::Adjustment::create(std::clamp(init_ls, 0.5, 4.0),
                                          0.5, 4.0, 0.1, 0.5, 0.0);
    auto* ls_scale = Gtk::make_managed<Gtk::Scale>(ls_adj, Gtk::Orientation::HORIZONTAL);
    ls_scale->set_name("focus-spacing-scale");
    ls_scale->set_size_request(120, -1);
    ls_scale->set_draw_value(false);
    ls_adj->signal_value_changed().connect([this, ls_adj]() {
        m_prefs.focus_line_spacing =
            std::round(std::clamp(ls_adj->get_value(), 0.5, 4.0) * 10.0) / 10.0;
        apply_focus_look();
        try { m_prefs.save(); } catch (...) {}
    });

    bar->append(*w_lbl);  bar->append(*w_scale);  bar->append(*sep1);
    bar->append(*sz_lbl); bar->append(*sz_scale); bar->append(*sep2);
    bar->append(*ls_lbl); bar->append(*ls_scale);

    m_control_bar = bar;
    m_overlay.add_overlay(*bar);
    bar->set_visible(false);

    // Hover-reveal: show when the pointer nears the bottom of the viewport.
    auto motion = Gtk::EventControllerMotion::create();
    motion->signal_motion().connect([this](double, double y) {
        double h = m_overlay.get_height();
        if (m_control_bar)
            m_control_bar->set_visible(h > 0 && y >= h * 0.85);
    });
    m_overlay.add_controller(motion);

    // Exit affordance, top-right.
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
                if (m_switcher && m_switcher->get_visible()) {
                    m_switcher->set_visible(false);
                    m_view.grab_focus();
                } else {
                    close();
                }
                return true;
            }
            if (ctrl && (keyval == GDK_KEY_p || keyval == GDK_KEY_P)) {
                open_switcher();
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
    m_view.set_left_margin(side);
    m_view.set_right_margin(side);
}

void FocusWindow::apply_typewriter_padding() {
    if (m_prefs.focus_typewriter_mode) {
        int h = m_scroll.get_height();
        int half = h > 0 ? h / 2 : 320;
        m_view.set_top_margin(half);
        m_view.set_bottom_margin(half);
    } else {
        m_view.set_top_margin(24);
        m_view.set_bottom_margin(96);
    }
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
    LOG_DEBUG("focus goto {} ({})", node->iid,
              node->title.empty() ? "(untitled)" : node->title);
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

// ── Open ─────────────────────────────────────────────────────────────────────
void FocusWindow::present_focus(BinderNode* start) {
    rebuild_scene_list();
    BinderNode* node = start ? start : m_editor.current_node();
    if (!node && !m_scenes.empty()) node = m_scenes.front();
    m_current = node;
    if (node)
        set_title("Focus — " + (node->title.empty() ? std::string("(untitled)")
                                                     : node->title));

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
