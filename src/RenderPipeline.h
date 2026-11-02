#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

namespace vn {

struct SceneCameraState {
    float x = 0.0f;
    float y = 0.0f;
    float zoom = 1.0f;
};

struct SceneRenderLayer {
    SDL_Texture* current = nullptr;
    SDL_Texture* previous = nullptr;
    float crossfadeMix = 1.0f;
};

struct SceneSpriteRenderCommand {
    int slot = 0;
    SDL_Texture* current = nullptr;
    SDL_Texture* previous = nullptr;
    float crossfadeMix = 1.0f;
    SDL_FRect screenRect{ 0.0f, 0.0f, 0.0f, 0.0f };
    bool screenRectValid = false;
    SDL_Color tintColor{ 255, 255, 255, 255 };
    float opacity = 1.0f;
    float rotationDeg = 0.0f;
    SDL_FPoint anchor{ 0.5f, 1.0f };
    bool clipEnabled = false;
    SDL_FRect clipRect{ 0.0f, 0.0f, 0.0f, 0.0f };
};

struct SceneRenderState {
    SceneRenderLayer bgLayer;
    int bgTransitionEffect = 0;
    int bgDissolveCellSizePx = 28;
    std::vector<SceneSpriteRenderCommand> sprites;
    SceneCameraState camera;
    SDL_FPoint cameraOffset{ 0.0f, 0.0f };
    float letterboxRatio = 0.0f;
    SDL_Color screenFadeColor{ 0, 0, 0, 0 };
    float screenFadeAlpha = 0.0f;
    float parallaxNormX = 0.0f;
    float parallaxNormY = 0.0f;
    bool enableParallaxEffect = true;
    int screenW = 1280;
    int screenH = 720;
};

enum class RenderCommandType {
    Texture,
    FillRect,
    Geometry
};

struct RenderCommand {
    RenderCommandType type = RenderCommandType::Texture;
    float zIndex = 0.0f;
    SDL_Texture* texture = nullptr;
    std::optional<SDL_FRect> srcRect;
    SDL_FRect dstRect{ 0.0f, 0.0f, 0.0f, 0.0f };
    SDL_Color colorMod{ 255, 255, 255, 255 };
    float alpha = 1.0f;
    float rotationDeg = 0.0f;
    SDL_FPoint pivot{ 0.5f, 0.5f };
    std::optional<SDL_Rect> clipRect;
    SDL_Color fillColor{ 0, 0, 0, 0 };
    std::vector<SDL_Vertex> vertices;
    std::vector<int> indices;
};

class RenderCommandQueue {
public:
    void Clear() {
        m_commands.clear();
    }

    [[nodiscard]] bool Empty() const noexcept {
        return m_commands.empty();
    }

    void Submit(RenderCommand command) {
        m_commands.push_back(std::move(command));
    }

    void SubmitTexture(float zIndex,
                       SDL_Texture* texture,
                       const SDL_FRect& dst,
                       const SDL_Color& colorMod,
                       float alpha,
                       float rotationDeg = 0.0f,
                       SDL_FPoint pivot = SDL_FPoint{ 0.5f, 0.5f },
                       std::optional<SDL_Rect> clipRect = std::nullopt,
                       std::optional<SDL_FRect> srcRect = std::nullopt) {
        if (!texture || alpha <= 0.0f || dst.w <= 0.0f || dst.h <= 0.0f) {
            return;
        }

        RenderCommand command;
        command.type = RenderCommandType::Texture;
        command.zIndex = zIndex;
        command.texture = texture;
        command.srcRect = srcRect;
        command.dstRect = dst;
        command.colorMod = colorMod;
        command.alpha = alpha;
        command.rotationDeg = rotationDeg;
        command.pivot = pivot;
        command.clipRect = clipRect;
        Submit(std::move(command));
    }

    void SubmitFillRect(float zIndex, const SDL_FRect& rect, SDL_Color color) {
        if (color.a == 0 || rect.w <= 0.0f || rect.h <= 0.0f) {
            return;
        }

        RenderCommand command;
        command.type = RenderCommandType::FillRect;
        command.zIndex = zIndex;
        command.dstRect = rect;
        command.fillColor = color;
        Submit(std::move(command));
    }

