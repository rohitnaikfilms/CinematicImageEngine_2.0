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
echo "[2/2] Installing to /Library/OFX/Plugins..."
sudo cp -r CinematicImageEngine.ofx.bundle /Library/OFX/Plugins/

echo ""
echo "✅ Done. Restart DaVinci Resolve to load the plugin."
