#pragma once

#include <cstdint>

namespace Dither {

struct Params {
  bool enable;
  double amount; // 0..1, dither strength
};

// Fast spatial hash for dithering — returns -0.5..+0.5
inline float ditherHash(int x, int y, uint32_t seed) {
  uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
  h = (h ^ (h >> 13)) ^ seed;
  h *= 1274126177u;
  return (float)(h & 0x00FFFFFF) / (float)0x01000000 - 0.5f;
}

// Triangular-PDF dither to break banding in 8/10-bit output.
inline void process(float *r, float *g, float *b, int x, int y,
                    const Params &params) {
  if (!params.enable || params.amount <= 0.0)
    return;

  // Triangular-PDF: sum of two uniform — better perceptual quality
  float nR = ditherHash(x, y, 0xA1B2C3D4u) + ditherHash(x + 1, y, 0xA1B2C3D4u);
  float nG = ditherHash(x, y, 0xE5F6A7B8u) + ditherHash(x + 1, y, 0xE5F6A7B8u);
  float nB = ditherHash(x, y, 0xC9D0E1F2u) + ditherHash(x + 1, y, 0xC9D0E1F2u);

  // Scale: 1/512 is a good middle ground between 8-bit and 10-bit output
  float scale = (float)params.amount * (1.0f / 512.0f);

  *r += nR * scale;
  *g += nG * scale;
  *b += nB * scale;
}

} // namespace Dither