    void SubmitGeometry(float zIndex,
                        SDL_Texture* texture,
                        std::vector<SDL_Vertex> vertices,
                        std::vector<int> indices) {
        if (!texture || vertices.empty() || indices.empty()) {
            return;
        }

        RenderCommand command;
        command.type = RenderCommandType::Geometry;
        command.zIndex = zIndex;
        command.texture = texture;
        command.vertices = std::move(vertices);
        command.indices = std::move(indices);
        Submit(std::move(command));
    }

    void Execute(SDL_Renderer* renderer) {
        if (!renderer || m_commands.empty()) {
            return;
        }

        std::stable_sort(m_commands.begin(), m_commands.end(), [](const RenderCommand& lhs, const RenderCommand& rhs) {
            return lhs.zIndex < rhs.zIndex;
        });

        for (const RenderCommand& command : m_commands) {
            switch (command.type) {
            case RenderCommandType::Texture:
                ExecuteTexture(renderer, command);
                break;
            case RenderCommandType::FillRect:
                ExecuteFillRect(renderer, command);
                break;
            case RenderCommandType::Geometry:
                ExecuteGeometry(renderer, command);
                break;
            }
        }

        SDL_SetRenderClipRect(renderer, nullptr);
        Clear();
    }

private:
    static Uint8 UnitAlpha(float value) {
        return static_cast<Uint8>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
    }

    static void ExecuteTexture(SDL_Renderer* renderer, const RenderCommand& command) {
        if (!command.texture || command.alpha <= 0.0f) {
            return;
        }

        if (command.clipRect.has_value()) {
            SDL_SetRenderClipRect(renderer, &command.clipRect.value());
        } else {
            SDL_SetRenderClipRect(renderer, nullptr);
        }

        SDL_SetTextureColorMod(command.texture, command.colorMod.r, command.colorMod.g, command.colorMod.b);
        const float modulatedAlpha = command.alpha * (static_cast<float>(command.colorMod.a) / 255.0f);
        SDL_SetTextureAlphaMod(command.texture, UnitAlpha(modulatedAlpha));

        const SDL_FRect* src = command.srcRect.has_value() ? &command.srcRect.value() : nullptr;
        const SDL_FPoint pivotPx{ command.dstRect.w * command.pivot.x, command.dstRect.h * command.pivot.y };
        SDL_RenderTextureRotated(renderer,
                                 command.texture,
                                 src,
                                 &command.dstRect,
                                 command.rotationDeg,
                                 &pivotPx,
                                 SDL_FLIP_NONE);

        SDL_SetTextureColorMod(command.texture, 255, 255, 255);
        SDL_SetTextureAlphaMod(command.texture, 255);
        SDL_SetRenderClipRect(renderer, nullptr);
    }

    static void ExecuteFillRect(SDL_Renderer* renderer, const RenderCommand& command) {
        SDL_SetRenderClipRect(renderer, nullptr);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer,
                               command.fillColor.r,
                               command.fillColor.g,
                               command.fillColor.b,
                               command.fillColor.a);
        SDL_RenderFillRect(renderer, &command.dstRect);
    }

    static void ExecuteGeometry(SDL_Renderer* renderer, const RenderCommand& command) {
        if (!command.texture || command.vertices.empty() || command.indices.empty()) {
            return;
        }

        SDL_SetRenderClipRect(renderer, nullptr);
        SDL_SetTextureColorMod(command.texture, 255, 255, 255);
        SDL_SetTextureAlphaMod(command.texture, 255);
        SDL_RenderGeometry(renderer,
                           command.texture,
                           command.vertices.data(),
                           static_cast<int>(command.vertices.size()),
                           command.indices.data(),
                           static_cast<int>(command.indices.size()));
    }

    std::vector<RenderCommand> m_commands;
};

