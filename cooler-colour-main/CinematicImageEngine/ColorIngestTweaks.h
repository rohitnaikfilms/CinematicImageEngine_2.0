#pragma once
#include "Utils.h"

namespace ColorIngestTweaks {

struct Params {
  double exposureTrim;
  double chromaCeiling;
  double whiteBias;        // -1.0 (Cool) to 1.0 (Warm)
  double temperature;      // -1.0 (Cool) to 1.0 (Warm)
  double tint;             // -1.0 (Green) to 1.0 (Magenta)
  double globalSaturation; // 0.0 .. 2.0, default 1.0
  bool enable;
};

inline void process(float *r, float *g, float *b, const Params &p) {
  if (!p.enable)
    return;

  // 1. Exposure Trim — RGB *= 2^trim
  if (p.exposureTrim != 0.0) {
    float gain = exp2f((float)p.exposureTrim);
    *r *= gain;
    *g *= gain;
    *b *= gain;
  }

  // 2. White Balance — Temperature (R/B shift) + Tint (G/M shift)
  if (p.temperature != 0.0 || p.tint != 0.0) {
    float temp = (float)p.temperature * 0.1f;
    float tnt = (float)p.tint * 0.1f;
    *r += temp;
    *g += tnt;
    *b -= temp;
  }

  // 3. Global Saturation — scale chroma from luminance
  if (p.globalSaturation != 1.0) {
    float luma = 0.2126f * (*r) + 0.7152f * (*g) + 0.0722f * (*b);
    float sat = (float)p.globalSaturation;
    *r = luma + (*r - luma) * sat;
    *g = luma + (*g - luma) * sat;
    *b = luma + (*b - luma) * sat;
  }

  // 4. Chroma Ceiling — soft compress extreme saturation
  if (p.chromaCeiling < 1.0) {
    float luma = 0.2126f * (*r) + 0.7152f * (*g) + 0.0722f * (*b);
    float cr = *r - luma;
    float cg = *g - luma;
    float cb = *b - luma;
    float cMag = std::sqrt(cr * cr + cg * cg + cb * cb);
    float limit = (float)p.chromaCeiling;
    if (cMag > limit && limit > 0.001f) {
      float compressed = limit + std::tanh(cMag - limit) * 0.1f;
      float scale = compressed / cMag;
      *r = luma + cr * scale;
      *g = luma + cg * scale;
      *b = luma + cb * scale;
    } else if (limit <= 0.001f) {
      *r = luma;
      *g = luma;
      *b = luma;
    }
  }

  // 5. Highlight White Bias
  if (p.whiteBias != 0.0) {
    float luma = 0.2126f * (*r) + 0.7152f * (*g) + 0.0722f * (*b);
    if (luma > 0.5f) {
      float factor = (luma - 0.5f) * 2.0f;
      factor = factor * factor;
      float biasStrength = (float)p.whiteBias * 0.05f * factor;
      if (p.whiteBias > 0) {
        *r += biasStrength;
        *g += biasStrength * 0.8f;
        *b -= biasStrength;
      } else {
        *r -= std::abs(biasStrength);
        *g -= std::abs(biasStrength) * 0.2f;
        *b += std::abs(biasStrength);
      }
    }
  }
}

} // namespace ColorIngestTweaks
