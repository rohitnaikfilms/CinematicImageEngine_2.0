#pragma once

#include "Utils.h"
#include <algorithm>
#include <cmath>

namespace DreamyMist {

struct Params {
  bool enable;
  double strength;   // Mist Strength (0-1)
  double threshold;  // Highlight Threshold
  double softness;   // Diffusion falloff (width of smoothstep)
  double depthBias;  // Gamma/Travel control
  double colorBias;  // Warmth/Cool bias
  double blurRadius; // Fixed small radius (scaled)
};

// Computes the source for the mist diffusion (isolated, tinted highlights)
inline void computeMistSource(float r, float g, float b, float &outR,
                              float &outG, float &outB, const Params &params) {
  if (!params.enable) {
    outR = outG = outB = 0.0f;
    return;
  }

  // 1. Luminance
  float L = Utils::getLuminance(r, g, b);

  // 2. Generate Mask
  float softRange = std::max(0.001, params.softness);
  float mask = Utils::smoothstep((float)params.threshold,
                                 (float)params.threshold + (float)softRange, L);

  // 3. Depth Bias (Gamma)
  if (params.depthBias != 1.0 && mask > 0.0f) {
    mask = std::pow(mask, (float)params.depthBias);
  }

  // 5. Color Bias (Warmth)
  // -1 (Cool) to +1 (Warm)
  float biasR = 1.0f;
  float biasB = 1.0f;
  if (params.colorBias > 0) {
    biasR += (float)params.colorBias * 0.5f;
    biasB -= (float)params.colorBias * 0.2f;
  } else if (params.colorBias < 0) {
    biasB += std::abs((float)params.colorBias) * 0.5f;
    biasR -= std::abs((float)params.colorBias) * 0.2f;
  }

  // 6. Extract Source
  // Mist is "mostly achromatic".
  // We can use L * mask * colorBias, or Input * mask * colorBias.
  // "Mist behaves like light scattering... affects only the highest-energy
  // light". "Mist is mostly achromatic... Never oversaturate". Let's use
  // Luminance based source to keep it achromatic, then apply tint. Or
  // desaturated Input? Let's use L * mask.
  float mistL = L * mask;

  outR = mistL * biasR;
  outG = mistL; // Green is anchor
  outB = mistL * biasB;
}

// Applies the diffused mist to the image
inline void applyMist(float &r, float &g, float &b, float mistR, float mistG,
                      float mistB, const Params &params) {
  if (!params.enable)
    return;

  // Model: output = mix(input, input + mistLayer, mistStrength)
  // Which is: output = input + mistLayer * mistStrength
  // "Energy Conserving Veil Model: Mist must reduce local contrast WITHOUT
  // reducing exposure." "The image must never get darker."

  float s = (float)params.strength;

  r = r + mistR * s;
  g = g + mistG * s;
  b = b + mistB * s;

  // Note: This purely adds light (lifts). This reduces contrast by lifting
  // shadows/mids relative to highlights? Wait, mistR/G/B comes from highlights.
  // So we are adding highlights to the image. If we add blurred highlights to
  // the image, we get a glow/mist effect. This lifts the area AROUND the
  // highlights (because it's blurred). This matches "Lifts perceived air...
  // Softens highlight contrast". "Shadows must contribute ZERO mist". Correct,
  // source is highlights.
}

} // namespace DreamyMist
