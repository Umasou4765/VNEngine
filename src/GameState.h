#pragma once

#include <deque>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "SaveSnapshot.h"

namespace vn {

struct GameState {
    std::unordered_map<std::string, bool> flags;
    std::unordered_map<std::string, double> numbers;
    std::string chapterName = "Prologue";
    std::string currentBgmPath;
    std::unordered_set<std::string> seenLines;
    std::deque<SaveSnapshot> rollbackHistory;

    void ResetForNewScript() {
        flags.clear();
        numbers.clear();
        chapterName = "Prologue";
        currentBgmPath.clear();
        rollbackHistory.clear();
    }
};

} // namespace vn
