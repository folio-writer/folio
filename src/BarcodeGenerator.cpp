// ─────────────────────────────────────────────────────────────────────────────
// Folio — BarcodeGenerator.cpp
//
// EAN-13 SVG generator using FreeType to load glyph outlines directly from
// TTF files, bypassing the system font stack entirely.
//
// Font files are read from ~/.config/folio/fonts/:
//   UpcEan72.ttf   — full-height barcode font (bars + embedded digits)
//   UpcEan36.ttf   — half-height barcode font (bars + embedded digits)
//   OcrBUpcEan.ttf — OCR-B for ISBN label and EAN rotated label
//
// Each glyph outline is extracted via FT_Load_Glyph with FT_LOAD_NO_SCALE,
// giving raw font-unit bezier curves. We apply scale = font_pt/upm and
// Y-flip to place glyphs at the correct position in SVG coordinate space.
// Advance widths from FT_GlyphSlot->advance.x / 64.0 * scale.
//
// The recording surface captures all drawn ink for exact bounding box
// measurement via cairo_recording_surface_ink_extents() — pure vector math.
//
// SVG viewBox = ink_extents + 9pt padding. Zero font dependency in output.
//
// Encoding ported from original Qt implementation by Scott Combs (2008).
// ─────────────────────────────────────────────────────────────────────────────
#include "BarcodeGenerator.hpp"
#include "FolioLog.hpp"
#include <cairo/cairo.h>
#include <glibmm.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include <sstream>
#include <iomanip>
#include <cctype>
#include <cmath>
#include <algorithm>
#include <map>

namespace Folio {

// ─────────────────────────────────────────────────────────────────────────────
// Encoding tables
// ─────────────────────────────────────────────────────────────────────────────
const char* BarcodeGenerator::s_structure[10] = {
    "LLLLLL","LLGLGG","LLGGLG","LLGGGL","LGLLGG",
    "LGGLLG","LGGGLL","LGLGLG","LGLGGL","LGGLGL",
};
const char* BarcodeGenerator::s_estructure[10] = {
    "GGLLL","GLGLL","GLLGL","GLLLG","LGGLL",
    "LLGGL","LLLGG","LGLGL","LGLLG","LLGLG",
};

// ─────────────────────────────────────────────────────────────────────────────
// Validation
// ─────────────────────────────────────────────────────────────────────────────
std::string BarcodeGenerator::normalise(const std::string& str) {
    std::string out;
    for (char c : str) {
        if (std::isdigit((unsigned char)c)) out += c;
        else if (c == '-' || c == ' ')     continue;
        else                               return "";
    }
    return out;
}

bool BarcodeGenerator::is_valid_ean13(const std::string& str) {
    std::string d = normalise(str);
    if (d.size() != 13) return false;
    int sum = 0;
    for (int i = 0; i < 12; ++i)
        sum += (d[i]-'0') * (i%2==0 ? 1 : 3);
    return (d[12]-'0') == (10-(sum%10))%10;
}

bool BarcodeGenerator::is_valid_isbn10(const std::string& str) {
    std::string d = normalise(str);
    if (d.size() != 10) return false;
    int sum = 0;
    for (int i = 0; i < 9; ++i) sum += (d[i]-'0') * (10-i);
    int check = (11-(sum%11))%11;
    return check==10 ? (d[9]=='X'||d[9]=='x') : (d[9]-'0')==check;
}

char BarcodeGenerator::ean13_check_digit(const std::string& twelve) {
    if (twelve.size() < 12) return '0';
    int sum = 0;
    for (int i = 0; i < 12; ++i)
        sum += (twelve[i]-'0') * (i%2==0 ? 1 : 3);
    return (char)('0' + (10-(sum%10))%10);
}

std::string BarcodeGenerator::isbn10_to_ean13(const std::string& isbn10_digits) {
    if (isbn10_digits.size() < 9) return "";
    std::string twelve = "978" + isbn10_digits.substr(0,9);
    return twelve + ean13_check_digit(twelve);
}

std::string BarcodeGenerator::map_to_font(const std::string& isbn) {
    if (isbn.size() != 13) return "";
    int ns = isbn[0] - '0';
    if (ns < 0 || ns > 9) return "";
    std::string rtn;
    rtn += (char)(isbn[0] + 61);
    rtn += (char)33;
    const char* mapstr = s_structure[ns];
    for (int i = 1; i <= 12; ++i) {
        if (i == 7) rtn += (char)34;
        int d = isbn[i] - '0';
        char enc = mapstr[i-1];
        if      (enc=='L') rtn += (char)(d+57);
        else if (enc=='G') rtn += (char)(d+67);
        else               rtn += (char)(d+77);
    }
    rtn += (char)33;
    rtn += (char)108;
    return rtn;
}

std::string BarcodeGenerator::ean5_string(const std::string& price5) {
    if (price5.size() != 5) return "";
    static const int w[5] = {3,9,3,9,3};
    int sum = 0;
    for (int i = 0; i < 5; ++i) sum += (price5[i]-'0') * w[i];
    const char* mapstr = s_estructure[sum%10];
    std::string rtn;
    rtn += (char)35;
    for (int i = 0; i < 5; ++i) {
        int d = price5[i] - '0';
        rtn += (mapstr[i]=='L') ? (char)(d+37) : (char)(d+47);
        if (i < 4) rtn += (char)36;
    }
    return rtn;
}

// ─────────────────────────────────────────────────────────────────────────────
// FreeType font wrapper
// Loads a TTF file directly, caches glyph outlines as SVG path strings.
// ─────────────────────────────────────────────────────────────────────────────
struct FTFont {
    FT_Library lib  = nullptr;
    FT_Face    face = nullptr;
    double     upm  = 1000.0;
    bool       ok   = false;

