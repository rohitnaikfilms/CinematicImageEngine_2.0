# Cinematic Image Engine

A modular cinematic image-processing pipeline for **DaVinci Resolve** (Free & Studio), built as an OpenFX plugin. Applies photographic colour science, spatial effects, and film-emulation in a single node on the Colour Page.

---

## Modules

| # | Module | Description |
|---|--------|-------------|
| 1 | **Color Ingest** | Exposure trim, chroma ceiling, highlight white bias |
| 2 | **Film Response** | Photochemical hue/sat behaviour by luminance zone (shadow cool, midtone focus, highlight warmth + compression) |
| 3 | **Tonal Engine** | Power-curve contrast around a pivot, with black floor |
| 4 | **Color Energy** | Subtractive density simulation + chroma separation, attenuated at luminance extremes |
| 5 | **Highlight Protection** | Asymptotic superwhite compression (per-channel or luminance-preserving) |
| 6 | **Split Toning** | Hue-angle tinting for shadows and highlights with balance control |
| 7 | **Film Grain** | Resolution-independent grain with presets (8mm, 16mm, S16, 35mm, 65mm, Clean) and per-zone weighting |
| 8 | **Dreamy Mist** | Highlight diffusion — achromatic light scatter with warmth/depth control |
| 9 | **Dreamy Blur** | Soft-light luminance blend with tonal masking (shadow/highlight amount) |
| 10 | **Cinematic Glow** | Threshold-based bloom with colour fidelity and warmth |
| 11 | **Sharpening** | Four modes (Soft Detail, Micro Contrast, Edge Aware, Deconvolution) with noise suppression and tonal protection |
| 12 | **Halation** | Red-channel highlight bleed with warmth and saturation control |
| 13 | **Vignette** | Dark, light, or defocus vignette with roundness, size, and softness controls |

Every module has an **Enable** toggle. When all modules are disabled, the plugin passes through at zero cost.

---

## Compatibility

| | Supported |
|---|---|
| **Host** | DaVinci Resolve 17 / 18 / 19 (Free & Studio) — Color Page |
| **macOS** | Apple Silicon (arm64) and Intel (x86_64) — universal binary |
| **Linux** | x86_64 |
| **OpenFX** | 1.4 |

---

## Prerequisites

- **CMake** 3.10 or newer
- **C++ compiler** — Xcode Command Line Tools (macOS) or GCC/Clang (Linux)
- **OpenFX 1.4 headers** — included in the repository under `OpenFX-1.4/`

### Install Xcode Command Line Tools (macOS)

```bash
xcode-select --install
```

### Install CMake (if not present)

```bash
# macOS (Homebrew)
brew install cmake

# Ubuntu / Debian
sudo apt install cmake build-essential
```

---

## Build

From the repository root:

```bash
cd CinematicImageEngine
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(sysctl -n hw.ncpu)        # macOS
# make -j$(nproc)                  # Linux
```

This produces the OFX bundle at:

```
build/CinematicImageEngine.ofx.bundle/
├── Contents/
│   ├── Info.plist
│   └── MacOS/
│       └── CinematicImageEngine.ofx
```

### Quick Build + Install (macOS)

A convenience script is provided:

```bash
cd CinematicImageEngine
chmod +x build_and_install.sh
./build_and_install.sh
```

This cleans, builds in Release mode, and copies the bundle to `/Library/OFX/Plugins/`. Requires `sudo` for the install step.

---

## Install

### macOS

Copy the bundle to the system OFX directory:

```bash
sudo cp -r build/CinematicImageEngine.ofx.bundle /Library/OFX/Plugins/
```

### Linux

```bash
sudo mkdir -p /usr/OFX/Plugins
sudo cp -r build/CinematicImageEngine.ofx.bundle /usr/OFX/Plugins/
```

**Restart DaVinci Resolve** after installing.

---

## Usage in DaVinci Resolve

1. Open the **Color Page**
2. Right-click the node graph → **Add Node** → **Add Serial Node** (or press `Alt+S`)
3. Open the **OpenFX** panel (top-right)
4. Find **Cinematic Image Engine** under the **ColormetricLabs** group
5. Drag it onto the new node
6. Expand each module group in the inspector and adjust controls

> **Tip:** Disable any modules you're not using — the plugin skips them entirely for maximum performance.

---

## Uninstall

```bash
# macOS
sudo rm -rf /Library/OFX/Plugins/CinematicImageEngine.ofx.bundle

# Linux
sudo rm -rf /usr/OFX/Plugins/CinematicImageEngine.ofx.bundle
```

Restart DaVinci Resolve.

---

## Performance Notes

- All spatial effects (Mist, Blur, Glow, Halation, Sharpening) use an **O(N) box-blur approximation** of Gaussian — constant time regardless of radius.
- Built with `-O3 -ffast-math -funroll-loops -flto` in Release mode.
- Vertical blur uses cache-friendly strip-based processing to avoid L1 thrashing at 4K+.
- Universal binary (arm64 + x86_64) with no runtime architecture checks.

---

## Version

**1.3** — ColormetricLabs
