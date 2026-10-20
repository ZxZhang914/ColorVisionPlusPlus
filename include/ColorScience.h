#pragma once
#include <cstdint>
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

// Qualitative reading of a ΔE00 value: "imperceptible", "just noticeable",
// "clearly noticeable" or "very different".
const char* deltaELabel(double dE);

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

// The k nearest distinctly-named colors by ΔE2000, closest first.
struct ColorMatch {
    const NamedColor* color;
    double dE;
};
std::vector<ColorMatch> nearestColors(const Lab& query, const std::vector<NamedColor>& palette,
                                      size_t k);

// Memoised nearest-name lookup on a 6-bit-per-channel RGB grid (64³ cells).
// Each cell runs the exact ΔE2000 search once, on first use, at the cell centre;
// after that a lookup is a single array read. Meant for per-frame work where the
// same colors recur constantly (e.g. naming every block of the pixel-art filter).
class NamedColorCache {
public:
    explicit NamedColorCache(const std::vector<NamedColor>& palette);
    const NamedColor& lookup(int r, int g, int b);

private:
    const std::vector<NamedColor>* palette_;
    std::vector<int16_t> cell_;   // palette index, -1 = not computed yet
};
