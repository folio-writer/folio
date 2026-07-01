#include "Shortcuts.hpp"

#include <cctype>
#include <map>

namespace Folio {

// ─────────────────────────────────────────────────────────────────────────────
// Accel formatting
// ─────────────────────────────────────────────────────────────────────────────
namespace {

std::string lower(const std::string& s) {
  std::string o;
  o.reserve(s.size());
  for (char c : s) o += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return o;
}

// GTK key name → display glyph. Single alpha keys uppercase; named keys map to a
// glyph where one reads better, otherwise the name is prettified (underscores →
// spaces). Covers the keys the registry actually uses; falls through cleanly.
std::string map_key(const std::string& key) {
  if (key.empty()) return "";
  static const std::map<std::string, std::string> named = {
      {"comma", ","},   {"period", "."},   {"question", "?"},
      {"slash", "/"},   {"backslash", "\\"}, {"minus", "\u2212"},
      {"plus", "+"},    {"equal", "="},    {"space", "Space"},
      {"Return", "Enter"}, {"KP_Enter", "Enter"}, {"Escape", "Esc"},
      {"bracketleft", "["}, {"bracketright", "]"}, {"grave", "`"},
      {"semicolon", ";"}, {"apostrophe", "'"},
  };
  auto it = named.find(key);
  if (it != named.end()) return it->second;
  if (key.size() == 1) {
    std::string s = key;
    s[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    return s;
  }
  std::string s = key;
  for (char& c : s)
    if (c == '_') c = ' ';
  return s;
}

}  // namespace

std::string format_accel(const std::string& accel) {
  bool ctrl = false, alt = false, shift = false, super = false;
  std::string key;
  std::size_t i = 0;
  while (i < accel.size()) {
    if (accel[i] == '<') {
      std::size_t j = accel.find('>', i);
      if (j == std::string::npos) break;
      const std::string ml = lower(accel.substr(i + 1, j - i - 1));
      if (ml == "ctrl" || ml == "control" || ml == "primary") ctrl = true;
      else if (ml == "alt" || ml == "mod1")                    alt = true;
      else if (ml == "shift")                                  shift = true;
      else if (ml == "super" || ml == "meta" || ml == "mod4")  super = true;
      i = j + 1;
    } else {
      key = accel.substr(i);
      break;
    }
  }
  const std::string k = map_key(key);
  std::string out;
  auto add = [&](const std::string& s) {
    if (!out.empty()) out += "+";
    out += s;
  };
  if (ctrl)  add("Ctrl");
  if (alt)   add("Alt");
  if (shift) add("Shift");
  if (super) add("Super");
  if (!k.empty()) add(k);
  return out;
}

std::string ShortcutSpec::display_keys() const {
  if (!keys.empty()) return keys;
  std::string out;
  for (const auto& a : accels) {
    if (!out.empty()) out += "  /  ";
    out += format_accel(a);
  }
  return out;
}

// ─────────────────────────────────────────────────────────────────────────────
// The registry  (authored tab → section [A-Z] → row)
// ─────────────────────────────────────────────────────────────────────────────
const std::vector<ShortcutSpec>& shortcut_registry() {
  using T = ShortcutTab;
  using C = ShortcutContext;
  // Fields: {tab, context, section, action, accels, keys, description}
  // action non-empty ⇒ wired via set_accels_for_action. keys empty ⇒ derived.
  static const std::vector<ShortcutSpec> kReg = {
      // ── Keyboard · Binder ──────────────────────────────────────────────
      {T::Keyboard, C::Main, "Binder", "win.add-scene", {"<Ctrl><Alt>s"}, "", "Add scene"},
      {T::Keyboard, C::Main, "Binder", "win.add-character", {"<Ctrl><Alt>c"}, "", "Add character"},
      {T::Keyboard, C::Main, "Binder", "win.add-place", {"<Ctrl><Alt>p"}, "", "Add place"},
      {T::Keyboard, C::Main, "Binder", "win.add-reference", {"<Ctrl><Alt>r"}, "", "Add reference"},
      {T::Keyboard, C::Main, "Binder", "win.add-template", {"<Ctrl><Alt>t"}, "", "Add template"},
      {T::Keyboard, C::Main, "Binder", "win.add-group-manuscript", {"<Ctrl><Alt><Shift>s"}, "", "Add group (Manuscript)"},
      {T::Keyboard, C::Main, "Binder", "win.add-group-characters", {"<Ctrl><Alt><Shift>c"}, "", "Add group (Characters)"},
      {T::Keyboard, C::Main, "Binder", "win.add-group-places", {"<Ctrl><Alt><Shift>p"}, "", "Add group (Places)"},
      {T::Keyboard, C::Main, "Binder", "", {}, "\u2191 / \u2193", "Move selection up / down"},
      {T::Keyboard, C::Main, "Binder", "", {}, "Shift+\u2192", "Enter / expand group"},
      {T::Keyboard, C::Main, "Binder", "", {}, "Shift+\u2190", "Exit / collapse group"},

      // ── Keyboard · Editing ─────────────────────────────────────────────
      {T::Keyboard, C::Main, "Editing", "", {"<Ctrl>b"}, "", "Bold"},
      {T::Keyboard, C::Main, "Editing", "", {"<Ctrl>i"}, "", "Italic"},
      {T::Keyboard, C::Main, "Editing", "", {"<Ctrl>u"}, "", "Underline"},
      {T::Keyboard, C::Main, "Editing", "", {"<Ctrl><Shift>h"}, "", "Format screenplay reference"},

      // ── Keyboard · File ────────────────────────────────────────────────
      {T::Keyboard, C::Main, "File", "win.new", {"<Ctrl>n"}, "", "New project"},
      {T::Keyboard, C::Main, "File", "win.open", {"<Ctrl>o"}, "", "Open\u2026"},
      {T::Keyboard, C::Main, "File", "win.open-sample", {"<Ctrl><Shift>d"}, "", "Open sample project"},
      {T::Keyboard, C::Main, "File", "win.save", {"<Ctrl>s"}, "", "Save"},
      {T::Keyboard, C::Main, "File", "win.save-as", {"<Ctrl><Shift>s"}, "", "Save as\u2026"},
      {T::Keyboard, C::Main, "File", "win.export", {"<Ctrl>e"}, "", "Export\u2026"},
      {T::Keyboard, C::Main, "File", "win.print", {"<Ctrl>p"}, "", "Print\u2026"},
      {T::Keyboard, C::Main, "File", "win.close-project", {"<Ctrl>w"}, "", "Close project"},
      {T::Keyboard, C::Main, "File", "win.quit", {"<Ctrl>q"}, "", "Quit"},

      // ── Keyboard · Focus Mode (separate window) ────────────────────────
      {T::Keyboard, C::Focus, "Focus Mode  (in the focus window)", "", {"<Ctrl>slash"}, "", "Styles & formatting bar"},
      {T::Keyboard, C::Focus, "Focus Mode  (in the focus window)", "", {}, "Ctrl+B / I / U", "Bold / italic / underline selection"},
      {T::Keyboard, C::Focus, "Focus Mode  (in the focus window)", "", {"<Ctrl>p"}, "", "Jump to scene\u2026"},
      {T::Keyboard, C::Focus, "Focus Mode  (in the focus window)", "", {"<Ctrl>k"}, "", "Insert / edit link"},
      {T::Keyboard, C::Focus, "Focus Mode  (in the focus window)", "", {}, "Ctrl+] / Ctrl+[", "Next / previous scene"},
      {T::Keyboard, C::Focus, "Focus Mode  (in the focus window)", "", {"<Ctrl>comma"}, "", "Focus settings"},
      {T::Keyboard, C::Focus, "Focus Mode  (in the focus window)", "", {}, "Ctrl+? / F1", "Focus shortcut list"},
      {T::Keyboard, C::Focus, "Focus Mode  (in the focus window)", "", {}, "Esc", "Step back / leave focus"},

      // ── Keyboard · Inspector ───────────────────────────────────────────
      {T::Keyboard, C::Main, "Inspector", "win.inspector-project", {"<Alt>p"}, "", "Project tab"},
      {T::Keyboard, C::Main, "Inspector", "win.inspector-metadata", {"<Alt>m"}, "", "Metadata tab"},
      {T::Keyboard, C::Main, "Inspector", "win.inspector-notes", {"<Alt>n"}, "", "Notes tab"},
      {T::Keyboard, C::Main, "Inspector", "win.inspector-snapshots", {"<Alt>s"}, "", "Snapshots tab"},

      // ── Keyboard · Special Characters (editor) ─────────────────────────
      {T::Keyboard, C::Main, "Special Characters  (in the editor)", "", {"<Ctrl>space"}, "", "Word joiner (zero-width no-break)"},
      {T::Keyboard, C::Main, "Special Characters  (in the editor)", "", {"<Ctrl><Shift>space"}, "", "Non-breaking space"},
      {T::Keyboard, C::Main, "Special Characters  (in the editor)", "", {"<Ctrl><Alt>space"}, "", "Thin space"},
      {T::Keyboard, C::Main, "Special Characters  (in the editor)", "", {"<Ctrl><Alt><Shift>space"}, "", "Zero-width space"},
      {T::Keyboard, C::Main, "Special Characters  (in the editor)", "", {"<Ctrl>minus"}, "", "Soft hyphen"},
      {T::Keyboard, C::Main, "Special Characters  (in the editor)", "", {"<Ctrl><Shift>minus"}, "", "Non-breaking hyphen"},

      // ── Keyboard · Timeline ────────────────────────────────────────────
      {T::Keyboard, C::Main, "Timeline  (canvas focused)", "", {}, "T", "Toggle Told \u2194 Chrono lens"},
      {T::Keyboard, C::Main, "Timeline  (canvas focused)", "", {}, "+ / \u2212", "Zoom in / out"},
      {T::Keyboard, C::Main, "Timeline  (canvas focused)", "", {}, "0", "Reset zoom"},
      {T::Keyboard, C::Main, "Timeline  (canvas focused)", "win.timeline-prev", {"<Alt>Left", "<Ctrl><Shift>Tab"}, "", "Previous timeline / tab"},
      {T::Keyboard, C::Main, "Timeline  (canvas focused)", "win.timeline-next", {"<Alt>Right", "<Ctrl>Tab"}, "", "Next timeline / tab"},

      // ── Keyboard · Tools ───────────────────────────────────────────────
      {T::Keyboard, C::Main, "Tools", "win.search", {"<Ctrl><Shift>g"}, "", "Search\u2026"},
      {T::Keyboard, C::Main, "Tools", "win.pomodoro", {"<Ctrl><Shift>p"}, "", "Pomodoro timer"},
      {T::Keyboard, C::Main, "Tools", "win.batch-snapshot", {"<Ctrl><Shift>t"}, "", "Batch snapshot"},
      {T::Keyboard, C::Main, "Tools", "win.preferences", {"<Ctrl>comma"}, "", "Preferences\u2026"},
      {T::Keyboard, C::Main, "Tools", "win.shortcuts", {"<Ctrl>question"}, "", "Keyboard shortcuts (this window)"},

      // ── Keyboard · View ────────────────────────────────────────────────
      {T::Keyboard, C::Main, "View", "win.focus-mode", {"<Ctrl><Shift>f"}, "", "Focus mode"},
      {T::Keyboard, C::Main, "View", "win.toggle-binder", {"<Ctrl><Shift>b"}, "", "Toggle binder"},
      {T::Keyboard, C::Main, "View", "win.toggle-inspector", {"<Ctrl><Shift>i"}, "", "Toggle inspector"},
      {T::Keyboard, C::Main, "View", "win.close-all-tabs", {"<Ctrl><Shift>w"}, "", "Close all tabs"},

      // ── Mouse · Binder ─────────────────────────────────────────────────
      {T::Mouse, C::Main, "Binder", "", {}, "Drag row", "Reorder / move into or out of a group"},
      {T::Mouse, C::Main, "Binder", "", {}, "Ctrl+Alt+click category", "Expand / collapse all categories"},

      // ── Mouse · Editor ─────────────────────────────────────────────────
      {T::Mouse, C::Main, "Editor", "", {}, "Alt+click", "Follow link / edit annotation under the cursor"},
      {T::Mouse, C::Main, "Editor", "", {}, "Drag", "Select text"},

      // ── Mouse · Timeline ───────────────────────────────────────────────
      {T::Mouse, C::Main, "Timeline", "", {}, "Ctrl+click", "Zoom in (about the click)"},
      {T::Mouse, C::Main, "Timeline", "", {}, "Ctrl+Shift+click", "Zoom out (about the click)"},
      {T::Mouse, C::Main, "Timeline", "", {}, "Ctrl+scroll", "Zoom in / out"},
      {T::Mouse, C::Main, "Timeline", "", {}, "Alt+click cell", "Toggle a relationship cell"},
      {T::Mouse, C::Main, "Timeline", "", {}, "Drag across cells", "Sweep to add / remove a span"},
      {T::Mouse, C::Main, "Timeline", "", {}, "Ctrl+Alt+click rail", "Expand / collapse all rail rows"},
      {T::Mouse, C::Main, "Timeline", "", {}, "Drag card", "Reorder / reschedule a scene"},
      {T::Mouse, C::Main, "Timeline", "", {}, "Click a seam", "Open the gap bar (drag to open room)"},
  };
  return kReg;
}

// ─────────────────────────────────────────────────────────────────────────────
// Collision detection
// ─────────────────────────────────────────────────────────────────────────────
std::vector<std::string> find_accel_collisions() {
  std::map<std::string, std::pair<int, bool>> seen;  // accel → {count, any_action}
  for (const auto& s : shortcut_registry()) {
    if (s.context != ShortcutContext::Main) continue;
    for (const auto& a : s.accels) {
      auto& e = seen[a];
      e.first += 1;
      if (!s.action.empty()) e.second = true;
    }
  }
  std::vector<std::string> out;
  for (const auto& [accel, e] : seen)
    if (e.first >= 2 && e.second) out.push_back(accel);  // std::map keeps it sorted+unique
  return out;
}

}  // namespace Folio
