
namespace Folio {

// ─────────────────────────────────────────────────────────────────────────────
// CSS — Dark palette (Catppuccin Mocha-inspired)
// ─────────────────────────────────────────────────────────────────────────────
static const char *FOLIO_CSS_DARK = R"CSS(
    @define-color adw_bg          #1e1e2e;
    @define-color adw_bg2         #181825;
    @define-color adw_surface     #242436;
    @define-color adw_surface2    #2a2a3e;
    @define-color adw_overlay     #2a2a3e;
    @define-color adw_overlay2    #3a3a54;
    @define-color accent          #5bc8af;
    @define-color accent_dim      rgba(91,200,175,0.15);
    @define-color accent_border   rgba(91,200,175,0.35);
    @define-color tx1             #cdd6f4;
    @define-color tx2             #b8bfdd;
    @define-color tx3             #9196b4;
    @define-color tx4             #5a5d75;
    @define-color tx_popup        #e8ecfc;
    @define-color col_red         #f38ba8;
    @define-color col_yellow      #f9e2af;
    @define-color col_green       #a6e3a1;
    @define-color col_mauve       #cba6f7;
    @define-color col_peach       #fab387;
    @define-color border_subtle   rgba(255,255,255,0.06);
    @define-color border_strong   rgba(0,0,0,0.28);
    @define-color hover_overlay   rgba(255,255,255,0.04);
    @define-color body_text       #b0b8d8;
    @define-color btn_neutral     #4a4a4a;
    @define-color btn_neutral_hov #585858;
    @define-color input_bg        #2a2a3e;
    @define-color paper_bg        #242436;
    .snap-date { color: #8a90b0; }   /* brighter than tx3 for dark surfaces */
    )CSS";

// ─────────────────────────────────────────────────────────────────────────────
// CSS — Light palette (Catppuccin Latte-inspired)
// ─────────────────────────────────────────────────────────────────────────────
static const char *FOLIO_CSS_LIGHT = R"CSS(
    @define-color adw_bg          #c8cbd4;
    @define-color adw_bg2         #babdc8;
    @define-color adw_surface     #adb1bc;
    @define-color adw_surface2    #a0a4b0;
    @define-color adw_overlay     #a0a4b0;
    @define-color adw_overlay2    #868b98;
    @define-color accent          #0e6368;
    @define-color accent_dim      rgba(14,99,104,0.16);
    @define-color accent_border   rgba(14,99,104,0.45);
    @define-color tx1             #080a10;
    @define-color tx2             #12152a;
    @define-color tx3             #2e3150;
    @define-color tx4             #52566e;
    @define-color tx_popup        #060810;
    @define-color col_red         #960d28;
    @define-color col_yellow      #724a00;
    @define-color col_green       #246018;
    @define-color col_mauve       #5a1fb0;
    @define-color col_peach       #a03800;
    @define-color border_subtle   rgba(0,0,0,0.18);
    @define-color border_strong   rgba(0,0,0,0.32);
    @define-color hover_overlay   rgba(0,0,0,0.09);
    @define-color body_text       #12152a;
    @define-color btn_neutral     #787878;
    @define-color btn_neutral_hov #686868;
    @define-color input_bg        #a0a4b0;
    @define-color paper_bg        #d4d6dc;
    .snap-date { color: #2e3150; }
    )CSS";

// ─────────────────────────────────────────────────────────────────────────────
// CSS — Shared structural rules (reference the palette variables above)
// ─────────────────────────────────────────────────────────────────────────────
static const char *FOLIO_CSS_SHARED = R"CSS(
    * { font-family: "Cantarell", "Noto Sans", sans-serif; color: @tx1; }
    window { background-color: @adw_bg; }
    
    /* ── Header bar ─────────────────────────────────────────────────────────── */
    headerbar {
        background-color: @adw_surface;
        border-bottom: 1px solid @border_strong;
        box-shadow: none; padding: 0 8px; min-height: 36px;
    }
    headerbar button {
        background-color: transparent; border: none; box-shadow: none;
        color: @tx2; min-height: 0; padding: 0px 0px; margin: 0px 0px; font-size: 13px;
    }
    headerbar button:hover { background-color: @adw_overlay; color: @tx1; }
    
