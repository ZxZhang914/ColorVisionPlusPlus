#include "ColorHunt.h"
#include <algorithm>
#include <cmath>
#include <set>

const char* huntLevelName(HuntLevel level) {
    switch (level) {
        case HuntLevel::Easy: return "Easy";
        case HuntLevel::Hard: return "Hard";
        default:              return "Normal";
    }
}

double huntThreshold(HuntLevel level) {
    switch (level) {
        case HuntLevel::Easy: return 15.0;
        case HuntLevel::Hard: return 6.0;
        default:              return 10.0;
    }
}

ColorHunt::ColorHunt(const std::vector<NamedColor>& palette, unsigned seed) : rng_(seed) {
    // Webcams under room light rarely reproduce neon colors (C* > 75), and
    // near-black targets are trivial (cover the lens), so both are left out.
    std::set<std::string> seen;
    for (const auto& nc : palette) {
        double chroma = std::hypot(nc.lab.a, nc.lab.b);
        if (chroma <= 75.0 && nc.lab.L >= 15.0 && seen.insert(nc.name).second)
            pool_.push_back(&nc);
    }
    target_ = pool_.front();
}

void ColorHunt::start(double now) {
    phase_ = Phase::Countdown;
    phaseStart_ = lastUpdate_ = now;
    score_ = skips_ = 0;
    finds_.clear();
    bag_.clear();
    nextTarget(nullptr);
}

void ColorHunt::skip() {
    if (phase_ != Phase::Playing) return;
    ++skips_;
    nextTarget(&lastSample_);
    targetStart_ = lastUpdate_;
}

void ColorHunt::cycleLevel() {
    level_ = level_ == HuntLevel::Easy   ? HuntLevel::Normal
           : level_ == HuntLevel::Normal ? HuntLevel::Hard
                                         : HuntLevel::Easy;
}

double ColorHunt::timeLeft(double now) const {
    if (phase_ == Phase::Playing) return std::max(0.0, kRoundSeconds - (now - phaseStart_));
    return phase_ == Phase::Over ? 0.0 : kRoundSeconds;
}

double ColorHunt::countdownLeft(double now) const {
    return phase_ == Phase::Countdown ? std::max(0.0, kCountdown - (now - phaseStart_)) : 0.0;
}

void ColorHunt::nextTarget(const Lab* avoid) {
    // Don't hand out a target the camera is already looking at — that would be
    // a free capture.
    double margin = 2.0 * huntThreshold(level_);
    for (int attempt = 0; attempt < 8; ++attempt) {
        if (bag_.empty()) {
            bag_ = pool_;
            std::shuffle(bag_.begin(), bag_.end(), rng_);
        }
        const NamedColor* candidate = bag_.back();
        bag_.pop_back();
        target_ = candidate;
        if (!avoid || deltaE2000(*avoid, candidate->lab) > margin) break;
    }
    hold_ = 0;
    bestDE_ = 100;
}

bool ColorHunt::update(double now, const Lab& sample, const cv::Vec3b& sampleBGR) {
    double dt = std::max(0.0, now - lastUpdate_);
    lastUpdate_ = now;
    lastSample_ = sample;
    currentDE_  = deltaE2000(sample, target_->lab);

    if (phase_ == Phase::Countdown && now - phaseStart_ >= kCountdown) {
        phase_ = Phase::Playing;
        phaseStart_ = targetStart_ = now;
        nextTarget(&sample);
        currentDE_ = deltaE2000(sample, target_->lab);
        return false;
    }
    if (phase_ != Phase::Playing) return false;
    if (now - phaseStart_ >= kRoundSeconds) {
        phase_ = Phase::Over;
        return false;
    }

    // Lock-on: time inside the threshold accumulates; drifting out drains it
    // twice as fast, so a shaky hand gets a little forgiveness.
    double threshold = huntThreshold(level_);
    if (currentDE_ < threshold) {
        hold_ += dt;
        bestDE_ = std::min(bestDE_, currentDE_);
    } else {
        hold_ = std::max(0.0, hold_ - 2.0 * dt);
        if (hold_ == 0.0) bestDE_ = 100;
    }
    if (hold_ < kHoldSeconds) return false;

    // Score: 50–100 for precision, plus up to 25 for finding it within 3 s
    // (the bonus fades out by 15 s).
    double precision = 1.0 - std::min(bestDE_, threshold) / threshold;
    double elapsed   = now - targetStart_;
    double speed     = std::clamp((15.0 - elapsed) / 12.0, 0.0, 1.0);
    int points = static_cast<int>(std::lround(50.0 + 50.0 * precision + 25.0 * speed));

    finds_.push_back({target_, sampleBGR, bestDE_, points, cv::Mat()});
    score_ += points;
    nextTarget(&sample);
    targetStart_ = now;
    return true;
}
