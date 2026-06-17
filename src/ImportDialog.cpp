// ─────────────────────────────────────────────────────────────────────────────
// Folio — ImportDialog.cpp
// ─────────────────────────────────────────────────────────────────────────────
#include "ImportDialog.hpp"
#include <filesystem>
#include <sstream>

namespace Folio {

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

ImportDialog::ImportDialog(Gtk::Window& parent, DocumentModel& model)
    : Gtk::Window(), m_model(model)
{
    set_transient_for(parent);
    set_modal(true);
    set_title("Import Files");
    set_default_size(760, 520);
    set_resizable(true);

    // Escape closes
    auto kc = Gtk::EventControllerKey::create();
    kc->signal_key_pressed().connect([this](guint kv, guint, Gdk::ModifierType) -> bool {
        if (kv == GDK_KEY_Escape) { close(); return true; }
        return false;
    }, false);
    add_controller(kc);

    // ── CSS ───────────────────────────────────────────────────────────────────
    auto css = Gtk::CssProvider::create();
    css->load_from_data(R"(
        .imp-section-title {
            font-size: 11px; font-weight: 700; letter-spacing: 0.06em;
            color: alpha(currentColor, 0.5); text-transform: uppercase;
            padding: 4px 0 2px 0;
        }
        .imp-pick-bar {
            padding: 8px 10px;
            border-bottom: 1px solid alpha(currentColor, 0.10);
        }
        .imp-file-row {
            padding: 4px 10px;
            min-height: 30px;
        }
        .imp-file-row:hover { background-color: alpha(currentColor, 0.04); }
        .imp-file-name {
            font-size: 13px;
        }
        .imp-file-type {
            font-size: 11px;
            color: alpha(currentColor, 0.5);
        }
        .imp-row-label {
            font-size: 13px;
            color: alpha(currentColor, 0.85);
        }
        .imp-preview {
            font-size: 12px;
            color: alpha(currentColor, 0.55);
            padding: 6px 14px 4px 14px;
        }
        .imp-footer {
            padding: 10px 14px;
            border-top: 1px solid alpha(currentColor, 0.10);
        }
        .imp-status {
            font-size: 12px;
            color: alpha(currentColor, 0.6);
        }
        .imp-empty-hint {
            font-size: 13px;
            color: alpha(currentColor, 0.35);
            padding: 24px 12px;
        }
    )");
    get_style_context()->add_provider_for_display(
        get_display(), css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);

    // ── Pick bar ──────────────────────────────────────────────────────────────
    m_btn_add_files.set_label("Add Files…");
    m_btn_add_files.set_tooltip_text("Add one or more files to import");
    m_btn_add_files.signal_clicked().connect([this]{ pick_files(); });

    m_btn_add_folder.set_label("Add Folder…");
    m_btn_add_folder.set_tooltip_text("Import all supported files inside a folder");
    m_btn_add_folder.signal_clicked().connect([this]{ pick_folder(); });

    m_btn_clear.set_label("Clear");
    m_btn_clear.set_tooltip_text("Remove all queued files");
    m_btn_clear.signal_clicked().connect([this]{
        m_entries.clear();
        refresh_file_list();
        update_preview();
    });

    m_pick_bar.add_css_class("imp-pick-bar");
    m_pick_bar.set_hexpand(true);
    m_pick_bar.append(m_btn_add_files);
    m_pick_bar.append(m_btn_add_folder);

    auto* spacer = Gtk::make_managed<Gtk::Box>();
    spacer->set_hexpand(true);
    m_pick_bar.append(*spacer);
    m_pick_bar.append(m_btn_clear);

    // ── File list scroll ──────────────────────────────────────────────────────
    m_file_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_file_scroll.set_vexpand(true);
    m_file_scroll.set_propagate_natural_height(false);
    m_file_scroll.set_min_content_height(1);
    m_file_scroll.set_overflow(Gtk::Overflow::HIDDEN);
    m_file_list.set_size_request(-1, 1);
    m_file_list.set_overflow(Gtk::Overflow::HIDDEN);
    m_file_scroll.set_child(m_file_list);

    m_left.set_size_request(300, -1);
    m_left.append(m_pick_bar);
    m_left.append(m_file_scroll);

    // ── Options panel ─────────────────────────────────────────────────────────
    build_options();

    m_opts_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_opts_scroll.set_vexpand(true);
    m_opts_scroll.set_child(m_opts_box);
    m_opts_box.set_margin_start(16);
    m_opts_box.set_margin_end(16);
    m_opts_box.set_margin_top(12);
    m_opts_box.set_margin_bottom(12);

    m_right.set_hexpand(true);
    m_right.append(m_opts_scroll);

    // ── Divider between left and right ────────────────────────────────────────
    auto* sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL);
    m_body.append(m_left);
    m_body.append(*sep);
    m_body.append(m_right);
    m_body.set_vexpand(true);

