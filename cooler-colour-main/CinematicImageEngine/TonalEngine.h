#pragma once

#include <algorithm>
#include <cmath>

/**
 * @brief Tonal Engine Module.
 *
 * Reshape tonal response around a defined pivot.
 * Supports independent highlight contrast and soft clipping.
 *
 * MATH MODEL:
 * L = 0.2126*R + 0.7152*G + 0.0722*B
 * Below pivot: Lout = Pivot * (Lin / Pivot)^Contrast
 * Above pivot: Lout = Pivot + (1-Pivot) * ((Lin-Pivot)/(1-Pivot))^HighContrast
 * Lout = max(Lout, BlackFloor)
 * SoftClip: x / (1 + k*x) at boundaries
 */
class TonalEngine {
public:
  struct Params {
    double contrast;   // > 0, default 1.0
    double pivot;      // > 0, default 0.18
    double strength;   // 0.0 .. 1.0
    double blackFloor; // 0.0 .. 0.1
    double
        highlightContrast; // > 0, default 1.0 (independent above-pivot power)
    double softClip;       // 0.0 .. 1.0, soft clipping strength
  };

  static void processPixel(float *r, float *g, float *b, const Params &params) {
    if (params.strength <= 0.0)
      return;

    float R = *r;
    float G = *g;
    float B = *b;

    float L = 0.2126f * R + 0.7152f * G + 0.0722f * B;

    const float epsilon = 1e-7f;
    float safePivot = (float)std::max(params.pivot, 1e-4);

    float L_mapped;
    if (L <= safePivot) {
      // Below pivot: standard power curve
      float Ln = std::max(L / safePivot, epsilon);
      float Lc = std::pow(Ln, (float)params.contrast);
      L_mapped = Lc * safePivot;
    } else {
      // Above pivot: independent highlight contrast
      float range = 1.0f - safePivot;
      if (range < epsilon)
        range = epsilon;
      float Ln = (L - safePivot) / range;
      float hContrast = (float)std::max(params.highlightContrast, 0.01);
      float Lc = std::pow(std::max(Ln, epsilon), hContrast);
      L_mapped = safePivot + Lc * range;
    }

    // Strength-controlled blending
    float str = std::min(std::max((float)params.strength, 0.0f), 1.0f);
    float L_out = L * (1.0f - str) + L_mapped * str;

    // Black Floor
    if (params.blackFloor > 0.0) {
      L_out = std::max(L_out, (float)params.blackFloor);
    }

    // Soft Clip — x/(1+kx) at boundaries
    if (params.softClip > 0.0) {
      float k = (float)params.softClip * 2.0f;
      // Soft clip near 0 (floor) and near 1 (ceiling)
      if (L_out > 0.0f && L_out < 1.0f) {
        // Upper soft clip: compress values approaching 1.0
        float headroom = 1.0f - L_out;
        if (headroom < k * 0.5f) {
          float excess = k * 0.5f - headroom;
          L_out = 1.0f - (k * 0.5f) / (1.0f + excess * 4.0f);
        }
      } else if (L_out >= 1.0f) {
        // Already above 1 — compress with asymptotic curve
        L_out = 1.0f - 1.0f / (1.0f + (L_out - 1.0f + 1.0f) * (1.0f + k));
        L_out = std::min(L_out, 1.0f);
      }
    }

    // RGB Reapplication (Preserve Chroma)
    float ratio = L_out / std::max(L, epsilon);

    *r = R * ratio;
    *g = G * ratio;
    *b = B * ratio;
  }
};
