// ─────────────────────────────────────────────────────────────────────────────
// Folio — PatternDialog.cpp   (s24 — Layer 3)   GTK glue over the pure planner.
// ─────────────────────────────────────────────────────────────────────────────
#include "PatternDialog.hpp"
#include "ModulePlanner.hpp"   // ModulePlanner::plan — the live preview engine

#include <algorithm>
#include <sstream>
#include <unordered_map>

namespace Folio {

namespace {
// s30 — pacing pattern text ↔ levels. Levels are 0..1; the field shows and reads
// percentages ("30, 30, 62, 100"). Tolerant: splits on commas or whitespace,
// accepts either 0..1 or 0..100 per token (>1.5 ⇒ treated as a percentage).
std::string format_levels(const std::vector<double>& lv) {
    std::string s;
    for (size_t i = 0; i < lv.size(); ++i) {
        if (i) s += ", ";
        s += std::to_string((int)std::lround(std::clamp(lv[i], 0.0, 1.0) * 100.0));
    }
    return s;
}

std::vector<double> parse_levels(const std::string& text) {
    std::vector<double> out;
    std::string tok;
    auto flush = [&] {
        if (tok.empty()) return;
        try {
            double v = std::stod(tok);
            if (v > 1.5) v /= 100.0;            // a percentage
            out.push_back(std::clamp(v, 0.0, 1.0));
        } catch (...) { /* skip garbage token */ }
        tok.clear();
    };
    for (char c : text) {
        if (c == ',' || c == ' ' || c == '\t' || c == ';') flush();
        else tok += c;
    }
    flush();
    return out;
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

PatternDialog::PatternDialog(Gtk::Window& parent)
    : Gtk::Window()
{
    m_module = built_in_folio_keypoints();   // the SHELL (id/name/top/pacing/deploy)
    for (const auto& act : m_module.craft.acts)   // flatten the built-in arc into
        for (const auto& k : act.kps) m_arc.push_back(k);   // the editable list

    set_transient_for(parent);
    set_modal(true);
    set_title("New from Pattern");
    set_default_size(720, -1);
    set_resizable(true);
    set_name("pattern-dialog");

    // Escape closes (no scaffold).
    auto kc = Gtk::EventControllerKey::create();
    kc->signal_key_pressed().connect(
        [this](guint kv, guint, Gdk::ModifierType) -> bool {
            if (kv == GDK_KEY_Escape) { close(); return true; }
            return false;
        }, false);
    add_controller(kc);

    // ── CSS ───────────────────────────────────────────────────────────────────
    auto css = Gtk::CssProvider::create();
    css->load_from_data(R"(
        .pat-section-title {
            font-size: 11px; font-weight: 700; letter-spacing: 0.06em;
            color: alpha(currentColor, 0.5); text-transform: uppercase;
            padding: 10px 0 2px 0;
        }
        .pat-row-label {
            font-size: 13px;
            color: alpha(currentColor, 0.85);
        }
        .pat-preview {
            font-size: 12px;
            color: alpha(currentColor, 0.6);
            padding: 4px 0 2px 0;
        }
        .pat-part-row {
            padding: 2px 0;
        }
        .pat-kp-row {
            padding: 4px 2px;
            border-bottom: 1px solid alpha(currentColor, 0.06);
        }
        .pat-kp-ord {
            font-size: 12px; font-weight: 700;
            color: alpha(currentColor, 0.45);
        }
        .pat-kp-name entry, .pat-kp-name {
            font-size: 13px;
        }
        .pat-kp-mean entry, .pat-kp-mean {
            font-size: 11px;
            color: alpha(currentColor, 0.6);
        }
        .pat-footer {
            padding: 12px 0 2px 0;
            border-top: 1px solid alpha(currentColor, 0.10);
            margin-top: 6px;
        }
    )");
    get_style_context()->add_provider_for_display(
        get_display(), css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);

    // ── Sections (each build_* appends to the page set in m_dest) ─────────────
    m_dest = &m_board_host;     build_board();
    m_dest = &m_page_kp;        build_keypoints();
    m_dest = &m_page_length;    build_length();
                                build_chapters();
                                build_pacing();
    m_dest = &m_page_structure; build_structure();
                                build_bookends();

    // Page margins (the board + footer carry their own).
    for (Gtk::Box* p : { &m_page_kp, &m_page_length, &m_page_structure }) {
        p->set_margin_start(20);
        p->set_margin_end(20);
        p->set_margin_top(10);
        p->set_margin_bottom(6);
    }
    m_board_host.set_margin_start(20);
    m_board_host.set_margin_end(20);
    m_board_host.set_margin_top(12);

    // Tabs.
    m_stack.set_name("pattern-stack");
    m_stack.set_transition_type(Gtk::StackTransitionType::CROSSFADE);
    m_stack.set_transition_duration(100);
    m_stack.add(m_page_kp,        "kp",        "Key Points");
    m_stack.add(m_page_length,    "length",    "Length");
    m_stack.add(m_page_structure, "structure", "Structure");
    m_switcher.set_name("pattern-switcher");
    m_switcher.set_stack(m_stack);
    m_switcher.set_halign(Gtk::Align::CENTER);
    m_switcher.set_margin_top(8);

    // Assemble: pattern picker · board · tabs · stack · footer (footer last).
    build_pattern_row();          // appends the pattern row to m_root first
    m_root.append(m_board_host);
    m_root.append(m_switcher);
    m_root.append(m_stack);
    build_footer();            // appends m_footer to m_root LAST → bottom
    set_child(m_root);

    refresh_pattern_dropdown("folio_keypoints");   // seed + list + select default
    update_preview();
}

// ─────────────────────────────────────────────────────────────────────────────
// Build helpers (mirror ImportDialog's make_section / make_row)
// ─────────────────────────────────────────────────────────────────────────────

Gtk::Widget* PatternDialog::make_section(const std::string& title) {
    auto* lbl = Gtk::make_managed<Gtk::Label>(title);
    lbl->set_halign(Gtk::Align::START);
    lbl->add_css_class("pat-section-title");
    return lbl;
}

Gtk::Widget* PatternDialog::make_row(const std::string& label, Gtk::Widget& w) {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto* lbl = Gtk::make_managed<Gtk::Label>(label);
    lbl->set_halign(Gtk::Align::START);
    lbl->set_hexpand(true);
    lbl->add_css_class("pat-row-label");
    box->append(*lbl);
    box->append(w);
    return box;
}

// ── Board (s28 — the read-only mixing-board render) ──────────────────────────

void PatternDialog::build_board() {
    m_dest->append(*make_section("Arc"));
    m_board.set_module(build_module());   // render the (editable) arc
    m_board.set_margin_top(4);
    m_board.set_margin_bottom(4);
    // s29 — fader drags write straight back into the authoritative arc so the
    // edits survive the next structural refresh (build_module rebuilds from m_arc).
    m_board.signal_value_changed().connect(
        [this](int idx, bool is_arc, double v) {
            if (idx < 0 || idx >= static_cast<int>(m_arc.size())) return;
            if (is_arc) m_arc[idx].arc = v;
            else        m_arc[idx].frenetic = v;
        });
    // s29 — divider drag transfers weight (scenes per KP) between the nearest
    // unpinned neighbours; pull the whole arc's weights back from the board, then
    // re-plan so the per-KP scene counts update.
    m_board.signal_weight_changed().connect([this] {
        const Module& bm = m_board.module();
        size_t i = 0;
        for (const auto& act : bm.craft.acts)
            for (const auto& k : act.kps) {
                if (i < m_arc.size()) m_arc[i].weight = k.weight;
                ++i;
            }
        update_preview();   // recomputes the plan and pushes counts to the board
    });
    m_dest->append(m_board);
}

// ── Pattern library (s30) ────────────────────────────────────────────────────
//
// A dropdown of saved patterns at the top of the dialog. Seeded with the
// built-ins; picking one loads its whole arc into the editor; "Save as…" banks
// the current arc as a new pattern (the author's genres accrue here). The
// library is the global stamp (~/.local/share/folio/modules); the bundle copy
// that travels with a book is written separately on Create.

void PatternDialog::build_pattern_row() {
    auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    row->set_name("pattern-library-row");
    row->set_margin_start(20);
    row->set_margin_end(20);
    row->set_margin_top(10);

    auto* lbl = Gtk::make_managed<Gtk::Label>("Pattern");
    lbl->add_css_class("dim-label");
    lbl->set_valign(Gtk::Align::CENTER);
    row->append(*lbl);

    auto empty = Gtk::StringList::create({});
    m_pattern_dd = Gtk::make_managed<Gtk::DropDown>(
        empty, Glib::RefPtr<Gtk::Expression<Glib::ustring>>{});
    m_pattern_dd->set_name("pattern-library-dd");
    m_pattern_dd->set_hexpand(true);
    m_pattern_dd->set_tooltip_text(
        "Choose a saved pattern (genre) to load its arc into the editor.");
    m_pattern_dd->property_selected().signal_changed().connect(
        [this]{ on_pattern_selected(); });
    row->append(*m_pattern_dd);

    m_btn_save_pattern.set_label("Save as…");
    m_btn_save_pattern.add_css_class("flat");
    m_btn_save_pattern.set_valign(Gtk::Align::CENTER);
    m_btn_save_pattern.set_tooltip_text(
        "Save the current arc as a new pattern you can reuse across books.");
    m_btn_save_pattern.signal_clicked().connect([this]{
        if (!m_save_pop) {                       // build once, reuse (persistent)
            m_save_pop = Gtk::make_managed<Gtk::Popover>();
            m_save_pop->set_parent(m_btn_save_pattern);
            m_save_pop->set_autohide(true);
            auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 6);
            box->set_margin_top(8); box->set_margin_bottom(8);
            box->set_margin_start(8); box->set_margin_end(8);
            auto* cap = Gtk::make_managed<Gtk::Label>("Save pattern as");
            cap->add_css_class("dim-label");
            cap->set_halign(Gtk::Align::START);
            box->append(*cap);
            m_save_name = Gtk::make_managed<Gtk::Entry>();
            m_save_name->set_placeholder_text("e.g. Cussler thriller");
            box->append(*m_save_name);
            auto* go = Gtk::make_managed<Gtk::Button>("Save");
            go->add_css_class("suggested-action");
            auto do_save = [this]{
                const std::string nm = m_save_name->get_text();
                if (!nm.empty()) { save_current_pattern(nm); m_save_pop->popdown(); }
            };
            go->signal_clicked().connect(do_save);
            m_save_name->signal_activate().connect(do_save);   // Enter saves too
            box->append(*go);
            m_save_pop->set_child(*box);
        }
        m_save_name->set_text(m_module.name);
        m_save_pop->popup();
        m_save_name->grab_focus();
    });
    row->append(m_btn_save_pattern);

    m_root.append(*row);
}

void PatternDialog::refresh_pattern_dropdown(const std::string& select_id) {
    ModuleLibrary::seed_builtins();              // idempotent: writes only if empty
    m_entries = ModuleLibrary::list();

    std::vector<Glib::ustring> names;
    names.reserve(m_entries.size());
    for (const auto& e : m_entries) names.push_back(e.name);

    guint sel = 0;
    for (size_t i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].id == select_id) { sel = static_cast<guint>(i); break; }

