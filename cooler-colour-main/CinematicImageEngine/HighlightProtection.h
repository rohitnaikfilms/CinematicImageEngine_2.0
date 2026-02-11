#pragma once

#include <cmath>
#include <algorithm>

/**
 * @brief Highlight Protection Module (Simplified).
 *
 * PURPOSE:
 * Luminance-only superwhite compression.
 *
 * MATH MODEL:
 * Asymptotic compression: f(x) = x / (1 + kx)
 * Applied to Luminance (Ratio) or Channels individually.
 */
class HighlightProtection
{
public:
    struct Params
    {
        double threshold;      // Start point
        double rolloff;        // Strength of compression (k)
        bool preserveColor;    // Toggle
    };

    static void processPixel(float* r, float* g, float* b, const Params& params)
    {
        float R = *r;
        float G = *g;
        float B = *b;
        const float epsilon = 1e-7f;

        // Helper: Asymptotic compression
        // f(x) = x for x < thresh? No, usually smooth transition.
        // Prompt says: f(x) = x / (1 + kx)
        // This function compresses entire range if applied directly?
        // Usually applied only above threshold:
        // delta = max(x - threshold, 0)
        // compressed = threshold + delta / (1 + k * delta) ?
        // Or simply: x / (1 + k * max(x-thresh, 0)) which scales x down.
        // The previous implementation was: L / (1.0f + rolloff * diff) blended with L by H.
        // The prompt says "f(x) = x / (1 + kx)".
        // If x is large, limit approaches 1/k.
        // Let's stick to the previous logic which is robust, but simplify if needed.
        // Previous logic: L_compressed = L / (1 + rolloff * (L-thresh)).
        // This is effectively scaling L down when it exceeds threshold.

        auto compress = [&](float val) -> float {
            if (val < params.threshold) return val;
            float diff = val - (float)params.threshold;
            return val / (1.0f + (float)params.rolloff * diff);
        };

        if (params.preserveColor)
        {
            // Luminance Only
            float L = 0.2126f * R + 0.7152f * G + 0.0722f * B;
            float L_new = compress(L);

            float ratio = L_new / std::max(L, epsilon);
            *r = R * ratio;
            *g = G * ratio;
            *b = B * ratio;
        }
        else
        {
            // Per Channel
            *r = compress(R);
            *g = compress(G);
            *b = compress(B);
        }
    }
};
