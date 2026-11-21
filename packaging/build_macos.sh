#!/usr/bin/env bash
# Configure, build, test and verify ShutterFX on macOS.
# Usage: packaging/build_macos.sh [Release|Debug] [extra cmake -D options...]
# Default is a universal (arm64 + x86_64) build. For Intel-only: -DCMAKE_OSX_ARCHITECTURES=x86_64
set -euo pipefail

CONFIG="${1:-Release}"; [ $# -gt 0 ] && shift
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD="$ROOT/build"

[ "$(uname -s)" = "Darwin" ] || { echo "build_macos.sh must run on macOS." >&2; exit 1; }
command -v cmake >/dev/null || { echo "cmake not found (brew install cmake)." >&2; exit 1; }
xcode-select -p >/dev/null 2>&1 || { echo "Xcode Command Line Tools missing: xcode-select --install" >&2; exit 1; }
[ -f "$ROOT/external/openfx/include/ofxCore.h" ] || git -C "$ROOT" submodule update --init --recursive

GEN=(); command -v ninja >/dev/null && GEN=(-G Ninja)
# One build directory per config so Debug and Release bundles can coexist.
cmake -S "$ROOT" -B "$BUILD/$CONFIG-cmake" "${GEN[@]}" -DCMAKE_BUILD_TYPE="$CONFIG" \
      "$@"
cmake --build "$BUILD/$CONFIG-cmake" --parallel
( cd "$BUILD/$CONFIG-cmake" && ctest --output-on-failure )

# The bundle is emitted under <build-dir>/<Config>/. Mirror it to build/<Config>/ for install.sh.
SRC="$BUILD/$CONFIG-cmake/$CONFIG/ShutterFX.ofx.bundle"
mkdir -p "$BUILD/$CONFIG"; rm -rf "$BUILD/$CONFIG/ShutterFX.ofx.bundle"
ditto "$SRC" "$BUILD/$CONFIG/ShutterFX.ofx.bundle"
"$ROOT/packaging/verify_bundle.sh" "$BUILD/$CONFIG/ShutterFX.ofx.bundle"
echo
echo "Built: $BUILD/$CONFIG/ShutterFX.ofx.bundle"
echo "Install with: packaging/install.sh"
