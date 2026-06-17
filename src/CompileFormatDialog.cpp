// ─────────────────────────────────────────────────────────────────────────────
// Folio — CompileFormatDialog.cpp  (s18)
// PDF compile-format editor. See CompileFormatDialog.hpp for the design + the
// validated read-only permission matrix.
// ─────────────────────────────────────────────────────────────────────────────
#include "CompileFormatDialog.hpp"
#include "CompileFormatIO.hpp"   // align/case token helpers reused for dropdowns
#include "FolioLog.hpp"

#include <cstdio>

namespace Folio {

// ElementMap slot order shown in m_slot_dd: body, 9 headings, 6 screenplay.
namespace {
constexpr int N_HEAD = OUTLINE_LEVELS;                                  // 9
constexpr int N_SP   = static_cast<int>(ScreenplayElement::COUNT);      // 6
const char* SP_NAMES[N_SP] = {"Scene", "Action", "Character",
                              "Parenthetical", "Dialogue", "Transition"};

std::string hex_from_rgba(const Gdk::RGBA& c) {
    char buf[16];
    std::snprintf(buf, sizeof buf, "#%02x%02x%02x",
                  (int)(c.get_red() * 255), (int)(c.get_green() * 255),
                  (int)(c.get_blue() * 255));
    return buf;
}
}  // namespace

// ─────────────────────────────────────────────────────────────────────────────
// Construction
// ─────────────────────────────────────────────────────────────────────────────
CompileFormatDialog::CompileFormatDialog(Gtk::Window& parent, FolioPrefs& prefs)
    : m_prefs(prefs) {
    set_transient_for(parent);
    set_modal(true);
    set_title("PDF Formats");
    set_default_size(720, 560);

    build_ui();
    wire_signals();
    rebuild_list();
}

// ─────────────────────────────────────────────────────────────────────────────
// Small layout helpers (mirror ExportDialog's make_section / make_row look)
// ─────────────────────────────────────────────────────────────────────────────
Gtk::Widget* CompileFormatDialog::section(const std::string& title) {
    auto* lbl = Gtk::make_managed<Gtk::Label>(title);
    lbl->add_css_class("export-section-title");
    lbl->set_halign(Gtk::Align::START);
    lbl->set_margin_top(8);
    return lbl;
}

Gtk::Widget* CompileFormatDialog::row(const std::string& label, Gtk::Widget& w) {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
    box->add_css_class("export-row");
    auto* lbl = Gtk::make_managed<Gtk::Label>(label);
    lbl->add_css_class("export-row-label");
    lbl->set_halign(Gtk::Align::START);
    lbl->set_hexpand(true);
    w.set_halign(Gtk::Align::END);
    box->append(*lbl);
    box->append(w);
    return box;
}

// ─────────────────────────────────────────────────────────────────────────────
// UI build
// ─────────────────────────────────────────────────────────────────────────────
void CompileFormatDialog::build_ui() {
    set_child(m_root);

    // ── Left: list + actions ──────────────────────────────────────────────────
    m_left.set_size_request(220, -1);
    m_left.add_css_class("export-left");
    m_list.set_selection_mode(Gtk::SelectionMode::SINGLE);
    m_list_scroll.set_child(m_list);
    m_list_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_list_scroll.set_vexpand(true);
    m_left.append(m_list_scroll);

    m_btn_new.set_label("+ New");
    m_btn_dup.set_label("Duplicate");
    m_btn_del.set_label("Delete");
    m_btn_new.add_css_class("pill-btn");
    m_btn_dup.add_css_class("pill-btn");
    m_btn_del.add_css_class("pill-btn");
    m_btn_new.set_hexpand(true);
    m_btn_dup.set_hexpand(true);
    m_btn_del.set_hexpand(true);
    m_list_actions.set_margin(6);
    m_list_actions.append(m_btn_new);
    m_list_actions.append(m_btn_dup);
    m_list_actions.append(m_btn_del);
    m_left.append(m_list_actions);

    m_root.append(m_left);
    m_root.append(*Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL));

