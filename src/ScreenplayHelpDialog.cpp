// ─────────────────────────────────────────────────────────────────────────────
// Folio — ScreenplayHelpDialog.cpp
// Floating reference panel: screenplay format rules + example page
// ─────────────────────────────────────────────────────────────────────────────
#include "ScreenplayHelpDialog.hpp"
#include <sstream>
#include <string>
#include <vector>

namespace Folio {

// ─────────────────────────────────────────────────────────────────────────────
// Constructor
// ─────────────────────────────────────────────────────────────────────────────
ScreenplayHelpDialog::ScreenplayHelpDialog(Gtk::Window& parent)
    : Gtk::Window()
{
    set_transient_for(parent);
    set_modal(false);         // floating — stays open while writing
    set_title("Screenplay Format Reference");
    set_default_size(400, 680);
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
        .sp-help-header {
            font-size: 13px; font-weight: 700;
            padding: 14px 16px 6px 16px;
            color: alpha(currentColor, 0.9);
        }
        .sp-help-subheader {
            font-size: 10px; font-weight: 700; letter-spacing: 0.07em;
            text-transform: uppercase;
            color: alpha(currentColor, 0.45);
            padding: 10px 16px 3px 16px;
        }
        .sp-element-name {
            font-size: 12px; font-weight: 700; font-family: "Courier New", monospace;
            min-width: 110px;
        }
        .sp-element-key {
            font-size: 11px; font-family: monospace;
            background: alpha(currentColor, 0.08);
            border-radius: 4px; padding: 1px 6px;
            color: alpha(currentColor, 0.75);
            min-width: 80px;
        }
        .sp-element-desc {
            font-size: 12px;
            color: alpha(currentColor, 0.70);
        }
        .sp-element-row {
            padding: 5px 16px;
        }
        .sp-element-row:hover {
            background: alpha(currentColor, 0.04);
        }
        .sp-rule-text {
            font-size: 12px;
            color: alpha(currentColor, 0.72);
            padding: 2px 16px 2px 16px;
        }
        .sp-tip-box {
            margin: 8px 12px 4px 12px;
            padding: 8px 12px;
            border-radius: 8px;
            background: alpha(currentColor, 0.05);
            border: 1px solid alpha(currentColor, 0.10);
        }
        .sp-tip-text {
            font-size: 11px;
            color: alpha(currentColor, 0.65);
        }
        .sp-example-label {
            font-size: 10px; font-weight: 700; letter-spacing: 0.07em;
            text-transform: uppercase;
            color: alpha(currentColor, 0.45);
            padding: 10px 12px 6px 12px;
        }
    )");
    get_style_context()->add_provider_for_display(
        get_display(), css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);

    // Single scrolled column: rules on top, collapsible example below
    auto* scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroll->set_vexpand(true);

    auto* col = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);
    build_rules_panel(*col);
    build_example_panel(*col);
    scroll->set_child(*col);

    m_root.append(*scroll);
    set_child(m_root);
}

