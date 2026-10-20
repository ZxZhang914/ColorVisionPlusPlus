#pragma once
#include <map>
#include <set>
#include <string>

// Persistent player progress: which named colors have been collected and the
// best Color Hunt score per level. Stored as a small text file and rewritten
// on every change.
class Progress {
public:
    explicit Progress(std::string path);

    bool   discover(const std::string& colorName);   // true when it is new
    bool   has(const std::string& colorName) const { return found_.count(colorName) > 0; }
    size_t count() const { return found_.size(); }

    int  best(const std::string& key) const;
    bool submit(const std::string& key, int score);  // true on a new best

private:
    void save() const;

    std::string path_;
    std::set<std::string> found_;
    std::map<std::string, int> best_;
};
