#include "Progress.h"
#include <filesystem>
#include <fstream>
#include <sstream>

// File format, one record per line:
//   found <color name, may contain spaces>
//   best <level> <score>
Progress::Progress(std::string path) : path_(std::move(path)) {
    std::ifstream f(path_);
    std::string line;
    while (std::getline(f, line)) {
        if (line.rfind("found ", 0) == 0) {
            found_.insert(line.substr(6));
        } else if (line.rfind("best ", 0) == 0) {
            std::istringstream in(line.substr(5));
            std::string key;
            int score;
            if (in >> key >> score) best_[key] = score;
        }
    }
}

bool Progress::discover(const std::string& colorName) {
    if (!found_.insert(colorName).second) return false;
    save();
    return true;
}

int Progress::best(const std::string& key) const {
    auto it = best_.find(key);
    return it == best_.end() ? 0 : it->second;
}

bool Progress::submit(const std::string& key, int score) {
    if (score <= best(key)) return false;
    best_[key] = score;
    save();
    return true;
}

void Progress::save() const {
    std::error_code ec;
    auto dir = std::filesystem::path(path_).parent_path();
    if (!dir.empty()) std::filesystem::create_directories(dir, ec);

    std::ofstream f(path_);
    f << "# ColorVision++ progress\n";
    for (const auto& name : found_) f << "found " << name << "\n";
    for (const auto& [key, score] : best_) f << "best " << key << " " << score << "\n";
}