    m_loading_pattern = true;                    // suppress the selection echo
    m_pattern_dd->set_model(Gtk::StringList::create(names));
    if (!m_entries.empty()) m_pattern_dd->set_selected(sel);
    m_loading_pattern = false;
}

void PatternDialog::on_pattern_selected() {
    if (m_loading_pattern) return;               // programmatic change, not a pick
    const guint idx = m_pattern_dd->get_selected();
    if (idx >= m_entries.size()) return;
    if (auto m = ModuleLibrary::load(m_entries[idx].path))
        load_module_into_editor(*m);
}

void PatternDialog::load_module_into_editor(const Module& m) {
    m_module = m;                                // shell: id/name/top/pacing/deploy
    m_arc.clear();
    for (const auto& act : m.craft.acts)
        for (const auto& k : act.kps) m_arc.push_back(k);
    if (m_arc.empty()) m_arc.push_back(KeyPoint{}); // never leave an empty arc
    renumber_kps();
    rebuild_kp_list();                           // redraw the rows
    refresh_pace_pattern();                      // sync the pacing field + strip
    refresh_arc_views();                         // re-render board + re-plan preview
}

void PatternDialog::save_current_pattern(const std::string& name) {
    Module m = build_module();
    m.name = name;
    m.id   = name;                               // save() sanitizes the filename
    ModuleLibrary::save(m);
    refresh_pattern_dropdown(m.id);              // re-list and select the new one
}

// ── Key Points (s29 — the big chunk: count / names / meanings / reorder) ──────
//
// The dialog's centerpiece. m_arc is authoritative; the rows are rebuilt from it
// on structural change (add/remove/reorder) and edited in place for name/meaning
// (no rebuild → focus stays while typing — same discipline as the Parts list).
// Energies live elsewhere (Inspector + board faders), so a row is pure identity.

void PatternDialog::build_keypoints() {
    auto* head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto* title = make_section("Key Points");
    title->set_hexpand(true);
    head->append(*title);

    m_btn_load_kps.set_name("pattern-load-kps-btn");
    m_btn_load_kps.set_label("Load our 14");
    m_btn_load_kps.add_css_class("flat");
    m_btn_load_kps.set_valign(Gtk::Align::CENTER);
    m_btn_load_kps.set_tooltip_text(
        "Replace the list with Folio's calibrated 14-beat arc to start from.");
    m_btn_load_kps.signal_clicked().connect([this]{ load_builtin_kps(); });
    head->append(m_btn_load_kps);

    m_btn_add_kp.set_name("pattern-add-kp-btn");
    m_btn_add_kp.set_label("Add Key Point");
    m_btn_add_kp.add_css_class("flat");
    m_btn_add_kp.set_valign(Gtk::Align::CENTER);
    m_btn_add_kp.set_tooltip_text("Append a new Key Point to the end of the arc.");
    m_btn_add_kp.signal_clicked().connect([this]{ add_keypoint(); });
    head->append(m_btn_add_kp);

    m_dest->append(*head);

    m_kp_list.set_name("pattern-kp-list");
    m_kp_scroller.set_name("pattern-kp-scroller");
    m_kp_scroller.set_child(m_kp_list);
    m_kp_scroller.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_kp_scroller.set_propagate_natural_height(true);
    m_kp_scroller.set_max_content_height(240);   // scroll past ~4 rows
    m_kp_scroller.set_margin_top(4);
    m_kp_scroller.set_margin_bottom(2);
    m_dest->append(m_kp_scroller);

    rebuild_kp_list();
}

void PatternDialog::rebuild_kp_list() {
    while (auto* child = m_kp_list.get_first_child())
        m_kp_list.remove(*child);

    for (size_t i = 0; i < m_arc.size(); ++i) {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        row->add_css_class("pat-kp-row");
        row->set_hexpand(true);

        // s43 — drag handle (replaces the up/down arrows). DragSource lives on the
        // grip (these rows are entry-dominated, so a dedicated handle is the
        // reliable drag spot); the DropTarget is the whole row.
        auto* grip = Gtk::make_managed<Gtk::Image>();
        grip->set_from_icon_name("list-drag-handle-symbolic");
        grip->add_css_class("dim-label");
        grip->set_valign(Gtk::Align::CENTER);
        grip->set_tooltip_text("Drag to reorder");
        row->append(*grip);

        // Order number.
        auto* ord = Gtk::make_managed<Gtk::Label>(std::to_string(m_arc[i].order));
        ord->set_name(std::string("pattern-kp-ord-") + std::to_string(i));
        ord->add_css_class("pat-kp-ord");
        ord->set_width_chars(2);
        ord->set_valign(Gtk::Align::CENTER);
        row->append(*ord);

        // Name + meaning, stacked (keeps the row narrow enough for both fields).
        auto* fields = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 3);
        fields->set_hexpand(true);

        auto* name = Gtk::make_managed<Gtk::Entry>();
        name->set_name(std::string("pattern-kp-name-") + std::to_string(i));
        name->set_text(m_arc[i].label);
        name->set_placeholder_text("Key Point name");
        name->add_css_class("pat-kp-name");
        name->signal_changed().connect([this, i, name]{
            if (i < m_arc.size()) m_arc[i].label = name->get_text();
            // Name isn't drawn on the board yet (order/weight are), so no redraw.
        });
        fields->append(*name);

        auto* mean = Gtk::make_managed<Gtk::Entry>();
        mean->set_name(std::string("pattern-kp-mean-") + std::to_string(i));
        mean->set_text(m_arc[i].description);
        mean->set_placeholder_text("What this beat does (optional)");
        mean->add_css_class("pat-kp-mean");
        mean->signal_changed().connect([this, i, mean]{
            if (i < m_arc.size()) m_arc[i].description = mean->get_text();
        });
        fields->append(*mean);
        row->append(*fields);

        // Controls: pin / up / down / remove.
        auto* ctl = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 2);
        ctl->set_valign(Gtk::Align::CENTER);