    // ── Preview line ──────────────────────────────────────────────────────────
    m_preview_lbl.add_css_class("imp-preview");
    m_preview_lbl.set_halign(Gtk::Align::START);
    m_preview_lbl.set_text("No files selected.");

    // ── Footer ────────────────────────────────────────────────────────────────
    build_footer();

    // ── Assemble root ─────────────────────────────────────────────────────────
    m_root.append(m_body);
    m_root.append(m_preview_lbl);
    m_root.append(m_footer);
    set_child(m_root);

    // Initial empty hint
    refresh_file_list();
}

// ─────────────────────────────────────────────────────────────────────────────
// Options panel
// ─────────────────────────────────────────────────────────────────────────────

Gtk::Widget* ImportDialog::make_section(const std::string& title) {
    auto* lbl = Gtk::make_managed<Gtk::Label>(title);
    lbl->set_halign(Gtk::Align::START);
    lbl->add_css_class("imp-section-title");
    return lbl;
}

Gtk::Widget* ImportDialog::make_row(const std::string& label, Gtk::Widget& w) {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    auto* lbl = Gtk::make_managed<Gtk::Label>(label);
    lbl->set_halign(Gtk::Align::START);
    lbl->set_hexpand(true);
    lbl->add_css_class("imp-row-label");
    box->append(*lbl);
    box->append(w);
    return box;
}

void ImportDialog::build_options() {
    // ── Section: Splitting ────────────────────────────────────────────────────
    m_opts_box.append(*make_section("Splitting"));

    m_sep_entry.set_text("---");
    m_sep_entry.set_placeholder_text("e.g.  ---  or  * * *");
    m_sep_entry.set_tooltip_text(
        "Lines that exactly match this text (after trimming whitespace) split the "
        "content into separate scenes. Leave blank to treat each file as one scene.");
    m_sep_entry.set_max_width_chars(20);
    m_sep_entry.signal_changed().connect([this]{ update_preview(); });
    m_opts_box.append(*make_row("Scene separator", m_sep_entry));

    m_chk_md_headings.set_label("Use Markdown headings (# / ##) as hierarchy");
    m_chk_md_headings.set_active(true);
    m_chk_md_headings.set_tooltip_text(
        "For .md files: H1 lines become Groups, H2+ lines become Scenes. "
        "Files without any headings fall back to separator splitting.");
    m_chk_md_headings.signal_toggled().connect([this]{ update_preview(); });
    m_opts_box.append(m_chk_md_headings);

    // ── Section: Titles ───────────────────────────────────────────────────────
    m_opts_box.append(*make_section("Scene Titles"));

    auto title_store = Gtk::StringList::create({
        "First line of content",
        "Derive from filename",
        "Sequential (Scene 1, 2, …)"
    });
    m_title_dd = Gtk::make_managed<Gtk::DropDown>(title_store, Glib::RefPtr<Gtk::Expression<Glib::ustring>>{});
    m_title_dd->set_selected(0);
    m_title_dd->set_tooltip_text(
        "How to assign a title to each imported scene.\n"
        "\"First line\" uses the first non-empty line as the title and strips it from the body.");
    m_title_dd->property_selected().signal_changed().connect([this]{ update_preview(); });
    m_opts_box.append(*make_row("Title from", *m_title_dd));

    // ── Section: Folders ─────────────────────────────────────────────────────
    m_opts_box.append(*make_section("Folder Import"));

    m_chk_folder_group.set_label("Wrap folder contents in a Group");
    m_chk_folder_group.set_active(true);
    m_chk_folder_group.set_tooltip_text(
        "When importing a folder, create a Group node named after the folder and "
        "place all imported scenes inside it.");
    m_opts_box.append(m_chk_folder_group);
}

ImportOptions ImportDialog::current_opts() const {
    ImportOptions opts;
    opts.separator               = m_sep_entry.get_text();
    opts.md_headings_as_hierarchy = m_chk_md_headings.get_active();
    opts.folder_as_group         = m_chk_folder_group.get_active();

    switch (m_title_dd ? m_title_dd->get_selected() : 0) {
    case 1:  opts.title_source = ImportOptions::TitleSource::Filename;   break;
    case 2:  opts.title_source = ImportOptions::TitleSource::Sequential; break;
    default: opts.title_source = ImportOptions::TitleSource::FirstLine;  break;
    }
    return opts;
}

