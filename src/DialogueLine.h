#pragma once

#include <string>

namespace vn {
    struct DialogueLine {
        std::string speaker;
        std::string text;
        std::string voicePath;
        std::string lineId;
        std::string rawText;
    };
}
