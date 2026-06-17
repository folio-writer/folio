#pragma once
//
// Editor_internal.hpp — implementation-shared declarations for the five
// Editor*.cpp translation units. NOT part of the public Editor surface;
// included only by Editor.cpp (CORE) and Editor_{build,format,views,text}.cpp.
//
// Created in s13 when Editor.cpp (9,669 lines, one class, 121 methods) was
// split into CORE + four functional TUs (BUILD / FORMAT / VIEWS / TEXT),
// following the Curvz convention (cf. Canvas s161, MainWindow s164).
// See HANDOFF.md (s12→s13) for the full split design.
//
// Cross-TU file-scope helpers and statics would live here as C++17 `inline`
// variables / shared structs. Per the triage rule (a file-scope helper used by
// exactly ONE TU stays `static` in that TU; one used by several moves here),
// none of the existing Editor.cpp file-statics qualified — each is used by a
// single TU and stays `static` in its own file:
//
//   get_para_range        → Editor_format.cpp  (FORMAT)
//   remove_prefix_tags    → Editor_format.cpp  (FORMAT)
//   SP_ELEMENTS           → Editor_format.cpp  (FORMAT)
//   hex_to_rgba_raw       → Editor_text.cpp    (TEXT)
//
// (make_joined_divider was a sixth such static, but s14's -Wall pass confirmed
// it had no callers, so it was deleted rather than kept.)
//
// This header is therefore an intentional stub: it carries the convention and
// the include slot so the first genuinely cross-TU helper has an obvious home,
// without forcing today's single-TU statics out of their files. (Mirrors the
// s161 note in Canvas_internal.hpp, where FTOutlineCtx stayed static in OPS.)

#include <Editor.hpp>

namespace Folio {

// (No cross-TU shared state yet — see the file header for the triage rationale.)

}  // namespace Folio
