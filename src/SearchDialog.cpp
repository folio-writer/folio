// ─────────────────────────────────────────────────────────────────────────────
// Folio — SearchDialog.cpp  (VSCode-style layout)
// ─────────────────────────────────────────────────────────────────────────────
#include "SearchDialog.hpp"
#include <sstream>

namespace Folio {

static const char* SEARCH_CSS = R"CSS(
.sd-panel {
    background-color: @adw_surface;
}
.sd-input-area {
    background-color: @adw_surface;
    border-bottom: 1px solid @border_strong;
    padding: 8px 8px 6px 8px;
}
.sd-scope-bar {
    background-color: @adw_surface_1;
    border-bottom: 1px solid @border_light;
    padding: 4px 8px;
}
.sd-scope-menu-btn {
    font-size: 11px; font-weight: 700;
    padding: 1px 6px;
    min-height: 0;
    border-radius: 4px;
    background: transparent;
    border: none;
    box-shadow: none;
    color: @tx3;
}
.sd-scope-menu-btn:hover { background-color: @adw_overlay; color: @tx1; }
.sd-scope-lbl { font-size: 11px; }
.sd-scope-popover > contents {
    padding: 8px;
}
.sd-scope-col {
    padding: 4px;
    min-width: 130px;
}
.sd-scope-col-hdr {
    font-size: 11px;
    font-weight: bold;
    color: @tx3;
    padding-bottom: 4px;
}
.sd-scope-ck {
    font-size: 12px;
    padding: 1px 0;
    min-height: 0;
}
.sd-entry-row {
    margin-bottom: 4px;
}
/* Case/word/regex toggles use fmt-btn from main stylesheet */
.sd-collapse-btn {
    padding: 0 4px;
    min-height: 0;
    border: none;
    background: transparent;
}
.sd-results-box { background-color: @adw_surface; }
.sd-group-header {
    padding: 4px 8px 2px 8px;
    border-bottom: 1px solid @border_light;
}
.sd-group-title { font-size: 12px; font-weight: bold; }
.sd-group-count { font-size: 11px; color: @tx3; }
.sd-match-row {
    padding: 2px 8px 2px 8px;
    border-radius: 0;
    min-height: 0;
}
.sd-match-row:hover { background-color: @adw_overlay; }
.sd-match-loc {
    font-size: 11px;
    font-family: monospace;
    color: @tx4;
    min-width: 52px;
}
.sd-match-row label { font-size: 12px; font-family: monospace; color: @tx2; }
.sd-empty { color: @tx3; font-size: 13px; margin: 48px; }
.sd-count-lbl { font-size: 11px; color: @tx3; padding: 0 4px; }
.sd-replace-btn { font-size: 11px; padding: 2px 8px; min-height: 0; }
)CSS";

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────