    // ── Right: editor stack ───────────────────────────────────────────────────
    m_empty_lbl.set_text("Select a format to edit, or create a new one.");
    m_empty_lbl.add_css_class("dim-label");
    m_empty_lbl.set_halign(Gtk::Align::CENTER);
    m_empty_lbl.set_valign(Gtk::Align::CENTER);

    m_editor.set_margin(16);

    m_readonly_note.set_text("Built-in format — read-only. Use Duplicate to make "
                             "an editable copy.");
    m_readonly_note.add_css_class("dim-label");
    m_readonly_note.set_halign(Gtk::Align::START);
    m_readonly_note.set_wrap(true);
    m_editor.append(m_readonly_note);

    // Identity --------------------------------------------------------------
    m_editor.append(*section("Format"));
    m_editor.append(*row("Name", m_name_entry));

    m_radio_formal.set_label("Formal (impose format)");
    m_radio_adaptable.set_label("Adaptable (honor document styling)");
    m_radio_adaptable.set_group(m_radio_formal);
    auto* mode_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    mode_box->append(m_radio_formal);
    mode_box->append(m_radio_adaptable);
    m_editor.append(*row("Render mode", *mode_box));

    m_chk_hyphenate.set_label("Auto-hyphenate (Adaptable, justified)");
    m_editor.append(m_chk_hyphenate);
    m_chk_pb_top.set_label("Page break before top-level heading");
    m_editor.append(m_chk_pb_top);

    // Page ------------------------------------------------------------------
    m_editor.append(*section("Page"));
    auto paper_list = Gtk::StringList::create({"Letter", "A4", "Legal", "Custom"});
    m_paper_dd = Gtk::make_managed<Gtk::DropDown>(paper_list);
    m_editor.append(*row("Paper size", *m_paper_dd));

    auto orient_list = Gtk::StringList::create({"Portrait", "Landscape"});
    m_orient_dd = Gtk::make_managed<Gtk::DropDown>(orient_list);
    m_editor.append(*row("Orientation", *m_orient_dd));

    m_cw_spin = Gtk::make_managed<Gtk::SpinButton>();
    m_cw_spin->set_adjustment(Gtk::Adjustment::create(0, 0, 5000, 1, 36));
    m_ch_spin = Gtk::make_managed<Gtk::SpinButton>();
    m_ch_spin->set_adjustment(Gtk::Adjustment::create(0, 0, 5000, 1, 36));
    m_custom_dim_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
    m_custom_dim_row->append(*Gtk::make_managed<Gtk::Label>("W"));
    m_custom_dim_row->append(*m_cw_spin);
    m_custom_dim_row->append(*Gtk::make_managed<Gtk::Label>("H"));
    m_custom_dim_row->append(*m_ch_spin);
    m_editor.append(*row("Custom size (pt)", *m_custom_dim_row));

    auto mk_margin = [&]() {
        auto* sp = Gtk::make_managed<Gtk::SpinButton>();
        sp->set_adjustment(Gtk::Adjustment::create(0, 0, 720, 1, 9));
        sp->set_digits(1);
        return sp;
    };
    m_mi_spin = mk_margin(); m_mo_spin = mk_margin();
    m_mt_spin = mk_margin(); m_mb_spin = mk_margin();
    {
        auto* r1 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        auto* l_in = Gtk::make_managed<Gtk::Label>("Inner");  m_mi_lbl = l_in;
        auto* l_out= Gtk::make_managed<Gtk::Label>("Outer");  m_mo_lbl = l_out;
        r1->append(*l_in);  r1->append(*m_mi_spin);
        r1->append(*l_out); r1->append(*m_mo_spin);
        m_editor.append(*row("Margins (pt)", *r1));
        auto* r2 = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        r2->append(*Gtk::make_managed<Gtk::Label>("Top"));    r2->append(*m_mt_spin);
        r2->append(*Gtk::make_managed<Gtk::Label>("Bottom")); r2->append(*m_mb_spin);
        m_editor.append(*row("", *r2));
    }
    m_chk_mirror.set_label("Mirror margins on facing pages (binding gutter)");
    m_editor.append(m_chk_mirror);

