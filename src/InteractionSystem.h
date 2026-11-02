#pragma once

#include <SDL3/SDL.h>

#include <functional>
#include <string>
#include <vector>

namespace vn {

struct SpriteHitBox {
    int slot = -1;
    SDL_FRect rect{ 0.0f, 0.0f, 0.0f, 0.0f };
    std::string characterTag;
    std::string partTag;
    std::string callbackName;
};

class InteractionSystem {
public:
    using InteractionCallback = std::function<void(const std::string& callbackName,
                                                   const std::string& characterTag,
                                                   const std::string& partTag,
                                                   const std::string& eventName)>;

    void Update(float mouseX,
                float mouseY,
                bool clicked,
                const std::vector<SpriteHitBox>& hitboxes,
                const InteractionCallback& onInteract) {
        int currentHover = -1;
        SpriteHitBox currentData{};

        for (auto it = hitboxes.rbegin(); it != hitboxes.rend(); ++it) {
            if (mouseX >= it->rect.x && mouseX <= it->rect.x + it->rect.w &&
                mouseY >= it->rect.y && mouseY <= it->rect.y + it->rect.h) {
                currentHover = it->slot;
                currentData = *it;
                break;
            }
        }

        if (currentHover != m_hoveredSlot) {
            if (m_hoveredSlot >= 0 && !m_hoveredData.callbackName.empty() && onInteract) {
                onInteract(m_hoveredData.callbackName, m_hoveredData.characterTag, m_hoveredData.partTag, "hover_leave");
            }
            if (currentHover >= 0 && !currentData.callbackName.empty() && onInteract) {
                onInteract(currentData.callbackName, currentData.characterTag, currentData.partTag, "hover_enter");
            }
            m_hoveredSlot = currentHover;
            m_hoveredData = currentData;
        }

        if (clicked && m_hoveredSlot >= 0 && !m_hoveredData.callbackName.empty() && onInteract) {
            onInteract(m_hoveredData.callbackName, m_hoveredData.characterTag, m_hoveredData.partTag, "click");
        }
    }

    [[nodiscard]] int GetHoveredSprite() const {
        return m_hoveredSlot;
    }

    void Reset() {
        m_hoveredSlot = -1;
        m_hoveredData = {};
    }

private:
    int m_hoveredSlot = -1;
    SpriteHitBox m_hoveredData;
};

} // namespace vn