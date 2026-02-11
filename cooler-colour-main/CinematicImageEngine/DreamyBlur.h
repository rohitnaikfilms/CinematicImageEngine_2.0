#pragma once

#include "Utils.h"
#include <cmath>
#include <algorithm>

namespace DreamyBlur {

struct Params {
    bool enable;

    // New Param Mapping for Struct:
    double blurRadius;      // (Handled by spatial loop, but passed here?) No, spatial loop uses it.
    double strength;        // dreamySoftLightStrength
    double shadowAmt;       // dreamyShadowAmount
    double highlightAmt;    // dreamyHighlightAmount
    double tonalSoftness;   // dreamyTonalSoftness
    double saturation;      // dreamySaturation
};

// Helper: Soft Light Logic (Scalar)
inline float softLight(float base, float blend) {
    if (blend < 0.5f) {
        return base - (1.0f - 2.0f * blend) * base * (1.0f - base);
    } else {
        // sqrt approximation or actual sqrt? Spec says sqrt.
        // guard negative
        float s = (base > 0.0f) ? std::sqrt(base) : 0.0f;
        return base + (2.0f * blend - 1.0f) * (s - base);
    }
}

inline void applyDreamyBlur(float& r, float& g, float& b, float blurredR, float blurredG, float blurredB, float skinMask, const Params& params) {
    if (!params.enable) return;

    // 1. Base = Input (r,g,b). Blend = Blurred (blurredR,g,b).
    // Spec: "Soft Light Blend... Apply using hue-preserving scaling"

    // Compute Luminances
    float lumaBase = 0.2126f * r + 0.7152f * g + 0.0722f * b;
    float lumaBlend = 0.2126f * blurredR + 0.7152f * blurredG + 0.0722f * blurredB;

    // Apply Soft Light to Luminance
    float lumaResult = softLight(lumaBase, lumaBlend);

    // Hue-preserving blend:
    // New RGB = Base RGB * (LumaResult / LumaBase)
    // Guard div by zero
    float ratio = 1.0f;
    if (lumaBase > 0.0001f) {
        ratio = lumaResult / lumaBase;
    }

    float slR = r * ratio;
    float slG = g * ratio;
    float slB = b * ratio;

    // 2. Saturation Control
    // "Saturation of the blended result is controllable"
    // Apply saturation to slRGB
    // Luma is lumaResult.
    float sat = (float)params.saturation;
    if (std::abs(sat - 1.0f) > 0.001f) {
        slR = lumaResult + (slR - lumaResult) * sat;
        slG = lumaResult + (slG - lumaResult) * sat;
        slB = lumaResult + (slB - lumaResult) * sat;
    }

    // 3. Tonal Masking
    // "shadowMask = smooth shadow weighting from luma"
    // "highlightMask = smooth highlight weighting from luma"
    // "tonalMask = shadowMask * shadowAmount + highlightMask * highlightAmount"

    // Define zones. Smoothstep ramps modulated by tonalSoftness.
    // If tonalSoftness is high (1.0), use linear or very smooth ramp.
    // If tonalSoftness is low (0.0), use sharper ramp (but still smoothstep).
    // We can modulate the edge definitions.

    // Default Shadow: 1 at 0, 0 at 0.5?
    // User wants "Smooth".
    // Let's use smoothstep.
    // Shadow Ramp: 1.0 -> 0.0.
    // Edge0 = 0.0. Edge1 = modulated by softness.
    // If softness is 1.0, Edge1 = 1.0 (Full range linear-ish).
    // If softness is 0.0, Edge1 = 0.2 (Tight shadow range).
    // Let's assume broad shadows are standard.
    // Spec: "Smooth (no hard thresholds)".

    // Let's map tonalSoftness to the transition width.
    // 0.0 = Harder transition (narrower zone or steeper curve?)
    // Usually softness implies width.
    // Shadow Weight:
    // Base range 0.5.
    // Width = 0.2 + 0.8 * softness.
    float width = 0.2f + 0.8f * (float)params.tonalSoftness;

    // Shadow Weight: 1 at 0, 0 at width.
    // smoothstep(width, 0, luma) is not standard.
    // 1.0 - smoothstep(0, width, luma).
    float shadowWeight = 1.0f - Utils::smoothstep(0.0f, width, lumaBase);

    // Highlight Weight:
    // 0 at (1-width), 1 at 1.
    float highlightWeight = Utils::smoothstep(1.0f - width, 1.0f, lumaBase);

    // Mix them
    float maskVal = shadowWeight * (float)params.shadowAmt + highlightWeight * (float)params.highlightAmt;

    // 4. Final Blend
    // blended = lerp(base, softLightResult, tonalMask * softLightStrength)
    float finalMix = maskVal * (float)params.strength;

    // Skin Preservation Attenuation (Standard practice, implied by context "attenuate downstream effects")
    if (skinMask > 0.0f) {
        finalMix *= (1.0f - skinMask);
    }

    r = Utils::mix(r, slR, finalMix);
    g = Utils::mix(g, slG, finalMix);
    b = Utils::mix(b, slB, finalMix);
}

}
