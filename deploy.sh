#!/bin/bash
# deploy.sh — Build, install, and hot-reload in Ableton
#
# Usage: ./deploy.sh
#
# Ableton caches plugin binaries in memory — a simple rescan won't reload
# new code. This script handles that by gracefully quitting Ableton,
# deploying the new build, and relaunching it.

set -e

PLUGIN_NAME="KawaiiK50000SV"
PROJECT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="${PROJECT_DIR}/build"
INSTALL_DIR="/Library/Audio/Plug-Ins/VST3"
BUNDLE="${INSTALL_DIR}/${PLUGIN_NAME}.vst3"
ABLETON_APP="/Applications/Ableton Live 12 Suite.app"

# --- Build ---
echo "=== Building ==="
cd "$BUILD_DIR"
cmake --build . --config Release 2>&1
echo ""

# --- Quit Ableton if running ---
if pgrep -f "Ableton" > /dev/null 2>&1; then
    echo "=== Killing Ableton ==="
    pkill -9 -f "Ableton" 2>/dev/null || true
    sleep 1
    echo "Ableton killed."
    echo ""
fi

# --- Install ---
echo "=== Installing to ${INSTALL_DIR} ==="
rm -rf "$BUNDLE"
cp -r "${BUILD_DIR}/VST3/${PLUGIN_NAME}.vst3" "$INSTALL_DIR/"
xattr -cr "$BUNDLE"
codesign --force --deep --sign - "$BUNDLE"
SetFile -a B "$BUNDLE"

# Clear Ableton caches
rm -rf "$HOME/Library/Caches/Ableton/Cache" 2>/dev/null

codesign -v "$BUNDLE" 2>&1 && echo "Codesign: valid" || echo "Codesign: FAILED"
echo ""

# --- Relaunch Ableton ---
if [ -d "$ABLETON_APP" ]; then
    echo "=== Relaunching Ableton ==="
    open "$ABLETON_APP"
else
    echo "Ableton not found at ${ABLETON_APP}"
    echo "Open it manually."
fi

echo "=== Done ==="
