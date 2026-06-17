// ─────────────────────────────────────────────────────────────────────────────
// Folio — ExportDialog.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "ExportDialog.hpp"
#include "PdfExporter.hpp"
#include "PdfPaginator.hpp"
#include "CompileFormatDialog.hpp"
#include <FolioLog.hpp>
#include <sstream>

namespace Folio {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
ExportDialog::ExportDialog(Gtk::Window& parent, DocumentModel& model,
                            FolioPrefs& prefs)
    : Gtk::Window(), m_model(model), m_prefs(prefs)
{
    set_transient_for(parent);
    set_modal(true);
    set_title("Export Manuscript");
    set_default_size(820, 600);
    set_resizable(true);

    // Escape closes
    auto kc = Gtk::EventControllerKey::create();
    kc->signal_key_pressed().connect([this](guint kv, guint, Gdk::ModifierType) -> bool {
        if (kv == GDK_KEY_Escape) { close(); return true; }
        return false;
    }, false);
    add_controller(kc);

    // CSS
    auto css = Gtk::CssProvider::create();
    css->load_from_data(R"(
        .export-section-title {
            font-size: 11px; font-weight: 700; letter-spacing: 0.06em;
            color: alpha(currentColor, 0.5); text-transform: uppercase;
            padding: 4px 0 2px 0;
        }
        .export-row {
            padding: 4px 0;
        }
        .export-row-label {
            font-size: 13px;
            color: alpha(currentColor, 0.85);
        }
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
    )");
    get_style_context()->add_provider_for_display(
        get_display(), css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);

    // ── Build UI ──────────────────────────────────────────────────────────────
    build_scene_list();
    build_settings();
    build_footer();

    // Left panel
    m_left.set_size_request(280, -1);
    m_left.append(m_list_toolbar);

    m_list_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_list_scroll.set_vexpand(true);
    m_list_scroll.set_child(m_list_box);
    m_left.append(m_list_scroll);

    // Right panel
    m_settings_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_settings_scroll.set_vexpand(true);
    m_settings_scroll.set_child(m_settings_box);
    m_settings_box.set_margin_start(16);
    m_settings_box.set_margin_end(16);
    m_settings_box.set_margin_top(12);
    m_settings_box.set_margin_bottom(12);
    m_right.append(m_settings_scroll);

    // Paned
    m_paned.set_start_child(m_left);
    m_paned.set_end_child(m_right);
    m_paned.set_position(280);
    m_paned.set_vexpand(true);

    m_root.append(m_paned);
    m_root.append(m_footer);
    set_child(m_root);
}

// ─────────────────────────────────────────────────────────────────────────────
// Scene list
// ─────────────────────────────────────────────────────────────────────────────
void ExportDialog::add_nodes_recursive(const std::vector<BinderNode>& nodes,
                                        int depth) {
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
            chk->signal_toggled().connect([this](){ update_sel_count(); });
            row.check = chk;

            auto* lbl = Gtk::make_managed<Gtk::Label>(n.title.empty() ? "Untitled" : n.title);
            lbl->set_halign(Gtk::Align::START);
            lbl->set_hexpand(true);
            lbl->set_ellipsize(Pango::EllipsizeMode::END);

            // Word count hint
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

void ExportDialog::build_scene_list() {
    // Toolbar
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

    // Scene rows
    add_nodes_recursive(m_model.manuscript, 0);
    update_sel_count();
}

void ExportDialog::update_sel_count() {
    int total = 0, sel = 0;
    for (const auto& r : m_rows) {
        if (!r.check) continue;
        ++total;
        if (r.check->get_active()) ++sel;
    }
    m_sel_count_lbl.set_text(std::to_string(sel) + " / " +
                              std::to_string(total) + " scenes");
}

// ─────────────────────────────────────────────────────────────────────────────
// Settings panel
// ─────────────────────────────────────────────────────────────────────────────
Gtk::Widget* ExportDialog::make_section(const std::string& title) {
    auto* lbl = Gtk::make_managed<Gtk::Label>(title);
    lbl->add_css_class("export-section-title");
    lbl->set_halign(Gtk::Align::START);
    lbl->set_margin_top(8);
    return lbl;
}

Gtk::Widget* ExportDialog::make_row(const std::string& label,
                                     Gtk::Widget& widget) {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
    box->add_css_class("export-row");
    auto* lbl = Gtk::make_managed<Gtk::Label>(label);
    lbl->add_css_class("export-row-label");
    lbl->set_halign(Gtk::Align::START);
    lbl->set_hexpand(true);
    widget.set_halign(Gtk::Align::END);
    box->append(*lbl);
    box->append(widget);
    return box;
}

// Format index: 0=DOCX 1=EPUB 2=HTML 3=Markdown 4=ODT 5=RTF 6=TXT
void ExportDialog::update_format_sensitivity() {
    if (!m_format_dd) return;
    guint sel = m_format_dd->get_selected();

    // PDF (appended at index 7) runs the CompileFormat/paginator path and shares
    // none of the ExportOptions controls — swap the whole property set.
    bool is_pdf = (sel == 7);
    m_standard_settings.set_visible(!is_pdf);
    m_pdf_settings.set_visible(is_pdf);
    if (is_pdf) return;   // standard controls below don't apply to PDF

    bool is_docx = (sel == 0);
    bool is_epub = (sel == 1);
    bool is_odt  = (sel == 4);
    bool is_rtf  = (sel == 5);

    bool is_archive  = is_docx || is_epub || is_odt;
    bool can_flatten = is_rtf  || is_docx || is_epub;
    bool can_pagebrk = is_rtf  || is_docx || is_odt;
    bool can_sep     = true; // all formats support scene separators

    // ZIP output only makes sense for plain-text formats
    m_radio_zip.set_sensitive(!is_archive);
    if (is_archive) m_radio_combined.set_active(true);

    // Page break between groups
    m_chk_page_break.set_sensitive(can_pagebrk);

    // Scene separator
    m_sep_entry.set_sensitive(can_sep);
    m_chk_sep_own_line.set_sensitive(can_sep);

    // Flatten formatting
    m_chk_flatten.set_sensitive(can_flatten);
    if (!can_flatten) m_flatten_box.set_visible(false);

    // Cover image — only relevant for EPUB and only when cover exists
    bool has_cover = !m_model.cover_thumbnail.empty();
    m_chk_cover.set_visible(is_epub && has_cover);
}

void ExportDialog::build_settings() {
    // ── Format ────────────────────────────────────────────────────────────────
    m_settings_box.append(*make_section("Format"));

    // DOCX, EPUB, HTML, Markdown, ODT, RTF, TXT are alphabetical; PDF is appended
    // last (index 7) rather than slotted alphabetically so the hardcoded indices
    // in build_opts/update_format_sensitivity stay stable.
    auto fmt_list = Gtk::StringList::create(
        {"DOCX", "EPUB", "HTML", "Markdown", "ODT", "RTF", "TXT", "PDF"});
    m_format_dd = Gtk::make_managed<Gtk::DropDown>(fmt_list);
    m_format_dd->set_selected(1); // default: EPUB
    m_format_dd->set_hexpand(false);
    m_settings_box.append(*make_row("File format", *m_format_dd));

    m_format_dd->property_selected().signal_changed().connect(
        [this]() { update_format_sensitivity(); });
    update_format_sensitivity(); // set initial state

    // The two swappable property boxes hang off the settings column directly
    // below the format dropdown. update_format_sensitivity() shows exactly one.
    m_settings_box.append(m_standard_settings);
    m_settings_box.append(m_pdf_settings);

    // ── PDF property set (shown only when format == PDF) ──────────────────────
    m_pdf_settings.append(*make_section("PDF Format"));
    // Picker is built dynamically from all_compile_formats() (builtins + the
    // user's saved custom formats). An "Edit formats…" button opens the editor.
    m_pdf_format_dd = Gtk::make_managed<Gtk::DropDown>();
    m_pdf_format_dd->set_hexpand(false);
    m_pdf_settings.append(*make_row("Format", *m_pdf_format_dd));

    m_pdf_edit_btn = Gtk::make_managed<Gtk::Button>("Edit formats…");
    m_pdf_edit_btn->add_css_class("pill-btn");
    m_pdf_edit_btn->set_halign(Gtk::Align::END);
    m_pdf_edit_btn->signal_clicked().connect(
        [this]() { open_format_editor(); });
    m_pdf_settings.append(*m_pdf_edit_btn);

    rebuild_pdf_formats();   // populate from prefs

    // ── Output Mode ───────────────────────────────────────────────────────────
    m_standard_settings.append(*make_section("Output"));
    auto* out_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 16);
    m_radio_combined.set_label("Combined file");
    m_radio_combined.set_active(true);
    m_radio_zip.set_label("ZIP (one file per scene)");
    m_radio_zip.set_group(m_radio_combined);
    out_box->append(m_radio_combined);
    out_box->append(m_radio_zip);
    m_standard_settings.append(*out_box);

    // ── Groups ────────────────────────────────────────────────────────────────
    m_standard_settings.append(*make_section("Groups"));

    // Heading style dropdown
    auto heading_styles = Gtk::StringList::create({
        "Title as-is",
        "Auto-number only  (e.g. Chapter 1)",
        "Auto-number + title  (e.g. Chapter 1: Title)",
        "No heading  (content only)"});
    m_group_heading_dd = Gtk::make_managed<Gtk::DropDown>(heading_styles);
    m_group_heading_dd->set_selected(0);
    m_group_heading_dd->set_hexpand(false);
    m_standard_settings.append(*make_row("Group heading style", *m_group_heading_dd));

    // Prefix word (Chapter / Part / Book / Section)
    auto prefix_words = Gtk::StringList::create({"Chapter", "Part", "Book", "Section"});
    m_group_prefix_dd = Gtk::make_managed<Gtk::DropDown>(prefix_words);
    m_group_prefix_dd->set_selected(0);
    m_standard_settings.append(*make_row("Prefix word", *m_group_prefix_dd));

    // Show prefix only when auto-number modes are selected
    auto update_prefix_vis = [this]() {
        guint sel = m_group_heading_dd ? m_group_heading_dd->get_selected() : 0;
        if (m_group_prefix_dd)
            m_group_prefix_dd->get_parent()->set_visible(sel == 1 || sel == 2);
    };
    if (m_group_heading_dd)
        m_group_heading_dd->property_selected().signal_changed().connect(update_prefix_vis);
    update_prefix_vis();

    m_chk_group_content.set_label("Include group body text");
    m_chk_group_content.set_active(true);
    m_standard_settings.append(m_chk_group_content);

    // ── Separators ────────────────────────────────────────────────────────────
    m_standard_settings.append(*make_section("Separators"));

    m_sep_entry.set_text("* * *");
    m_sep_entry.set_placeholder_text("Scene separator (leave blank to omit)");
    m_sep_entry.set_hexpand(true);
    m_standard_settings.append(*make_row("Scene separator", m_sep_entry));

    m_chk_sep_own_line.set_label("Surround separator with blank lines");
    m_chk_sep_own_line.set_active(true);
    m_standard_settings.append(m_chk_sep_own_line);

    m_chk_page_break.set_label("Page break between groups");
    m_chk_page_break.set_active(true);
    m_standard_settings.append(m_chk_page_break);

    // ── Flatten Formatting ────────────────────────────────────────────────────
    m_standard_settings.append(*make_section("Flatten Formatting"));
    m_chk_flatten.set_label("Override paragraph formatting (RTF only)");
    m_chk_flatten.set_active(false);
    m_standard_settings.append(m_chk_flatten);

    m_flatten_box.set_margin_start(16);
    m_flatten_box.set_visible(false);

    // Font
    m_flatten_font_entry.set_text("Times New Roman");
    m_flatten_font_entry.set_hexpand(true);
    m_flatten_box.append(*make_row("Font", m_flatten_font_entry));

    // Size
    auto* size_spin = Gtk::make_managed<Gtk::SpinButton>();
    size_spin->set_adjustment(Gtk::Adjustment::create(12, 6, 72, 1, 2));
    size_spin->set_digits(0);
    size_spin->set_numeric(true);
    size_spin->set_width_chars(4);
    m_flatten_size_spin = size_spin;
    m_flatten_box.append(*make_row("Size (pt)", *size_spin));

    // Line spacing
    auto* ls_spin = Gtk::make_managed<Gtk::SpinButton>();
    ls_spin->set_adjustment(Gtk::Adjustment::create(2.0, 0.5, 4.0, 0.25, 0.5));
    ls_spin->set_digits(2);
    ls_spin->set_numeric(true);
    ls_spin->set_width_chars(5);
    m_flatten_ls_spin = ls_spin;
    m_flatten_box.append(*make_row("Line spacing", *ls_spin));

    // Margins
    auto* margin_spin = Gtk::make_managed<Gtk::SpinButton>();
    margin_spin->set_adjustment(Gtk::Adjustment::create(72, 0, 288, 4, 36));
    margin_spin->set_digits(0);
    margin_spin->set_numeric(true);
    margin_spin->set_width_chars(4);
    m_flatten_margin_spin = margin_spin;
    m_flatten_box.append(*make_row("Margins (pt)", *margin_spin));

    // Paragraph spacing
    auto* para_spin = Gtk::make_managed<Gtk::SpinButton>();
    para_spin->set_adjustment(Gtk::Adjustment::create(0, 0, 72, 2, 12));
    para_spin->set_digits(0);
    para_spin->set_numeric(true);
    para_spin->set_width_chars(4);
    m_flatten_para_spin = para_spin;
    m_flatten_box.append(*make_row("Space after paragraph (pt)", *para_spin));

    m_standard_settings.append(m_flatten_box);

    // Toggle flatten box visibility
    m_chk_flatten.signal_toggled().connect([this]() {
        m_flatten_box.set_visible(m_chk_flatten.get_active());
    });

    // ── Cover Image (EPUB only) ───────────────────────────────────────────────
    m_chk_cover.set_label("Include cover image");
    m_chk_cover.set_active(true);
    // Only show when EPUB is selected AND a cover thumbnail exists
    bool has_cover = !m_model.cover_thumbnail.empty();
    m_chk_cover.set_visible(false); // shown by update_format_sensitivity
    if (has_cover)
        m_standard_settings.append(m_chk_cover);
}

// ─────────────────────────────────────────────────────────────────────────────
// Footer
// ─────────────────────────────────────────────────────────────────────────────
void ExportDialog::build_footer() {
    m_footer.add_css_class("export-footer");
    m_footer.set_halign(Gtk::Align::FILL);

    m_status_lbl.add_css_class("export-status");
    m_status_lbl.set_halign(Gtk::Align::START);
    m_status_lbl.set_hexpand(true);

    m_btn_cancel.set_label("Cancel");
    m_btn_cancel.signal_clicked().connect([this]() { close(); });

    m_btn_export.set_label("Export…");
    m_btn_export.add_css_class("suggested-action");
    m_btn_export.signal_clicked().connect([this]() { on_export(); });

    m_footer.append(m_status_lbl);
    m_footer.append(m_btn_cancel);
    m_footer.append(m_btn_export);
}

// ─────────────────────────────────────────────────────────────────────────────
// Build ExportOptions from current UI state
// ─────────────────────────────────────────────────────────────────────────────
ExportOptions ExportDialog::build_opts() const {
    ExportOptions opts;
    // Format index: 0=DOCX 1=EPUB 2=HTML 3=Markdown 4=ODT 5=RTF 6=TXT
    guint fmt_sel = m_format_dd ? m_format_dd->get_selected() : 5;
    switch (fmt_sel) {
        case 0:  opts.format = ExportOptions::Format::DOCX;     break;
        case 1:  opts.format = ExportOptions::Format::EPUB;     break;
        case 2:  opts.format = ExportOptions::Format::HTML;     break;
        case 3:  opts.format = ExportOptions::Format::Markdown; break;
        case 4:  opts.format = ExportOptions::Format::ODT;      break;
        case 6:  opts.format = ExportOptions::Format::TXT;      break;
        default: opts.format = ExportOptions::Format::RTF;      break;
    }
    opts.mode = m_radio_combined.get_active()
        ? ExportOptions::Mode::Combined : ExportOptions::Mode::Zipped;
    opts.scene_separator   = m_sep_entry.get_text();
    opts.separator_own_line = m_chk_sep_own_line.get_active();
    opts.page_break_on_group   = m_chk_page_break.get_active();
    opts.include_group_content = m_chk_group_content.get_active();
    if (m_group_heading_dd) {
        switch (m_group_heading_dd->get_selected()) {
            case 0: opts.group_heading_style = ExportOptions::GroupHeadingStyle::AsIs; break;
            case 1: opts.group_heading_style = ExportOptions::GroupHeadingStyle::AutoNumber; break;
            case 2: opts.group_heading_style = ExportOptions::GroupHeadingStyle::AutoNumberTitle; break;
            default: opts.group_heading_style = ExportOptions::GroupHeadingStyle::NoHeading; break;
        }
    }
    if (m_group_prefix_dd) {
        static const char* words[] = {"Chapter", "Part", "Book", "Section"};
        guint sel = m_group_prefix_dd->get_selected();
        opts.group_heading_word = words[sel < 4 ? sel : 0];
    }
    opts.first_line_indent    = m_prefs.first_line_indent;
    opts.first_line_indent_px = m_prefs.first_line_indent_px;
    opts.flatten = m_chk_flatten.get_active();
    if (opts.flatten) {
        opts.flatten_font         = m_flatten_font_entry.get_text();
        opts.flatten_size_pt      = m_flatten_size_spin
            ? (int)m_flatten_size_spin->get_value() : 12;
        opts.flatten_line_spacing = m_flatten_ls_spin
            ? m_flatten_ls_spin->get_value() : 2.0;
        opts.flatten_margin_pt    = m_flatten_margin_spin
            ? (int)m_flatten_margin_spin->get_value() : 72;
        opts.flatten_para_space_pt = m_flatten_para_spin
            ? (int)m_flatten_para_spin->get_value() : 0;
    }
    opts.include_cover = m_chk_cover.get_active();
    if (opts.include_cover && !m_model.cover_thumbnail.empty())
        opts.cover_thumbnail_b64 = m_model.cover_thumbnail;
    return opts;
}

// ─────────────────────────────────────────────────────────────────────────────
// Collect selected nodes preserving group structure
// ─────────────────────────────────────────────────────────────────────────────
std::vector<Exporter::SourceNode>
ExportDialog::collect_selected_nodes() const {
    // Helper: treat content as empty if it contains only whitespace when stripped of HTML
    auto meaningful_content = [](const std::string& html) -> std::string {
        if (html.empty()) return {};
        // Strip tags to get plain text
        std::string plain;
        bool in_tag = false;
        for (char c : html) {
            if (c == '<') { in_tag = true; continue; }
            if (c == '>') { in_tag = false; continue; }
            if (!in_tag) plain += c;
        }
        // If only whitespace remains, content is placeholder-only
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
// on_export — show file chooser then write
// ─────────────────────────────────────────────────────────────────────────────
void ExportDialog::on_export() {
    auto nodes = collect_selected_nodes();
    // Count actual content nodes
    int n_scenes = 0;
    for (const auto& n : nodes) if (!n.is_group) ++n_scenes;
    if (n_scenes == 0) {
        m_status_lbl.set_text("No scenes selected.");
        return;
    }

    // PDF runs the CompileFormat/paginator path, not the ExportOptions writers.
    if (m_format_dd && m_format_dd->get_selected() == 7) {
        on_export_pdf();
        return;
    }

    ExportOptions opts = build_opts();
    bool is_zip  = (opts.mode == ExportOptions::Mode::Zipped);
    bool is_rtf  = (opts.format == ExportOptions::Format::RTF);
    bool is_html = (opts.format == ExportOptions::Format::HTML);
    bool is_md   = (opts.format == ExportOptions::Format::Markdown);
    bool is_epub = (opts.format == ExportOptions::Format::EPUB);
    bool is_docx = (opts.format == ExportOptions::Format::DOCX);
    bool is_odt  = (opts.format == ExportOptions::Format::ODT);
    std::string ext  = is_epub ? "epub" : is_docx ? "docx"
                     : is_odt  ? "odt"  : is_zip  ? "zip"
                     : is_html ? "html" : is_md   ? "md"
                     : is_rtf  ? "rtf"  : "txt";
    std::string desc = is_epub ? "EPUB eBook"
                     : is_docx ? "Word Document"
                     : is_odt  ? "ODF Text Document"
                     : is_zip  ? "ZIP Archive"
                     : is_html ? "HTML Document"
                     : is_md   ? "Markdown File"
                     : is_rtf  ? "RTF Document" : "Text File";
    std::string default_name = m_model.project_title.empty()
        ? "export" : m_model.project_title;
    // Sanitise
    std::string safe;
    for (unsigned char c : default_name)
        safe += (std::isalnum(c) || c == '-' || c == '_') ? (char)c : '_';
    default_name = safe + "." + ext;

    auto file_dialog = Gtk::FileDialog::create();
    file_dialog->set_title("Save Export As…");
    file_dialog->set_initial_name(default_name);

    // Restore last export folder
    if (!m_prefs.last_export_folder.empty()) {
        auto folder = Gio::File::create_for_path(m_prefs.last_export_folder);
        file_dialog->set_initial_folder(folder);
    }

    auto filter = Gtk::FileFilter::create();
    filter->set_name(desc);
    filter->add_pattern("*." + ext);
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    filters->append(filter);
    file_dialog->set_filters(filters);

    file_dialog->save(*this,
        [this, file_dialog, opts, nodes, n_scenes](
                Glib::RefPtr<Gio::AsyncResult>& res) mutable {
            Glib::RefPtr<Gio::File> file;
            try { file = file_dialog->save_finish(res); } catch (...) { return; }
            if (!file) return;
            std::string path = file->get_path();

            m_btn_export.set_sensitive(false);
            m_status_lbl.set_text("Exporting…");

            std::string err;
            if (opts.format == ExportOptions::Format::EPUB) {
                auto bytes = Exporter::compile_epub(nodes, opts,
                    m_model.project_title, m_model.author);
                FILE* f = fopen(path.c_str(), "wb");
                if (!f) { err = "Cannot open file for writing."; }
                else { fwrite(bytes.data(), 1, bytes.size(), f); fclose(f); }
            } else if (opts.format == ExportOptions::Format::DOCX) {
                auto bytes = Exporter::compile_docx(nodes, opts,
                    m_model.project_title, m_model.author);
                FILE* f = fopen(path.c_str(), "wb");
                if (!f) { err = "Cannot open file for writing."; }
                else { fwrite(bytes.data(), 1, bytes.size(), f); fclose(f); }
            } else if (opts.format == ExportOptions::Format::ODT) {
                auto bytes = Exporter::compile_odt(nodes, opts,
                    m_model.project_title, m_model.author);
                FILE* f = fopen(path.c_str(), "wb");
                if (!f) { err = "Cannot open file for writing."; }
                else { fwrite(bytes.data(), 1, bytes.size(), f); fclose(f); }
            } else if (opts.mode == ExportOptions::Mode::Zipped) {
                auto entries = Exporter::compile_entries(nodes, opts);
                err = Exporter::write_zip(entries, path);
            } else {
                std::string content;
                if      (opts.format == ExportOptions::Format::HTML)
                    content = Exporter::compile_html(nodes, opts, m_model.project_title);
                else if (opts.format == ExportOptions::Format::Markdown)
                    content = Exporter::compile_markdown(nodes, opts);
                else
                    content = Exporter::compile_combined(nodes, opts);
                FILE* f = fopen(path.c_str(), "wb");
                if (!f) { err = "Cannot open file for writing."; }
                else { fwrite(content.data(), 1, content.size(), f); fclose(f); }
            }

            m_btn_export.set_sensitive(true);
            if (err.empty()) {
                // Remember the folder for next time
                auto exported_file = Gio::File::create_for_path(path);
                auto parent = exported_file->get_parent();
                if (parent) {
                    m_prefs.last_export_folder = parent->get_path();
                    try { m_prefs.save(); } catch (...) {}
                }
                m_status_lbl.set_text("✓  Exported " + std::to_string(n_scenes) +
                    " scene" + (n_scenes != 1 ? "s" : "") + " to " + path);
            } else {
                m_status_lbl.set_text("Error: " + err);
            }
        });
}

// ─────────────────────────────────────────────────────────────────────────────
// PDF format picker — populated from FolioPrefs::all_compile_formats() (s18)
// ─────────────────────────────────────────────────────────────────────────────
ExportDialog::~ExportDialog() = default;

void ExportDialog::rebuild_pdf_formats(const std::string& select_name) {
    if (!m_pdf_format_dd) return;

    // Remember the currently-selected name so a rebuild keeps the user's pick
    // when no explicit target is given.
    auto all = m_prefs.all_compile_formats();
    std::string keep = select_name;
    if (keep.empty()) {
        guint cur = m_pdf_format_dd->get_selected();
        if (cur != GTK_INVALID_LIST_POSITION && cur < all.size())
            keep = all[cur].name;
    }

    std::vector<Glib::ustring> names;
    names.reserve(all.size());
    guint want = 0;
    for (guint i = 0; i < all.size(); ++i) {
        names.push_back(all[i].name);
        if (!keep.empty() && all[i].name == keep) want = i;
    }
    auto list = Gtk::StringList::create(names);
    m_pdf_format_dd->set_model(list);
    if (!all.empty()) m_pdf_format_dd->set_selected(want);
}

void ExportDialog::open_format_editor() {
    if (!m_format_editor) {
        m_format_editor = std::make_unique<CompileFormatDialog>(*this, m_prefs);
        m_format_editor->on_formats_changed =
            [this](const std::string& name) { rebuild_pdf_formats(name); };
        m_format_editor->signal_close_request().connect(
            [this]() -> bool {
                // Final refresh on close, then release the dialog.
                rebuild_pdf_formats();
                m_format_editor.reset();
                return false;
            }, false);
    }
    m_format_editor->present();
}

// ─────────────────────────────────────────────────────────────────────────────
// on_export_pdf — PDF export via the F-PDF CompileFormat/paginator path (s17).
// Lifted from the s16 PrintDialog button when PDF moved from Print to Export.
// The PDF format picker selects from all_compile_formats() (builtins + s18 user
// customs); the preflight coverage report is logged, then the paginator renders
// to the chosen path.
// ─────────────────────────────────────────────────────────────────────────────
void ExportDialog::on_export_pdf() {
    auto nodes = collect_selected_nodes();
    int n_scenes = 0;
    for (const auto& n : nodes) if (!n.is_group) ++n_scenes;
    if (n_scenes == 0) {
        m_status_lbl.set_text("No scenes selected.");
        return;
    }

    // PDF format picker → compile format. Index maps to all_compile_formats()
    // order (builtins first, then user customs — matches rebuild_pdf_formats()).
    auto fmts = m_prefs.all_compile_formats();
    std::size_t fmt_idx = m_pdf_format_dd ? m_pdf_format_dd->get_selected() : 0;
    const Folio::CompileFormat& fmt = Folio::builtin_format_at(fmts, fmt_idx);

    Folio::PreflightResult pre = Folio::pdf_export_preflight(nodes);

    LOG_INFO("F-PDF export preflight — format \"{}\" ({}), {} scene(s) selected",
             fmt.name,
             fmt.mode == Folio::RenderMode::Adaptable ? "Adaptable" : "Formal",
             n_scenes);
    LOG_INFO("F-PDF coverage report:\n{}", Folio::format_report(pre.report));

    std::ostringstream st;
    int tag_types = 0;
    for (int i = 0; i < Folio::FORMAT_TAG_COUNT; ++i)
        if (pre.report.present(static_cast<Folio::FormatTag>(i))) ++tag_types;

    st << "Coverage: " << pre.report.nodes_scanned << " node(s), "
       << tag_types << " tag type(s)";
    if (!pre.ok()) {
        st << " — " << pre.unsupported.size() << " unsupported element(s): ";
        for (std::size_t i = 0; i < pre.unsupported.size(); ++i)
            st << (i ? ", " : "") << "<" << pre.unsupported[i] << ">";
        st << " (those tags are skipped)";
        LOG_WARN("F-PDF preflight found {} unsupported element(s)",
                 pre.unsupported.size());
    }
    std::string summary = st.str();

    // Everything the async save callback needs is copied by VALUE — the local
    // fmts vector (and the fmt reference into it) and the node list would dangle
    // once on_export_pdf returns, since FileDialog::save runs its callback later.
    Folio::CompileFormat fmt_copy = fmt;
    Folio::PdfDocInfo info;
    info.title  = m_model.project_title;
    info.author = m_model.author;

    auto dialog = Gtk::FileDialog::create();
    dialog->set_title("Export PDF");
    std::string base = info.title.empty() ? "Untitled" : info.title;
    for (char& c : base) {
        if (c == '/' || c == '\\') c = '_';
    }
    dialog->set_initial_name(base + ".pdf");
    if (!m_prefs.last_export_folder.empty()) {
        auto folder = Gio::File::create_for_path(m_prefs.last_export_folder);
        dialog->set_initial_folder(folder);
    }
    auto filter = Gtk::FileFilter::create();
    filter->set_name("PDF files");
    filter->add_pattern("*.pdf");
    auto filters = Gio::ListStore<Gtk::FileFilter>::create();
    filters->append(filter);
    dialog->set_filters(filters);

    m_status_lbl.set_text("Choose where to save the PDF…");

    dialog->save(*this,
        [this, dialog, nodes, fmt_copy, info, summary]
        (Glib::RefPtr<Gio::AsyncResult>& result) {
            Glib::RefPtr<Gio::File> file;
            try {
                file = dialog->save_finish(result);
            } catch (...) {
                m_status_lbl.set_text("PDF export cancelled.");
                return;
            }
            if (!file) { m_status_lbl.set_text("PDF export cancelled."); return; }

            std::string path = file->get_path();
            std::string err  = Folio::export_pdf(nodes, fmt_copy, info, path);
            if (!err.empty()) {
                m_status_lbl.set_text("PDF export failed: " + err);
                return;
            }
            // Remember the folder for next time (matches the other export paths).
            auto exported_file = Gio::File::create_for_path(path);
            auto parent = exported_file->get_parent();
            if (parent) {
                m_prefs.last_export_folder = parent->get_path();
                try { m_prefs.save(); } catch (...) {}
            }
            m_status_lbl.set_text("✓  PDF saved to " + path + "  —  " + summary);
        });
}

} // namespace Folio