    // Furniture -------------------------------------------------------------
    m_editor.append(*section("Furniture"));
    m_chk_title_page.set_label("Title page");
    m_editor.append(m_chk_title_page);
    m_chk_restart.set_label("Restart page numbers per section");
    m_editor.append(m_chk_restart);

    auto* tok_hint = Gtk::make_managed<Gtk::Label>(
        "Tokens: {title} {author} {page} {chapter}");
    tok_hint->add_css_class("dim-label");
    tok_hint->set_halign(Gtk::Align::START);
    m_editor.append(*tok_hint);

    m_chk_header.set_label("Running header");
    m_editor.append(m_chk_header);
    {
        auto* hb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        m_hdr_l.set_placeholder_text("left");   m_hdr_l.set_hexpand(true);
        m_hdr_c.set_placeholder_text("center");  m_hdr_c.set_hexpand(true);
        m_hdr_r.set_placeholder_text("right");   m_hdr_r.set_hexpand(true);
        hb->append(m_hdr_l); hb->append(m_hdr_c); hb->append(m_hdr_r);
        m_editor.append(*row("Header slots", *hb));
    }
    m_chk_footer.set_label("Running footer");
    m_editor.append(m_chk_footer);
    {
        auto* fb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        m_ftr_l.set_placeholder_text("left");   m_ftr_l.set_hexpand(true);
        m_ftr_c.set_placeholder_text("center");  m_ftr_c.set_hexpand(true);
        m_ftr_r.set_placeholder_text("right");   m_ftr_r.set_hexpand(true);
        fb->append(m_ftr_l); fb->append(m_ftr_c); fb->append(m_ftr_r);
        m_editor.append(*row("Footer slots", *fb));
    }

    // Element map -----------------------------------------------------------
    m_editor.append(*section("Element styling"));
    std::vector<Glib::ustring> slots;
    slots.push_back("Body");
    for (int i = 0; i < N_HEAD; ++i) slots.push_back("Heading " + std::to_string(i + 1));
    for (int i = 0; i < N_SP; ++i)   slots.push_back(std::string("Screenplay · ") + SP_NAMES[i]);
    auto slot_list = Gtk::StringList::create(slots);
    m_slot_dd = Gtk::make_managed<Gtk::DropDown>(slot_list);
    m_editor.append(*row("Element", *m_slot_dd));

    m_tf_font.set_placeholder_text("inherit base font");
    m_editor.append(*row("Font family", m_tf_font));

    m_tf_size = Gtk::make_managed<Gtk::SpinButton>();
    m_tf_size->set_adjustment(Gtk::Adjustment::create(0, 0, 96, 1, 2));
    m_tf_size->set_digits(1);
    m_tf_size->set_tooltip_text("0 = inherit");
    m_editor.append(*row("Font size (pt, 0=inherit)", *m_tf_size));

    m_tf_bold.set_label("B"); m_tf_italic.set_label("I"); m_tf_underline.set_label("U");
    auto* style_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    style_box->append(m_tf_bold); style_box->append(m_tf_italic); style_box->append(m_tf_underline);
    m_editor.append(*row("Weight / style", *style_box));

    auto align_list = Gtk::StringList::create({"Left", "Center", "Right", "Justify"});
    m_tf_align_dd = Gtk::make_managed<Gtk::DropDown>(align_list);
    m_editor.append(*row("Alignment", *m_tf_align_dd));

    auto case_list = Gtk::StringList::create({"As-is", "UPPER", "lower", "Title"});
    m_tf_case_dd = Gtk::make_managed<Gtk::DropDown>(case_list);
    m_editor.append(*row("Text case", *m_tf_case_dd));

    m_tf_ls = Gtk::make_managed<Gtk::SpinButton>();
    m_tf_ls->set_adjustment(Gtk::Adjustment::create(0, 0, 5, 0.05, 0.25));
    m_tf_ls->set_digits(2);
    m_tf_ls->set_tooltip_text("Multiple of single; 0 = inherit");
    m_editor.append(*row("Line spacing (0=inherit)", *m_tf_ls));

