// ─────────────────────────────────────────────────────────────────────────────
// Folio — RulerUnits.hpp
// Unit conversion utilities for the editor ruler.
//
// GTK4 / CSS screen pixels are defined as 1px = 1/96 inch (device-independent).
// Pango points are 1pt = 1/72 inch.
// All conversions use this 96 px/inch anchor.
// ─────────────────────────────────────────────────────────────────────────────

#pragma once
#include <string>
#include <cmath>

namespace Folio {

// ─────────────────────────────────────────────────────────────────────────────
// Unit enum
// ─────────────────────────────────────────────────────────────────────────────

enum class RulerUnit {
    Mm,     // millimetres
    Cm,     // centimetres
    Inch,   // inches
    Pt,     // typographic points (1/72 inch)
    Pc,     // picas (12 points = 1/6 inch)
};

// ─────────────────────────────────────────────────────────────────────────────
// RulerUnits — static conversion helpers
// ─────────────────────────────────────────────────────────────────────────────

struct RulerUnits {
    // Parse a unit string from prefs: "mm" "cm" "inch" "pt" "pc"
    static RulerUnit from_string(const std::string& s) {
        if (s == "mm")   return RulerUnit::Mm;
        if (s == "cm")   return RulerUnit::Cm;
        if (s == "inch") return RulerUnit::Inch;
        if (s == "pt")   return RulerUnit::Pt;
        if (s == "pc")   return RulerUnit::Pc;
        return RulerUnit::Cm; // default
    }

    static std::string to_string(RulerUnit u) {
        switch (u) {
        case RulerUnit::Mm:   return "mm";
        case RulerUnit::Cm:   return "cm";
        case RulerUnit::Inch: return "inch";
        case RulerUnit::Pt:   return "pt";
        case RulerUnit::Pc:   return "pc";
        }
        return "cm";
    }

    static std::string display_label(RulerUnit u) {
        switch (u) {
        case RulerUnit::Mm:   return "mm";
        case RulerUnit::Cm:   return "cm";
        case RulerUnit::Inch: return "in";
        case RulerUnit::Pt:   return "pt";
        case RulerUnit::Pc:   return "pc";
        }
        return "cm";
    }

    // ── Conversion constants (all anchored at 96 CSS px = 1 inch) ─────────────
    static constexpr double PX_PER_INCH = 96.0;
    static constexpr double PX_PER_MM   = PX_PER_INCH / 25.4;
    static constexpr double PX_PER_CM   = PX_PER_INCH / 2.54;
    static constexpr double PX_PER_PT   = PX_PER_INCH / 72.0;
    static constexpr double PX_PER_PC   = PX_PER_INCH / 6.0;  // 1 pica = 1/6 inch

    // Convert CSS pixels → chosen unit
    static double px_to_unit(double px, RulerUnit u) {
        switch (u) {
        case RulerUnit::Mm:   return px / PX_PER_MM;
        case RulerUnit::Cm:   return px / PX_PER_CM;
        case RulerUnit::Inch: return px / PX_PER_INCH;
        case RulerUnit::Pt:   return px / PX_PER_PT;
        case RulerUnit::Pc:   return px / PX_PER_PC;
        }
        return px / PX_PER_CM;
    }

    // Convert chosen unit → CSS pixels
    static double unit_to_px(double val, RulerUnit u) {
        switch (u) {
        case RulerUnit::Mm:   return val * PX_PER_MM;
        case RulerUnit::Cm:   return val * PX_PER_CM;
        case RulerUnit::Inch: return val * PX_PER_INCH;
        case RulerUnit::Pt:   return val * PX_PER_PT;
        case RulerUnit::Pc:   return val * PX_PER_PC;
        }
        return val * PX_PER_CM;
    }

    // Convert points → CSS pixels (used for tab stop storage)
    static double pt_to_px(double pt) { return pt * PX_PER_PT; }
    static double px_to_pt(double px) { return px / PX_PER_PT; }

    // Appropriate major tick interval in the chosen unit
    // Returns the interval value in that unit
    static double major_tick_interval(RulerUnit u) {
        switch (u) {
        case RulerUnit::Mm:   return 10.0;  // every 10mm
        case RulerUnit::Cm:   return 1.0;   // every 1cm
        case RulerUnit::Inch: return 1.0;   // every inch
        case RulerUnit::Pt:   return 36.0;  // every 36pt (half inch)
        case RulerUnit::Pc:   return 1.0;   // every pica
        }
        return 1.0;
    }

    // Minor tick subdivisions per major tick
    static int minor_subdivisions(RulerUnit u) {
        switch (u) {
        case RulerUnit::Mm:   return 10; // 1mm minor ticks
        case RulerUnit::Cm:   return 10; // 1mm minor ticks
        case RulerUnit::Inch: return 8;  // 1/8 inch minor ticks
        case RulerUnit::Pt:   return 6;  // 6pt minor ticks
        case RulerUnit::Pc:   return 12; // 1pt minor ticks
        }
        return 10;
    }

    // Format a unit value for display on the ruler tick label
    static std::string format_label(double val, RulerUnit u) {
        char buf[32];
        switch (u) {
        case RulerUnit::Mm:
            std::snprintf(buf, sizeof(buf), "%.0f", val);
            break;
        case RulerUnit::Cm:
            std::snprintf(buf, sizeof(buf), "%.0f", val);
            break;
        case RulerUnit::Inch:
            // Show as fraction or decimal
            std::snprintf(buf, sizeof(buf), "%.1f", val);
            break;
        case RulerUnit::Pt:
            std::snprintf(buf, sizeof(buf), "%.0f", val);
            break;
        case RulerUnit::Pc:
            std::snprintf(buf, sizeof(buf), "%.0f", val);
            break;
        }
        return buf;
    }
};

} // namespace Folio