SearchDialog::SearchDialog(Gtk::Window& parent, DocumentModel& model)
    : m_model(model)
{
    auto provider = Gtk::CssProvider::create();
    provider->load_from_data(SEARCH_CSS);
    Gtk::StyleContext::add_provider_for_display(
        Gdk::Display::get_default(), provider,
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    set_transient_for(parent);
    set_modal(false);
    set_title("Search & Replace");
    set_default_size(640, 560);
    set_resizable(true);
    set_hide_on_close(true);

    auto kc = Gtk::EventControllerKey::create();
    kc->signal_key_pressed().connect(
        [this](guint kv, guint, Gdk::ModifierType) -> bool {
            if (kv == GDK_KEY_Escape) { close(); return true; }
            return false;
        }, false);
    add_controller(kc);

    build_ui();
    set_child(m_root);
}

SearchDialog::~SearchDialog() {
    m_debounce_conn.disconnect();
}

// ─────────────────────────────────────────────────────────────────────────────
// build_ui
// ─────────────────────────────────────────────────────────────────────────────

void SearchDialog::build_ui() {
    m_root.add_css_class("sd-panel");

    build_input_area();
    build_scope_bar();
    build_results_area();
}

// ─────────────────────────────────────────────────────────────────────────────
// build_input_area  — search + replace fields stacked, VSCode style
// ─────────────────────────────────────────────────────────────────────────────

void SearchDialog::build_input_area() {
    auto* input_area = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    input_area->add_css_class("sd-input-area");

    // ── Search row ────────────────────────────────────────────────────────────
    auto* search_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    search_row->add_css_class("sd-entry-row");

    // Collapse/expand replace toggle (chevron)
    m_replace_toggle = Gtk::make_managed<Gtk::ToggleButton>();
    m_replace_toggle->set_icon_name("pan-end-symbolic");
    m_replace_toggle->add_css_class("flat");
    m_replace_toggle->add_css_class("sd-collapse-btn");
    m_replace_toggle->set_tooltip_text("Toggle replace");
    m_replace_toggle->signal_toggled().connect([this]{
        bool show = m_replace_toggle->get_active();
        m_replace_toggle->set_icon_name(
            show ? "pan-down-symbolic" : "pan-end-symbolic");
        m_replace_revealer.set_reveal_child(show);
    });

    // Search entry
    m_query_entry = Gtk::make_managed<Gtk::Entry>();
    m_query_entry->set_placeholder_text("Search");
    m_query_entry->set_hexpand(true);
    m_query_entry->signal_changed().connect([this]{ schedule_search(); });
    auto qkc = Gtk::EventControllerKey::create();
    qkc->signal_key_pressed().connect(
        [this](guint kv, guint, Gdk::ModifierType /*mods*/) -> bool {
            if (kv == GDK_KEY_Return || kv == GDK_KEY_KP_Enter) {
                run_search(); return true;
            }
            return false;
        }, false);
    m_query_entry->add_controller(qkc);

    // Inline option toggle buttons (inside search row, right-aligned)
    m_case_btn  = make_field_toggle("Aa", "Case sensitive");
    m_word_btn  = make_field_toggle("ab|", "Whole word");  // approximation
    m_regex_btn = make_field_toggle(".*", "Regular expression");

    m_count_lbl = Gtk::make_managed<Gtk::Label>("");
    m_count_lbl->add_css_class("sd-count-lbl");
    m_count_lbl->set_xalign(1.0f);
    m_count_lbl->set_width_chars(12);

    search_row->append(*m_replace_toggle);
    search_row->append(*m_query_entry);
    search_row->append(*m_case_btn);
    search_row->append(*m_word_btn);
    search_row->append(*m_regex_btn);
    search_row->append(*m_count_lbl);

    // ── Replace row (revealed) ────────────────────────────────────────────────
    m_replace_revealer.set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);
    m_replace_revealer.set_transition_duration(100);
    m_replace_revealer.set_reveal_child(false);

    auto* rep_row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    rep_row->add_css_class("sd-entry-row");

    // Spacer matching the collapse button width
    auto* spacer = Gtk::make_managed<Gtk::Label>("");
    spacer->set_size_request(28, -1);

    m_replace_entry = Gtk::make_managed<Gtk::Entry>();
    m_replace_entry->set_placeholder_text("Replace  ($1 $2 … for backreferences)");
    m_replace_entry->set_hexpand(true);
    auto rkc = Gtk::EventControllerKey::create();
    rkc->signal_key_pressed().connect(
        [this](guint kv, guint, Gdk::ModifierType) -> bool {
            if (kv == GDK_KEY_Return || kv == GDK_KEY_KP_Enter) {
                do_replace_all(); return true;
            }
            return false;
        }, false);
    m_replace_entry->add_controller(rkc);

    m_replace_all_btn = Gtk::make_managed<Gtk::Button>("Replace All");
    m_replace_all_btn->add_css_class("sd-replace-btn");
    m_replace_all_btn->signal_clicked().connect([this]{ do_replace_all(); });

    rep_row->append(*spacer);
    rep_row->append(*m_replace_entry);
    rep_row->append(*m_replace_all_btn);
    m_replace_revealer.set_child(*rep_row);

    input_area->append(*search_row);
    input_area->append(m_replace_revealer);
    m_root.append(*input_area);
}

