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

    // ── Category ──────────────────────────────────────────────────────────────
    // Which binder section this template stamps into (the create-from picker filters
    // by it). character / place / reference, in this fixed order.
    {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        auto* l = Gtk::make_managed<Gtk::Label>("Category");
        l->add_css_class("pref-row-label");
        l->set_halign(Gtk::Align::START);
        l->set_hexpand(true);
        auto model = Gtk::StringList::create(
            std::vector<Glib::ustring>{ "Character", "Place", "Reference" });
        m_category_dd = Gtk::make_managed<Gtk::DropDown>(model);
        m_category_dd->set_halign(Gtk::Align::END);
        m_category_dd->property_selected().signal_changed().connect([this]() {
            static const char* cats[] = { "character", "place", "reference" };
            guint i = m_category_dd->get_selected();
            if (i < 3) m_draft.category = cats[i];
        });
        row->append(*l);
        row->append(*m_category_dd);
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

    // ── Add field / Add section ────────────────────────────────────────────────
    {
        auto* bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        bar->set_halign(Gtk::Align::START);
        auto* add = Gtk::make_managed<Gtk::Button>("+ Add field");
        add->signal_clicked().connect(sigc::mem_fun(*this, &TemplateBuilderDialog::on_add_field));
        bar->append(*add);
        auto* sec = Gtk::make_managed<Gtk::Button>("+ Add section");
        sec->set_tooltip_text("A heading that groups the fields beneath it");
        sec->signal_clicked().connect(sigc::mem_fun(*this, &TemplateBuilderDialog::on_add_section));
        bar->append(*sec);
        m_root.append(*bar);
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
    if (m_category_dd) {
        guint idx = m_draft.category == "place"     ? 1
                  : m_draft.category == "reference" ? 2
                                                    : 0;   // default → character
        m_category_dd->set_selected(idx);
    }
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

// One field row: [ label entry ][ type ▾ ][ ↑ ][ ↓ ][ ✕ ], with a per-type
// CONFIG sub-editor (s36) hosted indented beneath it. The trailing floor buffer
// (§4) is rendered with its destructive controls disabled — it stays put and is
// never removed; its type stays Rich text (and carries no config).
void TemplateBuilderDialog::append_field_row(const Folio::FieldSchema& f, bool is_buffer) {
    const std::string field_id = f.id;

    // s39 — a Heading is a section divider, not a data field: a title entry plus the
    // reorder/remove controls, with no type dropdown and no config sub-editor. (A
    // heading is never the floor buffer, so is_buffer is always false here.)
    if (f.type == Folio::FieldType::Heading) {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        row->set_margin_start(6);
        row->set_margin_end(6);
        row->set_margin_top(10);   // sets the section apart from the field above
        row->set_margin_bottom(2);
        row->set_name(std::string("tpl-section-row-") + field_id);

        auto* tag = Gtk::make_managed<Gtk::Label>("Section");
        tag->add_css_class("dim-label");
        tag->set_valign(Gtk::Align::CENTER);
        row->append(*tag);

        auto* title = Gtk::make_managed<Gtk::Entry>();
        title->set_text(f.label);
        title->set_hexpand(true);
        title->set_placeholder_text("Section heading");
        title->add_css_class("inspector-section-label");
        title->signal_changed().connect([this, field_id, title]() {
            TemplateEdit::rename_field(m_draft, field_id, std::string(title->get_text()));
        });
        row->append(*title);

        auto* up = Gtk::make_managed<Gtk::Button>();
        up->set_icon_name("go-up-symbolic");
        up->add_css_class("flat");
        up->signal_clicked().connect([this, field_id]() {
            if (TemplateEdit::move_field(m_draft, field_id, -1)) rebuild_field_rows();
        });
        row->append(*up);

        auto* down = Gtk::make_managed<Gtk::Button>();
        down->set_icon_name("go-down-symbolic");
        down->add_css_class("flat");
        down->signal_clicked().connect([this, field_id]() {
            if (TemplateEdit::move_field(m_draft, field_id, +1)) rebuild_field_rows();
        });
        row->append(*down);

        auto* del = Gtk::make_managed<Gtk::Button>();
        del->set_icon_name("user-trash-symbolic");
        del->add_css_class("flat");
        del->signal_clicked().connect([this, field_id]() {
            if (TemplateEdit::remove_field(m_draft, field_id)) rebuild_field_rows();
        });
        row->append(*del);

        m_field_list.append(*row);
        return;
    }

    auto* outer = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    outer->set_name(std::string("tpl-field-") + field_id);

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
        // Record the retype, then rebuild the WHOLE row list on idle so the
        // config sub-editor follows the new type. Deferred to idle because
        // destroying this dropdown from inside its own property-notify is unsafe
        // (the s24 modal lesson, in miniature) — the notify returns first, then
        // the idle tick rebuilds. retype_field clears config, so the new type's
        // sub-editor starts blank.
        TemplateEdit::retype_field(m_draft, field_id,
                                   type_for_index(type_dd->get_selected()));
        Glib::signal_idle().connect_once([this]() { rebuild_field_rows(); });
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

    outer->append(*row);

    // Config sub-editor, indented beneath the row (empty for types with no config).
    auto* config_host = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    config_host->set_margin_start(28);
    config_host->set_margin_end(6);
    config_host->set_margin_bottom(2);
    config_host->set_name(std::string("tpl-field-config-") + field_id);
    if (!is_buffer) append_config_editor(*config_host, f);
    outer->append(*config_host);

    m_field_list.append(*outer);
}

// ── Config sub-editors (s36) ─────────────────────────────────────────────────

const Folio::FieldSchema*
TemplateBuilderDialog::draft_field(const std::string& field_id) const {
    return m_draft.find_field(field_id);
}

void TemplateBuilderDialog::append_config_editor(Gtk::Box& host, const Folio::FieldSchema& f) {
    switch (f.type) {
        case Folio::FieldType::Number:
        case Folio::FieldType::Slider:       build_number_config(host, f.id); break;
        case Folio::FieldType::Dropdown:
        case Folio::FieldType::MultiSelect:  build_options_config(host, f.id); break;
        case Folio::FieldType::List:         build_presets_config(host, f.id); break;
        default: break;  // text/richtext/toggle/image/color/date/relation — no config UI yet
    }
}

namespace {
// Clean number → text for the config entries (no trailing zeros / dot).
std::string fmt_num(double d) {
    if (d == static_cast<double>(static_cast<long long>(d)))
        return std::to_string(static_cast<long long>(d));
    std::string s = std::to_string(d);
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}
double parse_num(Gtk::Entry* e, double dflt) {
    try { return std::stod(std::string(e->get_text())); } catch (...) { return dflt; }
}
}  // namespace

// number / slider: [ Min ___ ] [ Max ___ ] [ Step ___ ] writing {min,max,step}.
void TemplateBuilderDialog::build_number_config(Gtk::Box& host, const std::string& field_id) {
    while (Gtk::Widget* c = host.get_first_child()) host.remove(*c);
    const Folio::FieldSchema* f = draft_field(field_id);
    if (!f) return;

    auto* rowb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    auto add_field_entry = [&](const char* lbl, double val) -> Gtk::Entry* {
        auto* l = Gtk::make_managed<Gtk::Label>(lbl);
        l->add_css_class("dim-label");
        auto* e = Gtk::make_managed<Gtk::Entry>();
        e->set_width_chars(6);
        e->set_max_width_chars(7);
        e->set_text(fmt_num(val));
        rowb->append(*l);
        rowb->append(*e);
        return e;
    };
    auto* emin  = add_field_entry("Min",  Folio::config_num(f->config, "min",  0.0));
    auto* emax  = add_field_entry("Max",  Folio::config_num(f->config, "max",  100.0));
    auto* estep = add_field_entry("Step", Folio::config_num(f->config, "step", 1.0));

    auto settle = [this, field_id, emin, emax, estep]() {
        TemplateEdit::set_number_range(m_draft, field_id,
                                       parse_num(emin, 0.0),
                                       parse_num(emax, 100.0),
                                       parse_num(estep, 1.0));
    };
    emin->signal_changed().connect(settle);
    emax->signal_changed().connect(settle);
    estep->signal_changed().connect(settle);
    host.append(*rowb);
}

// dropdown / multiselect: an editable choice list — [ label ][ ✕ ] rows + add.
void TemplateBuilderDialog::build_options_config(Gtk::Box& host, const std::string& field_id) {
    while (Gtk::Widget* c = host.get_first_child()) host.remove(*c);
    const Folio::FieldSchema* f = draft_field(field_id);
    if (!f) return;

    auto* hint = Gtk::make_managed<Gtk::Label>("Choices");
    hint->add_css_class("dim-label");
    hint->set_halign(Gtk::Align::START);
    host.append(*hint);

    for (const auto& opt : Folio::config_options(f->config)) {
        const std::string oid = opt.id;
        auto* r = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        auto* e = Gtk::make_managed<Gtk::Entry>();
        e->set_text(opt.label);
        e->set_hexpand(true);
        e->signal_changed().connect([this, field_id, oid, e]() {
            TemplateEdit::rename_option(m_draft, field_id, oid,
                                        std::string(e->get_text()));
        });
        auto* del = Gtk::make_managed<Gtk::Button>();
        del->set_icon_name("user-trash-symbolic");
        del->add_css_class("flat");
        del->signal_clicked().connect([this, field_id, oid, &host]() {
            TemplateEdit::remove_option(m_draft, field_id, oid);
            build_options_config(host, field_id);   // refill this box only
        });
        r->append(*e);
        r->append(*del);
        host.append(*r);
    }

    auto* add = Gtk::make_managed<Gtk::Button>("+ Add choice");
    add->add_css_class("flat");
    add->set_halign(Gtk::Align::START);
    add->signal_clicked().connect([this, field_id, &host]() {
        TemplateEdit::add_option(m_draft, field_id, "Choice");
        build_options_config(host, field_id);
    });
    host.append(*add);
}

// list: a preset list — [ value ][ ✕ ] rows + add. Presets seed the value list.
void TemplateBuilderDialog::build_presets_config(Gtk::Box& host, const std::string& field_id) {
    while (Gtk::Widget* c = host.get_first_child()) host.remove(*c);
    const Folio::FieldSchema* f = draft_field(field_id);
    if (!f) return;

    auto* hint = Gtk::make_managed<Gtk::Label>("Presets (optional)");
    hint->add_css_class("dim-label");
    hint->set_halign(Gtk::Align::START);
    host.append(*hint);

    const auto presets = Folio::config_presets(f->config);
    for (std::size_t i = 0; i < presets.size(); ++i) {
        const std::size_t idx = i;
        auto* r = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        auto* e = Gtk::make_managed<Gtk::Entry>();
        e->set_text(presets[i]);
        e->set_hexpand(true);
        e->signal_changed().connect([this, field_id, idx, e]() {
            TemplateEdit::set_preset(m_draft, field_id, idx,
                                     std::string(e->get_text()));
        });
        auto* del = Gtk::make_managed<Gtk::Button>();
        del->set_icon_name("user-trash-symbolic");
        del->add_css_class("flat");
        del->signal_clicked().connect([this, field_id, idx, &host]() {
            TemplateEdit::remove_preset(m_draft, field_id, idx);
            build_presets_config(host, field_id);
        });
        r->append(*e);
        r->append(*del);
        host.append(*r);
    }

    auto* add = Gtk::make_managed<Gtk::Button>("+ Add preset");
    add->add_css_class("flat");
    add->set_halign(Gtk::Align::START);
    add->signal_clicked().connect([this, field_id, &host]() {
        TemplateEdit::add_preset(m_draft, field_id, "");
        build_presets_config(host, field_id);
    });
    host.append(*add);
}

void TemplateBuilderDialog::on_add_field() {
    // New fields default to Text with a generic label; the author renames/retypes
    // inline. add_field keeps the floor buffer at the bottom (§4).
    TemplateEdit::add_field(m_draft, Folio::FieldType::Text, "New field");
    rebuild_field_rows();
}

void TemplateBuilderDialog::on_add_section() {
    // s39 — a section heading is a Heading-typed marker field; it groups the fields
    // beneath it in the form. add_field keeps it above the trailing floor buffer.
    TemplateEdit::add_field(m_draft, Folio::FieldType::Heading, "Section");
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
