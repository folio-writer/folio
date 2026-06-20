#pragma once
// ─────────────────────────────────────────────────────────────────────────────
// Folio — ModuleLibrary.hpp   (s27 — module persistence; pure, GTK/GLib-free)
//
// The save-home for patterns. DESIGN §5.4's stamp-vs-instance split made real:
//   • The LIBRARY (the stamp) — JSON files in ~/.local/share/folio/modules/.
//     Reusable across books; this is where "save my arc as a module" lands and
//     where "New from Pattern" lists choices. Seeded with the built-ins.
//   • The BUNDLE COPY (the instance) — applying a module writes module.json INTO
//     the .folio bundle, so a book (and a series) carries its own structure and
//     it travels independent of the library. (Cussler's module handed to a
//     protégé; a series cloning a prior book's arc.)
//
// Pure: std::filesystem + ModuleIO only. No GTK, no GLib — path resolution uses
// XDG_DATA_HOME / HOME like FolioLog and Hyphenator, so this is sandbox-testable.
// Directory enumeration is fed here, not reached from the UI (DESIGN §4.7a).
// ─────────────────────────────────────────────────────────────────────────────

#include "Module.hpp"
#include <optional>
#include <string>
#include <vector>

namespace Folio {
namespace ModuleLibrary {

// One listed module in the library (cheap: id + name + path, parsed from file).
struct Entry {
    std::string id;
    std::string name;
    std::string path;   // absolute path to the .json file
};

// ── Library (the global stamp) ───────────────────────────────────────────────
// Resolved library directory (~/.local/share/folio/modules), no trailing slash.
std::string default_library_dir();

// List modules in `dir` (default = default_library_dir()), name-sorted. Malformed
// files are skipped, never throwing. Missing directory → empty list.
std::vector<Entry> list(const std::string& dir = default_library_dir());

// Write `m` to `dir`/<sanitized-id>.json (pretty). Creates the directory.
bool save(const Module& m, const std::string& dir = default_library_dir());

// Load a module from an exact file path. nullopt on missing / malformed.
std::optional<Module> load(const std::string& path);

// Seed the built-ins (three-act + Folio Key Points) into `dir` IF it has no
// modules yet. Returns true if it wrote them, false if already populated.
bool seed_builtins(const std::string& dir = default_library_dir());

// ── Bundle copy (the instance that travels) ──────────────────────────────────
// Path of the module instance inside a .folio bundle root (sibling of project.json).
std::string bundle_module_path(const std::string& bundle_root);
bool                  write_to_bundle(const Module& m, const std::string& bundle_root);
bool                  bundle_has_module(const std::string& bundle_root);
std::optional<Module> load_from_bundle(const std::string& bundle_root);

} // namespace ModuleLibrary
} // namespace Folio