// ─────────────────────────────────────────────────────────────────────────────
// File list management
// ─────────────────────────────────────────────────────────────────────────────

void ImportDialog::refresh_file_list() {
    // Remove all children
    while (auto* child = m_file_list.get_first_child())
        m_file_list.remove(*child);

    if (m_entries.empty()) {
        auto* hint = Gtk::make_managed<Gtk::Label>(
            "No files queued.\nClick \"Add Files…\" or \"Add Folder…\" above.");
        hint->add_css_class("imp-empty-hint");
        hint->set_justify(Gtk::Justification::CENTER);
        hint->set_wrap(true);
        m_file_list.append(*hint);
        return;
    }

    for (size_t i = 0; i < m_entries.size(); ++i) {
        auto& e = m_entries[i];

        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        row->add_css_class("imp-file-row");
        row->set_hexpand(true);

        // Icon (folder or file)
        auto* icon = Gtk::make_managed<Gtk::Image>();
        icon->set_from_icon_name(e.is_folder ? "folder-symbolic" : "text-x-generic-symbolic");
        icon->set_pixel_size(16);
        row->append(*icon);

        // Name
        fs::path p(e.path);
        std::string name = p.filename().string();
        auto* name_lbl = Gtk::make_managed<Gtk::Label>(name);
        name_lbl->add_css_class("imp-file-name");
        name_lbl->set_halign(Gtk::Align::START);
        name_lbl->set_hexpand(true);
        name_lbl->set_ellipsize(Pango::EllipsizeMode::MIDDLE);
        name_lbl->set_tooltip_text(e.path);
        row->append(*name_lbl);

        // Type badge
        std::string type_str;
        if (e.is_folder) {
            type_str = "folder";
        } else {
            std::string ext = p.extension().string();
            if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
            for (char& c : ext) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            type_str = ext;
        }
        auto* type_lbl = Gtk::make_managed<Gtk::Label>(type_str);
        type_lbl->add_css_class("imp-file-type");
        row->append(*type_lbl);

        // Remove button
        auto* rm = Gtk::make_managed<Gtk::Button>();
        rm->set_icon_name("window-close-symbolic");
        rm->set_tooltip_text("Remove from queue");
        rm->add_css_class("flat");
        rm->add_css_class("circular");
        // Capture index by value; rebuild is cheap
        rm->signal_clicked().connect([this, i]{
            remove_entry(i);
        });
        row->append(*rm);

        e.row = row;
        m_file_list.append(*row);
    }
}

void ImportDialog::remove_entry(size_t idx) {
    if (idx >= m_entries.size()) return;
    m_entries.erase(m_entries.begin() + static_cast<ptrdiff_t>(idx));
    refresh_file_list();
    update_preview();
}

void ImportDialog::add_file_row(const std::string& path, bool is_folder) {
    // Deduplicate
    for (auto& e : m_entries)
        if (e.path == path) return;
    m_entries.push_back({ path, is_folder, nullptr });
    refresh_file_list();
    update_preview();
}

void ImportDialog::pick_files() {
    auto dlg = Gtk::FileChooserNative::create(
        "Choose Files to Import", *this,
        Gtk::FileChooser::Action::OPEN, "Add", "Cancel");
    dlg->set_select_multiple(true);

    // Supported formats filter
    auto filter_all = Gtk::FileFilter::create();
    filter_all->set_name("Supported formats (txt, md, rtf, docx, odt)");
    filter_all->add_pattern("*.txt");
    filter_all->add_pattern("*.md");
    filter_all->add_pattern("*.markdown");
    filter_all->add_pattern("*.rtf");
    filter_all->add_pattern("*.docx");
    filter_all->add_pattern("*.odt");
    dlg->add_filter(filter_all);

    auto filter_any = Gtk::FileFilter::create();
    filter_any->set_name("All files");
    filter_any->add_pattern("*");
    dlg->add_filter(filter_any);

    dlg->signal_response().connect([this, dlg](int response) {
        if (response != Gtk::ResponseType::ACCEPT) return;
        auto files = dlg->get_files();
        for (size_t i = 0; i < files->get_n_items(); ++i) {
            auto f = std::dynamic_pointer_cast<Gio::File>(files->get_object(i));
            if (f) add_file_row(f->get_path(), false);
        }
    });
    dlg->show();
}

