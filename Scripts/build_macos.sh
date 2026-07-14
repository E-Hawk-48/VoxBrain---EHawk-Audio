#!/usr/bin/env bash
# ============================================================================
#  build_macos.sh — build VocalForge for macOS (VST3 + AU + Standalone).
#
#  Runs on a Mac (your friend's, or a GitHub Actions macOS runner). It cannot
#  run on Windows — macOS binaries must be compiled on macOS.
#
#  Requirements: Xcode command-line tools (`xcode-select --install`) and
#  CMake 3.22+ (`brew install cmake`).
#
#  Produces a UNIVERSAL build (Apple Silicon + Intel) so one download runs on
#  every Mac. Neural CREPE analysis is Windows-only for now (VF_HAS_ONNX=0 on
#  macOS); everything else — auto-mix, pitch, saturation, reverb, updater —
#  works identically.
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/.."

BUILD_DIR="build-mac"

cmake -B "$BUILD_DIR" -G Xcode \
    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
    -DCMAKE_OSX_DEPLOYMENT_TARGET=10.13

cmake --build "$BUILD_DIR" --config Release --parallel

echo ""
echo "Build complete. Artefacts:"
echo "  $BUILD_DIR/VocalForge_artefacts/Release/VST3/VocalForge.vst3"
echo "  $BUILD_DIR/VocalForge_artefacts/Release/AU/VocalForge.component"
echo "  $BUILD_DIR/VocalForge_artefacts/Release/Standalone/VocalForge.app"
echo ""
echo "Next: bash Scripts/package_macos.sh <version>   # makes the .pkg installer"
