#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>

namespace Draw {

inline bool Contains(const SDL_FRect& rect, float x, float y) {
    return x >= rect.x && x <= rect.x + rect.w && y >= rect.y && y <= rect.y + rect.h;
}

inline void Rect(SDL_Renderer* renderer,
                 const SDL_FRect& rect,
                 SDL_Color fill,
                 SDL_Color border) {
    if (!renderer) return;
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, fill.r, fill.g, fill.b, fill.a);
    SDL_RenderFillRect(renderer, &rect);
    SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
    SDL_RenderRect(renderer, &rect);
}

inline bool Texture9Slice(SDL_Renderer* renderer,
                          SDL_Texture* texture,
                          const SDL_FRect& dst,
                          float cornerSize,
                          Uint8 alpha = 255) {
    if (!renderer || !texture || dst.w <= 1.0f || dst.h <= 1.0f) {
        return false;
    }

    float texW = 0.0f;
    float texH = 0.0f;
    if (!SDL_GetTextureSize(texture, &texW, &texH) || texW <= 1.0f || texH <= 1.0f) {
        return false;
    }

    const float maxCorner = std::max(1.0f, std::min(texW, texH) * 0.5f - 1.0f);
    const float srcCorner = std::clamp(cornerSize, 1.0f, maxCorner);
    const float dstCorner = std::clamp(srcCorner, 1.0f, std::min(dst.w, dst.h) * 0.5f - 0.5f);

    const float srcCenterW = std::max(1.0f, texW - srcCorner * 2.0f);
    const float srcCenterH = std::max(1.0f, texH - srcCorner * 2.0f);
    const float dstCenterW = std::max(1.0f, dst.w - dstCorner * 2.0f);
    const float dstCenterH = std::max(1.0f, dst.h - dstCorner * 2.0f);

    const std::array<float, 4> x = {
        dst.x,
        dst.x + dstCorner,
        dst.x + dst.w - dstCorner,
        dst.x + dst.w
    };
    const std::array<float, 4> y = {
        dst.y,
        dst.y + dstCorner,
        dst.y + dst.h - dstCorner,
        dst.y + dst.h
    };
    const std::array<float, 4> u = {
        0.0f,
        srcCorner / texW,
        (texW - srcCorner) / texW,
        1.0f
    };
    const std::array<float, 4> v = {
        0.0f,
        srcCorner / texH,
        (texH - srcCorner) / texH,
        1.0f
    };

    std::array<SDL_Vertex, 16> vertices{};
    const SDL_FColor color{
        1.0f,
        1.0f,
        1.0f,
        static_cast<float>(alpha) / 255.0f
    };

    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            SDL_Vertex& vertex = vertices[static_cast<std::size_t>(row * 4 + col)];
            vertex.position = { x[static_cast<std::size_t>(col)], y[static_cast<std::size_t>(row)] };
            vertex.color = color;
            vertex.tex_coord = { u[static_cast<std::size_t>(col)], v[static_cast<std::size_t>(row)] };
        }
    }

    std::array<int, 54> indices{};
    int indexCursor = 0;
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            const int topLeft = row * 4 + col;
            const int topRight = topLeft + 1;
            const int bottomLeft = topLeft + 4;
            const int bottomRight = bottomLeft + 1;

            indices[static_cast<std::size_t>(indexCursor++)] = topLeft;
            indices[static_cast<std::size_t>(indexCursor++)] = topRight;
            indices[static_cast<std::size_t>(indexCursor++)] = bottomRight;
            indices[static_cast<std::size_t>(indexCursor++)] = topLeft;
            indices[static_cast<std::size_t>(indexCursor++)] = bottomRight;
            indices[static_cast<std::size_t>(indexCursor++)] = bottomLeft;
        }
    }

    return SDL_RenderGeometry(renderer,
                              texture,
                              vertices.data(),
                              static_cast<int>(vertices.size()),
                              indices.data(),
                              static_cast<int>(indices.size()));
}

} // namespace Draw

namespace UI {

struct ControlSample {
    bool hovered = false;
    bool activated = false;
};

inline ControlSample Button(const SDL_FRect& rect,
                            float x,
                            float y,
                            bool clicked,
                            bool disabled = false) {
    if (disabled) {
        return {};
    }
    const bool hovered = Draw::Contains(rect, x, y);
    return { hovered, hovered && clicked };
}

inline ControlSample Toggle(const SDL_FRect& rect,
                            float x,
                            float y,
                            bool clicked,
                            bool& value,
                            bool disabled = false) {
    ControlSample sample = Button(rect, x, y, clicked, disabled);
    if (sample.activated) {
        value = !value;
    }
    return sample;
}

inline ControlSample Slider(const SDL_FRect& track,
                            float x,
                            float y,
                            bool clicked,
                            bool held,
                            bool captured,
                            float& normalized) {
    const SDL_FRect hitRect{
        track.x - 20.0f,
        track.y - 30.0f,
        track.w + 40.0f,
        track.h + 60.0f
    };
    const bool hovered = Draw::Contains(hitRect, x, y);
    const bool active = (hovered && clicked) || (captured && held);
    if (active && track.w > 0.0f) {
        normalized = std::clamp((x - track.x) / track.w, 0.0f, 1.0f);
    }
    return { hovered, active };
}

} // namespace UI
