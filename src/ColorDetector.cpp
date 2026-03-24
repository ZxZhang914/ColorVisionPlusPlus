#include "ColorDetector.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace {
    double min3(double a, double b, double c) {
        return std::min(a, std::min(b, c));
    }

    double max3(double a, double b, double c) {
        return std::max(a, std::max(b, c));
    }

    bool inRange(double x, double l, double r) {
        return x >= l && x <= r;
    }

    int hexDigitToInt(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        c = std::toupper(static_cast<unsigned char>(c));
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        throw std::runtime_error("Invalid hex digit");
    }
}

HSV ColorDetector::rgbToHsv(int r, int g, int b) {
    double r0 = r / 255.0;
    double g0 = g / 255.0;
    double b0 = b / 255.0;

    double C_max = max3(r0, g0, b0);
    double C_min = min3(r0, g0, b0);
    double dlt   = C_max - C_min;

    double hue = 0.0;
    if (dlt == 0) {
        hue = 0;
    }
    else {
        if (C_max == r0) hue = 60 * ((g0 - b0) / dlt + 0);
        else if (C_max == g0) hue = 60 * ((b0 - r0) / dlt + 2);
        else if (C_max == b0) hue = 60 * ((r0 - g0) / dlt + 4);
    }
    if (hue < 0) hue += 360;

    // convert to OpenCV style (0-180)
    hue /= 2.0;

    double sat = 0.0;
    if (C_max == 0) sat = 0;
    else sat = dlt / C_max * 255.0;

    double val = C_max * 255.0;

    HSV hsv{hue, sat, val};
    return hsv;
}

std::tuple<int,int,int> ColorDetector::hexToRgb(const std::string& hex) {
    if (hex.size() != 6) {
        throw std::runtime_error("Hex color must be 6 characters");
    }
    int values[6];
    for (size_t i = 0; i < 6; ++i) {
        values[i] = hexDigitToInt(hex[i]);
    }
    int r = values[0] * 16 + values[1];
    int g = values[2] * 16 + values[3];
    int b = values[4] * 16 + values[5];
    return {r, g, b};
}

// RGB -> HSV -> Color Name
std::string ColorDetector::classifyRGB(int r, int g, int b) {
    HSV hsv = rgbToHsv(r, g, b);
    double hue = hsv.h;
    double sat = hsv.s;
    double val = hsv.v;

    if (inRange(hue, 0, 180) && inRange(sat, 0, 255) && inRange(val, 0, 46))
        return "BLACK";
    if (inRange(hue, 0, 180) && inRange(sat, 0, 43) && inRange(val, 46, 220))
        return "GREY";
    if (inRange(hue, 0, 180) && inRange(sat, 0, 30) && inRange(val, 221, 255))
        return "WHITE";
    if (inRange(hue, 0, 10) && inRange(sat, 43, 255) && inRange(val, 46, 255))
        return "RED";
    if (inRange(hue, 156, 180) && inRange(sat, 43, 255) && inRange(val, 46, 255))
        return "RED";
    if (inRange(hue, 11, 25) && inRange(sat, 43, 255) && inRange(val, 46, 255))
        return "ORANGE";
    if (inRange(hue, 25, 34) && inRange(sat, 43, 255) && inRange(val, 46, 255))
        return "YELLOW";
    if (inRange(hue, 35, 77) && inRange(sat, 43, 255) && inRange(val, 46, 255))
        return "GREEN";
    if (inRange(hue, 78, 99) && inRange(sat, 43, 255) && inRange(val, 46, 255))
        return "CYAN";
    if (inRange(hue, 100, 124) && inRange(sat, 43, 255) && inRange(val, 46, 255))
        return "BLUE";
    if (inRange(hue, 125, 155) && inRange(sat, 43, 255) && inRange(val, 46, 255))
        return "PURPLE";

    return "UNKNOWN";
}
/*
cd build
cmake ..
make -j4
./color_cli
*/