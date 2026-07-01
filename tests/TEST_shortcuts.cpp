// TEST_shortcuts — pure-layer test for the shortcut registry.
// Verifies format_accel() rendering and, crucially, that the registry is
// collision-free (no chord claimed by a wired GAction and another Main
// behaviour) — the automated version of the s98 hand audit.
//
// Build + run:
/*
g++ -std=c++20 -Wall -Wextra -Wconversion -Wshadow -I ../include TEST_shortcuts.cpp ../src/Shortcuts.cpp -o /tmp/test_shortcuts && /tmp/test_shortcuts
clang++ -std=c++20 -Wall -Wextra -Werror -Wconversion -Wshadow -I ../include TEST_shortcuts.cpp ../src/Shortcuts.cpp -o /tmp/test_shortcuts && /tmp/test_shortcuts
*/

#include "Shortcuts.hpp"

#include <cstdio>
#include <string>

static int g_pass = 0;
static int g_fail = 0;

static void check(const std::string& got, const std::string& want,
                  const char* label) {
  if (got == want) {
    ++g_pass;
  } else {
    ++g_fail;
    std::printf("  FAIL %-28s got \"%s\"  want \"%s\"\n", label, got.c_str(),
                want.c_str());
  }
}

int main() {
  using namespace Folio;

  // ── format_accel ─────────────────────────────────────────────────────
  check(format_accel("<Ctrl>n"), "Ctrl+N", "ctrl+n");
  check(format_accel("<Ctrl><Shift>s"), "Ctrl+Shift+S", "ctrl+shift+s");
  check(format_accel("<Ctrl><Alt>s"), "Ctrl+Alt+S", "ctrl+alt+s");
  check(format_accel("<Ctrl><Alt><Shift>p"), "Ctrl+Alt+Shift+P", "ctrl+alt+shift+p");
  check(format_accel("<Ctrl>comma"), "Ctrl+,", "ctrl+comma");
  check(format_accel("<Ctrl>question"), "Ctrl+?", "ctrl+question");
  check(format_accel("<Ctrl>space"), "Ctrl+Space", "ctrl+space");
  check(format_accel("<Ctrl><Alt>space"), "Ctrl+Alt+Space", "ctrl+alt+space");
  check(format_accel("<Ctrl>minus"), "Ctrl+\u2212", "ctrl+minus");
  check(format_accel("<Alt>Left"), "Alt+Left", "alt+left");
  check(format_accel("<Ctrl><Shift>Tab"), "Ctrl+Shift+Tab", "ctrl+shift+tab");
  check(format_accel("<Primary>o"), "Ctrl+O", "primary→ctrl");

  // ── display_keys (explicit overrides derivation; multi-accel joins) ───
  {
    ShortcutSpec a{ShortcutTab::Keyboard, ShortcutContext::Main, "x", "act",
                   {"<Alt>Left", "<Ctrl><Shift>Tab"}, "", "d"};
    check(a.display_keys(), "Alt+Left  /  Ctrl+Shift+Tab", "multi-accel join");
    ShortcutSpec b{ShortcutTab::Keyboard, ShortcutContext::Main, "x", "",
                   {}, "Esc", "d"};
    check(b.display_keys(), "Esc", "explicit keys");
  }

  // ── registry is collision-free ───────────────────────────────────────
  {
    auto cols = find_accel_collisions();
    if (cols.empty()) {
      ++g_pass;
    } else {
      ++g_fail;
      std::printf("  FAIL registry has %zu colliding accel(s):\n", cols.size());
      for (const auto& c : cols) std::printf("       %s\n", c.c_str());
    }
  }

  // ── every wired action carries at least one accel ─────────────────────
  {
    bool ok = true;
    for (const auto& s : shortcut_registry())
      if (!s.action.empty() && s.accels.empty()) {
        ok = false;
        std::printf("  FAIL action %s has no accel\n", s.action.c_str());
      }
    if (ok) ++g_pass; else ++g_fail;
  }

  // ── every spec renders a non-empty key string and description ─────────
  {
    bool ok = true;
    for (const auto& s : shortcut_registry()) {
      if (s.display_keys().empty()) {
        ok = false;
        std::printf("  FAIL empty display_keys for \"%s\"\n", s.description.c_str());
      }
      if (s.description.empty()) {
        ok = false;
        std::printf("  FAIL empty description in section \"%s\"\n", s.section.c_str());
      }
    }
    if (ok) ++g_pass; else ++g_fail;
  }

  std::printf("shortcuts: %d passed, %d failed\n", g_pass, g_fail);
  return g_fail == 0 ? 0 : 1;
}
