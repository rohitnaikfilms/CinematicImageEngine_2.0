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
  double warmth;     // -1 (Red) to 1 (Orange/Yellow)
  double radius;     // Spatial radius
  double saturation; // To bound the color
  double hueShift;   // 0..360, rotates scatter color (0 = red default)
};

// Compute the source color that will be blurred
inline void computeHalationSource(float r, float g, float b, float skinMask,
                                  float &hR, float &hG, float &hB,
                                  const Params &params) {
  if (!params.enable || params.amount <= 0.0) {
    hR = hG = hB = 0.0f;
    return;
  }

  float L = Utils::getLuminance(r, g, b);

  // Highlight Mask (using shared smoothstep)
  float mask = Utils::smoothstep(
      (float)params.threshold, (float)params.threshold + (float)params.knee, L);

  // Skin Preservation Attenuation
  mask *= (1.0f - skinMask);

  if (mask <= 0.001f) {
    hR = hG = hB = 0.0f;
    return;
  }

  // Color Logic — hue-shifted scatter
  // Default (hueShift=0): Red-dominant (film physics)
  // HueShift rotates the scatter color in RGB space.
  float hueRad =
      (float)params.hueShift * 0.0174532925f; // Convert degrees to radians
  float cosH = std::cos(hueRad);
  float sinH = std::sin(hueRad);

  // Base scatter: primarily red channel energy
  float baseG = 0.1f;
  float mixG = baseG + (float)params.warmth * 0.4f;
  mixG = std::max(0.0f, mixG);

  float sourceEnergy = r;

  // Original base color vector (before rotation): (1, mixG, 0)
  // Apply rotation around the achromatic axis (1/sqrt(3), 1/sqrt(3), 1/sqrt(3))
  // Simplified hue rotation matrix components for a vector (x, y, z)
  // x' = x * (cosH + (1-cosH)/3) + y * (-sinH/sqrt(3) + (1-cosH)/3) + z *
  // (sinH/sqrt(3) + (1-cosH)/3) y' = x * (sinH/sqrt(3) + (1-cosH)/3) + y *
  // (cosH + (1-cosH)/3) + z * (-sinH/sqrt(3) + (1-cosH)/3) z' = x *
  // (-sinH/sqrt(3) + (1-cosH)/3) + y * (sinH/sqrt(3) + (1-cosH)/3) + z * (cosH
  // + (1-cosH)/3)

  // For our base vector (1, mixG, 0):
  float oneMinusCosH_div_3 = (1.0f - cosH) / 3.0f;
  float sinH_div_sqrt3 = sinH * 0.577350269f; // 1/sqrt(3) approx 0.57735

  hR = sourceEnergy * mask *
       (1.0f * (cosH + oneMinusCosH_div_3) +
        mixG * (-sinH_div_sqrt3 + oneMinusCosH_div_3));
  hG = sourceEnergy * mask *
       (1.0f * (sinH_div_sqrt3 + oneMinusCosH_div_3) +
        mixG * (cosH + oneMinusCosH_div_3));
  hB = sourceEnergy * mask *
       (1.0f * (-sinH_div_sqrt3 + oneMinusCosH_div_3) +
        mixG * (sinH_div_sqrt3 + oneMinusCosH_div_3));
}

// Apply the blurred halation to the original pixel
// Now implements the saturation control that was previously unused.
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

  // Additive blend (HDR safe — no clamping needed)
  *r += hBlurR * amt;
  *g += hBlurG * amt;
  *b += hBlurB * amt;
}

} // namespace Halation