    auto mk_pt = [&]() {
        auto* sp = Gtk::make_managed<Gtk::SpinButton>();
        sp->set_adjustment(Gtk::Adjustment::create(0, 0, 720, 1, 9));
        sp->set_digits(1);
        return sp;
    };
    m_tf_above = mk_pt(); m_tf_below = mk_pt();
    {
        auto* sb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        sb->append(*Gtk::make_managed<Gtk::Label>("Above")); sb->append(*m_tf_above);
        sb->append(*Gtk::make_managed<Gtk::Label>("Below")); sb->append(*m_tf_below);
        m_editor.append(*row("Space (pt)", *sb));
    }
    m_tf_il = mk_pt(); m_tf_ir = mk_pt(); m_tf_fl = mk_pt();
    {
        auto* ib = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        ib->append(*Gtk::make_managed<Gtk::Label>("Left"));  ib->append(*m_tf_il);
        ib->append(*Gtk::make_managed<Gtk::Label>("Right")); ib->append(*m_tf_ir);
        m_editor.append(*row("Indent (pt)", *ib));
        auto* fb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 12);
        fb->append(*Gtk::make_managed<Gtk::Label>("First line")); fb->append(*m_tf_fl);
        m_editor.append(*row("", *fb));
    }

    {
        auto color_dlg = Gtk::ColorDialog::create();
        m_tf_color = Gtk::make_managed<Gtk::ColorDialogButton>(color_dlg);
        auto* cb = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        m_tf_color_chk.set_label("Set colour");
        cb->append(m_tf_color_chk);
        cb->append(*m_tf_color);
        m_editor.append(*row("Text colour", *cb));
    }

    m_editor_scroll.set_child(m_editor);
    m_editor_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_editor_scroll.set_hexpand(true);
    m_editor_scroll.set_vexpand(true);

    m_stack.add(m_empty_lbl, "empty");
    m_stack.add(m_editor_scroll, "editor");
    m_stack.set_visible_child("empty");
    m_stack.set_hexpand(true);
    m_root.append(m_stack);
}

// ─────────────────────────────────────────────────────────────────────────────
// Selection helpers
// ─────────────────────────────────────────────────────────────────────────────
bool CompileFormatDialog::selected_is_builtin() const {
    if (m_sel_idx < 0 || m_sel_idx >= (int)m_all.size()) return false;
    return m_all[m_sel_idx].builtin;
}

int CompileFormatDialog::custom_index_for(int all_idx) const {
    int n_builtin = (int)builtin_compile_formats().size();
    if (all_idx < n_builtin) return -1;
    int ci = all_idx - n_builtin;
    if (ci < 0 || ci >= (int)m_prefs.custom_compile_formats.size()) return -1;
    return ci;
}

CompileFormat* CompileFormatDialog::working() {
    int ci = custom_index_for(m_sel_idx);
    if (ci < 0) return nullptr;
    return &m_prefs.custom_compile_formats[ci];
}

TextFormat* CompileFormatDialog::active_slot_tf(CompileFormat& f) {
    if (m_slot_idx == 0) return &f.elements.body;
    if (m_slot_idx <= N_HEAD) return &f.elements.heading[m_slot_idx - 1];
    int sp = m_slot_idx - 1 - N_HEAD;
    if (sp >= 0 && sp < N_SP) return &f.elements.screenplay[sp];
    return &f.elements.body;
}

// ─────────────────────────────────────────────────────────────────────────────
// List
// ─────────────────────────────────────────────────────────────────────────────
Gtk::ListBoxRow* CompileFormatDialog::make_list_row(const CompileFormat& f) {
    auto* row_w = Gtk::make_managed<Gtk::ListBoxRow>();
    row_w->set_child(*make_list_row_child(f));
    return row_w;
}

Gtk::Widget* CompileFormatDialog::make_list_row_child(const CompileFormat& f) {
    auto* box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    box->set_margin(8);
    auto* name = Gtk::make_managed<Gtk::Label>(f.name);
    name->set_halign(Gtk::Align::START);
    box->append(*name);
    auto* sub = Gtk::make_managed<Gtk::Label>(
        f.builtin ? "built-in" :
        (f.mode == RenderMode::Formal ? "custom · Formal" : "custom · Adaptable"));
    sub->add_css_class("dim-label");
    sub->set_halign(Gtk::Align::START);
    box->append(*sub);
    return box;
}