        // Pin = this KP is a single extended hinge scene (fixed_scenes), exempt
        // from weight expansion. Toggling re-plans so the board shows the pin
        // glyph and the count snaps to 1.
        auto* pin = Gtk::make_managed<Gtk::ToggleButton>();
        pin->set_icon_name("folio-pin-symbolic");
        pin->add_css_class("flat");
        pin->add_css_class("circular");
        pin->set_active(m_arc[i].fixed_scenes > 0);
        pin->set_tooltip_text(
            "Pin to a single scene — a hinge the story turns on (a crossing, a "
            "revelation). When off, the Key Point expands across scenes by weight.");
        pin->signal_toggled().connect([this, i, pin]{
            if (i >= m_arc.size()) return;
            if (pin->get_active()) {
                m_arc[i].fixed_scenes = 1;
                m_arc[i].weight       = 1.0;   // single file — rigid, slides only
            } else {
                m_arc[i].fixed_scenes = 0;      // rejoins weight expansion
            }
            refresh_arc_views();
        });
        ctl->append(*pin);

        auto* rm = Gtk::make_managed<Gtk::Button>();
        rm->set_icon_name("window-close-symbolic");
        rm->add_css_class("flat");
        rm->add_css_class("circular");
        rm->set_tooltip_text("Remove this Key Point");
        rm->set_sensitive(m_arc.size() > 1);   // never empty the arc
        rm->signal_clicked().connect([this, i]{ remove_kp(i); });
        ctl->append(*rm);

