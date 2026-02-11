#pragma once

#include "Utils.h"
#include <algorithm>
#include <cmath>

namespace SplitToning {

struct Params {
  bool enable;
  float strength;
  float shadowHue;
  float highlightHue;
  float balance;
  float midtoneHue;
  float midtoneSaturation; // 0..1, default 0 (off)
  // Pre-computed hue vectors (set once per frame, not per pixel)
  float shadowPb, shadowPr;
  float highlightPb, highlightPr;
  float midtonePb, midtonePr;
};

// Map Hue (Degrees) to unit vector
inline void hueToPbPr(float hueDeg, float &pb, float &pr) {
  float rad = hueDeg * 0.0174532925f; // PI / 180
  pb = std::cos(rad);
  pr = std::sin(rad);
}

// Pre-compute hue vectors — call once per frame before processing pixels
inline void precomputeVectors(Params &params) {
  hueToPbPr(params.shadowHue, params.shadowPb, params.shadowPr);
  hueToPbPr(params.highlightHue, params.highlightPb, params.highlightPr);
  hueToPbPr(params.midtoneHue, params.midtonePb, params.midtonePr);
}

inline void processPixel(float *r, float *g, float *b, const Params &params) {
  if (!params.enable || params.strength <= 0.0f)
    return;

  // 1. LUMINANCE
  float L = 0.2126f * (*r) + 0.7152f * (*g) + 0.0722f * (*b);

  // 2. WEIGHTS — three zones
  float shadowW = 1.0f - Utils::smoothstep(0.0f, 0.4f, L);
  float highlightW = Utils::smoothstep(0.6f, 1.0f, L);
  float midtoneW = (1.0f - shadowW) * (1.0f - highlightW);

  // Balance: -1 (shadows) to +1 (highlights)
  float sFactor = 1.0f - params.balance;
  float hFactor = 1.0f + params.balance;
  shadowW *= sFactor;
  highlightW *= hFactor;

  // 3. VECTORS (pre-computed, no trig per pixel)
  float dPb = (params.shadowPb * shadowW + params.highlightPb * highlightW) *
              params.strength * 0.05f;
  float dPr = (params.shadowPr * shadowW + params.highlightPr * highlightW) *
              params.strength * 0.05f;

  // Midtone toning (if saturation > 0)
  if (params.midtoneSaturation > 0.0f) {
    float mStr = params.midtoneSaturation * midtoneW * params.strength * 0.05f;
    dPb += params.midtonePb * mStr;
    dPr += params.midtonePr * mStr;
  }

  // 4. APPLY (Preserving L)
  float newR = *r + dPr / 0.6350f;
  float newB = *b + dPb / 0.5389f;
  float newG = (L - 0.2126f * newR - 0.0722f * newB) / 0.7152f;

  *r = newR;
  *g = newG;
  *b = newB;
}

} // namespace SplitToning
