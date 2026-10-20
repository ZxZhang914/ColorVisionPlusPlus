#include "ColorblindFilter.h"
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
//  Colorblindness simulation
//
//  Reference: Machado, G. M., Oliveira, M. M., & Fernandes, L. A. F. (2009).
//  "A Physiologically-based Model for Simulation of Color Vision Deficiency."
//  IEEE Transactions on Visualization and Computer Graphics, 15(6), 1291–1298.
//
//  Important: matrices must be applied in LINEAR sRGB space, not gamma-encoded.
//  The correct pipeline is:
//    gamma-decode → apply simulation matrix → gamma-encode
//  Naive implementations that skip gamma expansion produce incorrect results,
//  especially in saturated regions (e.g., pure red desaturates incorrectly).
// ─────────────────────────────────────────────────────────────────────────────

namespace {

// Both transfer functions are tabulated once, so the per-pixel cost is three
// table reads, nine multiply-adds and three more table reads — no pow() calls.
struct GammaTables {
    static constexpr int kEncodeSize = 4096;
    float   toLinear[256];
    uint8_t toEncoded[kEncodeSize];   // index = linear value × (kEncodeSize - 1)

    GammaTables() {
        for (int v = 0; v < 256; ++v) {
            float c = v / 255.0f;
            toLinear[v] = (c <= 0.04045f) ? c / 12.92f
                                          : std::pow((c + 0.055f) / 1.055f, 2.4f);
        }
        for (int i = 0; i < kEncodeSize; ++i) {
            float c = static_cast<float>(i) / (kEncodeSize - 1);
            float enc = (c <= 0.0031308f) ? 12.92f * c
                                          : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
            toEncoded[i] = static_cast<uint8_t>(std::round(enc * 255.0f));
        }
    }

    uint8_t encode(float c) const {
        c = std::max(0.0f, std::min(1.0f, c));
        return toEncoded[static_cast<int>(c * (kEncodeSize - 1) + 0.5f)];
    }
};

const GammaTables& gamma() {
    static const GammaTables tables;
    return tables;
}

// Simulation matrices (operate on linear RGB, row-major, R→G→B order)
// Protanopia: complete absence of L-cone (red-sensitive)
const float M_prot[9] = {
    0.152286f, 1.052583f, -0.204868f,
    0.114503f, 0.786281f,  0.099216f,
   -0.003882f, -0.048116f, 1.051998f
};
// Deuteranopia: complete absence of M-cone (green-sensitive)
const float M_deut[9] = {
    0.367322f, 0.860646f, -0.227968f,
    0.280085f, 0.672501f,  0.047413f,
   -0.011820f, 0.042940f,  0.968881f
};
// Tritanopia: complete absence of S-cone (blue-sensitive)
const float M_trit[9] = {
    1.255528f, -0.076749f, -0.178779f,
   -0.078411f,  0.930809f,  0.147602f,
    0.004733f,  0.691367f,  0.303900f
};

const float* matrixFor(ColorblindType type) {
    switch (type) {
        case ColorblindType::Protanopia:   return M_prot;
        case ColorblindType::Deuteranopia: return M_deut;
        case ColorblindType::Tritanopia:   return M_trit;
        default:                           return nullptr;
    }
}

// BGR byte pixel → simulated BGR byte pixel
cv::Vec3b applyMatrix(const cv::Vec3b& px, const float M[9], const GammaTables& g) {
    float rl = g.toLinear[px[2]];
    float gl = g.toLinear[px[1]];
    float bl = g.toLinear[px[0]];
    return { g.encode(M[6]*rl + M[7]*gl + M[8]*bl),
             g.encode(M[3]*rl + M[4]*gl + M[5]*bl),
             g.encode(M[0]*rl + M[1]*gl + M[2]*bl) };
}

} // namespace

cv::Mat simulateColorblindness(const cv::Mat& bgrFrame, ColorblindType type) {
    const float* M = matrixFor(type);
    if (!M) return bgrFrame.clone();

    const GammaTables& g = gamma();
    cv::Mat result(bgrFrame.size(), bgrFrame.type());
    cv::parallel_for_(cv::Range(0, bgrFrame.rows), [&](const cv::Range& rows) {
        for (int row = rows.start; row < rows.end; ++row) {
            const cv::Vec3b* srcRow = bgrFrame.ptr<cv::Vec3b>(row);
            cv::Vec3b*       dstRow = result.ptr<cv::Vec3b>(row);
            for (int col = 0; col < bgrFrame.cols; ++col)
                dstRow[col] = applyMatrix(srcRow[col], M, g);
        }
    });
    return result;
}

cv::Vec3b simulateColorblindness(const cv::Vec3b& bgr, ColorblindType type) {
    const float* M = matrixFor(type);
    return M ? applyMatrix(bgr, M, gamma()) : bgr;
}

std::string colorblindTypeName(ColorblindType type) {
    switch (type) {
        case ColorblindType::Protanopia:   return "Protanopia (no L-cone)";
        case ColorblindType::Deuteranopia: return "Deuteranopia (no M-cone)";
        case ColorblindType::Tritanopia:   return "Tritanopia (no S-cone)";
        default:                           return "Normal vision";
    }
}

ColorblindType nextColorblindType(ColorblindType current) {
    switch (current) {
        case ColorblindType::None:         return ColorblindType::Protanopia;
        case ColorblindType::Protanopia:   return ColorblindType::Deuteranopia;
        case ColorblindType::Deuteranopia: return ColorblindType::Tritanopia;
        case ColorblindType::Tritanopia:   return ColorblindType::None;
    }
    return ColorblindType::None;
}