// ─────────────────────────────────────────────────────────────────────────────
// build_scope_bar  — pill buttons for scope selection
// ─────────────────────────────────────────────────────────────────────────────

void SearchDialog::build_scope_bar() {
    // Single "Scope ▾" MenuButton that opens a popover with two sections:
    // Sections (which parts of the project) and Fields (which data fields).

    auto* bar = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    bar->add_css_class("sd-scope-bar");

    m_scope_btn = Gtk::make_managed<Gtk::MenuButton>();
    m_scope_btn->add_css_class("sd-scope-menu-btn");
    m_scope_btn->set_direction(Gtk::ArrowType::NONE); // suppress built-in arrow

    auto* btn_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 4);
    m_scope_lbl = Gtk::make_managed<Gtk::Label>("Scope");
    m_scope_lbl->add_css_class("sd-scope-lbl");
    auto* arrow = Gtk::make_managed<Gtk::Label>("▾");
    arrow->add_css_class("section-arrow");
    arrow->set_margin_start(2);
    btn_box->append(*m_scope_lbl);
    btn_box->append(*arrow);
    m_scope_btn->set_child(*btn_box);

    // ── Build the popover ─────────────────────────────────────────────────────
    auto* pop_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 0);
    pop_box->add_css_class("sd-scope-pop");

    // Helper: make a checkbox row
    auto make_ck = [](const char* label, bool active = true) {
        auto* ck = Gtk::make_managed<Gtk::CheckButton>(label);
        ck->set_active(active);
        ck->add_css_class("sd-scope-ck");
        return ck;
    };

    // ── Left column: Sections ─────────────────────────────────────────────────
    auto* sec_col = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    sec_col->add_css_class("sd-scope-col");

    auto* sec_hdr = Gtk::make_managed<Gtk::Label>("Sections");
    sec_hdr->add_css_class("sd-scope-col-hdr");
    sec_hdr->set_halign(Gtk::Align::START);

    m_ck_all_sec = make_ck("All sections");
    auto* sec_sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
    sec_sep->set_margin_top(2);
    sec_sep->set_margin_bottom(2);

    m_ck_ms  = make_ck("Manuscript");
    m_ck_ch  = make_ck("Characters");
    m_ck_pl  = make_ck("Places");
    m_ck_ref = make_ck("References");
    m_ck_tpl = make_ck("Templates", false);

    sec_col->append(*sec_hdr);
    sec_col->append(*m_ck_all_sec);
    sec_col->append(*sec_sep);
    sec_col->append(*m_ck_ms);
    sec_col->append(*m_ck_ch);
    sec_col->append(*m_ck_pl);
    sec_col->append(*m_ck_ref);
    sec_col->append(*m_ck_tpl);

    // ── Divider ───────────────────────────────────────────────────────────────
    auto* col_sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::VERTICAL);
    col_sep->set_margin_start(8);
    col_sep->set_margin_end(8);

    // ── Right column: Fields ─────────────────────────────────────────────────
    auto* fld_col = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 2);
    fld_col->add_css_class("sd-scope-col");

    auto* fld_hdr = Gtk::make_managed<Gtk::Label>("Fields");
    fld_hdr->add_css_class("sd-scope-col-hdr");
    fld_hdr->set_halign(Gtk::Align::START);

    m_ck_all_fld = make_ck("All fields");
    auto* fld_sep = Gtk::make_managed<Gtk::Separator>(Gtk::Orientation::HORIZONTAL);
    fld_sep->set_margin_top(2);
    fld_sep->set_margin_bottom(2);

    m_ck_title = make_ck("Title");
    m_ck_body  = make_ck("Body");
    m_ck_syn   = make_ck("Synopsis");
    m_ck_notes = make_ck("Notes");
    m_ck_desc  = make_ck("Description");

    fld_col->append(*fld_hdr);
    fld_col->append(*m_ck_all_fld);
    fld_col->append(*fld_sep);
    fld_col->append(*m_ck_title);
    fld_col->append(*m_ck_body);
    fld_col->append(*m_ck_syn);
    fld_col->append(*m_ck_notes);
    fld_col->append(*m_ck_desc);

    pop_box->append(*sec_col);
    pop_box->append(*col_sep);
    pop_box->append(*fld_col);

    m_scope_popover = Gtk::make_managed<Gtk::Popover>();
    m_scope_popover->set_child(*pop_box);
    m_scope_popover->set_has_arrow(false);
    m_scope_popover->add_css_class("sd-scope-popover");
    m_scope_btn->set_popover(*m_scope_popover);

    // ── Wire signals — all checkboxes update label + re-search ────────────────

    // "All sections" master
    m_ck_all_sec->signal_toggled().connect([this]{
        bool on = m_ck_all_sec->get_active();
        for (auto* b : {m_ck_ms, m_ck_ch, m_ck_pl, m_ck_ref, m_ck_tpl})
            b->set_active(on);
        update_scope_label();
        schedule_search();
    });

    // Individual section checkboxes sync master
    auto sync_sec_master = [this]{
        bool all = m_ck_ms->get_active() && m_ck_ch->get_active() &&
                   m_ck_pl->get_active() && m_ck_ref->get_active() &&
                   m_ck_tpl->get_active();
        if (m_ck_all_sec->get_active() != all) {
            auto blk = m_ck_all_sec->signal_toggled().connect([]{});
            m_ck_all_sec->set_active(all);
            blk.disconnect();
        }
        update_scope_label();
        schedule_search();
    };
    for (auto* b : {m_ck_ms, m_ck_ch, m_ck_pl, m_ck_ref, m_ck_tpl})
        b->signal_toggled().connect(sync_sec_master);

    // "All fields" master
    m_ck_all_fld->signal_toggled().connect([this]{
        bool on = m_ck_all_fld->get_active();
        for (auto* b : {m_ck_title, m_ck_body, m_ck_syn, m_ck_notes, m_ck_desc})
            b->set_active(on);
        update_scope_label();
        schedule_search();
    });

    // Individual field checkboxes sync master
    auto sync_fld_master = [this]{
        bool all = m_ck_title->get_active() && m_ck_body->get_active() &&
                   m_ck_syn->get_active()   && m_ck_notes->get_active() &&
                   m_ck_desc->get_active();
        if (m_ck_all_fld->get_active() != all) {
            auto blk = m_ck_all_fld->signal_toggled().connect([]{});
            m_ck_all_fld->set_active(all);
            blk.disconnect();
        }
        update_scope_label();
        schedule_search();
    };
    for (auto* b : {m_ck_title, m_ck_body, m_ck_syn, m_ck_notes, m_ck_desc})
        b->signal_toggled().connect(sync_fld_master);

    bar->append(*m_scope_btn);
    m_root.append(*bar);

    update_scope_label();
}

