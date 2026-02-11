#pragma once

#include "Utils.h"
#include <cmath>
#include <algorithm>

namespace CinematicGlow {

struct Params {
    bool enable;
    double amount;
    double threshold;
    double knee;
    double radius;
    double colorFidelity;
    double warmth;
};

// Removed skinMask param to match call site or updated call site?
// Call site: CinematicGlow::computeGlowSource(s[0], s[1], s[2], gR, gG, gB, glow);
// Header has: float r, float g, float b, float skinMask, ...
// The call site is missing skinMask.
// The new structure removes skin preservation logic? Or implicit?
// The prompt removed Skin Preservation MODULE.
// But we still have `skinMask` passed around?
// In `PipelineProcessor::multiThreadProcessImages`, I am NOT computing a skin mask anymore in Stage 0.
// `bufA` stores 1.0f in alpha.
// So `skinMask` is not available.
// I should remove `skinMask` from `computeGlowSource`.

inline void computeGlowSource(float r, float g, float b, float& outR, float& outG, float& outB, const Params& params) {
    if (!params.enable) {
        outR = outG = outB = 0.0f;
        return;
    }

    // 1. Luminance
    float L = Utils::getLuminance(r, g, b);

    // 2. Highlight Mask
    float mask = Utils::smoothstep((float)params.threshold, (float)params.threshold + (float)params.knee + 0.001f, L);

    // 3. Color Response
    float srcR = r * mask;
    float srcG = g * mask;
    float srcB = b * mask;

    float lumR = L * mask;
    float lumG = L * mask;
    float lumB = L * mask;

    float f = (float)params.colorFidelity;
    float baseR = Utils::mix(lumR, srcR, f);
    float baseG = Utils::mix(lumG, srcG, f);
    float baseB = Utils::mix(lumB, srcB, f);

    // 4. Warmth Bias
    float w = (float)params.warmth;
    if (w > 0) {
        baseR *= (1.0f + w * 0.5f);
        baseB *= (1.0f - w * 0.2f);
    } else if (w < 0) {
        baseB *= (1.0f + std::abs(w) * 0.5f);
        baseR *= (1.0f - std::abs(w) * 0.2f);
    }

    outR = baseR;
    outG = baseG;
    outB = baseB;
}

inline void applyGlow(float& r, float& g, float& b, float glowR, float glowG, float glowB, const Params& params) {
    if (!params.enable) return;

    float a = (float)params.amount;
    r += glowR * a;
    g += glowG * a;
    b += glowB * a;
}

}
