#include "CameraApp.h"
#include "Hud.h"
#include <algorithm>
#include <cmath>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <set>
#include <sstream>

namespace {

// Screen layout: floating glass bars at the top and bottom edges
constexpr int kMargin  = 10;
constexpr int kTopBar  = 52;    // reserved height at the top (bar + gap)
constexpr int kHintBar = 46;    // reserved height at the bottom
constexpr int kStripH  = 40;
constexpr const char* kModeNames[] = {"Explore", "Hunt", "Splash", "Album"};

// Explore collects a color only when the sample is reasonably close to it
constexpr double kCollectDE = 10.0;

const cv::Scalar kWhite(255, 255, 255);
const cv::Scalar kDivider = hud::mix(hud::kPanel, kWhite, 0.14);

std::string fmt(double v, int precision) {
    std::ostringstream o;
    o << std::fixed << std::setprecision(precision) << v;
    return o.str();
}

std::string timestamp(const char* pattern) {
    std::time_t t = std::time(nullptr);
    std::tm tm = *std::localtime(&t);
    std::ostringstream o;
    o << std::put_time(&tm, pattern);
    return o.str();
}

std::string slug(std::string s) {
    std::replace(s.begin(), s.end(), ' ', '-');
    return s;
}

std::string upper(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string hexOf(const cv::Vec3b& bgr) {
    char buf[8];
    std::snprintf(buf, sizeof buf, "#%02X%02X%02X", bgr[2], bgr[1], bgr[0]);
    return buf;
}

cv::Scalar sc(const cv::Vec3b& bgr) { return {double(bgr[0]), double(bgr[1]), double(bgr[2])}; }
cv::Scalar sc(const NamedColor& nc) { return {double(nc.b), double(nc.g), double(nc.r)}; }

const char* viewerName(ColorblindType t) {
    switch (t) {
        case ColorblindType::Protanopia:   return "Protanope";
        case ColorblindType::Deuteranopia: return "Deuteranope";
        case ColorblindType::Tritanopia:   return "Tritanope";
        default:                           return "Viewer";
    }
}

const char* chipName(ColorblindType t) {
    switch (t) {
        case ColorblindType::Protanopia:   return "PROTANOPIA";
        case ColorblindType::Deuteranopia: return "DEUTERANOPIA";
        case ColorblindType::Tritanopia:   return "TRITANOPIA";
        default:                           return "";
    }
}

// How well a ΔE00 distance matches, as a 0–1 bar and a color
double matchQuality(double dE) { return std::clamp(1.0 - dE / 20.0, 0.0, 1.0); }
cv::Scalar matchColor(double dE) {
    return dE < 5 ? hud::kGood : dE < 10 ? hud::kWarn : hud::kBad;
}

// Hot/cold feedback for Color Hunt
struct Temperature { const char* word; cv::Scalar color; };
Temperature temperature(double dE, double threshold) {
    if (dE < threshold)        return {"LOCKING ON", hud::kGood};
    if (dE < threshold * 1.6)  return {"HOT",  cv::Scalar(70, 70, 255)};
    if (dE < 25)               return {"WARM", cv::Scalar(60, 165, 255)};
    if (dE < 40)               return {"COOL", cv::Scalar(230, 200, 120)};
    return                            {"COLD", cv::Scalar(255, 150, 80)};
}

void drawReticle(cv::Mat& img, cv::Point p, int r, const cv::Scalar& color) {
    cv::circle(img, p, r, cv::Scalar(0, 0, 0), 4, cv::LINE_AA);
    cv::circle(img, p, r, color, 2, cv::LINE_AA);
}

// Small uppercase caption above a value
void label(cv::Mat& img, const std::string& s, cv::Point baseline) {
    hud::text(img, s, baseline, 11, hud::kMuted, true);
}

double hueKey(const NamedColor* c) {
    // Neutrals first (light → dark), then hue starting at red
    double chroma = std::hypot(c->lab.a, c->lab.b);
    if (chroma < 8.0) return -200.0 + (100.0 - c->lab.L);
    double h = std::atan2(c->lab.b, c->lab.a) * 180.0 / M_PI;
    return std::fmod(h - 20.0 + 720.0, 360.0);
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────
//  Setup & input
// ─────────────────────────────────────────────────────────────────────────────
CameraApp::CameraApp(std::string captureDir, bool mirror, unsigned seed)
    : palette_(getBuiltinPalette()), names_(palette_), tracker_(6, 1500),
      hunt_(palette_, seed), captureDir_(std::move(captureDir)),
      progress_((std::filesystem::path(captureDir_) / "progress.txt").string()),
      mirror_(mirror)
{
    std::set<std::string> seen;
    for (const auto& nc : palette_)
        if (seen.insert(nc.name).second) album_.push_back(&nc);
    std::sort(album_.begin(), album_.end(),
              [](const NamedColor* a, const NamedColor* b) { return hueKey(a) < hueKey(b); });
}

void CameraApp::onFrame(const cv::Mat& bgr) {
    if (bgr.empty() || (frozen_ && !frame_.empty())) return;
    double s = static_cast<double>(kCanvasWidth) / bgr.cols;
    cv::Size size(kCanvasWidth, static_cast<int>(std::lround(bgr.rows * s)));
    cv::resize(bgr, frame_, size, 0, 0, s < 1.0 ? cv::INTER_AREA : cv::INTER_LINEAR);
    if (mirror_) cv::flip(frame_, frame_, 1);
    ++frameId_;
}

void CameraApp::setMode(AppMode m) {
    if (mode_ == AppMode::Hunt && m != AppMode::Hunt) hunt_.abort();
    huntPhase_ = hunt_.phase();
    mode_ = m;
}

void CameraApp::showToast(const std::string& msg) {
    toast_ = msg;
    toastUntil_ = lastNow_ + 2.8;
}

void CameraApp::onMouse(int event, int x, int y, int flags) {
    mouse_ = {x, y};
    if (frame_.empty()) return;

    if (event == cv::EVENT_LBUTTONDOWN && y < kTopBar) {
        auto tabs = tabRects();
        for (size_t i = 0; i < tabs.size(); ++i)
            if (tabs[i].contains(mouse_)) setMode(static_cast<AppMode>(i));
        return;
    }

    cv::Point p(std::clamp(x, 0, frame_.cols - 1), std::clamp(y, 0, frame_.rows - 1));
    bool press = event == cv::EVENT_LBUTTONDOWN;
    bool drag  = event == cv::EVENT_MOUSEMOVE && (flags & cv::EVENT_FLAG_LBUTTON);
    if (press || drag) {
        if (mode_ == AppMode::Explore) pin_ = p;
        if (mode_ == AppMode::Splash)  pickAt_ = p;
    }
    if (event == cv::EVENT_RBUTTONDOWN) {
        if (mode_ == AppMode::Explore) pin_ = {-1, -1};
        if (mode_ == AppMode::Splash)  hasKey_ = false;
    }
    if (event == cv::EVENT_MOUSEWHEEL)
        turnKnob(cv::getMouseWheelDelta(flags) > 0 ? 1 : -1);
}

bool CameraApp::onKey(int key) {
    if (key >= 'A' && key <= 'Z') key += 'a' - 'A';
    bool huntIdle = hunt_.phase() == ColorHunt::Phase::Ready || hunt_.phase() == ColorHunt::Phase::Over;

    switch (key) {
        case 27: case 'q': return false;
        case '1': case '2': case '3': case '4': setMode(static_cast<AppMode>(key - '1')); break;
        case '\t': setMode(static_cast<AppMode>((static_cast<int>(mode_) + 1) % 4)); break;
        case 'h': help_ = !help_; break;
        case ' ': primaryAction(); break;
        case '+': case '=': turnKnob(+1); break;
        case '-': case '_': turnKnob(-1); break;
        case 'c':
            cb_ = nextColorblindType(cb_);
            if (cb_ == ColorblindType::None) split_ = false;
            showToast(colorblindTypeName(cb_) + (cb_ != ColorblindType::None ? "  -  V for split screen" : ""));
            break;
        case 'v':
            if (cb_ == ColorblindType::None) showToast("Turn on a colorblind simulation first (C)");
            else split_ = !split_;
            break;
        case 'p':
            pixel_ = !pixel_;
            showToast(pixel_ ? "Pixel art: every block snaps to its nearest named color  (+/- size)"
                             : "Pixel art off");
            break;
        case 'f':
            frozen_ = !frozen_;
            showToast(frozen_ ? "Frame frozen - F to go live" : "Live");
            break;
        case 'm':
            mirror_ = !mirror_;
            if (!frame_.empty()) cv::flip(frame_, frame_, 1);
            ++frameId_;
            break;
        case 'n':
            if (mode_ == AppMode::Hunt) hunt_.skip();
            break;
        case 'd':
            if (mode_ == AppMode::Hunt && huntIdle) {
                hunt_.cycleLevel();
                showToast(std::string("Level: ") + huntLevelName(hunt_.level()));
            }
            break;
        case 'i':
            if (mode_ == AppMode::Splash) invert_ = !invert_;
            break;
        case 'r':
            if (mode_ == AppMode::Explore) pin_ = {-1, -1};
            if (mode_ == AppMode::Splash)  hasKey_ = false;
            break;
        default: break;
    }
    return true;
}

void CameraApp::turnKnob(int dir) {
    if (mode_ == AppMode::Splash) {
        tolerance_ = std::clamp(tolerance_ + 2.0 * dir, 4.0, 60.0);
        showToast("Keep colors within \xCE\x94" "E00 " + fmt(tolerance_, 0));
    } else {
        block_ = std::clamp(block_ + 4 * dir, 6, 48);
        showToast(pixel_ ? "Pixel size " + std::to_string(block_) + " px"
                         : "Pixel art is off - press P");
    }
}

void CameraApp::primaryAction() {
    switch (mode_) {
        case AppMode::Explore:
            saveColorCard();
            break;
        case AppMode::Hunt:
            if (hunt_.phase() == ColorHunt::Phase::Ready || hunt_.phase() == ColorHunt::Phase::Over) {
                hunt_.start(lastNow_);
                huntPhase_ = hunt_.phase();
                huntCardPath_.clear();
                floaters_.clear();
            }
            break;
        case AppMode::Splash:
            if (!hasKey_) { showToast("Click a color first"); break; }
            {
                std::string path = saveImage(cleanView_, "splash_" + timestamp("%Y%m%d_%H%M%S") + ".png");
                showToast(path.empty() ? "Could not save the photo" : "Saved " + path);
            }
            break;
        case AppMode::Album:
            break;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  Per-frame logic
// ─────────────────────────────────────────────────────────────────────────────
cv::Vec3b CameraApp::meanAt(cv::Point p, int radius) const {
    cv::Rect r(p.x - radius, p.y - radius, 2 * radius + 1, 2 * radius + 1);
    r &= cv::Rect(0, 0, frame_.cols, frame_.rows);
    cv::Scalar m = cv::mean(frame_(r));
    return cv::Vec3b(cv::saturate_cast<uchar>(m[0]), cv::saturate_cast<uchar>(m[1]),
                     cv::saturate_cast<uchar>(m[2]));
}

CameraApp::Reading CameraApp::analyze(const cv::Vec3b& bgr) {
    Reading r;
    r.bgr = bgr;
    r.lab = rgbToLab(bgr[2], bgr[1], bgr[0]);
    r.matches = nearestColors(r.lab, palette_, 3);
    r.hsvName = hsv_.classifyRGB(bgr[2], bgr[1], bgr[0]);
    HSV hsv = hsv_.rgbToHsv(bgr[2], bgr[1], bgr[0]);
    r.h = static_cast<int>(hsv.h * 2);
    r.s = static_cast<int>(hsv.s / 255.0 * 100);
    r.v = static_cast<int>(hsv.v / 255.0 * 100);
    if (cb_ != ColorblindType::None) {
        r.simulated = true;
        r.simBGR  = simulateColorblindness(bgr, cb_);
        r.simName = &nearestColor(rgbToLab(r.simBGR[2], r.simBGR[1], r.simBGR[0]), palette_);
    }
    return r;
}

void CameraApp::tick(double now) {
    lastNow_ = now;
    if (frame_.empty()) return;

    switch (mode_) {
        case AppMode::Explore: {
            // Light exponential smoothing steadies the readout against sensor
            // noise; jumping to a new spot resets it.
            cv::Point p = samplePoint();
            cv::Vec3d s = meanAt(p, 7);
            if (cv::norm(p - lastSamplePoint_) > 3) smooth_ = s;
            else                                     smooth_ = smooth_ * 0.65 + s * 0.35;
            lastSamplePoint_ = p;
            reading_ = analyze(cv::Vec3b(cv::saturate_cast<uchar>(smooth_[0]),
                                         cv::saturate_cast<uchar>(smooth_[1]),
                                         cv::saturate_cast<uchar>(smooth_[2])));
            if (trackedFrameId_ != frameId_) {
                tracker_.update(frame_, palette_);
                trackedFrameId_ = frameId_;
            }
            break;
        }
        case AppMode::Hunt: {
            huntSample_ = meanAt(center(), 12);
            Lab lab = rgbToLab(huntSample_[2], huntSample_[1], huntSample_[0]);
            if (hunt_.update(now, lab, huntSample_)) onHuntCapture(now);
            if (huntPhase_ == ColorHunt::Phase::Playing && hunt_.phase() == ColorHunt::Phase::Over)
                finishHunt();
            huntPhase_ = hunt_.phase();
            break;
        }
        case AppMode::Splash:
            if (pickAt_.x >= 0) {
                setSplashKey(meanAt(pickAt_, 5));
                pickAt_ = {-1, -1};
            }
            break;
        case AppMode::Album:
            break;
    }
}

void CameraApp::onHuntCapture(double now) {
    HuntFind& f = hunt_.finds().back();
    cv::Rect r(center().x - 60, center().y - 60, 120, 120);
    f.thumb = frame_(r & cv::Rect(0, 0, frame_.cols, frame_.rows)).clone();
    flashAt_ = now;
    floaters_.push_back({"+" + std::to_string(f.points), now});

    if (progress_.discover(f.target->name))
        showToast("NEW in your album: " + f.target->name + "  (" + std::to_string(progress_.count()) +
                  "/" + std::to_string(album_.size()) + ")");
    else
        showToast("Found " + f.target->name + "!");
}

void CameraApp::finishHunt() {
    huntNewBest_ = progress_.submit(huntLevelName(hunt_.level()), hunt_.score());
    cv::Mat card(440, 780, CV_8UC3, hud::kPanel);
    drawHuntSummary(card, cv::Rect(0, 0, card.cols, card.rows - 30));
    hud::logo(card, {28, card.rows - 22}, 7);
    hud::text(card, "ColorVision++  \xC2\xB7  Color Hunt  \xC2\xB7  " + timestamp("%Y-%m-%d %H:%M"),
              {44, card.rows - 17}, 12, hud::kMuted);
    huntCardPath_ = saveImage(card, "hunt_" + timestamp("%Y%m%d_%H%M%S") + ".png");
}

void CameraApp::setSplashKey(const cv::Vec3b& bgr) {
    hasKey_  = true;
    keyBGR_  = bgr;
    Lab key  = rgbToLab(bgr[2], bgr[1], bgr[0]);
    keyName_ = &nearestColor(key, palette_);

    // Tabulate ΔE00 to the key over a 64³ RGB grid once per pick, so the
    // per-pixel mask is a table read instead of a full CIEDE2000 evaluation.
    keyDistance_.resize(64 * 64 * 64);
    cv::parallel_for_(cv::Range(0, 64), [&](const cv::Range& range) {
        for (int r = range.start; r < range.end; ++r)
            for (int g = 0; g < 64; ++g)
                for (int b = 0; b < 64; ++b) {
                    double d = deltaE2000(rgbToLab(r * 4 + 2, g * 4 + 2, b * 4 + 2), key);
                    keyDistance_[(r << 12) | (g << 6) | b] =
                        static_cast<uint8_t>(std::min(255.0, std::round(d * 4.0)));
                }
    });
}

std::string CameraApp::saveImage(const cv::Mat& img, const std::string& name) {
    std::error_code ec;
    std::filesystem::create_directories(captureDir_, ec);
    std::string path = (std::filesystem::path(captureDir_) / name).generic_string();
    return cv::imwrite(path, img) ? path : "";
}

void CameraApp::saveColorCard() {
    if (cleanView_.empty() || reading_.matches.empty()) return;
    const Reading& r = reading_;
    const NamedColor& best = *r.matches[0].color;
    const double dE = r.matches[0].dE;

    // Left: the view at capture time; right: the color's details
    const int pad = 28, thumbH = 360, colW = 300;
    double s = static_cast<double>(thumbH) / cleanView_.rows;
    cv::Mat thumb;
    cv::resize(cleanView_, thumb, {}, s, s, cv::INTER_AREA);
    cv::Point p = samplePoint();
    drawReticle(thumb, cv::Point(int(p.x * s), int(p.y * s)), 10, kWhite);
    cv::Mat card(thumbH + 2 * pad + 30, thumb.cols + 3 * pad + colW, CV_8UC3, hud::kPanel);
    hud::pasteRounded(card, thumb, cv::Rect(pad, pad, thumb.cols, thumb.rows), 14);

    int x = thumb.cols + 2 * pad, y = pad;
    hud::swatch(card, {x, y, colW, 120}, sc(r.bgr), 14);
    hud::text(card, hud::fit(best.name, 30, colW, true), {x, y + 162}, 30, hud::kText, true);
    hud::text(card, "\xCE\x94" "E00 " + fmt(dE, 2) + "  \xC2\xB7  " + deltaELabel(dE), {x, y + 186}, 13, hud::kMuted);
    hud::bar(card, {x, y + 196, colW, 4}, matchQuality(dE), matchColor(dE));

    label(card, "HEX", {x, y + 226});
    hud::text(card, hexOf(r.bgr), {x, y + 246}, 16, hud::kText);
    label(card, "RGB", {x + 150, y + 226});
    hud::text(card, std::to_string(r.bgr[2]) + ", " + std::to_string(r.bgr[1]) + ", " +
              std::to_string(r.bgr[0]), {x + 150, y + 246}, 16, hud::kText);
    label(card, "CIE LAB", {x, y + 274});
    hud::text(card, fmt(r.lab.L, 1) + ", " + fmt(r.lab.a, 1) + ", " + fmt(r.lab.b, 1),
              {x, y + 294}, 16, hud::kText);
    label(card, "HSV RULES", {x + 150, y + 274});
    hud::text(card, r.hsvName, {x + 150, y + 294}, 16, hud::kText);

    // Dominant palette of the scene
    const auto& dom = tracker_.colors();
    cv::Rect strip(x, y + 318, colW, 26);
    cv::Mat stripImg(strip.size(), CV_8UC3, hud::kPanel);
    int px = 0;
    for (size_t i = 0; i < dom.size(); ++i) {
        int w = (i + 1 == dom.size()) ? colW - px : static_cast<int>(dom[i].fraction * colW);
        if (w > 0) cv::rectangle(stripImg, cv::Rect(px, 0, w, strip.height),
                                 cv::Scalar(dom[i].b, dom[i].g, dom[i].r), cv::FILLED);
        px += std::max(w, 0);
    }
    hud::pasteRounded(card, stripImg, strip, 8);
    label(card, "SCENE PALETTE", {x, y + 362});

    hud::logo(card, {pad + 7, card.rows - 22}, 7);
    hud::text(card, "ColorVision++  \xC2\xB7  " + timestamp("%Y-%m-%d %H:%M"),
              {pad + 22, card.rows - 17}, 12, hud::kMuted);

    std::string path = saveImage(card, "card_" + timestamp("%Y%m%d_%H%M%S") + "_" + slug(best.name) + ".png");
    if (path.empty()) { showToast("Could not save the color card"); return; }

    std::string tally = "  (" + std::to_string(progress_.count()) + "/" + std::to_string(album_.size()) + ")";
    if (dE >= kCollectDE)
        showToast("Saved card. Too far from '" + best.name + "' to collect (\xCE\x94" "E " +
                  fmt(dE, 1) + ", need < 10)");
    else if (progress_.discover(best.name))
        showToast("NEW in your album: " + best.name + "  (" + std::to_string(progress_.count()) + "/" +
                  std::to_string(album_.size()) + ")");
    else
        showToast("Saved card - " + best.name + " is already in your album" + tally);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Rendering
// ─────────────────────────────────────────────────────────────────────────────
cv::Mat CameraApp::render(double now) {
    if (lastRender_ > 0 && now > lastRender_) {
        double inst = 1.0 / (now - lastRender_);
        fps_ = fps_ == 0 ? inst : 0.9 * fps_ + 0.1 * inst;
    }
    lastRender_ = now;

    if (frame_.empty()) {
        cv::Mat blank(540, kCanvasWidth, CV_8UC3, hud::kPanel);
        hud::logo(blank, {kCanvasWidth / 2, 230}, 18);
        hud::textCentered(blank, "Waiting for the camera\xE2\x80\xA6", kCanvasWidth / 2, 290, 18, hud::kMuted);
        return blank;
    }

    cv::Mat view = composeView();
    cleanView_ = view.clone();

    if (split_ && cb_ != ColorblindType::None && mode_ != AppMode::Album) drawSplit(view);
    switch (mode_) {
        case AppMode::Explore: drawExplore(view);   break;
        case AppMode::Hunt:    drawHunt(view, now); break;
        case AppMode::Splash:  drawSplash(view);    break;
        case AppMode::Album:   drawAlbum(view);     break;
    }
    drawTopBar(view);
    drawHintBar(view);
    drawToast(view, now);
    if (help_) drawHelp(view);
    return view;
}

cv::Mat CameraApp::composeView() {
    if (mode_ == AppMode::Album) {
        cv::Mat dim;
        cv::GaussianBlur(frame_, dim, {0, 0}, 12);
        dim.convertTo(dim, -1, 0.35);
        return dim;
    }
    cv::Mat v = mode_ == AppMode::Splash ? splash(frame_) : frame_.clone();
    if (pixel_) v = pixelArt(v);
    if (cb_ != ColorblindType::None) {
        cv::Mat sim = simulateColorblindness(v, cb_);
        if (split_) {
            cv::Rect left(0, 0, v.cols / 2, v.rows);
            v(left).copyTo(sim(left));
        }
        v = sim;
    }
    return v;
}

cv::Mat CameraApp::pixelArt(const cv::Mat& src) {
    // Average each block (INTER_AREA), then repaint it with its nearest named
    // color: the scene redrawn with the classifier's 129-color vocabulary.
    int bw = (src.cols + block_ - 1) / block_, bh = (src.rows + block_ - 1) / block_;
    cv::Mat small;
    cv::resize(src, small, {bw, bh}, 0, 0, cv::INTER_AREA);
    for (int y = 0; y < small.rows; ++y) {
        auto* row = small.ptr<cv::Vec3b>(y);
        for (int x = 0; x < small.cols; ++x) {
            const NamedColor& nc = names_.lookup(row[x][2], row[x][1], row[x][0]);
            row[x] = cv::Vec3b(nc.b, nc.g, nc.r);
        }
    }
    cv::Mat big;
    cv::resize(small, big, {bw * block_, bh * block_}, 0, 0, cv::INTER_NEAREST);
    cv::Mat out = big(cv::Rect(0, 0, src.cols, src.rows)).clone();

    const cv::Scalar grout(26, 22, 20);
    for (int x = block_; x < out.cols; x += block_) cv::line(out, {x, 0}, {x, out.rows}, grout, 1);
    for (int y = block_; y < out.rows; y += block_) cv::line(out, {0, y}, {out.cols, y}, grout, 1);
    return out;
}

cv::Mat CameraApp::splash(const cv::Mat& src) const {
    cv::Mat gray, out;
    cv::cvtColor(src, gray, cv::COLOR_BGR2GRAY);
    if (!hasKey_) {
        cv::cvtColor(gray, out, cv::COLOR_GRAY2BGR);
        return out;
    }

    // Blend weight per tabulated distance: full color inside 70% of the
    // tolerance, fading to grey at the tolerance itself.
    uint8_t weight[256];
    for (int v = 0; v < 256; ++v) {
        double t = std::clamp((tolerance_ - v / 4.0) / (0.3 * tolerance_), 0.0, 1.0);
        weight[v] = static_cast<uint8_t>(std::lround((invert_ ? 1.0 - t : t) * 255.0));
    }

    out.create(src.size(), src.type());
    cv::parallel_for_(cv::Range(0, src.rows), [&](const cv::Range& rows) {
        for (int y = rows.start; y < rows.end; ++y) {
            const auto* s = src.ptr<cv::Vec3b>(y);
            const auto* g = gray.ptr<uchar>(y);
            auto* o = out.ptr<cv::Vec3b>(y);
            for (int x = 0; x < src.cols; ++x) {
                int w = weight[keyDistance_[((s[x][2] >> 2) << 12) | ((s[x][1] >> 2) << 6) | (s[x][0] >> 2)]];
                for (int c = 0; c < 3; ++c)
                    o[x][c] = static_cast<uchar>((s[x][c] * w + g[x] * (255 - w) + 127) / 255);
            }
        }
    });
    return out;
}

// ── Chrome: top bar, hint bar, toast, help, split screen ─────────────────────
std::vector<cv::Rect> CameraApp::tabRects() const {
    std::vector<cv::Rect> rects;
    int x = kMargin + 40 + hud::textWidth("ColorVision++", 15, true) + 18;
    for (int i = 0; i < 4; ++i) {
        std::string label = std::to_string(i + 1) + "  " + kModeNames[i];
        int w = hud::textWidth(label, 14, true) + 26;
        rects.emplace_back(x, kMargin + 5, w, 28);
        x += w + 4;
    }
    return rects;
}

void CameraApp::drawTopBar(cv::Mat& img) const {
    cv::Rect bar(kMargin, kMargin - 2, img.cols - 2 * kMargin, 38);
    hud::glass(img, bar, 13, 0.7);
    hud::logo(img, {bar.x + 20, bar.y + 19}, 8);
    hud::text(img, "ColorVision++", {bar.x + 38, bar.y + 25}, 15, hud::kText, true);

    // Tabs as a segmented control; the active one is a filled pill
    auto tabs = tabRects();
    for (int i = 0; i < 4; ++i) {
        bool active = static_cast<int>(mode_) == i;
        cv::Rect t = tabs[i];
        bool hover = t.contains(mouse_);
        if (active)      hud::roundedFill(img, t, 14, hud::kAccent);
        else if (hover)  hud::roundedFill(img, t, 14, hud::mix(hud::kPanel, kWhite, 0.12));
        std::string label = std::to_string(i + 1) + "  " + kModeNames[i];
        hud::textCentered(img, label, t.x + t.width / 2, t.y + 19, 14,
                          active ? kWhite : hud::kMuted, active);
    }

    // Right side: active filters and frame rate
    std::string fps = std::to_string(static_cast<int>(std::lround(fps_))) + " fps";
    int x = bar.br().x - 14 - hud::textWidth(fps, 12);
    hud::text(img, fps, {x, bar.y + 24}, 12, hud::kMuted);

    std::vector<std::string> chips;
    if (frozen_) chips.push_back("FROZEN");
    if (split_ && cb_ != ColorblindType::None) chips.push_back("SPLIT");
    if (cb_ != ColorblindType::None) chips.push_back(chipName(cb_));
    if (pixel_ && mode_ != AppMode::Album) chips.push_back("PIXEL " + std::to_string(block_));
    for (const auto& c : chips) {
        int w = hud::textWidth(c, 11, true) + 18;
        x -= w + 8;
        cv::Rect r(x, bar.y + 9, w, 20);
        hud::roundedFill(img, r, 10, hud::mix(hud::kPanel, hud::kAccent, 0.35));
        hud::roundedStroke(img, r, 10, hud::kAccent, 1);
        hud::textCentered(img, c, r.x + w / 2, r.y + 14, 11, kWhite, true);
    }
}

std::vector<std::pair<std::string, std::string>> CameraApp::hints() const {
    switch (mode_) {
        case AppMode::Explore:
            return {{"Click / Drag", "sample anywhere"}, {"Right-click", "recentre"},
                    {"Space", "save color card"}, {"P", "pixel art"}, {"C", "colorblind"}, {"H", "help"}};
        case AppMode::Hunt:
            if (hunt_.phase() == ColorHunt::Phase::Playing)
                return {{"Aim", "fill the circle with the target color and hold still"}, {"N", "skip target"}};
            return {{"Space", "start"}, {"D", "difficulty"}, {"H", "help"}};
        case AppMode::Splash:
            return {{"Click", "pick a color"}, {"+ / -", "tolerance"}, {"I", "invert"},
                    {"R", "reset"}, {"Space", "save photo"}};
        case AppMode::Album:
            return {{"Hover", "see a color"}, {"1", "collect in Explore"}, {"2", "or in Hunt"}};
    }
    return {};
}

void CameraApp::drawHintBar(cv::Mat& img) const {
    cv::Rect bar(kMargin, img.rows - kHintBar + 6, img.cols - 2 * kMargin, 32);
    hud::glass(img, bar, 12, 0.7);
    int x = bar.x + 12, baseline = bar.y + 21;
    for (const auto& [key, action] : hints()) {
        int need = hud::keycapWidth(key) + 7 + hud::textWidth(action, 12);
        if (x + need > bar.br().x - 12) break;
        x += hud::keycap(img, key, {x, baseline}) + 7;
        hud::text(img, action, {x, baseline - 1}, 12, hud::kMuted);
        x += hud::textWidth(action, 12) + 20;
    }
}

void CameraApp::drawToast(cv::Mat& img, double now) const {
    if (toast_.empty() || now > toastUntil_) return;
    std::string msg = hud::fit(toast_, 14, img.cols - 120);
    int w = hud::textWidth(msg, 14) + 44;
    int bottom = img.rows - kHintBar - (mode_ == AppMode::Explore ? kStripH + 40 : 16);
    cv::Rect r(img.cols / 2 - w / 2, bottom - 36, w, 36);
    hud::glass(img, r, 18, 0.8);
    cv::circle(img, {r.x + 18, r.y + 18}, 4, hud::kAccent, cv::FILLED, cv::LINE_AA);
    hud::text(img, msg, {r.x + 30, r.y + 23}, 14, hud::kText);
}

void CameraApp::drawHelp(cv::Mat& img) const {
    const std::pair<const char*, const char*> rows[] = {
        {"1-4", "switch mode (or click a tab, or Tab)"},
        {"C", "colorblind simulation: off / protan / deutan / tritan"},
        {"V", "split screen: normal | simulated"},
        {"P", "pixel art - blocks snap to the nearest named color"},
        {"+ / -", "pixel size, or tolerance in Splash (mouse wheel too)"},
        {"F", "freeze frame"},
        {"M", "mirror"},
        {"Esc", "quit"},
    };
    const std::pair<const char*, const char*> modes[] = {
        {"Explore", "click/drag to sample, right-click to recentre, Space saves a color card"},
        {"Hunt", "Space start, N skip target, D difficulty"},
        {"Splash", "click a color, I invert, R reset, Space saves the photo"},
    };
    hud::panel(img, {0, 0, img.cols, img.rows}, 0.35, cv::Scalar(0, 0, 0));
    int w = 640, h = 84 + 26 * int(std::size(rows)) + 22 + 24 * int(std::size(modes));
    cv::Rect r((img.cols - w) / 2, (img.rows - h) / 2, w, h);
    hud::glass(img, r, 20, 0.82);
    hud::text(img, "Controls", {r.x + 28, r.y + 42}, 22, hud::kText, true);
    hud::keycap(img, "H", {r.x + w - 100, r.y + 38});
    hud::text(img, "to close", {r.x + w - 74, r.y + 37}, 12, hud::kMuted);
    int y = r.y + 84;
    for (const auto& [key, what] : rows) {
        hud::keycap(img, key, {r.x + 28, y});
        hud::text(img, what, {r.x + 120, y - 1}, 14, hud::kText);
        y += 26;
    }
    cv::line(img, {r.x + 28, y - 10}, {r.br().x - 28, y - 10}, kDivider, 1);
    y += 14;
    for (const auto& [mode, what] : modes) {
        hud::text(img, mode, {r.x + 28, y}, 13, hud::kAccent, true);
        hud::text(img, what, {r.x + 120, y}, 13, hud::kMuted);
        y += 24;
    }
}

void CameraApp::drawSplit(cv::Mat& img) const {
    int mid = img.cols / 2;
    cv::line(img, {mid, 0}, {mid, img.rows}, cv::Scalar(0, 0, 0), 4, cv::LINE_AA);
    cv::line(img, {mid, 0}, {mid, img.rows}, kWhite, 2, cv::LINE_AA);
    std::string left = "Normal vision", right = colorblindTypeName(cb_);
    int lw = hud::textWidth(left, 12, true) + 24, rw = hud::textWidth(right, 12, true) + 24;
    cv::Rect lr(mid - 10 - lw, kTopBar + 6, lw, 26), rr(mid + 10, kTopBar + 6, rw, 26);
    hud::glass(img, lr, 13, 0.75);
    hud::glass(img, rr, 13, 0.75);
    hud::textCentered(img, left, lr.x + lw / 2, lr.y + 18, 12, hud::kText, true);
    hud::textCentered(img, right, rr.x + rw / 2, rr.y + 18, 12, hud::kText, true);
}

// ── Explore ──────────────────────────────────────────────────────────────────
void CameraApp::drawLoupe(cv::Mat& img, cv::Point p, const cv::Scalar& ringColor) const {
    // A magnifier at the sample point: 3× nearest-neighbour zoom of the view,
    // with the 15×15 averaging window outlined.
    const int R = 46, zoom = 3, half = R / zoom;
    cv::Rect src(p.x - half, p.y - half, 2 * half, 2 * half);
    src.x = std::clamp(src.x, 0, cleanView_.cols - src.width);
    src.y = std::clamp(src.y, 0, cleanView_.rows - src.height);
    cv::Mat zoomed;
    cv::resize(cleanView_(src), zoomed, {2 * R, 2 * R}, 0, 0, cv::INTER_NEAREST);

    cv::Point c((p.x - src.x) * zoom + zoom / 2, (p.y - src.y) * zoom + zoom / 2);
    cv::Rect window(c.x - 7 * zoom - 1, c.y - 7 * zoom - 1, 15 * zoom + 2, 15 * zoom + 2);
    cv::rectangle(zoomed, window, cv::Scalar(0, 0, 0), 3, cv::LINE_AA);
    cv::rectangle(zoomed, window, kWhite, 1, cv::LINE_AA);

    hud::pasteRounded(img, zoomed, cv::Rect(p.x - R, p.y - R, 2 * R, 2 * R), R);
    cv::circle(img, p, R + 8, cv::Scalar(0, 0, 0), 2, cv::LINE_AA);
    cv::circle(img, p, R + 4, ringColor, 7, cv::LINE_AA);
    cv::circle(img, p, R, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
}

void CameraApp::drawExplore(cv::Mat& img) const {
    if (reading_.matches.empty()) return;
    const Reading& r = reading_;
    const std::string& name = r.matches[0].color->name;
    const double dE = r.matches[0].dE;
    const cv::Point p = samplePoint();

    // Readout card — moves to the right edge when the sample point is under it
    const int w = 330, y = kTopBar + 8;
    const int simRows = r.simulated ? 50 : 0;
    const int h = 316 + simRows;
    int x = kMargin;
    if (cv::Rect(x - 70, y - 70, w + 140, h + 140).contains(p)) x = img.cols - w - kMargin;
    hud::glass(img, {x, y, w, h}, 18);

    // Header: swatch, name, match quality
    hud::swatch(img, {x + 18, y + 18, 76, 76}, sc(r.bgr), 14);
    const int tx = x + 110;
    hud::text(img, hud::fit(name, 24, w - 126, true), {tx, y + 46}, 24, hud::kText, true);
    hud::text(img, "\xCE\x94" "E00 " + fmt(dE, 2) + "  \xC2\xB7  " + deltaELabel(dE), {tx, y + 68}, 12, hud::kMuted);
    hud::bar(img, {tx, y + 80, w - 128, 4}, matchQuality(dE), matchColor(dE));

    cv::line(img, {x + 18, y + 112}, {x + w - 18, y + 112}, kDivider, 1);

    // Value grid
    const int c2 = x + 172;
    label(img, "HEX", {x + 18, y + 136});
    hud::text(img, hexOf(r.bgr), {x + 18, y + 157}, 15, hud::kText);
    label(img, "RGB", {c2, y + 136});
    hud::text(img, std::to_string(r.bgr[2]) + ", " + std::to_string(r.bgr[1]) + ", " + std::to_string(r.bgr[0]),
              {c2, y + 157}, 15, hud::kText);
    label(img, "CIE LAB", {x + 18, y + 184});
    hud::text(img, fmt(r.lab.L, 1) + ", " + fmt(r.lab.a, 1) + ", " + fmt(r.lab.b, 1), {x + 18, y + 205}, 15, hud::kText);
    label(img, "HSV", {c2, y + 184});
    hud::text(img, std::to_string(r.h) + "\xC2\xB0, " + std::to_string(r.s) + "%, " + std::to_string(r.v) + "%",
              {c2, y + 205}, 15, hud::kText);

    // Runner-up names and what the original HSV rules say
    label(img, "ALSO CLOSE", {x + 18, y + 232});
    int cx = x + 18;
    for (size_t i = 1; i < r.matches.size(); ++i) {
        const NamedColor& nc = *r.matches[i].color;
        std::string t = nc.name + "  " + fmt(r.matches[i].dE, 1);
        int tw = hud::textWidth(t, 13);
        if (cx + 18 + tw > x + w - 18) break;
        hud::swatch(img, {cx, y + 241, 12, 12}, sc(nc), 6);
        hud::text(img, t, {cx + 18, y + 252}, 13, hud::kText);
        cx += 18 + tw + 16;
    }
    hud::text(img, "HSV rules (v1) say  " + r.hsvName, {x + 18, y + 274}, 12, hud::kMuted);

    int fy = y + 274;
    if (r.simulated) {
        label(img, upper(viewerName(cb_)) + " SEES", {x + 18, fy + 26});
        hud::swatch(img, {x + 18, fy + 34, 16, 16}, sc(r.simBGR), 5);
        hud::text(img, hud::fit(r.simName->name, 15, w - 70, true), {x + 42, fy + 48}, 15, hud::kWarn, true);
        fy += simRows;
    }

    // Footer: album progress
    cv::line(img, {x + 18, fy + 14}, {x + w - 18, fy + 14}, kDivider, 1);
    label(img, "ALBUM", {x + 18, fy + 34});
    hud::bar(img, {x + 66, fy + 28, 90, 4}, double(progress_.count()) / album_.size(), hud::kAccent);
    hud::text(img, std::to_string(progress_.count()) + "/" + std::to_string(album_.size()),
              {x + 164, fy + 34}, 12, hud::kText);
    int kw = hud::keycapWidth("Space");
    hud::keycap(img, "Space", {x + w - 18 - kw - 52, fy + 35});
    hud::text(img, "capture", {x + w - 18 - 46, fy + 34}, 12, hud::kMuted);

    drawPaletteStrip(img);

    // Magnifier and floating name tag, on top of everything else
    drawLoupe(img, p, sc(r.bgr));
    int tagW = hud::textWidth(name, 15, true) + 46, tagH = 34;
    cv::Rect tag(p.x + 62, p.y - tagH / 2, tagW, tagH);
    if (tag.br().x > img.cols - kMargin) tag.x = p.x - 62 - tagW;
    hud::glass(img, tag, 17, 0.78);
    cv::circle(img, {tag.x + 18, tag.y + 17}, 7, sc(r.bgr), cv::FILLED, cv::LINE_AA);
    cv::circle(img, {tag.x + 18, tag.y + 17}, 7, hud::mix(sc(r.bgr), kWhite, 0.45), 1, cv::LINE_AA);
    hud::text(img, name, {tag.x + 32, tag.y + 22}, 15, hud::kText, true);
}

void CameraApp::drawPaletteStrip(cv::Mat& img) const {
    // Clusters that share a name are shown as one segment
    struct Segment { cv::Scalar color; std::string name; float share; float biggest; };
    std::vector<Segment> segs;
    float total = 0;
    for (const auto& d : tracker_.colors()) {
        total += d.fraction;
        auto it = std::find_if(segs.begin(), segs.end(), [&](const Segment& s) { return s.name == d.nearestName; });
        cv::Scalar c(d.b, d.g, d.r);
        if (it == segs.end()) { segs.push_back({c, d.nearestName, d.fraction, d.fraction}); continue; }
        it->share += d.fraction;
        if (d.fraction > it->biggest) { it->biggest = d.fraction; it->color = c; }
    }
    if (segs.empty() || total <= 0) return;
    std::stable_sort(segs.begin(), segs.end(), [](const Segment& a, const Segment& b) { return a.share > b.share; });

    cv::Rect r(kMargin, img.rows - kHintBar - kStripH - 4, img.cols - 2 * kMargin, kStripH);
    cv::Mat strip(r.size(), CV_8UC3, hud::kPanel);
    int x = 0;
    for (size_t i = 0; i < segs.size(); ++i) {
        int w = (i + 1 == segs.size()) ? r.width - x
                                       : static_cast<int>(std::lround(segs[i].share / total * r.width));
        if (w <= 0) continue;
        cv::rectangle(strip, cv::Rect(x, 0, w, r.height), segs[i].color, cv::FILLED);
        std::string pct = std::to_string(static_cast<int>(std::lround(segs[i].share / total * 100))) + "%";
        std::string label = segs[i].name + "  " + pct;
        cv::Scalar ink = hud::inkOn(segs[i].color);
        if (hud::textWidth(label, 12, true) + 16 <= w)
            hud::textCentered(strip, label, x + w / 2, 25, 12, ink, true);
        else if (hud::textWidth(pct, 11) + 8 <= w)
            hud::textCentered(strip, pct, x + w / 2, 25, 11, ink);
        x += w;
    }
    hud::pasteRounded(img, strip, r, 12);
    hud::roundedStroke(img, r, 12, hud::mix(hud::kPanel, kWhite, 0.25), 1);
    hud::textShadow(img, "Dominant colors  \xC2\xB7  K-Means++ in CIE Lab", {r.x + 6, r.y - 8}, 12, hud::kText);
}

// ── Hunt ─────────────────────────────────────────────────────────────────────
void CameraApp::drawHunt(cv::Mat& img, double now) const {
    const cv::Point c = center();
    const double threshold = huntThreshold(hunt_.level());

    switch (hunt_.phase()) {
    case ColorHunt::Phase::Ready: {
        hud::panel(img, {0, 0, img.cols, img.rows}, 0.35, cv::Scalar(0, 0, 0));
        cv::Rect r(c.x - 290, c.y - 150, 580, 290);
        hud::glass(img, r, 24, 0.75);
        hud::logo(img, {c.x, r.y + 42}, 14);
        hud::textCentered(img, "COLOR HUNT", c.x, r.y + 100, 36, hud::kText, true);
        hud::textCentered(img, "Show the camera real objects that match the target color.",
                          c.x, r.y + 136, 15, hud::kMuted);
        hud::textCentered(img, "Fill the circle and hold steady until it locks. 60 seconds.",
                          c.x, r.y + 158, 15, hud::kMuted);

        // Level selector: three pills
        const HuntLevel levels[] = {HuntLevel::Easy, HuntLevel::Normal, HuntLevel::Hard};
        int px = c.x - 3 * 96 / 2 - 6;
        for (HuntLevel lv : levels) {
            cv::Rect pill(px, r.y + 182, 92, 28);
            bool on = lv == hunt_.level();
            if (on) hud::roundedFill(img, pill, 14, hud::kAccent);
            else    hud::roundedStroke(img, pill, 14, hud::mix(hud::kPanel, kWhite, 0.3), 1);
            hud::textCentered(img, std::string(huntLevelName(lv)) + "  <" + fmt(huntThreshold(lv), 0),
                              pill.x + 46, pill.y + 19, 12, on ? kWhite : hud::kMuted, on);
            px += 96 + 4;
        }
        hud::textCentered(img, "Best " + std::to_string(progress_.best(huntLevelName(hunt_.level()))) +
                          "   \xC2\xB7   D changes difficulty (\xCE\x94" "E00 threshold)",
                          c.x, r.y + 232, 12, hud::kMuted);

        double pulse = 0.5 + 0.5 * std::sin(now * 4.0);
        cv::Rect go(c.x - 110, r.y + 246, 220, 32);
        hud::roundedFill(img, go, 16, hud::mix(hud::kPanel, hud::kAccent, 0.55 + 0.45 * pulse));
        hud::textCentered(img, "Press SPACE to start", c.x, go.y + 22, 14, kWhite, true);
        break;
    }
    case ColorHunt::Phase::Countdown: {
        double left = hunt_.countdownLeft(now);
        int n = std::max(1, static_cast<int>(std::ceil(left)));
        double frac = left - std::floor(left);
        cv::Rect disc(c.x - 80, c.y - 80, 160, 160);
        hud::glass(img, disc, 80, 0.6);
        hud::ring(img, c, 86, frac == 0 ? 1.0 : frac, hud::kAccent, 6);
        hud::textCentered(img, std::to_string(n), c.x, c.y + 34, 96, hud::kText, true);
        hud::textShadow(img, "Get ready!", {c.x - hud::textWidth("Get ready!", 20, true) / 2, c.y + 130},
                        20, hud::kText, true);
        break;
    }
    case ColorHunt::Phase::Playing: {
        const NamedColor& t = hunt_.target();
        double dE = hunt_.currentDE();
        Temperature temp = temperature(dE, threshold);

        // Capture flash
        double since = now - flashAt_;
        if (since < 0.25) hud::panel(img, {0, 0, img.cols, img.rows}, 0.55 * (1.0 - since / 0.25), kWhite);

        // Reticle: ring tinted by temperature, outer arc fills while locking
        cv::circle(img, c, 40, cv::Scalar(0, 0, 0), 6, cv::LINE_AA);
        cv::circle(img, c, 40, temp.color, 3, cv::LINE_AA);
        cv::circle(img, c, 52, cv::Scalar(0, 0, 0, 0), 1, cv::LINE_AA);
        hud::ring(img, c, 52, hunt_.holdProgress(), hud::kGood, 8);

        // Target card
        cv::Rect card(c.x - 250, kTopBar + 8, 500, 112);
        hud::glass(img, card, 20, 0.72);
        hud::swatch(img, {card.x + 16, card.y + 16, 80, 80}, sc(t), 16);
        label(img, "FIND", {card.x + 112, card.y + 34});
        hud::text(img, hud::fit(t.name, 26, 220, true), {card.x + 112, card.y + 66}, 26, hud::kText, true);
        hud::text(img, temp.word, {card.x + 112, card.y + 92}, 14, temp.color, true);

        cv::line(img, {card.x + 348, card.y + 18}, {card.x + 348, card.y + 94}, kDivider, 1);
        label(img, "YOURS", {card.x + 364, card.y + 34});
        hud::swatch(img, {card.x + 364, card.y + 44, 44, 44}, sc(huntSample_), 10);
        hud::text(img, "\xCE\x94" "E", {card.x + 418, card.y + 60}, 12, hud::kMuted, true);
        hud::text(img, fmt(dE, 1), {card.x + 418, card.y + 84}, 22, hud::kText, true);

        // Proximity meter
        double proximity = dE <= threshold ? 1.0 : std::clamp(1.0 - (dE - threshold) / (45.0 - threshold), 0.0, 1.0);
        hud::bar(img, {card.x + 16, card.br().y - 9, card.width - 32, 4}, proximity, temp.color);

        // Timer and score
        double left = hunt_.timeLeft(now);
        int secs = static_cast<int>(std::ceil(left));
        char clock[16];
        std::snprintf(clock, sizeof clock, "%d:%02d", secs / 60, secs % 60);
        cv::Rect tr(img.cols - 150 - kMargin, kTopBar + 8, 150, 112);
        hud::glass(img, tr, 20, 0.72);
        label(img, "TIME", {tr.x + 18, tr.y + 30});
        hud::text(img, clock, {tr.x + 18, tr.y + 64}, 32, left < 10 ? hud::kBad : hud::kText, true);
        hud::bar(img, {tr.x + 18, tr.y + 74, tr.width - 36, 4}, left / ColorHunt::kRoundSeconds,
                 left < 10 ? hud::kBad : hud::kAccent);
        hud::text(img, "Score  " + std::to_string(hunt_.score()), {tr.x + 18, tr.y + 98}, 14, hud::kAccent, true);

        // Floating points
        for (const auto& f : floaters_) {
            double age = now - f.born;
            if (age > 1.2) continue;
            int rise = static_cast<int>(age * 70);
            hud::textShadow(img, f.text, {c.x + 62, c.y - 20 - rise}, 32,
                            hud::mix(hud::kGood, hud::kText, age / 1.2), true);
        }

        // Finds so far, as a row of little swatches
        const auto& finds = hunt_.finds();
        std::string found = std::to_string(finds.size()) + " found";
        int fw = hud::textWidth(found, 13, true) + 36 + 18 * std::min<int>(int(finds.size()), 10);
        cv::Rect fr(kMargin, img.rows - kHintBar - 40, fw, 32);
        hud::glass(img, fr, 16, 0.72);
        hud::text(img, found, {fr.x + 14, fr.y + 21}, 13, hud::kText, true);
        int sx = fr.x + 22 + hud::textWidth(found, 13, true);
        for (size_t i = finds.size() > 10 ? finds.size() - 10 : 0; i < finds.size(); ++i, sx += 18)
            hud::swatch(img, {sx, fr.y + 9, 14, 14}, sc(*finds[i].target), 7);
        break;
    }
    case ColorHunt::Phase::Over: {
        hud::panel(img, {0, 0, img.cols, img.rows}, 0.4, cv::Scalar(0, 0, 0));
        cv::Rect r(c.x - 390, std::max(kTopBar + 4, c.y - 222), 780, 420);
        hud::glass(img, r, 24, 0.8);
        drawHuntSummary(img, cv::Rect(r.x, r.y, r.width, r.height - 26));
        std::string footer = "Space  play again   \xC2\xB7   D  difficulty";
        if (!huntCardPath_.empty()) footer += "   \xC2\xB7   saved " + huntCardPath_;
        hud::textCentered(img, hud::fit(footer, 12, r.width - 40), c.x, r.br().y - 18, 12, hud::kMuted);
        break;
    }
    }
}

void CameraApp::drawHuntSummary(cv::Mat& img, cv::Rect area) const {
    const int cx = area.x + area.width / 2;
    const auto& finds = hunt_.finds();
    const std::string level = huntLevelName(hunt_.level());

    label(img, "TIME'S UP", {cx - hud::textWidth("TIME'S UP", 11, true) / 2, area.y + 38});
    hud::textCentered(img, std::to_string(hunt_.score()), cx, area.y + 96, 56, hud::kText, true);
    if (huntNewBest_) {
        cv::Rect badge(cx - 52, area.y + 108, 104, 24);
        hud::roundedFill(img, badge, 12, hud::kGood);
        hud::textCentered(img, "NEW BEST", cx, badge.y + 17, 12, cv::Scalar(20, 40, 20), true);
    } else {
        hud::textCentered(img, "Best " + std::to_string(progress_.best(level)), cx, area.y + 126, 13, hud::kMuted);
    }
    hud::textCentered(img, std::to_string(finds.size()) + " found  \xC2\xB7  " + std::to_string(hunt_.skips()) +
                      " skipped  \xC2\xB7  " + level + " (\xCE\x94" "E00 < " +
                      fmt(huntThreshold(hunt_.level()), 0) + ")", cx, area.y + 158, 13, hud::kMuted);

    if (finds.empty()) {
        hud::textCentered(img, "Nothing locked this time - try Easy (D) or brighter light.",
                          cx, area.y + 250, 15, hud::kMuted);
        return;
    }

    // The most recent finds: object photo above the target swatch
    const int tile = 104, gap = 16;
    int n = std::min<int>(static_cast<int>(finds.size()), 6);
    int x = cx - (n * tile + (n - 1) * gap) / 2, y = area.y + 184;
    for (int i = 0; i < n; ++i, x += tile + gap) {
        const HuntFind& f = finds[finds.size() - n + i];
        cv::Rect photo(x, y, tile, tile);
        cv::Mat t(photo.size(), CV_8UC3, sc(f.found));
        if (!f.thumb.empty()) cv::resize(f.thumb, t, photo.size(), 0, 0, cv::INTER_AREA);
        hud::pasteRounded(img, t, photo, 14);
        // Target chip overlapping the photo's corner
        hud::swatch(img, {x + tile - 30, y + tile - 30, 24, 24}, sc(*f.target), 12);
        hud::text(img, hud::fit(f.target->name, 13, tile, true), {x, y + tile + 22}, 13, hud::kText, true);
        hud::text(img, "\xCE\x94" "E " + fmt(f.dE, 1) + "   +" + std::to_string(f.points),
                  {x, y + tile + 40}, 12, hud::kMuted);
    }
}

// ── Splash ───────────────────────────────────────────────────────────────────
void CameraApp::drawSplash(cv::Mat& img) const {
    if (!hasKey_) {
        cv::Point c = center();
        cv::Rect r(c.x - 230, c.y - 54, 460, 100);
        hud::glass(img, r, 22, 0.72);
        hud::textCentered(img, "Click any color to bring it back", c.x, r.y + 46, 22, hud::kText, true);
        hud::textCentered(img, "Everything else stays black & white", c.x, r.y + 74, 13, hud::kMuted);
        return;
    }
    cv::Rect r(kMargin, kTopBar + 8, 300, 96);
    hud::glass(img, r, 18);
    hud::swatch(img, {r.x + 16, r.y + 16, 64, 64}, sc(keyBGR_), 14);
    label(img, invert_ ? "REMOVING" : "KEEPING", {r.x + 96, r.y + 32});
    hud::text(img, hud::fit(keyName_->name, 20, r.width - 110, true), {r.x + 96, r.y + 56}, 20, hud::kText, true);
    hud::text(img, "within \xCE\x94" "E00 " + fmt(tolerance_, 0), {r.x + 96, r.y + 78}, 12, hud::kMuted);
    hud::bar(img, {r.x + 190, r.y + 72, 92, 4}, (tolerance_ - 4.0) / 56.0, hud::kAccent);
}

// ── Album ────────────────────────────────────────────────────────────────────
void CameraApp::drawAlbum(cv::Mat& img) const {
    const int total = static_cast<int>(album_.size());
    const int found = static_cast<int>(progress_.count());
    const int cols = 16, rows = (total + cols - 1) / cols;
    const int top = kTopBar + 74, bottomArea = kHintBar + 70;
    const int cell = std::min((img.cols - 80) / cols, (img.rows - top - bottomArea) / rows);
    const int chip = cell - 6;
    const int gridW = cols * cell;
    const int x0 = (img.cols - gridW) / 2;

    cv::Rect card(x0 - 20, kTopBar + 8, gridW + 40, img.rows - kTopBar - kHintBar - 8);
    hud::glass(img, card, 22, 0.55);

    hud::text(img, "Color Album", {x0, kTopBar + 48}, 24, hud::kText, true);
    std::string tally = std::to_string(found) + " / " + std::to_string(total);
    int barW = 200, barX = x0 + gridW - barW;
    hud::text(img, tally, {barX - 12 - hud::textWidth(tally, 14, true), kTopBar + 46}, 14, hud::kText, true);
    hud::bar(img, {barX, kTopBar + 38, barW, 6}, double(found) / std::max(total, 1), hud::kAccent);

    const NamedColor* hovered = nullptr;
    for (int i = 0; i < total; ++i) {
        const NamedColor* nc = album_[i];
        cv::Rect r(x0 + (i % cols) * cell + 3, top + (i / cols) * cell + 3, chip, chip);
        if (progress_.has(nc->name)) {
            hud::swatch(img, r, sc(*nc), 8);
        } else {
            // Undiscovered: a faint hint of the color behind a question mark
            hud::roundedFill(img, r, 8, hud::mix(sc(*nc), hud::kPanel, 0.7));
            hud::textCentered(img, "?", r.x + r.width / 2, r.y + r.height / 2 + 6, 16,
                              cv::Scalar(150, 150, 150), true);
        }
        if (r.contains(mouse_)) {
            hovered = nc;
            hud::roundedStroke(img, cv::Rect(r.x - 2, r.y - 2, r.width + 4, r.height + 4), 10, kWhite, 2);
        }
    }

    // Details for the hovered chip
    int y = top + rows * cell + 14;
    if (hovered) {
        bool have = progress_.has(hovered->name);
        hud::swatch(img, {x0, y, 44, 44}, sc(*hovered), 10);
        hud::text(img, hovered->name, {x0 + 58, y + 20}, 18, hud::kText, true);
        char hex[8];
        std::snprintf(hex, sizeof hex, "#%02X%02X%02X", hovered->r, hovered->g, hovered->b);
        hud::text(img, std::string(hex) + "   \xC2\xB7   " +
                  (have ? "collected" : "not found yet - try Hunt, or capture one in Explore"),
                  {x0 + 58, y + 40}, 12, have ? hud::kGood : hud::kMuted);
    } else {
        hud::text(img, found == 0 ? "Your album is empty. Every named color you find shows up here."
                                  : "Hover a color to see its name.",
                  {x0, y + 28}, 13, hud::kMuted);
    }
}
