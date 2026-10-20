// color_bench - Quantitative evaluation of color classification approaches
//
// Compares two classifiers on the built-in named-color palette:
//   1. Baseline: rule-based HSV threshold classifier (original ColorDetector)
//   2. dE2000:   CIE L*a*b* nearest-neighbor classifier
//
// Methodology:
//   - 80/20 train/test split (every 5th entry held out)
//   - Both classifiers are evaluated on the SAME broad-category task
//     (11 categories: red, orange, yellow, green, cyan, blue, purple,
//      pink, brown, white/gray/black) so the comparison is fair.
//   - Perceptual accuracy uses objective dE2000 distance, independent of naming.
//
// Key metric definitions:
//   - "Broad accuracy": classifier's output maps to the same broad color
//     category as the ground-truth color name (fair for both methods).
//   - "Mean dE2000 error": average perceptual distance from classifier output
//     to ground truth Lab - fully objective, no name matching needed.

#include "ColorDetector.h"
#include "ColorScience.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <string>
#include <vector>

// ── Coarse category mapping ────────────────────────────────────────────────
// Maps any color name to a broad 11-category label for fair comparison.
static std::string coarseCategory(const std::string& name) {
    auto n = name;
    for (auto& c : n) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    // Order matters - check more specific terms first
    if (n.find("black") != std::string::npos || n.find("charcoal") != std::string::npos)
        return "black";
    if (n.find("white") != std::string::npos || n.find("cream") != std::string::npos ||
        n.find("off white") != std::string::npos)
        return "white";
    if (n.find("gray") != std::string::npos || n.find("grey") != std::string::npos ||
        n.find("silver") != std::string::npos)
        return "gray";
    if (n.find("pink") != std::string::npos || n.find("salmon") != std::string::npos ||
        n.find("blush") != std::string::npos || n.find("rose") != std::string::npos ||
        n.find("coral") != std::string::npos || n.find("raspberry") != std::string::npos)
        return "pink";
    if (n.find("red") != std::string::npos || n.find("crimson") != std::string::npos ||
        n.find("scarlet") != std::string::npos || n.find("maroon") != std::string::npos ||
        n.find("cherry") != std::string::npos || n.find("wine") != std::string::npos ||
        n.find("burgundy") != std::string::npos || n.find("brick") != std::string::npos)
        return "red";
    if (n.find("orange") != std::string::npos || n.find("amber") != std::string::npos ||
        n.find("tangerine") != std::string::npos || n.find("rust") != std::string::npos ||
        n.find("pumpkin") != std::string::npos)
        return "orange";
    if (n.find("yellow") != std::string::npos || n.find("gold") != std::string::npos ||
        n.find("mustard") != std::string::npos || n.find("lemon") != std::string::npos ||
        n.find("banana") != std::string::npos)
        return "yellow";
    if (n.find("brown") != std::string::npos || n.find("chocolate") != std::string::npos ||
        n.find("coffee") != std::string::npos || n.find("caramel") != std::string::npos ||
        n.find("sienna") != std::string::npos || n.find("copper") != std::string::npos ||
        n.find("tan") != std::string::npos || n.find("beige") != std::string::npos ||
        n.find("khaki") != std::string::npos || n.find("sand") != std::string::npos)
        return "brown";
    if (n.find("cyan") != std::string::npos || n.find("teal") != std::string::npos ||
        n.find("aqua") != std::string::npos || n.find("turquoise") != std::string::npos ||
        n.find("seafoam") != std::string::npos || n.find("aquamarine") != std::string::npos ||
        n.find("jade") != std::string::npos)
        return "cyan";
    if (n.find("green") != std::string::npos || n.find("lime") != std::string::npos ||
        n.find("olive") != std::string::npos || n.find("forest") != std::string::npos ||
        n.find("emerald") != std::string::npos || n.find("mint") != std::string::npos ||
        n.find("sage") != std::string::npos || n.find("chartreuse") != std::string::npos ||
        n.find("grass") != std::string::npos || n.find("pine") != std::string::npos ||
        n.find("sea green") != std::string::npos || n.find("army") != std::string::npos ||
        n.find("swamp") != std::string::npos || n.find("pea") != std::string::npos ||
        n.find("kelly") != std::string::npos || n.find("hunter") != std::string::npos)
        return "green";
    if (n.find("purple") != std::string::npos || n.find("violet") != std::string::npos ||
        n.find("magenta") != std::string::npos || n.find("fuchsia") != std::string::npos ||
        n.find("lavender") != std::string::npos || n.find("lilac") != std::string::npos ||
        n.find("orchid") != std::string::npos || n.find("wisteria") != std::string::npos ||
        n.find("plum") != std::string::npos || n.find("grape") != std::string::npos ||
        n.find("mauve") != std::string::npos || n.find("eggplant") != std::string::npos ||
        n.find("indigo") != std::string::npos)
        return "purple";
    if (n.find("blue") != std::string::npos || n.find("navy") != std::string::npos ||
        n.find("cobalt") != std::string::npos || n.find("sky") != std::string::npos ||
        n.find("cerulean") != std::string::npos || n.find("denim") != std::string::npos ||
        n.find("periwinkle") != std::string::npos || n.find("ocean") != std::string::npos ||
        n.find("steel blue") != std::string::npos || n.find("powder blue") != std::string::npos ||
        n.find("cornflower") != std::string::npos || n.find("electric blue") != std::string::npos ||
        n.find("midnight blue") != std::string::npos || n.find("baby blue") != std::string::npos ||
        n.find("royal blue") != std::string::npos || n.find("bright blue") != std::string::npos ||
        n.find("light blue") != std::string::npos || n.find("dark blue") != std::string::npos)
        return "blue";
    return "unknown";
}

