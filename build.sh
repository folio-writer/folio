#!/usr/bin/env bash
# ─────────────────────────────────────────────────────────────────────────────
# Folio — GTK 4 Writing Studio
# Build script
# ─────────────────────────────────────────────────────────────────────────────

set -e

# Check for required tools
check_dep() {
    if ! command -v "$1" &>/dev/null; then
        echo "ERROR: '$1' not found. Please install it."
        exit 1
    fi
}

# A C++ compiler must exist. CMake prefers clang (see CMakeLists.txt) and falls
# back to the system default, so accept either clang++ or g++ here.
if ! command -v clang++ &>/dev/null && ! command -v g++ &>/dev/null; then
    echo "ERROR: no C++ compiler found (need clang++ or g++)."
    exit 1
fi
check_dep cmake
check_dep pkg-config

# Check for gtkmm-4.0
if ! pkg-config --exists gtkmm-4.0; then
    echo "ERROR: gtkmm-4.0 not found."
    echo ""
    echo "Install on Ubuntu/Debian:"
    echo "  sudo apt install libgtkmm-4.0-dev"
    echo ""
    echo "Install on Fedora/RHEL:"
    echo "  sudo dnf install gtkmm4.0-devel"
    echo ""
    echo "Install on Arch Linux:"
    echo "  sudo pacman -S gtkmm-4.0"
    echo ""
    echo "Install on openSUSE:"
    echo "  sudo zypper install gtkmm4-devel"
    exit 1
fi

GTKMM_VER=$(pkg-config --modversion gtkmm-4.0)
echo "✓ Found gtkmm-4.0 version: $GTKMM_VER"

# Build
BUILD_DIR="${1:-build}"
echo "Building in: $BUILD_DIR/"
echo ""

mkdir -p "$BUILD_DIR"
cmake \
    -S . \
    -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON

cmake --build "$BUILD_DIR" --parallel "$(nproc)"

echo ""
echo "✓ Build complete! Run with:"
echo "  ./$BUILD_DIR/folio"
