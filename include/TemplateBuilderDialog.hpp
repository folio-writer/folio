#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — TemplateBuilderDialog.hpp   (s33 — the schema editor; GTK/gtkmm4)
//
// §7 of the objects design made visible: "the door you walk through only when you
// want more." Opened from the object form's "Edit fields…" affordance, it edits a
// template's SCHEMA — name the type, add / remove / reorder / rename / retype
// fields — and hands the finished schema back via a callback. It is the thin GTK
// surface over the pure TemplateEdit brain; no schema logic lives here.
//
// COMMIT CONTRACT (the s24 modal rule): the dialog edits a LOCAL COPY (m_draft)
// and never touches the model. On Save it validates, fires the apply callback
// with the draft, and hides; the receiver (Inspector) installs it into the store
// and repopulates the form on an idle tick — once nothing modal is on screen.
//
// SCOPE this slice: schema editing of EXISTING templates (Character/Place + any
// present). Per-field CONFIG editors (slider range, dropdown options, relation
// target) and brand-new instantiable types are follow-ups — fields added here
// carry empty/default config and render through the existing FormPlan path.
// ─────────────────────────────────────────────────────────────────────────────

#include "Object.hpp"
#include "TemplateEdit.hpp"
#include "FormPlan.hpp"   // s36 — config accessors (config_options/presets/num)

#include <gtkmm.h>
#include <functional>
#include <string>
#include <vector>

namespace Folio {

class TemplateBuilderDialog : public Gtk::Window {
public:
    // Fired on Save with the finished (validated, floor-buffer-ensured) template.
    using ApplyCallback = std::function<void(const Folio::Template&)>;

    explicit TemplateBuilderDialog(Gtk::Window& parent);

    void set_apply_callback(ApplyCallback cb) { m_on_apply = std::move(cb); }

    // Load `tmpl` as the editable draft and (re)build the rows. Safe to call
    // repeatedly on the one owned instance (persistent-window rule).
    void open_for(const Folio::Template& tmpl);

private:
    ApplyCallback   m_on_apply;
    Folio::Template m_draft;          // the local copy being edited

    // Chrome
    Gtk::Box            m_root{Gtk::Orientation::VERTICAL, 0};
    Gtk::Entry          m_type_name_entry;
    Gtk::DropDown*      m_category_dd = nullptr;   // s39 — character/place/reference
    Gtk::ScrolledWindow m_scroll;
    Gtk::Box            m_field_list{Gtk::Orientation::VERTICAL, 0};
    Gtk::Label          m_error_label;

    void build_chrome();
    void rebuild_field_rows();
    void append_field_row(const Folio::FieldSchema& f, bool is_buffer);
    void on_add_field();
    void on_add_section();   // s39 — append a Heading marker
    void on_save();

    // s36 — per-type CONFIG sub-editors, hosted under each field row. Routed by
    // type; number/slider edit {min,max,step}, dropdown/multiselect edit the
    // {options} choice list, list edits {presets}. Each rebuilds only its own
    // host box on add/remove (focus stays put); a RETYPE rebuilds the whole row
    // list on idle so the sub-editor follows the new type.
    void append_config_editor(Gtk::Box& host, const Folio::FieldSchema& f);
    void build_number_config(Gtk::Box& host, const std::string& field_id);
    void build_options_config(Gtk::Box& host, const std::string& field_id);
    void build_presets_config(Gtk::Box& host, const std::string& field_id);
    const Folio::FieldSchema* draft_field(const std::string& field_id) const;

    // FieldType <-> dropdown index over the pickable type list.
    static Gtk::DropDown* make_type_dropdown(Folio::FieldType current);
    static Folio::FieldType type_for_index(unsigned int idx);
    static unsigned int     index_for_type(Folio::FieldType t);
};

}  // namespace Folio