// ─────────────────────────────────────────────────────────────────────────────
// Rules panel (left)
// ─────────────────────────────────────────────────────────────────────────────
void ScreenplayHelpDialog::build_rules_panel(Gtk::Box& container) {

    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_policy(Gtk::PolicyType::NEVER, Gtk::PolicyType::AUTOMATIC);
    scroll->set_vexpand(true);

    auto box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);

    // ── Title ──────────────────────────────────────────────────────────────
    auto* title = Gtk::make_managed<Gtk::Label>("Screenplay Formatting");
    title->add_css_class("sp-help-header");
    title->set_halign(Gtk::Align::START);
    box->append(*title);

    auto* intro = Gtk::make_managed<Gtk::Label>(
        "Industry-standard format uses Courier 12pt, specific margins\n"
        "and indentation for each element. Folio handles this automatically.");
    intro->add_css_class("sp-rule-text");
    intro->set_halign(Gtk::Align::START);
    intro->set_wrap(true);
    intro->set_xalign(0.0f);
    box->append(*intro);

    // ── Elements section ──────────────────────────────────────────────────
    auto* el_hdr = Gtk::make_managed<Gtk::Label>("Elements & Hotkeys");
    el_hdr->add_css_class("sp-help-subheader");
    el_hdr->set_halign(Gtk::Align::START);
    box->append(*el_hdr);

    struct ElementInfo {
        const char* name;
        const char* key;
        const char* indent;
        const char* desc;
    };

    static const ElementInfo elements[] = {
        { "SCENE HEADING",   "Tab → scene",     "Full width, left margin",
          "INT. or EXT. — LOCATION — DAY/NIGHT. Always ALL CAPS. "
          "Establishes where and when the scene takes place." },
        { "ACTION",          "Tab → action",    "Full width",
          "Description of what is seen and heard. Written in present tense. "
          "Do not direct the camera. Keep it brief and visual." },
        { "CHARACTER",       "Tab → character", "Centred (~3.5\" from left)",
          "Name of the character about to speak. ALL CAPS. "
          "Appears directly above their dialogue." },
        { "PARENTHETICAL",   "Tab from character", "Indented (~3\" from left)",
          "Brief direction in (parentheses) below the character name. "
          "Use sparingly — only when tone is not clear from context." },
        { "DIALOGUE",        "Enter from character", "Indented (~2.5\" from left)",
          "The spoken words. Flows directly from CHARACTER or PARENTHETICAL. "
          "Each speech block is max ~3\" wide." },
        { "TRANSITION",      "Tab → transition", "Right-justified",
          "CUT TO:, DISSOLVE TO:, SMASH CUT TO: etc. Always ALL CAPS, "
          "ends with a colon. Use sparingly — CUT TO: is implied." },
    };

    for (const auto& el : elements) {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 3);
        row->add_css_class("sp-element-row");

        // Top line: name + key badge
        auto* top = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 8);
        top->set_valign(Gtk::Align::CENTER);

        auto* name_lbl = Gtk::make_managed<Gtk::Label>(el.name);
        name_lbl->add_css_class("sp-element-name");
        name_lbl->set_halign(Gtk::Align::START);
        name_lbl->set_xalign(0.0f);

        auto* key_lbl = Gtk::make_managed<Gtk::Label>(el.key);
        key_lbl->add_css_class("sp-element-key");
        key_lbl->set_halign(Gtk::Align::START);

        auto* indent_lbl = Gtk::make_managed<Gtk::Label>(el.indent);
        indent_lbl->add_css_class("sp-element-desc");
        indent_lbl->set_halign(Gtk::Align::START);
        indent_lbl->set_hexpand(true);

        top->append(*name_lbl);
        top->append(*key_lbl);
        top->append(*indent_lbl);
        row->append(*top);

        // Description
        auto* desc_lbl = Gtk::make_managed<Gtk::Label>(el.desc);
        desc_lbl->add_css_class("sp-rule-text");
        desc_lbl->set_halign(Gtk::Align::START);
        desc_lbl->set_xalign(0.0f);
        desc_lbl->set_wrap(true);
        desc_lbl->set_max_width_chars(52);
        row->append(*desc_lbl);

        box->append(*row);
    }

    // ── Tab cycling ───────────────────────────────────────────────────────
    auto* flow_hdr = Gtk::make_managed<Gtk::Label>("Tab Flow");
    flow_hdr->add_css_class("sp-help-subheader");
    flow_hdr->set_halign(Gtk::Align::START);
    box->append(*flow_hdr);

    struct FlowRow { const char* from; const char* arrow; const char* to; };
    static const FlowRow flows[] = {
        { "Enter after SCENE",        "→", "ACTION" },
        { "Enter after ACTION",       "→", "ACTION  (Tab to start dialogue)" },
        { "Enter after CHARACTER",    "→", "DIALOGUE" },
        { "Enter after PARENTHETICAL","→", "DIALOGUE" },
        { "Enter after DIALOGUE",     "→", "CHARACTER  (next speaker)" },
        { "Enter after TRANSITION",   "→", "SCENE HEADING" },
        { "Tab from CHARACTER",       "→", "PARENTHETICAL" },
        { "Shift+Tab",                "→", "Previous element in cycle" },
    };

    for (const auto& f : flows) {
        auto* row = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::HORIZONTAL, 6);
        row->set_margin_start(16);
        row->set_margin_top(2);
        row->set_margin_bottom(2);

        auto* from = Gtk::make_managed<Gtk::Label>(f.from);
        from->add_css_class("sp-element-desc");
        from->set_xalign(0.0f);
        from->set_width_chars(30);

        auto* arrow = Gtk::make_managed<Gtk::Label>(f.arrow);
        arrow->add_css_class("sp-element-desc");

        auto* to = Gtk::make_managed<Gtk::Label>(f.to);
        to->add_css_class("sp-element-desc");
        to->set_xalign(0.0f);

        row->append(*from);
        row->append(*arrow);
        row->append(*to);
        box->append(*row);
    }

    // ── Tips ──────────────────────────────────────────────────────────────
    auto* tips_hdr = Gtk::make_managed<Gtk::Label>("Style Rules");
    tips_hdr->add_css_class("sp-help-subheader");
    tips_hdr->set_halign(Gtk::Align::START);
    box->append(*tips_hdr);

    static const char* tips[] = {
        "• One page ≈ one minute of screen time. Target 90–120 pages.",
        "• Scene headings always start with INT. or EXT.",
        "• Action lines: present tense, active voice. \"He runs.\" not \"He ran.\"",
        "• Never write camera directions (CLOSE ON, PAN TO) in spec scripts.",
        "• Character names in action are ALL CAPS on first introduction only.",
        "• Parentheticals should be used only when essential — trust your dialogue.",
        "• A speech block should rarely exceed 3–4 lines.",
        "• Avoid widows: a single dialogue line alone on a page.",
        ("• (CONT'D) after a character name when the same character continues\n"
          "  speaking after an action interruption."),
        "• Use CONTINUOUS instead of DAY/NIGHT when action flows between locations.",
    };

    auto* tip_box = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 4);
    tip_box->add_css_class("sp-tip-box");
    for (const char* tip : tips) {
        auto* lbl = Gtk::make_managed<Gtk::Label>(tip);
        lbl->add_css_class("sp-tip-text");
        lbl->set_halign(Gtk::Align::START);
        lbl->set_xalign(0.0f);
        lbl->set_wrap(true);
        lbl->set_max_width_chars(58);
        tip_box->append(*lbl);
    }
    box->append(*tip_box);

    // Bottom padding
    auto* pad = Gtk::make_managed<Gtk::Label>("");
    pad->set_margin_bottom(16);
    box->append(*pad);

    scroll->set_child(*box);
    container.append(*scroll);
}

