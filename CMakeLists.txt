cmake_minimum_required(VERSION 3.20)

# Prefer clang when present, but stay overridable: an explicit
# -DCMAKE_CXX_COMPILER=... or a CC/CXX environment variable wins, and if clang
# isn't installed we fall back to the system default compiler. Must run before
# project() — that's when CMake locks in the toolchain. (s14: replaced an
# unconditional FORCE pin to /usr/bin/clang++, which broke on hosts without that
# exact path and couldn't be overridden.)
if(NOT DEFINED CMAKE_CXX_COMPILER AND NOT DEFINED ENV{CXX})
  find_program(_FOLIO_CLANGXX clang++)
  find_program(_FOLIO_CLANGCC  clang)
  if(_FOLIO_CLANGXX AND _FOLIO_CLANGCC)
    set(CMAKE_C_COMPILER   "${_FOLIO_CLANGCC}"  CACHE FILEPATH "C compiler")
    set(CMAKE_CXX_COMPILER "${_FOLIO_CLANGXX}" CACHE FILEPATH "C++ compiler")
  endif()
endif()

project(folio VERSION 0.1.0 LANGUAGES C CXX)

# CMP0135 (CMake 3.24+): stamp FetchContent-extracted files with extraction
# time, not the archive's stored mtimes, so a changed URL triggers a rebuild.
# Guarded so older CMake (we allow 3.20) simply skips it.
if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

# s14 — default to a Debug build (no -O, assertions live). A release build is
# opt-in via an explicit arg: `cmake -B build -DCMAKE_BUILD_TYPE=Release`, which
# is what build.sh passes. Guarded so multi-config generators are left alone.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# s15 — compile-time debug logging, decoupled from build type *on purpose*.
# LOG_DEBUG -> SPDLOG_DEBUG is gated at compile time by SPDLOG_ACTIVE_LEVEL; if
# that gate is raised to INFO the LOG_DEBUG calls are stripped, not merely
# filtered. The log-mode debug workflow (add LOG_DEBUG, ship, read the log) must
# keep working under whatever build Folio is normally compiled with — and that
# build is Release (build.sh passes it). So this is its own switch, default ON,
# rather than being tied to CMAKE_BUILD_TYPE: a lean/packaged build can strip
# debug logging with -DFOLIO_DEBUG_LOG=OFF without disturbing the dev workflow.
option(FOLIO_DEBUG_LOG "Compile in LOG_DEBUG/SPDLOG_DEBUG calls" ON)

set(CMAKE_CXX_STANDARD 20)           # s14 — pin C++20 (gtkmm 4 needs ≥17; 20 is
set(CMAKE_CXX_STANDARD_REQUIRED ON)  #       fully supported & is the project's target).
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)#       Extensions left ON → gnu++20 (glib/GTK use them).

# ── Dependencies ──────────────────────────────────────────────────────────────
find_package(PkgConfig REQUIRED)
pkg_check_modules(GTKMM    REQUIRED gtkmm-4.0)
pkg_check_modules(ENCHANT  REQUIRED enchant-2)
pkg_check_modules(LIBRSVG  REQUIRED librsvg-2.0)
pkg_check_modules(FREETYPE REQUIRED freetype2)

# nlohmann/json — try pkg-config first, fall back to FetchContent
find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        nlohmann_json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        URL_HASH SHA256=d6c65aca6b1ed68e7a182f4757257b107ae403032760ed6ef121c9d55e81757d
    )
    FetchContent_MakeAvailable(nlohmann_json)
endif()