class RenderPipeline {
public:
    void RenderScene(SDL_Renderer* renderer,
                     const SceneRenderState& state,
                     bool enableParallax) const {
        if (!renderer) {
            return;
        }

        const SDL_FPoint shake = state.cameraOffset;
        const float zoom = state.camera.zoom;

        SDL_FRect bgRect = { 0.0f, 0.0f, static_cast<float>(state.screenW), static_cast<float>(state.screenH) };
        if (SDL_Texture* bgTex = ActiveTexture(state.bgLayer)) {
            float tw = 0.0f;
            float th = 0.0f;
            SDL_GetTextureSize(bgTex, &tw, &th);
            bgRect = AspectFillRect(tw, th, 0.0f, 0.0f, static_cast<float>(state.screenW), static_cast<float>(state.screenH));
        }

        bgRect.w *= zoom;
        bgRect.h *= zoom;
        bgRect.x = (state.screenW - bgRect.w) * 0.5f - state.camera.x * zoom;
        bgRect.y = (state.screenH - bgRect.h) * 0.5f - state.camera.y * zoom;

        if (enableParallax && state.enableParallaxEffect) {
            bgRect.x += state.parallaxNormX * -20.0f + shake.x;
            bgRect.y += state.parallaxNormY * -10.0f + shake.y;
        } else {
            bgRect.x += shake.x;
            bgRect.y += shake.y;
        }

        SDL_FRect bgDrawRect = { bgRect.x - 30.0f, bgRect.y - 30.0f, bgRect.w + 60.0f, bgRect.h + 60.0f };
        RenderCommandQueue queue;
        SubmitBackgroundLayer(queue, state, bgDrawRect, kZBackground);

        for (const auto& sprite : state.sprites) {
            if (!sprite.screenRectValid) {
                continue;
            }

            const std::optional<SDL_Rect> clipRect = sprite.clipEnabled
                ? std::optional<SDL_Rect>{ TransformClipRectToScreen(sprite.clipRect,
                                                                     state.parallaxNormX,
                                                                     state.parallaxNormY,
                                                                     shake,
                                                                     zoom,
                                                                     state.screenW,
                                                                     state.screenH,
                                                                     state.enableParallaxEffect) }
                : std::nullopt;

            SubmitSprite(queue,
                         kZSpriteBase + static_cast<float>(sprite.slot),
                         sprite,
                         sprite.screenRect,
                         clipRect,
                         state.enableParallaxEffect);
        }

        SubmitLetterbox(queue, state.screenW, state.screenH, state.letterboxRatio, kZLetterbox);

        if (state.screenFadeAlpha > 0.001f) {
            SDL_Color fadeColor = state.screenFadeColor;
            fadeColor.a = static_cast<Uint8>(std::clamp(state.screenFadeAlpha, 0.0f, 1.0f) * 255.0f);
            SDL_FRect screen = { 0.0f, 0.0f, static_cast<float>(state.screenW), static_cast<float>(state.screenH) };
            queue.SubmitFillRect(kZScreenFade, screen, fadeColor);
        }

        queue.Execute(renderer);
    }

private:
    static constexpr float kZBackground = 0.0f;
    static constexpr float kZSpriteBase = 100.0f;
    static constexpr float kZLetterbox = 10000.0f;
    static constexpr float kZScreenFade = 11000.0f;

    [[nodiscard]] static SDL_Texture* ActiveTexture(const SceneRenderLayer& layer) {
        if (layer.current) return layer.current;
        return layer.previous;
    }

    [[nodiscard]] static SDL_Rect ClipToIntRect(const SDL_FRect& rect) {
        SDL_Rect out{};
        out.x = static_cast<int>(std::lround(rect.x));
        out.y = static_cast<int>(std::lround(rect.y));
        out.w = static_cast<int>(std::lround(rect.w));
        out.h = static_cast<int>(std::lround(rect.h));
        return out;
    }

    [[nodiscard]] static SDL_FRect AspectFitRect(float srcW, float srcH, float boxX, float boxY, float boxW, float boxH) {
        if (srcW <= 0.0f || srcH <= 0.0f || boxW <= 0.0f || boxH <= 0.0f) {
            return { boxX, boxY, boxW, boxH };
        }
        const float scale = std::min(boxW / srcW, boxH / srcH);
        const float w = srcW * scale;
        const float h = srcH * scale;
        return { boxX + (boxW - w) * 0.5f, boxY + (boxH - h) * 0.5f, w, h };
    }

