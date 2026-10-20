#include "Hud.h"
#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <vector>

#if __has_include(<opencv2/freetype.hpp>)
#include <opencv2/freetype.hpp>
#define HUD_HAVE_FREETYPE 1
#endif

namespace hud {
namespace {

#ifdef HUD_HAVE_FREETYPE
cv::Ptr<cv::freetype::FreeType2> loadFont(const char* envVar, const std::vector<std::string>& candidates) {
    std::vector<std::string> paths;
    if (const char* env = std::getenv(envVar)) paths.push_back(env);
    paths.insert(paths.end(), candidates.begin(), candidates.end());
    for (const auto& p : paths) {
        if (!std::ifstream(p).good()) continue;
        try {
            auto ft = cv::freetype::createFreeType2();
            ft->loadFontData(p, 0);
            return ft;
        } catch (const cv::Exception&) {}
    }
    return nullptr;
}

struct Fonts {
    cv::Ptr<cv::freetype::FreeType2> regular, bold;
    Fonts() {
        regular = loadFont("COLORVISION_FONT", {
            "C:/Windows/Fonts/segoeui.ttf",
            "/System/Library/Fonts/Supplemental/Arial.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
        });
        bold = loadFont("COLORVISION_FONT_BOLD", {
            "C:/Windows/Fonts/seguisb.ttf",
            "C:/Windows/Fonts/segoeuib.ttf",
            "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
            "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
        });
        if (!bold) bold = regular;
    }
};

cv::freetype::FreeType2* font(bool bold) {
    static Fonts fonts;
    return bold ? fonts.bold.get() : fonts.regular.get();
}
#endif

// Hershey fonts only cover ASCII
std::string ascii(const std::string& s) {
    static const std::pair<const char*, const char*> subs[] = {
        {"\xCE\x94", "d"}, {"\xC2\xB0", ""}, {"\xC2\xB7", "|"}, {"\xE2\x80\x94", "-"},
        {"\xE2\x86\x92", "->"}, {"\xE2\x80\xA6", "..."}, {"\xC2\xB1", "+/-"},
    };
    std::string out = s;
    for (const auto& [from, to] : subs) {
        for (size_t pos; (pos = out.find(from)) != std::string::npos;)
            out.replace(pos, std::char_traits<char>::length(from), to);
    }
    return out;
}

double hersheyScale(int size) { return size / 30.0; }

} // namespace

int textWidth(const std::string& s, int size, bool bold) {
#ifdef HUD_HAVE_FREETYPE
    if (auto* ft = font(bold)) {
        int base = 0;
        return ft->getTextSize(s, size, -1, &base).width;
    }
#endif
    int base = 0;
    return cv::getTextSize(ascii(s), cv::FONT_HERSHEY_SIMPLEX, hersheyScale(size),
                           bold ? 2 : 1, &base).width;
}

void text(cv::Mat& img, const std::string& s, cv::Point baseline, int size,
          const cv::Scalar& color, bool bold) {
#ifdef HUD_HAVE_FREETYPE
    if (auto* ft = font(bold)) {
        // bottomLeftOrigin=true puts org on the baseline, matching cv::putText
        ft->putText(img, s, baseline, size, color, -1, cv::LINE_AA, true);
        return;
    }
#endif
    cv::putText(img, ascii(s), baseline, cv::FONT_HERSHEY_SIMPLEX, hersheyScale(size),
                color, bold ? 2 : 1, cv::LINE_AA);
}

void textShadow(cv::Mat& img, const std::string& s, cv::Point baseline, int size,
                const cv::Scalar& color, bool bold) {
    text(img, s, baseline + cv::Point(0, 1), size, cv::Scalar(0, 0, 0), bold);
    text(img, s, baseline + cv::Point(1, 1), size, cv::Scalar(0, 0, 0), bold);
    text(img, s, baseline, size, color, bold);
}

void textCentered(cv::Mat& img, const std::string& s, int centerX, int baselineY, int size,
                  const cv::Scalar& color, bool bold) {
    text(img, s, {centerX - textWidth(s, size, bold) / 2, baselineY}, size, color, bold);
}

std::string fit(const std::string& s, int size, int maxWidth, bool bold) {
    if (textWidth(s, size, bold) <= maxWidth) return s;
    std::string t = s;
    while (!t.empty() && textWidth(t + "\xE2\x80\xA6", size, bold) > maxWidth) t.pop_back();
    return t.empty() ? "" : t + "\xE2\x80\xA6";
}

void roundedFill(cv::Mat& img, cv::Rect r, int radius, const cv::Scalar& color) {
    radius = std::max(0, std::min(radius, std::min(r.width, r.height) / 2));
    if (radius == 0) { cv::rectangle(img, r, color, cv::FILLED); return; }
    const int x0 = r.x + radius, x1 = r.x + r.width - 1 - radius;
    const int y0 = r.y + radius, y1 = r.y + r.height - 1 - radius;
    cv::rectangle(img, cv::Rect(x0, r.y, x1 - x0 + 1, r.height), color, cv::FILLED);
    cv::rectangle(img, cv::Rect(r.x, y0, r.width, y1 - y0 + 1), color, cv::FILLED);
    for (cv::Point c : {cv::Point(x0, y0), cv::Point(x1, y0), cv::Point(x0, y1), cv::Point(x1, y1)})
        cv::circle(img, c, radius, color, cv::FILLED, cv::LINE_AA);
}

void roundedStroke(cv::Mat& img, cv::Rect r, int radius, const cv::Scalar& color, int thickness) {
    radius = std::max(0, std::min(radius, std::min(r.width, r.height) / 2));
    if (radius == 0) { cv::rectangle(img, r, color, thickness, cv::LINE_AA); return; }
    const int x0 = r.x + radius, x1 = r.x + r.width - 1 - radius;
    const int y0 = r.y + radius, y1 = r.y + r.height - 1 - radius;
    const int right = r.x + r.width - 1, bottom = r.y + r.height - 1;
    cv::line(img, {x0, r.y}, {x1, r.y}, color, thickness, cv::LINE_AA);
    cv::line(img, {x0, bottom}, {x1, bottom}, color, thickness, cv::LINE_AA);
    cv::line(img, {r.x, y0}, {r.x, y1}, color, thickness, cv::LINE_AA);
    cv::line(img, {right, y0}, {right, y1}, color, thickness, cv::LINE_AA);
    const cv::Size ax(radius, radius);
    cv::ellipse(img, {x0, y0}, ax, 180, 0, 90, color, thickness, cv::LINE_AA);
    cv::ellipse(img, {x1, y0}, ax, 270, 0, 90, color, thickness, cv::LINE_AA);
    cv::ellipse(img, {x1, y1}, ax,   0, 0, 90, color, thickness, cv::LINE_AA);
    cv::ellipse(img, {x0, y1}, ax,  90, 0, 90, color, thickness, cv::LINE_AA);
}

void pasteRounded(cv::Mat& img, const cv::Mat& src, cv::Rect r, int radius) {
    cv::Rect clip = r & cv::Rect(0, 0, img.cols, img.rows);
    if (clip.empty()) return;
    cv::Mat mask(r.size(), CV_8U, cv::Scalar(0));
    roundedFill(mask, cv::Rect(0, 0, r.width, r.height), radius, cv::Scalar(255));

    cv::Rect local(clip.x - r.x, clip.y - r.y, clip.width, clip.height);
    cv::Mat alpha, out;
    mask(local).convertTo(alpha, CV_32F, 1.0 / 255.0);
    cv::Mat inverse = 1.0 - alpha;
    cv::blendLinear(src(local), img(clip), alpha, inverse, out);
    out.copyTo(img(clip));
}

void glass(cv::Mat& img, cv::Rect r, int radius, double opacity, const cv::Scalar& tint) {
    cv::Rect clip = r & cv::Rect(0, 0, img.cols, img.rows);
    if (clip.empty()) return;
    cv::Mat frosted;
    cv::blur(img(clip), frosted, {15, 15});          // two box passes ≈ Gaussian
    cv::blur(frosted, frosted, {15, 15});
    cv::addWeighted(frosted, 1.0 - opacity, cv::Mat(frosted.size(), frosted.type(), tint), opacity,
                    0.0, frosted);

    cv::Mat card(r.size(), img.type(), tint);
    frosted.copyTo(card(cv::Rect(clip.x - r.x, clip.y - r.y, clip.width, clip.height)));
    pasteRounded(img, card, r, radius);
    roundedStroke(img, r, radius, mix(tint, cv::Scalar(255, 255, 255), 0.16), 1);
}

void panel(cv::Mat& img, cv::Rect r, double opacity, const cv::Scalar& color) {
    r &= cv::Rect(0, 0, img.cols, img.rows);
    if (r.empty()) return;
    cv::Mat roi = img(r);
    cv::Mat fill(roi.size(), roi.type(), color);
    cv::addWeighted(fill, opacity, roi, 1.0 - opacity, 0.0, roi);
}

void swatch(cv::Mat& img, cv::Rect r, const cv::Scalar& bgr, int radius) {
    roundedFill(img, r, radius, bgr);
    double luma = 0.114 * bgr[0] + 0.587 * bgr[1] + 0.299 * bgr[2];
    cv::Scalar edge = luma > 160 ? mix(bgr, cv::Scalar(0, 0, 0), 0.25)
                                 : mix(bgr, cv::Scalar(255, 255, 255), 0.35);
    roundedStroke(img, r, radius, edge, 1);
}

int keycapWidth(const std::string& key) {
    return std::max(20, textWidth(key, 11, true) + 12);
}

int keycap(cv::Mat& img, const std::string& key, cv::Point baseline) {
    int w = keycapWidth(key);
    cv::Rect r(baseline.x, baseline.y - 14, w, 19);
    roundedFill(img, r, 5, mix(kPanel, cv::Scalar(255, 255, 255), 0.14));
    roundedStroke(img, r, 5, mix(kPanel, cv::Scalar(255, 255, 255), 0.32), 1);
    textCentered(img, key, r.x + w / 2, baseline.y, 11, kText, true);
    return w;
}

void bar(cv::Mat& img, cv::Rect r, double progress, const cv::Scalar& color) {
    roundedFill(img, r, r.height / 2, mix(kPanel, cv::Scalar(255, 255, 255), 0.15));
    int w = static_cast<int>(r.width * std::clamp(progress, 0.0, 1.0));
    if (w >= r.height) roundedFill(img, cv::Rect(r.x, r.y, w, r.height), r.height / 2, color);
}

void logo(cv::Mat& img, cv::Point center, int radius) {
    for (int i = 0; i < 12; ++i) {
        cv::Mat hsv(1, 1, CV_8UC3, cv::Scalar(i * 15, 190, 255)), bgr;
        cv::cvtColor(hsv, bgr, cv::COLOR_HSV2BGR);
        cv::Vec3b c = bgr.at<cv::Vec3b>(0, 0);
        cv::ellipse(img, center, {radius, radius}, i * 30 - 90, 0, 31, cv::Scalar(c[0], c[1], c[2]),
                    4, cv::LINE_AA);
    }
}

void ring(cv::Mat& img, cv::Point center, int radius, double progress,
          const cv::Scalar& color, int thickness) {
    if (progress <= 0.0) return;
    cv::ellipse(img, center, {radius, radius}, -90.0, 0.0, 360.0 * std::min(progress, 1.0),
                color, thickness, cv::LINE_AA);
}

cv::Scalar inkOn(const cv::Scalar& bgr) {
    double luma = 0.114 * bgr[0] + 0.587 * bgr[1] + 0.299 * bgr[2];
    return luma > 150 ? cv::Scalar(20, 20, 20) : cv::Scalar(250, 250, 250);
}

cv::Scalar mix(const cv::Scalar& a, const cv::Scalar& b, double t) {
    return a * (1.0 - t) + b * t;
}

} // namespace hud
