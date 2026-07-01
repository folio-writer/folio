// ─────────────────────────────────────────────────────────────────────────────
// Folio — DiffView.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "DiffView.hpp"
#include <cmath>
#include <string>

namespace Folio {

namespace {
constexpr const char* kRed   = "#f38ba8";  // removed (matches @col_red)
constexpr const char* kGreen = "#a6e3a1";  // added   (matches @col_green)
constexpr const char* kMuted = "#6c7086";  // line numbers / filler

int diff_count_words(const std::string& s) {
    int c = 0;
    for (const auto& w : SnapshotDiff::split_words(s))
        if (w != " " && w != "\t" && w != "\n") ++c;
    return c;
}
}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// construction
// ─────────────────────────────────────────────────────────────────────────────
DiffView::DiffView() : Gtk::Box(Gtk::Orientation::VERTICAL, 0) {
    add_css_class("diff-view");
    set_hexpand(true);
    set_vexpand(true);

    // s98 — a provider that sizes only the diff BODY labels (.diff-body) so the
    // paragraphs + line numbers follow the editor zoom, and shades the filler cells
    // (.diff-filler) that pad a missing paragraph. Registered DISPLAY-WIDE — a
    // per-widget provider in GTK4 does not cascade to descendant labels.
    m_text_css = Gtk::CssProvider::create();
    get_style_context()->add_provider_for_display(
        Gdk::Display::get_default(), m_text_css, GTK_STYLE_PROVIDER_PRIORITY_USER);
    set_text_scale(1.0);

    // Header (toolbar).
    m_header.set_margin_start(12);
    m_header.set_margin_end(12);
    m_header.set_margin_top(8);
    m_header.set_margin_bottom(6);
    build_header();
    append(m_header);

    // Pane headers — sit over the two panes, swap-aware, Current marked live.
    m_pane_left.set_use_markup(true);
    m_pane_right.set_use_markup(true);
    m_pane_left.set_xalign(0.0f);
    m_pane_right.set_xalign(0.0f);
    m_pane_left.set_hexpand(true);
    m_pane_right.set_hexpand(true);
    m_pane_left.set_margin_start(28);
    m_pane_right.set_margin_start(28);
    m_pane_headers.set_margin_start(12);
    m_pane_headers.set_margin_end(12);
    m_pane_headers.set_margin_bottom(4);
    m_pane_headers.append(m_pane_left);
    m_pane_headers.append(m_pane_right);
    append(m_pane_headers);

    // Body — one grid in one scroller ⇒ shared scroll, guaranteed row alignment.
    m_grid.set_column_spacing(4);
    m_grid.set_row_spacing(10);   // s98 — breathing room between paragraphs so a
                                  // row reads as one block and the eye tracks the
                                  // left/right pairing across the gutter
    m_grid.set_margin_start(12);
    m_grid.set_margin_end(12);
    m_grid.set_hexpand(true);
    m_grid.add_css_class("diff-grid");
    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_scroll.set_child(m_grid);
    m_scroll.set_vexpand(true);
    append(m_scroll);

    // Footer — change stats.
    m_footer.set_margin_start(12);
    m_footer.set_margin_end(12);
    m_footer.set_margin_top(6);
    m_footer.set_margin_bottom(8);
    m_stats.set_use_markup(true);
    m_stats.set_xalign(0.0f);
    m_footer.append(m_stats);
    auto* fspacer = Gtk::make_managed<Gtk::Box>();
    fspacer->set_hexpand(true);
    m_footer.append(*fspacer);
    m_pick_hint.set_use_markup(true);
    m_pick_hint.set_xalign(1.0f);
    m_footer.append(m_pick_hint);
    append(m_footer);
}

// ─────────────────────────────────────────────────────────────────────────────
// text scale (follows the editor zoom)
// ─────────────────────────────────────────────────────────────────────────────
void DiffView::set_text_scale(double factor) {
    m_scale = factor;
    // Font size is baked into the Pango markup per label (inline attributes win
    // over CSS regardless of provider cascade). CSS here only shades the filler.
    m_text_css->load_from_data(
        ".diff-filler { background-color: rgba(108,112,134,0.10); }");
    if (m_node) rebuild();   // re-render existing rows at the new size
}

// ─────────────────────────────────────────────────────────────────────────────
// header
// ─────────────────────────────────────────────────────────────────────────────
void DiffView::build_header() {
    m_title.set_use_markup(true);
    m_title.set_markup("<b>Diff</b>");
    m_title.set_xalign(0.0f);
    m_header.append(m_title);

    m_snap_picker.set_tooltip_text("Compare a different snapshot against Current");
    m_snap_picker.signal_changed().connect([this]() {
        if (m_building) return;
        const auto id = m_snap_picker.get_active_id();
        if (id.empty()) return;
        try {
            m_snap_idx = std::stoi(id.raw());
        } catch (...) {
            return;
        }
        rebuild();
    });
    m_header.append(m_snap_picker);

    m_swap_btn.set_label("\u21c4  Swap");
    m_swap_btn.set_tooltip_text(
        "Swap sides — presentation only; Current stays the source of truth");
    m_swap_btn.signal_clicked().connect([this]() {
        m_swapped = !m_swapped;
        rebuild();
    });
    m_header.append(m_swap_btn);

    m_annotate_btn.set_label("\u270e  Annotate Current");
    m_annotate_btn.set_tooltip_text(
        "1) Select text on the Snapshot side (or click a snapshot paragraph). "
        "2) Click a Current paragraph to choose where the note lands (optional — "
        "defaults to the matching paragraph). 3) Click here to add it as an "
        "annotation on Current — review, copy or delete it later in the editor.");
    m_annotate_btn.set_sensitive(false);
    m_annotate_btn.signal_clicked().connect(
        [this]() { annotate_current_from_pick(); });
    m_header.append(m_annotate_btn);

    auto* spacer = Gtk::make_managed<Gtk::Box>();
    spacer->set_hexpand(true);
    m_header.append(*spacer);

    auto make_chip = [](const std::string& txt, const std::string& css) {
        auto* l = Gtk::make_managed<Gtk::Label>(txt);
        l->add_css_class("badge-chip");
        l->add_css_class(css);
        return l;
    };
    m_header.append(*make_chip("\u2212 removed", "diff-del"));
    m_header.append(*make_chip("+ added", "diff-ins"));

    m_close_btn.set_icon_name("window-close-symbolic");
    m_close_btn.set_tooltip_text("Close diff — back to writing");
    m_close_btn.add_css_class("flat");
    m_close_btn.signal_clicked().connect([this]() {
        if (m_on_close) m_on_close();
    });
    m_header.append(m_close_btn);
}

// ─────────────────────────────────────────────────────────────────────────────
// target / picker
// ─────────────────────────────────────────────────────────────────────────────
void DiffView::set_target(const BinderNode* node, int snap_idx) {
    m_node = node;
    m_snap_idx = snap_idx;
    m_swapped = false;
    m_last_clicked_row = -1;
    m_dest_current_para = -1;
    m_pick_hint.set_text("");

    m_building = true;
    m_snap_picker.remove_all();
    if (m_node) {
        for (std::size_t i = 0; i < m_node->snapshots.size(); ++i) {
            const auto& s = m_node->snapshots[i];
            std::string label = s.name.empty() ? "Snapshot" : s.name;
            if (!s.timestamp.empty()) label += "  \u00b7  " + s.timestamp;
            m_snap_picker.append(std::to_string(i), label);
        }
        if (snap_idx >= 0 &&
            snap_idx < static_cast<int>(m_node->snapshots.size()))
            m_snap_picker.set_active_id(std::to_string(snap_idx));
    }
    m_building = false;

    rebuild();
}

// ─────────────────────────────────────────────────────────────────────────────
// rendering helpers
// ─────────────────────────────────────────────────────────────────────────────
int DiffView::side_lineno(const DiffRow& r, Side side) {
    return side == Side::Snapshot ? r.left_no : r.right_no;
}

std::string DiffView::side_markup(const DiffRow& r, Side side) {
    auto esc = [](const std::string& s) {
        return std::string(Glib::Markup::escape_text(s).raw());
    };
    const std::string red_open =
        std::string("<span foreground=\"") + kRed + "\" strikethrough=\"true\">";
    const std::string grn_open =
        std::string("<span foreground=\"") + kGreen + "\">";

    std::string out;
    if (side == Side::Snapshot) {
        switch (r.kind) {
            case DiffRow::Kind::Equal:  out = esc(r.left); break;
            case DiffRow::Kind::Delete: out = red_open + esc(r.left) + "</span>"; break;
            case DiffRow::Kind::Insert: out.clear(); break;  // blank filler
            case DiffRow::Kind::Change:
                for (const auto& op : r.left_ops) {
                    if (op.kind == DiffOp::Kind::Delete)
                        out += red_open + esc(op.text) + "</span>";
                    else  // Equal
                        out += esc(op.text);
                }
                break;
        }
    } else {  // Current
        switch (r.kind) {
            case DiffRow::Kind::Equal:  out = esc(r.right); break;
            case DiffRow::Kind::Insert: out = grn_open + esc(r.right) + "</span>"; break;
            case DiffRow::Kind::Delete: out.clear(); break;  // blank filler
            case DiffRow::Kind::Change:
                for (const auto& op : r.right_ops) {
                    if (op.kind == DiffOp::Kind::Insert)
                        out += grn_open + esc(op.text) + "</span>";
                    else  // Equal
                        out += esc(op.text);
                }
                break;
        }
    }
    return out;
}

void DiffView::refresh_pane_headers() {
    std::string snap_nm = "Snapshot";
    if (m_node && m_snap_idx >= 0 &&
        m_snap_idx < static_cast<int>(m_node->snapshots.size())) {
        const auto& s = m_node->snapshots[static_cast<std::size_t>(m_snap_idx)];
        if (!s.name.empty()) snap_nm = s.name;
    }
    const std::string snp =
        "<b>Snapshot</b>  \u00b7  " +
        std::string(Glib::Markup::escape_text(snap_nm).raw());
    const std::string cur =
        std::string("<b>Current</b>  <span foreground=\"") + kGreen + "\">\u25cf live</span>";

    m_pane_left.set_markup(m_swapped ? cur : snp);
    m_pane_right.set_markup(m_swapped ? snp : cur);
}

// ─────────────────────────────────────────────────────────────────────────────
// rebuild
// ─────────────────────────────────────────────────────────────────────────────
void DiffView::clear_grid() {
    while (Gtk::Widget* c = m_grid.get_first_child())
        m_grid.remove(*c);
}

void DiffView::rebuild() {
    clear_grid();
    m_rows.clear();
    m_snap_labels.clear();
    m_curr_labels.clear();

    if (!m_node || m_snap_idx < 0 ||
        m_snap_idx >= static_cast<int>(m_node->snapshots.size())) {
        m_annotate_btn.set_sensitive(false);
        refresh_pane_headers();
        m_stats.set_text("");
        return;
    }

    const auto& snap = m_node->snapshots[static_cast<std::size_t>(m_snap_idx)];
    const auto snap_lines = SnapshotDiff::html_to_lines(snap.content);
    const auto curr_lines = SnapshotDiff::html_to_lines(m_node->content);
    m_rows = SnapshotDiff::diff_rows(snap_lines, curr_lines);

    // Font size baked directly into the Pango markup — inline attributes override
    // any CSS, so this works regardless of provider cascade. Base 11pt ≈ the old
    // dialog's 15px at 100%; scales with the editor zoom (m_scale). Built from ints
    // so the decimal is always '.' (locale-safe for Pango).
    double pt = 11.0 * m_scale;
    if (pt < 7.0)   pt = 7.0;
    if (pt > 120.0) pt = 120.0;
    const int tenths = static_cast<int>(std::lround(pt * 10.0));
    const std::string ptstr =
        std::to_string(tenths / 10) + "." + std::to_string(tenths % 10);
    m_size_open = "<span font='" + ptstr + "'>";
    const std::string& size_open = m_size_open;

    auto make_no = [&size_open](int no) {
        auto* n = Gtk::make_managed<Gtk::Label>();
        n->set_use_markup(true);
        if (no > 0)
            n->set_markup(size_open + "<span foreground=\"" + kMuted +
                          "\" font_family=\"monospace\">" + std::to_string(no) +
                          "</span></span>");
        n->set_xalign(1.0f);
        n->set_yalign(0.0f);
        n->set_valign(Gtk::Align::START);
        n->set_width_chars(4);
        n->set_margin_start(2);
        n->set_margin_end(6);
        n->set_margin_top(2);
        n->add_css_class("diff-body");
        return n;
    };
    auto make_text = [&size_open](const std::string& markup, const char* row_css) {
        const bool filler = markup.empty();   // the blank side of a delete/insert row
        auto* l = Gtk::make_managed<Gtk::Label>();
        l->set_use_markup(true);
        l->set_markup(size_open + markup + "</span>");
        l->set_wrap(true);
        l->set_wrap_mode(Pango::WrapMode::WORD_CHAR);
        l->set_xalign(0.0f);
        l->set_yalign(0.0f);
        l->set_halign(Gtk::Align::FILL);
        // Filler stretches to the row height (set by the paragraph on the other
        // side) so the gap is padded and the next match lines up; real text
        // top-aligns so unequal-height matches still align at the top.
        l->set_valign(filler ? Gtk::Align::FILL : Gtk::Align::START);
        l->set_hexpand(true);
        l->set_selectable(!filler);
        l->set_margin_top(2);
        l->set_margin_bottom(2);
        l->set_margin_start(6);
        l->set_margin_end(6);
        l->add_css_class("diff-body");
        if (filler)  l->add_css_class("diff-filler");
        if (row_css) l->add_css_class(row_css);
        return l;
    };

    // A per-row gutter connector: a line linking the two paragraphs across the
    // centre. Equal → faint neutral line; Change → amber (corresponds, but edited);
    // an add/remove points a short stub + dot toward whichever physical side holds
    // the content (so swap keeps it honest). Drawn per row so it rides the row's Y.
    auto make_connector = [](DiffRow::Kind kind, bool left_has, bool right_has) {
        auto* da = Gtk::make_managed<Gtk::DrawingArea>();
        da->set_content_width(26);
        da->set_valign(Gtk::Align::FILL);
        da->set_draw_func([kind, left_has, right_has](
                              const Cairo::RefPtr<Cairo::Context>& cr, int w, int h) {
            const double cy = h / 2.0;
            cr->set_line_width(1.5);
            switch (kind) {
                case DiffRow::Kind::Equal:  cr->set_source_rgba(0.42, 0.45, 0.53, 0.50); break;
                case DiffRow::Kind::Change: cr->set_source_rgba(0.91, 0.63, 0.23, 0.95); break;
                case DiffRow::Kind::Delete: cr->set_source_rgba(0.95, 0.55, 0.66, 0.90); break;
                case DiffRow::Kind::Insert: cr->set_source_rgba(0.65, 0.89, 0.63, 0.90); break;
            }
            if (left_has && right_has) {                 // a linked pair → full line
                cr->move_to(0, cy);
                cr->line_to(w, cy);
                cr->stroke();
            } else if (left_has) {                       // content only on the left
                cr->move_to(0, cy);
                cr->line_to(w / 2.0, cy);
                cr->stroke();
                cr->arc(w / 2.0, cy, 2.0, 0, 2 * M_PI);
                cr->fill();
            } else if (right_has) {                      // content only on the right
                cr->move_to(w / 2.0, cy);
                cr->line_to(w, cy);
                cr->stroke();
                cr->arc(w / 2.0, cy, 2.0, 0, 2 * M_PI);
                cr->fill();
            }
        });
        return da;
    };

    const int nrows = static_cast<int>(m_rows.size());
    for (int ri = 0; ri < nrows; ++ri) {
        const DiffRow& r = m_rows[static_cast<std::size_t>(ri)];
        const Side ls = left_side();
        const Side rs = right_side();
        const bool lh = side_lineno(r, ls) > 0;
        const bool rh = side_lineno(r, rs) > 0;
        auto* col0_no = make_no(side_lineno(r, ls));
        auto* col1_tx = make_text(side_markup(r, ls), nullptr);
        auto* col3_no = make_no(side_lineno(r, rs));
        auto* col4_tx = make_text(side_markup(r, rs), nullptr);
        m_grid.attach(*col0_no,                        0, ri);
        m_grid.attach(*col1_tx,                        1, ri);
        m_grid.attach(*make_connector(r.kind, lh, rh), 2, ri);
        m_grid.attach(*col3_no,                        3, ri);
        m_grid.attach(*col4_tx,                        4, ri);

        // Resolve which physical widgets hold which logical side (Swap-aware).
        Gtk::Label* snap_tx = (ls == Side::Snapshot) ? col1_tx : col4_tx;
        Gtk::Label* curr_tx = (ls == Side::Current)  ? col1_tx : col4_tx;
        Gtk::Label* curr_no = (ls == Side::Current)  ? col0_no : col3_no;

        // Snapshot side with text → pick SOURCE (whole paragraph on a plain click;
        // a drag-select is found directly by the harvest).
        if (side_lineno(r, Side::Snapshot) > 0) {
            m_snap_labels.emplace_back(ri, snap_tx);
            auto click = Gtk::GestureClick::create();
            const int captured = ri;
            click->signal_released().connect(
                [this, captured](int, double, double) {
                    m_last_clicked_row = captured;
                    m_pick_hint.set_markup(
                        std::string("<span foreground=\"") + kMuted +
                        "\">Snapshot paragraph picked as source</span>");
                });
            snap_tx->add_controller(click);
        }
        // Current side with text → pick DESTINATION (which paragraph the note lands
        // on). Clicking the Current paragraph sets it; the number turns green/bold.
        if (side_lineno(r, Side::Current) > 0) {
            m_curr_labels.emplace_back(ri, curr_no);
            auto dclick = Gtk::GestureClick::create();
            const int drow = ri;
            dclick->signal_released().connect(
                [this, drow](int, double, double) {
                    set_annotation_destination(drow);
                });
            curr_tx->add_controller(dclick);
        }
    }
    m_annotate_btn.set_sensitive(!m_snap_labels.empty());
    // Re-apply a previously chosen destination after a rebuild (swap / zoom), so
    // the highlight and target survive those context changes.
    if (m_dest_current_para >= 0) {
        for (const auto& e : m_curr_labels) {
            if (m_rows[static_cast<std::size_t>(e.first)].right_no - 1 ==
                m_dest_current_para) {
                set_annotation_destination(e.first);
                break;
            }
        }
    }

    // Stats — anchored to Current (removed from snapshot, added to reach current).
    int removed = 0, added = 0;
    for (const auto& r : m_rows) {
        if (r.kind == DiffRow::Kind::Delete) {
            removed += diff_count_words(r.left);
        } else if (r.kind == DiffRow::Kind::Insert) {
            added += diff_count_words(r.right);
        } else if (r.kind == DiffRow::Kind::Change) {
            for (const auto& op : r.left_ops)
                if (op.kind == DiffOp::Kind::Delete) removed += diff_count_words(op.text);
            for (const auto& op : r.right_ops)
                if (op.kind == DiffOp::Kind::Insert) added += diff_count_words(op.text);
        }
    }
    m_stats.set_markup(
        std::string("<span foreground=\"") + kRed + "\">\u2212" +
        std::to_string(removed) + " words removed</span>     <span foreground=\"" +
        kGreen + "\">+" + std::to_string(added) + " words added</span>");

    refresh_pane_headers();
    const std::string sn = snap.name.empty() ? "Snapshot" : snap.name;
    m_title.set_markup("<b>Diff</b>   \u00b7   " +
                       std::string(Glib::Markup::escape_text(sn).raw()) +
                       "   \u2192   Current");
}

// ─────────────────────────────────────────────────────────────────────────────
// annotate_current_from_pick — turn a picked bit of Snapshot text into an
// annotation on the matching Current paragraph. The Editor does the real char
// anchoring (it owns the buffer); we just resolve WHAT and WHICH-paragraph.
// ─────────────────────────────────────────────────────────────────────────────
void DiffView::annotate_current_from_pick() {
    if (!m_node) return;

    int pick_row = -1;
    std::string text;

    // 1) A live selection inside a snapshot paragraph wins (pick "part of old").
    for (const auto& entry : m_snap_labels) {
        Gtk::Label* lbl = entry.second;
        int a = 0, b = 0;
        if (lbl && lbl->get_selection_bounds(a, b) && b > a) {
            text = lbl->get_text().substr(
                       static_cast<Glib::ustring::size_type>(a),
                       static_cast<Glib::ustring::size_type>(b - a)).raw();
            pick_row = entry.first;
            break;
        }
    }
    // 2) Otherwise the last plainly-clicked snapshot paragraph → whole paragraph.
    if (pick_row < 0 && m_last_clicked_row >= 0) {
        for (const auto& entry : m_snap_labels) {
            if (entry.first == m_last_clicked_row) {
                pick_row = entry.first;
                text = m_rows[static_cast<std::size_t>(pick_row)].left;
                break;
            }
        }
    }
    if (pick_row < 0) {
        m_pick_hint.set_markup(
            std::string("<span foreground=\"") + kMuted +
            "\">Select text on the Snapshot side, or click a snapshot "
            "paragraph, first</span>");
        return;
    }

    // Trim so a stray edge-selection stays clean.
    const auto lo = text.find_first_not_of(" \t\r\n");
    const auto hi = text.find_last_not_of(" \t\r\n");
    if (lo == std::string::npos) {
        m_pick_hint.set_markup(
            std::string("<span foreground=\"") + kMuted +
            "\">Nothing picked</span>");
        return;
    }
    text = text.substr(lo, hi - lo + 1);

    // Destination: an explicitly chosen Current paragraph wins. Otherwise auto-
    // anchor to the source row's Current side (nearest Current paragraph above for
    // a delete-only row). Indices are 0-based, empty paragraphs excluded to match
    // SnapshotDiff::html_to_lines.
    int cur_para;
    if (m_dest_current_para >= 0) {
        cur_para = m_dest_current_para;
    } else {
        const DiffRow& pr = m_rows[static_cast<std::size_t>(pick_row)];
        cur_para = pr.right_no - 1;
        if (pr.right_no <= 0) {
            cur_para = 0;
            for (int k = pick_row - 1; k >= 0; --k) {
                const int rn = m_rows[static_cast<std::size_t>(k)].right_no;
                if (rn > 0) { cur_para = rn - 1; break; }
            }
        }
    }

    if (on_add_annotation)
        on_add_annotation(m_node, cur_para, text);

    m_pick_hint.set_markup(
        std::string("<span foreground=\"") + kGreen +
        "\">\u2713 Added to Current \u00b6" + std::to_string(cur_para + 1) +
        " \u00b7 review it in the editor</span>");
}

// ─────────────────────────────────────────────────────────────────────────────
// set_annotation_destination — mark which Current paragraph the note lands on.
// Re-marks the Current line numbers only (no grid rebuild), so a pending snapshot
// text selection on the other side is left untouched.
// ─────────────────────────────────────────────────────────────────────────────
void DiffView::set_annotation_destination(int chosen_row) {
    if (chosen_row < 0 || chosen_row >= static_cast<int>(m_rows.size()))
        return;
    const int no = m_rows[static_cast<std::size_t>(chosen_row)].right_no;
    if (no <= 0) return;   // not a real Current paragraph
    m_dest_current_para = no - 1;

    for (const auto& e : m_curr_labels) {
        const int rn = m_rows[static_cast<std::size_t>(e.first)].right_no;
        if (rn <= 0 || !e.second) continue;
        const bool on = (e.first == chosen_row);
        e.second->set_markup(
            m_size_open + "<span foreground=\"" +
            std::string(on ? kGreen : kMuted) +
            "\" font_family=\"monospace\"" + (on ? " weight=\"bold\"" : "") +
            ">" + std::to_string(rn) + "</span></span>");
    }
    m_pick_hint.set_markup(
        std::string("<span foreground=\"") + kGreen +
        "\">\u2192 lands on Current \u00b6" + std::to_string(no) +
        "  (click another Current paragraph to change)</span>");
}

} // namespace Folio
