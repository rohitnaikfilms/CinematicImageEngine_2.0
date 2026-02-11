#pragma once

#include "Utils.h"
#include <algorithm>
#include <cmath>

class Vignette {
public:
  enum Type { eDark = 0, eLight = 1, eDefocus = 2 };

  struct Params {
    bool enable;
    int type; // Type enum
    double amount;
    bool invert;
    double size;
    double roundness;
    double edgeSoftness;
    double defocusAmount;
    double defocusSoftness;
    double centerX; // -1..1, offset from frame center (0 = centered)
    double centerY; // -1..1, offset from frame center (0 = centered)
    double tintR;   // 0..1, vignette color tint red
    double tintG;   // 0..1, vignette color tint green
    double tintB;   // 0..1, vignette color tint blue
  };

  // Compute vignette mask at UV coordinates.
  // u, v are normalized 0..1 across the full image.
  static float computeMask(float u, float v, float aspect, const Params &p) {
    // Center with offset
    float cx = 0.5f + (float)p.centerX * 0.5f;
    float cy = 0.5f + (float)p.centerY * 0.5f;
    float dx = u - cx;
    float dy = v - cy;

    // Aspect correction for circular vignette
    if (aspect > 1.0f) {
      dx *= aspect;
    } else {
      dy /= aspect;
    }

    // Distance metric: circle vs square blend
    float dCircle = std::sqrt(dx * dx + dy * dy);
    float dSquare = std::max(std::abs(dx), std::abs(dy));
    float dist = Utils::mix(dSquare, dCircle, (float)p.roundness);

    // Softness and size
    float softness = std::max(0.01f, (float)p.edgeSoftness);
    float start = (float)p.size * 0.7f;
    float end = start + softness;

    float V = Utils::smoothstep(start, end, dist);
    return V;
  }

  static void processPixel(float *r, float *g, float *b, float V,
                           float skinMask, const Params &p) {
    if (!p.enable)
      return;

    // Invert mask
    float mask = p.invert ? (1.0f - V) : V;

    // Attenuate by skin mask
    mask *= (1.0f - skinMask);

    if (mask <= 0.0f)
      return;

    float amount = (float)p.amount;

    if (p.type == eDark || p.type == eLight) {
      float effAmount = 0.0f;
      if (p.type == eDark)
        effAmount = -amount;
      else
        effAmount = amount;

      float L = Utils::getLuminance(*r, *g, *b);
      float L_out = L * (1.0f + effAmount * mask);
      L_out = std::max(0.0f, L_out);

      float scale = (L > 1e-6f) ? (L_out / L) : 1.0f;
      *r *= scale;
      *g *= scale;
      *b *= scale;

      // Apply color tint to the vignette region
      if (p.tintR > 0.0f || p.tintG > 0.0f || p.tintB > 0.0f) {
        float tintStr = mask * amount * 0.5f;
        *r += (float)p.tintR * tintStr;
        *g += (float)p.tintG * tintStr;
        *b += (float)p.tintB * tintStr;
      }
    } else if (p.type == eDefocus) {
      // Defocus handled by caller (PipelineProcessor) using computeMask
    }
  }
};