// ─────────────────────────────────────────────────────────────────────────────
// update_scope_label — compact summary on the Scope button
// ─────────────────────────────────────────────────────────────────────────────

void SearchDialog::update_scope_label() {
    if (!m_scope_lbl) return;

    // Sections summary
    std::string sec_str;
    bool all_sec = m_ck_all_sec && m_ck_all_sec->get_active();
    if (all_sec) {
        sec_str = "All sections";
    } else {
        std::vector<const char*> names;
        if (m_ck_ms  && m_ck_ms->get_active())  names.push_back("MS");
        if (m_ck_ch  && m_ck_ch->get_active())  names.push_back("CH");
        if (m_ck_pl  && m_ck_pl->get_active())  names.push_back("PL");
        if (m_ck_ref && m_ck_ref->get_active()) names.push_back("REF");
        if (m_ck_tpl && m_ck_tpl->get_active()) names.push_back("TPL");
        if (names.empty()) sec_str = "No sections";
        else for (size_t i = 0; i < names.size(); ++i) {
            if (i) sec_str += "+";
            sec_str += names[i];
        }
    }

    // Fields summary
    std::string fld_str;
    bool all_fld = m_ck_all_fld && m_ck_all_fld->get_active();
    if (all_fld) {
        fld_str = "all fields";
    } else {
        std::vector<const char*> names;
        if (m_ck_title && m_ck_title->get_active()) names.push_back("title");
        if (m_ck_body  && m_ck_body->get_active())  names.push_back("body");
        if (m_ck_syn   && m_ck_syn->get_active())   names.push_back("synopsis");
        if (m_ck_notes && m_ck_notes->get_active()) names.push_back("notes");
        if (m_ck_desc  && m_ck_desc->get_active())  names.push_back("desc");
        if (names.empty()) fld_str = "no fields";
        else for (size_t i = 0; i < names.size(); ++i) {
            if (i) fld_str += "+";
            fld_str += names[i];
        }
    }

    m_scope_lbl->set_text(sec_str + "  ·  " + fld_str);
}


