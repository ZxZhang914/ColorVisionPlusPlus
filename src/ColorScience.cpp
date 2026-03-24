#include "ColorScience.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <limits>

// ─────────────────────────────────────────────────────────────────────────────
//  sRGB → CIE XYZ  (D65 illuminant, IEC 61966-2-1)
// ─────────────────────────────────────────────────────────────────────────────
static double linearize(double c) {
    // Gamma expansion: sRGB encoded value → linear light
    c /= 255.0;
    return (c <= 0.04045) ? c / 12.92
                          : std::pow((c + 0.055) / 1.055, 2.4);
}

XYZ rgbToXyz(int r, int g, int b) {
    double rl = linearize(r);
    double gl = linearize(g);
    double bl = linearize(b);

    // D65 adaptation matrix (IEC 61966-2-1)
    return {
        0.4124564 * rl + 0.3575761 * gl + 0.1804375 * bl,
        0.2126729 * rl + 0.7151522 * gl + 0.0721750 * bl,
        0.0193339 * rl + 0.1191920 * gl + 0.9503041 * bl
    };
}

// ─────────────────────────────────────────────────────────────────────────────
//  CIE XYZ → CIE L*a*b*  (D65 white point)
// ─────────────────────────────────────────────────────────────────────────────
static double labF(double t) {
    // Piecewise cube-root function defined in CIE 15:2004
    static const double delta  = 6.0 / 29.0;
    static const double delta2 = delta * delta;
    static const double delta3 = delta * delta * delta;
    return (t > delta3) ? std::cbrt(t)
                        : t / (3.0 * delta2) + 4.0 / 29.0;
}

Lab xyzToLab(const XYZ& xyz) {
    // D65 white point (CIE standard illuminant)
    static const double Xn = 0.95047;
    static const double Yn = 1.00000;
    static const double Zn = 1.08883;

    double fx = labF(xyz.X / Xn);
    double fy = labF(xyz.Y / Yn);
    double fz = labF(xyz.Z / Zn);

    return {
        116.0 * fy - 16.0,   // L*
        500.0 * (fx - fy),   // a*
        200.0 * (fy - fz)    // b*
    };
}

Lab rgbToLab(int r, int g, int b) {
    return xyzToLab(rgbToXyz(r, g, b));
}

// ─────────────────────────────────────────────────────────────────────────────
//  CIEDE2000 Color Difference
//  Sharma, G., Wu, W., & Dalal, E. N. (2005). The CIEDE2000 Color-Difference
//  Formula: Implementation Notes, Supplementary Test Data, and Mathematical
//  Observations. Color Research & Application, 30(1), 21–30.
// ─────────────────────────────────────────────────────────────────────────────
static double deg2rad(double deg) { return deg * M_PI / 180.0; }
static double rad2deg(double rad) { return rad * 180.0 / M_PI; }