// Map HSV classifier output to same broad category
static std::string hsvToCoarse(const std::string& hsvOut) {
    std::string s = hsvOut;
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (s == "grey") return "gray";
    return s;
}

// ── Pretty printing ───────────────────────────────────────────────────────────
static void printSeparator(char c = '-', int w = 68) {
    std::cout << std::string(w, c) << '\n';
}
static void printBar(const std::string& label, double value,
                     double maxVal, int barWidth = 30) {
    int filled = static_cast<int>(std::round(value / maxVal * barWidth));
    filled = std::max(0, std::min(filled, barWidth));
    std::cout << "  " << std::left << std::setw(18) << label
              << " [" << std::string(filled, '#')
                     << std::string(barWidth - filled, ' ')
              << "] " << std::fixed << std::setprecision(1) << value << "%\n";
}

// ─────────────────────────────────────────────────────────────────────────────
int main() {
    using Clock = std::chrono::high_resolution_clock;

    std::cout << "\n";
    printSeparator('=');
    std::cout << "  ColorVision++ Benchmark - Color Classification Evaluation\n";
    printSeparator('=');

    // ── Load dataset ────────────────────────────────────────────────────────
    const auto& fullPalette = getBuiltinPalette();
    std::cout << "\nDataset  : built-in palette (" << fullPalette.size()
              << " named colors, sampled from XKCD color survey)\n";

    // 80/20 split: every 5th entry goes to test set
    std::vector<const NamedColor*> trainSet, testSet;
    for (size_t i = 0; i < fullPalette.size(); ++i) {
        if (i % 5 == 4) testSet.push_back(&fullPalette[i]);
        else             trainSet.push_back(&fullPalette[i]);
    }
    std::cout << "Split    : " << trainSet.size() << " train / "
              << testSet.size() << " test\n";
    std::cout << "Task     : broad color category classification (11 classes)\n\n";

    // Build dE2000 lookup palette from train set
    std::vector<NamedColor> trainPalette;
    trainPalette.reserve(trainSet.size());
    for (const auto* nc : trainSet) trainPalette.push_back(*nc);

    ColorDetector hsvClassifier;

    // ── Evaluation ──────────────────────────────────────────────────────────
    int    hsvCorrect = 0, deCorrect = 0;
    double hsvTotalDE = 0.0, deTotalDE = 0.0;
    long long hsvNs = 0, deNs = 0;
    int    unknown = 0;

    for (const auto* nc : testSet) {
        std::string gtCategory = coarseCategory(nc->name);
        if (gtCategory == "unknown") { unknown++; continue; }

        int r = nc->r, g = nc->g, b = nc->b;

        // ── HSV classifier ────────────────────────────────────────────────
        auto t0 = Clock::now();
        std::string hsvOut = hsvClassifier.classifyRGB(r, g, b);
        hsvNs += (Clock::now() - t0).count();

        std::string hsvCat = hsvToCoarse(hsvOut);
        if (hsvCat == gtCategory) hsvCorrect++;

        // Perceptual dE: find mean representative Lab for HSV category
        double hsvDE = std::numeric_limits<double>::max();
        for (const auto& pc : getBuiltinPalette()) {
            if (coarseCategory(pc.name) == hsvCat) {
                double de = deltaE2000(nc->lab, pc.lab);
                hsvDE = std::min(hsvDE, de);
            }
        }
        if (hsvDE == std::numeric_limits<double>::max()) hsvDE = 35.0;
        hsvTotalDE += hsvDE;

        // ── dE2000 classifier ─────────────────────────────────────────────
        t0 = Clock::now();
        Lab queryLab = rgbToLab(r, g, b);
        const NamedColor& nearest = nearestColor(queryLab, trainPalette);
        deNs += (Clock::now() - t0).count();

        std::string deCat = coarseCategory(nearest.name);
        if (deCat == gtCategory) deCorrect++;

        double de = deltaE2000(nc->lab, nearest.lab);
        deTotalDE += de;
    }

    int N = static_cast<int>(testSet.size()) - unknown;
    if (N <= 0) { std::cerr << "No evaluable test samples.\n"; return 1; }

    double hsvAcc  = 100.0 * hsvCorrect / N;
    double deAcc   = 100.0 * deCorrect  / N;
    double hsvMDE  = hsvTotalDE / N;
    double deMDE   = deTotalDE  / N;
    double hsvTput = 1e9 / (static_cast<double>(hsvNs) / N);
    double deTput  = 1e9 / (static_cast<double>(deNs)  / N);

    // ── Results ─────────────────────────────────────────────────────────────
    printSeparator();
    std::cout << "  RESULTS  (N = " << N << " evaluable test samples)\n";
    printSeparator();

    std::cout << "\n  Broad-Category Accuracy (11 classes, same task for both):\n";
    printBar("HSV baseline",   hsvAcc, 100.0);
    printBar("dE2000 nearest", deAcc,  100.0);

    std::cout << "\n  Mean dE2000 Error  (lower = better)\n"
              << "    < 1.0  imperceptible\n"
              << "    < 2.0  just noticeable\n"
              << "    < 10.0 clearly noticeable\n\n";

    std::cout << "  HSV baseline   : " << std::fixed << std::setprecision(2)
              << hsvMDE << "  (" << deltaELabel(hsvMDE) << ")\n";
    std::cout << "  dE2000 nearest : " << std::fixed << std::setprecision(2)
              << deMDE  << "  (" << deltaELabel(deMDE)  << ")\n";

    std::cout << "\n  Throughput:\n";
    std::cout << "  HSV baseline   : " << std::fixed << std::setprecision(0)
              << hsvTput / 1e6 << "M cls/sec\n";
    std::cout << "  dE2000 nearest : " << std::fixed << std::setprecision(0)
              << deTput / 1e3  << "K cls/sec\n";

    printSeparator();
    std::cout << "\n  Summary:\n";
    double accGain = deAcc - hsvAcc;
    double deGain  = (hsvMDE - deMDE) / hsvMDE * 100.0;
    std::cout << "    Category accuracy gain  : "
              << (accGain >= 0 ? "+" : "") << std::fixed << std::setprecision(1)
              << accGain << " pp\n";
    std::cout << "    dE error reduction      : "
              << std::fixed << std::setprecision(2) << hsvMDE
              << " -> " << deMDE
              << "  (" << std::fixed << std::setprecision(0) << deGain << "% improvement)\n";
    std::cout << "    HSV speed advantage     : "
              << std::fixed << std::setprecision(0)
              << hsvTput / deTput << "x\n";
    printSeparator('=');
    std::cout << "\n  Note: HSV classifier recognises 10 broad categories.\n"
              << "  dE2000 classifier identifies specific named colors then\n"
              << "  maps to the same 11-class task for a fair comparison.\n\n";
    return 0;
}