    [[nodiscard]] static SDL_FRect AspectFillRect(float srcW, float srcH, float boxX, float boxY, float boxW, float boxH) {
        if (srcW <= 0.0f || srcH <= 0.0f || boxW <= 0.0f || boxH <= 0.0f) {
            return { boxX, boxY, boxW, boxH };
        }
        const float scale = std::max(boxW / srcW, boxH / srcH);
        const float w = srcW * scale;
        const float h = srcH * scale;
        return { boxX + (boxW - w) * 0.5f, boxY + (boxH - h) * 0.5f, w, h };
    }

    [[nodiscard]] static Uint32 TileHash(int x, int y) {
        Uint32 value = 2166136261u;
        value = (value ^ static_cast<Uint32>(x * 374761393)) * 16777619u;
        value = (value ^ static_cast<Uint32>(y * 668265263)) * 16777619u;
        return value;
    }

    [[nodiscard]] static SDL_Rect TransformClipRectToScreen(const SDL_FRect& clipRect,
                                                            float parallaxNormX,
                                                            float parallaxNormY,
                                                            const SDL_FPoint& shake,
                                                            float zoom,
                                                            int screenW,
                                                            int screenH,
                                                            bool enableParallax) {
        SDL_FRect transformed = clipRect;
        transformed.w *= zoom;
        transformed.h *= zoom;
        transformed.x = (screenW / 2.0f) + (transformed.x - screenW / 2.0f) * zoom;
        transformed.y = (screenH / 2.0f) + (transformed.y - screenH / 2.0f) * zoom;

        if (enableParallax) {
            transformed.x += parallaxNormX * 15.0f + shake.x;
            transformed.y += parallaxNormY * 5.0f + shake.y;
        } else {
            transformed.x += shake.x;
            transformed.y += shake.y;
        }

        return ClipToIntRect(transformed);
    }

    static void SubmitTexture(RenderCommandQueue& queue,
                              float zIndex,
                              SDL_Texture* texture,
                              const SDL_FRect& dst,
                              const SDL_Color& tint,
                              float alpha,
                              float rotationDeg,
                              const SDL_FPoint& pivot,
                              const std::optional<SDL_Rect>& clipRect) {
        queue.SubmitTexture(zIndex, texture, dst, tint, alpha, rotationDeg, pivot, clipRect);
    }

    static void SubmitTextureMappedClip(RenderCommandQueue& queue,
                                        float zIndex,
                                        SDL_Texture* texture,
                                        const SDL_FRect& dst,
                                        const SDL_Color& tint,
                                        float alpha,
                                        const SDL_Rect& clipRect) {
        if (!texture || alpha <= 0.0f) return;
        if (dst.w <= 0.0f || dst.h <= 0.0f || clipRect.w <= 0 || clipRect.h <= 0) return;

        const SDL_FRect clipF{
            static_cast<float>(clipRect.x),
            static_cast<float>(clipRect.y),
            static_cast<float>(clipRect.w),
            static_cast<float>(clipRect.h)
        };

        const float ix0 = std::max(dst.x, clipF.x);
        const float iy0 = std::max(dst.y, clipF.y);
        const float ix1 = std::min(dst.x + dst.w, clipF.x + clipF.w);
        const float iy1 = std::min(dst.y + dst.h, clipF.y + clipF.h);
        if (ix1 <= ix0 || iy1 <= iy0) {
            return;
        }

        float texW = 0.0f;
        float texH = 0.0f;
        if (!SDL_GetTextureSize(texture, &texW, &texH) || texW <= 0.0f || texH <= 0.0f) {
            return;
        }

        const float u0 = std::clamp((ix0 - dst.x) / dst.w, 0.0f, 1.0f);
        const float v0 = std::clamp((iy0 - dst.y) / dst.h, 0.0f, 1.0f);
        const float u1 = std::clamp((ix1 - dst.x) / dst.w, 0.0f, 1.0f);
        const float v1 = std::clamp((iy1 - dst.y) / dst.h, 0.0f, 1.0f);
        if (u1 <= u0 || v1 <= v0) {
            return;
        }

        const SDL_FRect src{
            u0 * texW,
            v0 * texH,
            (u1 - u0) * texW,
            (v1 - v0) * texH
        };
        const SDL_FRect dstPart{ ix0, iy0, ix1 - ix0, iy1 - iy0 };

        queue.SubmitTexture(zIndex,
                            texture,
                            dstPart,
                            tint,
                            alpha,
                            0.0f,
                            SDL_FPoint{ 0.5f, 0.5f },
                            std::nullopt,
                            src);
    }

