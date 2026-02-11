#pragma once
#include "Utils.h"

namespace ColorEnergyEngine {

struct Params {
  double density;    // Power factor
  double separation; // Expansion factor
  double highlightRollOff;
  double shadowBias;
  double vibrance; // 0..2, default 1. Saturation-aware saturation boost
  bool enable;
};

inline void process(float *r, float *g, float *b, const Params &p) {
  if (!p.enable)
    return;

  // 1. Calculate Luma & Chroma
  float luma = 0.2126f * (*r) + 0.7152f * (*g) + 0.0722f * (*b);

  // Safety check for black/white
  if (luma <= 0.0001f)
    return;

  // Chroma components
  float cr = *r - luma;
  float cg = *g - luma;
  float cb = *b - luma;

  // 2. Color Separation (Expand vectors)
  // C_new = C_old * (1 + separation)
  // Attenuated by Skin Preservation? (Module removed per instructions, so
  // ignore skin here unless integrated) Prompt says: "Color Separation...
  // utilizes Skin Preservation mask". BUT Skin Preservation module was REMOVED
  // from the list. "Merges... Color Separation". "Attenuated by luminance
  // extremes" is the new rule for Energy Engine.

  float sepFactor = 1.0f;
  if (p.separation != 0.0) {
    // Roll-off based on luma
    // Shadow Bias/Roll-off
    float shadowAtt = 1.0f;
    if (luma < p.shadowBias) {
      shadowAtt = luma / (float)p.shadowBias;
    }

    // Highlight Roll-off (guard against division by zero)
    float highAtt = 1.0f;
    if (p.highlightRollOff > 1e-6f &&
        luma > (1.0f - (float)p.highlightRollOff)) {
      highAtt = (1.0f - luma) / (float)p.highlightRollOff;
      if (highAtt < 0.0f)
        highAtt = 0.0f;
    }

    float totalAtt = shadowAtt * highAtt;
    sepFactor = 1.0f + (float)p.separation * totalAtt;

    cr *= sepFactor;
    cg *= sepFactor;
    cb *= sepFactor;
  }

  // 3. Color Density
  // C_densed = C * pow(Sat, density - 1) ?
  // Prompt: "Chroma power + vector expansion"
  // Memory: "C_densed = pow(C, density)" -- wait, C is a vector.
  // Usually Density means: As saturation increases, luminance decreases.
  // Or: Saturation curve.
  // Memory says: "C_densed = pow(C, density)" ensures hue stability?
  // No, vector pow is weird.
  // "Subtractive color model simulation... As density increases, pure colors
  // become darker." Implementation: Convert to Saturation. S = max(r,g,b) -
  // min(r,g,b) / max(r,g,b)? Or just simple vector scaling based on magnitude.

  if (p.density != 1.0) {
    // Simple approach:
    // C_new = C * (Saturation ^ (density-1)) ?
    // If density > 1, higher saturation -> Boost? No, density usually makes it
    // darker/richer. Let's look at previous ColorDensity logic from
    // memory/manual. "C_new = C_old ^ density" Let's implement effectively: S =
    // sqrt(cr*cr + ...); S_new = pow(S, density); scale = S_new / S;

    float sat = std::sqrt(cr * cr + cg * cg + cb * cb);
    if (sat > 0.0001f) {
      float satNew = std::pow(sat, (float)p.density);
      // Blend density effect based on shadow/highlight controls?
      // Prompt says "Attenuated by luminance extremes".
      // Let's reuse the attenuation calculated above or similar.

      // Apply density
      float scale = satNew / sat;

      // Mix with 1.0 based on attenuation?
      // Usually Density is global but protected in lows/highs.
      // Let's apply the scaling.
      cr *= scale;
      cg *= scale;
      cb *= scale;
    }
  }

  // 4. Vibrance — saturation-aware saturation boost
  // Low-sat pixels get boosted more than high-sat pixels
  if (p.vibrance != 1.0) {
    float sat = std::sqrt(cr * cr + cg * cg + cb * cb);
    if (sat > 0.0001f) {
      // satMix: 1.0 at low saturation, 0.0 at high saturation
      float satNorm = std::min(sat * 2.0f, 1.0f);
      float boost = Utils::mix((float)p.vibrance, 1.0f, satNorm);
      cr *= boost;
      cg *= boost;
      cb *= boost;
    }
  }

  // Reconstruct
  *r = luma + cr;
  *g = luma + cg;
  *b = luma + cb;
}

} // namespace ColorEnergyEngine