void SearchDialog::build_results_area() {
    m_results_scroll.set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    m_results_scroll.set_vexpand(true);
    m_results_scroll.set_hexpand(true);

    m_results_box.add_css_class("sd-results-box");
    m_results_box.set_vexpand(true);

    // Keep empty label permanently in the box — show/hide rather than
    // add/remove to avoid managed-widget lifetime issues.
    m_empty_lbl = Gtk::make_managed<Gtk::Label>("Enter a search term above.");
    m_empty_lbl->add_css_class("sd-empty");
    m_empty_lbl->set_halign(Gtk::Align::CENTER);
    m_empty_lbl->set_valign(Gtk::Align::CENTER);
    m_empty_lbl->set_justify(Gtk::Justification::CENTER);
    m_results_box.append(*m_empty_lbl);

    m_results_scroll.set_child(m_results_box);
    m_root.append(m_results_scroll);
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers
// ─────────────────────────────────────────────────────────────────────────────

Gtk::ToggleButton* SearchDialog::make_field_toggle(const char* label, const char* tip) {
    auto* btn = Gtk::make_managed<Gtk::ToggleButton>(label);
    btn->add_css_class("fmt-btn");
    btn->set_tooltip_text(tip);
    btn->signal_toggled().connect([this]{ schedule_search(); });
    return btn;
}

// ─────────────────────────────────────────────────────────────────────────────
// current_opts
// ─────────────────────────────────────────────────────────────────────────────

SearchOptions SearchDialog::current_opts() const {
    SearchOptions o;
    o.case_sensitive    = m_case_btn  && m_case_btn->get_active();
    o.whole_word        = m_word_btn  && m_word_btn->get_active();
    o.use_regex         = m_regex_btn && m_regex_btn->get_active();
    o.search_title      = m_ck_title  && m_ck_title->get_active();
    o.search_body       = m_ck_body   && m_ck_body->get_active();
    o.search_synopsis   = m_ck_syn    && m_ck_syn->get_active();
    o.search_notes      = m_ck_notes  && m_ck_notes->get_active();
    o.search_description= m_ck_desc   && m_ck_desc->get_active();
    o.sec_manuscript    = m_ck_ms     && m_ck_ms->get_active();
    o.sec_characters    = m_ck_ch     && m_ck_ch->get_active();
    o.sec_places        = m_ck_pl     && m_ck_pl->get_active();
    o.sec_references    = m_ck_ref    && m_ck_ref->get_active();
    o.sec_templates     = m_ck_tpl    && m_ck_tpl->get_active();
    return o;
}

// ─────────────────────────────────────────────────────────────────────────────
// schedule_search / run_search
// ─────────────────────────────────────────────────────────────────────────────

void SearchDialog::schedule_search() {
    m_debounce_conn.disconnect();
    m_debounce_conn = Glib::signal_timeout().connect(
        [this]() -> bool { run_search(); return false; }, 250);
}

void SearchDialog::run_search() {
    m_debounce_conn.disconnect();
    if (!m_query_entry) return;

    std::string query = std::string(m_query_entry->get_text());

    if (m_regex_btn && m_regex_btn->get_active() && !query.empty()) {
        try {
            SearchEngine::build_regex(query, current_opts());
            m_query_entry->remove_css_class("find-error");
        } catch (...) {
            m_query_entry->add_css_class("find-error");
            m_results.clear();
            m_total_matches = 0;
            populate_results();
            return;
        }
    } else {
        m_query_entry->remove_css_class("find-error");
    }

    if (query.empty()) {
        m_results.clear();
        m_total_matches = 0;
        if (m_count_lbl) m_count_lbl->set_text("");
        if (m_empty_lbl) m_empty_lbl->set_text("Enter a search term above.");
        populate_results();
        return;
    }

    m_results = SearchEngine::search(m_model, query, current_opts());
    m_total_matches = 0;
    for (auto& r : m_results) {
        if (r.match_in_title) ++m_total_matches;
        m_total_matches += (int)r.body_matches.size();
    }

    populate_results();

    if (m_count_lbl) {
        if (m_results.empty())
            m_count_lbl->set_text("No results");
        else
            m_count_lbl->set_text(
                std::to_string(m_total_matches) + " in " +
                std::to_string((int)m_results.size()) + " file" +
                (m_results.size() == 1 ? "" : "s"));
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// populate_results
// ─────────────────────────────────────────────────────────────────────────────

void SearchDialog::populate_results() {
    // Remove all children except m_empty_lbl
    auto* child = m_results_box.get_first_child();
    while (child) {
        auto* next = child->get_next_sibling();
        if (child != m_empty_lbl) m_results_box.remove(*child);
        child = next;
    }

    if (m_results.empty()) {
        std::string msg = m_query_entry && !m_query_entry->get_text().empty()
            ? "No results." : "Enter a search term above.";
        m_empty_lbl->set_text(msg);
        m_empty_lbl->set_visible(true);
        return;
    }

    m_empty_lbl->set_visible(false);
    for (int i = 0; i < (int)m_results.size(); ++i)
        m_results_box.append(*make_result_group(m_results[i], i));
}

// ─────────────────────────────────────────────────────────────────────────────
// make_result_group  — one collapsible node group
// ─────────────────────────────────────────────────────────────────────────────

Gtk::Widget* SearchDialog::make_result_group(const SearchResult& r, int idx) {
    auto* group = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);

    // ── Group header ──────────────────────────────────────────────────────────
    auto* hdr = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
    hdr->add_css_class("sd-group-header");

    // Collapse toggle
    auto* collapse = Gtk::make_managed<Gtk::ToggleButton>();
    collapse->set_icon_name("pan-down-symbolic");
    collapse->set_active(true);
    collapse->add_css_class("flat");
    collapse->add_css_class("fmt-btn");

    // Section icon
    const char* sec_icon =
        r.section == Section::Manuscript ? "document-edit-symbolic"  :
        r.section == Section::Characters ? "system-users-symbolic"   :
        r.section == Section::Places     ? "map-symbolic"            :
        r.section == Section::References ? "bookmark-symbolic"       :
                                           "document-symbolic";
    auto* icon = Gtk::make_managed<Gtk::Image>();
    icon->set_from_icon_name(sec_icon);
    icon->set_pixel_size(13);

    // Title
    std::string display = r.title.empty() ? "(Untitled)" : r.title;
    if (r.match_in_title) display = "★ " + display;
    auto* title_lbl = Gtk::make_managed<Gtk::Label>(display);
    title_lbl->add_css_class("sd-group-title");
    title_lbl->set_halign(Gtk::Align::START);
    title_lbl->set_hexpand(true);
    title_lbl->set_ellipsize(Pango::EllipsizeMode::END);

    // Match count badge
    int mc = (int)r.body_matches.size() + (r.match_in_title ? 1 : 0);
    auto* cnt = Gtk::make_managed<Gtk::Label>(std::to_string(mc));
    cnt->add_css_class("sd-group-count");

    hdr->append(*collapse);
    hdr->append(*icon);
    hdr->append(*title_lbl);
    hdr->append(*cnt);
    group->append(*hdr);

    // ── Match rows (in a revealer) ────────────────────────────────────────────
    auto* rev = Gtk::make_managed<Gtk::Revealer>();
    rev->set_transition_type(Gtk::RevealerTransitionType::SLIDE_DOWN);
    rev->set_transition_duration(80);
    rev->set_reveal_child(true);

    auto* matches_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);

    for (int mi = 0; mi < (int)r.body_matches.size(); ++mi) {
        const auto& bm = r.body_matches[mi];

        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        row->add_css_class("sd-match-row");

        // Line:col + field badge
        std::string loc_str = std::to_string(bm.line) + ":" + std::to_string(bm.col);
        if (!bm.field.empty() && bm.field != "Body")
            loc_str = bm.field + "  " + loc_str;
        auto* loc_lbl = Gtk::make_managed<Gtk::Label>(loc_str);
        loc_lbl->add_css_class("sd-match-loc");
        loc_lbl->set_xalign(1.0f);
        loc_lbl->set_width_chars(18);

        // Context with bold match — use Pango markup
        auto esc = [](const std::string& s) {
            std::string out;
            for (unsigned char c : s) {
                if      (c == '&') out += "&amp;";
                else if (c == '<') out += "&lt;";
                else if (c == '>') out += "&gt;";
                else               out += static_cast<char>(c);
            }
            return out;
        };
        std::string markup = esc(bm.context_before) +
                             "<b>" + esc(bm.context_match) + "</b>" +
                             esc(bm.context_after);

        auto* ctx_lbl = Gtk::make_managed<Gtk::Label>();
        ctx_lbl->set_markup(markup);
        ctx_lbl->set_halign(Gtk::Align::START);
        ctx_lbl->set_ellipsize(Pango::EllipsizeMode::END);
        ctx_lbl->set_xalign(0.0f);
        ctx_lbl->set_hexpand(true);

        // Make whole row clickable
        auto gc2 = Gtk::GestureClick::create();
        gc2->signal_pressed().connect([this, idx](int, double, double){
            if (idx < (int)m_results.size() && m_on_open)
                m_on_open(m_results[idx].section, m_results[idx].path);
        });
        row->add_controller(gc2);

        row->append(*loc_lbl);
        row->append(*ctx_lbl);
        matches_box->append(*row);
    }

    rev->set_child(*matches_box);
    group->append(*rev);

    // Wire collapse button
    collapse->signal_toggled().connect([rev, collapse]{
        bool open = collapse->get_active();
        rev->set_reveal_child(open);
        collapse->set_icon_name(open ? "pan-down-symbolic" : "pan-end-symbolic");
    });

    // Click on header also opens node
    auto gc = Gtk::GestureClick::create();
    gc->signal_pressed().connect([this, idx](int, double, double){
        if (idx < (int)m_results.size() && m_on_open)
            m_on_open(m_results[idx].section, m_results[idx].path);
    });
    title_lbl->add_controller(gc);

    return group;
}

// ─────────────────────────────────────────────────────────────────────────────
// do_replace_all
// ─────────────────────────────────────────────────────────────────────────────

void SearchDialog::do_replace_all() {
    if (!m_query_entry || !m_replace_entry) return;
    std::string query   = std::string(m_query_entry->get_text());
    std::string rep_str = std::string(m_replace_entry->get_text());
    if (query.empty() || m_results.empty()) return;

    auto opts  = current_opts();
    int  total = 0;

    for (auto& r : m_results) {
        if (r.body_matches.empty()) continue;
        BinderNode* node = m_model.node_at(r.section, r.path);
        if (!node) continue;

        auto result = SearchEngine::replace_html(node->content, query, rep_str, opts);
        if (!result.error.empty() || result.replacements == 0) continue;

        node->content          = result.new_html;
        node->content_modified = true;
        total += result.replacements;

        if (m_on_changed) m_on_changed(r.section, r.path, result.new_html);
    }

    m_model.mark_modified();

    if (m_count_lbl)
        m_count_lbl->set_text("Replaced " + std::to_string(total) +
                              " occurrence" + (total == 1 ? "" : "s"));
    run_search();
}

// ─────────────────────────────────────────────────────────────────────────────
// set_query / refresh
// ─────────────────────────────────────────────────────────────────────────────

void SearchDialog::set_query(const std::string& q) {
    if (m_query_entry) { m_query_entry->set_text(q); m_query_entry->grab_focus(); }
    run_search();
}

void SearchDialog::refresh() { run_search(); }

} // namespace Folio
