#pragma once

#include "Utils.h"
#include <algorithm>
#include <cmath>

namespace DreamyBlur {

struct Params {
  bool enable;

  // New Param Mapping for Struct:
  double blurRadius; // (Handled by spatial loop, but passed here?) No, spatial
                     // loop uses it.
  double strength;   // dreamySoftLightStrength
  double shadowAmt;  // dreamyShadowAmount
  double highlightAmt;  // dreamyHighlightAmount
  double tonalSoftness; // dreamyTonalSoftness
  double saturation;    // dreamySaturation
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

inline void applyDreamyBlur(float &r, float &g, float &b, float blurredR,
                            float blurredG, float blurredB,
                            const Params &params) {
  if (!params.enable)
    return;

  // 1. Base = Input (r,g,b). Blend = Blurred (blurredR,g,b).
  // Compute Luminances
  float lumaBase = 0.2126f * r + 0.7152f * g + 0.0722f * b;
  float lumaBlend =
      0.2126f * blurredR + 0.7152f * blurredG + 0.0722f * blurredB;

  // Apply Soft Light to Luminance
  float lumaResult = softLight(lumaBase, lumaBlend);

  // Hue-preserving blend
  float ratio = 1.0f;
  if (lumaBase > 0.0001f) {
    ratio = lumaResult / lumaBase;
  }

  float slR = r * ratio;
  float slG = g * ratio;
  float slB = b * ratio;

  // 2. Saturation Control
  float sat = (float)params.saturation;
  if (std::abs(sat - 1.0f) > 0.001f) {
    slR = lumaResult + (slR - lumaResult) * sat;
    slG = lumaResult + (slG - lumaResult) * sat;
    slB = lumaResult + (slB - lumaResult) * sat;
  }

  // 3. Tonal Masking
  float width = 0.2f + 0.8f * (float)params.tonalSoftness;
  float shadowWeight = 1.0f - Utils::smoothstep(0.0f, width, lumaBase);
  float highlightWeight = Utils::smoothstep(1.0f - width, 1.0f, lumaBase);
  float maskVal = shadowWeight * (float)params.shadowAmt +
                  highlightWeight * (float)params.highlightAmt;

  // 4. Final Blend
  float finalMix = maskVal * (float)params.strength;

  r = Utils::mix(r, slR, finalMix);
  g = Utils::mix(g, slG, finalMix);
  b = Utils::mix(b, slB, finalMix);
}

} // namespace DreamyBlur
