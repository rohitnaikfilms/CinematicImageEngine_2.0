#pragma once

#include "Utils.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

class FilmGrain {
public:
  enum GrainType {
    GT_CUSTOM = 0,
    GT_8MM,
    GT_16MM,
    GT_SUPER16,
    GT_35MM,
    GT_65MM,
    GT_CLEAN
  };

  struct Params {
    bool enable;
    float amount; // Master strength
    float size;   // Resolution-relative
    float shadowWeight;
    float midWeight;
    float highlightWeight;
    int grainType;       // Dropdown enum
    bool chromatic;      // Per-channel independent grain
    float temporalSpeed; // 0 = static, 1 = 24fps variation
  };

  // 1. FAST INTEGER HASH
  static inline float hash2D(int x, int y, int seed) {
    uint32_t h = uint32_t(x) * 374761393u + uint32_t(y) * 668265263u;
    h = (h ^ (h >> 13)) ^ uint32_t(seed);
    h *= 1274126177u;
    return (h & 0x00FFFFFF) / float(0x01000000); // 0..1
  }

  // 2. GAUSSIAN APPROXIMATION
  static inline float gaussianApprox(float n1, float n2) {
    return (n1 + n2 - 1.0f);
  }

  static void applyGrain(float *r, float *g, float *b, int x, int y,
                         int frameSeed, int imageW, int imageH,
                         const Params &p) {
    if (!p.enable || p.amount <= 0.0f)
      return;

    // 3. GRAIN SPACE
    float minDim = (float)std::min(imageW, imageH);
    float rawSize = std::max(0.001f, p.size);
    float scale = (0.0015f + rawSize * 0.005f) * minDim;
    if (scale < 1.0f)
      scale = 1.0f;

    int gx = int(x / scale);
    int gy = int(y / scale);

    // 4. TEMPORAL — scale frame seed by temporal speed
    // 0 = static grain (same every frame), 1 = full 24fps variation
    int effectiveSeed = frameSeed;
    if (p.temporalSpeed < 1.0f) {
      // Quantize seed to reduce temporal variation
      float interval = std::max(1.0f, 24.0f * (1.0f - p.temporalSpeed));
      effectiveSeed = (int)(frameSeed / interval) * (int)interval;
    }

    // 5. GRAIN GEN
    if (p.chromatic) {
      // Per-channel independent grain with offset seeds
      float n1R = hash2D(gx, gy, effectiveSeed);
      float n2R = hash2D(gx + 17, gy + 29, effectiveSeed);
      float n1G = hash2D(gx, gy, effectiveSeed + 7);
      float n2G = hash2D(gx + 17, gy + 29, effectiveSeed + 7);
      float n1B = hash2D(gx, gy, effectiveSeed + 13);
      float n2B = hash2D(gx + 17, gy + 29, effectiveSeed + 13);

      float grainR = gaussianApprox(n1R, n2R);
      float grainG = gaussianApprox(n1G, n2G);
      float grainB = gaussianApprox(n1B, n2B);

      // 6. LUM WEIGHTING
      float L = 0.2126f * (*r) + 0.7152f * (*g) + 0.0722f * (*b);
      float finalWeight = computeWeight(L, p);
      float strength = p.amount * finalWeight;

      *r *= (1.0f + grainR * strength);
      *g *= (1.0f + grainG * strength);
      *b *= (1.0f + grainB * strength);
    } else {
      // Monochromatic grain (original behavior)
      float n1 = hash2D(gx, gy, effectiveSeed);
      float n2 = hash2D(gx + 17, gy + 29, effectiveSeed);
      float grain = gaussianApprox(n1, n2);

      float L = 0.2126f * (*r) + 0.7152f * (*g) + 0.0722f * (*b);
      float finalWeight = computeWeight(L, p);
      float strength = p.amount * finalWeight;

      float grainSignal = 1.0f + grain * strength;
      *r *= grainSignal;
      *g *= grainSignal;
      *b *= grainSignal;
    }
  }

private:
  static inline float computeWeight(float L, const Params &p) {
    if (L < 0.5f) {
      float t = Utils::smoothstep(0.0f, 0.5f, L);
      float sw = std::min(1.0f, std::max(0.0f, p.shadowWeight));
      float mw = std::min(1.0f, std::max(0.0f, p.midWeight));
      return sw * (1.0f - t) + mw * t;
    } else {
      float t = Utils::smoothstep(0.5f, 1.0f, L);
      float mw = std::min(1.0f, std::max(0.0f, p.midWeight));
      float hw = std::min(1.0f, std::max(0.0f, p.highlightWeight));
      return mw * (1.0f - t) + hw * t;
    }
  }
};
