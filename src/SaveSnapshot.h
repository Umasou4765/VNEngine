#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "DialogueLine.h"

namespace vn {

struct SaveSnapshot {
    struct SaveSpriteState {
        int slot = 0;
        std::string path;
        float x = 0.0f;
        float y = 0.0f;
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        float rotation = 0.0f;
        float anchorX = 0.5f;
        float anchorY = 1.0f;
        bool useAbsolute = false;
        std::uint8_t r = 255;
        std::uint8_t g = 255;
        std::uint8_t b = 255;
        float alpha = 1.0f;
    };

    struct ChoiceRecord {
        std::string id;
        int selectedIndex = 0;
    };

    struct ObserveRecord {
        std::string id;
        std::string selectedTarget;
    };

    std::string scriptPath;
    std::uint64_t scriptContentHash = 0;
    std::string luaEntryPoint = "main";
    std::string luaResumeId;
    std::string luaWaitReason;
    std::uint64_t luaYieldSequence = 0;
    std::uint32_t luaYieldOccurrence = 0;
    std::size_t luaChoiceCursor = 0;
    std::size_t luaObserveCursor = 0;
    std::vector<ChoiceRecord> luaChoiceHistory;
    std::vector<ObserveRecord> luaObserveHistory;
    std::uint64_t luaDeterministicSeed = 0;
    std::uint64_t luaDeterministicCounter = 0;
    std::string chapterName;
    std::string saveTime;
    std::string bgPath;
    std::vector<SaveSpriteState> spriteStates;
    std::unordered_map<std::string, bool> flags;
    std::unordered_map<std::string, double> numVars;
    std::string currentBgmPath;
    float cameraX = 0.0f;
    float cameraY = 0.0f;
    float cameraZoom = 1.0f;
    std::vector<DialogueLine> dialogueLog;
};

} // namespace vn
