# Cinematic Image Engine — Technical Reference Manual

## 1. Overview

The **Cinematic Image Engine** is a high-performance, modular OpenFX plugin for DaVinci Resolve designed to emulate the photochemical and optical characteristics of motion picture film.

The engine operates in a strict, ordered pipeline of 13 modules across two processing stages. It relies on DaVinci Resolve's Color Management (RCM) or Color Space Transforms (CST) for initial decoding and linearization.

**Version:** 1.3  
**Platform:** macOS (arm64 / x86_64 universal), Linux (x86_64)  
**Host:** DaVinci Resolve 17 / 18 / 19 (Color Page)

---

## 2. Pipeline Architecture

The pipeline executes in a **FIXED CANONICAL ORDER**. This order is mathematically critical and cannot be changed by the user.

```
┌─────────────────────────────────────────────────────────────┐
│  HOST: Resolve CST / RCM → Linear Working Space            │
└────────────────────────┬────────────────────────────────────┘
                         ▼
    ┌─── STAGE 0: Per-Pixel Processing (O(1)/pixel) ───┐
    │                                                    │
    │  1. Color Ingest Tweaks    → Exposure, Ceiling     │
    │  2. Film Response (PCR)    → Hue/Sat by zone       │
    │  3. Tonal Engine           → Contrast/Pivot        │
    │  4. Color Energy           → Density/Separation    │
    │  5. Highlight Protection   → Superwhite compress   │
    │  6. Split Toning           → Chromatic bias        │
    │  7. Film Grain             → Photochemical noise   │
    │                                                    │
    └────────────────────┬───────────────────────────────┘
                         ▼
    ┌─── STAGE 1: Spatial Processing (O(N)) ───────────┐
    │                                                    │
    │  8.  Dreamy Mist     → Highlight diffusion         │
    │  9.  Dreamy Blur     → Soft-light luminance blend  │
    │  10. Cinematic Glow  → Threshold bloom             │
    │  11. Sharpening      → Detail enhancement          │
    │  12. Halation         → Film gate scatter           │
    │  13. Vignette         → Lens falloff / defocus      │
    │                                                    │
    └────────────────────────────────────────────────────┘
```

### Identity Bypass

When all modules are disabled or at their default (neutral) values, `isIdentity()` returns `true` and the host passes the source frame through at **zero processing cost**. Individual module enable toggles also skip that module's computation entirely.

---

## 3. Performance

### Stage 0 — Per-Pixel (Fast)

- **Complexity:** O(1) per pixel. Cost scales linearly with resolution only.
- **Optimizations applied:**
  - `exp2f()` replaces `std::pow(2, x)` for exposure gain (single instruction on ARM/x86).
  - Split Toning hue vectors (`sin`/`cos`) pre-computed once per frame, not per pixel.
  - Final output uses bulk `memcpy` per row instead of per-pixel copy.

### Stage 1 — Spatial (Expensive but O(N))

- **Complexity:** O(N), **not** O(R²). All blur-based modules use a 3-pass iterated box blur approximation of Gaussian, making cost **independent of radius**.
- **Cache optimisation:** Vertical blur uses strip-based processing (8-column tiles, 128-byte working set) to avoid L1 cache thrashing at 4K+.
- **Pointer annotations:** `__restrict__` on blur functions enables compiler auto-vectorisation.
- **Impact:** Spatial modules increase the Region of Interest (apron) fetched from the host.

### Build-Level Optimisation

| Flag | Effect |
|------|--------|
| `-O3` | Maximum instruction-level optimisation |
| `-ffast-math` | IEEE-relaxed float for speed |
| `-funroll-loops` | Reduces branch overhead in tight loops |
| `-flto` | Link-Time Optimisation — cross-TU inlining between plugin and OFX Support Library |

---

## 4. Module Reference

### 1. Color Ingest Tweaks (CIT)

**Role:** Lightweight perceptual conditioning immediately after Resolve's CST.

| Control | Math |
|---------|------|
| Exposure Trim | $RGB \times 2^{trim}$ via `exp2f()` |
| Chroma Ceiling | Soft compression of extreme saturation vectors (neon suppression) |
| Highlight White Bias | Chroma vector addition at high luminance (cool/warm white point shift) |

### 2. Photochemical Color Response (PCR)

**Role:** Emulates film colour behaviour (Hue & Saturation) driven strictly by luminance.  
**Strict Scope:** NO contrast or compression curves.

Operates on chroma ($C = RGB - Y$), weighted by luminance zone via `smoothstep` transitions:

- **Shadows:** Cool bias + desaturation
- **Midtones:** Saturation peak
- **Highlights:** Warm bias + channel-aware desaturation (dye exhaustion)

**Controls:** Amount, Shadow Density/Coolness, Midtone Saturation/Hue, Highlight Warmth/Compression.

### 3. Tonal Engine

**Role:** The sole module for luminance shaping and contrast.

$$L_{out} = Pivot \times (L_{in} / Pivot)^{Contrast}$$

- **Black Floor:** Hard limits the lowest black level — $\max(L_{out}, Floor)$.
- **Strength:** Blends between the original and processed luminance.