        row->append(*ctl);

        // s43 — wire DnD now that the row + grip exist (grip = source, row = target).
        attach_kp_dnd(*row, *grip, i);
        m_kp_list.append(*row);
    }
}

void PatternDialog::renumber_kps() {
    for (size_t i = 0; i < m_arc.size(); ++i) {
        m_arc[i].order     = static_cast<int>(i) + 1;
        m_arc[i].color_idx = static_cast<int>(i) + 1;
    }
}

void PatternDialog::refresh_arc_views() {
    m_board.set_module(build_module());
    update_preview();
}

void PatternDialog::add_keypoint() {
    KeyPoint k;
    k.id          = "kp_new_" + std::to_string(m_arc.size() + 1);
    k.label       = "New Key Point";
    k.frenetic    = 0.5;   // mid; the author tunes on the board fader (s29-next)
    k.arc         = 0.5;
    k.weight      = 1.0;
    m_arc.push_back(k);
    renumber_kps();
    rebuild_kp_list();
    refresh_arc_views();
}

void PatternDialog::load_builtin_kps() {
    m_arc.clear();
    Module b = built_in_folio_keypoints();
    for (const auto& act : b.craft.acts)
        for (const auto& kp : act.kps) m_arc.push_back(kp);
    renumber_kps();
    rebuild_kp_list();
    refresh_arc_views();
}

void PatternDialog::move_kp(size_t idx, int dir) {
    const ptrdiff_t j = static_cast<ptrdiff_t>(idx) + dir;
    if (j < 0 || j >= static_cast<ptrdiff_t>(m_arc.size())) return;
    std::swap(m_arc[idx], m_arc[static_cast<size_t>(j)]);
    renumber_kps();
    rebuild_kp_list();
    refresh_arc_views();
}

// s43 — arbitrary drag-and-drop reorder (same vector-rebuild shape as the
// template builder's move_field_relative). Move the KP at `src` to sit just
// before/after the KP at `tgt`, then renumber so order/colour follow the new
// position (palette = arc) and refresh the board + preview.
void PatternDialog::move_kp_relative(size_t src, size_t tgt, bool after) {
    if (src >= m_arc.size() || tgt >= m_arc.size() || src == tgt) return;
    KeyPoint moved = m_arc[src];   // capture before the rebuild
    std::vector<KeyPoint> out;
    out.reserve(m_arc.size());
    for (size_t k = 0; k < m_arc.size(); ++k) {
        if (k == src) continue;
        if (k == tgt) {
            if (!after) out.push_back(moved);
            out.push_back(m_arc[k]);
            if (after)  out.push_back(moved);
        } else {
            out.push_back(m_arc[k]);
        }
    }
    if (out.size() != m_arc.size()) return;   // safety — never drop a KP
    m_arc = std::move(out);
    renumber_kps();
    rebuild_kp_list();
    refresh_arc_views();
}

