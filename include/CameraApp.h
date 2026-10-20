#pragma once
#include "ColorDetector.h"
#include "ColorHunt.h"
#include "ColorScience.h"
#include "ColorblindFilter.h"
#include "DominantColors.h"
#include "Progress.h"
#include <opencv2/opencv.hpp>
#include <random>
#include <string>
#include <vector>

// The interactive camera application, independent of where frames, input and
// time come from (realtime.cpp wires it to a webcam and a HighGUI window).
//
// Modes
//   Explore  color dropper: name, ΔE00, Lab/HSV readout, live dominant palette
//   Hunt     timed game: show the camera objects matching a target color
//   Splash   keep one color (within a ΔE00 tolerance), everything else grey
//   Album    collection of every named color the player has found
// View filters (any mode): pixel art snapped to named colors, colorblind
// simulation with optional split screen.
enum class AppMode { Explore, Hunt, Splash, Album };

class CameraApp {
public:
    static constexpr int kCanvasWidth = 960;   // frames are scaled to this width

    CameraApp(std::string captureDir, bool mirror, unsigned seed = std::random_device{}());

    void onFrame(const cv::Mat& bgr);              // ignored while frozen
    void onMouse(int event, int x, int y, int flags);
    bool onKey(int key);                           // false = quit
    void tick(double now);                         // per-frame logic
    cv::Mat render(double now);                    // frame + overlay

    AppMode mode() const { return mode_; }
    const ColorHunt& hunt() const { return hunt_; }

private:
    struct Reading {
        cv::Vec3b bgr;
        Lab lab;
        std::vector<ColorMatch> matches;     // closest first
        std::string hsvName;                 // the original HSV rule classifier
        int h = 0, s = 0, v = 0;             // degrees, percent, percent
        bool simulated = false;              // colorblind view of the same sample
        cv::Vec3b simBGR;
        const NamedColor* simName = nullptr;
    };
    struct Floater { std::string text; double born; };

    // ── logic ──
    void setMode(AppMode m);
    void primaryAction();
    void turnKnob(int dir);
    void showToast(const std::string& msg);
    cv::Point center() const { return {frame_.cols / 2, frame_.rows / 2}; }
    cv::Point samplePoint() const { return pin_.x >= 0 ? pin_ : center(); }
    cv::Vec3b meanAt(cv::Point p, int radius) const;
    Reading analyze(const cv::Vec3b& bgr);
    void onHuntCapture(double now);
    void finishHunt();
    void setSplashKey(const cv::Vec3b& bgr);
    std::string saveImage(const cv::Mat& img, const std::string& name);
    void saveColorCard();

    // ── rendering ──
    cv::Mat composeView();
    cv::Mat pixelArt(const cv::Mat& src);
    cv::Mat splash(const cv::Mat& src) const;
    std::vector<cv::Rect> tabRects() const;
    std::vector<std::pair<std::string, std::string>> hints() const;   // key → action
    void drawLoupe(cv::Mat& img, cv::Point p, const cv::Scalar& ringColor) const;
    void drawTopBar(cv::Mat& img) const;
    void drawHintBar(cv::Mat& img) const;
    void drawToast(cv::Mat& img, double now) const;
    void drawHelp(cv::Mat& img) const;
    void drawSplit(cv::Mat& img) const;
    void drawExplore(cv::Mat& img) const;
    void drawPaletteStrip(cv::Mat& img) const;
    void drawHunt(cv::Mat& img, double now) const;
    void drawHuntSummary(cv::Mat& img, cv::Rect area) const;
    void drawSplash(cv::Mat& img) const;
    void drawAlbum(cv::Mat& img) const;

    // ── shared state ──
    const std::vector<NamedColor>& palette_;
    NamedColorCache names_;
    ColorDetector hsv_;
    DominantColorTracker tracker_;
    ColorHunt hunt_;
    std::string captureDir_;
    Progress progress_;
    std::vector<const NamedColor*> album_;   // unique names, sorted by hue

    cv::Mat frame_, cleanView_;
    int  frameId_ = 0, trackedFrameId_ = -1;
    bool mirror_, frozen_ = false, help_ = false;
    AppMode mode_ = AppMode::Explore;
    ColorblindType cb_ = ColorblindType::None;
    bool split_ = false, pixel_ = false;
    int  block_ = 16;
    cv::Point mouse_{-1, -1};
    double lastNow_ = 0, lastRender_ = 0, fps_ = 0;
    std::string toast_;
    double toastUntil_ = 0;

    // Explore
    cv::Point pin_{-1, -1};                  // (-1,-1) = follow the centre
    cv::Point lastSamplePoint_{-1, -1};
    cv::Vec3d smooth_;
    Reading reading_;

    // Hunt
    ColorHunt::Phase huntPhase_ = ColorHunt::Phase::Ready;
    cv::Vec3b huntSample_;
    double flashAt_ = -10;
    std::vector<Floater> floaters_;
    bool huntNewBest_ = false;
    std::string huntCardPath_;

    // Splash
    bool hasKey_ = false, invert_ = false;
    cv::Vec3b keyBGR_;
    const NamedColor* keyName_ = nullptr;
    double tolerance_ = 18;
    std::vector<uint8_t> keyDistance_;       // ΔE00 × 4 to the key, per 6-bit RGB cell
    cv::Point pickAt_{-1, -1};
};
