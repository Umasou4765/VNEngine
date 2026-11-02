#pragma once

#include <string>
#include <unordered_map>

namespace vn {

struct GameSettings {
    float textSpeed = 30.0f;
    float bgmVolume = 0.8f;
    float sfxVolume = 0.9f;
    std::string locale = "zh";
    bool skipUnseen = false;
    bool wheelAdvanceEnabled = false;
    bool voiceBleedEnabled = false;
    bool instantText = false;
    bool animatedSprites = true;
    bool fullscreen = false;
    std::unordered_map<std::string, bool> unlockFlags;
};

} // namespace vn