    explicit FTFont(const std::string& path) {
        if (FT_Init_FreeType(&lib)) return;
        if (FT_New_Face(lib, path.c_str(), 0, &face)) return;
        upm = face->units_per_EM;
        ok = true;
    }

    ~FTFont() {
        if (face) FT_Done_Face(face);
        if (lib)  FT_Done_FreeType(lib);
    }

    // Get glyph index for a unicode codepoint
    FT_UInt glyph_index(unsigned int codepoint) const {
        return FT_Get_Char_Index(face, codepoint);
    }

    // Load glyph and return SVG path "d" string + advance in font units.
    // FT_LOAD_NO_SCALE: outline in raw font units, no hinting.
    bool glyph_path(FT_UInt gi, std::string& d_out, double& advance_out) const {
        if (FT_Load_Glyph(face, gi, FT_LOAD_NO_SCALE | FT_LOAD_NO_BITMAP))
            return false;
        advance_out = (double)face->glyph->advance.x;
        if (face->glyph->outline.n_points == 0) {
            d_out = "";
            return true; // empty glyph (space etc) — advance still valid
        }

        // Convert FT_Outline to SVG path
        // FreeType Y is UP (font coords), SVG Y is DOWN.
        // We emit raw font-unit coordinates — caller applies scale + Y-flip
        // via transform="translate(tx,ty) scale(s,-s)"
        std::ostringstream d;
        d << std::fixed << std::setprecision(3);

        int pt_idx = 0;
        for (int c = 0; c < face->glyph->outline.n_contours; ++c) {
            int end = face->glyph->outline.contours[c];
            int start = pt_idx;
            bool first = true;

            // FreeType contours may have on/off-curve points (TrueType quadratic)
            // We need to handle both quadratic (TrueType) and cubic (CFF) splines.
            auto pt = [&](int i) -> std::pair<double,double> {
                return {(double)face->glyph->outline.points[i].x,
                        (double)face->glyph->outline.points[i].y};
            };
            auto tag = [&](int i) -> int {
                return face->glyph->outline.tags[i] & 0x03;
            };

            for (int i = start; i <= end; ) {
                auto [x0, y0] = pt(i);
                int t = tag(i);

                if (first) {
                    d << "M" << x0 << "," << y0 << " ";
                    first = false;
                    ++i;
                    continue;
                }

                if (t == FT_CURVE_TAG_ON) {
                    // Line to
                    d << "L" << x0 << "," << y0 << " ";
                    ++i;
                } else if (t == FT_CURVE_TAG_CUBIC) {
                    // Cubic bezier — need 2 control points + on-curve
                    if (i + 2 <= end) {
                        auto [x1,y1] = pt(i+1);
                        auto [x2,y2] = pt(i+2);
                        d << "C" << x0<<","<<y0<<" "<<x1<<","<<y1<<" "<<x2<<","<<y2<<" ";
                        i += 3;
                    } else { ++i; }
                } else {
                    // Quadratic (TrueType conic) — FT_CURVE_TAG_CONIC
                    // Convert to cubic or use Q command
                    if (i + 1 <= end) {
                        auto [x1,y1] = pt(i+1);
                        int t1 = tag(i+1);
                        if (t1 == FT_CURVE_TAG_ON) {
                            d << "Q" << x0<<","<<y0<<" "<<x1<<","<<y1<<" ";
                            i += 2;
                        } else {
                            // Two consecutive off-curve: implied on-curve midpoint
                            double mx = (x0+x1)/2.0, my = (y0+y1)/2.0;
                            d << "Q" << x0<<","<<y0<<" "<<mx<<","<<my<<" ";
                            ++i;
                        }
                    } else { ++i; }
                }
            }
            d << "Z ";
            pt_idx = end + 1;
        }
        d_out = d.str();
        return true;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// SVG path helper — emit a glyph at (tx, baseline_y) with scale s
//
// transform: translate(tx, baseline_y) scale(s, -s)
// This maps font-unit Y-up to SVG Y-down at the correct scale.
// ─────────────────────────────────────────────────────────────────────────────
static std::string glyph_svg(const std::string& d,
                               double tx, double baseline_y, double s)
{
    if (d.empty()) return "";
    std::ostringstream o;
    o << std::fixed << std::setprecision(6);
    o << "<g transform=\"translate(" << tx << "," << baseline_y
      << ") scale(" << s << "," << -s << ")\">"
      << "<path d=\"" << d << "\" fill=\"black\"/></g>\n";
    return o.str();
}

// Draw a glyph into a Cairo recording surface for ink measurement,
// and return its SVG element string. x is advanced by glyph width.
static std::string draw_glyph(cairo_t* cr,
                                const FTFont& font,
                                unsigned int codepoint,
                                double font_pt,
                                double& x,
                                double baseline_y)
{
    FT_UInt gi = font.glyph_index(codepoint);
    if (gi == 0) return "";

    std::string d;
    double advance_fu = 0.0;
    if (!font.glyph_path(gi, d, advance_fu)) return "";

    double s = font_pt / font.upm;
    double advance_pt = advance_fu * s;

    // Draw into recording surface for bbox measurement
    if (!d.empty()) {
        cairo_save(cr);
        cairo_translate(cr, x, baseline_y);
        cairo_scale(cr, s, -s);
        // Parse and replay the path into Cairo
        // Simple approach: use cairo_move_to/line_to from the FT outline directly
        FT_GlyphSlot slot = font.face->glyph;
        int pt_idx = 0;
        for (int c = 0; c < slot->outline.n_contours; ++c) {
            int end = slot->outline.contours[c];
            int start = pt_idx;
            bool first = true;
            for (int i = start; i <= end; ) {
                double px = slot->outline.points[i].x;
                double py = slot->outline.points[i].y;
                int t = slot->outline.tags[i] & 0x03;
                if (first) {
                    cairo_move_to(cr, px, py); first = false; ++i; continue;
                }
                if (t == FT_CURVE_TAG_ON) {
                    cairo_line_to(cr, px, py); ++i;
                } else if (t == FT_CURVE_TAG_CUBIC && i+2 <= end) {
                    cairo_curve_to(cr, px, py,
                        slot->outline.points[i+1].x, slot->outline.points[i+1].y,
                        slot->outline.points[i+2].x, slot->outline.points[i+2].y);
                    i += 3;
                } else if (t == FT_CURVE_TAG_CONIC) {
                    if (i+1 <= end) {
                        double x1=slot->outline.points[i+1].x, y1=slot->outline.points[i+1].y;
                        int t1 = slot->outline.tags[i+1] & 0x03;
                        if (t1 == FT_CURVE_TAG_ON) {
                            // Convert quadratic to cubic
                            double cx = slot->outline.points[i].x;
                            double cy = slot->outline.points[i].y;
                            double x0c, y0c;
                            cairo_get_current_point(cr, &x0c, &y0c);
                            cairo_curve_to(cr,
                                x0c + 2.0/3.0*(cx-x0c), y0c + 2.0/3.0*(cy-y0c),
                                x1  + 2.0/3.0*(cx-x1),  y1  + 2.0/3.0*(cy-y1),
                                x1, y1);
                            i += 2;
                        } else {
                            double mx=(px+x1)/2.0, my=(py+y1)/2.0;
                            double x0c,y0c;
                            cairo_get_current_point(cr, &x0c, &y0c);
                            cairo_curve_to(cr,
                                x0c+2.0/3.0*(px-x0c), y0c+2.0/3.0*(py-y0c),
                                mx+2.0/3.0*(px-mx),   my+2.0/3.0*(py-my),
                                mx, my);
                            ++i;
                        }
                    } else { ++i; }
                } else { ++i; }
            }
            cairo_close_path(cr);
            pt_idx = end + 1;
        }
        cairo_fill(cr);
        cairo_restore(cr);
    }

    std::string svg = glyph_svg(d, x, baseline_y, s);
    x += advance_pt;
    return svg;
}

// Draw a string into recording surface, return SVG elements, advance x
static std::string draw_string(cairo_t* cr,
                                const FTFont& font,
                                const std::string& text,
                                double font_pt,
                                double& x,
                                double baseline_y)
{
    std::string result;
    for (unsigned char ch : text)
        result += draw_glyph(cr, font, (unsigned int)ch, font_pt, x, baseline_y);
    return result;
}

// Draw string centred at cx
static std::string draw_string_centred(cairo_t* cr,
                                        const FTFont& font,
                                        const std::string& text,
                                        double font_pt,
                                        double cx,
                                        double baseline_y)
{
    double s = font_pt / font.upm;
    // Measure total advance
    double total = 0.0;
    for (unsigned char ch : text) {
        FT_UInt gi = font.glyph_index(ch);
        if (gi == 0) continue;
        std::string d; double adv = 0.0;
        font.glyph_path(gi, d, adv);
        total += adv * s;
    }
    double x = cx - total / 2.0;
    return draw_string(cr, font, text, font_pt, x, baseline_y);
}

// Draw string rotated -90° centred at (cx, cy)
static std::string draw_string_rotated(cairo_t* cr,
                                        const FTFont& font,
                                        const std::string& text,
                                        double font_pt,
                                        double cx, double cy)
{
    double s = font_pt / font.upm;
    // Measure total advance
    double total = 0.0;
    for (unsigned char ch : text) {
        FT_UInt gi = font.glyph_index(ch);
        if (gi == 0) continue;
        std::string d; double adv = 0.0;
        font.glyph_path(gi, d, adv);
        total += adv * s;
    }

    // Draw each glyph at local origin, collect SVG
    std::string inner;
    double x = 0.0;
    // For recording surface: apply rotation
    cairo_save(cr);
    cairo_translate(cr, cx, cy);
    cairo_rotate(cr, -M_PI/2.0);
    cairo_translate(cr, -total/2.0, 0.0);
    inner = draw_string(cr, font, text, font_pt, x, 0.0);
    cairo_restore(cr);

    // SVG: wrap in rotate group
    std::ostringstream out;
    out << std::fixed << std::setprecision(3);
    out << "<g transform=\"translate("<<cx<<","<<cy<<") "
        << "rotate(-90) translate("<<-total/2.0<<",0)\">\n"
        << inner << "</g>\n";
    return out.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// Triangle (filled outline, even-odd inner/outer offset polygons)
// ─────────────────────────────────────────────────────────────────────────────
static std::string draw_triangle(cairo_t* cr,
                                  double tx, double tri_bot,
                                  double tw, double th,
                                  bool strippable,
                                  const FTFont& ocr_font,
                                  double s_pt)
{
    double tri_cx  = tx + tw/2.0;
    double tri_top = tri_bot - th;
    const double OW=0.75, hW=OW/2.0, sv=hW*2.0;
    const double s60=0.866025, c60=0.5;

    double ox0=tri_cx,       oy0=tri_top-sv;
    double ox1=tx-sv*s60,    oy1=tri_bot+sv*c60;
    double ox2=tx+tw+sv*s60, oy2=tri_bot+sv*c60;
    double ix0=tri_cx,       iy0=tri_top+sv;
    double ix1=tx+sv*s60,    iy1=tri_bot-sv*c60;
    double ix2=tx+tw-sv*s60, iy2=tri_bot-sv*c60;

    // Draw into recording surface
    cairo_new_path(cr);
    cairo_move_to(cr,ox0,oy0); cairo_line_to(cr,ox1,oy1);
    cairo_line_to(cr,ox2,oy2); cairo_close_path(cr);
    cairo_move_to(cr,ix0,iy0); cairo_line_to(cr,ix1,iy1);
    cairo_line_to(cr,ix2,iy2); cairo_close_path(cr);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
    cairo_fill(cr);
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_WINDING);

    std::ostringstream svg;
    svg << std::fixed << std::setprecision(3);
    svg << "<path fill-rule=\"evenodd\" fill=\"black\" d=\""
        << "M"<<ox0<<","<<oy0<<" L"<<ox1<<","<<oy1<<" L"<<ox2<<","<<oy2<<" Z "
        << "M"<<ix0<<","<<iy0<<" L"<<ix1<<","<<iy1<<" L"<<ix2<<","<<iy2<<" Z"
        << "\"/>\n";

    if (strippable) {
        double cent_y = tri_bot - th/3.0 + 1.5;
        svg << draw_string_centred(cr, ocr_font, "S", s_pt, tri_cx, cent_y);
    }
    return svg.str();
}

// ─────────────────────────────────────────────────────────────────────────────
// Main SVG generation
// ─────────────────────────────────────────────────────────────────────────────
std::string BarcodeGenerator::generate_svg(const std::string& ean13_digits,
                                            const BarcodeOptions& opts)
{
    std::string digits = normalise(ean13_digits);
    if (digits.size() != 13) {
        LOG_DEBUG("BarcodeGenerator: invalid EAN-13 '{}'", ean13_digits);
        return "";
    }

    // ── Font paths ────────────────────────────────────────────────────────────
    std::string font_dir = Glib::get_user_config_dir() + "/folio/fonts/";
    std::string bar_path = font_dir + (opts.full_height ? "UpcEan72.ttf" : "UpcEan36.ttf");
    std::string ocr_path = font_dir + "OcrBUpcEan.ttf";

    FTFont bar_font(bar_path);
    FTFont ocr_font(ocr_path);

    if (!bar_font.ok) {
        LOG_DEBUG("BarcodeGenerator: cannot load '{}'", bar_path);
        return "";
    }
    if (!ocr_font.ok) {
        LOG_DEBUG("BarcodeGenerator: cannot load '{}'", ocr_path);
        return "";
    }

    // ── Layout constants ──────────────────────────────────────────────────────
    const double BAR_PT  = 72.0;
    const double OCR_PT  =  8.0;
    const double S_PT    =  6.5 * 0.935;
    const double BAR_X   = 41.0;
    const double BAR_BASE= 123.0;
    const double ISBN_X  = 50.0;
    const double ISBN_Y  = opts.full_height ? 54.5 : 90.5;
    const double EAN_CX  = 51.5 - 8.0 + 4.0;  // net -4pt from original
    const double EAN_CY  = opts.full_height ? 87.0 : 105.0;
    const double TRI_GAP =  5.0;
    const double TRI_OFF = -6.0;
    const double TRI_W   = 18.0 * 0.935;
    const double TRI_H   = TRI_W * 0.866;
    const double TRI_BOT = BAR_BASE - 1.0;

    // ── Recording surface — captures exact ink for bbox ───────────────────────
    cairo_surface_t* rec = cairo_recording_surface_create(
        CAIRO_CONTENT_COLOR_ALPHA, nullptr);
    cairo_t* cr = cairo_create(rec);
    cairo_set_source_rgb(cr, 0, 0, 0);

    // ── Build and render barcode ──────────────────────────────────────────────
    std::string svg_body;

    // Barcode string — bars + embedded digits via UpcEan font
    std::string barcode = map_to_font(digits);
    if (opts.include_price && opts.price.size() == 4)
        barcode += ean5_string(opts.currency + opts.price);

    double x = BAR_X;
    svg_body += draw_string(cr, bar_font, barcode, BAR_PT, x, BAR_BASE);
    double x_bars_end = x;  // exact end of last bar — anchor for triangle

    // Whitespace '>' marker (char 120 in UpcEan font).
    // Advance = 117 font units / 1000 UPM * 72pt = 8.424pt
    const double WS_ADV = 8.424;

    if (opts.show_whitespace)
        svg_body += draw_string(cr, bar_font, std::string(1,(char)120),
                                 BAR_PT, x, BAR_BASE);

    // Triangle always anchored at x_bars_end + WS_ADV + gap.
    // This matches the whitespace-on position exactly — the '>' glyph
    // occupies WS_ADV pt whether drawn or not, so the triangle sits at
    // the same distance from the last bar in both cases.
    if (opts.triangle_state > 0) {
        double tx = x_bars_end + WS_ADV + TRI_GAP + TRI_OFF;
        svg_body += draw_triangle(cr, tx, TRI_BOT, TRI_W, TRI_H,
                                   opts.triangle_state == 2,
                                   ocr_font, S_PT);
    }

    // ISBN label (strip 978/979 prefix)
    std::string display = digits;
    if (digits.size()==13 &&
        (digits.substr(0,3)=="978"||digits.substr(0,3)=="979"))
        display = digits.substr(3);
    double isbn_x = ISBN_X;
    svg_body += draw_string(cr, ocr_font, "ISBN " + display,
                             OCR_PT, isbn_x, ISBN_Y);

    // EAN rotated label
    svg_body += draw_string_rotated(cr, ocr_font, "EAN",
                                     OCR_PT, EAN_CX, EAN_CY);

    // ── Exact ink extents from recording surface ──────────────────────────────
    double ink_x, ink_y, ink_w, ink_h;
    cairo_recording_surface_ink_extents(rec, &ink_x, &ink_y, &ink_w, &ink_h);
    cairo_destroy(cr);
    cairo_surface_destroy(rec);

    if (ink_w <= 0 || ink_h <= 0) {
        LOG_DEBUG("BarcodeGenerator: empty ink extents for '{}'", digits);
        return "";
    }

    // ── Emit SVG ──────────────────────────────────────────────────────────────
    const double PAD = 9.0;
    double vx = ink_x - PAD, vy = ink_y - PAD;
    double vw = ink_w + PAD*2.0, vh = ink_h + PAD*2.0;

    std::ostringstream svg;
    svg << std::fixed << std::setprecision(2);
    svg << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        << "<svg xmlns=\"http://www.w3.org/2000/svg\""
        << " width=\""  << vw << "pt\""
        << " height=\"" << vh << "pt\""
        << " viewBox=\""<< vx <<" "<< vy <<" "<< vw <<" "<< vh <<"\">\n"
        << "<rect x=\""<<vx<<"\" y=\""<<vy
        <<"\" width=\""<<vw<<"\" height=\""<<vh<<"\" fill=\"white\"/>\n"
        << svg_body
        << "</svg>\n";

    return svg.str();
}

std::string BarcodeGenerator::generate_svg_from_isbn(const std::string& isbn,
                                                      const BarcodeOptions& opts)
{
    std::string d = normalise(isbn);
    if (d.empty()) return "";
    if (d.size()==13 && is_valid_ean13(d))  return generate_svg(d, opts);
    if (d.size()==10 && is_valid_isbn10(d)) {
        std::string e = isbn10_to_ean13(d);
        if (!e.empty()) return generate_svg(e, opts);
    }
    if (d.size()==12) {
        std::string e = d + ean13_check_digit(d);
        if (is_valid_ean13(e)) return generate_svg(e, opts);
    }
    LOG_DEBUG("BarcodeGenerator: unrecognised isbn '{}'", isbn);
    return "";
}

} // namespace Folio
