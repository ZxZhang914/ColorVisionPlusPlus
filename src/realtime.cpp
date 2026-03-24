#include <opencv2/opencv.hpp>
#include "ColorDetector.h"
#include "ColorScience.h"
#include "DominantColors.h"
#include "ColorblindFilter.h"
#include <ctime>
#include <sstream>
#include <iomanip>
#include <iostream>

// ── Utilities ─────────────────────────────────────────────────────────────────
static cv::Mat applyMosaic(const cv::Mat& frame, int blockSize) {
    cv::Mat result = frame.clone();
    for (int y = 0; y < frame.rows; y += blockSize) {
        for (int x = 0; x < frame.cols; x += blockSize) {
            int w = std::min(blockSize, frame.cols - x);
            int h = std::min(blockSize, frame.rows - y);
            cv::Rect roi(x, y, w, h);
            cv::Scalar avg = cv::mean(frame(roi));
            result(roi).setTo(avg);
        }
    }
    return result;
}

static std::string getTimestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tm = *std::localtime(&t);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    return oss.str();
}

static void drawLabel(cv::Mat& img, const std::string& text,
                      cv::Point pos, double scale, cv::Scalar color,
                      int thickness = 2) {
    cv::putText(img, text, pos + cv::Point(1, 1),
                cv::FONT_HERSHEY_SIMPLEX, scale, cv::Scalar(0,0,0), thickness + 1);
    cv::putText(img, text, pos,
                cv::FONT_HERSHEY_SIMPLEX, scale, color, thickness);
}

// Draw the dominant-color palette strip at the bottom of the frame
static void drawPaletteStrip(cv::Mat& display,
                              const std::vector<DominantColor>& palette)
{
    if (palette.empty()) return;
    const int stripH   = 44;
    const int labelH   = 14;
    int       totalW   = display.cols;
    int       startY   = display.rows - stripH;

    // Background
    cv::rectangle(display, cv::Rect(0, startY, totalW, stripH),
                  cv::Scalar(20, 20, 20), cv::FILLED);

    int x = 0;
    for (const auto& dc : palette) {
        int w = static_cast<int>(std::round(dc.fraction * totalW));
        if (w < 4) w = 4;
        w = std::min(w, totalW - x);
        if (w <= 0) break;

        // Color swatch
        cv::Rect swatch(x, startY, w, stripH - labelH);
        display(swatch).setTo(cv::Scalar(dc.b, dc.g, dc.r));

        // ΔE label — small text below swatch
        std::ostringstream label;
        label << std::fixed << std::setprecision(1) << dc.nearestDE;
        int fontFace   = cv::FONT_HERSHEY_SIMPLEX;
        double scale   = 0.32;
        int baseline   = 0;
        cv::Size ts    = cv::getTextSize(label.str(), fontFace, scale, 1, &baseline);
        int tx = x + (w - ts.width) / 2;
        int ty = startY + (stripH - labelH) + labelH - 2;
        cv::putText(display, label.str(), cv::Point(tx, ty),
                    fontFace, scale, cv::Scalar(180,180,180), 1, cv::LINE_AA);

        x += w;
    }

    // Strip title — drawn inside the background bar at the far right to avoid overlap
    drawLabel(display, "K-Means palette",
              cv::Point(totalW - 120, startY + 12), 0.32, cv::Scalar(100,100,100), 1);
}

