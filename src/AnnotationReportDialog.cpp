// ─────────────────────────────────────────────────────────────────────────────
// Folio — AnnotationReportDialog.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "AnnotationReportDialog.hpp"
#include "NoteStep.hpp"                 // s108 §26 — the note stepper (phase + next action)
#include <gtkmm/checkbutton.h>
#include <gtkmm/stringlist.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/cssprovider.h>
#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace Folio {

namespace {
// Multiply every label under `w` by `scale` via a Pango scale attribute. The app
// CSS pins font-size per class, so a container-wide CSS size can't cascade past
// them; a scale attribute multiplies whatever Pango resolves. Empty list = 100%.
void scale_labels(Gtk::Widget& w, double scale) {
    for (Gtk::Widget* c = w.get_first_child(); c; c = c->get_next_sibling()) {
        if (auto* lbl = dynamic_cast<Gtk::Label*>(c)) {
            Pango::AttrList attrs;   // empty list clears scaling (back to 100%)
            if (scale != 1.0) {
                auto a = Pango::Attribute::create_attr_scale(scale);
                attrs.insert(a);
            }
            lbl->set_attributes(attrs);   // set_attributes takes a non-const lvalue
        }
        scale_labels(*c, scale);
    }
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────────

AnnotationReportDialog::AnnotationReportDialog(Gtk::Window& parent,
                                               DocumentModel& model,
                                               FolioPrefs& prefs)
    : m_model(model),
      m_prefs(prefs),
      m_scale(prefs.data_view_zoom_pct / 100.0),
      m_vbox(Gtk::Orientation::VERTICAL, 0),
      m_toolbar(Gtk::Orientation::HORIZONTAL, 8),
      m_list(Gtk::Orientation::VERTICAL, 0) {
    set_transient_for(parent);
    set_modal(false);
    set_title("Annotation Report");
    set_default_size(680, 640);
    set_resizable(true);
    // The Inspector keeps this dialog in a unique_ptr and reuses it (refresh() +
    // present()) each time the report is opened. Without hide-on-close, GTK4
    // DESTROYS the window on close while the pointer lives on, so the next
    // present() shows a destroyed window ("shown after it has been destroyed" ->
    // it then won't close). Hide instead, so the same instance re-presents cleanly
    // (matches BarcodeDialog / the reuse pattern in open_annotation_report).
    set_hide_on_close(true);
    build();
}

void AnnotationReportDialog::build() {
    // ── Toolbar ───────────────────────────────────────────────────────────────
    m_toolbar.add_css_class("folio-viewbar");
    m_toolbar.set_margin_start(8);
    m_toolbar.set_margin_end(8);

    auto* kind_lbl = Gtk::make_managed<Gtk::Label>("Filter:");
    kind_lbl->add_css_class("stat-label");
    m_toolbar.append(*kind_lbl);

    auto kind_items = Gtk::StringList::create({"All", "Writer", "Editor", "Proofreader"});
    m_filter_kind = Gtk::make_managed<Gtk::DropDown>(kind_items);
    m_filter_kind->set_selected(0);
    m_filter_kind->property_selected().signal_changed().connect(
        [this]() { rebuild_list(); });
    m_toolbar.append(*m_filter_kind);

    // s103 — source filter (multi-author annotations from editorial passes). The
    // model is filled by rebuild_source_filter(): item 0 = All, 1 = Mine (self /
    // legacy), 2..N = each distinct imported source. Built before the first
    // rebuild_list() so a selection is valid.
    auto src_items = Gtk::StringList::create({"All sources", "Mine"});
    m_filter_source = Gtk::make_managed<Gtk::DropDown>(src_items);
    m_filter_source->set_selected(0);
    m_filter_source->set_tooltip_text("Filter by who made the comment");
    m_filter_source->property_selected().signal_changed().connect(
        [this]() { rebuild_list(); });
    m_toolbar.append(*m_filter_source);

    // s107 — verdict filter for imported proposals. 0 = All, 1 = Proposed,
    // 2 = Accepted, 3 = Declined. "All" shows everything (self notes + every
    // verdict); the others narrow to proposals in that state, so picking
    // "Proposed" or "Accepted" is how declined notes tuck away.
    auto verdict_items = Gtk::StringList::create(
        {"All verdicts", "Proposed", "Accepted", "Declined"});
    m_filter_verdict = Gtk::make_managed<Gtk::DropDown>(verdict_items);
    m_filter_verdict->set_selected(0);
    m_filter_verdict->set_tooltip_text("Filter by the author's verdict on a proposal");
    m_filter_verdict->property_selected().signal_changed().connect(
        [this]() { rebuild_list(); });
    m_toolbar.append(*m_filter_verdict);

    auto* sort_lbl = Gtk::make_managed<Gtk::Label>("Sort:");
    sort_lbl->add_css_class("stat-label");
    m_toolbar.append(*sort_lbl);

    auto sort_items = Gtk::StringList::create({"Sidebar order", "Date (newest)", "Kind"});
    m_sort_dd = Gtk::make_managed<Gtk::DropDown>(sort_items);
    m_sort_dd->set_selected(0);
    m_sort_dd->property_selected().signal_changed().connect(
        [this]() { rebuild_list(); });
    m_toolbar.append(*m_sort_dd);

    m_search.set_placeholder_text("Search annotations…");
    m_search.set_hexpand(true);
    m_search.signal_search_changed().connect([this]() { rebuild_list(); });
    m_toolbar.append(m_search);

    // s108 §24 — resolved notes recede from the active list (archive is a VIEW
    // consequence, not a delete). This reveals them so they can be reviewed or
    // reopened; off by default so the working list shows only live conversations.
    m_show_resolved = Gtk::make_managed<Gtk::CheckButton>("Show resolved");
    m_show_resolved->set_tooltip_text(
        "Reveal resolved notes (they recede from the list when resolved; nothing is deleted)");
    m_show_resolved->set_name("report-show-resolved");
    m_show_resolved->signal_toggled().connect([this]() { rebuild_list(); });
    m_toolbar.append(*m_show_resolved);

    m_export_btn = Gtk::make_managed<Gtk::Button>("Export…");
    m_export_btn->add_css_class("flat");
    m_export_btn->set_tooltip_text("Copy report to clipboard as plain text");
    m_export_btn->signal_clicked().connect([this]() {
        std::ostringstream ss;
        ss << "ANNOTATION REPORT\n=================\n\n";
        std::function<void(std::vector<BinderNode>&, int)> walk =
            [&](std::vector<BinderNode>& nodes, int depth) {
                for (auto& node : nodes) {
                    if (!node.annotations.empty()) {
                        std::string indent(depth * 2, ' ');
                        ss << indent << "## " << node.title << "\n\n";
                        for (const auto& ann : node.annotations) {
                            ss << indent << "[" << ann.kind << "]";
                            if (!ann.created_at.empty())
                                ss << "  " << ann.created_at.substr(0, 10);
                            ss << "\n";
                            std::string ex = excerpt_from(&node,
                                ann.range_start, ann.range_end);
                            if (!ex.empty())
                                ss << indent << "  \u201c" << ex << "\u201d\n";
                            ss << indent << "  " << ann.text << "\n\n";
                        }
                    }
                    if (!node.children.empty())
                        walk(node.children, depth + 1);
                }
            };
        walk(m_model.root(Section::Manuscript), 0);
        auto clipboard = get_clipboard();
        if (clipboard) clipboard->set_text(ss.str());
        set_title("Annotation Report  (copied to clipboard)");
        Glib::signal_timeout().connect_once([this]() {
            set_title("Annotation Report"); }, 2000);
    });
    m_toolbar.append(*m_export_btn);

    // ── text zoom (webpage-style −/%/+); shared with the editorial ledger ──────
    auto* zoom = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    zoom->add_css_class("linked");

    m_zoom_out = Gtk::make_managed<Gtk::Button>("\u2212");   // minus sign
    m_zoom_out->add_css_class("flat");
    m_zoom_out->set_tooltip_text("Smaller text");
    m_zoom_out->signal_clicked().connect([this]() { nudge_scale(-0.10); });
    zoom->append(*m_zoom_out);

    m_zoom_reset = Gtk::make_managed<Gtk::Button>("100%");
    m_zoom_reset->add_css_class("flat");
    m_zoom_reset->set_tooltip_text("Reset text size");
    m_zoom_reset->signal_clicked().connect([this]() { nudge_scale(1.0 - m_scale); });
    zoom->append(*m_zoom_reset);

    m_zoom_in = Gtk::make_managed<Gtk::Button>("+");
    m_zoom_in->add_css_class("flat");
    m_zoom_in->set_tooltip_text("Larger text");
    m_zoom_in->signal_clicked().connect([this]() { nudge_scale(+0.10); });
    zoom->append(*m_zoom_in);

    m_toolbar.append(*zoom);

    // ── List area ─────────────────────────────────────────────────────────────
    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_scroll.set_vexpand(true);
    m_list.set_margin_top(8);
    m_list.set_margin_bottom(16);
    m_list.set_margin_start(16);
    m_list.set_margin_end(16);
    m_list.set_spacing(0);
    m_scroll.set_child(m_list);

    m_vbox.append(m_toolbar);
    auto* sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
    m_vbox.append(*sep);
    m_vbox.append(m_scroll);
    set_child(m_vbox);

    rebuild_source_filter();   // s103 — populate the source dropdown, then list
    rebuild_list();
}

// ─────────────────────────────────────────────────────────────────────────────
// excerpt_from — pull text from node content at char offsets
// The content is HTML so we strip tags to get plain text, then slice.
// ─────────────────────────────────────────────────────────────────────────────

std::string AnnotationReportDialog::excerpt_from(BinderNode* node,
                                                   int start, int end) const {
    if (!node || node->content.empty()) return "";
    // Strip HTML tags to get plain text
    std::string plain;
    plain.reserve(node->content.size());
    bool in_tag = false;
    for (unsigned char c : node->content) {
        if (c == '<') { in_tag = true; continue; }
        if (c == '>') { in_tag = false; continue; }
        if (!in_tag) {
            if (c == '\n') plain += ' ';
            else plain += (char)c;
        }
    }
    // Decode common HTML entities
    auto replace_all = [](std::string s, const std::string& from,
                           const std::string& to) {
        size_t p = 0;
        while ((p = s.find(from, p)) != std::string::npos) {
            s.replace(p, from.size(), to);
            p += to.size();
        }
        return s;
    };
    plain = replace_all(plain, "&amp;",  "&");
    plain = replace_all(plain, "&lt;",   "<");
    plain = replace_all(plain, "&gt;",   ">");
    plain = replace_all(plain, "&quot;", "\"");

    // start/end are CODEPOINT offsets (visible-text char offsets, s105 reanchor),
    // but `plain` is UTF-8 BYTES. Slicing by byte splits multibyte characters and
    // yields invalid UTF-8 that Pango rejects ("Invalid UTF-8 string passed to
    // pango_layout_set_text"). Walk lead bytes (a continuation byte is 10xxxxxx)
    // to map each codepoint offset to a byte offset, so the slice lands on
    // character boundaries. For pure-ASCII scenes byte == codepoint, so this is
    // identical to the old behaviour; it only differs where multibyte text exists.
    auto is_lead = [](unsigned char b) { return (b & 0xC0) != 0x80; };

    int cp_len = 0;
    for (unsigned char c : plain) if (is_lead(c)) ++cp_len;

    start = std::max(0, std::min(start, cp_len));
    end   = std::max(start, std::min(end, cp_len));
    if (start >= end) return "";

    // Byte offsets of codepoint `start` and codepoint `end`.
    size_t b_start = plain.size(), b_end = plain.size();
    int cp = 0;
    for (size_t i = 0; i < plain.size(); ++i) {
        if (is_lead((unsigned char)plain[i])) {
            if (cp == start) b_start = i;
            if (cp == end)   { b_end = i; break; }
            ++cp;
        }
    }
    std::string ex = plain.substr(b_start, b_end - b_start);
    // Trim leading/trailing ASCII whitespace (safe at the byte level).
    while (!ex.empty() && (ex.front() == ' ' || ex.front() == '\n')) ex.erase(0, 1);
    while (!ex.empty() && (ex.back()  == ' ' || ex.back()  == '\n')) ex.pop_back();

    // Truncate to 80 CODEPOINTS on a character boundary (a byte-80 cut could split
    // a multibyte character, re-introducing the invalid-UTF-8 warning).
    {
        int kept = 0; size_t cut = ex.size();
        for (size_t i = 0; i < ex.size(); ++i) {
            if (is_lead((unsigned char)ex[i])) {
                if (kept == 80) { cut = i; break; }
                ++kept;
            }
        }
        if (cut < ex.size()) ex = ex.substr(0, cut) + "…";
    }
    return ex;
}

// ─────────────────────────────────────────────────────────────────────────────
// rebuild_list
// ─────────────────────────────────────────────────────────────────────────────

void AnnotationReportDialog::rebuild_list() {
    while (auto* c = m_list.get_first_child()) m_list.remove(*c);

    std::string filter_kind;
    if (m_filter_kind) {
        guint sel = m_filter_kind->get_selected();
        static const char* kinds[] = {"", "Writer", "Editor", "Proofreader"};
        if (sel < 4) filter_kind = kinds[sel];
    }
    std::string search_text = m_search.get_text().lowercase();

    // s103 — source filter selection: 0 = All, 1 = Mine (self/legacy, empty
    // source), 2..N = m_sources[sel-2].
    int src_sel = m_filter_source ? static_cast<int>(m_filter_source->get_selected()) : 0;

    // s107 — verdict filter: 0 = All, 1 = Proposed, 2 = Accepted, 3 = Declined.
    int verdict_sel = m_filter_verdict ? static_cast<int>(m_filter_verdict->get_selected()) : 0;
    static const char* verdict_words[] = {"", "proposed", "accepted", "declined"};

    // Sort mode: 0=Binder order, 1=Date newest, 2=Kind
    guint sort_sel = m_sort_dd ? m_sort_dd->get_selected() : 0;

    // Collect all (node, annotation, depth) tuples from manuscript
    struct Entry {
        BinderNode* node;
        const Annotation* ann;
        int depth;
        std::string scene_title;
    };
    std::vector<Entry> entries;

    // Recursive walk preserving binder order and depth
    std::function<void(std::vector<BinderNode>&, int)> walk =
        [&](std::vector<BinderNode>& nodes, int depth) {
            for (auto& node : nodes) {
                for (const auto& ann : node.annotations) {
                    if (!filter_kind.empty() && ann.kind != filter_kind) continue;
                    if (src_sel == 1) {
                        if (!ann.source.empty()) continue;          // Mine = self/legacy only
                    } else if (src_sel >= 2) {
                        int idx = src_sel - 2;
                        if (idx >= static_cast<int>(m_sources.size()) ||
                            ann.source != m_sources[static_cast<std::size_t>(idx)])
                            continue;
                    }
                    if (verdict_sel >= 1) {   // narrow to proposals in one verdict state
                        if (ann.verdict != verdict_words[verdict_sel]) continue;
                    }
                    // §24 — resolved notes RECEDE from the active list (archive =
                    // view consequence, not delete). Hidden unless "Show resolved"
                    // is on. Effective state is author-authoritative over the log.
                    {
                        const bool resolved = resolution_resolved(ann.resolution_log);
                        const bool reveal = m_show_resolved && m_show_resolved->get_active();
                        if (resolved && !reveal) continue;
                    }
                    if (!search_text.empty()) {
                        std::string h = ann.text;
                        for (auto& c : h) c = std::tolower((unsigned char)c);
                        if (h.find(search_text) == std::string::npos) continue;
                    }
                    entries.push_back({&node, &ann, depth, node.title});
                }
                if (!node.children.empty())
                    walk(node.children, depth + 1);
            }
        };
    walk(m_model.root(Section::Manuscript), 0);

    // Sort if needed
    if (sort_sel == 1) {
        // Date newest first
        std::stable_sort(entries.begin(), entries.end(),
            [](const Entry& a, const Entry& b) {
                return a.ann->created_at > b.ann->created_at;
            });
    } else if (sort_sel == 2) {
        // Kind alphabetical
        std::stable_sort(entries.begin(), entries.end(),
            [](const Entry& a, const Entry& b) {
                return a.ann->kind < b.ann->kind;
            });
    }
    // sort_sel == 0: binder order — already in order from walk

    if (entries.empty()) {
        const bool no_filter_active =
            filter_kind.empty() && search_text.empty() &&
            src_sel == 0 && verdict_sel == 0;
        auto* empty = Gtk::make_managed<Gtk::Label>(
            no_filter_active
                ? "No annotations in this project."
                : "No annotations match the current filter.");
        empty->add_css_class("dim-label");
        empty->set_margin_top(40);
        empty->set_justify(Gtk::Justification::CENTER);
        m_list.append(*empty);
        apply_scale();
        return;
    }

    // Group by scene when in binder order; flat list otherwise
    bool group_by_scene = (sort_sel == 0);
    BinderNode* last_node = nullptr;

    for (const auto& e : entries) {
        if (group_by_scene && e.node != last_node) {
            last_node = e.node;
            // Scene/group heading with depth indent
            int indent = e.depth * 16;
            auto* heading = Gtk::make_managed<Gtk::Label>(e.scene_title);
            heading->add_css_class(e.depth == 0 ? "heading" : "caption-heading");
            heading->set_halign(Gtk::Align::START);
            heading->set_margin_start(indent + 2);
            heading->set_margin_top(e.depth == 0 ? 16 : 10);
            heading->set_margin_bottom(4);
            m_list.append(*heading);
            auto* hdiv = Gtk::make_managed<Gtk::Separator>(
                Gtk::Orientation::HORIZONTAL);
            hdiv->set_margin_start(indent);
            hdiv->set_margin_bottom(6);
            m_list.append(*hdiv);
        }

        int indent = group_by_scene ? e.depth * 16 : 0;

        // Card
        auto* card = Gtk::make_managed<Gtk::Box>(
            Gtk::Orientation::VERTICAL, 4);
        card->add_css_class("annotation-card");
        card->set_margin_bottom(8);
        card->set_margin_start(indent);
        // §24 — a resolved note only appears here when "Show resolved" is on; dim it
        // so its receded state reads at a glance (it's archived from the live list,
        // not deleted).
        if (resolution_resolved(e.ann->resolution_log)) card->set_opacity(0.55);

        // Header: dot + kind + scene title (when not grouped) + date
        auto* hdr = Gtk::make_managed<Gtk::Box>(
            Gtk::Orientation::HORIZONTAL, 8);
        hdr->set_margin_top(8);
        hdr->set_margin_start(10);
        hdr->set_margin_end(10);

        auto* dot = Gtk::make_managed<Gtk::Label>(" ");
        dot->set_size_request(10, 10);
        {
            auto css = Gtk::CssProvider::create();
            css->load_from_data(std::string("label { background:") +
                                e.ann->color_hex + "; border-radius:50%; }");
            dot->get_style_context()->add_provider(
                css, GTK_STYLE_PROVIDER_PRIORITY_USER);
        }
        dot->set_valign(Gtk::Align::CENTER);
        hdr->append(*dot);

        auto* kind_lbl = Gtk::make_managed<Gtk::Label>(e.ann->kind);
        kind_lbl->add_css_class("annotation-kind");
        kind_lbl->set_halign(Gtk::Align::START);
        kind_lbl->set_hexpand(!group_by_scene);
        hdr->append(*kind_lbl);

        // Show scene title inline when not grouped by scene
        if (!group_by_scene) {
            auto* scene_lbl = Gtk::make_managed<Gtk::Label>(
                "— " + e.scene_title);
            scene_lbl->add_css_class("dim-label");
            scene_lbl->set_halign(Gtk::Align::START);
            scene_lbl->set_hexpand(true);
            scene_lbl->set_ellipsize(Pango::EllipsizeMode::END);
            hdr->append(*scene_lbl);
        }

        // s107 — verdict chip for imported proposals (glyph + state word, per
        // §20.1). The state WORD is stored on the annotation; the glyph is
        // rendered here (a per-face concern): balloon (proposed) / ✓ (accepted) /
        // ✗ (declined). Self/legacy notes (verdict == "") get no chip.
        if (e.ann->is_proposal()) {
            const std::string& v = e.ann->verdict;
            auto* chip = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
            chip->add_css_class("verdict-chip");
            chip->set_hexpand(true);
            chip->set_halign(Gtk::Align::END);
            chip->set_valign(Gtk::Align::CENTER);

            const char* word = "Proposed";
            const char* colour = "#cba6f7";                 // mauve (proposed)
            if (v == Verdicts::kAccepted)      { word = "Acknowledged"; colour = "#a6e3a1"; }  // green
            else if (v == Verdicts::kDeclined) { word = "Declined"; colour = "#f38ba8"; }  // red

            if (v == Verdicts::kProposed) {
                auto* img = Gtk::make_managed<Gtk::Image>();
                img->set_from_icon_name("folio-proposal-symbolic");  // first consumer of the s106 icon
                img->set_valign(Gtk::Align::CENTER);
                auto icss = Gtk::CssProvider::create();              // symbolic -> tint to mauve
                icss->load_from_data(std::string("image { color:") + colour + "; }");
                img->get_style_context()->add_provider(icss, GTK_STYLE_PROVIDER_PRIORITY_USER);
                chip->append(*img);
            } else {
                auto* glyph = Gtk::make_managed<Gtk::Label>(
                    v == Verdicts::kAccepted ? "\u2713" : "\u2717");  // ✓ / ✗
                glyph->set_valign(Gtk::Align::CENTER);
                auto gcss = Gtk::CssProvider::create();
                gcss->load_from_data(std::string("label { color:") + colour +
                                     "; font-weight:bold; }");
                glyph->get_style_context()->add_provider(gcss, GTK_STYLE_PROVIDER_PRIORITY_USER);
                chip->append(*glyph);
            }

            auto* state_lbl = Gtk::make_managed<Gtk::Label>(word);
            state_lbl->add_css_class("annotation-kind");
            {
                auto ccss = Gtk::CssProvider::create();
                ccss->load_from_data(std::string("label { color:") + colour + "; }");
                state_lbl->get_style_context()->add_provider(ccss, GTK_STYLE_PROVIDER_PRIORITY_USER);
            }
            chip->append(*state_lbl);
            hdr->append(*chip);
        }

        if (!e.ann->created_at.empty()) {
            auto* date = Gtk::make_managed<Gtk::Label>(
                e.ann->created_at.substr(0, 10));
            date->add_css_class("dim-label");
            hdr->append(*date);
        }
        card->append(*hdr);

        // Excerpt
        std::string ex = excerpt_from(e.node, e.ann->range_start, e.ann->range_end);
        if (!ex.empty()) {
            auto* ex_lbl = Gtk::make_managed<Gtk::Label>(
                "\u201c" + ex + "\u201d");
            ex_lbl->add_css_class("dim-label");
            ex_lbl->set_wrap(true);
            ex_lbl->set_xalign(0.0f);
            ex_lbl->set_margin_start(10);
            ex_lbl->set_margin_end(10);
            ex_lbl->set_margin_top(2);
            card->append(*ex_lbl);
        }

        // Comment
        auto* comment = Gtk::make_managed<Gtk::Label>(e.ann->text);
        comment->set_wrap(true);
        comment->set_xalign(0.0f);
        comment->set_margin_start(10);
        comment->set_margin_end(10);
        comment->set_margin_bottom(8);
        card->append(*comment);

        // §26 — the note stepper: one derived line telling you WHERE the note is in
        // the initiate -> loop -> end journey and WHAT TO DO NEXT (imperative). It
        // subsumes the raw resolution marker; a Done note recedes (below) so it never
        // reaches here except under "Show resolved". (lap count wiring is a follow-on.)
        if (e.ann->is_proposal()) {
            const ResolutionState rs = resolution_state(e.ann->resolution_log);
            const NoteStep step = Folio::note_step(
                e.ann->verdict,
                static_cast<folioedit::ResolutionState>(static_cast<int>(rs)), 0);

            auto* badge = Gtk::make_managed<Gtk::Label>(step.badge);
            badge->add_css_class("annotation-kind");
            badge->set_halign(Gtk::Align::START);
            badge->set_margin_start(10);
            card->append(*badge);

            if (!step.next_action.empty()) {
                auto* na = Gtk::make_managed<Gtk::Label>(step.next_action);
                na->add_css_class("dim-label");
                na->set_halign(Gtk::Align::START);
                na->set_margin_start(10);
                na->set_margin_bottom(4);
                na->set_wrap(true);
                na->set_xalign(0.0f);
                card->append(*na);
            }
        }

        // s107 — verdict actions for imported proposals. Accept/Decline record the
        // author's decision (never touching the prose — §5/§13). The button
        // matching the current verdict is shown active + insensitive; the other
        // flips it in one click (v1 = a single verdict, re-choosable, no threading
        // — §20.5). Self/legacy notes get no action row (they keep plain delete).
        if (e.ann->is_proposal()) {
            BinderNode* vnode = e.node;
            int vaid = e.ann->id;
            const std::string& v = e.ann->verdict;

            auto* act = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
            act->set_halign(Gtk::Align::END);
            act->set_margin_end(10);
            act->set_margin_bottom(8);

            auto* accept = Gtk::make_managed<Gtk::Button>("\u2713 Acknowledge");
            accept->add_css_class("flat");
            accept->set_tooltip_text("Acknowledge this note \u2014 you'll correct the text, then send back");
            if (v == Verdicts::kAccepted) {
                accept->add_css_class("suggested-action");
                accept->set_sensitive(false);
            }
            accept->signal_clicked().connect([this, vnode, vaid]() {
                m_model.set_annotation_verdict(vnode, vaid, Verdicts::kAccepted);
                rebuild_list();
            });

            auto* decline = Gtk::make_managed<Gtk::Button>("\u2717 Decline");
            decline->add_css_class("flat");
            decline->set_tooltip_text("Author rejects this note (prose unchanged; the \u201cno\u201d is on record)");
            if (v == Verdicts::kDeclined) {
                decline->add_css_class("destructive-action");
                decline->set_sensitive(false);
            }
            decline->signal_clicked().connect([this, vnode, vaid]() {
                m_model.set_annotation_verdict(vnode, vaid, Verdicts::kDeclined);
                rebuild_list();
            });

            act->append(*accept);
            act->append(*decline);

            // §25 — Resolve is a TWO-KEY handshake: the author's click sets only the
            // author's half; the note is fully Resolved (and recedes) only when the
            // editor agrees too. A half state is shown and stays in the list. The
            // author's REOPEN is authoritative (clears the editor's half); the author
            // can confirm the editor's half to reach Resolved.
            const ResolutionState rstate = resolution_state(e.ann->resolution_log);
            const bool author_in = (rstate == ResolutionState::HalfAuthor ||
                                    rstate == ResolutionState::Resolved);
            const char* rlabel = author_in ? "Reopen"
                               : (rstate == ResolutionState::HalfEditor ? "Confirm"
                                                                        : "Resolve");
            auto* resolve = Gtk::make_managed<Gtk::Button>(rlabel);
            resolve->add_css_class("flat");
            resolve->set_tooltip_text(
                author_in ? "Withdraw your resolve \u2014 the note reopens (you can always reopen)"
              : rstate == ResolutionState::HalfEditor
                    ? "The editor resolved this \u2014 confirm to fully resolve (both agree)"
                    : "Resolve from your side \u2014 it needs the editor too before it archives");
            resolve->signal_clicked().connect([this, vnode, vaid, author_in]() {
                // author_in -> reopen (false); otherwise put the author's key in (true)
                m_model.set_annotation_resolution(vnode, vaid, !author_in);
                rebuild_list();
            });
            act->append(*resolve);
            card->append(*act);
        }

        m_list.append(*card);
    }

    apply_scale();
}

// ── text zoom ────────────────────────────────────────────────────────────────
void AnnotationReportDialog::apply_scale() {
    scale_labels(m_list, m_scale);
    if (m_zoom_reset)
        m_zoom_reset->set_label(
            std::to_string(static_cast<int>(std::lround(m_scale * 100.0))) + "%");
}

void AnnotationReportDialog::nudge_scale(double delta) {
    const double next = std::clamp(m_scale + delta, 0.5, 3.0);
    if (next == m_scale) return;
    m_scale = next;
    apply_scale();
    m_prefs.data_view_zoom_pct = static_cast<int>(m_scale * 100.0 + 0.5);
}

void AnnotationReportDialog::rebuild_source_filter() {
    if (!m_filter_source) return;

    // Remember the current selection by value so a rebuild keeps the user's pick.
    std::string keep;                          // "" = All, "\x01" = Mine, else a source
    guint cur = m_filter_source->get_selected();
    if (cur == 1) keep = "\x01";
    else if (cur >= 2 && (cur - 2) < m_sources.size())
        keep = m_sources[cur - 2];

    // Collect the distinct non-empty sources present across manuscript annotations.
    std::set<std::string> found;
    std::function<void(std::vector<BinderNode>&)> walk =
        [&](std::vector<BinderNode>& nodes) {
            for (auto& n : nodes) {
                for (const auto& a : n.annotations)
                    if (!a.source.empty()) found.insert(a.source);
                if (!n.children.empty()) walk(n.children);
            }
        };
    walk(m_model.root(Section::Manuscript));
    m_sources.assign(found.begin(), found.end());

    std::vector<Glib::ustring> items = {"All sources", "Mine"};
    for (const auto& s : m_sources) items.push_back(s);
    m_filter_source->set_model(Gtk::StringList::create(items));

    guint want = 0;
    if (keep == "\x01") want = 1;
    else if (!keep.empty()) {
        for (std::size_t i = 0; i < m_sources.size(); ++i)
            if (m_sources[i] == keep) { want = static_cast<guint>(i + 2); break; }
    }
    m_filter_source->set_selected(want);
}

void AnnotationReportDialog::refresh() {
    rebuild_source_filter();
    rebuild_list();
}

} // namespace Folio
