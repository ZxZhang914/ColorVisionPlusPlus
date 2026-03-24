#pragma once
#include <string>
#include <vector>
#include <cmath>

// ── CIE color space structures ────────────────────────────────────────────────
struct XYZ { double X, Y, Z; };

struct Lab {
    double L, a, b;
};

// Named color entry (from XKCD color survey or similar dataset)
struct NamedColor {
    std::string name;
    int r, g, b;
    Lab lab;   // precomputed at dataset load time
};

// ── Color space conversions ───────────────────────────────────────────────────

// sRGB (0-255) → CIE XYZ (D65 illuminant, IEC 61966-2-1)
XYZ rgbToXyz(int r, int g, int b);

// CIE XYZ → CIE L*a*b* (D65 white point)
Lab xyzToLab(const XYZ& xyz);

// Convenience: sRGB directly to CIE L*a*b*
Lab rgbToLab(int r, int g, int b);

// ── CIEDE2000 perceptual color difference ─────────────────────────────────────
// Reference: Sharma et al., "The CIEDE2000 Color-Difference Formula",
//            Color Research & Application, 2005.
// Returns ΔE00 ∈ [0, ~100]; < 1 is imperceptible, > 10 is clearly different.
double deltaE2000(const Lab& c1, const Lab& c2);

// ── Named color palette ───────────────────────────────────────────────────────

// Returns the built-in palette (~120 colors sampled from the XKCD color survey).
// Lab values are precomputed. Call once and reuse.
const std::vector<NamedColor>& getBuiltinPalette();

// Load an extended palette from a CSV file (format: name,rrggbb per line).
// Falls back to getBuiltinPalette() if the file cannot be opened.
std::vector<NamedColor> loadPaletteFromCSV(const std::string& path);

// Find the nearest named color by ΔE2000 distance.
const NamedColor& nearestColor(const Lab& query, const std::vector<NamedColor>& palette);

// Classify an sRGB color by its nearest named color (ΔE2000 nearest-neighbor).
std::string classifyByDeltaE(int r, int g, int b, const std::vector<NamedColor>& palette);