**Controls:** Contrast, Pivot, Strength, Black Floor.

### 4. Color Energy Engine

**Role:** Controls the relationship between chroma energy and luminance.

| Function | Math |
|----------|------|
| Density | $C_{dense} = C^{power}$ — deepens colours as they saturate |
| Separation | Expands chroma vectors away from neutral |
| Attenuation | Rolled off at shadow and highlight extremes via luminance-weighted masks |

**Division safety:** `highlightRollOff` is guarded against zero to prevent crashing.

**Controls:** Density, Separation, Highlight Rolloff, Shadow Bias.

### 5. Highlight Protection

**Role:** Luminance-only super-white compression.

$$f(x) = \frac{x}{1 + kx}$$

| Mode | Behaviour |
|------|-----------|
| Preserve Color ON | Scales RGB ratios (natural desaturation to white) |
| Preserve Color OFF | Compresses channels individually |

**Controls:** Threshold, Rolloff, Preserve Color.

### 6. Split Toning

**Role:** Applies subtle chromatic bias to shadows and highlights.

- Hue angles are converted to Pb/Pr chroma vectors via `cos`/`sin` — **pre-computed once per frame**, not per pixel.
- Vectors are weighted by luminance position and blended additively.
- Balance control shifts emphasis between shadow and highlight tinting.

**Controls:** Strength, Shadow Hue, Highlight Hue, Balance.

### 7. Film Grain

**Role:** Photochemical density noise.

| Property | Detail |
|----------|--------|
| Blend mode | Multiplicative: $Pixel \times (1 + Grain)$ |
| Colour | Monochromatic |
| Scale | Resolution-independent (scales with image dimensions) |
| Zone weighting | Separate shadow, midtone, highlight intensity |

**Presets:** 8mm, 16mm, Super 16, 35mm, 65mm, Clean.

**Controls:** Type, Amount, Size, Shadow/Mid/Highlight Weight.

### 8. Dreamy Mist

**Role:** Highlight diffusion — achromatic light scatter.

Isolates highlights using luminance thresholding (`smoothstep`), applies Gaussian blur, then blends additively. Depth bias shifts the effect towards shadows or highlights. Warmth adds a colour tint to the mist layer.

**Controls:** Amount, Threshold, Softness, Depth Bias, Warmth.

### 9. Dreamy Blur

**Role:** Optical softening via soft-light luminance blend.

Creates a blurred copy of the image, calculates a soft-light blend with the original, then applies a tonal mask to limit the effect to specific luminance regions.

**Controls:** Radius, Strength, Shadow Amount, Highlight Amount, Tonal Softness, Saturation.

### 10. Cinematic Glow

**Role:** Specular emission bloom.

Isolates highlights above a threshold (with adjustable knee), blurs, and blends additively. Colour fidelity controls how much of the original hue is preserved vs. pure white emission.

**Controls:** Amount, Threshold, Knee, Radius, Fidelity, Warmth.

### 11. Sharpening

**Role:** Detail enhancement with four algorithm modes.

| Mode | Approach |
|------|----------|
| Soft Detail | Gentle unsharp mask |
| Micro Contrast | Local contrast enhancement |
| Edge Aware | Gradient-weighted to protect edges |
| Deconvolution | Iterative detail recovery |

All modes include noise suppression (threshold gating), edge protection, and tonal protection (shadow/highlight roll-off).

**Controls:** Type, Amount, Radius, Detail, Edge Protection, Noise Suppression, Shadow/Highlight Protection.

### 12. Halation

**Role:** Film gate scatter — red-channel highlight bleed.

Energy is derived primarily from the red channel (matching physical halation in film). Warmth controls green contribution (pure red → orange/yellow). The blur radius simulates scatter distance.

- **Saturation control:** Desaturates the halation contribution before blending (1.0 = full colour, 0.0 = luminance only).

**Controls:** Amount, Threshold, Knee, Warmth, Radius, Saturation.

### 13. Vignette

**Role:** Lens falloff simulation.

| Mode | Effect |
|------|--------|
| Dark | Traditional darkening vignette |
| Light | Brightening vignette (flash falloff) |
| Defocus | Blur-based optical vignette |

Mask is computed from normalised UV distance with aspect ratio correction. Roundness interpolates between elliptical and circular shapes.

**Controls:** Type, Amount, Invert, Size, Roundness, Softness, Defocus, Defocus Softness.

---

## 5. Build & Installation

See [README.md](CinematicImageEngine/README.md) for prerequisites, build steps, and installation instructions.

Quick reference:

```bash
cd CinematicImageEngine
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(sysctl -n hw.ncpu)
sudo cp -r CinematicImageEngine.ofx.bundle /Library/OFX/Plugins/
```

---

## 6. Development Constraints

| Constraint | Detail |
|------------|--------|
| **Input controls** | Sliders, toggles, dropdowns only — no manual text entry |
| **Precision** | 32-bit float throughout |
| **Concurrency** | Built on OFX `ImageProcessor` multi-threaded model |
| **Standard** | C++14 (constrained by OFX Support Library's `throw()` specs) |
| **Dependencies** | OpenFX 1.4 C++ Support Library (included in repo) |