void CompileFormatDialog::rebuild_list(const std::string& select_name) {
    m_inhibit = true;
    while (auto* c = m_list.get_row_at_index(0)) m_list.remove(*c);

    m_all = m_prefs.all_compile_formats();
    int want = -1;
    for (int i = 0; i < (int)m_all.size(); ++i) {
        m_list.append(*make_list_row(m_all[i]));
        if (!select_name.empty() && m_all[i].name == select_name) want = i;
    }
    m_inhibit = false;

    if (want < 0) want = m_all.empty() ? -1 : 0;
    if (want >= 0) select_all_index(want);
    else { m_sel_idx = -1; m_stack.set_visible_child("empty"); }

    // Let the Export picker mirror the new set + selection. Fires after the
    // selection settles, so it carries the right name (unlike a per-field
    // persist(), which fires before a structural rebuild re-selects).
    if (on_formats_changed) {
        std::string sel = (m_sel_idx >= 0 && m_sel_idx < (int)m_all.size())
                              ? m_all[m_sel_idx].name : std::string();
        on_formats_changed(sel);
    }
}

void CompileFormatDialog::select_all_index(int all_idx) {
    if (all_idx < 0 || all_idx >= (int)m_all.size()) return;
    m_sel_idx = all_idx;
    if (auto* r = m_list.get_row_at_index(all_idx)) {
        m_inhibit = true;
        m_list.select_row(*r);
        m_inhibit = false;
    }
    load_format_to_editor();
}

// ─────────────────────────────────────────────────────────────────────────────
// Load model → controls
// ─────────────────────────────────────────────────────────────────────────────
void CompileFormatDialog::load_format_to_editor() {
    if (m_sel_idx < 0 || m_sel_idx >= (int)m_all.size()) {
        m_stack.set_visible_child("empty");
        return;
    }
    m_stack.set_visible_child("editor");
    const CompileFormat& f = m_all[m_sel_idx];

    m_inhibit = true;

    m_name_entry.set_text(f.name);
    m_radio_formal.set_active(f.mode == RenderMode::Formal);
    m_radio_adaptable.set_active(f.mode == RenderMode::Adaptable);
    m_chk_hyphenate.set_active(f.hyphenate);
    m_chk_pb_top.set_active(f.page_break_before_top_heading);

    m_paper_dd->set_selected((guint)f.page.size);
    m_orient_dd->set_selected((guint)f.page.orientation);
    m_cw_spin->set_value(f.page.custom_w_pt);
    m_ch_spin->set_value(f.page.custom_h_pt);
    m_mi_spin->set_value(f.page.margin_inner_pt);
    m_mo_spin->set_value(f.page.margin_outer_pt);
    m_mt_spin->set_value(f.page.margin_top_pt);
    m_mb_spin->set_value(f.page.margin_bottom_pt);
    m_chk_mirror.set_active(f.page.mirror_margins);

    m_chk_title_page.set_active(f.furniture.title_page);
    m_chk_restart.set_active(f.furniture.restart_numbers_per_section);
    m_chk_header.set_active(f.furniture.header_enabled);
    m_hdr_l.set_text(f.furniture.header.left);
    m_hdr_c.set_text(f.furniture.header.center);
    m_hdr_r.set_text(f.furniture.header.right);
    m_chk_footer.set_active(f.furniture.footer_enabled);
    m_ftr_l.set_text(f.furniture.footer.left);
    m_ftr_c.set_text(f.furniture.footer.center);
    m_ftr_r.set_text(f.furniture.footer.right);

    m_inhibit = false;

    load_slot_to_tf();        // fills the TextFormat controls for the active slot
    update_sensitivity();
}

