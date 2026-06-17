#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// CompileFormatIO.hpp — pure (string ⇄ CompileFormat) serialization seam (s18)
//
// WHY THIS EXISTS
//   s18 adds user-editable, persisted custom CompileFormats (the "Format editor
//   + persistence" F-PDF backlog item). A CompileFormat is large — PageSpec +
//   Furniture (two RunningHeads) + an ElementMap of 16 TextFormats (body, 9
//   headings, 6 screenplay elements), each TextFormat ~14 fields. Inlining all
//   of that into FolioPrefs.cpp the way TextStyle is would bury ~220 g_key_file
//   calls in the prefs writer and leave the only novel logic (the round-trip)
//   untestable.
//
//   So the conversion lives HERE as a pair of pure functions over a flat
//   key/value list, with ZERO GTK/GLib dependency. FolioPrefs just pumps the
//   pairs in/out of a GKeyFile group (mirrors how it already drives
//   text_styles). The round-trip is g++-compilable and unit-checkable in the
//   sandbox — the "thin test seam around pure-logic units" the testing-gap note
//   in HANDOFF.md keeps calling for. (s18 verified: see tests/compileformatio_test.)
//
// THE SCHEME
//   to_kv(fmt)  → an ordered vector<{key,value}> of ONLY the fields that differ
//                 from a default-constructed CompileFormat (so a manuscript
//                 serialises to ~20 keys, not ~220). Keys are dotted/flat:
//                   name, builtin, mode, hyphenate, pb-top
//                   page.size, page.orient, page.cw, page.ch,
//                   page.mi, page.mo, page.mt, page.mb, page.mirror
//                   fur.hdr, fur.hdr.l|c|r, fur.ftr, fur.ftr.l|c|r,
//                   fur.restart, fur.title
//                   body.<f>, heading.<n>.<f>, sp.<n>.<f>
//                 where <f> is a TextFormat field key (see tf_* below).
//   from_kv(m)  → start from a default CompileFormat and overlay every present
//                 key. Absent key = inherit the default. Round-trips to_kv.
//
//   Enums serialise to short stable tokens (NOT integer ordinals — reordering
//   an enum must not silently re-map saved data):
//     RenderMode   formal | adaptable
//     PaperSize    letter | a4 | legal | custom
//     Orientation  portrait | landscape
//     Align        left | center | right | justify
//     TextCase     asis | upper | lower | title
//
//   Persistence note: `builtin` is serialised but a loaded format is ALWAYS
//   forced to builtin=false by the caller — only the three code-seeded presets
//   are built-ins; anything read back from prefs is, by definition, custom.
//   (We still round-trip the flag so to_kv/from_kv are pure inverses for tests.)
// ─────────────────────────────────────────────────────────────────────────────

#include "CompileFormat.hpp"

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace Folio {

using FormatKV = std::pair<std::string, std::string>;

// ── enum ⇄ token ─────────────────────────────────────────────────────────────
std::string  render_mode_token(RenderMode m);
RenderMode   render_mode_from_token(const std::string& s, RenderMode fallback = RenderMode::Adaptable);

std::string  paper_size_token(PaperSize p);
PaperSize    paper_size_from_token(const std::string& s, PaperSize fallback = PaperSize::Letter);

std::string  orientation_token(Orientation o);
Orientation  orientation_from_token(const std::string& s, Orientation fallback = Orientation::Portrait);

std::string  align_token(Align a);
Align        align_from_token(const std::string& s, Align fallback = Align::Left);

std::string  text_case_token(TextCase c);
TextCase     text_case_from_token(const std::string& s, TextCase fallback = TextCase::AsIs);

// ── CompileFormat ⇄ flat key/value ───────────────────────────────────────────
// to_kv emits only non-default fields, in a stable order. from_kv overlays the
// present keys onto a default CompileFormat.
std::vector<FormatKV> compile_format_to_kv(const CompileFormat& fmt);
CompileFormat         compile_format_from_kv(const std::map<std::string, std::string>& kv);

}  // namespace Folio
