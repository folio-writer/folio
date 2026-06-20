// ─────────────────────────────────────────────────────────────────────────────
// Folio — PrintDialog.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "PrintDialog.hpp"
#include "ReportEngine.hpp"
#include "SearchEngine.hpp"
#include <FolioLog.hpp>
#include <cstdlib>
#include <sstream>
#include <cmath>

namespace Folio {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
PrintDialog::PrintDialog(Gtk::Window& parent, DocumentModel& model,
                         FolioPrefs& prefs)
    : Gtk::Window(), m_model(model), m_prefs(prefs)
{
    set_transient_for(parent);
    set_modal(true);
    set_title("Print Manuscript");
    set_default_size(820, 600);
    set_resizable(true);

    // Escape closes
    auto kc = Gtk::EventControllerKey::create();
    kc->signal_key_pressed().connect([this](guint kv, guint, Gdk::ModifierType) -> bool {
        if (kv == GDK_KEY_Escape) { close(); return true; }
        return false;
    }, false);
    add_controller(kc);

    // CSS — reuse export styles + print-specific additions
    auto css = Gtk::CssProvider::create();
    css->load_from_data(R"(
        .export-section-title {
            font-size: 11px; font-weight: 700; letter-spacing: 0.06em;
            color: alpha(currentColor, 0.5); text-transform: uppercase;
            padding: 4px 0 2px 0;
        }
        .export-row { padding: 4px 0; }
        .export-row-label { font-size: 13px; color: alpha(currentColor, 0.85); }
        .export-footer {
            padding: 10px 14px;
            border-top: 1px solid alpha(currentColor, 0.10);
        }
        .export-list-toolbar {
            padding: 6px 10px;
            border-bottom: 1px solid alpha(currentColor, 0.10);
        }
        .export-scene-row {
            padding: 3px 10px;
            min-height: 28px;
        }
        .export-scene-row:hover { background-color: alpha(currentColor, 0.04); }
        .export-group-label {
            font-size: 11px; font-weight: 700;
            color: alpha(currentColor, 0.5);
            text-transform: uppercase;
        }
        .export-status { font-size: 12px; color: alpha(currentColor, 0.6); }
        .print-mode-bar {
            padding: 8px 10px 6px;
            border-bottom: 1px solid alpha(currentColor, 0.10);
        }
    )");
    get_style_context()->add_provider_for_display(
        get_display(), css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);

    // ── Mode bar ──────────────────────────────────────────────────────────────
    m_mode_bar.add_css_class("print-mode-bar");
    m_mode_manuscript.set_label("Manuscript");
    m_mode_manuscript.set_active(true);
    m_mode_report.set_label("Project Report");
    m_mode_report.set_group(m_mode_manuscript);
    m_mode_manuscript.signal_toggled().connect([this]() { on_mode_changed(); });
    m_mode_bar.append(m_mode_manuscript);
    m_mode_bar.append(m_mode_report);
    m_mode_bar.set_spacing(16);

    build_scene_list();
    build_settings();
    build_report_settings();
    build_footer();

    // Left panel
    m_left.set_size_request(280, -1);
    m_left.append(m_mode_bar);
    m_left.append(m_list_toolbar);
    m_list_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_list_scroll.set_vexpand(true);
    m_list_scroll.set_child(m_list_box);
    m_left.append(m_list_scroll);

    // Right panel — stack manuscript settings and report settings; show/hide via on_mode_changed
    m_settings_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_settings_scroll.set_vexpand(true);
    m_settings_scroll.set_child(m_settings_box);  // default: manuscript
    m_settings_box.set_margin_start(16);
    m_settings_box.set_margin_end(16);
    m_settings_box.set_margin_top(12);
    m_settings_box.set_margin_bottom(12);
    m_report_settings_box.set_margin_start(16);
    m_report_settings_box.set_margin_end(16);
    m_report_settings_box.set_margin_top(12);
    m_report_settings_box.set_margin_bottom(12);
    m_right.append(m_settings_scroll);

    m_paned.set_start_child(m_left);
    m_paned.set_end_child(m_right);
    m_paned.set_position(280);
    m_paned.set_vexpand(true);

    m_root.append(m_paned);
    m_root.append(m_footer);
    set_child(m_root);
}

// ─────────────────────────────────────────────────────────────────────────────
// Mode switching
// ─────────────────────────────────────────────────────────────────────────────
void PrintDialog::on_mode_changed() {
    bool report_mode = m_mode_report.get_active();

    // Swap the settings panel child
    if (report_mode) {
        m_settings_scroll.set_child(m_report_settings_box);
        set_title("Print Project Report");
    } else {
        m_settings_scroll.set_child(m_settings_box);
        set_title("Print Manuscript");
    }

    // Dim the scene checklist in report mode — it isn't used
    m_list_toolbar.set_sensitive(!report_mode);
    m_list_scroll.set_sensitive(!report_mode);
    m_list_toolbar.set_opacity(report_mode ? 0.35 : 1.0);
    m_list_scroll.set_opacity(report_mode ? 0.35 : 1.0);
}

void PrintDialog::build_report_settings() {
    m_report_settings_box.append(*make_section("Report Options"));

    m_chk_report_dark.set_label("Dark mode (dark background, light text)");
    m_chk_report_dark.set_active(false);
    m_report_settings_box.append(m_chk_report_dark);

    // Paper / orientation — reuse the same widgets; they live in m_settings_box
    // but the page setup is applied from on_print regardless of mode.
    auto* note = Gtk::make_managed<Gtk::Label>(
        "Paper size and orientation are set in the system print dialog.\n"
        "The report prints all 15 project sections.");
    note->set_wrap(true);
    note->set_halign(Gtk::Align::START);
    note->add_css_class("export-status");
    note->set_margin_top(8);
    m_report_settings_box.append(*note);
}

// ─────────────────────────────────────────────────────────────────────────────
// Scene list — identical pattern to ExportDialog
// ─────────────────────────────────────────────────────────────────────────────
void PrintDialog::add_nodes_recursive(const std::vector<BinderNode>& nodes, int depth) {
    for (const auto& n : nodes) {
        bool is_group = binder_kind_is_group(n.kind);
        SceneRow row;
        row.node     = &n;
        row.is_group = is_group;
        row.depth    = depth;

        auto* hbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        hbox->add_css_class("export-scene-row");
        hbox->set_margin_start(depth * 16);

        if (is_group) {
            auto* lbl = Gtk::make_managed<Gtk::Label>(n.title.empty() ? "Group" : n.title);
            lbl->add_css_class("export-group-label");
            lbl->set_halign(Gtk::Align::START);
            lbl->set_hexpand(true);
            hbox->append(*lbl);
            row.check = nullptr;
        } else {
            auto* chk = Gtk::make_managed<Gtk::CheckButton>();
            chk->set_active(n.include_in_export);
            chk->signal_toggled().connect([this]() { update_sel_count(); });
            row.check = chk;

            auto* lbl = Gtk::make_managed<Gtk::Label>(n.title.empty() ? "Untitled" : n.title);
            lbl->set_halign(Gtk::Align::START);
            lbl->set_hexpand(true);
            lbl->set_ellipsize(Pango::EllipsizeMode::END);

            int wc = n.word_count();
            auto* wlbl = Gtk::make_managed<Gtk::Label>(
                wc > 0 ? std::to_string(wc) + "w" : "—");
            wlbl->add_css_class("export-status");

            hbox->append(*chk);
            hbox->append(*lbl);
            hbox->append(*wlbl);
        }

        m_list_box.append(*hbox);
        m_rows.push_back(row);

        if (!n.children.empty())
            add_nodes_recursive(n.children, depth + 1);
    }
}

void PrintDialog::build_scene_list() {
    m_list_toolbar.add_css_class("export-list-toolbar");
    m_btn_all.set_label("All");
    m_btn_all.add_css_class("flat");
    m_btn_all.signal_clicked().connect([this]() {
        for (auto& r : m_rows) if (r.check) r.check->set_active(true);
        update_sel_count();
    });
    m_btn_none.set_label("None");
    m_btn_none.add_css_class("flat");
    m_btn_none.signal_clicked().connect([this]() {
        for (auto& r : m_rows) if (r.check) r.check->set_active(false);
        update_sel_count();
    });
    m_sel_count_lbl.add_css_class("export-status");
    m_sel_count_lbl.set_halign(Gtk::Align::END);
    m_sel_count_lbl.set_hexpand(true);

    m_list_toolbar.append(m_btn_all);
    m_list_toolbar.append(m_btn_none);
    m_list_toolbar.append(m_sel_count_lbl);

    // Helper: add a section divider label between binder sections
    auto add_section_divider = [this](const std::string& label) {
        auto* hbox = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
        auto* lbl  = Gtk::make_managed<Gtk::Label>(label);
        lbl->add_css_class("export-section-title");
        lbl->set_halign(Gtk::Align::START);
        lbl->set_margin_start(10);
        lbl->set_margin_top(6);
        lbl->set_margin_bottom(2);
        hbox->append(*lbl);
        m_list_box.append(*hbox);
    };

    add_section_divider("Manuscript");
    add_nodes_recursive(m_model.root(Section::Manuscript), 0);

    if (!m_model.root(Section::Characters).empty()) {
        add_section_divider("Characters");
        add_nodes_recursive(m_model.root(Section::Characters), 0);
    }
    if (!m_model.root(Section::Places).empty()) {
        add_section_divider("Places");
        add_nodes_recursive(m_model.root(Section::Places), 0);
    }
    if (!m_model.root(Section::References).empty()) {
        add_section_divider("References");
        add_nodes_recursive(m_model.root(Section::References), 0);
    }
    if (!m_model.root(Section::Templates).empty()) {
        add_section_divider("Templates");
        add_nodes_recursive(m_model.root(Section::Templates), 0);
    }

    update_sel_count();
}

void PrintDialog::update_sel_count() {
    int total = 0, sel = 0;
    for (const auto& r : m_rows) {
        if (!r.check) continue;
        ++total;
        if (r.check->get_active()) ++sel;
    }
    m_sel_count_lbl.set_text(std::to_string(sel) + " / " +
                              std::to_string(total) + " items");
}

std::vector<Exporter::SourceNode> PrintDialog::collect_selected_nodes() const {
    auto meaningful_content = [](const std::string& html) -> std::string {
        if (html.empty()) return {};
        std::string plain;
        bool in_tag = false;
        for (char c : html) {
            if (c == '<') { in_tag = true; continue; }
            if (c == '>') { in_tag = false; continue; }
            if (!in_tag) plain += c;
        }
        for (char c : plain)
            if (!std::isspace((unsigned char)c)) return html;
        return {};
    };

    std::vector<Exporter::SourceNode> result;
    for (const auto& row : m_rows) {
        if (row.is_group) {
            Exporter::SourceNode sn;
            sn.title        = row.node->title;
            sn.html_content = meaningful_content(row.node->content);
            sn.is_group     = true;
            sn.depth        = row.depth;
            result.push_back(sn);
        } else {
            if (!row.check || !row.check->get_active()) continue;
            Exporter::SourceNode sn;
            sn.title        = row.node->title;
            sn.html_content = meaningful_content(row.node->content);
            sn.is_group     = false;
            sn.depth        = row.depth;
            result.push_back(sn);
        }
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Settings panel
// ─────────────────────────────────────────────────────────────────────────────
Gtk::Widget* PrintDialog::make_section(const std::string& title) {
    auto* lbl = Gtk::make_managed<Gtk::Label>(title);
    lbl->add_css_class("export-section-title");
    lbl->set_halign(Gtk::Align::START);
    return lbl;
}

Gtk::Widget* PrintDialog::make_row(const std::string& label, Gtk::Widget& widget) {
    auto* row  = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto* lbl  = Gtk::make_managed<Gtk::Label>(label);
    lbl->add_css_class("export-row-label");
    lbl->set_halign(Gtk::Align::START);
    lbl->set_hexpand(true);
    row->add_css_class("export-row");
    row->append(*lbl);
    row->append(widget);
    return row;
}

void PrintDialog::build_settings() {
    // ── Formatting ────────────────────────────────────────────────────────────
    m_settings_box.append(*make_section("Formatting"));

    m_radio_asis.set_label("Use as-is");
    m_radio_asis.set_active(true);
    m_radio_normalise.set_label("Normalise to defaults (Courier 12pt, double-spaced, 1\" margins)");
    m_radio_normalise.set_group(m_radio_asis);
    m_settings_box.append(m_radio_asis);
    m_settings_box.append(m_radio_normalise);

    // ── Groups ────────────────────────────────────────────────────────────────
    m_settings_box.append(*make_section("Groups"));

    auto heading_model = Gtk::StringList::create({"As-is", "Auto-number", "None"});
    m_group_heading_dd = Gtk::make_managed<Gtk::DropDown>(heading_model);
    m_group_heading_dd->set_selected(0);
    m_settings_box.append(*make_row("Group headings", *m_group_heading_dd));

    m_chk_page_break.set_label("Page break between groups");
    m_chk_page_break.set_active(false);
    m_settings_box.append(m_chk_page_break);

    // ── Separator ─────────────────────────────────────────────────────────────
    m_settings_box.append(*make_section("Scene Separator"));
    m_sep_entry.set_text("* * *");
    m_sep_entry.set_max_width_chars(20);
    m_settings_box.append(*make_row("Separator text", m_sep_entry));

    // ── Header / Footer ───────────────────────────────────────────────────────
    m_settings_box.append(*make_section("Header & Footer"));
    m_chk_header.set_label("Header: title (left) + author (right)");
    m_chk_header.set_active(true);
    m_chk_footer.set_label("Footer: page number (centre)");
    m_chk_footer.set_active(true);
    m_settings_box.append(m_chk_header);
    m_settings_box.append(m_chk_footer);

    // ── Paper ─────────────────────────────────────────────────────────────────
    m_settings_box.append(*make_section("Paper"));

    auto paper_model = Gtk::StringList::create({"Letter (8.5\" × 11\")", "A4 (210 × 297 mm)", "Legal (8.5\" × 14\")"});
    m_paper_dd = Gtk::make_managed<Gtk::DropDown>(paper_model);
    m_paper_dd->set_selected(0);
    m_settings_box.append(*make_row("Paper size", *m_paper_dd));

    auto orient_model = Gtk::StringList::create({"Portrait", "Landscape"});
    m_orientation_dd = Gtk::make_managed<Gtk::DropDown>(orient_model);
    m_orientation_dd->set_selected(0);
    m_settings_box.append(*make_row("Orientation", *m_orientation_dd));
}

// ─────────────────────────────────────────────────────────────────────────────
// Footer
// ─────────────────────────────────────────────────────────────────────────────
void PrintDialog::build_footer() {
    m_footer.add_css_class("export-footer");

    m_btn_print.set_label("Print…");
    m_btn_print.add_css_class("suggested-action");
    m_btn_print.signal_clicked().connect([this]() { on_print(); });

    m_btn_cancel.set_label("Cancel");
    m_btn_cancel.signal_clicked().connect([this]() { close(); });

    m_status_lbl.set_halign(Gtk::Align::START);
    m_status_lbl.set_hexpand(true);
    m_status_lbl.add_css_class("export-status");

    m_footer.append(m_status_lbl);
    m_footer.append(m_btn_cancel);
    m_footer.append(m_btn_print);
}

// ─────────────────────────────────────────────────────────────────────────────
// Paginate — build m_pages vector
// ─────────────────────────────────────────────────────────────────────────────
void PrintDialog::paginate(const Glib::RefPtr<Gtk::PrintContext>& ctx,
                            const std::vector<Exporter::SourceNode>& nodes,
                            Gtk::PrintOperation& op)
{
    m_pages.clear();

    double page_height = ctx->get_height();
    bool   normalise   = m_radio_normalise.get_active();

    // Normalised: Courier 12pt double-spaced, 1" margins (72pt each side)
    double margin      = normalise ? 72.0 : 36.0;  // pts
    double usable_h    = page_height - margin * 2.0;
    double line_height = normalise ? 24.0 : 16.0;  // pts (12pt * 2 = 24)
    // Reserve header/footer space
    if (m_chk_header.get_active()) usable_h -= line_height * 1.5;
    if (m_chk_footer.get_active()) usable_h -= line_height * 1.5;
    if (usable_h < line_height) usable_h = line_height;

    std::string sep_text = m_sep_entry.get_text();
    bool page_break_groups = m_chk_page_break.get_active();
    guint heading_style = m_group_heading_dd ? m_group_heading_dd->get_selected() : 0;

    // Collect paragraphs into a flat list of PagedPara
    std::vector<PagedPara> all_paras;
    int group_counter = 0;
    bool first_group  = true;

    for (const auto& node : nodes) {
        if (node.is_group) {
            ++group_counter;
            bool force_break = page_break_groups && !first_group;
            first_group = false;

            std::string heading;
            if (heading_style == 0)      heading = node.title;  // as-is
            else if (heading_style == 1) heading = "Chapter " + std::to_string(group_counter);
            // style 2 = none: no heading emitted

            if (heading_style != 2 && !heading.empty()) {
                PagedPara pp;
                pp.text       = heading;
                pp.is_heading = true;
                pp.page_break = force_break;
                all_paras.push_back(pp);
                force_break = false;
            }

            // Group body text (preface)
            if (!node.html_content.empty()) {
                std::string plain = SearchEngine::html_to_plain(node.html_content);
                if (!plain.empty()) {
                    std::istringstream iss(plain);
                    std::string line;
                    bool first_line = true;
                    while (std::getline(iss, line)) {
                        if (line.empty()) continue;
                        PagedPara pp;
                        pp.text       = line;
                        pp.page_break = first_line ? force_break : false;
                        first_line    = false;
                        all_paras.push_back(pp);
                    }
                }
            }
        } else {
            // Scene — strip HTML, split into paragraphs
            std::string plain = SearchEngine::html_to_plain(node.html_content);
            std::istringstream iss(plain);
            std::string line;
            [[maybe_unused]] bool first_scene_para = true;
            while (std::getline(iss, line)) {
                if (line.empty()) continue;
                PagedPara pp;
                pp.text = line;
                all_paras.push_back(pp);
                first_scene_para = false;
            }
            // Add separator after scene (not after last)
            if (!sep_text.empty()) {
                PagedPara sep;
                sep.text = sep_text;
                all_paras.push_back(sep);
            }
        }
    }
    // Remove trailing separator if present
    if (!all_paras.empty() && all_paras.back().text == sep_text)
        all_paras.pop_back();

    // Distribute paragraphs across pages
    std::vector<PagedPara> current_page;
    double y_used = 0.0;

    for (auto& para : all_paras) {
        double para_h = para.is_heading ? line_height * 2.0 : line_height;

        bool need_new_page = (para.page_break && !current_page.empty())
                          || (y_used + para_h > usable_h && !current_page.empty());
        if (need_new_page) {
            m_pages.push_back(current_page);
            current_page.clear();
            y_used = 0.0;
        }
        current_page.push_back(para);
        y_used += para_h + (para.is_heading ? line_height * 0.5 : 4.0);
    }
    if (!current_page.empty())
        m_pages.push_back(current_page);

    if (m_pages.empty())
        m_pages.push_back({}); // at least one page

    op.set_n_pages((int)m_pages.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// draw_page — render one page via Cairo/Pango
// ─────────────────────────────────────────────────────────────────────────────
void PrintDialog::draw_page(const Glib::RefPtr<Gtk::PrintContext>& ctx,
                             int page_nr,
                             const std::string& title,
                             const std::string& author,
                             int total_pages)
{
    auto cr     = ctx->get_cairo_context();
    double pw   = ctx->get_width();
    double ph   = ctx->get_height();
    bool normalise = m_radio_normalise.get_active();
    double margin  = normalise ? 72.0 : 36.0;
    double line_h  = normalise ? 24.0 : 16.0;
    double font_pt = normalise ? 12.0 : 10.0;
    double small_pt = 9.0;

    cr->set_source_rgb(0, 0, 0);

    auto make_layout = [&](const std::string& font_desc_str) {
        auto layout = ctx->create_pango_layout();
        auto fd     = Pango::FontDescription(font_desc_str);
        layout->set_font_description(fd);
        layout->set_width(PANGO_SCALE * (pw - margin * 2));
        layout->set_wrap(Pango::WrapMode::WORD_CHAR);
        return layout;
    };

    std::string body_font  = normalise
        ? ("Courier " + std::to_string((int)font_pt))
        : ("Sans "    + std::to_string((int)font_pt));
    std::string small_font = "Sans " + std::to_string((int)small_pt);

    // ── Header ────────────────────────────────────────────────────────────────
    if (m_chk_header.get_active()) {
        double hy = margin - line_h * 1.5;

        // Title left
        auto title_layout = make_layout(small_font);
        title_layout->set_text(title.empty() ? "Untitled" : title);
        title_layout->set_alignment(Pango::Alignment::LEFT);
        cr->move_to(margin, hy);
        title_layout->show_in_cairo_context(cr);

        // Author right
        if (!author.empty()) {
            auto auth_layout = make_layout(small_font);
            auth_layout->set_text(author);
            auth_layout->set_alignment(Pango::Alignment::RIGHT);
            cr->move_to(margin, hy);
            auth_layout->show_in_cairo_context(cr);
        }

        // Thin separator line
        cr->set_line_width(0.5);
        cr->move_to(margin, hy + line_h + 2);
        cr->line_to(pw - margin, hy + line_h + 2);
        cr->stroke();
    }

    // ── Footer ────────────────────────────────────────────────────────────────
    if (m_chk_footer.get_active()) {
        double fy = ph - margin + 4.0;
        auto pg_layout = make_layout(small_font);
        pg_layout->set_text("— " + std::to_string(page_nr + 1) + " of " +
                             std::to_string(total_pages) + " —");
        pg_layout->set_alignment(Pango::Alignment::CENTER);
        cr->move_to(margin, fy);
        pg_layout->show_in_cairo_context(cr);
    }

    // ── Body ──────────────────────────────────────────────────────────────────
    if (page_nr < 0 || page_nr >= (int)m_pages.size()) return;

    double y = margin;
    const auto& page_paras = m_pages[page_nr];

    for (const auto& para : page_paras) {

        // ── Burnup chart ──────────────────────────────────────────────────────
        if (para.is_chart) {
            const auto& hist    = para.chart_history;
            int         target  = para.chart_target;
            double      cw      = pw - margin * 2.0;
            double      ch      = para.chart_h;
            double      cx      = margin;
            double      cy      = y;

            if (hist.empty()) { y += ch; continue; }

            // Compute max daily words and cumulative total
            int max_daily = 0;
            int cumulative = 0;
            std::vector<int> daily_vals, cum_vals;
            for (const auto& r : hist) {
                daily_vals.push_back(r.words);
                cumulative += r.words;
                cum_vals.push_back(cumulative);
                if (r.words > max_daily) max_daily = r.words;
            }
            int max_cum   = cum_vals.back();
            int y_max     = std::max({ max_daily, max_cum, target, 1 });
            int n         = (int)hist.size();

            // Chart area insets
            double ax = cx + 42.0;  // left axis label space
            double ay = cy + 8.0;
            double aw = cw - 48.0;
            double ah = ch - 28.0;  // bottom axis label space

            // Background
            cr->set_source_rgb(0.97, 0.97, 0.97);
            cr->rectangle(ax, ay, aw, ah);
            cr->fill();

            // Grid lines (5 horizontal)
            cr->set_line_width(0.3);
            cr->set_source_rgb(0.75, 0.75, 0.75);
            for (int gi = 0; gi <= 4; ++gi) {
                double gy = ay + ah - (gi / 4.0) * ah;
                cr->move_to(ax, gy);
                cr->line_to(ax + aw, gy);
                cr->stroke();
                // Y-axis label
                auto lbl = ctx->create_pango_layout();
                lbl->set_font_description(Pango::FontDescription("Sans 6"));
                int val = (int)(y_max * gi / 4.0);
                std::string s = val >= 1000
                    ? std::to_string(val / 1000) + "k"
                    : std::to_string(val);
                lbl->set_text(s);
                lbl->set_alignment(Pango::Alignment::RIGHT);
                lbl->set_width(PANGO_SCALE * 38);
                cr->set_source_rgb(0.4, 0.4, 0.4);
                Pango::Rectangle ink2, log2;
                lbl->get_extents(ink2, log2);
                cr->move_to(cx, gy - PANGO_PIXELS(log2.get_height()) / 2.0);
                lbl->show_in_cairo_context(cr);
            }

            // Daily bars
            double bar_w = std::max(1.0, aw / n - 1.0);
            for (int i = 0; i < n; ++i) {
                double bh = (daily_vals[i] / (double)y_max) * ah;
                double bx = ax + (i / (double)n) * aw;
                double by = ay + ah - bh;
                cr->set_source_rgba(0.38, 0.60, 0.84, 0.65);
                cr->rectangle(bx, by, bar_w, bh);
                cr->fill();
            }

            // Cumulative line
            cr->set_line_width(1.5);
            cr->set_source_rgb(0.18, 0.48, 0.18);
            for (int i = 0; i < n; ++i) {
                double px2 = ax + (i / (double)n) * aw + bar_w / 2.0;
                double py2 = ay + ah - (cum_vals[i] / (double)y_max) * ah;
                if (i == 0) cr->move_to(px2, py2);
                else        cr->line_to(px2, py2);
            }
            cr->stroke();

            // Target line (dashed)
            if (target > 0) {
                double ty2 = ay + ah - (std::min(target, y_max) / (double)y_max) * ah;
                cr->set_line_width(1.0);
                cr->set_dash(std::vector<double>{4.0, 3.0}, 0.0);
                cr->set_source_rgb(0.75, 0.20, 0.20);
                cr->move_to(ax, ty2);
                cr->line_to(ax + aw, ty2);
                cr->stroke();
                cr->set_dash(std::vector<double>{}, 0.0); // reset dash
            }

            // Border
            cr->set_line_width(0.5);
            cr->set_source_rgb(0.55, 0.55, 0.55);
            cr->rectangle(ax, ay, aw, ah);
            cr->stroke();

            // X-axis: first and last date labels
            cr->set_source_rgb(0.4, 0.4, 0.4);
            auto date_lbl = [&](const std::string& date, double x2, Pango::Alignment align) {
                auto ll = ctx->create_pango_layout();
                ll->set_font_description(Pango::FontDescription("Sans 6"));
                ll->set_text(date);
                ll->set_alignment(align);
                ll->set_width(PANGO_SCALE * 60);
                cr->move_to(x2, ay + ah + 3.0);
                ll->show_in_cairo_context(cr);
            };
            date_lbl(hist.front().date, ax, Pango::Alignment::LEFT);
            date_lbl(hist.back().date,  ax + aw - 60.0, Pango::Alignment::RIGHT);

            // Legend
            auto draw_legend = [&](double lx, double ly,
                                   double r, double g, double b, double a,
                                   bool dashed, const std::string& text) {
                if (dashed) {
                    cr->set_dash(std::vector<double>{4.0, 3.0}, 0.0);
                    cr->set_line_width(1.0);
                } else {
                    cr->set_line_width(2.0);
                }
                cr->set_source_rgba(r, g, b, a);
                cr->move_to(lx, ly + 4); cr->line_to(lx + 14, ly + 4);
                cr->stroke();
                cr->set_dash(std::vector<double>{}, 0.0);
                auto ll = ctx->create_pango_layout();
                ll->set_font_description(Pango::FontDescription("Sans 6"));
                ll->set_text(text);
                cr->set_source_rgb(0.3, 0.3, 0.3);
                cr->move_to(lx + 17, ly);
                ll->show_in_cairo_context(cr);
            };
            draw_legend(ax,        cy + ch - 10, 0.38, 0.60, 0.84, 0.65, false, "Daily words");
            draw_legend(ax + 72,   cy + ch - 10, 0.18, 0.48, 0.18, 1.0,  false, "Cumulative");
            if (target > 0)
                draw_legend(ax + 148, cy + ch - 10, 0.75, 0.20, 0.20, 1.0, true, "Target");

            cr->set_source_rgb(0, 0, 0); // reset colour for next paras
            y += ch + 4.0;
            continue;
        }

        // ── Regular text para ─────────────────────────────────────────────────
        auto layout = make_layout(para.is_heading
            ? ("Sans Bold " + std::to_string((int)(font_pt + 2)))
            : body_font);
        layout->set_text(para.text);

        Pango::Rectangle ink, logical;
        layout->get_extents(ink, logical);
        double text_h = PANGO_PIXELS(logical.get_height());

        cr->move_to(margin, y);
        layout->show_in_cairo_context(cr);
        y += text_h + (para.is_heading ? line_h * 0.5 : 4.0);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// paginate_report — convert ReportData into PagedPara lines
// ─────────────────────────────────────────────────────────────────────────────
void PrintDialog::paginate_report(const Glib::RefPtr<Gtk::PrintContext>& ctx,
                                   const ReportData& data,
                                   Gtk::PrintOperation& op)
{
    m_pages.clear();

    double page_height = ctx->get_height();
    double margin      = 54.0;   // ~0.75"
    double usable_h    = page_height - margin * 2.0;
    double line_h      = 16.0;
    double heading_h   = 22.0;
    // Reserve footer space (always printed for reports)
    usable_h -= line_h * 1.5;
    if (usable_h < line_h) usable_h = line_h;

    // Build a flat list of PagedPara from the report sections.
    // We emit: section title (heading), then key stat lines.
    // This gives a readable plain-text version of the HTML report.
    std::vector<PagedPara> all_paras;

    auto add_heading = [&](const std::string& text, bool force_break = false) {
        PagedPara p;
        p.text       = text;
        p.is_heading = true;
        p.page_break = force_break;
        all_paras.push_back(p);
    };
    auto add_line = [&](const std::string& label, const std::string& value) {
        if (value.empty() || value == "0" || value == "—") return;
        PagedPara p;
        p.text = label + ": " + value;
        all_paras.push_back(p);
    };
    auto add_blank = [&]() {
        PagedPara p; p.text = ""; all_paras.push_back(p);
    };

    // Title page block
    add_heading(data.title.empty() ? "Untitled Project" : data.title);
    add_line("Author",    data.author);
    add_line("Genre",     data.genre);
    add_line("Publisher", data.publisher);
    add_line("ISBN",      data.isbn);
    add_line("Due date",  data.due_date);
    add_blank();

    // Health
    if (!data.warnings.empty()) {
        add_heading("⚠  Warnings");
        for (const auto& w : data.warnings) { PagedPara p; p.text = "• " + w; all_paras.push_back(p); }
        add_blank();
    }
    if (!data.positives.empty()) {
        add_heading("✓  Achievements");
        for (const auto& pos : data.positives) { PagedPara p; p.text = "• " + pos; all_paras.push_back(p); }
        add_blank();
    }

    // Progress
    add_heading("Word Count & Progress");
    add_line("Total words",    std::to_string(data.total_words));
    add_line("Word target",    std::to_string(data.project_word_target));
    {
        std::ostringstream pct;
        pct << std::fixed << std::setprecision(1) << data.pct_complete;
        add_line("Complete",   pct.str() + "%");
    }
    add_line("Daily target",   std::to_string(data.daily_target) + " words/day");
    add_line("Days written",   std::to_string(data.days_written));
    add_line("Current streak", std::to_string(data.current_streak) + " days");
    add_line("Longest streak", std::to_string(data.longest_streak) + " days");
    {
        std::ostringstream avg;
        avg << std::fixed << std::setprecision(0) << data.avg_words_per_day;
        add_line("Avg words/day", avg.str());
    }
    add_blank();

    // Burnup chart — injected as a special para with reserved height
    if (!data.daily_history.empty()) {
        PagedPara chart;
        chart.is_chart      = true;
        chart.chart_h       = 180.0; // points (~2.5 inches)
        chart.chart_history = data.daily_history;
        chart.chart_target  = data.project_word_target;
        all_paras.push_back(std::move(chart));
        add_blank();
    }

    // Structure
    add_heading("Manuscript Structure");
    add_line("Groups",          std::to_string(data.ms_groups));
    add_line("Scenes",          std::to_string(data.ms_scenes));
    add_line("Empty scenes",    std::to_string(data.scenes_empty));
    add_line("Excluded",        std::to_string(data.scenes_excluded));
    add_line("Total snapshots", std::to_string(data.total_snapshots));
    add_line("Modified, no snap", std::to_string(data.nodes_modified_no_snap));
    add_blank();

    // Annotations
    add_heading("Annotations & Notes");
    add_line("Total annotations", std::to_string(data.total_annotations));
    for (const auto& [kind, count] : data.annotations_by_kind)
        add_line("  " + kind, std::to_string(count));
    add_line("Notes", std::to_string(data.total_notes));
    add_blank();

    // Characters
    add_heading("Characters");
    add_line("Nodes",       std::to_string(data.char_nodes));
    add_line("Groups",      std::to_string(data.char_groups));
    add_line("With image",  std::to_string(data.chars_with_image));
    add_line("Empty",       std::to_string(data.chars_empty));
    for (const auto& [role, count] : data.chars_by_role)
        add_line("  " + role, std::to_string(count));
    add_blank();

    // Places
    add_heading("Places");
    add_line("Nodes",      std::to_string(data.place_nodes));
    add_line("With image", std::to_string(data.places_with_image));
    add_line("Empty",      std::to_string(data.places_empty));
    add_blank();

    // References / Templates / Trash
    add_heading("References, Templates & Trash");
    add_line("References",   std::to_string(data.ref_nodes));
    add_line("With URL",     std::to_string(data.refs_with_url));
    add_line("Templates",    std::to_string(data.template_nodes));
    add_line("Trash items",  std::to_string(data.trash_count));
    add_line("Trash words",  std::to_string(data.trash_words));
    add_blank();

    // Backlinks
    add_heading("Backlinks");
    add_line("Total links",     std::to_string(data.total_links));
    add_line("Most linked",     data.most_linked_title);
    add_blank();

    // Pomodoro
    add_heading("Pomodoro");
    add_line("Focus sessions", std::to_string(data.pomo_total_sessions));
    add_line("Completed",      std::to_string(data.pomo_completed));
    {
        std::ostringstream hrs, avg;
        hrs << std::fixed << std::setprecision(1) << data.pomo_total_focus_hrs;
        avg << std::fixed << std::setprecision(0) << data.pomo_avg_session_min;
        add_line("Total focus", hrs.str() + " hours");
        add_line("Avg session", avg.str() + " minutes");
    }
    add_line("Best day", data.pomo_best_day);

    // Paginate
    std::vector<PagedPara> current_page;
    double y_used = 0.0;

    for (auto& para : all_paras) {
        double para_h = para.is_chart   ? para.chart_h
                      : para.is_heading ? heading_h
                      : (para.text.empty() ? line_h * 0.5 : line_h);

        bool need_new_page = (para.page_break && !current_page.empty())
                          || (y_used + para_h > usable_h && !current_page.empty());
        if (need_new_page) {
            m_pages.push_back(current_page);
            current_page.clear();
            y_used = 0.0;
        }
        if (para.is_chart || !para.text.empty() || !current_page.empty())
            current_page.push_back(para);
        y_used += para_h + (para.is_heading ? 6.0 : 2.0);
    }
    if (!current_page.empty())
        m_pages.push_back(current_page);
    if (m_pages.empty())
        m_pages.push_back({});

    op.set_n_pages((int)m_pages.size());
}

// ─────────────────────────────────────────────────────────────────────────────
// on_print
// ─────────────────────────────────────────────────────────────────────────────
void PrintDialog::on_print() {
    bool report_mode = m_mode_report.get_active();

    // Validate manuscript mode selection
    std::vector<Exporter::SourceNode> nodes;
    int n_scenes = 0;
    if (!report_mode) {
        nodes = collect_selected_nodes();
        for (const auto& n : nodes) if (!n.is_group) ++n_scenes;
        if (n_scenes == 0) {
            m_status_lbl.set_text("No scenes selected.");
            return;
        }
    }

    m_btn_print.set_sensitive(false);
    m_status_lbl.set_text("Opening print dialog…");

    std::string title  = m_model.project_title;
    std::string author = m_model.author;
    int total_pages    = 1;

    // Generate report data upfront if needed
    ReportData report_data;
    if (report_mode)
        report_data = ReportEngine::generate(m_model, m_prefs);

    auto op = Gtk::PrintOperation::create();
    op->set_job_name(report_mode
        ? (title.empty() ? "Folio Report" : title + " — Report")
        : (title.empty() ? "Folio Print"  : title));

    // Paper / orientation (manuscript settings panel has the dropdowns)
    auto page_setup = Gtk::PageSetup::create();
    if (m_paper_dd) {
        guint paper_sel = m_paper_dd->get_selected();
        // Use Gtk::PaperSize value constructor — no ::create() in this gtkmm version.
        // Assign via named Glib::ustring to avoid char[] being picked up as a sigc++ functor.
        Glib::ustring paper_name =
            (paper_sel == 1) ? Glib::ustring(GTK_PAPER_NAME_A4)    :
            (paper_sel == 2) ? Glib::ustring(GTK_PAPER_NAME_LEGAL)  :
                               Glib::ustring(GTK_PAPER_NAME_LETTER);
        Gtk::PaperSize ps(paper_name);
        page_setup->set_paper_size_and_default_margins(ps);
    }
    if (m_orientation_dd && m_orientation_dd->get_selected() == 1)
        page_setup->set_orientation(Gtk::PageOrientation::LANDSCAPE);
    op->set_default_page_setup(page_setup);

    op->signal_begin_print().connect(
        [this, report_mode, &nodes, &report_data, &op, &total_pages]
        (const Glib::RefPtr<Gtk::PrintContext>& ctx) {
            if (report_mode)
                paginate_report(ctx, report_data, *op);
            else
                paginate(ctx, nodes, *op);
            total_pages = (int)m_pages.size();
        });

    op->signal_draw_page().connect(
        [this, &title, &author, &total_pages]
        (const Glib::RefPtr<Gtk::PrintContext>& ctx, int page_nr) {
            draw_page(ctx, page_nr, title, author, total_pages);
        });

    try {
        auto result = op->run(Gtk::PrintOperation::Action::PRINT_DIALOG, *this);
        if (result == Gtk::PrintOperation::Result::APPLY) {
            if (report_mode)
                m_status_lbl.set_text("✓  Report sent to printer.");
            else
                m_status_lbl.set_text("✓  Sent " + std::to_string(n_scenes) +
                    " scene" + (n_scenes != 1 ? "s" : "") + " to printer.");
        } else {
            m_status_lbl.set_text("Print cancelled.");
        }
    } catch (const Gtk::PrintError& e) {
        m_status_lbl.set_text("Print error: " + std::string(e.what()));
    } catch (const std::exception& e) {
        m_status_lbl.set_text("Error: " + std::string(e.what()));
    }

    m_btn_print.set_sensitive(true);
}
} // namespace Folio
