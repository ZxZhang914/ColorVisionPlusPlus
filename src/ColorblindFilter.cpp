#include "ColorblindFilter.h"
#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
//  Colorblindness simulation via Daltonization
//
//  Reference: Machado, G. M., Oliveira, M. M., & Fernandes, L. A. F. (2009).
//  "A Physiologically-based Model for Simulation of Color Vision Deficiency."
//  IEEE Transactions on Visualization and Computer Graphics, 15(6), 1291–1298.
//
//  Important: matrices must be applied in LINEAR sRGB space, not gamma-encoded.
//  The correct pipeline is:
//    gamma-decode → apply Daltonization matrix → gamma-encode
//  Naive implementations that skip gamma expansion produce incorrect results,
//  especially in saturated regions (e.g., pure red desaturates incorrectly).
// ─────────────────────────────────────────────────────────────────────────────

// Gamma expand: sRGB encoded byte → linear float
static float toLinear(uint8_t v) {
    float c = v / 255.0f;
    return (c <= 0.04045f) ? c / 12.92f
                           : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

// Gamma compress: linear float → sRGB encoded byte
static uint8_t toEncoded(float c) {
    c = std::max(0.0f, std::min(1.0f, c));
    float enc = (c <= 0.0031308f) ? 12.92f * c
                                  : 1.055f * std::pow(c, 1.0f/2.4f) - 0.055f;
    return static_cast<uint8_t>(std::round(enc * 255.0f));
}

// Apply a row-major 3×3 matrix to (r,g,b) in linear space and return encoded
static cv::Vec3b applyMatrix(float rl, float gl, float bl, const float M[9]) {
    float ro = M[0]*rl + M[1]*gl + M[2]*bl;
    float go = M[3]*rl + M[4]*gl + M[5]*bl;
    float bo = M[6]*rl + M[7]*gl + M[8]*bl;
    return { toEncoded(bo), toEncoded(go), toEncoded(ro) }; // BGR order
}

cv::Mat simulateColorblindness(const cv::Mat& bgrFrame, ColorblindType type) {
    if (type == ColorblindType::None) return bgrFrame.clone();

    // Daltonization matrices (operate on linear RGB, row-major, R→G→B order)
    // Protanopia: complete absence of L-cone (red-sensitive)
    static const float M_prot[9] = {
        0.152286f, 1.052583f, -0.204868f,
        0.114503f, 0.786281f,  0.099216f,
       -0.003882f, -0.048116f, 1.051998f
    };
    // Deuteranopia: complete absence of M-cone (green-sensitive)
    static const float M_deut[9] = {
        0.367322f, 0.860646f, -0.227968f,
        0.280085f, 0.672501f,  0.047413f,
       -0.011820f, 0.042940f,  0.968881f
    };
    // Tritanopia: complete absence of S-cone (blue-sensitive)
    static const float M_trit[9] = {
        1.255528f, -0.076749f, -0.178779f,
       -0.078411f,  0.930809f,  0.147602f,
        0.004733f,  0.691367f,  0.303900f
    };

    const float* M = nullptr;
    switch (type) {
        case ColorblindType::Protanopia:   M = M_prot; break;
        case ColorblindType::Deuteranopia: M = M_deut; break;
        case ColorblindType::Tritanopia:   M = M_trit; break;
        default: return bgrFrame.clone();
    }

    cv::Mat result(bgrFrame.size(), bgrFrame.type());

    for (int row = 0; row < bgrFrame.rows; ++row) {
        const cv::Vec3b* srcRow = bgrFrame.ptr<cv::Vec3b>(row);
        cv::Vec3b*       dstRow = result.ptr<cv::Vec3b>(row);
        for (int col = 0; col < bgrFrame.cols; ++col) {
            const cv::Vec3b& px = srcRow[col];
            // BGR → linear RGB
            float rl = toLinear(px[2]);
            float gl = toLinear(px[1]);
            float bl = toLinear(px[0]);
            dstRow[col] = applyMatrix(rl, gl, bl, M);
        }
    }
    return result;
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
