#include <iostream>
#include <iomanip>
#include <string>
#include <cstring>
#include "ColorDetector.h"
#include "ColorScience.h"
#include "DominantColors.h"

// Non-interactive JSON mode: color_cli --json R G B
static int jsonMode(int r, int g, int b) {
    ColorDetector detector;
    const std::vector<NamedColor>& palette = getBuiltinPalette();

    HSV hsv = detector.rgbToHsv(r, g, b);
    std::string hsvName = detector.classifyRGB(r, g, b);
    Lab lab = rgbToLab(r, g, b);
    const NamedColor& nearest = nearestColor(lab, palette);
    double de = deltaE2000(lab, nearest.lab);

    // Hex string
    char hex[8];
    std::snprintf(hex, sizeof(hex), "%02X%02X%02X", r, g, b);

    std::cout << std::fixed << std::setprecision(2)
              << "{"
              << "\"r\":" << r << ","
              << "\"g\":" << g << ","
              << "\"b\":" << b << ","
              << "\"hex\":\"" << hex << "\","
              << "\"hsv\":{\"h\":" << static_cast<int>(hsv.h * 2)
                        << ",\"s\":" << static_cast<int>(hsv.s / 255.0 * 100)
                        << ",\"v\":" << static_cast<int>(hsv.v / 255.0 * 100) << "},"
              << "\"lab\":{\"L\":" << lab.L << ",\"a\":" << lab.a << ",\"b\":" << lab.b << "},"
              << "\"hsv_name\":\"" << hsvName << "\","
              << "\"de_name\":\"" << nearest.name << "\","
              << "\"de_value\":" << de << ","
              << "\"de_label\":\"" << deltaELabel(de) << "\""
              << "}\n";
    return 0;
}

// Non-interactive palette mode: color_cli --palette IMAGE [K]
// K-Means++ in Lab on the image, each centroid named by ΔE2000; JSON output.
static int paletteMode(const char* path, int K) {
    cv::Mat img = cv::imread(path, cv::IMREAD_COLOR);
    if (img.empty()) {
        std::cerr << "Cannot read image: " << path << "\n";
        return 1;
    }
    auto colors = extractDominantColors(img, getBuiltinPalette(), K, 25, 4000);

    std::cout << std::fixed << std::setprecision(3) << "{\"colors\":[";
    for (size_t i = 0; i < colors.size(); ++i) {
        const auto& c = colors[i];
        char hex[8];
        std::snprintf(hex, sizeof(hex), "%02X%02X%02X", c.r, c.g, c.b);
        std::cout << (i ? "," : "") << "{"
                  << "\"r\":" << c.r << ",\"g\":" << c.g << ",\"b\":" << c.b << ","
                  << "\"hex\":\"" << hex << "\","
                  << "\"fraction\":" << c.fraction << ","
                  << "\"de_name\":\"" << c.nearestName << "\","
                  << "\"de_value\":" << c.nearestDE
                  << "}";
    }
    std::cout << "]}\n";
    return 0;
}

int main(int argc, char* argv[]) {
    // --json R G B  →  single-shot JSON output for API calls
    if (argc == 5 && std::strcmp(argv[1], "--json") == 0) {
        return jsonMode(std::atoi(argv[2]), std::atoi(argv[3]), std::atoi(argv[4]));
    }
    if ((argc == 3 || argc == 4) && std::strcmp(argv[1], "--palette") == 0) {
        int K = argc == 4 ? std::atoi(argv[3]) : 6;
        return paletteMode(argv[2], std::max(1, std::min(K, 12)));
    }
    ColorDetector detector;
    const std::vector<NamedColor>& palette = getBuiltinPalette();

    std::cout << "ColorVision++ — Perceptual Color Analysis Engine\n"
              << "Select mode: 1 = RGB input, 2 = HEX input, 0 = Exit\n\n";

    while (true) {
        std::cout << "Mode [0/1/2]: ";
        int mode;
        if (!(std::cin >> mode)) break;
        if (mode == 0) break;

        int r, g, b;
        if (mode == 1) {
            std::cout << "Enter R G B values (0-255): ";
            std::cin >> r >> g >> b;
        } else if (mode == 2) {
            std::cout << "Enter 6-digit HEX code (e.g., FF00AA): ";
            std::string hex;
            std::cin >> hex;
            try {
                std::tie(r, g, b) = detector.hexToRgb(hex);
            } catch (const std::exception& e) {
                std::cerr << "HEX parse error: " << e.what() << "\n";
                continue;
            }
        } else {
            std::cout << "Invalid mode.\n";
            continue;
        }

        // ── HSV classification (original) ─────────────────────────────────
        HSV hsv = detector.rgbToHsv(r, g, b);
        std::string hsvName = detector.classifyRGB(r, g, b);

        // ── CIE L*a*b* + ΔE2000 nearest-neighbor ─────────────────────────
        Lab lab = rgbToLab(r, g, b);
        const NamedColor& nearest = nearestColor(lab, palette);
        double de = deltaE2000(lab, nearest.lab);

        // ── Display ───────────────────────────────────────────────────────
        std::cout << "─────────────────────────────────────────────\n";
        std::cout << "  sRGB       : R=" << r << "  G=" << g << "  B=" << b << "\n";
        std::cout << "  HSV        : H=" << static_cast<int>(hsv.h * 2)
                  << "°  S=" << static_cast<int>(hsv.s / 255 * 100)
                  << "%  V=" << static_cast<int>(hsv.v / 255 * 100) << "%\n";
        std::cout << "  CIE L*a*b* : L=" << std::fixed << std::setprecision(2)
                  << lab.L << "  a=" << lab.a << "  b=" << lab.b << "\n";
        std::cout << "\n";
        std::cout << "  HSV classifier : " << hsvName << "\n";
        std::cout << "  ΔE2000 nearest : " << nearest.name
                  << "  (ΔE=" << std::fixed << std::setprecision(2) << de
                  << ", " << deltaELabel(de) << ")\n";
        std::cout << "─────────────────────────────────────────────\n\n";
    }
    return 0;
}
