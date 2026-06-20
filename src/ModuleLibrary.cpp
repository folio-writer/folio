// ─────────────────────────────────────────────────────────────────────────────
// Folio — ModuleLibrary.cpp   (s27)   Pure. GTK/GLib-free. See ModuleLibrary.hpp.
// ─────────────────────────────────────────────────────────────────────────────
#include "ModuleLibrary.hpp"
#include "ModuleIO.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace Folio {
namespace ModuleLibrary {

namespace fs = std::filesystem;
namespace {

std::optional<std::string> read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    if (!in) return std::nullopt;
    std::ostringstream ss;
    ss << in.rdbuf();
    if (!in.good() && !in.eof()) return std::nullopt;
    return ss.str();
}

bool write_file(const fs::path& p, const std::string& s) {
    std::error_code ec;
    if (p.has_parent_path()) fs::create_directories(p.parent_path(), ec);
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << s;
    return out.good();
}

// Filesystem-safe id: keep [A-Za-z0-9_-], fold everything else to '_'.
std::string sanitize(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in) {
        if (std::isalnum(c) || c == '_' || c == '-') out.push_back((char)c);
        else                                          out.push_back('_');
    }
    return out;
}

std::optional<Module> load_text(const std::optional<std::string>& txt) {
    if (!txt) return std::nullopt;
    try { return ModuleIO::from_string(*txt); }
    catch (...) { return std::nullopt; }
}

} // namespace

std::string default_library_dir() {
    if (const char* xdg = std::getenv("XDG_DATA_HOME"); xdg && *xdg)
        return std::string(xdg) + "/folio/modules";
    if (const char* home = std::getenv("HOME"); home && *home)
        return std::string(home) + "/.local/share/folio/modules";
    return "./folio_modules";   // last-resort fallback
}

std::vector<Entry> list(const std::string& dir) {
    std::vector<Entry> out;
    std::error_code ec;
    if (!fs::exists(dir, ec) || ec) return out;
    for (const auto& de : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!de.is_regular_file(ec) || ec) continue;
        if (de.path().extension() != ".json") continue;
        auto m = load_text(read_file(de.path()));
        if (!m) continue;   // skip malformed
        out.push_back({ m->id, m->name, de.path().string() });
    }
    std::sort(out.begin(), out.end(),
              [](const Entry& a, const Entry& b) { return a.name < b.name; });
    return out;
}

bool save(const Module& m, const std::string& dir) {
    std::string id = sanitize(!m.id.empty() ? m.id : m.name);
    if (id.empty()) id = "module";
    return write_file(fs::path(dir) / (id + ".json"), ModuleIO::to_string(m, true));
}

std::optional<Module> load(const std::string& path) {
    return load_text(read_file(path));
}

bool seed_builtins(const std::string& dir) {
    if (!list(dir).empty()) return false;   // already populated — don't clobber
    bool ok = true;
    ok = save(built_in_three_act(), dir)        && ok;
    ok = save(built_in_folio_keypoints(), dir)  && ok;
    return ok;
}

std::string bundle_module_path(const std::string& bundle_root) {
    return (fs::path(bundle_root) / "module.json").string();
}

bool write_to_bundle(const Module& m, const std::string& bundle_root) {
    return write_file(bundle_module_path(bundle_root), ModuleIO::to_string(m, true));
}

bool bundle_has_module(const std::string& bundle_root) {
    std::error_code ec;
    bool e = fs::exists(bundle_module_path(bundle_root), ec);
    return e && !ec;
}

std::optional<Module> load_from_bundle(const std::string& bundle_root) {
    if (!bundle_has_module(bundle_root)) return std::nullopt;
    return load(bundle_module_path(bundle_root));
}

} // namespace ModuleLibrary
} // namespace Folio
