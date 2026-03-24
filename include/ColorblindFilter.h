#pragma once
#include <opencv2/opencv.hpp>
#include <string>

// Clinical colorblindness types modeled by Daltonization matrices
enum class ColorblindType {
    None,
    Protanopia,    // red-cone (L-cone) absence   — affects ~1% of males
    Deuteranopia,  // green-cone (M-cone) absence — affects ~1% of males
    Tritanopia     // blue-cone (S-cone) absence   — affects ~0.003% of population
};

// Simulate colorblindness on a BGR frame.
//
// Implementation notes:
//   - Applies a linear Daltonization matrix in linearized sRGB space.
//   - Correct pipeline: gamma-expand → apply matrix → re-apply gamma.
//   - Naive implementations that skip gamma expansion produce incorrect results,
//     especially for saturated colors.
//   - Matrix values from Machado et al. 2009, "A Physiologically-based Model
//     for Simulation of Color Vision Deficiency".
cv::Mat simulateColorblindness(const cv::Mat& bgrFrame, ColorblindType type);

// Human-readable name for display overlay
std::string colorblindTypeName(ColorblindType type);

// Cycle to next type (None → Protanopia → Deuteranopia → Tritanopia → None)
ColorblindType nextColorblindType(ColorblindType current);
