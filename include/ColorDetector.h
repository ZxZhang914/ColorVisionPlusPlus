#pragma once

#include <string>
#include <tuple>

struct HSV {
    double h; // 0-180 (OpenCV style)
    double s; // 0-255
    double v; // 0-255
};

class ColorDetector {
public:
    // 主接口：给 RGB，返回颜色名称
    std::string classifyRGB(int r, int g, int b);

    // 工具函数：RGB -> HSV
    HSV rgbToHsv(int r, int g, int b);

    // 工具函数：HEX(6位) -> RGB
    std::tuple<int,int,int> hexToRgb(const std::string& hex);
};
