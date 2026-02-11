#pragma once

#include "Utils.h"
#include <algorithm>
#include <cmath>

namespace ChromaticAberration {

struct Params {
  bool enable;
  double amount;  // 0..1, strength of the radial shift
  double centerX; // -1..1, offset from frame center
  double centerY; // -1..1, offset from frame center
};

// Sample a single channel from the buffer at UV coordinates
inline float sampleChannel(const float *src, float su, float sv, float imgW,
                           float imgH, float rodX1, float rodY1, int bufX1,
                           int bufY1, int w, int h, int channel) {
  float px = (su * imgW + rodX1) - (float)bufX1;
  float py = (sv * imgH + rodY1) - (float)bufY1;
  int ix = std::max(0, std::min((int)px, w - 1));
  int iy = std::max(0, std::min((int)py, h - 1));
  return src[(iy * w + ix) * 4 + channel];
}

// Apply chromatic aberration by radially shifting R and B channels.
inline void process(const float *__restrict__ src, float *__restrict__ dst,
                    int w, int h, float rodX1, float rodY1, float imgW,
                    float imgH, int bufX1, int bufY1, const Params &params) {
  if (!params.enable || params.amount <= 0.0) {
    if (src != dst)
      std::memcpy(dst, src, (size_t)w * h * 4 * sizeof(float));
    return;
  }

  const float cx = 0.5f + (float)params.centerX * 0.5f;
  const float cy = 0.5f + (float)params.centerY * 0.5f;
  const float strength = (float)params.amount * 0.02f;
  const float invW = 1.0f / std::max(1.0f, imgW);
  const float invH = 1.0f / std::max(1.0f, imgH);

  for (int y = 0; y < h; ++y) {
    for (int x = 0; x < w; ++x) {
      float u = ((float)(bufX1 + x) - rodX1) * invW;
      float v = ((float)(bufY1 + y) - rodY1) * invH;

      float du = u - cx;
      float dv = v - cy;
      float dist = std::sqrt(du * du + dv * dv);

      float shiftR = dist * strength;
      float shiftB = -dist * strength;

      float uR = u + du * shiftR;
      float vR = v + dv * shiftR;
      float uB = u + du * shiftB;
      float vB = v + dv * shiftB;

      int idx = (y * w + x) * 4;
      dst[idx + 0] = sampleChannel(src, uR, vR, imgW, imgH, rodX1, rodY1, bufX1,
                                   bufY1, w, h, 0);
      dst[idx + 1] = src[idx + 1];
      dst[idx + 2] = sampleChannel(src, uB, vB, imgW, imgH, rodX1, rodY1, bufX1,
                                   bufY1, w, h, 2);
      dst[idx + 3] = src[idx + 3];
    }
  }
}

} // namespace ChromaticAberration
