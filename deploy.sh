#!/bin/bash
# deploy.sh — Build, install, and hot-reload in Ableton
#
# Usage: ./deploy.sh

set -e

PLUGIN_NAME="KawaiiK50000SV"
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
INSTALL_DIR="/Library/Audio/Plug-Ins/VST3"
BUNDLE="${INSTALL_DIR}/${PLUGIN_NAME}.vst3"
ABLETON_APP="/Applications/Ableton Live 12 Suite.app"
ABLETON_PROJECT="/Users/xenonbug/Documents/ableton sets/basic loopback Project/basic loopback.als"

# --- Build ---
echo "=== Building ==="
cd "$BUILD_DIR"
cmake --build . --config Release 2>&1
echo ""

# --- Quit Ableton ---
if pgrep -xq "Live"; then
    echo "=== Quitting Ableton ==="

    # Clean quit via Accessibility API.
    # Handles: crash recovery dialog → "No", save dialog → "Don't Save".
    "${PROJECT_DIR}/scripts/quit_live" || true  # Don't abort if it times out

    # Wait for exit (quit_live may return before process fully exits)
    WAIT_START=$SECONDS
    while pgrep -xq "Live"; do
        sleep 1
    done

    echo "  Done. (process exited after $((SECONDS - WAIT_START))s)"
    echo ""
fi

# --- Install ---
echo "=== Installing to ${INSTALL_DIR} ==="
rm -rf "$BUNDLE"
cp -r "${BUILD_DIR}/VST3/${PLUGIN_NAME}.vst3" "$INSTALL_DIR/"
xattr -cr "$BUNDLE"
codesign --force --deep --sign - "$BUNDLE"
SetFile -a B "$BUNDLE"

rm -rf "$HOME/Library/Caches/Ableton/Cache" 2>/dev/null

codesign -v "$BUNDLE" 2>&1 && echo "Codesign: valid" || echo "Codesign: FAILED"
echo ""

# --- Relaunch ---
if [ -d "$ABLETON_APP" ]; then
    echo "=== Relaunching Ableton ==="
    if [ -f "$ABLETON_PROJECT" ]; then
        echo "  Opening: $(basename "$ABLETON_PROJECT")"
        open "$ABLETON_PROJECT"
    else
        echo "  Project not found: $ABLETON_PROJECT"
        open "$ABLETON_APP"
    fi

    # Dismiss crash recovery dialog if it appears on launch
    echo "  Waiting for Ableton to load..."
    "${PROJECT_DIR}/scripts/dismiss_recovery"
else
    echo "Ableton not found at ${ABLETON_APP}"
fi

echo "=== Done ==="
