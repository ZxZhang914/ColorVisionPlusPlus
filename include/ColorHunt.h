#pragma once
#include "ColorScience.h"
#include <opencv2/core.hpp>
#include <random>
#include <vector>

// "Color Hunt": a timed game where the player points the camera at real-world
// objects that match a target named color. The judge is CIEDE2000 — the target
// locks once the reticle sample stays within the level's ΔE threshold for
// kHoldSeconds. Pure game logic; drawing lives in CameraApp.
enum class HuntLevel { Easy, Normal, Hard };
const char* huntLevelName(HuntLevel level);
double      huntThreshold(HuntLevel level);   // ΔE00 needed to lock a target

struct HuntFind {
    const NamedColor* target;
    cv::Vec3b found;     // BGR of the object the player showed
    double    dE;        // best ΔE00 reached while locking
    int       points;
    cv::Mat   thumb;     // filled in by the caller after a capture
};

class ColorHunt {
public:
    enum class Phase { Ready, Countdown, Playing, Over };

    static constexpr double kRoundSeconds = 60.0;
    static constexpr double kCountdown    = 3.0;
    static constexpr double kHoldSeconds  = 0.6;

    explicit ColorHunt(const std::vector<NamedColor>& palette,
                       unsigned seed = std::random_device{}());

    void start(double now);
    void abort() { phase_ = Phase::Ready; }
    void skip();
    void cycleLevel();
    void setLevel(HuntLevel level) { level_ = level; }

    // Advance the clock with the current reticle sample.
    // Returns true when the target was captured by this call; the new find is
    // then finds().back() and a new target is already in place.
    bool update(double now, const Lab& sample, const cv::Vec3b& sampleBGR);

    Phase     phase() const { return phase_; }
    HuntLevel level() const { return level_; }
    const NamedColor& target() const { return *target_; }
    double currentDE()    const { return currentDE_; }
    double holdProgress() const { return hold_ / kHoldSeconds; }
    double timeLeft(double now) const;
    double countdownLeft(double now) const;
    int    score() const { return score_; }
    int    skips() const { return skips_; }
    const std::vector<HuntFind>& finds() const { return finds_; }
    std::vector<HuntFind>& finds() { return finds_; }

private:
    void nextTarget(const Lab* avoid);

    std::vector<const NamedColor*> pool_;   // targets a webcam can plausibly reach
    std::vector<const NamedColor*> bag_;    // shuffled, drawn without repeats
    std::mt19937 rng_;

    Phase     phase_ = Phase::Ready;
    HuntLevel level_ = HuntLevel::Normal;
    const NamedColor* target_ = nullptr;
    double phaseStart_ = 0, targetStart_ = 0, lastUpdate_ = 0;
    double currentDE_ = 100, hold_ = 0, bestDE_ = 100;
    Lab    lastSample_{50, 0, 0};
    int    score_ = 0, skips_ = 0;
    std::vector<HuntFind> finds_;
};
