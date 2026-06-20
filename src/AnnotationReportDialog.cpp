// ─────────────────────────────────────────────────────────────────────────────
// Folio — AnnotationReportDialog.cpp
// ─────────────────────────────────────────────────────────────────────────────

#include "AnnotationReportDialog.hpp"
#include <gtkmm/stringlist.h>
#include <gtkmm/gestureclick.h>
#include <gtkmm/cssprovider.h>
#include <algorithm>
#include <sstream>

namespace Folio {

// ─────────────────────────────────────────────────────────────────────────────

AnnotationReportDialog::AnnotationReportDialog(Gtk::Window& parent,
                                               DocumentModel& model)
    : m_model(model),
      m_vbox(Gtk::Orientation::VERTICAL, 0),
      m_toolbar(Gtk::Orientation::HORIZONTAL, 8),
      m_list(Gtk::Orientation::VERTICAL, 0) {
    set_transient_for(parent);
    set_modal(false);
    set_title("Annotation Report");
    set_default_size(680, 640);
    set_resizable(true);
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

    // Clamp to valid range
    int len = (int)plain.size();
    start = std::max(0, std::min(start, len));
    end   = std::max(start, std::min(end, len));
    if (start >= end) return "";

    std::string ex = plain.substr(start, end - start);
    // Trim whitespace
    while (!ex.empty() && (ex.front() == ' ' || ex.front() == '\n')) ex.erase(0, 1);
    while (!ex.empty() && (ex.back()  == ' ' || ex.back()  == '\n')) ex.pop_back();
    if (ex.size() > 80) ex = ex.substr(0, 80) + "…";
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
        auto* empty = Gtk::make_managed<Gtk::Label>(
            filter_kind.empty() && search_text.empty()
                ? "No annotations in this project."
                : "No annotations match the current filter.");
        empty->add_css_class("dim-label");
        empty->set_margin_top(40);
        empty->set_justify(Gtk::Justification::CENTER);
        m_list.append(*empty);
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

        m_list.append(*card);
    }
}

void AnnotationReportDialog::refresh() {
    rebuild_list();
}

} // namespace Folio
