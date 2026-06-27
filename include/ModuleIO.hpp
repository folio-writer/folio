#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — ModuleIO.hpp   (s23 — pure json ⇄ Module seam; GTK-free)
//
// Mirrors CompileFormatIO's role: the only novel logic (the round-trip) lives
// here as pure functions over nlohmann::json, with ZERO GTK/GLib dependency, so
// it is g++-compilable and unit-checkable in the sandbox. The app reads/writes
// the JSON files in ~/.local/share/folio/modules/ and pumps the text through
// these; directory enumeration is thin app glue, not part of this seam (UI
// reaches; seams are fed — DESIGN §4.7a).
//
// Scott specified modules are JSON files, so this seam is json-native (unlike
// CompileFormatIO's flat key/value, which served GKeyFile).
// ─────────────────────────────────────────────────────────────────────────────

#include "Module.hpp"
#include "KpPalette.hpp"   // s81 — KpSwatch {id, name, hex} (the install record)
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <utility>

namespace Folio {

using json = nlohmann::json;

namespace ModuleIO {

// struct → json (the on-disk / in-bundle shape)
json to_json(const Module& m);

// json → struct. Tolerant: missing fields take struct defaults; unknown fields
// ignored. Never throws on a well-formed-but-partial module.
Module from_json(const json& j);

// Convenience string round-trip (pretty for files, compact optional).
std::string to_string(const Module& m, bool pretty = true);
Module      from_string(const std::string& text);

// ── Spectrum palette (s23) ───────────────────────────────────────────────────
// A scene's KP is a *tag*, and a tag is a named colour. So the palette IS the
// arc: one continuous spectrum (blue→cyan→green→yellow→orange→red→magenta→
// purple) sliced into one swatch per Key Point, each named after its KP. Tag
// and colour become a single entry — they cannot drift. (Scott's design.)
//
// spectrum_hex(t): sample the ramp at t∈[0,1] → "#rrggbb".
std::string spectrum_hex(double t);
// keypoint_palette: one swatch {id, name, hex} per KP in arc order, sampled
// evenly across the spectrum. Pure. The id is the KeyPoint's id, so installing
// these into the project palette gives each swatch a stable identity that a
// scene's kp_id resolves against (s81); color_idx (= KP order) lands on the
// matching swatch. Returns KpSwatch (KpPalette.hpp) so the install is a copy.
std::vector<KpSwatch> keypoint_palette(const Module& m);

} // namespace ModuleIO
} // namespace Folio
