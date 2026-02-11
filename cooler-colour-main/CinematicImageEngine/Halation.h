#pragma once

#include "Utils.h"
#include <algorithm>
#include <cmath>

namespace Halation {

struct Params {
  bool enable;
  double amount;
  double threshold;
  double knee;       // Softness of the threshold
  double warmth;     // 0 (pure red) to 1 (orange/amber)
  double radius;     // Spatial radius
  double saturation; // Halation color saturation (0=mono, 1=full color)
};

// Compute the source color that will be blurred to create the halation effect.
// Uses luminance as energy source (not just red channel) so the effect is
// visible on all footage, not just red-dominant scenes.
inline void computeHalationSource(float r, float g, float b, float &hR,
                                  float &hG, float &hB, const Params &params) {
  if (!params.enable || params.amount <= 0.0) {
    hR = hG = hB = 0.0f;
    return;
  }

  float L = Utils::getLuminance(r, g, b);

  // Highlight mask — only bright areas scatter
  float mask = Utils::smoothstep(
      (float)params.threshold, (float)params.threshold + (float)params.knee, L);

  if (mask <= 0.001f) {
    hR = hG = hB = 0.0f;
    return;
  }

  // Energy: luminance-weighted (visible on ALL footage, not just red scenes)
  float energy = L * mask;

  // Scatter color: warm red-orange tint (film halation physics)
  // warmth=0 → pure red (1.0, 0.1, 0.0)
  // warmth=1 → amber/orange (1.0, 0.5, 0.05)
  float warmth = std::max(0.0f, std::min(1.0f, (float)params.warmth));
  float tintR = 1.0f;
  float tintG = 0.1f + warmth * 0.4f;
  float tintB = warmth * 0.05f;

  hR = energy * tintR;
  hG = energy * tintG;
  hB = energy * tintB;
}

// Apply the blurred halation to the original pixel.
inline void applyHalation(float *r, float *g, float *b, float hBlurR,
                          float hBlurG, float hBlurB, const Params &params) {
  if (!params.enable || params.amount <= 0.0)
    return;

  float amt = (float)params.amount;

  // Saturation control: desaturate the halation contribution
  // before blending. 1.0 = full color, 0.0 = luminance only.
  float sat = std::max(0.0f, std::min(1.0f, (float)params.saturation));
  if (sat < 1.0f) {
    float hL = Utils::getLuminance(hBlurR, hBlurG, hBlurB);
    hBlurR = Utils::mix(hL, hBlurR, sat);
    hBlurG = Utils::mix(hL, hBlurG, sat);
    hBlurB = Utils::mix(hL, hBlurB, sat);
  }

  // Additive blend (HDR safe — no clamping)
  *r += hBlurR * amt;
  *g += hBlurG * amt;
  *b += hBlurB * amt;
}

} // namespace Halation
