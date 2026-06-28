#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — BarcodeGenerator.hpp
//
// Generates EAN-13 barcodes as SVG strings using the UpcEan72/UpcEan36
// and OcrBUpcEan fonts, embedded in the binary as GResource (like the icons).
//
// Glyph outlines are read directly via FreeType (FT_New_Memory_Face on the
// embedded TTF bytes) and converted to SVG <path> elements, so the SVG itself
// carries no font dependency — pure vector geometry.
//
// Encoding ported from original Qt EAN13 implementation by Scott Combs (2008).
//
// SVG dimensions equal the tight bounding box of all art + 9pt padding,
// computed via a two-pass render (oversized → measure ink extents → crop).
// ─────────────────────────────────────────────────────────────────────────────
#include <string>
#include <librsvg/rsvg.h>

namespace Folio {

struct BarcodeOptions {
    bool full_height     = true;   // true = UpcEan72 (full height), false = UpcEan36 (half)
    bool show_whitespace = false;  // append '>' quiet-zone marker after last element
    // Triangle / returnability indicator:
    //   0 = none
    //   1 = triangle outline only  (non-returnable)
    //   2 = triangle + S inside    (strippable — cover removable for reimbursement)
    int  triangle_state  = 0;
    bool include_price   = false;  // append EAN-5 price supplement
    std::string currency = "5";    // EAN-5 currency code: 5=US 6=CA 4=NZ 3=AU 0=GBP 9=NACS
    std::string price    = "";     // 4 digits e.g. "2395" (= $23.95)
};

class BarcodeGenerator {
public:
    // ── Validation ────────────────────────────────────────────────────────────
    static bool        is_valid_ean13(const std::string& str);
    static bool        is_valid_isbn10(const std::string& str);
    static std::string normalise(const std::string& str);
    static std::string isbn10_to_ean13(const std::string& isbn10_digits);

    // ── SVG generation ────────────────────────────────────────────────────────
    // Generate SVG for a validated 13-digit EAN string.
    // Returns empty string on failure (bad input or fonts unavailable).
    static std::string generate_svg(const std::string& ean13_digits,
                                    const BarcodeOptions& opts = {});

    // Convenience: accepts ISBN-10 or ISBN-13 (with or without hyphens).
    static std::string generate_svg_from_isbn(const std::string& isbn,
                                              const BarcodeOptions& opts = {});

    // ── Encoding helpers ──────────────────────────────────────────────────────
    static std::string map_to_font(const std::string& ean13);
    static std::string ean5_string(const std::string& price5_digits);
    static char        ean13_check_digit(const std::string& twelve_digits);

public:
    static const char* s_structure[10];
    static const char* s_estructure[10];
};

} // namespace Folio
