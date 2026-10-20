#pragma once
#include "ColorScience.h"
#include <opencv2/opencv.hpp>
#include <random>
#include <vector>

// One dominant color cluster returned by extractDominantColors()
struct DominantColor {
    Lab  lab;         // cluster centroid in Lab space
    int  r, g, b;     // approximate sRGB values (for display)
    float fraction;   // proportion of sampled pixels in this cluster
    std::string nearestName;  // nearest named color (by ΔE2000)
    double nearestDE;         // ΔE2000 distance to that named color
};

// Extract K dominant colors from a BGR frame using K-Means++ in CIE L*a*b* space.
//
// Design notes:
//   - Clustering uses Euclidean distance in Lab space (perceptually uniform),
//     which is substantially better than Euclidean in sRGB space.
//   - ΔE2000 is applied after clustering to name each centroid — this is the
//     computationally expensive part and is run only K times per frame.
//   - A random pixel sample (sampleSize) is used for real-time performance.
std::vector<DominantColor> extractDominantColors(
    const cv::Mat& bgrFrame,
    const std::vector<NamedColor>& palette,
    int K          = 5,
    int maxIter    = 25,
    int sampleSize = 1500
);

// Temporally coherent dominant colors for video.
//
// Re-running K-Means++ from scratch on every frame makes the palette flicker:
// a new random seeding finds slightly different clusters and the strip reorders.
// The tracker instead warm-starts Lloyd iterations from the previous frame's
// centroids, so each cluster keeps its identity, typically converges in 1–3
// iterations, and its share of the frame is smoothed with an exponential moving
// average.
class DominantColorTracker {
public:
    explicit DominantColorTracker(int K = 6, int sampleSize = 1500);

    // Update with a new frame; returns the palette, most dominant first.
    const std::vector<DominantColor>& update(const cv::Mat& bgrFrame,
                                             const std::vector<NamedColor>& palette);
    const std::vector<DominantColor>& colors() const { return result_; }
    void reset() { centers_.clear(); }

private:
    int K_, sampleSize_;
    std::mt19937 rng_;
    std::vector<Lab>   centers_;
    std::vector<float> share_;
    std::vector<DominantColor> result_;
};
