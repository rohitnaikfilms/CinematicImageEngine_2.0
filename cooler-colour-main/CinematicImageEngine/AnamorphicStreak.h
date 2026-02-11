#pragma once

#include "Utils.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace AnamorphicStreak {

struct Params {
  bool enable;
  double amount;    // 0..1, streak intensity
  double threshold; // Highlight isolation threshold
  double length;    // 0..1, horizontal streak length (maps to blur radius)
  double tint;      // -1 (cool/blue) to +1 (warm/orange)
};

// Horizontal-only box blur — 1D sliding window, O(W*H)
inline void boxBlurH1D(const float *__restrict__ src, float *__restrict__ dst,
                       int w, int h, int r) {
  if (r < 1) {
    std::memcpy(dst, src, (size_t)w * h * 4 * sizeof(float));
    return;
  }
  const float invK = 1.0f / (float)(2 * r + 1);
  const int stride = w * 4;

  for (int y = 0; y < h; ++y) {
    const float *row = src + y * stride;
    float *out = dst + y * stride;

    float sumR = row[0] * (r + 1);
    float sumG = row[1] * (r + 1);
    float sumB = row[2] * (r + 1);
    for (int i = 1; i <= r; ++i) {
      int px = std::min(i, w - 1) * 4;
      sumR += row[px + 0];
      sumG += row[px + 1];
      sumB += row[px + 2];
    }

    for (int x = 0; x < w; ++x) {
      out[x * 4 + 0] = sumR * invK;
      out[x * 4 + 1] = sumG * invK;
      out[x * 4 + 2] = sumB * invK;
      out[x * 4 + 3] = row[x * 4 + 3];

      int addIdx = std::min(x + r + 1, w - 1) * 4;
      int subIdx = std::max(x - r, 0) * 4;
      sumR += row[addIdx + 0] - row[subIdx + 0];
      sumG += row[addIdx + 1] - row[subIdx + 1];
      sumB += row[addIdx + 2] - row[subIdx + 2];
    }
  }
}

// Isolate highlights and compute streak source
inline void computeStreakSource(float r, float g, float b, float &outR,
                                float &outG, float &outB,
                                const Params &params) {
  float L = Utils::getLuminance(r, g, b);
  float mask = Utils::smoothstep((float)params.threshold,
                                 (float)params.threshold + 0.3f, L);
  if (mask <= 0.001f) {
    outR = outG = outB = 0.0f;
    return;
  }
  outR = r * mask;
  outG = g * mask;
  outB = b * mask;
}

// Apply the blurred streak with optional color tint
inline void applyStreak(float &r, float &g, float &b, float sR, float sG,
                        float sB, const Params &params) {
  if (params.amount <= 0.0)
    return;

  float amt = (float)params.amount;
  float tint = (float)params.tint;

  // Apply warmth/coolness tint to the streak
  if (tint > 0.0f) {
    // Warm: boost R, slight G, reduce B
    sR *= (1.0f + tint * 0.3f);
    sG *= (1.0f + tint * 0.1f);
    sB *= (1.0f - tint * 0.2f);
  } else if (tint < 0.0f) {
    // Cool: reduce R, boost B
    float ct = -tint;
    sR *= (1.0f - ct * 0.2f);
    sB *= (1.0f + ct * 0.3f);
  }

  // Additive blend (screen-like for highlights)
  r += sR * amt;
  g += sG * amt;
  b += sB * amt;
}

} // namespace AnamorphicStreak
