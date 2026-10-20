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

// ── Random pixel sample, converted to Lab ─────────────────────────────────────
static std::vector<Lab> samplePixels(const cv::Mat& bgrFrame, int sampleSize, std::mt19937& rng) {
    int totalPixels = bgrFrame.rows * bgrFrame.cols;
    int n = std::min(sampleSize, totalPixels);
    std::uniform_int_distribution<int> pick(0, totalPixels - 1);

    std::vector<Lab> pixels;
    pixels.reserve(n);
    for (int i = 0; i < n; ++i) {
        int idx = pick(rng);
        auto px = bgrFrame.at<cv::Vec3b>(idx / bgrFrame.cols, idx % bgrFrame.cols);
        pixels.push_back(rgbToLab(px[2], px[1], px[0])); // BGR → RGB
    }
    return pixels;
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

// ── Lloyd iterations ──────────────────────────────────────────────────────────
// Clustering in Lab Euclidean space is perceptually much better than RGB
// Euclidean, because Lab is designed to be perceptually uniform.
// Returns the final cluster sizes.
static std::vector<int> runLloyd(const std::vector<Lab>& pixels,
                                 std::vector<Lab>& centers, int maxIter)
{
    const int K = static_cast<int>(centers.size());
    const int n = static_cast<int>(pixels.size());
    std::vector<int> assignments(n, -1);
    std::vector<int> counts(K, 0);

    for (int iter = 0; iter < maxIter; ++iter) {
        bool changed = false;

        // Assignment step
        for (int i = 0; i < n; ++i) {
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
        std::vector<Lab> sums(K, {0,0,0});
        std::fill(counts.begin(), counts.end(), 0);
        for (int i = 0; i < n; ++i) {
            int k = assignments[i];
            sums[k].L += pixels[i].L;
            sums[k].a += pixels[i].a;
            sums[k].b += pixels[i].b;
            counts[k]++;
        }
        for (int k = 0; k < K; ++k) {
            if (counts[k] > 0) {
                centers[k] = { sums[k].L / counts[k], sums[k].a / counts[k], sums[k].b / counts[k] };
            } else {
                // Empty cluster: re-seed it on the worst-fitted pixel so it
                // picks up whatever new color entered the scene.
                int worst = 0;
                double worstD = -1.0;
                for (int i = 0; i < n; ++i) {
                    double d = labDist2(pixels[i], centers[assignments[i]]);
                    if (d > worstD) { worstD = d; worst = i; }
                }
                centers[k] = pixels[worst];
            }
        }
    }

    std::fill(counts.begin(), counts.end(), 0);
    for (int a : assignments) counts[a]++;
    return counts;
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

// Name each centroid and sort by share, most dominant first.
// ΔE2000 naming is applied K times total, not per-pixel.
static std::vector<DominantColor> describe(const std::vector<Lab>& centers,
                                           const std::vector<float>& shares,
                                           const std::vector<NamedColor>& palette)
{
    std::vector<DominantColor> result;
    result.reserve(centers.size());
    for (size_t k = 0; k < centers.size(); ++k) {
        DominantColor dc;
        dc.lab      = centers[k];
        dc.fraction = shares[k];
        std::tie(dc.r, dc.g, dc.b) = labToRgb(centers[k]);

        const NamedColor& nc = nearestColor(centers[k], palette);
        dc.nearestName = nc.name;
        dc.nearestDE   = deltaE2000(centers[k], nc.lab);
        result.push_back(dc);
    }
    std::stable_sort(result.begin(), result.end(),
                     [](const DominantColor& a, const DominantColor& b) { return a.fraction > b.fraction; });
    return result;
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

    std::mt19937 rng(42);  // fixed seed for deterministic results per frame
    std::vector<Lab> pixels  = samplePixels(bgrFrame, sampleSize, rng);
    std::vector<Lab> centers = kmeansPlusPlusInit(pixels, K, rng);
    std::vector<int> counts  = runLloyd(pixels, centers, maxIter);

    std::vector<float> shares(K);
    for (int k = 0; k < K; ++k) shares[k] = static_cast<float>(counts[k]) / pixels.size();
    return describe(centers, shares, palette);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Temporally coherent tracker for video
// ─────────────────────────────────────────────────────────────────────────────
DominantColorTracker::DominantColorTracker(int K, int sampleSize)
    : K_(K), sampleSize_(sampleSize), rng_(42) {}

const std::vector<DominantColor>& DominantColorTracker::update(
    const cv::Mat& bgrFrame, const std::vector<NamedColor>& palette)
{
    if (bgrFrame.empty() || K_ <= 0) return result_;

    std::vector<Lab> pixels = samplePixels(bgrFrame, sampleSize_, rng_);
    bool fresh = static_cast<int>(centers_.size()) != K_;
    if (fresh) centers_ = kmeansPlusPlusInit(pixels, K_, rng_);

    // Warm start: a handful of iterations suffices because last frame's
    // centroids are already close to this frame's optimum.
    std::vector<int> counts = runLloyd(pixels, centers_, fresh ? 25 : 6);

    const float alpha = 0.3f;   // EMA weight of the newest frame
    if (fresh) share_.assign(K_, 0.0f);
    for (int k = 0; k < K_; ++k) {
        float now = static_cast<float>(counts[k]) / pixels.size();
        share_[k] = fresh ? now : (1.0f - alpha) * share_[k] + alpha * now;
    }
    result_ = describe(centers_, share_, palette);
    return result_;
}
