#!/bin/bash
set -e

echo "========================================="
echo " Cinematic Image Engine — Build & Install"
echo "========================================="

# Clean previous build
rm -rf build

# Create build directory and configure
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..

# Build with all available cores
echo ""
echo "[1/2] Building..."
if command -v sysctl &> /dev/null; then
    make -j$(sysctl -n hw.ncpu)
else
    make -j$(nproc)
fi

echo ""
echo "[2/3] Installing to /Library/OFX/Plugins..."
sudo cp -r CinematicImageEngine.ofx.bundle /Library/OFX/Plugins/
sudo chmod -R 755 /Library/OFX/Plugins/CinematicImageEngine.ofx.bundle

echo ""
echo "[3/3] Clearing DaVinci Resolve OFX plugin cache..."
RESOLVE_SUPPORT="$HOME/Library/Application Support/Blackmagic Design/DaVinci Resolve"
if [ -d "$RESOLVE_SUPPORT" ]; then
    rm -f "$RESOLVE_SUPPORT/OFXPluginCacheV2.xml" 2>/dev/null && echo "  ✓ Removed OFXPluginCacheV2.xml"
    rm -f "$RESOLVE_SUPPORT/OFXPluginCache.xml"   2>/dev/null && echo "  ✓ Removed OFXPluginCache.xml"
else
    echo "  ⚠ Resolve support directory not found — cache not cleared."
fi

echo ""
echo "✅ Done. Restart DaVinci Resolve to load the plugin."