double deltaE2000(const Lab& c1, const Lab& c2) {
    // Step 1: Compute C*ab (chroma in a*b* plane)
    double C1 = std::hypot(c1.a, c1.b);
    double C2 = std::hypot(c2.a, c2.b);

    // Step 2: G factor — adjusts a* to account for CIE94 failures near blue
    double Cmean7 = std::pow((C1 + C2) / 2.0, 7.0);
    static const double p25_7 = 6103515625.0; // 25^7
    double G = 0.5 * (1.0 - std::sqrt(Cmean7 / (Cmean7 + p25_7)));

    // Step 3: Adjusted a'
    double a1p = c1.a * (1.0 + G);
    double a2p = c2.a * (1.0 + G);

    // Step 4: C' (adjusted chroma)
    double C1p = std::hypot(a1p, c1.b);
    double C2p = std::hypot(a2p, c2.b);

    // Step 5: h' (adjusted hue angle in degrees, [0, 360))
    auto hprime = [](double ap, double b) -> double {
        if (ap == 0.0 && b == 0.0) return 0.0;
        double h = rad2deg(std::atan2(b, ap));
        return (h < 0.0) ? h + 360.0 : h;
    };
    double h1p = hprime(a1p, c1.b);
    double h2p = hprime(a2p, c2.b);

    // Step 6: ΔL', ΔC'
    double dLp = c2.L - c1.L;
    double dCp = C2p - C1p;

    // Step 7: Δh'
    double dhp = 0.0;
    if (C1p * C2p == 0.0) {
        dhp = 0.0;
    } else {
        double dh = h2p - h1p;
        if      (std::abs(dh) <= 180.0) dhp = dh;
        else if (dh > 180.0)            dhp = dh - 360.0;
        else                            dhp = dh + 360.0;
    }

    // Step 8: ΔH'
    double dHp = 2.0 * std::sqrt(C1p * C2p) * std::sin(deg2rad(dhp / 2.0));

    // Step 9: Arithmetic means L̄', C̄'
    double Lbarp = (c1.L + c2.L) / 2.0;
    double Cbarp = (C1p + C2p) / 2.0;

    // Step 10: h̄' (mean hue)
    double hbarp = 0.0;
    if (C1p * C2p == 0.0) {
        hbarp = h1p + h2p;
    } else {
        double dh = std::abs(h1p - h2p);
        double sh = h1p + h2p;
        if      (dh <= 180.0)   hbarp = sh / 2.0;
        else if (sh < 360.0)    hbarp = (sh + 360.0) / 2.0;
        else                    hbarp = (sh - 360.0) / 2.0;
    }

    // Step 11: T weighting function
    double T = 1.0
        - 0.17 * std::cos(deg2rad(hbarp - 30.0))
        + 0.24 * std::cos(deg2rad(2.0 * hbarp))
        + 0.32 * std::cos(deg2rad(3.0 * hbarp + 6.0))
        - 0.20 * std::cos(deg2rad(4.0 * hbarp - 63.0));

    // Step 12: Weighting functions S_L, S_C, S_H
    double Lbar50sq = (Lbarp - 50.0) * (Lbarp - 50.0);
    double S_L = 1.0 + 0.015 * Lbar50sq / std::sqrt(20.0 + Lbar50sq);
    double S_C = 1.0 + 0.045 * Cbarp;
    double S_H = 1.0 + 0.015 * Cbarp * T;

    // Step 13: Rotation term R_T (accounts for blue-purple region anomaly)
    double Cbarp7 = std::pow(Cbarp, 7.0);
    double R_C = 2.0 * std::sqrt(Cbarp7 / (Cbarp7 + p25_7));
    double dtheta = 30.0 * std::exp(-std::pow((hbarp - 275.0) / 25.0, 2.0));
    double R_T = -std::sin(deg2rad(2.0 * dtheta)) * R_C;

    // Step 14: Final ΔE00  (kL = kC = kH = 1)
    double lTerm = dLp / S_L;
    double cTerm = dCp / S_C;
    double hTerm = dHp / S_H;

    return std::sqrt(lTerm * lTerm + cTerm * cTerm + hTerm * hTerm
                     + R_T * cTerm * hTerm);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Built-in color palette (representative sample from XKCD color survey)
//  Monroe, R. (2010). XKCD Color Survey. https://blog.xkcd.com/2010/05/03/color-survey-results/
// ─────────────────────────────────────────────────────────────────────────────
static std::vector<NamedColor> buildBuiltinPalette() {
    struct RawColor { const char* name; int r, g, b; };

    // ~120 colors sampled across the hue range from the XKCD survey
    static const RawColor raw[] = {
        // Reds
        {"red",              229,   0,   0}, {"bright red",       255,   0,  13},
        {"dark red",        132,   0,   0}, {"crimson",          220,   0,  60},
        {"scarlet",         190,   1,  25}, {"rose",             207,  98, 117},
        {"raspberry",       176,   1,  73}, {"cherry",           207,   2,  52},
        {"maroon",          101,   0,  33}, {"blood red",       152,   0,   2},
        {"tomato red",      236,  45,  1}, {"brick red",        143,  20,   2},
        // Oranges
        {"orange",          249, 115,   6}, {"burnt orange",    192,  78,   1},
        {"dark orange",    200,  90,   1}, {"light orange",    253, 170,  72},
        {"amber",           255, 191,   0}, {"tangerine",       255, 148,   8},
        {"pumpkin",         225,  91,   0}, {"rust",            166,  47,   3},
        // Yellows
        {"yellow",          255, 255,  20}, {"bright yellow",   255, 253,   1},
        {"golden",          245, 182,   1}, {"mustard",         206, 179,   1},
        {"lemon yellow",   253, 255,  56}, {"banana yellow",   250, 254, 100},
        {"sand yellow",    252, 225, 102}, {"gold",             219, 180,  12},
        // Yellow-greens
        {"yellow green",   192, 251,  45}, {"lime",            170, 255,  50},
        {"lime green",      137, 254,   5}, {"chartreuse",     193, 248,  10},
        // Greens
        {"green",           21, 176,  26}, {"bright green",    1, 255,   7},
        {"dark green",       3,  53,   0}, {"forest green",    6,  71,  12},
        {"grass green",     63, 155,  11}, {"emerald",         1, 160,  73},
        {"olive green",     103, 122,   4}, {"pine green",     10,  72,  30},
        {"sea green",       83, 252, 161}, {"sage green",     135, 174, 115},
        {"mint green",     143, 255, 159}, {"pale green",     199, 253, 181},
        {"kelly green",     2, 171,  46}, {"hunter green",    11,  64,   8},
        // Cyan / Teals
        {"cyan",              0, 255, 255}, {"teal",              1, 128, 128},
        {"dark teal",         1,  77,  78}, {"light teal",       144, 228, 193},
        {"aqua",              19, 234, 201}, {"turquoise",        6, 194, 172},
        {"seafoam",          146, 254, 219}, {"aquamarine",       4, 216, 178},
        {"jade",              31, 167,  116}, {"cerulean",        4, 133, 209},
        // Blues
        {"blue",             3,  67, 223}, {"bright blue",    1,  56, 255},
        {"dark blue",        0,   3, 139}, {"navy blue",        0,  17, 70},
        {"cobalt blue",    30,  72, 143}, {"royal blue",       8,  76, 158},
        {"sky blue",       117, 187, 253}, {"light blue",     173, 215, 255},
        {"baby blue",      162, 207, 254}, {"cornflower blue",100, 149, 237},
        {"powder blue",    185, 209, 229}, {"steel blue",      70, 130, 180},
        {"midnight blue",    2,   0,  53}, {"electric blue",   6,  82, 255},
        {"denim",           59,  91, 146}, {"ocean blue",       3,  80, 150},
        // Blue-purples / Violets
        {"indigo",          56,   2, 130}, {"dark indigo",     31,   9,  84},
        {"purple",         126,  30, 156}, {"violet",          154,  14, 234},
        {"blue purple",    90,   6, 239}, {"medium purple",    137, 58, 209},
        // Purples / Magentas
        {"magenta",        194,   0, 120}, {"hot pink",       255,   2, 141},
        {"fuchsia",        237,  13, 217}, {"orchid",         218, 112, 214},
        {"lavender",       190, 153, 255}, {"light lavender",  223, 197, 254},
        {"plum",          112,  41, 99},  {"grape",           111,   1,  98},
        {"wisteria",       168, 124, 207},
        // Pinks
        {"pink",           255, 129, 192}, {"light pink",     255, 209, 223},
        {"pale pink",      255, 207, 220}, {"hot pink",       255,   2, 141},
        {"salmon",         255, 121, 108}, {"coral",          255,  88,  71},
        {"baby pink",      255, 183, 206}, {"blush",          242, 158, 163},
        // Browns
        {"brown",          101,  55,   0}, {"dark brown",      52,  28,   2},
        {"light brown",    173, 117,  42}, {"tan",            209, 178, 111},
        {"beige",          230, 218, 166}, {"khaki",          170, 166,  98},
        {"chocolate",      61,   7,   1}, {"coffee",         166, 129,  76},
        {"caramel",        255, 198, 118}, {"sand",           226, 202, 118},
        {"sienna",         169,  56,   6}, {"copper",         182,  99,  37},
        // Grays
        {"white",          255, 255, 255}, {"light gray",     215, 215, 215},
        {"gray",           146, 149, 145}, {"dark gray",       84,  84,  84},
        {"charcoal",        52,  52,  52}, {"black",            0,   0,   0},
        {"off white",      255, 255, 228}, {"cream",          255, 255, 204},
        {"light grey",    210, 211, 208}, {"silver",          196, 199, 200},
        // Others
        {"olive",          110, 117,  14}, {"army green",      75,  93,  22},
        {"pea green",      164, 191,  32}, {"swamp green",     116, 133,   0},
        {"peach",          255, 176, 124}, {"apricot",         251, 193, 137},
        {"lilac",          206, 167, 211}, {"periwinkle",     143, 153, 251},
        {"eggplant",        56,   8,  53}, {"wine",           128,   1,  63},
        {"burgundy",       97,   0,  35},  {"mauve",          174, 113, 129},
    };

    std::vector<NamedColor> palette;
    palette.reserve(sizeof(raw) / sizeof(raw[0]));
    for (const auto& c : raw) {
        NamedColor nc;
        nc.name = c.name;
        nc.r    = c.r;
        nc.g    = c.g;
        nc.b    = c.b;
        nc.lab  = rgbToLab(c.r, c.g, c.b);
        palette.push_back(std::move(nc));
    }
    return palette;
}

const std::vector<NamedColor>& getBuiltinPalette() {
    static std::vector<NamedColor> palette = buildBuiltinPalette();
    return palette;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Load palette from CSV file (format: name,rrggbb)
// ─────────────────────────────────────────────────────────────────────────────
std::vector<NamedColor> loadPaletteFromCSV(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) return getBuiltinPalette();

    std::vector<NamedColor> palette;
    std::string line;
    while (std::getline(f, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto comma = line.rfind(',');
        if (comma == std::string::npos) continue;

        std::string name = line.substr(0, comma);
        std::string hex  = line.substr(comma + 1);
        // strip whitespace
        while (!name.empty() && name.back() == ' ') name.pop_back();
        while (!hex.empty()  && (hex.front() == ' ' || hex.front() == '#')) hex.erase(hex.begin());
        if (hex.size() < 6) continue;

        try {
            int r = std::stoi(hex.substr(0, 2), nullptr, 16);
            int g = std::stoi(hex.substr(2, 2), nullptr, 16);
            int b = std::stoi(hex.substr(4, 2), nullptr, 16);
            NamedColor nc;
            nc.name = name;
            nc.r = r; nc.g = g; nc.b = b;
            nc.lab = rgbToLab(r, g, b);
            palette.push_back(std::move(nc));
        } catch (...) { continue; }
    }
    return palette.empty() ? getBuiltinPalette() : palette;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Nearest named color by ΔE2000
// ─────────────────────────────────────────────────────────────────────────────
const NamedColor& nearestColor(const Lab& query, const std::vector<NamedColor>& palette) {
    // Linear scan: 120 entries runs in ~1 µs — O(n) is appropriate here.
    // A KD-tree would give O(log n) but is overkill for palette sizes <1000.
    size_t best = 0;
    double bestDE = std::numeric_limits<double>::max();
    for (size_t i = 0; i < palette.size(); ++i) {
        double de = deltaE2000(query, palette[i].lab);
        if (de < bestDE) { bestDE = de; best = i; }
    }
    return palette[best];
}

std::string classifyByDeltaE(int r, int g, int b, const std::vector<NamedColor>& palette) {
    Lab lab = rgbToLab(r, g, b);
    return nearestColor(lab, palette).name;
}
