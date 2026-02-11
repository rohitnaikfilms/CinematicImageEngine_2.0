#pragma once

#include "Utils.h"
#include <algorithm>
#include <cmath>

/**
 * @brief Photochemical Color Response (PCR) Module.
 *        Hue + saturation behavior driven strictly by luminance.
 *        STRICT SCOPE: NO luminance contrast, NO compression curves.
 */
class FilmResponse {
public:
  enum Preset {
    PRESET_NONE = 0,
    PRESET_VISION3_500T,
    PRESET_ETERNA,
    PRESET_PORTRA,
    PRESET_EKTACHROME,
    PRESET_CROSS_PROCESS
  };

  struct Params {
    bool enable;
    double amount;
    double highlightWarmth;
    double highlightCompression;
    double midtoneColorFocus;
    double shadowCoolBias;
    int preset;        // Preset enum
    bool crossProcess; // Swap shadow/highlight hue vectors
  };

  // Apply preset values — call once per frame before processPixel
  static void applyPreset(Params &p) {
    switch (p.preset) {
    case PRESET_VISION3_500T:
      p.shadowCoolBias = 0.4;
      p.midtoneColorFocus = 0.6;
      p.highlightWarmth = 0.5;
      p.highlightCompression = 0.3;
      break;
    case PRESET_ETERNA:
      p.shadowCoolBias = 0.6;
      p.midtoneColorFocus = 0.3;
      p.highlightWarmth = 0.2;
      p.highlightCompression = 0.5;
      break;
    case PRESET_PORTRA:
      p.shadowCoolBias = 0.2;
      p.midtoneColorFocus = 0.5;
      p.highlightWarmth = 0.7;
      p.highlightCompression = 0.2;
      break;
    case PRESET_EKTACHROME:
      p.shadowCoolBias = 0.5;
      p.midtoneColorFocus = 0.8;
      p.highlightWarmth = 0.3;
      p.highlightCompression = 0.4;
      break;
    case PRESET_CROSS_PROCESS:
      p.shadowCoolBias = 0.7;
      p.midtoneColorFocus = 0.9;
      p.highlightWarmth = 0.8;
      p.highlightCompression = 0.1;
      p.crossProcess = true;
      break;
    default: // PRESET_NONE — use manual controls
      break;
    }
  }

  static void processPixel(float *r, float *g, float *b, const Params &params) {
    if (!params.enable || params.amount <= 0.0)
      return;

    float R = *r;
    float G = *g;
    float B = *b;

    float Y = 0.2126f * R + 0.7152f * G + 0.0722f * B;

    float cR = R - Y;
    float cG = G - Y;
    float cB = B - Y;

    float cMagSq = cR * cR + cG * cG + cB * cB;
    if (cMagSq < 1e-8f)
      return;

    float shadowW = 1.0f - Utils::smoothstep(0.0f, 0.3f, Y);
    float highlightW = Utils::smoothstep(0.7f, 1.0f, Y);
    float midWeight = (1.0f - shadowW) * (1.0f - highlightW);

    // Cross-processing: swap shadow and highlight vectors
    float shadowBias = (float)params.shadowCoolBias;
    float highlightWarmthVal = (float)params.highlightWarmth;
    float highlightComp = (float)params.highlightCompression;

    if (params.crossProcess) {
      // Shadows get warm instead of cool, highlights get cool
      std::swap(shadowBias, highlightWarmthVal);
    }

    // --- SHADOWS ---
    if (shadowW > 0.0f) {
      float bias = shadowBias * shadowW;
      float satFactor = 1.0f - bias * 0.5f;
      cR *= satFactor;
      cG *= satFactor;
      cB *= satFactor;

      float bStr = bias * 0.05f;
      if (!params.crossProcess) {
        // Cool bias: -R, +B, slight teal
        cR -= bStr;
        cB += bStr * 1.5f;
        cG += bStr * 0.2f;
      } else {
        // Warm bias in shadows (cross-process)
        cR += bStr;
        cG += bStr * 0.5f;
        cB -= bStr;
      }
    }

    // --- MIDTONES ---
    if (midWeight > 0.0f) {
      float satBoost = (float)params.midtoneColorFocus * midWeight;
      float factor = 1.0f + satBoost;
      cR *= factor;
      cG *= factor;
      cB *= factor;
    }

    // --- HIGHLIGHTS ---
    if (highlightW > 0.0f) {
      float hStr = highlightWarmthVal * highlightW;
      float hComp = highlightComp * highlightW;

      float wStr = hStr * 0.05f;
      if (!params.crossProcess) {
        // Warm bias
        cR += wStr;
        cG += wStr * 0.5f;
        cB -= wStr;
      } else {
        // Cool bias in highlights (cross-process)
        cR -= wStr;
        cB += wStr * 1.5f;
        cG += wStr * 0.2f;
      }

      cR *= (1.0f - hComp * 1.0f);
      cG *= (1.0f - hComp * 0.5f);
      cB *= (1.0f - hComp * 0.2f);
    }

    float R_new = Y + cR;
    float G_new = Y + cG;
    float B_new = Y + cB;

    float amt = (float)params.amount;
    *r = Utils::mix(R, R_new, amt);
    *g = Utils::mix(G, G_new, amt);
    *b = Utils::mix(B, B_new, amt);
  }
};