void PatternDialog::attach_kp_dnd(Gtk::Widget& row, Gtk::Widget& grip, size_t index) {
    auto alive = std::make_shared<bool>(true);
    row.signal_destroy().connect([alive]{ *alive = false; });
    Gtk::Widget* rowp = &row;

    // ── Drag source (on the grip) ────────────────────────────────────────────
    auto src = Gtk::DragSource::create();
    src->set_actions(Gdk::DragAction::MOVE);
    src->signal_prepare().connect(
        [this, index](double, double) -> Glib::RefPtr<Gdk::ContentProvider> {
            m_drag_kp = static_cast<int>(index);
            Glib::Value<int> val;
            val.init(G_TYPE_INT);
            val.set(static_cast<int>(index));
            return Gdk::ContentProvider::create(val);
        },
        false);
    src->signal_drag_begin().connect(
        [rowp, alive](const Glib::RefPtr<Gdk::Drag>&) {
            if (*alive) rowp->add_css_class("binder-drag-source");
        },
        false);
    src->signal_drag_end().connect(
        [this, rowp, alive](const Glib::RefPtr<Gdk::Drag>&, bool) {
            m_drag_kp = -1;
            if (*alive) rowp->remove_css_class("binder-drag-source");
        },
        false);
    grip.add_controller(src);

    // ── Drop target (the whole row) ──────────────────────────────────────────
    auto dst = Gtk::DropTarget::create(G_TYPE_INT, Gdk::DragAction::MOVE);

    auto clear_hl = [rowp, alive]{
        if (!*alive) return;
        rowp->remove_css_class("binder-drop-before");
        rowp->remove_css_class("binder-drop-after");
    };

    dst->signal_motion().connect(
        [this, index, rowp, alive, clear_hl](double, double y) -> Gdk::DragAction {
            if (!*alive || m_drag_kp < 0 || m_drag_kp == static_cast<int>(index))
                return Gdk::DragAction{};
            bool after = (y > rowp->get_height() * 0.5);
            clear_hl();
            rowp->add_css_class(after ? "binder-drop-after" : "binder-drop-before");
            return Gdk::DragAction::MOVE;
        },
        false);

    dst->signal_leave().connect([clear_hl]{ clear_hl(); }, false);

    dst->signal_drop().connect(
        [this, index, rowp, alive, clear_hl](const Glib::ValueBase&, double, double y) -> bool {
            if (!*alive) return false;
            clear_hl();
            const int src_idx = m_drag_kp;   // still set: drop fires before drag_end
            if (src_idx < 0 || src_idx == static_cast<int>(index)) return false;
            bool after = (y > rowp->get_height() * 0.5);
            // Defer off the live drop handler (the idle rule) — move_kp_relative
            // rebuilds the row list, destroying this very widget.
            Glib::signal_idle().connect_once(
                [this, src_idx, index, after]{
                    move_kp_relative(static_cast<size_t>(src_idx), index, after);
                });
            return true;
        },
        false);

    row.add_controller(dst);
}

void PatternDialog::remove_kp(size_t idx) {
    if (idx >= m_arc.size() || m_arc.size() <= 1) return;
    m_arc.erase(m_arc.begin() + static_cast<ptrdiff_t>(idx));
    renumber_kps();
    rebuild_kp_list();
    refresh_arc_views();
}

// build_module — reassemble the SHELL + the edited flat arc into one Module. The
// whole arc goes in a single synthetic act (acts→Parts only matter when top !=
// None, and the folio arc is top=None; Parts are author-defined chapter ranges,
// orthogonal to acts). Renumbered so order/color stay 1..n.
Module PatternDialog::build_module() const {
    Module m = m_module;            // id / name / top / pacing / deploy
    m.craft.acts.clear();
    Act a;
    a.id    = "arc";
    a.label = "Arc";
    a.kps   = m_arc;
    for (size_t i = 0; i < a.kps.size(); ++i) {
        a.kps[i].order     = static_cast<int>(i) + 1;
        a.kps[i].color_idx = static_cast<int>(i) + 1;
    }
    m.craft.acts.push_back(std::move(a));
    return m;
}

// ── Length ───────────────────────────────────────────────────────────────────

void PatternDialog::build_length() {
    m_dest->append(*make_section("Length"));

    m_spin_target.set_name("pattern-target-spin");
    m_spin_target.set_range(5000, 500000);
    m_spin_target.set_increments(1000, 10000);
    m_spin_target.set_value(90000);
    m_spin_target.set_digits(0);
    m_spin_target.set_size_request(120, -1);
    m_spin_target.set_tooltip_text(
        "Approximate finished length. Scenes expand to fill this at the "
        "words-per-scene size; bookends are reserved off the top.");
    m_spin_target.signal_value_changed().connect([this]{ update_preview(); });
    m_dest->append(*make_row("Target words", m_spin_target));

    m_spin_scene_words.set_name("pattern-scene-words-spin");
    m_spin_scene_words.set_range(250, 5000);
    m_spin_scene_words.set_increments(50, 250);
    m_spin_scene_words.set_value(1130);   // Scott's signature scene (4-novel mean)
    m_spin_scene_words.set_digits(0);
    m_spin_scene_words.set_size_request(120, -1);
    m_spin_scene_words.set_tooltip_text(
        "Average words per scene. The arc is sliced into scenes of about this "
        "size; the planner sets each scene's word target from it.");
    m_spin_scene_words.signal_value_changed().connect([this]{ update_preview(); });
    m_dest->append(*make_row("Words per scene", m_spin_scene_words));
}

// ── Chapters ─────────────────────────────────────────────────────────────────

void PatternDialog::build_chapters() {
    m_dest->append(*make_section("Chapters"));

    m_spin_chapters.set_name("pattern-chapters-spin");
    m_spin_chapters.set_range(1, 200);
    m_spin_chapters.set_increments(1, 5);
    m_spin_chapters.set_value(10);
    m_spin_chapters.set_digits(0);
    m_spin_chapters.set_size_request(120, -1);
    m_spin_chapters.set_tooltip_text(
        "How many chapters to chunk the scenes into. Chapters number "
        "continuously across the whole book.");
    m_spin_chapters.signal_value_changed().connect([this]{ update_preview(); });
    m_dest->append(*make_row("Chapters", m_spin_chapters));

    m_preview_lbl.set_name("pattern-preview-lbl");
    m_preview_lbl.add_css_class("pat-preview");
    m_preview_lbl.set_halign(Gtk::Align::START);
    m_preview_lbl.set_wrap(true);
    m_dest->append(m_preview_lbl);
}

// ── Pacing ─────────────────────────────────────────────────────────────────

