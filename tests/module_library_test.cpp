// ─────────────────────────────────────────────────────────────────────────────
// Folio — module_library_test.cpp   (s27)   Pure-layer checks for the module
//   library + bundle persistence: seed-once, list, save/load round-trip, and the
//   stamp→instance bundle copy. Uses a throwaway temp directory.
// ─────────────────────────────────────────────────────────────────────────────
/*
Build + run — sandbox (g++):
g++ -std=c++20 -Wall -Wextra -Werror -I include -I /home/claude/sbox tests/module_library_test.cpp src/ModuleLibrary.cpp src/ModuleIO.cpp -o /tmp/module_library_test
/tmp/module_library_test

Build + run — Fedora target (clang++):
clang++ -std=c++20 -Wall -Wextra -Werror -I include tests/module_library_test.cpp src/ModuleLibrary.cpp src/ModuleIO.cpp -o /tmp/module_library_test
/tmp/module_library_test
*/
#include "Module.hpp"
#include "ModuleLibrary.hpp"

#include <cstdio>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace Folio;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const char* what) {
    if (ok) ++g_pass;
    else { ++g_fail; std::printf("  FAIL: %s\n", what); }
}

int main() {
    // Unique temp workspace.
    fs::path base = fs::temp_directory_path() /
        ("folio_modlib_" + std::to_string(::getpid()));
    fs::path libdir = base / "modules";
    fs::path bundle = base / "Book.folio";
    std::error_code ec;
    fs::remove_all(base, ec);

    // ── default dir resolves to something under folio/modules ────────────────
    check(ModuleLibrary::default_library_dir().find("folio/modules") != std::string::npos,
          "default library dir is under folio/modules");

    // ── seed-once ─────────────────────────────────────────────────────────────
    check(ModuleLibrary::list(libdir.string()).empty(), "fresh dir lists empty");
    bool seeded = ModuleLibrary::seed_builtins(libdir.string());
    check(seeded, "seed_builtins writes into an empty dir");
    auto entries = ModuleLibrary::list(libdir.string());
    check(entries.size() == 2, "library lists the two built-ins after seeding");
    bool has_kp = false, has_3a = false;
    for (auto& e : entries) {
        if (e.id == "folio_keypoints") has_kp = true;
        if (e.id == "three_act")       has_3a = true;
    }
    check(has_kp && has_3a, "both built-in ids present");
    check(!ModuleLibrary::seed_builtins(libdir.string()),
          "seed_builtins is a no-op when already populated");
    check(ModuleLibrary::list(libdir.string()).size() == 2, "no double-seed");

    // ── load a listed module; the new model survives the file round-trip ──────
    std::string kp_path;
    for (auto& e : entries) if (e.id == "folio_keypoints") kp_path = e.path;
    auto loaded = ModuleLibrary::load(kp_path);
    check(loaded.has_value(), "load() reads a library module");
    if (loaded) {
        check(loaded->pacing.levels.size() == 4, "loaded module keeps the pacing pattern");
        bool any_desc = false, any_arc = false;
        for (auto& a : loaded->craft.acts) for (auto& k : a.kps) {
            if (!k.description.empty()) any_desc = true;
            if (k.arc > 0.0)            any_arc  = true;
        }
        check(any_desc, "loaded module keeps KP descriptions");
        check(any_arc,  "loaded module keeps KP arc values");
    }

    // ── save a customised module; it lists and reloads ───────────────────────
    {
        Module m = built_in_folio_keypoints();
        m.id = "cussler_caper";
        m.name = "Cussler Caper";
        m.pacing.levels = { 1.0, 0.4, 1.0, 1.0 };   // hotter signature
        check(ModuleLibrary::save(m, libdir.string()), "save() writes a custom module");
        check(ModuleLibrary::list(libdir.string()).size() == 3, "library now lists 3");
        std::string p = (libdir / "cussler_caper.json").string();
        auto back = ModuleLibrary::load(p);
        check(back && back->name == "Cussler Caper", "custom module reloads by id-derived name");
        check(back && back->pacing.levels.size() == 4 && back->pacing.levels[0] == 1.0,
              "custom pacing pattern round-trips");
    }

    // ── bundle copy (the travelling instance) ─────────────────────────────────
    check(!ModuleLibrary::bundle_has_module(bundle.string()), "fresh bundle has no module");
    check(!ModuleLibrary::load_from_bundle(bundle.string()).has_value(),
          "load_from_bundle is nullopt when absent");
    {
        Module m = built_in_folio_keypoints();
        check(ModuleLibrary::write_to_bundle(m, bundle.string()), "write_to_bundle succeeds");
        check(ModuleLibrary::bundle_has_module(bundle.string()), "bundle now has module.json");
        check(ModuleLibrary::bundle_module_path(bundle.string()).find("module.json") != std::string::npos,
              "bundle module path is module.json beside the manifest");
        auto inst = ModuleLibrary::load_from_bundle(bundle.string());
        check(inst && inst->id == "folio_keypoints", "bundle instance loads back with the right id");
    }

    fs::remove_all(base, ec);
    std::printf("\nmodule_library_test: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