// ─────────────────────────────────────────────────────────────────────────────
// Example panel (right)
// ─────────────────────────────────────────────────────────────────────────────

void ScreenplayHelpDialog::build_example_panel(Gtk::Box& container) {
    // Collapsible expander — example page is supplementary, not primary
    auto* expander = Gtk::make_managed<Gtk::Expander>("Example Page");
    expander->set_expanded(false); // collapsed by default
    expander->set_margin_start(4);
    expander->set_margin_end(4);
    expander->set_margin_top(4);
    expander->set_margin_bottom(4);

    auto* inner = Gtk::make_managed<Gtk::Box>(Gtk::Orientation::VERTICAL, 0);

    auto scroll = Gtk::make_managed<Gtk::ScrolledWindow>();
    scroll->set_policy(Gtk::PolicyType::AUTOMATIC, Gtk::PolicyType::AUTOMATIC);
    scroll->set_vexpand(true);
    scroll->set_hexpand(true);

    // DrawingArea that renders the SVG via librsvg
    auto* da = Gtk::make_managed<Gtk::DrawingArea>();
    da->set_size_request(340, 440);
    da->set_margin_top(8);
    da->set_margin_bottom(16);
    da->set_margin_start(8);
    da->set_margin_end(8);

    da->set_draw_func([da](const Cairo::RefPtr<Cairo::Context>& cr,
                                  int /*w*/, int /*h*/) {
        // Detect dark mode from fg luminance
        auto style = da->get_style_context();
        Gdk::RGBA fg = style->get_color();
        double lum = 0.2126 * fg.get_red() + 0.7152 * fg.get_green()
                   + 0.0722 * fg.get_blue();
        bool dark = lum > 0.5;

        // Colours
        struct C { double r,g,b; };
        C page_bg  = dark ? C{0.165,0.165,0.243} : C{1.0,1.0,1.0};
        C page_fg  = dark ? C{0.816,0.831,0.941} : C{0.07,0.07,0.07};
        C margin_c = dark ? C{0.227,0.227,0.329} : C{0.91,0.91,0.91};
        C accent   = dark ? C{0.357,0.784,0.686} : C{0.055,0.388,0.408};
        C label_c  = dark ? C{0.357,0.376,0.502} : C{0.667,0.667,0.667};

        auto set = [&](C c, double a=1.0){ cr->set_source_rgba(c.r,c.g,c.b,a); };

        // Page shadow + background
        set({0,0,0}, 0.18);
        cr->rectangle(10, 10, 320, 420);
        cr->fill();
        set(page_bg);
        cr->rectangle(8, 8, 320, 420);
        cr->fill();
        set(margin_c);
        cr->set_line_width(0.8);
        cr->rectangle(8, 8, 320, 420);
        cr->stroke();

        // Margin guide lines
        std::vector<double> dash = {3.0, 3.0};
        cr->set_dash(dash, 0);
        cr->set_line_width(0.5);
        set(margin_c);
        cr->move_to(55, 28); cr->line_to(55, 410); cr->stroke();
        cr->move_to(285, 28); cr->line_to(285, 410); cr->stroke();
        cr->unset_dash();

        // Page number
        auto pg_layout = da->create_pango_layout("1.");
        Pango::FontDescription pfd("Courier New 7");
        pg_layout->set_font_description(pfd);
        set(label_c);
        cr->move_to(271, 14);
        pg_layout->show_in_cairo_context(cr);

        // Helper: draw a line of text
        auto draw_text = [&](double x, double y, const std::string& text,
                              bool bold, bool centre, double max_w,
                              C colour, double font_sz = 8.0) {
            auto layout = da->create_pango_layout(text);
            std::string fdesc = std::string("Courier New ") + std::to_string((int)font_sz);
            if (bold) fdesc += " Bold";
            layout->set_font_description(Pango::FontDescription(fdesc));
            if (max_w > 0) layout->set_width((int)(max_w * Pango::SCALE));
            set(colour);
            int tw = 0, th = 0;
            layout->get_pixel_size(tw, th);
            double draw_x = centre ? (x - tw / 2.0) : x;
            cr->move_to(draw_x, y);
            layout->show_in_cairo_context(cr);
        };

        // Element label helper (right-aligned, small sans)
        auto draw_label = [&](double y, const std::string& text) {
            auto layout = da->create_pango_layout(text);
            layout->set_font_description(Pango::FontDescription("sans 6"));
            int tw = 0, th = 0;
            layout->get_pixel_size(tw, th);
            set(accent);
            cr->move_to(286 - tw, y);
            layout->show_in_cairo_context(cr);
        };

        const double LH = 11.0; // line height
        const double LEFT = 55, CHAR_X = 170, PAR_X = 125, DIA_X = 100;
        double y = 36;

        // SCENE HEADING
        draw_text(LEFT, y, "INT. COFFEE SHOP - DAY", true, false, 230, page_fg);
        draw_label(y, "SCENE HEADING");
        y += LH * 1.6;

        // ACTION
        draw_text(LEFT, y, "The shop is nearly empty.", false, false, 230, page_fg);
        y += LH;
        draw_text(LEFT, y, "SARAH (30s) stares at her phone.", false, false, 230, page_fg);
        draw_label(y - LH/2, "ACTION");
        y += LH * 1.6;

        // CHARACTER
        draw_text(CHAR_X, y, "SARAH", true, true, 0, page_fg);
        draw_label(y, "CHARACTER");
        y += LH;

        // PARENTHETICAL
        draw_text(PAR_X, y, "(to herself)", false, false, 0, page_fg);
        draw_label(y, "PARENTHETICAL");
        y += LH;

        // DIALOGUE
        draw_text(DIA_X, y, "Why did I say that?", false, false, 170, page_fg);
        draw_label(y, "DIALOGUE");
        y += LH * 1.6;

        // CHARACTER 2
        draw_text(CHAR_X, y, "BARISTA", true, true, 0, page_fg);
        y += LH;

        // DIALOGUE 2
        draw_text(DIA_X, y, "Another one?", false, false, 170, page_fg);
        y += LH * 1.6;

        // ACTION
        draw_text(LEFT, y, "Sarah looks up. A beat.", false, false, 230, page_fg);
        y += LH * 1.6;

        // CHARACTER
        draw_text(CHAR_X, y, "SARAH", true, true, 0, page_fg);
        y += LH;

        // DIALOGUE
        draw_text(DIA_X, y, "Make it a double.", false, false, 170, page_fg);
        y += LH * 2.0;

        // TRANSITION — right-justified
        {
            auto layout = da->create_pango_layout("CUT TO:");
            layout->set_font_description(Pango::FontDescription("Courier New Bold 8"));
            int tw = 0, th = 0;
            layout->get_pixel_size(tw, th);
            set(page_fg);
            cr->move_to(285 - tw, y);
            layout->show_in_cairo_context(cr);
        }
        draw_label(y, "TRANSITION");
        y += LH * 1.6;

        // NEXT SCENE
        draw_text(LEFT, y, "EXT. STREET - CONTINUOUS", true, false, 230, page_fg);
        y += LH;
        draw_text(LEFT, y, "Sarah steps outside, phone in hand.", false, false, 230, page_fg);

        // Left margin annotation
        set(label_c);
        cr->set_line_width(0.7);
        cr->move_to(8, 220); cr->line_to(55, 220); cr->stroke();
        auto ml = da->create_pango_layout("1.5in");
        ml->set_font_description(Pango::FontDescription("sans 6"));
        set(label_c);
        cr->move_to(20, 212);
        ml->show_in_cairo_context(cr);
    });

    scroll->set_child(*da);
    inner->append(*scroll);

    // Caption
    auto* caption = Gtk::make_managed<Gtk::Label>(
        "Coloured labels show element names — they don't appear in the actual script.");
    caption->add_css_class("sp-tip-text");
    caption->set_halign(Gtk::Align::CENTER);
    caption->set_wrap(true);
    caption->set_margin_start(8);
    caption->set_margin_end(8);
    caption->set_margin_bottom(10);
    inner->append(*caption);

    expander->set_child(*inner);
    container.append(*expander);
}

} // namespace Folio