void PatternDialog::build_pacing() {
    m_dest->append(*make_section("Pacing"));

    auto store = Gtk::StringList::create({
        "Flat — every scene at its Key Point's energy",
        "Build to spike — ramp within each cluster"
    });
    m_pace_dd = Gtk::make_managed<Gtk::DropDown>(
        store, Glib::RefPtr<Gtk::Expression<Glib::ustring>>{});
    m_pace_dd->set_name("pattern-pace-dd");
    m_pace_dd->set_selected(0);
    m_pace_dd->set_tooltip_text(
        "Scene-energy guide. \"Build to spike\" redistributes energy within "
        "each Key Point cluster (builds below, spikes above) while keeping the "
        "Key Point's frenetic as the cluster mean. A guide, never a verdict.");
    m_pace_dd->property_selected().signal_changed().connect([this]{ update_preview(); });
    m_pace_dd->property_selected().signal_changed().connect([this]{
        if (m_pace_pattern_box)          // pattern is only used by "Build to spike"
            m_pace_pattern_box->set_visible(m_pace_dd->get_selected() == 1);
        update_preview();
    });
    m_dest->append(*make_row("Scene energy", *m_pace_dd));

    // s30 — the pattern itself, programmable. Percentages, cycled across the run
    // when "Build to spike" is on. Edit the sequence; the strip below shows it.
    // The whole group hides under "Flat" (the pattern isn't applied then).
    m_pace_pattern_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);

    m_pace_pattern_entry = Gtk::make_managed<Gtk::Entry>();
    m_pace_pattern_entry->set_name("pattern-pace-levels");
    m_pace_pattern_entry->set_tooltip_text(
        "The pacing rhythm, as percentages cycled across the scenes (e.g. "
        "30, 30, 62, 100 — light, light, building, spike). Normalised to its own "
        "mean, so a Key Point's frenetic stays the cluster average.");
    m_pace_pattern_entry->signal_changed().connect([this]{
        if (m_loading_pace) return;
        auto lv = parse_levels(m_pace_pattern_entry->get_text());
        if (!lv.empty()) {                       // ignore transient empty/garbage
            m_module.pacing.levels = lv;
            m_pace_strip.queue_draw();
            update_preview();
        }
    });
    m_pace_pattern_box->append(*make_row("Pattern", *m_pace_pattern_entry));

    // Step-bar strip: a read-out of the current pattern, burgundy (frenetic),
    // with the mean line the Key Point fader rides.
    m_pace_strip.set_name("pattern-pace-strip");
    m_pace_strip.set_content_height(54);
    m_pace_strip.set_hexpand(true);
    m_pace_strip.set_margin_top(4);
    m_pace_strip.set_draw_func(
        [this](const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
            const auto& lv = m_module.pacing.levels;
            cr->set_source_rgb(0.118, 0.118, 0.137);
            cr->rectangle(0, 0, w, h); cr->fill();
            if (lv.empty()) return;
            const double PAD = 8, base = h - 16, top = 10;
            const double bw = (w - 2 * PAD) / static_cast<double>(lv.size());
            double mean = 0; for (double v : lv) mean += v; mean /= lv.size();
            const double my = base - (base - top) * std::clamp(mean, 0.0, 1.0);
            cr->set_source_rgba(1, 1, 1, 0.25); cr->set_line_width(1);
            cr->move_to(PAD, my); cr->line_to(w - PAD, my); cr->stroke();
            cr->set_font_size(7); cr->set_source_rgba(1, 1, 1, 0.40);
            cr->move_to(w - PAD - 26, my - 2); cr->show_text("mean");
            for (size_t i = 0; i < lv.size(); ++i) {
                const double v = std::clamp(lv[i], 0.0, 1.0);
                const double x = PAD + i * bw, bh = (base - top) * v;
                cr->set_source_rgba(0x8C / 255.0, 0x1D / 255.0, 0x40 / 255.0, 0.85);
                cr->rectangle(x + 3, base - bh, bw - 6, bh); cr->fill();
                cr->set_source_rgba(1, 1, 1, 0.70); cr->set_font_size(8);
                std::string lbl = std::to_string((int)std::lround(v * 100));
                Cairo::TextExtents e; cr->get_text_extents(lbl, e);
                cr->move_to(x + bw / 2 - e.width / 2, base + 11); cr->show_text(lbl);
            }
        });
    m_pace_pattern_box->append(m_pace_strip);
    m_dest->append(*m_pace_pattern_box);
    m_pace_pattern_box->set_visible(m_pace_dd->get_selected() == 1);  // hidden in Flat

    refresh_pace_pattern();   // prime the entry text from the current module
}

void PatternDialog::refresh_pace_pattern() {
    if (!m_pace_pattern_entry) return;
    m_loading_pace = true;
    m_pace_pattern_entry->set_text(format_levels(m_module.pacing.levels));
    m_loading_pace = false;
    m_pace_strip.queue_draw();
}

// ── Structure (optional Parts) ───────────────────────────────────────────────

