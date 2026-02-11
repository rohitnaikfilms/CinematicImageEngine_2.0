#pragma once

#include "Utils.h"
#include <algorithm>
#include <vector>
#include <cmath>

class Sharpening {
public:
    enum Type {
        eSoftDetail = 0,
        eMicroContrast = 1,
        eEdgeAware = 2,
        eDeconvolution = 3
    };

    struct Params {
        bool enable;
        int type;
        double amount;
        double radius;
        double detailAmount;
        double edgeProtection;
        double noiseSuppression;
        double shadowProtection;
        double highlightProtection;
    };

    // New static helper to match CinematicImageEngine.cpp call
    static void applySharpen(float& r, float& g, float& b, float bR, float bG, float bB, const Params& p) {
        if (!p.enable || p.amount <= 0.0) return;

        // Luma only
        float L = Utils::getLuminance(r, g, b);
        float bL = Utils::getLuminance(bR, bG, bB);

        // Difference (Detail)
        float detail = L - bL;

        // --- Type Logic ---
        float adjDetail = detail;

        if (p.type == eMicroContrast) {
            adjDetail *= 1.2f;
        }
        else if (p.type == eDeconvolution) {
            float limit = 0.1f;
            if (adjDetail > limit) adjDetail = limit + (adjDetail - limit) * 0.1f;
            if (adjDetail < -limit) adjDetail = -limit + (adjDetail + limit) * 0.1f;
        }

        // --- Noise Suppression ---
        if (p.noiseSuppression > 0.0) {
             float thresh = (float)p.noiseSuppression * 0.05f;
             if (std::abs(adjDetail) < thresh) {
                 adjDetail *= (std::abs(adjDetail) / thresh);
             }
        }

        // --- Edge Protection ---
        if (p.type == eEdgeAware || p.edgeProtection > 0.0) {
             float protStrength = (p.type == eEdgeAware) ? std::max(0.5f, (float)p.edgeProtection) : (float)p.edgeProtection;
             float dAbs = std::abs(adjDetail);
             if (dAbs > 0.05f) {
                  float att = 1.0f / (1.0f + (dAbs - 0.05f) * protStrength * 20.0f);
                  adjDetail *= att;
             }
        }

        // --- Tonal Protection ---
        float weight = 1.0f;
        if (p.shadowProtection > 0.0) {
             float s = 1.0f - std::min(L * 4.0f, 1.0f);
             weight *= (1.0f - s * (float)p.shadowProtection);
        }
        if (p.highlightProtection > 0.0) {
             float h = std::max(0.0f, L - 0.6f) * 2.5f;
             weight *= (1.0f - h * (float)p.highlightProtection);
        }

        // Apply
        float strength = (float)p.amount * weight;
        strength *= (0.5f + (float)p.detailAmount);

        float L_sharp = L + adjDetail * strength;

        // Reapply
        float diff = L_sharp - L;
        r += diff;
        g += diff;
        b += diff;
    }
};
