#pragma once
//
// color_utils.hpp — shared hex/RGBA colour helpers.
//
// Created in s14 to consolidate the "#rrggbb" parsing that had been
// reimplemented independently in Editor_text, MainWindow, PomodoroDialog,
// Sidebar, SpellCheckHighlighter, and the serializer in PreferencesDialog.
// All of those used the same logic (optional leading '#', >= 6 hex chars,
// first three byte pairs / 255) and differed only in return type and the
// fallback colour used on malformed input — so every entry point here takes
// an explicit fallback, preserving each call site's original behaviour.
//
// Header-only (all inline); no TU, nothing to add to CMake. Include where
// needed: #include <color_utils.hpp>

#include <gdkmm/rgba.h>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <tuple>

namespace Folio {
namespace color {

// Core parse: "#rrggbb" (or bare "rrggbb") -> (r, g, b) as 0..1 doubles.
// On malformed input (fewer than 6 hex chars, or a parse error) returns the
// supplied default components {dr, dg, db}. Double precision so callers that
// feed cairo directly keep their original values exactly.
inline std::tuple<double, double, double>
hex_to_rgb01(const std::string &hex, double dr, double dg, double db) {
  std::string h = hex;
  if (!h.empty() && h[0] == '#')
    h = h.substr(1);
  if (h.size() < 6)
    return {dr, dg, db};
  try {
    double r = std::stoul(h.substr(0, 2), nullptr, 16) / 255.0;
    double g = std::stoul(h.substr(2, 2), nullptr, 16) / 255.0;
    double b = std::stoul(h.substr(4, 2), nullptr, 16) / 255.0;
    return {r, g, b};
  } catch (...) {
    return {dr, dg, db};
  }
}

// Convenience constructor for an RGBA literal (handy for fallbacks).
inline Gdk::RGBA rgba(double r, double g, double b, double a = 1.0) {
  Gdk::RGBA c;
  c.set_rgba(r, g, b, a);
  return c;
}

// "#rrggbb" -> opaque Gdk::RGBA. Malformed input -> `fallback`.
inline Gdk::RGBA from_hex(const std::string &hex, const Gdk::RGBA &fallback) {
  auto [r, g, b] =
      hex_to_rgb01(hex, fallback.get_red(), fallback.get_green(),
                   fallback.get_blue());
  Gdk::RGBA c;
  c.set_rgba(r, g, b, 1.0);
  return c;
}

// Opaque RGBA -> "#rrggbb".
inline std::string to_hex(const Gdk::RGBA &c) {
  std::ostringstream ss;
  ss << "#" << std::hex << std::setfill('0') << std::setw(2)
     << (int)std::round(c.get_red() * 255) << std::setw(2)
     << (int)std::round(c.get_green() * 255) << std::setw(2)
     << (int)std::round(c.get_blue() * 255);
  return ss.str();
}

// "#rrggbb" + alpha -> CSS "rgba(r,g,b,a)" with r/g/b as 0..255 integers.
// Malformed hex -> the CSS form of `fallback` at the given alpha.
inline std::string to_css_rgba(const std::string &hex, double alpha,
                               const Gdk::RGBA &fallback) {
  auto [r, g, b] =
      hex_to_rgb01(hex, fallback.get_red(), fallback.get_green(),
                   fallback.get_blue());
  return "rgba(" + std::to_string((int)std::lround(r * 255)) + "," +
         std::to_string((int)std::lround(g * 255)) + "," +
         std::to_string((int)std::lround(b * 255)) + "," +
         std::to_string(alpha) + ")";
}

}  // namespace color
}  // namespace Folio