# spdlog — try system package first, fall back to FetchContent
find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
    include(FetchContent)
    FetchContent_Declare(
        spdlog
        URL https://github.com/gabime/spdlog/archive/refs/tags/v1.15.3.tar.gz
        URL_HASH SHA256=f89ffd9604b98e4b4c78e95f29e92c31cda4bdb94a1fa6e5b22b4bfee12ac47d
    )
    # Use header-only mode to avoid needing a compiled libspdlog
    set(SPDLOG_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SPDLOG_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(SPDLOG_INSTALL        OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(spdlog)
endif()

# libhyphen — Frank Liang's TeX hyphenation engine (LibreOffice/OpenOffice
# lineage). Used by F-PDF automatic hyphenation (Adaptable mode) to find legal
# in-word break points; Folio injects U+00AD soft hyphens at those points and
# lets Pango break/draw them (unlike Curvz, which owns its own line breaking).
#
# OPTIONAL on purpose: the package ships NO pkg-config or CMake config, so we
# probe with find_path/find_library. When absent the build still succeeds with
# automatic hyphenation compiled out — the hyphenate() pump returns no break
# points, so justification falls back to plain wrapping (no crash). Install:
#   Fedora:  dnf install hyphen-devel hyphen-en
#   Debian:  apt install libhyphen-dev hyphen-en-us
# The header is sometimes /usr/include/hyphen.h, sometimes .../hyphen/hyphen.h.
find_path(HYPHEN_INCLUDE_DIR hyphen.h PATH_SUFFIXES hyphen)
find_library(HYPHEN_LIBRARY NAMES hyphen)
if(HYPHEN_INCLUDE_DIR AND HYPHEN_LIBRARY)
    set(HYPHEN_FOUND TRUE)
    message(STATUS "libhyphen: ${HYPHEN_LIBRARY} (automatic hyphenation enabled)")
else()
    set(HYPHEN_FOUND FALSE)
    message(STATUS "libhyphen not found -- automatic hyphenation disabled "
                   "(manual soft hyphens still work). Install hyphen-devel to enable.")
endif()

# ── GLib resources ────────────────────────────────────────────────────────────
# Compile resources/resources.xml → src/folio-resources.c (+ header).
# Output lands in the source tree (src/ include/) which always exist.
# Adding an icon: drop the .svg in resources/icons/, add a <file> line to
# resources/resources.xml, rebuild. CONFIGURE_DEPENDS re-globs on rebuild so
# CMake notices new icon files without a fresh configure.
find_program(GLIB_COMPILE_RESOURCES glib-compile-resources REQUIRED)

set(RESOURCES_XML "${CMAKE_SOURCE_DIR}/resources/resources.xml")
set(RESOURCES_C   "${CMAKE_SOURCE_DIR}/src/folio-resources.c")
set(RESOURCES_H   "${CMAKE_SOURCE_DIR}/include/folio-resources.h")

file(GLOB RESOURCE_FILES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/resources/icons/*.svg")

add_custom_command(
    OUTPUT  "${RESOURCES_C}"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${RESOURCES_C}"
    COMMAND "${GLIB_COMPILE_RESOURCES}"
            --target "${RESOURCES_C}"
            --generate-source
            resources.xml
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/resources"
    DEPENDS "${RESOURCES_XML}" ${RESOURCE_FILES}
    COMMENT "Compiling GLib resources (source)"
    VERBATIM)

add_custom_command(
    OUTPUT  "${RESOURCES_H}"
    COMMAND ${CMAKE_COMMAND} -E remove -f "${RESOURCES_H}"
    COMMAND "${GLIB_COMPILE_RESOURCES}"
            --target "${RESOURCES_H}"
            --generate-header
            resources.xml
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/resources"
    DEPENDS "${RESOURCES_XML}" ${RESOURCE_FILES}
    COMMENT "Compiling GLib resources (header)"
    VERBATIM)

add_custom_target(folio_resources_gen DEPENDS "${RESOURCES_C}" "${RESOURCES_H}")

# ── Sources ───────────────────────────────────────────────────────────────────
set(SOURCES
    src/folio-resources.c   # generated by glib-compile-resources (see above)
    src/main.cpp
    src/Application.cpp
    src/BackupManager.cpp
    src/MainWindow.cpp
    src/FocusWindow.cpp           # s21 — distraction-free window on the shared editor buffer
    src/Sidebar.cpp
    src/EditorTabBar.cpp          # was SceneTimeline.cpp
    src/Editor.cpp                # s13 — CORE TU (lifecycle + glue)
    src/Editor_build.cpp          # s13 — BUILD TU (toolbar/footer/find/popover)
    src/Editor_format.cpp         # s13 — FORMAT TU (tags/indent/font/outline/screenplay)
    src/Editor_views.cpp          # s13 — VIEWS TU (board/grid/joined/focus)
    src/Editor_text.cpp           # s13 — TEXT TU (find/links/annotations)
    src/EditorHtmlSerializer.cpp  # extracted from Editor.cpp
    src/EditorRuler.cpp           # horizontal ruler above text view
    src/RulerManagerDialog.cpp    # fine-tune ruler values numerically
    src/AnnotationReportDialog.cpp # project-wide annotation report
    src/TextSubstitution.cpp      # smart quotes, em dash, ellipsis, autocorrect
    src/SpellChecker.cpp          # enchant-2 spell check wrapper
    src/SpellCheckHighlighter.cpp # buffer tag management + squiggle rendering
    src/UnicodePickerPopover.cpp  # reusable unicode character picker popover
    src/Inspector.cpp
    src/ObjectForm.cpp            # s31 — template-driven object form widget (renders a FormPlan)
    src/SnapshotDiff.cpp          # extracted from Inspector.cpp
    src/DocumentModel.cpp
    src/Iid.cpp                   # s19 — pure cross-layer part identity (GTK-free, tested)
    src/ModuleIO.cpp              # s23 — pure (json<->Module) anatomy seam (GTK-free, tested)
    src/ModulePlanner.cpp         # s23 — pure scaffold planner: module+inputs -> Part/Chapter/Scene plan
    src/ModuleMaterializer.cpp    # s23 — Layer 2: materialise a plan into BinderNodes (kp_id stamped)
    src/ModuleLibrary.cpp         # s27 — module persistence: global library (stamp) + bundle copy (instance)
    src/ObjectIO.cpp              # s31 — pure (json<->Object/Template) seam + legacy migration (GTK-free, tested)
    src/ObjectStore.cpp           # s31 — object registry + instances; binder-leaf projection, blob round-trip (GTK-free, tested)
    src/ProjectBundle.cpp         # s19 — pure v5 .folio bundle seam (blob<->bundle on disk;
                                  #        GTK-free, round-trip + (C)-hybrid reconcile tested)
    src/FolioPrefs.cpp
    src/PreferencesDialog.cpp
    src/SnapshotDialog.cpp
    src/ProjectGoalDialog.cpp
    src/StyleManagerDialog.cpp
    src/PomodoroDialog.cpp
    src/Exporter.cpp
    src/ExportDialog.cpp
    src/Importer.cpp
    src/ImportDialog.cpp
    src/PatternDialog.cpp         # s24 — Layer 3: pattern input dialog (PlanInputs over the pure planner)
    src/ModuleBoard.cpp           # s28 — read-only mixing-board render
    src/SearchEngine.cpp
    src/SearchDialog.cpp
    src/ScreenplayHelpDialog.cpp
    src/PrintDialog.cpp
    src/PdfExporter.cpp           # s16 — F-PDF: scan adapter + preflight (first consumer
                                  #        of CompileFormat.hpp / ExportScan.hpp); paginator next
    src/PdfPaginator.cpp          # s16 — F-PDF: Cairo/Pango render + pagination layer
    src/CompileFormatIO.cpp       # s18 — F-PDF: pure (string<->CompileFormat) serialization
                                  #        seam for persisted custom formats (GTK-free, tested)
    src/CompileFormatDialog.cpp   # s18 — F-PDF: custom-format editor (create/edit/dup/delete)
    src/Hyphenator.cpp            # s16 — F-PDF: libhyphen pump (auto-hyphenation;
                                  #        gated by FOLIO_HAVE_HYPHEN, no-op without the lib)
    src/ReportEngine.cpp
    src/BarcodeGenerator.cpp
    src/BarcodeDialog.cpp
)

add_executable(folio ${SOURCES})

# Ensure the generated resource .c/.h exist before any TU compiles.
add_dependencies(folio folio_resources_gen)

target_include_directories(folio PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${GTKMM_INCLUDE_DIRS}
    ${ENCHANT_INCLUDE_DIRS}
    ${LIBRSVG_INCLUDE_DIRS} ${FREETYPE_INCLUDE_DIRS}
)

target_link_libraries(folio PRIVATE
    ${GTKMM_LIBRARIES}
    ${ENCHANT_LIBRARIES}
    ${LIBRSVG_LIBRARIES} ${FREETYPE_LIBRARIES}
    z
    nlohmann_json::nlohmann_json
    spdlog::spdlog
)

# libhyphen — conditional. FOLIO_HAVE_HYPHEN gates the automatic-hyphenation
# code path in Hyphenator.cpp; when libhyphen is absent the pump compiles out
# (returns no break points) and manual soft hyphens are unaffected.
if(HYPHEN_FOUND)
    target_include_directories(folio PRIVATE ${HYPHEN_INCLUDE_DIR})
    target_link_libraries(folio PRIVATE ${HYPHEN_LIBRARY})
    target_compile_definitions(folio PRIVATE FOLIO_HAVE_HYPHEN)
endif()

target_compile_options(folio PRIVATE
    ${GTKMM_CFLAGS_OTHER}
    ${ENCHANT_CFLAGS_OTHER}
    ${LIBRSVG_CFLAGS_OTHER}
    # s14 — warning pass. CXX-only so the glib-generated folio-resources.c
    # doesn't flood the dump. -Werror added once the tree hit zero warnings
    # (s14): new warnings now fail the build instead of scrolling past.
    # Note: couples the build to clang's warning set — a g++ build may flag
    # extra things and fail. If that bites, scope -Werror to clang or add a
    # targeted -Wno-error=<cat>.
    $<$<COMPILE_LANGUAGE:CXX>:-Wall>
    $<$<COMPILE_LANGUAGE:CXX>:-Wextra>
    $<$<COMPILE_LANGUAGE:CXX>:-Werror>
)

# Source file/line in log output, and the spdlog compile-time level gate.
# SPDLOG_ACTIVE_LEVEL strips (does not merely filter) any LOG_* below it, so the
# DEBUG calls only exist in the binary when FOLIO_DEBUG_LOG is ON (the default;
# see the option above). OFF raises the floor to INFO for a lean release.
if(FOLIO_DEBUG_LOG)
    target_compile_definitions(folio PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_DEBUG)
else()
    target_compile_definitions(folio PRIVATE SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO)
endif()

# ── Install ───────────────────────────────────────────────────────────────────
install(TARGETS folio DESTINATION bin)
install(FILES folio.desktop DESTINATION share/applications)
# App icon: the brand mark, installed into the hicolor theme (see below).
# install it into the hicolor theme so Icon=folio in the .desktop resolves.
# Install the brand mark into the hicolor theme as "folio" so Icon=folio in
# the .desktop resolves. It's the same currentColor symbolic used in-app;
# renamed to folio.svg on install (the name the desktop entry references).
install(FILES resources/icons/folio-brand-symbolic.svg
        DESTINATION share/icons/hicolor/scalable/apps
        RENAME folio.svg)
