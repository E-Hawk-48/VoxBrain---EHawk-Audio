#!/usr/bin/env bash
# ============================================================================
#  package_macos.sh — build a VoxBrain .pkg installer for macOS.
#
#  Puts the VST3 + AU + Standalone into the standard macOS locations:
#    /Library/Audio/Plug-Ins/VST3/VoxBrain.vst3
#    /Library/Audio/Plug-Ins/Components/VoxBrain.component   (AU)
#    /Applications/VoxBrain.app
#
#  Run build_macos.sh first. Usage:  bash Scripts/package_macos.sh 1.2.0
#
#  Signing/notarisation (optional — needed for a warning-free install on other
#  Macs). If you have an Apple Developer account, set these env vars:
#    CODESIGN_ID   = "Developer ID Application: Your Name (TEAMID)"
#    NOTARY_PROFILE= name of a stored `notarytool store-credentials` profile
#  Without them the .pkg still installs, but the first launch needs a
#  right-click > Open (Gatekeeper). Fine for sending to a friend.
# ============================================================================
set -euo pipefail
cd "$(dirname "$0")/.."

VERSION="${1:-1.0.0}"
ART="build-mac/VoxBrain_artefacts/Release"
STAGE="build-mac/pkg-stage"

if [ ! -d "$ART/VST3/VoxBrain.vst3" ]; then
    echo "ERROR: build artefacts not found. Run: bash Scripts/build_macos.sh" >&2
    exit 1
fi

rm -rf "$STAGE"
mkdir -p "$STAGE/Library/Audio/Plug-Ins/VST3" \
         "$STAGE/Library/Audio/Plug-Ins/Components" \
         "$STAGE/Applications"

cp -R "$ART/VST3/VoxBrain.vst3" "$STAGE/Library/Audio/Plug-Ins/VST3/"
[ -d "$ART/AU/VoxBrain.component" ] && cp -R "$ART/AU/VoxBrain.component" "$STAGE/Library/Audio/Plug-Ins/Components/"
[ -d "$ART/Standalone/VoxBrain.app" ] && cp -R "$ART/Standalone/VoxBrain.app" "$STAGE/Applications/"

# Optional code-signing of each bundle.
if [ -n "${CODESIGN_ID:-}" ]; then
    echo "Signing bundles with: $CODESIGN_ID"
    while IFS= read -r -d '' bundle; do
        codesign --force --deep --options runtime --timestamp --sign "$CODESIGN_ID" "$bundle"
    done < <(find "$STAGE" \( -name "*.vst3" -o -name "*.component" -o -name "*.app" \) -maxdepth 4 -print0)
fi

PKG="build-mac/VoxBrain-$VERSION.pkg"
pkgbuild --root "$STAGE" \
         --identifier com.voxbrainaudio.voxbrain \
         --version "$VERSION" \
         --install-location / \
         "$PKG"

# Optional notarisation (Apple Developer account required).
if [ -n "${NOTARY_PROFILE:-}" ]; then
    echo "Notarising…"
    xcrun notarytool submit "$PKG" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$PKG"
fi

echo ""
echo "Built $PKG"
echo "Send it to your friend. If it's unsigned, they open it the first time with"
echo "right-click > Open (Gatekeeper only warns once)."