void ImportDialog::pick_folder() {
    auto dlg = Gtk::FileChooserNative::create(
        "Choose Folder to Import", *this,
        Gtk::FileChooser::Action::SELECT_FOLDER, "Add", "Cancel");

    dlg->signal_response().connect([this, dlg](int response) {
        if (response != Gtk::ResponseType::ACCEPT) return;
        auto f = dlg->get_file();
        if (f) add_file_row(f->get_path(), true);
    });
    dlg->show();
}

// ─────────────────────────────────────────────────────────────────────────────
// Preview summary
// ─────────────────────────────────────────────────────────────────────────────

void ImportDialog::update_preview() {
    if (m_entries.empty()) {
        m_preview_lbl.set_text("No files selected.");
        m_btn_import.set_sensitive(false);
        return;
    }

    // Quick dry-run to count expected nodes
    ImportOptions opts = current_opts();
    int total_scenes = 0, total_groups = 0, errors = 0;

    for (auto& e : m_entries) {
        ImportResult r;
        if (e.is_folder)
            r = Importer::import_folder(e.path, opts);
        else
            r = Importer::import_file(e.path, opts);

        if (!r.ok()) { ++errors; continue; }
        for (auto& nd : r.nodes)
            nd.is_group ? ++total_groups : ++total_scenes;
    }

    std::ostringstream ss;
    ss << m_entries.size() << (m_entries.size() == 1 ? " source" : " sources")
       << " → " << total_scenes << (total_scenes == 1 ? " scene" : " scenes");
    if (total_groups > 0)
        ss << ", " << total_groups << (total_groups == 1 ? " group" : " groups");
    if (errors > 0)
        ss << "  (" << errors << " error" << (errors > 1 ? "s" : "") << ")";
    ss << " will be added to Manuscript.";

    m_preview_lbl.set_text(ss.str());
    m_btn_import.set_sensitive(total_scenes > 0 || total_groups > 0);
}

// ─────────────────────────────────────────────────────────────────────────────
// Footer
// ─────────────────────────────────────────────────────────────────────────────

void ImportDialog::build_footer() {
    m_footer.add_css_class("imp-footer");
    m_footer.set_hexpand(true);

    m_status_lbl.add_css_class("imp-status");
    m_status_lbl.set_halign(Gtk::Align::START);
    m_status_lbl.set_hexpand(true);

    m_btn_cancel.set_label("Cancel");
    m_btn_cancel.signal_clicked().connect([this]{ close(); });

    m_btn_import.set_label("Import");
    m_btn_import.add_css_class("suggested-action");
    m_btn_import.set_sensitive(false);
    m_btn_import.signal_clicked().connect([this]{ on_import(); });

    m_footer.append(m_status_lbl);
    m_footer.append(m_btn_cancel);
    m_footer.append(m_btn_import);
}

// ─────────────────────────────────────────────────────────────────────────────
// Import action
// ─────────────────────────────────────────────────────────────────────────────

void ImportDialog::on_import() {
    if (m_entries.empty()) return;

    m_btn_import.set_sensitive(false);
    m_btn_cancel.set_sensitive(false);
    m_status_lbl.set_text("Importing…");

    ImportOptions opts = current_opts();
    std::vector<ImportNode> all_nodes;
    std::vector<std::string> errors;

    for (auto& e : m_entries) {
        ImportResult r;
        if (e.is_folder)
            r = Importer::import_folder(e.path, opts);
        else
            r = Importer::import_file(e.path, opts);

        if (!r.ok()) {
            errors.push_back(e.path + ": " + r.error);
            continue;
        }
        for (auto& nd : r.nodes)
            all_nodes.push_back(std::move(nd));
    }

    if (!errors.empty()) {
        std::string msg = "Some files could not be imported:\n";
        for (auto& err : errors) msg += "\n" + err;
        auto dlg = Gtk::AlertDialog::create(msg);
        dlg->set_modal(true);
        dlg->set_buttons({"OK"});
        dlg->set_default_button(0);
        dlg->set_cancel_button(0);
        dlg->choose(*this, [dlg](Glib::RefPtr<Gio::AsyncResult>& r) {
            try { dlg->choose_finish(r); } catch (...) {}
        });
        m_status_lbl.set_text("Finished with errors.");
        m_btn_cancel.set_sensitive(true);
        return;
    }

    if (all_nodes.empty()) {
        m_status_lbl.set_text("Nothing to import.");
        m_btn_cancel.set_sensitive(true);
        m_btn_import.set_sensitive(true);
        return;
    }

    if (m_on_import)
        m_on_import(std::move(all_nodes));

    close();
}

} // namespace Folio
