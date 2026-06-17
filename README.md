# Folio

A focused writing studio for novelists, screenwriters, and storytellers of
every kind. Folio is a native GTK 4 / gtkmm desktop application for drafting,
organizing, and compiling long-form manuscripts — with a binder-style project
sidebar, an inspector, snapshots, a distraction-free focus mode, spell
checking, and a PDF/compile export pipeline.

## Features

- **Project binder** — organize a manuscript into a tree of folders, scenes,
  characters, places, and references.
- **Rich text editor** — formatting, rulers, screenplay support, smart text
  substitution (curly quotes, em dashes, ellipses), and an inline Unicode
  character picker.
- **Inspector & snapshots** — per-document metadata plus point-in-time
  snapshots with side-by-side diffing.
- **Focus mode** — a distraction-free writing view.
- **Spell checking** — live spell check via Enchant.
- **Search** — project-wide search across all documents.
- **Compile & export** — export to PDF and other formats through a configurable
  compile-format pipeline, with optional automatic hyphenation.
- **Writing tools** — Pomodoro timer, project word-count goals, annotation
  reports, and backups.

## Requirements

Folio is built and tested on Linux. You will need:

- A C++ compiler with C++20 support (Clang preferred; GCC works)
- CMake ≥ 3.20
- pkg-config
- gtkmm-4.0
- enchant-2
- librsvg-2.0
- freetype2
- zlib

The following are fetched automatically at configure time if not found on the
system:

- [nlohmann/json](https://github.com/nlohmann/json) (v3.11.3)
- [spdlog](https://github.com/gabime/spdlog) (v1.15.3, header-only)

Optional:

- **libhyphen** — enables automatic hyphenation in the PDF/compile pipeline.
  Without it, Folio still builds and runs; only automatic hyphenation is
  compiled out (manual soft hyphens are unaffected).

### Installing dependencies

**Ubuntu / Debian**

```bash
sudo apt install build-essential cmake pkg-config \
    libgtkmm-4.0-dev libenchant-2-dev librsvg2-dev libfreetype-dev zlib1g-dev
# optional (automatic hyphenation):
sudo apt install libhyphen-dev hyphen-en-us
```

**Fedora / RHEL**

```bash
sudo dnf install gcc-c++ cmake pkgconf-pkg-config \
    gtkmm4.0-devel enchant2-devel librsvg2-devel freetype-devel zlib-devel
# optional (automatic hyphenation):
sudo dnf install hyphen-devel hyphen-en
```

**Arch Linux**

```bash
sudo pacman -S base-devel cmake pkgconf gtkmm-4.0 enchant librsvg freetype2 zlib
```

**openSUSE**

```bash
sudo zypper install gcc-c++ cmake pkg-config \
    gtkmm4-devel enchant-devel librsvg-devel freetype2-devel zlib-devel
```

## Building

The simplest path uses the included build script, which checks dependencies and
produces a Release build:

```bash
./build.sh
```

Then run:

```bash
./build/folio
```

### Building manually with CMake

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
./build/folio
```

By default (no `CMAKE_BUILD_TYPE` given) CMake configures a **Debug** build with
assertions live; `build.sh` passes `Release`.

### Build options

- `-DCMAKE_BUILD_TYPE=Release` — optimized build (default in `build.sh`).
- `-DFOLIO_DEBUG_LOG=OFF` — strip `LOG_DEBUG`/`SPDLOG_DEBUG` calls at compile
  time (default `ON`).

### Note on generated resources

Application icons in `resources/icons/` are compiled into the binary via
`glib-compile-resources` at build time, producing `src/folio-resources.c` and
`include/folio-resources.h`. These generated files are not checked into the
repository — CMake regenerates them on every build. To add an icon: drop the
`.svg` into `resources/icons/`, add a `<file>` entry to
`resources/resources.xml`, and rebuild.

## License

Folio is released under the MIT License. See [LICENSE](LICENSE) for details.

Copyright (c) 2026 Scott Combs