    /* ── Pill buttons ────────────────────────────────────────────────────────── */
    .pill-btn {
        border-radius: 9999px; background-color: @adw_overlay;
        border: 1px solid @border_subtle; color: @tx2;
        font-weight: 600; padding: 2px 12px; min-height: 0;
        margin: 4px 2px; font-size: 12px; box-shadow: none;
    }
    .pill-btn:hover { background-color: @adw_overlay2; color: @tx1; }
    .pill-btn.pill-btn-primary { background-color: @btn_neutral; border-color: transparent; color: #ffffff; }
    .pill-btn.pill-btn-primary:hover { background-color: @btn_neutral_hov; border-color: transparent; color: #ffffff; }
    /* Tighten the extra-menu popover menu items */
    textview > popover > contents menuitem,
    popover.menu menuitem {
        padding: 1px 8px;
        min-height: 0;
    }
    popover.menu separator { margin: 1px 0; }
    .notes-entry-frame {
        border: 1px solid @border_subtle;
        border-radius: 10px;
        background-color: @adw_surface2;
    }
    .notes-entry-view {
        background-color: transparent;
        border-radius: 10px;
        padding: 8px 10px;
        font-size: 12px;
        color: @tx2;
    }
    
    /* ── Icon buttons ────────────────────────────────────────────────────────── */
    .icon-btn {
        border-radius: 9999px; background: transparent; border: none;
        color: @tx2; padding: 2px 5px; min-height: 0; margin: 4px 2px;
        min-width: 24px; font-size: 14px; box-shadow: none;
    }
    .icon-btn:hover { background-color: @adw_overlay; color: @tx1; }
    .icon-btn.active { background-color: @accent_dim; color: @accent; }

    /* ── General toggle "on" state ────────────────────────────────────────────
       Flat and icon toggle buttons (e.g. the Inspector pin) had no checked-state
       styling, so "on" was nearly invisible. Give every ungrouped toggle a clear
       accent fill. More specific rules below (segmented groups, dropdowns,
       inspector tabs, swatches, switches) still override this where intended. */
    togglebutton:checked,
    togglebutton.active,
    togglebutton.flat:checked,
    togglebutton.flat.active,
    togglebutton.flat.circular:checked,
    button.toggle:checked {
        background-color: @accent_color;
        color: white;
        box-shadow: 0 1px 3px alpha(black, 0.25);
    }
    togglebutton:checked:hover,
    togglebutton.flat:checked:hover,
    togglebutton.flat.circular:checked:hover {
        background-color: @accent_color;
        filter: brightness(1.08);
    }

    /* Pin toggle (Inspector) — its own class so it doesn't collide with libadwaita's
       .flat styling. The "on" look is driven by a .pinned class set from code (this
       GTK build doesn't reliably put GtkToggleButton into the :checked CSS state).
       The visible node is the child symbolic <image>, which takes `color` (it's a
       mask), so we recolour the icon as well as filling the button. */
    .pin-toggle {
        min-width: 30px; min-height: 30px; padding: 4px;
        border-radius: 9999px; border: none; box-shadow: none;
        background: transparent;
    }
    .pin-toggle image { color: alpha(@tx1, 0.32); }    /* off — clearly ghosted */
    .pin-toggle:hover { background-color: alpha(@tx1, 0.10); }
    .pin-toggle:hover image { color: alpha(@tx1, 0.65); }
    .pin-toggle.pinned {
        background-color: @accent_color;
        box-shadow: 0 1px 4px alpha(black, 0.30);
    }
    .pin-toggle.pinned image { color: white; }         /* on — white pin on accent */
    .pin-toggle.pinned:hover {
        background-color: @accent_color; filter: brightness(1.10);
    }

    /* ── View toggle group (segmented control) ───────────────────────────────── */
    .view-toggle-group {
        background-color: @adw_surface_2; border-radius: 6px;
        border: 1px solid @border_subtle; padding: 2px;
    }
    .view-toggle-group button {
        border-radius: 4px; background: transparent; border: none;
        box-shadow: none; font-size: 13px; font-weight: 700;
        color: @tx3; padding: 2px 10px; min-height: 0; min-width: 0;
    }
    .view-toggle-group button:hover { color: @tx1; background-color: alpha(@tx1, 0.08); }
    .view-toggle-group button:checked,
    .view-toggle-group button.active {
        background-color: @accent_color; color: white;
        box-shadow: 0 1px 3px alpha(black, 0.25);
    }
    /* Writing mode toggle — Novel / Outline / Screenplay */
    /* ── Sidebar ─────────────────────────────────────────────────────────────── */
    .folio-sidebar { background-color: @adw_surface; border-right: 1px solid @border_strong; }
    .binder-section-sep { margin: 8px 0; min-height: 1px; background-color: alpha(@tx3, 0.4); border: none; }
    .sidebar-section-label {
        font-size: 11px; font-weight: 700; letter-spacing: 0.08em;
        color: @tx3; padding: 8px 6px 4px 6px; text-transform: uppercase;
    }
    .sidebar-section-icon { color: @tx3; margin-left: 4px; }
    .section-arrow {
        font-size: 20px; color: @tx3;
        padding-bottom: 2px;
        transition: color 150ms;
    }
    .section-arrow-collapsed { color: @tx4; }
    .folio-sidebar listbox {
        background-color: @adw_surface2; border-radius: 12px;
        border: 1px solid @border_subtle; margin: 0 4px 12px 4px;
    }
    .folio-sidebar listbox row {
        background-color: @adw_surface2; border-radius: 0;
        padding: 0; min-height: 44px; border-bottom: 1px solid @border_subtle;
    }
    .folio-sidebar listbox row:first-child  { border-radius: 12px 12px 0 0; }
    .folio-sidebar listbox row:last-child   { border-radius: 0 0 12px 12px; border-bottom: none; }
    .folio-sidebar listbox row:only-child   { border-radius: 12px; }
    .folio-sidebar listbox row:hover        { background-color: @adw_overlay; }
    .folio-sidebar listbox row:selected,
    .folio-sidebar listbox row:selected:hover { background-color: @accent_dim; }
    .folio-sidebar listbox row:selected .row-title { color: @accent; }
    .row-title    { font-size: 13px; font-weight: 700; color: @tx1; }
    .row-subtitle { font-size: 11px; color: @tx3; }
    /* Reference URL pill in sidebar leaf rows */
    .global-tpl-badge { color: @tx3; opacity: 0.8; }
    .ref-url-pill {
        background-color: alpha(@accent, 0.12);
        border-radius: 4px;
        border: none;
        box-shadow: none;
        padding: 1px 5px;
        margin-top: 1px;
        min-height: 0;
    }
    .ref-url-pill:hover { background-color: alpha(@accent, 0.22); }
    .ref-url-label { font-size: 10px; color: @accent; }
    .part-row-bg { background-color: @hover_overlay; }
    .part-header-btn {
        background-color: transparent; border: none;
        border-radius: 0; box-shadow: none; padding: 0; min-height: 0;
    }
    .part-header-btn:hover { background-color: @adw_overlay; }
    .part-header-btn:first-child { border-radius: 12px 12px 0 0; }
    .part-header-btn.collapsed   { border-radius: 12px; }
    .part-arrow { font-size: 20px; color: @tx2; }
    .part-arrow.expanded  { color: @accent; }
    .part-arrow.collapsed { color: @tx2; }
    .add-scene-btn {
        background-color: transparent; border: none; box-shadow: none;
        color: @tx3; font-size: 11px; font-weight: 700;
        padding: 6px 12px; min-height: 0; border-radius: 0 0 12px 12px;
    }
    .add-scene-btn:hover { background-color: @accent_dim; color: @accent; }
    
    /* Context menu popover */
    popover.background {
        background-color: @adw_surface2; border: 1px solid @border_subtle;
        border-radius: 10px; padding: 4px;
    }
    
    /* Scene icon — glyph only, no background pill */
    .scene-icon { min-width: 28px; min-height: 28px; }
    
    /* Status squares — drawn as Box widgets matching label-colour swatch style */
    .status-dot-dim { color: @tx4; }
    
    /* Badge chip */
    .badge-chip {
        border-radius: 9999px; padding: 2px 6px; font-size: 10px;
        font-weight: 700; min-height: 0;
        background-color: @adw_overlay2; color: @tx3;
    }
    .badge-chip.accent { background-color: @accent_dim; color: @accent; }
    .badge-chip.warn   { background-color: rgba(249,226,175,0.15); color: @col_yellow; }
    
    /* Session footer */
    .session-card {
        background-color: @adw_surface2; border-radius: 12px;
        border: 1px solid @border_subtle; padding: 10px 12px; margin: 8px;
    }
    .session-words {
        font-size: 20px; font-weight: 700;
        font-family: "Source Code Pro","Noto Mono",monospace; color: @tx1;
    }
    .session-label {
        font-size: 11px; font-weight: 700; letter-spacing: 0.06em;
        text-transform: uppercase; color: @tx3;
    }
    .inspector-section-label {
        font-size: 11px; font-weight: 700; letter-spacing: 0.06em;
        text-transform: uppercase; color: @tx3;
    }
    .session-pct { font-size: 11px; color: @tx3; }
    
    /* ── Timeline strip ──────────────────────────────────────────────────────── */
    .folio-timeline {
        background-color: @adw_bg2; border-bottom: 1px solid @border_strong;
        padding: 1px 4px; min-height: 14px;
    }
    /* Arrow nav buttons on each end of the timeline */
    .timeline-arrow-btn {
        background-color: transparent;
        border: none;
        box-shadow: none;
        border-radius: 4px;
        color: @tx3;
        font-size: 10px;
        min-width: 18px;
        min-height: 0;
        padding: 0 3px;
        margin: 0 1px;
    }
    .timeline-arrow-btn:hover {
        background-color: @adw_overlay;
        color: @tx1;
    }
    .timeline-arrow-btn:disabled { opacity: 0.25; }
    .timeline-chip {
        background-color: @adw_surface2; border-radius: 5px;
        border: 1px solid @border_subtle; padding: 0px 4px;
        min-width: 40px; min-height: 8px;
    }
    .timeline-chip:hover { background-color: @adw_overlay; }
    .timeline-chip.active { background-color: @adw_surface; border-color: @accent_border; }
    .timeline-chip.active .chip-name { color: @tx1; }
    .timeline-chip.chip-drag-source { opacity: 0.4; }
    .timeline-chip.chip-selected { border-color: @accent_border; background-color: @accent_dim; }
    .timeline-chip.chip-drop-before { border-left:  2px solid @accent; margin-left:  -1px; }
    .timeline-chip.chip-drop-after  { border-right: 2px solid @accent; margin-right: -1px; }
    .chip-name { font-size: 10px; font-weight: 700; color: @tx2; }
    .chip-wc   { font-size: 9px; color: @tx3; font-family: "Source Code Pro",monospace; }
    .chip-accent-bar-teal    { background-color: @accent; }
    .chip-accent-bar-mauve   { background-color: @col_mauve; }
    .chip-accent-bar-peach   { background-color: @col_peach; }
    .chip-accent-bar-red     { background-color: @col_red; }
    .chip-accent-bar-green   { background-color: @col_green; }
    .chip-accent-bar-yellow  { background-color: @col_yellow; }
    .chip-accent-bar-sky     { background-color: #89dceb; }
    .chip-accent-bar-blue    { background-color: #89b4fa; }
    .chip-accent-bar-flamingo{ background-color: @col_red; }
    
    /* ── Editor viewbar ─────────────────────────────────────────────────────── */
    .folio-viewbar {
        background-color: @adw_bg; border-bottom: 1px solid @border_strong;
        padding: 2px 4px; min-height: 32px;
    }
    .folio-viewbar spinbutton { padding: 0px 2px; min-height: 0; min-width: 0; font-size: 11px; }
    .folio-viewbar spinbutton button { padding: 0px 2px; min-height: 0; min-width: 0; }
    .folio-viewbar dropdown { padding: 0px 2px; min-height: 0; font-size: 11px; }
    .folio-viewbar dropdown button { padding: 0px 4px; min-height: 0; }
    .folio-viewbar label { font-size: 11px; padding: 0; margin: 0; }
    /* Page-width percentage scrollbar + entry */
    .page-width-box {
        border: 1px solid @border_subtle;
        border-radius: 6px;
        padding: 1px 4px;
        background-color: @adw_surface;
    }
    .page-width-bar { min-width: 90px; }
    .page-width-bar trough { min-height: 6px; border-radius: 3px; }
    .page-width-bar slider { min-width: 12px; min-height: 12px; border-radius: 6px; }
    .page-width-entry { font-size: 11px; min-width: 2em;
                        padding: 0 2px; }
    .fmt-btn {
        border-radius: 4px; background: transparent; border: none; box-shadow: none;
        color: @tx3; padding: 1px 5px; min-height: 0; min-width: 0;
        font-size: 11px; font-weight: 700;
    }
    .fmt-btn:hover { background-color: @adw_overlay; color: @tx1; }
    .fmt-btn:checked, .fmt-btn.active { background-color: @accent_dim; color: @accent; }
    .fmt-btn.active image { color: @accent; }   /* s44 — reach the symbolic icon mask */
    .fmt-btn-pilcrow { font-size: 19px; padding: 0px 4px; font-family: serif; }
    .fmt-btn-sp-help { font-size: 13px; font-weight: 700; padding: 0px 5px; }
    .typewriter-btn { font-size: 22px; padding: 4px 4px; }
    
    /* Focus mode exit button */
    .focus-exit-btn {
        background-color: alpha(@adw_surface, 0.85); border: 1px solid @border_subtle;
        border-radius: 9999px; color: @tx2; font-size: 11px; padding: 4px 12px;
        box-shadow: 0 2px 8px rgba(0,0,0,0.4);
        opacity: 0; transition: opacity 160ms ease;   /* s46 — hide until hovered */
    }
    .focus-exit-btn:hover { background-color: @adw_overlay; color: @tx1; opacity: 1; }

    /* Focus mode page-width bar (bottom-centre overlay) */
    .focus-width-bar {
        background-color: alpha(@adw_surface, 0.80); border: 1px solid @border_subtle;
        border-radius: 9999px; padding: 4px 14px;
        box-shadow: 0 2px 8px rgba(0,0,0,0.35);
    }
    .focus-width-scale trough {
        min-height: 6px;
        border-radius: 9999px;
        background-color: @adw_overlay2;
    }
    .focus-width-scale highlight {
        background-color: @accent;
        border-radius: 9999px;
    }
    .focus-width-scale slider {
        min-width: 18px;
        min-height: 18px;
        background-color: @accent;
        border-radius: 9999px;
        border: none;
        box-shadow: 0 1px 4px @border_strong;
    }

    /* s45 — focus typewriter rail slider (right-edge, alt-summoned) */
    .focus-tw-slider trough {
        min-width: 6px;
        border-radius: 9999px;
        background-color: alpha(@adw_overlay2, 0.85);
    }
    .focus-tw-slider highlight {
        background-color: @accent;
        border-radius: 9999px;
    }
    .focus-tw-slider slider {
        min-width: 18px;
        min-height: 18px;
        background-color: @accent;
        border-radius: 9999px;
        border: none;
        box-shadow: 0 1px 4px @border_strong;
    }

    /* s45 — focus backdrop layers. The dim scrim is solid black drawn at a widget
       opacity = the Dim knob; the panel is the plain focus surface colour drawn at
       a widget opacity = the Panel knob, so 1.0 reproduces today's surface exactly
       and lower values let the photo through. */
    #focus-backdrop-dim   { background-color: #000000; }
    /* When a backdrop is live, the text surface goes transparent so the panel (and
       photo in the margins) shows; with no backdrop the class is absent and the
       surface is unchanged. */
    #focus-window.backdrop #focus-scroll,
    #focus-window.backdrop #focus-scroll > viewport,
    #focus-window.backdrop #focus-view,
    #focus-window.backdrop #focus-view text { background-color: transparent; }

    /* s45 — left settings drawer */
    .focus-drawer {
        background-color: @adw_bg2;
        border-right: 1px solid @border_subtle;
        box-shadow: 6px 0 28px rgba(0,0,0,0.45);
        padding: 0;
    }
    .focus-drawer-header {
        padding: 14px 16px 12px 18px;
        border-bottom: 1px solid @border_subtle;
    }
    .focus-drawer-title {
        font-size: 14px; font-weight: 700; color: @tx1; letter-spacing: 0.01em;
    }
    .focus-drawer-close {
        font-size: 18px; color: @tx3; min-width: 28px; padding: 0 6px;
    }
    .focus-drawer-close:hover { color: @tx1; }
    .focus-drawer-body { padding: 6px 18px 18px 18px; }
    .focus-section-header {
        font-size: 10px; font-weight: 700; color: @tx3;
        letter-spacing: 0.10em; text-transform: uppercase;
        margin-top: 18px; margin-bottom: 4px;
    }
    .focus-setting-row { padding: 6px 0; }
    .focus-setting-label { font-size: 12px; color: @tx2; }
    .focus-setting-value { font-size: 12px; color: @tx3; font-feature-settings: "tnum"; }
    .focus-drawer-action {
        margin-top: 8px; padding: 7px 12px; border-radius: 8px;
        background-color: @adw_surface; color: @tx2; font-size: 12px;
    }
    .focus-drawer-action:hover { background-color: @adw_overlay; color: @tx1; }
    /* The always-visible pull tab on the left edge — quiet, low-contrast. */
    .focus-drawer-tab {
        background-color: alpha(@adw_surface, 0.72);
        color: @tx3; font-size: 15px;
        border: 1px solid @border_subtle; border-left: none;
        border-radius: 0 10px 10px 0;
        min-width: 24px; min-height: 200px; padding: 0;
        box-shadow: 2px 0 8px rgba(0,0,0,0.30);
        opacity: 0; transition: opacity 160ms ease;   /* s46 — hide until hovered */
    }
    .focus-drawer-tab:hover { background-color: @adw_overlay; color: @tx1; opacity: 1; }
    /* Transparent click-away catcher; only present while the drawer is open. */
    #focus-drawer-scrim { background-color: transparent; }

    /* s45 — top-centre scene breadcrumb (the visible nav door) */
    .focus-navbar {
        background-color: alpha(@adw_surface, 0.82);
        border: 1px solid @border_subtle;
        border-radius: 9999px;
        padding: 2px 4px;
        box-shadow: 0 2px 8px rgba(0,0,0,0.35);
        opacity: 0; transition: opacity 160ms ease;   /* s46 — hide until hovered */
    }
    .focus-navbar:hover { opacity: 1; }
    .focus-nav-arrow {
        background: none; border: none; box-shadow: none;
        color: @tx3; font-size: 15px; min-width: 26px; padding: 2px 6px;
        border-radius: 9999px;
    }
    .focus-nav-arrow:hover { background-color: @adw_overlay; color: @tx1; }
    .focus-nav-title {
        background: none; border: none; box-shadow: none;
        color: @tx2; font-size: 12px; font-weight: 600; padding: 2px 12px;
        border-radius: 9999px;
    }
    .focus-nav-title:hover { background-color: @adw_overlay; color: @tx1; }

    /* s46 — transient confirmation pill (bottom-centre), fades in then out */
    .focus-toast {
        background-color: alpha(@adw_surface, 0.92);
        border: 1px solid @border_subtle;
        border-radius: 9999px;
        color: @tx1; font-size: 12px; padding: 6px 16px;
        box-shadow: 0 2px 10px rgba(0,0,0,0.40);
        opacity: 0; transition: opacity 200ms ease;
    }
    .focus-toast.show { opacity: 1; }

    /* s46 — centred overlay panels: the scene switcher and the link picker share
       this card look (the switcher had none before). Dark, lifted, rounded. */
    .focus-switcher {
        background-color: alpha(@adw_surface, 0.97);
        border: 1px solid @border_subtle;
        border-radius: 14px;
        padding: 14px;
        box-shadow: 0 12px 40px rgba(0,0,0,0.55);
    }
    .focus-link-title {
        font-size: 13px; font-weight: 700; color: @tx1;
        letter-spacing: 0.02em; margin-bottom: 2px;
    }

    /* s45 — colour swatch buttons in the drawer (panel + text colour) */
    .focus-color-mb { min-width: 0; min-height: 0; padding: 0; }
    .focus-color-mb > button {
        padding: 3px; background: none; border: 1px solid @border_subtle;
        border-radius: 7px; box-shadow: none; min-height: 0;
    }
    .focus-color-mb > button:hover { background-color: @adw_overlay; }
    .folio-color-picker { padding: 10px; }
    .folio-color-picker-recents { margin-top: 8px; }
    
    /* ── Editor paper card ──────────────────────────────────────────────────── */
    .folio-paper {
        background-color: @paper_bg; border-radius: 16px; border: 1px solid @border_subtle;
    }
    .header-disclosure-btn {
        font-size: 11px; font-weight: 600; color: @tx4; padding: 0 2px;
        min-height: 0; border: none; box-shadow: none; background: none;
    }
    .header-disclosure-btn:hover { color: @tx2; }
    .paper-chapter-tag {
        border-radius: 9999px; background-color: @accent_dim;
        border: 1px solid @accent_border; color: @accent;
        font-size: 10px; font-weight: 700; letter-spacing: 0.06em;
        padding: 1px 10px;
    }
    .paper-title {
        font-family: "Lora","DejaVu Serif",serif;
        font-size: 16px; font-weight: 600; font-style: italic;
        color: @tx2; letter-spacing: -0.01em;
    }
    .paper-body text {
        font-family: "Lora","DejaVu Serif",serif;
        font-size: 16px; line-height: 1.9; color: @body_text; background-color: @paper_bg;
    }
    .paper-body { background-color: @paper_bg; }
    /* ── Outline / Grid view ─────────────────────────────────────────── */
    .outline-grid { background-color: @adw_bg; }
    .outline-col-header {
        font-size: 11px; font-weight: 700; letter-spacing: 0.06em;
        text-transform: uppercase; color: @tx3;
        background: @adw_surface; border: none; border-radius: 0;
        padding: 4px 8px; min-height: 0;
        border-bottom: 2px solid @accent;
    }
    .outline-col-header:hover { color: @accent; background: @adw_surface; }
    .outline-col-header:disabled { color: @tx4; }
    .outline-cell {
        font-size: 13px; padding: 2px 6px; min-height: 28px;
        border-right: 1px solid @border_subtle;
    }
    .outline-group-title { font-weight: 700; background: alpha(@tx1, 0.04); }
    .outline-row-alt { background-color: alpha(@tx1, 0.03); }
    .outline-row-selected { background-color: alpha(@accent, 0.12); }

    /* ── Line-number gutter ─────────────────────────────────────────────────── */
    .line-number-gutter {
        background-color: @paper_bg;
        min-width: 48px;
    }
    .backtrace-gutter {
        background-color: @paper_bg;
    }
    .invis-overlay {
        background: transparent;
    }
    /* ── Backtrace picker popover ────────────────────────────────────────────── */
    .backtrace-picker-icon {
        color: #60a5fa;
        font-size: 11px;
        margin-top: 1px;
    }
    .backtrace-list row {
        border-radius: 6px;
    }
    .backtrace-row-title {
        font-size: 13px;
        font-weight: 600;
        color: @tx1;
    }
    .backtrace-row-sub {
        font-size: 11px;
        font-style: italic;
        color: @tx3;
    }

    /* ── Format popover ──────────────────────────────────────────────────────── */
    .format-popover > contents {
        padding: 0;
    }
    /* Dedicated popup colour + font overrides via specificity, not !important */
    .format-popover *,
    .format-popover button,
    .format-popover label,
    .format-popover spinbutton,
    .format-popover dropdown {
        color: @tx_popup;
        font-size: 14px;
    }
    .format-popover .fmt-btn,
    .format-popover .view-toggle-group button {
        min-width: 40px;
        min-height: 36px;
        padding: 4px 8px;
        font-size: 16px;
        font-weight: 700;
        color: @tx_popup;
    }
    .format-popover spinbutton {
        font-size: 14px;
        min-height: 34px;
        min-width: 56px;
    }
    .format-popover dropdown,
    .format-popover dropdown button,
    .format-popover dropdown button.combo {
        font-size: 14px;
        min-height: 34px;
        padding: 2px 8px;
    }
    .format-popover dropdown > button > box > label,
    .format-popover dropdown label,
    .format-popover .stat-label {
        font-size: 14px;
        color: @tx_popup;
    }
    /* Clear-all-formatting button — full-width, muted destructive colour */
    .format-popover .clear-fmt-btn {
        background-color: transparent;
        color: @col_red;
        border-radius: 8px;
        font-weight: 600;
        font-size: 14px;
        min-height: 36px;
        padding: 4px 12px;
    }
    .format-popover .clear-fmt-btn:hover {
        background-color: alpha(@col_red, 0.22);
    }

    /* Page-width scale inside the layout popover */
    .format-popover scale {
        min-height: 28px;
    }
    .format-popover scale trough {
        min-height: 6px;
        border-radius: 9999px;
        background-color: @adw_overlay2;
    }
    .format-popover scale highlight {
        background-color: @accent;
        border-radius: 9999px;
    }
    .format-popover scale slider {
        min-width: 18px;
        min-height: 18px;
        background-color: @accent;
        border-radius: 9999px;
        border: none;
        box-shadow: 0 1px 4px @border_strong;
    }
    
    /* ── Editor footer ──────────────────────────────────────────────────────── */
    .folio-editor-footer {
        background-color: @adw_surface; border-top: 1px solid @border_strong;
        padding: 0 16px; min-height: 34px;
    }
    /* Heading outline navigator (WritingMode::Outline) */
    .heading-outline-box { background-color: @adw_surface; padding: 8px 0; }
    .houtline-row {
        border-radius: 4px; min-height: 0;
        padding: 3px 12px; margin: 1px 8px;
    }
    .houtline-row:hover { background-color: @adw_overlay; }
    .houtline-h1 { font-size: 15px; font-weight: bold;   padding-left: 12px; }
    .houtline-h2 { font-size: 13px; font-weight: bold;   padding-left: 28px; color: @tx2; }
    .houtline-h3 { font-size: 12px; font-style: italic;  padding-left: 44px; color: @tx3; }
    .houtline-empty { color: @tx3; font-size: 13px; }
    .find-bar {
        background-color: @adw_surface;
        border-top: 1px solid @border_strong;
    }
    .find-bar entry { min-height: 28px; }
    .find-bar entry.find-error { border-color: @error_color; }
    .find-bar checkbutton { font-size: 12px; font-family: monospace; }
    .find-bar button.flat { padding: 2px 6px; min-height: 0; border-radius: 4px; }
    .stat-label { font-size: 12px; font-family: "Source Code Pro",monospace; color: @tx3; }
    .stat-value { font-size: 12px; font-family: "Source Code Pro",monospace; color: @tx2; }
    .zoom-pct-entry {
        font-size: 12px; font-weight: 700;
        font-family: "Source Code Pro", monospace;
        color: @tx2; min-width: 0; padding: 0 0 0 4px;
        border-radius: 4px 0 0 4px; border: 1px solid transparent;
        background: transparent; box-shadow: none;
        caret-color: @accent;
    }
    .zoom-pct-entry:focus {
        border-color: @accent_border;
        background: @input_bg;
        color: @tx1;
    }
    .zoom-pct-sign {
        font-size: 12px; font-weight: 700;
        font-family: "Source Code Pro", monospace;
        color: @tx3; margin: 0; padding: 0;
    }
    .zoom-group { margin: 0; }
    .zoom-group button.fmt-btn { margin: 0; padding: 0 4px; }
    .zoom-scale { min-height: 0; margin: 0; }
    .zoom-scale trough { min-height: 3px; border-radius: 2px; }
    .zoom-scale marks mark { background-color: @tx4; min-height: 4px; min-width: 1px; }
    .zoom-menu-btn { font-size: 12px; padding: 4px 12px; min-height: 0; border-radius: 5px; }
    .zoom-menu-btn:hover { background-color: alpha(@accent_color, 0.15); }
    
    /* ── Inspector ──────────────────────────────────────────────────────────── */
    .folio-inspector { background-color: @adw_surface; border-left: 1px solid @border_strong; }
    .inspector-progress-footer {
        border-top: 1px solid @border_subtle;
        background-color: @adw_surface;
        padding: 8px 0 4px 0;
    }
    .inspector-tab-bar {
        background-color: @adw_bg2; border-bottom: 1px solid @border_strong; padding: 8px 8px 0 8px;
    }
    .inspector-tab {
        border-radius: 6px 6px 0 0; background: transparent; border: none;
        box-shadow: none; font-size: 12px; font-weight: 700; color: @tx3; padding: 4px 10px; min-height: 0;
    }
    .inspector-tab:hover { color: @tx2; }
    .inspector-tab:checked, .inspector-tab.active {
        background-color: @adw_surface; color: @accent;
        border: 1px solid @border_strong; border-bottom-color: @adw_surface;
    }
    .annotation-card {
        background-color: @adw_surface;
        border: 1px solid @border_subtle;
        border-radius: 8px;
    }
    .annotation-card:hover { border-color: @border_strong; }
    .annotation-kind {
        font-size: 11px; font-weight: 700; letter-spacing: 0.05em;
        text-transform: uppercase; color: @tx3;
    }
    .annotation-entry {
        background-color: @input_bg;
        border-radius: 6px;
        padding: 4px;
        font-size: 13px;
    }
    .ann-swatch { border-radius: 5px; min-width: 22px; min-height: 22px;
                  padding: 0; border: 2px solid transparent; }
    .ann-swatch:checked { border-color: @tx1; box-shadow: 0 0 0 1px @adw_bg; }
    .pref-group-title {
        font-size: 11px; font-weight: 700; letter-spacing: 0.08em;
        text-transform: uppercase; color: @tx3; padding: 0 4px 4px 4px;
    }
    .pref-listbox {
        background-color: @input_bg; border-radius: 12px; border: 1px solid @border_subtle;
    }
    .pref-listbox row {
        background-color: @input_bg;
        border-radius: 0; padding: 8px 12px; min-height: 44px; border-bottom: 1px solid @border_subtle;
    }
    .pref-listbox row:first-child { border-radius: 12px 12px 0 0; }
    .pref-listbox row:last-child  { border-radius: 0 0 12px 12px; border-bottom: none; }
    .pref-listbox row:only-child  { border-radius: 12px; }
    .pref-listbox row:hover       { background-color: @adw_overlay; }
    .folio-inspector dropdown,
    .folio-inspector dropdown button,
    .folio-inspector dropdown button.combo {
        min-height: 0; padding-top: 2px; padding-bottom: 2px;
        padding-left: 6px; padding-right: 4px; font-size: 12px;
    }
    .folio-inspector dropdown > button > box > label,
    .folio-inspector dropdown label { font-size: 12px; padding: 0; min-height: 0; }
    .folio-inspector .pref-listbox row { min-height: 0; padding-top: 4px; padding-bottom: 4px; }
    /* s43 — the in-Editor object form (set_name "object-form") is NOT under
       .folio-inspector, so it never inherited the row/dropdown tightening and
       kept the 44px base row min-height. Mirror it here, scoped by #object-form. */
    #object-form .pref-listbox row { min-height: 0; padding-top: 3px; padding-bottom: 3px; }
    #object-form dropdown,
    #object-form dropdown button,
    #object-form dropdown button.combo {
        min-height: 0; padding-top: 2px; padding-bottom: 2px;
        padding-left: 6px; padding-right: 4px; font-size: 12px;
    }
    #object-form dropdown > button > box > label,
    #object-form dropdown label { font-size: 12px; padding: 0; min-height: 0; }
    .pref-row-label { font-size: 13px; color: @tx1; }
    .pref-row-sub   { font-size: 11px; color: @tx3; }
    .synopsis-view text {
        font-size: 16px; font-family: "Cantarell",sans-serif;
        color: @tx2; background-color: transparent; padding: 6px 8px;
    }
    .synopsis-view textview { background-color: transparent; }
    .synopsis-view {
        background-color: @adw_overlay; border-radius: 10px; border: 1px solid @border_subtle;
    }
    .synopsis-view:focus-within { border-color: @accent_border; }
    
    /* ── Color swatches (legacy buttons) ────────────────────────────────────── */
    .color-swatch {
        border-radius: 6px; min-width: 26px; min-height: 26px;
        border: 2px solid transparent; padding: 0; box-shadow: none;
    }
    .color-swatch:hover { filter: brightness(1.15); }
    .color-swatch.sel   { border-color: @tx1; }

    /* ── Color chip (used inside color dropdown rows) ────────────────────────── */
    .color-chip {
        border-radius: 4px; min-width: 16px; min-height: 16px;
        border: 1px solid alpha(@tx1, 0.15);
    }
    /* Status chip — square shape to distinguish from round label-colour swatch */
    .status-chip {
        border-radius: 3px; min-width: 16px; min-height: 16px;
        border: 1px solid alpha(@tx1, 0.15);
    }

    /* ── Shared color palette ────────────────────────────────────────────────── */
    .teal     { background-color: #94e2d5; }
    .mauve    { background-color: #cba6f7; }
    .peach    { background-color: #fab387; }
    .blue     { background-color: #89b4fa; }
    .flamingo { background-color: #f38ba8; }
    .green    { background-color: #a6e3a1; }
    .red      { background-color: #f38ba8; }
    .yellow   { background-color: #f9e2af; }
    .sky      { background-color: #89dceb; }
    
    /* ── Note / snapshot cards ───────────────────────────────────────────────── */
    .note-card {
        background-color: @adw_surface2; border-radius: 10px;
        border: 1px solid @border_subtle; padding: 8px 10px; margin-bottom: 4px;
    }
    .note-card.note-dragging { opacity: 0.4; }
    .note-text   { font-size: 12px; color: @tx2; }
    .note-delete-btn {
        background: transparent; border: none; color: @tx4;
        font-size: 11px; padding: 0 4px; min-height: 0; box-shadow: none;
    }
    .note-delete-btn:hover { color: @col_red; }
    .note-drag-handle {
        color: @tx4; font-size: 14px; padding: 0 4px 0 0;
    }
    .note-drop-line {
        background-color: @accent; min-height: 2px;
        border-radius: 1px; margin: 1px 0;
    }
    .snap-item {
        background-color: @adw_surface2; border-radius: 10px;
        border: 1px solid @border_subtle; padding: 8px 10px; margin-bottom: 4px;
    }
    .snap-name { font-size: 13px; color: @tx2; }
    .snap-date { font-size: 11px; color: @tx3; font-family: "Source Code Pro",monospace; }
    .snap-action-btn { padding: 2px; min-width: 28px; min-height: 28px; }
    .badge-chip.diff-del { background-color: rgba(243,139,168,0.18); color: #f38ba8;
                           border: 1px solid rgba(243,139,168,0.35); }
    .badge-chip.diff-ins { background-color: rgba(166,227,161,0.18); color: #a6e3a1;
                           border: 1px solid rgba(166,227,161,0.35); }
    .editor-toast {
        background-color: @adw_overlay2; color: @tx1;
        border: 1px solid @border_strong; border-radius: 9999px;
        padding: 6px 18px; font-size: 12px; font-weight: 600;
    }
    
    /* ── Binder selection highlights ────────────────────────────────────────── */
    .part-header-btn.binder-selected,
    .binder-row:hover { background-color: @adw_overlay; border-radius: 6px; }
    .binder-row.binder-selected { background-color: @accent_dim; border-radius: 6px; }
    .part-header-btn.binder-selected .row-title,
    .binder-row.binder-selected .row-title { color: @accent; }
    row.scene-row-selected { background-color: @accent_dim; }
    row.scene-row-selected .row-title { color: @accent; }

    /* ── Binder drag-and-drop indicators ────────────────────────────────────── */
    .binder-drag-source {
        opacity: 0.45;
    }
    .binder-drop-before {
        border-top: 2px solid @accent;
        margin-top: -1px;
    }
    .binder-drop-after {
        border-bottom: 2px solid @accent;
        margin-bottom: -1px;
    }
    .binder-drop-inside {
        background-color: alpha(@accent, 0.18);
        border-radius: 8px;
        outline: 1.5px solid @accent;
        outline-offset: -1px;
    }
    
    /* ── Board view ──────────────────────────────────────────────────────────── */
    .board-placeholder { font-size: 14px; color: @tx3; padding: 24px; }
    .board-part-chip {
        font-size: 10px; font-weight: 700; letter-spacing: 0.05em;
        color: @tx3; text-transform: uppercase; padding-bottom: 2px;
    }
    .board-card { border-radius: 14px; }
    .board-card-preview-scroll { background-color: transparent; }
    .board-card-preview-text {
        background-color: transparent; font-family: "Lora","DejaVu Serif",serif;
        font-size: 12px; line-height: 1.7; color: @body_text;
    }
    .board-card-preview-text text {
        background-color: transparent; font-family: "Lora","DejaVu Serif",serif;
        font-size: 12px; color: @body_text;
    }
    .board-card-footer { border-top: 1px solid @border_subtle; }
    
    /* ── Level bar ───────────────────────────────────────────────────────────── */
    levelbar trough {
        background-color: @adw_overlay; border-radius: 9999px; min-height: 6px; border: none;
    }
    levelbar block.filled {
        background: linear-gradient(90deg, @accent, shade(@accent, 0.85));
        border-radius: 9999px; border: none;
    }
    
    /* ── Interactive input widgets — single consistent background ───────────── */
    /*   All inputs share @input_bg. background-image:none kills GTK gradients. */

    /* Entry */
    entry,
    spinbutton:not(.vertical),
    spinbutton.vertical > text {
        background-color: @input_bg;
        background-image: none;
        border: 1px solid @border_subtle;
        border-radius: 8px;
        color: @tx1;
        box-shadow: none;
        outline: none;
    }
    entry:focus-within,
    spinbutton:focus-within {
        border-color: @accent_border;
        background-color: @input_bg;
        outline: none;
    }
    /* Inner text node must be transparent so the parent colour shows */
    entry > text,
    spinbutton > text {
        background-color: transparent;
        background-image: none;
        color: @tx1;
    }
    searchentry {
        background-color: @input_bg;
        background-image: none;
        border-radius: 9999px;
        border: 1px solid @border_subtle;
        color: @tx1;
        font-size: 13px;
        padding: 4px 10px;
        min-height: 0;
        box-shadow: none;
    }
    searchentry:focus-within { border-color: @accent_border; }

    /* Spinbutton increment/decrement buttons */
    spinbutton > button,
    spinbutton button.up,
    spinbutton button.down {
        background-color: transparent;
        background-image: none;
        border: none;
        box-shadow: none;
        color: @tx2;
        min-width: 22px;
    }
    spinbutton > button:hover { background-color: @hover_overlay; }

    /* Dropdown closed state — outer widget + inner togglebutton */
    dropdown {
        background: none;
        border: none;
        box-shadow: none;
    }
    dropdown > button,
    dropdown > togglebutton {
        background-color: @input_bg;
        background-image: none;
        border: 1px solid @border_subtle;
        border-radius: 8px;
        box-shadow: none;
        color: @tx1;
        outline: none;
    }
    dropdown > button:hover,
    dropdown > togglebutton:hover {
        background-color: @adw_overlay;
        background-image: none;
    }
    dropdown > button:focus,
    dropdown > togglebutton:focus,
    dropdown > button:checked,
    dropdown > togglebutton:checked {
        border-color: @accent_border;
        background-color: @input_bg;
        background-image: none;
        outline: none;
    }
    dropdown > button > box > label,
    dropdown > togglebutton > box > label,
    dropdown label,
    dropdown arrow {
        color: @tx1;
    }

    /* Dropdown popup — popover + listview inside it */
    dropdown > popover,
    dropdown > popover.menu,
    dropdown > popover.background {
        background-color: @input_bg;
        border: 1px solid @border_strong;
        border-radius: 10px;
        padding: 4px;
        box-shadow: 0 4px 16px @border_strong;
    }
    dropdown > popover > contents,
    dropdown > popover.menu > contents {
        background-color: @input_bg;
        padding: 0;
    }
    dropdown popover listview,
    dropdown popover > contents listview {
        background-color: @input_bg;
        color: @tx1;
    }
    dropdown popover listview > row,
    dropdown popover listview > row.activatable {
        background-color: transparent;
        color: @tx1;
        border-radius: 6px;
        padding: 6px 6px;
    }
    dropdown popover listview > row:hover,
    dropdown popover listview > row.activatable:hover {
        background-color: @adw_overlay;
        background-image: none;
    }
    dropdown popover listview > row:selected,
    dropdown popover listview > row.activatable:selected,
    dropdown popover listview > row.activatable:selected:hover {
        background-color: @accent_dim;
        background-image: none;
        color: @tx1;
        box-shadow: none;
    }

    /* Generic listbox (bare, not .pref-listbox — e.g. Prefs nav) */
    listbox {
        background-color: @input_bg;
        color: @tx1;
    }
    listbox > row {
        background-color: @input_bg;
        color: @tx1;
    }
    listbox > row:hover  { background-color: @adw_overlay; background-image: none; }
    listbox > row:selected { background-color: @accent_dim; color: @tx1; }

    /* Generic popover background (context menus etc.) */
    popover.background,
    popover > contents {
        background-color: @input_bg;
        color: @tx1;
    }
    popover row,
    popover modelbutton {
        background-color: transparent;
        background-image: none;
        color: @tx1;
    }
    popover row:hover,
    popover modelbutton:hover {
        background-color: @adw_overlay;
        background-image: none;
    }
    /* Compact context menu — targets only our editor context menu via .ctx-menu class */
    popover.menu.ctx-menu > contents {
        padding: 2px;
        min-width: 200px;
    }
    popover.menu.ctx-menu modelbutton {
        padding: 2px 8px;
        min-height: 0;
        border-radius: 3px;
        font-size: 13px;
    }
    popover.menu.ctx-menu modelbutton label {
        padding: 0;
        margin: 0;
    }
    popover.menu.ctx-menu > contents separator {
        margin: 2px 0;
        min-height: 1px;
    }
    
    /* ── Scrollbar / paned ───────────────────────────────────────────────────── */
    scrollbar { background-color: transparent; }
    scrollbar slider {
        background-color: @adw_overlay2; border-radius: 9999px; min-width: 6px; min-height: 6px;
    }
    scrollbar slider:hover { background-color: @adw_overlay; }
    separator { background-color: @border_subtle; min-height: 1px; min-width: 1px; }
    paned > separator { background-color: @border_strong; min-width: 1px; min-height: 1px; }
    
    /* ── Switch ──────────────────────────────────────────────────────────────── */
    switch { background-color: @adw_overlay2; border-radius: 9999px; border: 1px solid @border_subtle; }
    switch:checked { background-color: @accent; }
    switch slider  { border-radius: 9999px; background-color: white; }

    /* ── Style Manager dialog ────────────────────────────────────────────────── */
    .folio-style-manager {
        background-color: @adw_bg;
    }
    .style-manager-left {
        background-color: @adw_surface;
        min-width: 200px;
    }
    .style-manager-list-header {
        padding: 12px 12px 8px 12px;
        border-bottom: 1px solid @border_subtle;
    }
    .style-manager-section-title {
        font-weight: 700;
        font-size: 13px;
        color: @tx1;
    }
    .style-manager-listbox {
        background-color: transparent;
    }
    .style-manager-listbox row {
        background-color: transparent;
        border-bottom: 1px solid @border_subtle;
        min-height: 38px;
    }
    .style-manager-listbox row:hover {
        background-color: @adw_overlay;
    }
    .style-manager-listbox row:selected,
    .style-manager-listbox row:selected:hover {
        background-color: @accent_dim;
    }
    .style-manager-actions {
        border-top: 1px solid @border_subtle;
    }
    .style-manager-right {
        background-color: @adw_bg;
        padding: 0;
    }
    /* Kind pill badges in list rows */
    .style-kind-pill {
        font-size: 11px;
        font-weight: 700;
        border-radius: 4px;
        padding: 1px 5px;
        min-width: 18px;
    }
    .style-kind-para {
        background-color: @accent_dim;
        color: @accent;
    }
    .style-kind-char {
        background-color: alpha(@col_mauve, 0.18);
        color: @col_mauve;
    }
    /* Dropdown in toolbar — slightly narrower, flush style */
    .style-picker-dropdown,
    .style-picker-dropdown > button,
    .style-picker-dropdown button {
        min-height: 0;
        padding-top: 2px;
        padding-bottom: 2px;
    }
    /* Format MenuButton: transparent at rest, matching GtkDropDown flush look */
    .folio-viewbar .style-picker-dropdown.menu-button > button {
        background-color: transparent;
        background-image: none;
        border-color: transparent;
        box-shadow: none;
    }
    /* Uniform full-background hover on Format MenuButton and GtkDropDown buttons */
    .folio-viewbar .style-picker-dropdown.menu-button > button:hover {
        background-color: @adw_overlay;
        background-image: none;
    }
    .folio-viewbar dropdown.style-picker-dropdown button:hover {
        background-color: @adw_overlay;
        background-image: none;
    }
    /* Color clear (✕) button next to each ColorDialogButton in format popover */
    .color-clear-btn {
        font-size: 11px;
        min-width: 22px;
        min-height: 22px;
        padding: 0;
        color: @tx3;
        border-radius: 5px;
    }
    .color-clear-btn:hover { color: @col_red; background-color: alpha(@col_red, 0.12); }
    /* Transparent colour button in Style Manager ─ icon + tooltip hint */
    .color-trans-btn {
        min-width: 26px;
        min-height: 26px;
        padding: 3px;
        border-radius: 6px;
        color: @tx3;
        border: 1px dashed @border_subtle;
        background-color: transparent;
        background-image: none;
        opacity: 0.75;
    }
    .color-trans-btn:hover {
        color: @tx1;
        border-color: @accent_border;
        background-color: @accent_dim;
        opacity: 1.0;
    }
    /* Preview text */
    .style-manager-preview {
        min-height: 52px;
        padding: 10px 14px;
        border-radius: 8px;
        background-color: @paper_bg;
        color: @body_text;
        border: 1px solid @border_subtle;
    }

    /* ── Pomodoro ──────────────────────────────────────────────────────────── */
    .folio-pomodoro-window { background-color: @adw_bg; }
    .pomo-root { background-color: @adw_bg; min-width: 260px; }
    .pomo-time-label {
        font-size: 40px; font-weight: 700;
        font-family: "Source Code Pro","Noto Mono",monospace;
        color: @tx1; letter-spacing: 2px;
    }
    .pomo-phase-label {
        font-size: 12px; font-weight: 600; color: @tx3;
        text-transform: uppercase; letter-spacing: 1.5px;
    }
    .pomo-play-btn {
        border-radius: 9999px; background-color: @accent;
        border: none; color: @adw_bg;
        min-width: 56px; min-height: 56px; padding: 0;
        box-shadow: 0 2px 8px alpha(@accent, 0.35);
    }
    .pomo-play-btn:hover {
        filter: brightness(1.1);
        box-shadow: 0 3px 12px alpha(@accent, 0.50);
    }
    .pomo-play-btn:active { filter: brightness(0.9); box-shadow: none; }
    .pomo-ctrl-btn {
        border-radius: 9999px; background-color: @adw_overlay;
        border: 1px solid @border_subtle; color: @tx2;
        min-width: 38px; min-height: 38px; padding: 0;
    }
    .pomo-ctrl-btn:hover { background-color: @adw_overlay2; color: @tx1; }
    .pomo-dot { border-radius: 9999px; min-width: 10px; min-height: 10px; }
    .pomo-dot-done   { background-color: @accent; }
    .pomo-dot-active { background-color: alpha(@accent, 0.45); border: 1px solid @accent; }
    .pomo-dot-empty  { background-color: @adw_overlay2; }

    /* Phase banner — coloured strip showing work/rest state */
    .pomo-phase-banner {
        border-radius: 10px;
        padding: 10px 14px;
        transition: background-color 400ms ease, border-color 400ms ease;
        border: 1px solid transparent;
    }
    /* Focus — teal accent */
    .pomo-banner-focus {
        background-color: alpha(@accent, 0.15);
        border-color: alpha(@accent, 0.35);
    }
    /* Short break — green */
    .pomo-banner-short {
        background-color: alpha(@col_green, 0.15);
        border-color: alpha(@col_green, 0.35);
    }
    /* Long break — mauve */
    .pomo-banner-long {
        background-color: alpha(@col_mauve, 0.15);
        border-color: alpha(@col_mauve, 0.35);
    }
    /* Cycle complete — gold celebration */
    .pomo-banner-cycle {
        background-color: alpha(@col_yellow, 0.18);
        border-color: alpha(@col_yellow, 0.45);
    }
    /* Paused state — dim the banner */
    .pomo-banner-paused { opacity: 0.45; }
    /* Flash animation on phase transition */
    .pomo-banner-flash { opacity: 1.0; filter: brightness(1.4); }
    .pomo-banner-icon { font-size: 18px; min-width: 26px; }
    .pomo-banner-text {
        font-size: 13px; font-weight: 600; color: @tx1;
    }
    .pomo-settings-panel {
        background-color: @adw_surface;
        border-top: 1px solid @border_subtle;
        padding: 12px 0 4px 0;
    }
    .pomo-settings-row  { padding: 2px 0; }
    .pomo-settings-label { font-size: 13px; color: @tx2; }
    .pomo-settings-unit  { font-size: 11px; color: @tx3; min-width: 24px; }

    /* ── Pomodoro sidebar disclosure tile ───────────────────────────────────── */
    .pomo-tile-card {
        background-color: @adw_surface2;
        border-radius: 12px;
        border: 1px solid @border_subtle;
        padding: 8px 12px;
        transition: background-color 150ms ease, border-color 150ms ease;
    }
    .session-tile-wrapper {
        background-color: @adw_surface2;
        border-radius: 12px;
        border: 1px solid @border_subtle;
        padding: 8px 12px;
    }
    /* Shared header row style for both disclosure tiles */
    .tile-header-row { padding: 0; }
    .tile-header-row:hover .section-arrow { color: @tx2; }
    .pomo-tile-time {
        font-size: 18px;
        font-weight: 700;
        font-family: "Source Code Pro","Noto Mono",monospace;
        color: @tx1;
        letter-spacing: 1px;
    }
    .pomo-tile-phase {
        font-size: 10px;
        font-weight: 700;
        letter-spacing: 1.2px;
        text-transform: uppercase;
        color: @tx3;
    }
    .pomo-tile-open-btn {
        background-color: @accent_dim;
        border: 1px solid @accent_border;
        border-radius: 8px;
        color: @accent;
        font-size: 11px;
        font-weight: 600;
        padding: 3px 10px;
        min-height: 0;
        box-shadow: none;
    }
    .pomo-tile-open-btn:hover {
        background-color: alpha(@accent, 0.25);
    }
    /* Play/pause button overlaid on the mini ring */
    .pomo-tile-play-btn {
        border-radius: 9999px;
        background-color: alpha(@adw_bg, 0.75);
        border: none;
        box-shadow: none;
        min-width: 26px;
        min-height: 26px;
        padding: 0;
        color: @tx1;
    }
    .pomo-tile-play-btn:hover {
        background-color: alpha(@accent, 0.25);
        color: @accent;
    }

    /* ── Pomodoro headerbar pill ─────────────────────────────────────────────── */
    .pomo-hdr-pill {
        background-color: @adw_surface2;
        border: 1px solid @border_subtle;
        border-radius: 9999px;
        padding: 2px 10px;
        margin: 4px 4px;
        transition: background-color 150ms;
    }
    .pomo-hdr-pill:hover { background-color: @adw_overlay; }
    .pomo-hdr-focus  { border-color: alpha(@accent, 0.45); }
    .pomo-hdr-short  { border-color: alpha(@col_green, 0.45); }
    .pomo-hdr-long   { border-color: alpha(@col_mauve, 0.45); }
    .pomo-hdr-paused { border-color: alpha(@tx4, 0.35); opacity: 0.75; }
    .pomo-hdr-phase {
        font-size: 11px;
        font-weight: 700;
        color: @tx2;
        letter-spacing: 0.04em;
    }
    .pomo-hdr-time {
        font-size: 11px;
        font-weight: 700;
        font-family: "Source Code Pro","Noto Mono",monospace;
        color: @tx1;
        letter-spacing: 0.5px;
    }
    .pomo-hdr-sep { font-size: 11px; color: @tx4; }

    /* ── Editor ruler ────────────────────────────────────────────────────────── */
    .editor-ruler {
        min-height: 26px;
        background-color: transparent;
        border-bottom: 1px solid alpha(@border_strong, 0.4);
    }

    /* ── Spell / right-click context menu ────────────────────────────────────── */
    .spell-context-menu > contents {
        padding: 4px 0;
    }
    .spell-context-menu button.spell-menu-item {
        font-size: 15px;
        font-weight: 500;
        min-height: 34px;
        padding: 4px 16px;
        border-radius: 0;
        color: @tx_popup;
    }
    .spell-context-menu button.spell-menu-item:hover {
        background-color: alpha(@accent, 0.15);
    }
    .spell-context-menu button.spell-suggestion {
        font-size: 15px;
        font-weight: 700;
        color: @accent;
    }
    .spell-context-menu button.spell-menu-item:disabled {
        opacity: 0.4;
    }
    .spell-context-menu separator {
        margin: 2px 8px;
    }

    /* ── Unicode character picker ─────────────────────────────────────────────── */
    .unicode-picker > contents {
        padding: 0;
    }
    .unicode-picker-section {
        font-size: 11px;
        font-weight: 700;
        letter-spacing: 0.06em;
        color: @tx3;
        text-transform: uppercase;
    }
    .unicode-char-btn {
        font-size: 20px;
        font-weight: 400;
        min-width: 40px;
        min-height: 40px;
        padding: 2px;
        border-radius: 4px;
        color: @tx_popup;
    }
    .unicode-char-btn:hover {
        background-color: alpha(@accent, 0.18);
        color: @accent;
    }
    )CSS";
} // namespace Folio