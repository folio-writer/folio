#pragma once
// s98 — Shortcut registry: the single source of truth for every keyboard/mouse
// shortcut in Folio. GTK-free (pure data + string helpers), so it is sandbox-
// testable and pulled into both the wiring (MainWindow::set_accels_for_action)
// and the documentation (ShortcutsDialog). Register a shortcut once here; the
// accelerator and its dialog row both come from this list, so the two can never
// drift apart, and a pure test guards against two behaviours claiming one chord.
#include <string>
#include <vector>

namespace Folio {

// Which dialog tab a spec belongs to.
enum class ShortcutTab { Keyboard, Mouse };

// The window a shortcut lives in. Main = everything hosted in the main window
// (global GActions, the editor, the binder, the timeline) — these coexist, so a
// GAction accelerator and any other Main behaviour on the same chord DO clash.
// Focus = the separate focus window, a distinct capture surface, so a Focus chord
// equal to a Main chord is NOT a clash.
enum class ShortcutContext { Main, Focus };

struct ShortcutSpec {
  ShortcutTab              tab;
  ShortcutContext          context;
  std::string              section;      // group heading, e.g. "File" (authored A-Z per tab)
  std::string              action;       // "win.new" — empty ⇒ not wired as a GAction (doc-only)
  std::vector<std::string> accels;       // GTK accel form(s), e.g. {"<Ctrl>n"}; wired iff action set
  std::string              keys;         // explicit display; empty ⇒ derived from accels
  std::string              description;

  // Human-readable key string for the dialog: `keys` if set, else the accels
  // formatted and joined with " / ".
  std::string display_keys() const;
};

// One GTK accelerator string → human display.  "<Ctrl><Shift>n" -> "Ctrl+Shift+N".
// Modifiers are emitted in canonical order (Ctrl, Alt, Shift, Super); named keys
// (comma, question, Left, space, minus, …) map to their glyphs where sensible.
std::string format_accel(const std::string& accel);

// The registry — authored in tab → section (A-Z) → row order so a consumer can
// walk it linearly and start a new heading whenever the section changes.
const std::vector<ShortcutSpec>& shortcut_registry();

// Collision detector. An accel collides when it is claimed by two or more Main
// specs and at least one of them is a wired GAction (a GAction accelerator fires
// regardless of focus, so any other Main behaviour on the same chord shadows or
// is shadowed by it — the Ctrl+Shift+T / batch-snapshot class of bug). Returns the
// offending accel strings (sorted, unique); empty ⇒ clean.
std::vector<std::string> find_accel_collisions();

}  // namespace Folio