    static void SubmitSprite(RenderCommandQueue& queue,
                             float zIndex,
                             const SceneSpriteRenderCommand& sprite,
                             const SDL_FRect& dst,
                             const std::optional<SDL_Rect>& clipRect,
                             bool enableParallax) {
        if (sprite.previous) {
            SubmitTexture(queue,
                          zIndex,
                          sprite.previous,
                          dst,
                          sprite.tintColor,
                          sprite.opacity * (1.0f - sprite.crossfadeMix),
                          sprite.rotationDeg,
                          sprite.anchor,
                          clipRect);
        }
        if (sprite.current) {
            const float alpha = sprite.previous ? sprite.crossfadeMix : 1.0f;
            SubmitTexture(queue,
                          zIndex,
                          sprite.current,
                          dst,
                          sprite.tintColor,
                          sprite.opacity * alpha,
                          sprite.rotationDeg,
                          sprite.anchor,
                          clipRect);
        }
        (void)enableParallax;
    }

    static void SubmitBackgroundLayer(RenderCommandQueue& queue,
                                      const SceneRenderState& state,
                                      const SDL_FRect& dst,
                                      float zIndex) {
        const SDL_Color tint{ 255, 255, 255, 255 };
        constexpr SDL_FPoint pivot{ 0.5f, 0.5f };

        if (!state.bgLayer.previous || !state.bgLayer.current) {
            SubmitTexture(queue, zIndex, ActiveTexture(state.bgLayer), dst, tint, 1.0f, 0.0f, pivot, std::nullopt);
            return;
        }

        const float mix = std::clamp(state.bgLayer.crossfadeMix, 0.0f, 1.0f);
        SDL_Texture* previous = state.bgLayer.previous;
        SDL_Texture* current = state.bgLayer.current;
        if (!previous || !current) {
            SubmitTexture(queue, zIndex, ActiveTexture(state.bgLayer), dst, tint, 1.0f, 0.0f, pivot, std::nullopt);
            return;
        }

        switch (state.bgTransitionEffect) {
        case 2:
        case 3:
        case 4:
        case 5: {
            SubmitTexture(queue, zIndex, previous, dst, tint, 1.0f, 0.0f, pivot, std::nullopt);
            if (mix > 0.0f) {
                SDL_Rect clip{};
                if (state.bgTransitionEffect == 2 || state.bgTransitionEffect == 3) {
                    const float revealW = static_cast<float>(state.screenW) * mix;
                    clip.y = 0;
                    clip.h = state.screenH;
                    clip.w = std::max(0, static_cast<int>(std::lround(revealW)));
                    clip.x = (state.bgTransitionEffect == 2) ? 0 : std::max(0, state.screenW - clip.w);
                } else {
                    const float revealH = static_cast<float>(state.screenH) * mix;
                    clip.x = 0;
                    clip.w = state.screenW;
                    clip.h = std::max(0, static_cast<int>(std::lround(revealH)));
                    clip.y = (state.bgTransitionEffect == 5) ? 0 : std::max(0, state.screenH - clip.h);
                }
                if (clip.w > 0 && clip.h > 0) {
                    SubmitTextureMappedClip(queue, zIndex, current, dst, tint, 1.0f, clip);
                }
            }
            break;
        }
        case 6: {
            SubmitTexture(queue, zIndex, previous, dst, tint, 1.0f, 0.0f, pivot, std::nullopt);
            if (mix > 0.0f) {
                const int blindCount = 12;
                const float blindH = static_cast<float>(state.screenH) / blindCount;
                const float revealH = blindH * mix;

                for (int i = 0; i < blindCount; ++i) {
                    SDL_Rect clip{};
                    clip.x = 0;
                    clip.w = state.screenW;
                    clip.y = static_cast<int>(std::lround(i * blindH));
                    clip.h = static_cast<int>(std::lround(revealH));
                    if (clip.h > 0) {
                        SubmitTextureMappedClip(queue, zIndex, current, dst, tint, 1.0f, clip);
                    }
                }
            }
            break;
        }
        case 1: {
            SubmitTexture(queue, zIndex, previous, dst, tint, 1.0f, 0.0f, pivot, std::nullopt);
            if (mix <= 0.0f) break;

            const int cell = std::max(8, state.bgDissolveCellSizePx);
            const int overscan = 64;
            std::vector<SDL_Vertex> vertices;
            std::vector<int> indices;

            const int drawW = state.screenW + overscan * 2;
            const int drawH = state.screenH + overscan * 2;
            const int cols = (drawW + cell - 1) / cell;
            const int rows = (drawH + cell - 1) / cell;
            vertices.reserve(cols * rows * 4);
            indices.reserve(cols * rows * 6);

            SDL_FColor fcol = { tint.r / 255.0f, tint.g / 255.0f, tint.b / 255.0f, 1.0f };

            for (int y = -overscan; y < state.screenH + overscan; y += cell) {
                for (int x = -overscan; x < state.screenW + overscan; x += cell) {
                    const Uint32 hash = TileHash(std::abs(x / cell), std::abs(y / cell));
                    const float normalized = static_cast<float>(hash & 0xFFFFu) / 65535.0f;
                    if (normalized > mix) continue;

                    float cx = static_cast<float>(x);
                    float cy = static_cast<float>(y);
                    float cw = static_cast<float>(std::min(cell, state.screenW - x));
                    float ch = static_cast<float>(std::min(cell, state.screenH - y));

                    float ix0 = std::max(dst.x, cx);
                    float iy0 = std::max(dst.y, cy);
                    float ix1 = std::min(dst.x + dst.w, cx + cw);
                    float iy1 = std::min(dst.y + dst.h, cy + ch);

                    if (ix1 <= ix0 || iy1 <= iy0) continue;

                    float u0 = (ix0 - dst.x) / dst.w;
                    float v0 = (iy0 - dst.y) / dst.h;
                    float u1 = (ix1 - dst.x) / dst.w;
                    float v1 = (iy1 - dst.y) / dst.h;

                    int baseIdx = static_cast<int>(vertices.size());
                    vertices.push_back({ {ix0, iy0}, fcol, {u0, v0} });
                    vertices.push_back({ {ix1, iy0}, fcol, {u1, v0} });
                    vertices.push_back({ {ix1, iy1}, fcol, {u1, v1} });
                    vertices.push_back({ {ix0, iy1}, fcol, {u0, v1} });

                    indices.push_back(baseIdx + 0);
                    indices.push_back(baseIdx + 1);
                    indices.push_back(baseIdx + 2);
                    indices.push_back(baseIdx + 0);
                    indices.push_back(baseIdx + 2);
                    indices.push_back(baseIdx + 3);
                }
            }

            if (!vertices.empty()) {
                queue.SubmitGeometry(zIndex, current, std::move(vertices), std::move(indices));
            }
            break;
        }
        case 0:
        default:
            SubmitTexture(queue, zIndex, previous, dst, tint, 1.0f, 0.0f, pivot, std::nullopt);
            if (mix > 0.0f) {
                SubmitTexture(queue, zIndex, current, dst, tint, mix, 0.0f, pivot, std::nullopt);
            }
            break;
        }
    }

    static void SubmitLetterbox(RenderCommandQueue& queue, int screenW, int screenH, float ratio, float zIndex) {
        if (ratio <= 0.0f) return;

        const float barHeight = static_cast<float>(screenH) * std::clamp(ratio, 0.0f, 0.45f);
        if (barHeight <= 0.5f) return;

        const SDL_FRect top{ 0.0f, 0.0f, static_cast<float>(screenW), barHeight };
        const SDL_FRect bottom{ 0.0f, static_cast<float>(screenH) - barHeight, static_cast<float>(screenW), barHeight };
        queue.SubmitFillRect(zIndex, top, SDL_Color{ 0, 0, 0, 245 });
        queue.SubmitFillRect(zIndex, bottom, SDL_Color{ 0, 0, 0, 245 });
    }
};

} // namespace vn
