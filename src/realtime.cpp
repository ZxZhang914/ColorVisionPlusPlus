// color_camera — interactive camera front end for the ColorVision++ engine.
//
//   color_camera                 webcam 0
//   color_camera --camera 1      another webcam
//   color_camera photo.jpg       explore a still image instead
#include "CameraApp.h"
#include <chrono>
#include <cstring>
#include <iostream>

int main(int argc, char* argv[]) {
    int cameraIndex = 0;
    std::string imagePath;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--camera") == 0 && i + 1 < argc) cameraIndex = std::atoi(argv[++i]);
        else imagePath = argv[i];
    }

    cv::VideoCapture cap;
    cv::Mat still;
    if (!imagePath.empty()) {
        still = cv::imread(imagePath);
        if (still.empty()) { std::cerr << "Error: cannot read image " << imagePath << "\n"; return -1; }
    } else {
        cap.open(cameraIndex);
        if (!cap.isOpened()) { std::cerr << "Error: Cannot open camera.\n"; return -1; }
        cap.set(cv::CAP_PROP_FRAME_WIDTH, 1280);
        cap.set(cv::CAP_PROP_FRAME_HEIGHT, 720);
    }

    // A webcam preview feels natural mirrored; a photo should not be.
    CameraApp app("captures", /*mirror=*/still.empty());
    if (!still.empty()) app.onFrame(still);

    const char* window = "ColorVision++";
    // GUI_NORMAL: no Qt toolbar / status bar — the app draws its own chrome
    cv::namedWindow(window, cv::WINDOW_AUTOSIZE | cv::WINDOW_GUI_NORMAL);
    cv::setMouseCallback(window, [](int event, int x, int y, int flags, void* user) {
        static_cast<CameraApp*>(user)->onMouse(event, x, y, flags);
    }, &app);

    std::cout << "ColorVision++ - press H in the window for controls, Esc to quit.\n";
    const auto start = std::chrono::steady_clock::now();
    cv::Mat frame;
    while (true) {
        if (cap.isOpened()) {
            cap >> frame;
            if (frame.empty()) { std::cerr << "Empty frame.\n"; break; }
            app.onFrame(frame);
        }
        double now = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        app.tick(now);
        cv::imshow(window, app.render(now));

        int key = cv::waitKey(still.empty() ? 1 : 15);
        if (key >= 0 && !app.onKey(key & 0xFF)) break;
        if (cv::getWindowProperty(window, cv::WND_PROP_VISIBLE) < 1) break;   // closed with [x]
    }
    return 0;
}
