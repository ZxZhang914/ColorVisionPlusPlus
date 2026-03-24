#include "DominantColors.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>

// ── Euclidean distance in Lab space ───────────────────────────────────────────
static double labDist2(const Lab& a, const Lab& b) {
    double dL = a.L - b.L, da = a.a - b.a, db = a.b - b.b;
    return dL*dL + da*da + db*db;
}

// ── K-Means++ centroid seeding ────────────────────────────────────────────────
// D² weighting: each subsequent centroid is chosen with probability proportional
// to its squared distance from the nearest already-chosen centroid.
// Ref: Arthur & Vassilvitskii, "k-means++: The Advantages of Careful Seeding"
//      SODA 2007.
static std::vector<Lab> kmeansPlusPlusInit(
    const std::vector<Lab>& pixels, int K, std::mt19937& rng)
{
    std::vector<Lab> centers;
    centers.reserve(K);

    // First centroid: uniform random
    std::uniform_int_distribution<size_t> firstPick(0, pixels.size() - 1);
    centers.push_back(pixels[firstPick(rng)]);

    std::vector<double> d2(pixels.size());

    for (int k = 1; k < K; ++k) {
        // Compute D² distance from each pixel to its nearest center
        double total = 0.0;
        for (size_t i = 0; i < pixels.size(); ++i) {
            double minD2 = std::numeric_limits<double>::max();
            for (const auto& c : centers)
                minD2 = std::min(minD2, labDist2(pixels[i], c));
            d2[i] = minD2;
            total += minD2;
        }

        // Sample proportional to D²
        std::uniform_real_distribution<double> spin(0.0, total);
        double r = spin(rng);
        double cumulative = 0.0;
        size_t chosen = pixels.size() - 1;
        for (size_t i = 0; i < pixels.size(); ++i) {
            cumulative += d2[i];
            if (cumulative >= r) { chosen = i; break; }
        }
        centers.push_back(pixels[chosen]);
    }
    return centers;
}

// ── Lab → approximate sRGB (for display only) ────────────────────────────────
// Inverse path: Lab → XYZ → linear RGB → gamma-compressed sRGB
static int clampByte(double v) {
    return static_cast<int>(std::max(0.0, std::min(255.0, std::round(v))));
}

static std::tuple<int,int,int> labToRgb(const Lab& lab) {
    // Lab → XYZ (D65)
    static const double Xn = 0.95047, Yn = 1.0, Zn = 1.08883;
    auto fInv = [](double t) {
        static const double delta = 6.0/29.0;
        return (t > delta) ? t*t*t : 3.0*delta*delta*(t - 4.0/29.0);
    };
    double fy = (lab.L + 16.0) / 116.0;
    double fx = lab.a / 500.0 + fy;
    double fz = fy - lab.b / 200.0;

    double X = Xn * fInv(fx);
    double Y = Yn * fInv(fy);
    double Z = Zn * fInv(fz);

    // XYZ → linear sRGB (D65)
    double rl =  3.2404542 * X - 1.5371385 * Y - 0.4985314 * Z;
    double gl = -0.9692660 * X + 1.8760108 * Y + 0.0415560 * Z;
    double bl =  0.0556434 * X - 0.2040259 * Y + 1.0572252 * Z;

    // Gamma compression
    auto gamma = [](double c) {
        c = std::max(0.0, c);
        return (c <= 0.0031308) ? 12.92 * c
                                : 1.055 * std::pow(c, 1.0/2.4) - 0.055;
    };
    return { clampByte(gamma(rl)*255), clampByte(gamma(gl)*255), clampByte(gamma(bl)*255) };
}

// ─────────────────────────────────────────────────────────────────────────────
//  Main function: extract K dominant colors via K-Means++ in Lab space
// ─────────────────────────────────────────────────────────────────────────────
std::vector<DominantColor> extractDominantColors(
    const cv::Mat& bgrFrame,
    const std::vector<NamedColor>& palette,
    int K, int maxIter, int sampleSize)
{
    if (bgrFrame.empty() || K <= 0) return {};

    // ── Sample pixels randomly ──────────────────────────────────────────────
    std::mt19937 rng(42);  // fixed seed for deterministic results per frame
    int totalPixels = bgrFrame.rows * bgrFrame.cols;
    int actualSample = std::min(sampleSize, totalPixels);

    std::vector<int> indices(totalPixels);
    std::iota(indices.begin(), indices.end(), 0);
    std::shuffle(indices.begin(), indices.end(), rng);
    indices.resize(actualSample);

    // ── Convert sampled pixels to Lab ───────────────────────────────────────
    std::vector<Lab> pixels;
    pixels.reserve(actualSample);
    for (int idx : indices) {
        int row = idx / bgrFrame.cols;
        int col = idx % bgrFrame.cols;
        auto px = bgrFrame.at<cv::Vec3b>(row, col);
        pixels.push_back(rgbToLab(px[2], px[1], px[0])); // BGR → RGB
    }

    // ── K-Means++ initialization ────────────────────────────────────────────
    std::vector<Lab> centers = kmeansPlusPlusInit(pixels, K, rng);
    std::vector<int> assignments(actualSample, 0);

    // ── K-Means iteration ───────────────────────────────────────────────────
    // Clustering in Lab Euclidean space is perceptually much better than RGB
    // Euclidean, because Lab is designed to be perceptually uniform.
    for (int iter = 0; iter < maxIter; ++iter) {
        bool changed = false;

        // Assignment step
        for (int i = 0; i < actualSample; ++i) {
            int best = 0;
            double bestD = std::numeric_limits<double>::max();
            for (int k = 0; k < K; ++k) {
                double d = labDist2(pixels[i], centers[k]);
                if (d < bestD) { bestD = d; best = k; }
            }
            if (assignments[i] != best) { assignments[i] = best; changed = true; }
        }
        if (!changed) break;

        // Update step: recompute centroids as mean of assigned pixels
        std::vector<Lab> newCenters(K, {0,0,0});
        std::vector<int> counts(K, 0);
        for (int i = 0; i < actualSample; ++i) {
            int k = assignments[i];
            newCenters[k].L += pixels[i].L;
            newCenters[k].a += pixels[i].a;
            newCenters[k].b += pixels[i].b;
            counts[k]++;
        }
        for (int k = 0; k < K; ++k) {
            if (counts[k] > 0) {
                newCenters[k].L /= counts[k];
                newCenters[k].a /= counts[k];
                newCenters[k].b /= counts[k];
                centers[k] = newCenters[k];
            }
        }
    }

    // ── Count cluster sizes ─────────────────────────────────────────────────
    std::vector<int> counts(K, 0);
    for (int a : assignments) counts[a]++;

    // ── Build result: sort by cluster size (most dominant first) ───────────
    std::vector<int> order(K);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(),
              [&](int a, int b){ return counts[a] > counts[b]; });

    std::vector<DominantColor> result;
    result.reserve(K);
    for (int k : order) {
        DominantColor dc;
        dc.lab      = centers[k];
        dc.fraction = static_cast<float>(counts[k]) / actualSample;
        std::tie(dc.r, dc.g, dc.b) = labToRgb(centers[k]);

        // Name the centroid using ΔE2000 — applied K times total, not per-pixel
        const NamedColor& nc = nearestColor(centers[k], palette);
        dc.nearestName = nc.name;
        dc.nearestDE   = deltaE2000(centers[k], nc.lab);
        result.push_back(dc);
    }
    return result;
}