void CompileFormatDialog::load_slot_to_tf() {
    if (m_sel_idx < 0 || m_sel_idx >= (int)m_all.size()) return;
    CompileFormat& f = m_all[m_sel_idx];   // read-only snapshot copy is fine here
    const TextFormat& tf = *active_slot_tf(f);

    m_inhibit = true;
    m_tf_font.set_text(tf.font_family);
    m_tf_size->set_value(tf.font_size_pt);
    m_tf_bold.set_active(tf.bold);
    m_tf_italic.set_active(tf.italic);
    m_tf_underline.set_active(tf.underline);
    m_tf_align_dd->set_selected((guint)tf.align);
    m_tf_case_dd->set_selected((guint)tf.text_case);
    m_tf_ls->set_value(tf.line_spacing);
    m_tf_above->set_value(tf.space_above_pt);
    m_tf_below->set_value(tf.space_below_pt);
    m_tf_il->set_value(tf.indent_left_pt);
    m_tf_ir->set_value(tf.indent_right_pt);
    m_tf_fl->set_value(tf.first_line_pt);
    bool has_color = !tf.color_hex.empty();
    m_tf_color_chk.set_active(has_color);
    if (has_color) {
        Gdk::RGBA c; c.set(tf.color_hex);
        m_tf_color->set_rgba(c);
    }
    m_inhibit = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// Commit controls → model (custom only)
// ─────────────────────────────────────────────────────────────────────────────
void CompileFormatDialog::commit_tf_from_controls() {
    CompileFormat* f = working();
    if (!f) return;
    TextFormat* tf = active_slot_tf(*f);
    tf->font_family    = m_tf_font.get_text();
    tf->font_size_pt   = m_tf_size->get_value();
    tf->bold           = m_tf_bold.get_active();
    tf->italic         = m_tf_italic.get_active();
    tf->underline      = m_tf_underline.get_active();
    tf->align          = (Align)m_tf_align_dd->get_selected();
    tf->text_case      = (TextCase)m_tf_case_dd->get_selected();
    tf->line_spacing   = m_tf_ls->get_value();
    tf->space_above_pt = m_tf_above->get_value();
    tf->space_below_pt = m_tf_below->get_value();
    tf->indent_left_pt = m_tf_il->get_value();
    tf->indent_right_pt= m_tf_ir->get_value();
    tf->first_line_pt  = m_tf_fl->get_value();
    tf->color_hex      = m_tf_color_chk.get_active()
                           ? hex_from_rgba(m_tf_color->get_rgba()) : "";
    persist();
}

void CompileFormatDialog::commit_format_from_controls() {
    CompileFormat* f = working();
    if (!f) return;
    f->name      = m_name_entry.get_text();
    f->builtin   = false;
    f->mode      = m_radio_formal.get_active() ? RenderMode::Formal : RenderMode::Adaptable;
    f->hyphenate = m_chk_hyphenate.get_active();
    f->page_break_before_top_heading = m_chk_pb_top.get_active();

    f->page.size        = (PaperSize)m_paper_dd->get_selected();
    f->page.orientation = (Orientation)m_orient_dd->get_selected();
    f->page.custom_w_pt = m_cw_spin->get_value();
    f->page.custom_h_pt = m_ch_spin->get_value();
    f->page.margin_inner_pt  = m_mi_spin->get_value();
    f->page.margin_outer_pt  = m_mo_spin->get_value();
    f->page.margin_top_pt    = m_mt_spin->get_value();
    f->page.margin_bottom_pt = m_mb_spin->get_value();
    f->page.mirror_margins   = m_chk_mirror.get_active();

    f->furniture.title_page = m_chk_title_page.get_active();
    f->furniture.restart_numbers_per_section = m_chk_restart.get_active();
    f->furniture.header_enabled = m_chk_header.get_active();
    f->furniture.header.left   = m_hdr_l.get_text();
    f->furniture.header.center = m_hdr_c.get_text();
    f->furniture.header.right  = m_hdr_r.get_text();
    f->furniture.footer_enabled = m_chk_footer.get_active();
    f->furniture.footer.left   = m_ftr_l.get_text();
    f->furniture.footer.center = m_ftr_c.get_text();
    f->furniture.footer.right  = m_ftr_r.get_text();

    persist();
}

void CompileFormatDialog::persist() {
    try { m_prefs.save(); } catch (...) {
        LOG_WARN("CompileFormatDialog: prefs save failed");
    }
    // Keep the snapshot + the list label (name/mode subtitle) current without
    // disturbing the selection.
    m_all = m_prefs.all_compile_formats();
    if (auto* r = m_list.get_row_at_index(m_sel_idx)) {
        // refresh just this row's labels by replacing its child
        if (m_sel_idx >= 0 && m_sel_idx < (int)m_all.size())
            r->set_child(*make_list_row_child(m_all[m_sel_idx]));
    }
    if (on_formats_changed) on_formats_changed(m_name_entry.get_text());
}

// ─────────────────────────────────────────────────────────────────────────────
// Sensitivity: read-only gating + paper/mirror dependents
// ─────────────────────────────────────────────────────────────────────────────
void CompileFormatDialog::update_sensitivity() {
    bool editable = (m_sel_idx >= 0) && !selected_is_builtin();
    m_readonly_note.set_visible(selected_is_builtin());

    // Delete: custom only.
    m_btn_del.set_sensitive(editable);
    // Duplicate / New: always (when something is selected for Duplicate).
    m_btn_dup.set_sensitive(m_sel_idx >= 0);

    // All edit controls follow `editable`.
    for (Gtk::Widget* w : std::initializer_list<Gtk::Widget*>{
            &m_name_entry, &m_radio_formal, &m_radio_adaptable, &m_chk_hyphenate,
            &m_chk_pb_top, m_paper_dd, m_orient_dd, m_cw_spin, m_ch_spin,
            m_mi_spin, m_mo_spin, m_mt_spin, m_mb_spin, &m_chk_mirror,
            &m_chk_title_page, &m_chk_restart, &m_chk_header,
            &m_hdr_l, &m_hdr_c, &m_hdr_r, &m_chk_footer, &m_ftr_l, &m_ftr_c, &m_ftr_r,
            m_slot_dd, &m_tf_font, m_tf_size, &m_tf_bold, &m_tf_italic, &m_tf_underline,
            m_tf_align_dd, m_tf_case_dd, m_tf_ls, m_tf_above, m_tf_below,
            m_tf_il, m_tf_ir, m_tf_fl, &m_tf_color_chk, m_tf_color}) {
        if (w) w->set_sensitive(editable);
    }

    // Custom-size spinners only when paper == Custom.
    bool is_custom = (m_paper_dd->get_selected() == (guint)PaperSize::Custom);
    if (m_custom_dim_row) m_custom_dim_row->set_sensitive(editable && is_custom);

    // Margin labels reflect mirror state.
    bool mirror = m_chk_mirror.get_active();
    if (m_mi_lbl) m_mi_lbl->set_text(mirror ? "Inner" : "Left");
    if (m_mo_lbl) m_mo_lbl->set_text(mirror ? "Outer" : "Right");
}

// ─────────────────────────────────────────────────────────────────────────────
// Signal wiring
// ─────────────────────────────────────────────────────────────────────────────
void CompileFormatDialog::wire_signals() {
    // List selection.
    m_list.signal_row_selected().connect([this](Gtk::ListBoxRow* r) {
        if (m_inhibit) return;
        if (!r) return;
        m_sel_idx = r->get_index();
        m_slot_idx = 0;
        if (m_slot_dd) { m_inhibit = true; m_slot_dd->set_selected(0); m_inhibit = false; }
        load_format_to_editor();
    });

    // New — duplicate the current selection (or a manuscript default) as a custom.
    m_btn_new.signal_clicked().connect([this]() {
        CompileFormat base = (m_sel_idx >= 0 && m_sel_idx < (int)m_all.size())
                                 ? m_all[m_sel_idx] : preset_manuscript();
        base.builtin = false;
        base.name = "New Format";
        m_prefs.custom_compile_formats.push_back(base);
        try { m_prefs.save(); } catch (...) { LOG_WARN("save failed (new format)"); }
        rebuild_list(base.name);
    });

    // Duplicate — copy current selection into a new custom.
    m_btn_dup.signal_clicked().connect([this]() {
        if (m_sel_idx < 0 || m_sel_idx >= (int)m_all.size()) return;
        CompileFormat copy = m_all[m_sel_idx];
        copy.builtin = false;
        copy.name = copy.name + " copy";
        m_prefs.custom_compile_formats.push_back(copy);
        try { m_prefs.save(); } catch (...) { LOG_WARN("save failed (duplicate)"); }
        rebuild_list(copy.name);
    });

    // Delete — custom only.
    m_btn_del.signal_clicked().connect([this]() {
        int ci = custom_index_for(m_sel_idx);
        if (ci < 0) return;
        m_prefs.custom_compile_formats.erase(
            m_prefs.custom_compile_formats.begin() + ci);
        try { m_prefs.save(); } catch (...) { LOG_WARN("save failed (delete)"); }
        rebuild_list();
    });

    // Identity + page + furniture: commit whole-format on change.
    auto commit_fmt = [this]() { if (!m_inhibit) commit_format_from_controls(); };
    m_name_entry.signal_changed().connect(commit_fmt);
    m_radio_formal.signal_toggled().connect(commit_fmt);
    m_chk_hyphenate.signal_toggled().connect(commit_fmt);
    m_chk_pb_top.signal_toggled().connect(commit_fmt);
    m_orient_dd->property_selected().signal_changed().connect(commit_fmt);
    m_cw_spin->signal_value_changed().connect(commit_fmt);
    m_ch_spin->signal_value_changed().connect(commit_fmt);
    m_mi_spin->signal_value_changed().connect(commit_fmt);
    m_mo_spin->signal_value_changed().connect(commit_fmt);
    m_mt_spin->signal_value_changed().connect(commit_fmt);
    m_mb_spin->signal_value_changed().connect(commit_fmt);
    m_chk_title_page.signal_toggled().connect(commit_fmt);
    m_chk_restart.signal_toggled().connect(commit_fmt);
    m_hdr_l.signal_changed().connect(commit_fmt);
    m_hdr_c.signal_changed().connect(commit_fmt);
    m_hdr_r.signal_changed().connect(commit_fmt);
    m_ftr_l.signal_changed().connect(commit_fmt);
    m_ftr_c.signal_changed().connect(commit_fmt);
    m_ftr_r.signal_changed().connect(commit_fmt);

    // Paper change also re-gates the custom-size row.
    m_paper_dd->property_selected().signal_changed().connect([this]() {
        if (m_inhibit) return;
        commit_format_from_controls();
        update_sensitivity();
    });
    // Mirror change re-labels the margin fields.
    m_chk_mirror.signal_toggled().connect([this]() {
        if (m_inhibit) return;
        commit_format_from_controls();
        update_sensitivity();
    });
    // Header/footer enable just commits (slots stay visible/editable regardless).
    m_chk_header.signal_toggled().connect(commit_fmt);
    m_chk_footer.signal_toggled().connect(commit_fmt);

    // Element-map slot switch: load that slot into the TF controls.
    m_slot_dd->property_selected().signal_changed().connect([this]() {
        if (m_inhibit) return;
        m_slot_idx = (int)m_slot_dd->get_selected();
        load_slot_to_tf();
    });

    // TextFormat controls: commit the active slot on change.
    auto commit_tf = [this]() { if (!m_inhibit) commit_tf_from_controls(); };
    m_tf_font.signal_changed().connect(commit_tf);
    m_tf_size->signal_value_changed().connect(commit_tf);
    m_tf_bold.signal_toggled().connect(commit_tf);
    m_tf_italic.signal_toggled().connect(commit_tf);
    m_tf_underline.signal_toggled().connect(commit_tf);
    m_tf_align_dd->property_selected().signal_changed().connect(commit_tf);
    m_tf_case_dd->property_selected().signal_changed().connect(commit_tf);
    m_tf_ls->signal_value_changed().connect(commit_tf);
    m_tf_above->signal_value_changed().connect(commit_tf);
    m_tf_below->signal_value_changed().connect(commit_tf);
    m_tf_il->signal_value_changed().connect(commit_tf);
    m_tf_ir->signal_value_changed().connect(commit_tf);
    m_tf_fl->signal_value_changed().connect(commit_tf);
    m_tf_color_chk.signal_toggled().connect(commit_tf);
    m_tf_color->property_rgba().signal_changed().connect(commit_tf);
}

} // namespace Folio
