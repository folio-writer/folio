// ─────────────────────────────────────────────────────────────────────────────
// Folio — TemplateBuilderDialog.cpp   (s33)   GTK/gtkmm4. See header.
//
// Thin GTK surface over the pure Folio::TemplateEdit brain. Every schema change
// (add / remove / move / rename / retype) calls a TemplateEdit function on the
// local m_draft, then rebuilds the row list; the dialog never touches the model.
// On Save it ensures the floor buffer, validates, fires the apply callback, and
// hides — the receiver commits on idle (the s24 modal rule).
// ─────────────────────────────────────────────────────────────────────────────
#include "TemplateBuilderDialog.hpp"

#include <gtkmm.h>

namespace Folio {

namespace {
// The user-pickable field types, in dropdown order. Index <-> FieldType maps
// through this single table so the two stay in lockstep.
struct TypeChoice { Folio::FieldType type; const char* label; };
const std::vector<TypeChoice>& pickable_types() {
    static const std::vector<TypeChoice> v = {
        { Folio::FieldType::Text,        "Text" },
        { Folio::FieldType::RichText,    "Rich text" },
        { Folio::FieldType::Number,      "Number" },
        { Folio::FieldType::Slider,      "Slider" },
        { Folio::FieldType::Toggle,      "Toggle" },
        { Folio::FieldType::Dropdown,    "Dropdown" },
        { Folio::FieldType::MultiSelect, "Multi-select" },
        { Folio::FieldType::List,        "List" },
        { Folio::FieldType::Image,       "Image" },
        { Folio::FieldType::Color,       "Color" },
        { Folio::FieldType::Date,        "Date" },
        { Folio::FieldType::Relation,    "Relation" },
    };
    return v;
}
}  // namespace

Folio::FieldType TemplateBuilderDialog::type_for_index(unsigned int idx) {
    const auto& v = pickable_types();
    if (idx < v.size()) return v[idx].type;
    return Folio::FieldType::Text;
}

unsigned int TemplateBuilderDialog::index_for_type(Folio::FieldType t) {
    const auto& v = pickable_types();
    for (unsigned int i = 0; i < v.size(); ++i)
        if (v[i].type == t) return i;
    return 0;  // Text
}

Gtk::DropDown* TemplateBuilderDialog::make_type_dropdown(Folio::FieldType current) {
    std::vector<Glib::ustring> labels;
    for (const auto& c : pickable_types()) labels.push_back(c.label);
    auto model = Gtk::StringList::create(labels);
    auto* dd = Gtk::make_managed<Gtk::DropDown>(model);
    dd->set_selected(index_for_type(current));
    return dd;
}

TemplateBuilderDialog::TemplateBuilderDialog(Gtk::Window& parent) {
    set_title("Edit fields");
    set_transient_for(parent);
    set_modal(true);
    set_default_size(520, 560);
    set_name("template-builder-dialog");
    build_chrome();
}

void TemplateBuilderDialog::build_chrome() {
    m_root.set_margin(16);
    m_root.set_spacing(10);

    // ── Type name ─────────────────────────────────────────────────────────────
    {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto* l = Gtk::make_managed<Gtk::Label>("Type name");
        l->add_css_class("pref-row-label");
        l->set_halign(Gtk::Align::START);
        l->set_hexpand(true);
        m_type_name_entry.set_size_request(220, -1);
        m_type_name_entry.set_halign(Gtk::Align::END);
        m_type_name_entry.signal_changed().connect([this]() {
            m_draft.type_name = std::string(m_type_name_entry.get_text());
        });
        row->append(*l);
        row->append(m_type_name_entry);
        m_root.append(*row);
    }

    // ── Fields heading ─────────────────────────────────────────────────────────
    {
        auto* hdr = Gtk::make_managed<Gtk::Label>("Fields");
        hdr->add_css_class("inspector-section-label");
        hdr->set_halign(Gtk::Align::START);
        hdr->set_margin_top(4);
        m_root.append(*hdr);
    }

    // ── Scrollable field list ───────────────────────────────────────────────────
    m_field_list.set_spacing(4);
    m_scroll.set_child(m_field_list);
    m_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_scroll.set_vexpand(true);
    m_scroll.set_has_frame(true);
    m_root.append(m_scroll);

    // ── Add field ────────────────────────────────────────────────────────────────
    {
        auto* add = Gtk::make_managed<Gtk::Button>("+ Add field");
        add->set_halign(Gtk::Align::START);
        add->signal_clicked().connect(sigc::mem_fun(*this, &TemplateBuilderDialog::on_add_field));
        m_root.append(*add);
    }

    // ── Error line ───────────────────────────────────────────────────────────────
    m_error_label.add_css_class("dim-label");
    m_error_label.set_halign(Gtk::Align::START);
    m_error_label.set_wrap(true);
    m_root.append(m_error_label);

    // ── Cancel / Save ─────────────────────────────────────────────────────────────
    {
        auto* bb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        bb->set_halign(Gtk::Align::END);
        bb->set_margin_top(4);
        auto* cancel = Gtk::make_managed<Gtk::Button>("Cancel");
        cancel->signal_clicked().connect([this]() { close(); });
        auto* save = Gtk::make_managed<Gtk::Button>("Save");
        save->add_css_class("suggested-action");
        save->signal_clicked().connect(sigc::mem_fun(*this, &TemplateBuilderDialog::on_save));
        bb->append(*cancel);
        bb->append(*save);
        m_root.append(*bb);
    }

    set_child(m_root);
}

void TemplateBuilderDialog::open_for(const Folio::Template& tmpl) {
    m_draft = tmpl;
    m_error_label.set_text("");
    m_type_name_entry.set_text(m_draft.type_name);
    rebuild_field_rows();
}

void TemplateBuilderDialog::rebuild_field_rows() {
    while (Gtk::Widget* c = m_field_list.get_first_child())
        m_field_list.remove(*c);

    const std::size_t n = m_draft.fields.size();
    for (std::size_t i = 0; i < n; ++i) {
        const bool is_buffer = TemplateEdit::is_trailing_buffer(m_draft, i);
        append_field_row(m_draft.fields[i], is_buffer);
    }
}

// One field row: [ label entry ][ type ▾ ][ ↑ ][ ↓ ][ ✕ ]. The trailing floor
// buffer (§4) is rendered with its destructive controls disabled — it stays put
// and is never removed; its type stays Rich text.
void TemplateBuilderDialog::append_field_row(const Folio::FieldSchema& f, bool is_buffer) {
    const std::string field_id = f.id;

    auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    row->set_margin_start(6);
    row->set_margin_end(6);
    row->set_margin_top(4);
    row->set_margin_bottom(4);
    row->set_name(std::string("tpl-field-row-") + field_id);

    auto* label_entry = Gtk::make_managed<Gtk::Entry>();
    label_entry->set_text(f.label);
    label_entry->set_hexpand(true);
    label_entry->set_placeholder_text("Field label");
    label_entry->signal_changed().connect([this, field_id, label_entry]() {
        TemplateEdit::rename_field(m_draft, field_id,
                                   std::string(label_entry->get_text()));
    });
    row->append(*label_entry);

    auto* type_dd = make_type_dropdown(f.type);
    type_dd->set_sensitive(!is_buffer);   // the buffer's type is fixed
    type_dd->property_selected().signal_changed().connect([this, field_id, type_dd]() {
        // Record only — no row rebuild here. Destroying the dropdown from inside
        // its own property-notify is unsafe, and a retype can't change which row
        // is the buffer (that is positional); ensure_floor_buffer settles the
        // no-buffer edge at save.
        TemplateEdit::retype_field(m_draft, field_id,
                                   type_for_index(type_dd->get_selected()));
    });
    row->append(*type_dd);

    auto* up = Gtk::make_managed<Gtk::Button>();
    up->set_icon_name("go-up-symbolic");
    up->add_css_class("flat");
    up->set_sensitive(!is_buffer);
    up->signal_clicked().connect([this, field_id]() {
        if (TemplateEdit::move_field(m_draft, field_id, -1)) rebuild_field_rows();
    });
    row->append(*up);

    auto* down = Gtk::make_managed<Gtk::Button>();
    down->set_icon_name("go-down-symbolic");
    down->add_css_class("flat");
    down->set_sensitive(!is_buffer);
    down->signal_clicked().connect([this, field_id]() {
        if (TemplateEdit::move_field(m_draft, field_id, +1)) rebuild_field_rows();
    });
    row->append(*down);

    auto* del = Gtk::make_managed<Gtk::Button>();
    del->set_icon_name("user-trash-symbolic");
    del->add_css_class("flat");
    del->set_sensitive(!is_buffer);       // never delete the floor buffer
    del->signal_clicked().connect([this, field_id]() {
        if (TemplateEdit::remove_field(m_draft, field_id)) rebuild_field_rows();
    });
    row->append(*del);

    m_field_list.append(*row);
}

void TemplateBuilderDialog::on_add_field() {
    // New fields default to Text with a generic label; the author renames/retypes
    // inline. add_field keeps the floor buffer at the bottom (§4).
    TemplateEdit::add_field(m_draft, Folio::FieldType::Text, "New field");
    rebuild_field_rows();
}

void TemplateBuilderDialog::on_save() {
    // Guarantee the floor buffer, then validate before committing.
    TemplateEdit::ensure_floor_buffer(m_draft);
    const std::string err = TemplateEdit::validate(m_draft);
    if (!err.empty()) {
        m_error_label.remove_css_class("dim-label");
        m_error_label.add_css_class("error");
        m_error_label.set_text(err);
        return;
    }
    // Capture by value and close BEFORE firing — the receiver schedules its store
    // mutation on idle, so nothing modal is in play when it runs (the s24 rule).
    ApplyCallback cb = m_on_apply;
    Folio::Template result = m_draft;
    close();
    if (cb) cb(result);
}

}  // namespace Folio