// ─────────────────────────────────────────────────────────────────────────────
int main() {
    cv::VideoCapture cap(0);
    if (!cap.isOpened()) {
        std::cerr << "Error: Cannot open camera.\n";
        return -1;
    }

    // Load color palette once at startup
    const std::vector<NamedColor>& palette = getBuiltinPalette();
    ColorDetector    hsvDetector;
    bool             mosaicMode = false;
    int              blockSize  = 20;
    ColorblindType   cbType     = ColorblindType::None;

    std::cout << "ColorVision++ — Perceptual Color Analysis Engine\n"
              << "  M / m  — Toggle mosaic mode\n"
              << "  + / =  — Increase mosaic block size\n"
              << "  -      — Decrease mosaic block size\n"
              << "  C / c  — Cycle colorblind simulation\n"
              << "  S / s  — Save screenshot\n"
              << "  ESC    — Exit\n\n";

    while (true) {
        cv::Mat frame;
        cap >> frame;
        if (frame.empty()) { std::cerr << "Empty frame.\n"; break; }

        // ── Center-region color detection ─────────────────────────────────
        int cx = frame.cols / 2, cy = frame.rows / 2;
        const int regionSize = 40;
        cv::Rect centerRoi(cx - regionSize/2, cy - regionSize/2,
                           regionSize, regionSize);
        cv::Scalar avgBGR = cv::mean(frame(centerRoi));
        int b = static_cast<int>(avgBGR[0]);
        int g = static_cast<int>(avgBGR[1]);
        int r = static_cast<int>(avgBGR[2]);

        // ── Color classification (both methods) ───────────────────────────
        std::string hsvName = hsvDetector.classifyRGB(r, g, b);
        Lab         lab     = rgbToLab(r, g, b);
        const NamedColor& nearest = nearestColor(lab, palette);
        std::string deName  = nearest.name;
        double      de      = deltaE2000(lab, nearest.lab);

        // ── HSV for display ───────────────────────────────────────────────
        cv::Mat px1ch(1,1,CV_8UC3, cv::Scalar(b,g,r));
        cv::Mat hsvPx; cv::cvtColor(px1ch, hsvPx, cv::COLOR_BGR2HSV);
        cv::Vec3b hsv = hsvPx.at<cv::Vec3b>(0,0);
        int H = hsv[0]*2, S = hsv[1]*100/255, V = hsv[2]*100/255;

        // ── Dominant color extraction (runs on every frame) ───────────────
        std::vector<DominantColor> dominant =
            extractDominantColors(frame, palette, /*K=*/6);

        // ── Apply visual effects ──────────────────────────────────────────
        cv::Mat base = mosaicMode ? applyMosaic(frame, blockSize) : frame.clone();
        cv::Mat display = simulateColorblindness(base, cbType);

        // Sampling region crosshair
        cv::rectangle(display, centerRoi, cv::Scalar(255,255,255), 2);
        cv::drawMarker(display, cv::Point(cx,cy),
                       cv::Scalar(255,255,255), cv::MARKER_CROSS, 10, 1);

        // ── Overlays ──────────────────────────────────────────────────────
        // Color swatch (top-left)
        cv::Rect swatchRect(16, 16, 56, 56);
        display(swatchRect).setTo(cv::Scalar(b, g, r));
        cv::rectangle(display, swatchRect, cv::Scalar(200,200,200), 1);

        // Color names
        drawLabel(display, deName,   cv::Point(82,  46), 0.9, cv::Scalar(255,255,255), 2);
        drawLabel(display, "(HSV: " + hsvName + ")",
                  cv::Point(82, 65), 0.45, cv::Scalar(160,160,160), 1);

        // Lab & ΔE readout
        std::ostringstream labStr, deStr;
        labStr << std::fixed << std::setprecision(1)
               << "Lab: (" << lab.L << ", " << lab.a << ", " << lab.b << ")";
        deStr  << std::fixed << std::setprecision(2)
               << "ΔE2000: " << de;
        drawLabel(display, labStr.str(), cv::Point(16,  96), 0.5, cv::Scalar(180,255,180), 1);
        drawLabel(display, deStr.str(),  cv::Point(16, 114), 0.5, cv::Scalar(180,255,180), 1);

        // RGB & HSV
        std::string rgbText = "RGB: (" + std::to_string(r) + ", " +
                              std::to_string(g) + ", " + std::to_string(b) + ")";
        std::string hsvText = "HSV: (H=" + std::to_string(H) +
                              "\xB0, S=" + std::to_string(S) +
                              "%, V=" + std::to_string(V) + "%)";
        drawLabel(display, rgbText, cv::Point(16, 132), 0.5, cv::Scalar(255,255,255), 1);
        drawLabel(display, hsvText, cv::Point(16, 150), 0.5, cv::Scalar(0,220,220),   1);

        // Mode line
        std::string modeStr = mosaicMode
            ? ("Mosaic [" + std::to_string(blockSize) + "px]")
            : "Live";
        if (cbType != ColorblindType::None)
            modeStr += "  |  " + colorblindTypeName(cbType);
        drawLabel(display, modeStr, cv::Point(16, 168), 0.45, cv::Scalar(200,200,100), 1);

        // Dominant palette strip
        drawPaletteStrip(display, dominant);

        // Help hint — positioned above the 44px palette strip with 10px gap
        drawLabel(display, "M=Mosaic  +/-=Size  C=ColorBlind  S=Save  ESC=Exit",
                  cv::Point(16, display.rows - 44 - 10), 0.38, cv::Scalar(140,140,140), 1);

        cv::imshow("ColorVision++ | Perceptual Color Analysis", display);

        // ── Key handling ──────────────────────────────────────────────────
        int key = cv::waitKey(1);
        if (key == 27)                 break;
        if (key == 'm' || key == 'M') mosaicMode = !mosaicMode;
        if (key == '+' || key == '=') blockSize = std::min(blockSize + 5, 80);
        if (key == '-')               blockSize = std::max(blockSize - 5,  5);
        if (key == 'c' || key == 'C') cbType = nextColorblindType(cbType);
        if (key == 's' || key == 'S') {
            std::string fn = "screenshot_" + getTimestamp() + ".png";
            cv::imwrite(fn, display);
            std::cout << "Screenshot saved: " << fn << "\n";
        }
    }
    return 0;
}