void PatternDialog::build_structure() {
    m_dest->append(*make_section("Structure"));

    auto* head = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);

    m_chk_parts.set_name("pattern-parts-check");
    m_chk_parts.set_label("Organize into Parts");
    m_chk_parts.set_active(false);
    m_chk_parts.set_hexpand(true);
    m_chk_parts.set_tooltip_text(
        "Group the chapters into author-defined Parts (or Books) with their own "
        "titles. Optional and independent of the arc; chapter numbering stays "
        "continuous across Parts.");
    m_chk_parts.signal_toggled().connect([this]{ on_parts_toggled(); });
    head->append(m_chk_parts);

    auto store = Gtk::StringList::create({ "Part", "Book" });
    m_container_dd = Gtk::make_managed<Gtk::DropDown>(
        store, Glib::RefPtr<Gtk::Expression<Glib::ustring>>{});
    m_container_dd->set_name("pattern-container-dd");
    m_container_dd->set_selected(0);
    m_container_dd->set_tooltip_text("Label the top-level groups \"Part\" or \"Book\".");
    head->append(*m_container_dd);

    m_dest->append(*head);

    // Detail (parts list + add button), hidden until the box is ticked.
    m_parts_detail.set_name("pattern-parts-detail");
    m_parts_detail.set_margin_top(6);
    m_parts_detail.set_margin_start(4);

    m_parts_list.set_name("pattern-parts-list");
    m_parts_detail.append(m_parts_list);

    m_btn_add_part.set_name("pattern-add-part-btn");
    m_btn_add_part.set_label("Add Part");
    m_btn_add_part.set_halign(Gtk::Align::START);
    m_btn_add_part.add_css_class("flat");
    m_btn_add_part.signal_clicked().connect([this]{
        add_part("", 1);   // an empty title falls back to "Part N" in the planner
    });
    m_parts_detail.append(m_btn_add_part);

    m_parts_detail.set_visible(false);
    m_container_dd->set_sensitive(false);
    m_dest->append(m_parts_detail);
}

// ── Bookends ─────────────────────────────────────────────────────────────────

void PatternDialog::build_bookends() {
    m_dest->append(*make_section("Bookends"));

    auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 18);

    m_chk_prologue.set_name("pattern-prologue-check");
    m_chk_prologue.set_label("Prologue");
    m_chk_prologue.set_active(true);   // preserve the shipped scaffold shape
    m_chk_prologue.set_tooltip_text("Add a single cool single-scene Prologue before the arc.");
    m_chk_prologue.signal_toggled().connect([this]{ update_preview(); });
    row->append(m_chk_prologue);

    m_chk_epilogue.set_name("pattern-epilogue-check");
    m_chk_epilogue.set_label("Epilogue");
    m_chk_epilogue.set_active(true);
    m_chk_epilogue.set_tooltip_text("Add a single cool single-scene Epilogue after the arc.");
    m_chk_epilogue.signal_toggled().connect([this]{ update_preview(); });
    row->append(m_chk_epilogue);

    m_dest->append(*row);
}

// ── Footer ─────────────────────────────────────────────────────────────────

void PatternDialog::build_footer() {
    m_footer.add_css_class("pat-footer");
    m_footer.set_hexpand(true);

    auto* spacer = Gtk::make_managed<Gtk::Box>();
    spacer->set_hexpand(true);
    m_footer.append(*spacer);

    m_btn_cancel.set_name("pattern-cancel-btn");
    m_btn_cancel.set_label("Cancel");
    m_btn_cancel.signal_clicked().connect([this]{ close(); });
    m_footer.append(m_btn_cancel);

    m_btn_create.set_name("pattern-create-btn");
    m_btn_create.set_label("Create");
    m_btn_create.add_css_class("suggested-action");
    m_btn_create.signal_clicked().connect([this]{ on_create(); });
    m_footer.append(m_btn_create);

    m_footer.set_margin_start(20);
    m_footer.set_margin_end(20);
    m_root.append(m_footer);
}

// ─────────────────────────────────────────────────────────────────────────────
// Parts management (model-authoritative; widgets rebuilt from m_parts)
// ─────────────────────────────────────────────────────────────────────────────

void PatternDialog::on_parts_toggled() {
    const bool on = m_chk_parts.get_active();
    if (on && m_parts.empty())
        seed_parts_from_chapters();

    m_parts_detail.set_visible(on);
    m_container_dd->set_sensitive(on);
    // When Parts drive the structure, the standalone chapter total is derived
    // from the Part rows — the lone spin would lie, so dim it.
    m_spin_chapters.set_sensitive(!on);
    rebuild_parts_list();
    update_preview();
}

void PatternDialog::seed_parts_from_chapters() {
    // Split the current chapter count into ~3 even Parts so the first ticked
    // state is immediately sensible (the author renames / rebalances).
    int total = m_spin_chapters.get_value_as_int();
    int n = std::clamp(total, 1, 3);
    int base = total / n, rem = total % n;
    m_parts.clear();
    for (int i = 0; i < n; ++i)
        m_parts.push_back({ "", base + (i < rem ? 1 : 0) });
}

void PatternDialog::add_part(const std::string& title, int chapters) {
    m_parts.push_back({ title, std::max(1, chapters) });
    rebuild_parts_list();
    update_preview();
}

void PatternDialog::remove_part(size_t idx) {
    if (idx >= m_parts.size()) return;
    m_parts.erase(m_parts.begin() + static_cast<ptrdiff_t>(idx));
    rebuild_parts_list();
    update_preview();
}

void PatternDialog::rebuild_parts_list() {
    while (auto* child = m_parts_list.get_first_child())
        m_parts_list.remove(*child);

    for (size_t i = 0; i < m_parts.size(); ++i) {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        row->add_css_class("pat-part-row");
        row->set_hexpand(true);

        // Title (empty → "Part N" in the planner)
        auto* title = Gtk::make_managed<Gtk::Entry>();
        title->set_hexpand(true);
        title->set_text(m_parts[i].title);
        title->set_placeholder_text("Part title (optional)");
        // Write back to the model on edit (no rebuild — keeps focus while typing).
        title->signal_changed().connect([this, i, title]{
            if (i < m_parts.size()) m_parts[i].title = title->get_text();
        });
        row->append(*title);

        // Chapter count for this Part
        auto* spin = Gtk::make_managed<Gtk::SpinButton>();
        spin->set_range(1, 100);
        spin->set_increments(1, 5);
        spin->set_value(m_parts[i].chapters);
        spin->set_digits(0);
        spin->set_tooltip_text("Chapters in this Part");
        spin->signal_value_changed().connect([this, i, spin]{
            if (i < m_parts.size()) m_parts[i].chapters = spin->get_value_as_int();
            update_preview();
        });
        row->append(*spin);

        // Remove
        auto* rm = Gtk::make_managed<Gtk::Button>();
        rm->set_icon_name("window-close-symbolic");
        rm->set_tooltip_text("Remove this Part");
        rm->add_css_class("flat");
        rm->add_css_class("circular");
        rm->signal_clicked().connect([this, i]{ remove_part(i); });
        row->append(*rm);

        m_parts_list.append(*row);
    }
}

