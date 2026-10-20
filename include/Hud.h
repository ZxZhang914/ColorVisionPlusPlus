#pragma once
#include <opencv2/opencv.hpp>
#include <string>

// Small drawing toolkit for the camera overlay.
//
// Text goes through FreeType with a system UI font when OpenCV was built with
// the freetype contrib module and a font file is found (override the search
// with the COLORVISION_FONT / COLORVISION_FONT_BOLD environment variables).
// Otherwise it falls back to OpenCV's Hershey fonts, which are ASCII-only, so
// glyphs like "Δ" and "°" are substituted.
namespace hud {

// Theme (BGR)
const cv::Scalar kPanel  (30, 24, 22);
const cv::Scalar kText   (245, 245, 245);
const cv::Scalar kMuted  (170, 165, 160);
const cv::Scalar kAccent (247, 110, 124);   // same violet as the web UI
const cv::Scalar kGood   (128, 222, 74);
const cv::Scalar kWarn   (60, 190, 250);
const cv::Scalar kBad    (90, 90, 245);

// Size is the font height in pixels; position is the left end of the baseline.
void text(cv::Mat& img, const std::string& s, cv::Point baseline, int size,
          const cv::Scalar& color, bool bold = false);
// Text with a soft dark shadow, for drawing straight onto video.
void textShadow(cv::Mat& img, const std::string& s, cv::Point baseline, int size,
                const cv::Scalar& color, bool bold = false);
void textCentered(cv::Mat& img, const std::string& s, int centerX, int baselineY, int size,
                  const cv::Scalar& color, bool bold = false);
int  textWidth(const std::string& s, int size, bool bold = false);
// Shorten s with an ellipsis until it fits in maxWidth pixels.
std::string fit(const std::string& s, int size, int maxWidth, bool bold = false);

// Anti-aliased rounded rectangles.
void roundedFill(cv::Mat& img, cv::Rect r, int radius, const cv::Scalar& color);
void roundedStroke(cv::Mat& img, cv::Rect r, int radius, const cv::Scalar& color, int thickness = 1);
// Copy src (same size as r) into img at r through a rounded-corner mask;
// radius = r.width / 2 on a square gives a circle.
void pasteRounded(cv::Mat& img, const cv::Mat& src, cv::Rect r, int radius);

// Frosted-glass card: the video behind it is blurred and tinted.
void glass(cv::Mat& img, cv::Rect r, int radius = 14, double opacity = 0.62,
           const cv::Scalar& tint = kPanel);
// Flat translucent rectangle (full-screen dims and flashes).
void panel(cv::Mat& img, cv::Rect r, double opacity = 0.72, const cv::Scalar& color = kPanel);
// Rounded color chip with an edge that stays visible on any background.
void swatch(cv::Mat& img, cv::Rect r, const cv::Scalar& bgr, int radius = 8);
// Keyboard key cap with a label; returns its width.
int  keycap(cv::Mat& img, const std::string& key, cv::Point baseline);
int  keycapWidth(const std::string& key);
// Thin horizontal progress bar.
void bar(cv::Mat& img, cv::Rect r, double progress, const cv::Scalar& color);
// Small hue-wheel logo.
void logo(cv::Mat& img, cv::Point center, int radius);
// Arc that fills clockwise from 12 o'clock as progress goes 0 → 1.
void ring(cv::Mat& img, cv::Point center, int radius, double progress,
          const cv::Scalar& color, int thickness);
// Black or white, whichever reads better on top of the given color.
cv::Scalar inkOn(const cv::Scalar& bgr);
cv::Scalar mix(const cv::Scalar& a, const cv::Scalar& b, double t);

} // namespace hud