int PatternDialog::parts_chapter_total() const {
    int n = 0;
    for (const auto& p : m_parts) n += std::max(1, p.chapters);
    return n;
}

// ─────────────────────────────────────────────────────────────────────────────
// Inputs
// ─────────────────────────────────────────────────────────────────────────────

PlanInputs PatternDialog::current_inputs() const {
    PlanInputs in;
    in.target_words    = m_spin_target.get_value_as_int();
    in.avg_scene_words = m_spin_scene_words.get_value_as_int();
    in.chapters        = m_spin_chapters.get_value_as_int();
    in.scene_pattern   = (m_pace_dd && m_pace_dd->get_selected() == 1)
                             ? ScenePattern::BuildToSpike
                             : ScenePattern::Flat;
    in.prologue        = m_chk_prologue.get_active();
    in.epilogue        = m_chk_epilogue.get_active();

    if (m_chk_parts.get_active() && !m_parts.empty()) {
        in.top = (m_container_dd && m_container_dd->get_selected() == 1)
                     ? TopContainer::Book
                     : TopContainer::Part;
        for (const auto& p : m_parts)
            in.part_specs.push_back({ p.title, std::max(1, p.chapters) });
    } else {
        in.top = TopContainer::None;   // chapters at the manuscript root
    }
    return in;
}

// ─────────────────────────────────────────────────────────────────────────────
// Live preview — call the real planner so the readout == what gets built
// ─────────────────────────────────────────────────────────────────────────────

void PatternDialog::update_preview() {
    if (m_updating) return;
    m_updating = true;

    PlanInputs in = current_inputs();
    ScaffoldPlan plan = ModulePlanner::plan(build_module(), in);

    // Per-KP scene counts (the board's bottom number = scenes per KP). Tally by
    // kp_id across the plan, in m_arc order.
    {
        std::vector<int> counts(m_arc.size(), 0);
        std::unordered_map<std::string, int> idx;
        for (size_t i = 0; i < m_arc.size(); ++i) idx[m_arc[i].id] = static_cast<int>(i);
        for (const auto& part : plan.parts)
            for (const auto& ch : part.chapters)
                for (const auto& sc : ch.scenes) {
                    auto it = idx.find(sc.kp_id);
                    if (it != idx.end()) ++counts[it->second];
                }
        m_board.set_scene_counts(counts);

        // Chapter band: scenes per chapter, in told-line order.
        std::vector<int> chsizes;
        for (const auto& part : plan.parts)
            for (const auto& ch : part.chapters)
                chsizes.push_back(static_cast<int>(ch.scenes.size()));
        m_board.set_chapters(chsizes);
    }

    int chapters = 0, scenes = 0, lo = -1, hi = 0;
    for (const auto& part : plan.parts)
        for (const auto& ch : part.chapters) {
            ++chapters;
            int n = static_cast<int>(ch.scenes.size());
            scenes += n;
            hi = std::max(hi, n);
            lo = (lo < 0) ? n : std::min(lo, n);
        }
    if (lo < 0) lo = 0;

    std::ostringstream ss;
    ss << "\u2248 " << scenes << (scenes == 1 ? " scene" : " scenes")
       << " in " << chapters << (chapters == 1 ? " chapter" : " chapters");
    if (chapters > 0) {
        if (lo == hi) ss << " (about " << lo << " per chapter)";
        else          ss << " (about " << lo << "\u2013" << hi << " per chapter)";
    }

    int bookends = static_cast<int>(plan.prologue.size() + plan.epilogue.size());
    if (bookends > 0) {
        ss << "  \u00b7  + ";
        if (!plan.prologue.empty()) ss << "prologue";
        if (!plan.prologue.empty() && !plan.epilogue.empty()) ss << " & ";
        if (!plan.epilogue.empty()) ss << "epilogue";
    }

    if (m_chk_parts.get_active() && !m_parts.empty()) {
        const char* w = (in.top == TopContainer::Book) ? "Book" : "Part";
        ss << "  \u00b7  " << m_parts.size() << " "
           << w << (m_parts.size() == 1 ? "" : "s");
        // The Part rows own the chapter total; surface it (the lone spin is dim).
        m_spin_chapters.set_value(parts_chapter_total());
    }

    m_preview_lbl.set_text(ss.str());
    m_updating = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Create
// ─────────────────────────────────────────────────────────────────────────────

void PatternDialog::on_create() {
    // Gather inputs, then close BEFORE doing any work. The apply callback swaps
    // the whole DocumentModel and rebuilds the sidebar/inspector — if that runs
    // while this modal is still up, close()'s focus restoration lands on widgets
    // the rebuild just destroyed (use-after-free). So we close first and run the
    // scaffold on an idle tick, once the dialog has fully torn down and nothing
    // modal is in play. cb/in are captured by value, independent of this dialog's
    // lifetime (it may be reset on its own hide-idle before this fires).
    PlanInputs    in  = current_inputs();
    Module        mod = build_module();   // the AUTHORED arc travels, not the default
    ApplyCallback cb  = m_on_apply;
    close();
    if (cb)
        Glib::signal_idle().connect_once([cb, mod, in]() { cb(mod, in); });
}

} // namespace Folio
