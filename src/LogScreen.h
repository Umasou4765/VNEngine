#pragma once
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <cmath>
#include <string>
#include "DialogueBox.h"
#include "AssetManager.h"
#include "Input.h"
#include "Tween.h"
#include "UITheme.h"

class LogScreen {
public:
    bool visible = false;

    void Init(TTF_Font* font,
              int w,
              int h,
              vn::AssetManager* assets = nullptr,
              const std::string& fontPath = "",
              int fontPointSize = 22) {
        m_font = font;
        m_W = w;
        m_H = h;
        m_assets = assets;
        m_fontPath = fontPath;
        m_fontPointSize = fontPointSize;
    }

    void Show() {
        visible = true;
        m_scrollY = 0.0f;
        m_targetScrollY = 0.0f;
        m_scrollVelocity = 0.0f;
        m_contentHeight = 0.0f;
        m_alphaVal = 0.0f;
        m_alpha.Start(&m_alphaVal, 0.0f, 1.0f, 0.2f, Ease::OutQuad);
    }

    void Hide() {
        visible = false;
    }

    void ReleaseResources() {
        Hide();
        m_font = nullptr;
        m_assets = nullptr;
    }

    void Update(float dt, Input& input, int scrollDelta, const DialogueBox& dlg) {
        (void)dlg;
        if (!visible) return;

        m_alpha.Update(dt);

        const LogLayout layout = GetLayout();
        const float maxScrollY = MaxScrollY(layout);

        if (scrollDelta != 0) {
            m_targetScrollY += scrollDelta * m_scrollWheelImpulse;
        }

        if (input.IsActionJustPressed(Input::Action::FocusUp)) {
            m_targetScrollY += m_keyboardScrollStep;
        }
        if (input.IsActionJustPressed(Input::Action::FocusDown)) {
            m_targetScrollY -= m_keyboardScrollStep;
        }
        if (input.JustPressed(SDL_SCANCODE_PAGEUP)) {
            m_targetScrollY += layout.viewportH * m_pageScrollRatio;
        }
        if (input.JustPressed(SDL_SCANCODE_PAGEDOWN)) {
            m_targetScrollY -= layout.viewportH * m_pageScrollRatio;
        }
        if (input.JustPressed(SDL_SCANCODE_HOME)) {
            m_targetScrollY = maxScrollY;
            m_scrollVelocity = 0.0f;
        }
        if (input.JustPressed(SDL_SCANCODE_END)) {
            m_targetScrollY = 0.0f;
            m_scrollVelocity = 0.0f;
        }

        m_targetScrollY = std::clamp(m_targetScrollY, -m_scrollOverscroll, maxScrollY + m_scrollOverscroll);

        const float clampedTarget = std::clamp(m_targetScrollY, 0.0f, maxScrollY);
        const float displacement = clampedTarget - m_targetScrollY;

        m_scrollVelocity += displacement * m_scrollSpringTension * std::max(0.0f, dt);
        m_scrollVelocity *= std::exp(-m_scrollSpringFriction * std::max(0.0f, dt));
        if (std::fabs(m_scrollVelocity) < 0.01f) {
            m_scrollVelocity = 0.0f;
        }

        m_targetScrollY += m_scrollVelocity * dt;
        m_targetScrollY = std::clamp(m_targetScrollY, -m_scrollOverscroll, maxScrollY + m_scrollOverscroll);
        m_scrollY += (m_targetScrollY - m_scrollY) * std::min(1.0f, m_scrollFollowSpeed * std::max(0.0f, dt));
        m_scrollY = std::clamp(m_scrollY, -m_scrollOverscroll, maxScrollY + m_scrollOverscroll);

        if (input.JustRightClicked() ||
            input.IsActionJustPressed(Input::Action::OpenSystemMenu) ||
            input.JustPressed(SDL_SCANCODE_RSHIFT)) {
            Hide();
            input.ConsumeRightClick();
        }
    }

    void Render(SDL_Renderer* renderer, const DialogueBox& dlg) {
        if (!visible || !m_font || !renderer) return;

        const Uint8 a = static_cast<Uint8>(m_alphaVal * 255);

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer,
                               ui::theme::kBlackBase.r,
                               ui::theme::kBlackBase.g,
                               ui::theme::kBlackBase.b,
                               static_cast<Uint8>(a * 0.92f));
        SDL_FRect bg = { 0.0f, 0.0f, static_cast<float>(m_W), static_cast<float>(m_H) };
        SDL_RenderFillRect(renderer, &bg);

        const LogLayout layout = GetLayout();
        RenderText(renderer,
                   "HISTORY LOG",
                   layout.titleX,
                   layout.titleY,
                   ui::theme::WithAlpha(ui::theme::kAccentSoft, a));

        const auto& log = dlg.GetLog();
        float cursorY = layout.viewportY + layout.viewportH - layout.bottomPadding + m_scrollY;
        m_contentHeight = layout.topPadding + layout.bottomPadding;

        const SDL_Rect clip = ToClipRect(layout);
        SDL_SetRenderClipRect(renderer, &clip);

        for (auto it = log.rbegin(); it != log.rend(); ++it) {
            const float textH = MeasureTextWrapped(renderer, it->text, layout.textW);
            const float nameH = it->speaker.empty()
                ? 0.0f
                : MeasureTextWrapped(renderer, it->speaker, layout.nameW);
            const float rowH = std::max(MinEntryHeight(), std::max(textH, nameH));
            const float blockHeight = rowH + layout.entrySpacing;
            cursorY -= blockHeight;
            m_contentHeight += blockHeight;

            if (cursorY > layout.ViewportBottom() + layout.cullMargin ||
                cursorY + rowH < layout.viewportY - layout.cullMargin) {
                continue;
            }

            SDL_Color txtCol = ui::theme::WithAlpha(ui::theme::kTextPrimary, a);
            RenderTextWrapped(renderer, it->text, layout.textX, cursorY, layout.textW, txtCol);

            if (!it->speaker.empty()) {
                SDL_Color nameCol = ui::theme::WithAlpha(ui::theme::kBluePrimary, a);
                RenderTextWrapped(renderer, it->speaker, layout.nameX, cursorY, layout.nameW, nameCol);
            }
        }

        SDL_SetRenderClipRect(renderer, nullptr);
        RenderScrollbar(renderer, layout, a);
    }

private:
    struct LogLayout {
        float titleX = 0.0f;
        float titleY = 30.0f;
        float viewportX = 0.0f;
        float viewportY = 88.0f;
        float viewportW = 0.0f;
        float viewportH = 0.0f;
        float nameX = 0.0f;
        float nameW = 0.0f;
        float textX = 0.0f;
        float textW = 0.0f;
        float topPadding = 18.0f;
        float bottomPadding = 20.0f;
        float entrySpacing = 28.0f;
        float cullMargin = 80.0f;
        float scrollbarX = 0.0f;
        float scrollbarW = 3.0f;

        [[nodiscard]] float ViewportBottom() const noexcept {
            return viewportY + viewportH;
        }
    };

    [[nodiscard]] LogLayout GetLayout() const {
        LogLayout layout;
        const float screenW = static_cast<float>(std::max(1, m_W));
        const float screenH = static_cast<float>(std::max(1, m_H));

        const float sideMargin = std::max(48.0f, screenW * 0.05f);
        const float rightMargin = std::max(64.0f, screenW * 0.08f);
        const float columnGap = std::max(36.0f, screenW * 0.035f);

        layout.titleX = sideMargin;
        layout.viewportX = sideMargin;
        layout.viewportY = std::clamp(screenH * 0.12f, 84.0f, 112.0f);
        layout.viewportW = std::max(1.0f, screenW - sideMargin - rightMargin);
        layout.viewportH = std::max(1.0f, screenH - layout.viewportY - 58.0f);
        layout.nameX = sideMargin;
        layout.nameW = std::clamp(screenW * 0.13f, 120.0f, 190.0f);
        layout.textX = layout.nameX + layout.nameW + columnGap;
        layout.textW = std::max(1.0f, layout.viewportX + layout.viewportW - layout.textX - 22.0f);
        layout.scrollbarX = layout.viewportX + layout.viewportW - layout.scrollbarW;
        return layout;
    }

    [[nodiscard]] float MaxScrollY(const LogLayout& layout) const noexcept {
        return std::max(0.0f, m_contentHeight - layout.viewportH);
    }

    [[nodiscard]] float MinEntryHeight() const noexcept {
        return m_font ? static_cast<float>(std::max(1, TTF_GetFontLineSkip(m_font))) : 24.0f;
    }

    [[nodiscard]] static SDL_Rect ToClipRect(const LogLayout& layout) {
        const int x = static_cast<int>(std::floor(layout.viewportX));
        const int y = static_cast<int>(std::floor(layout.viewportY));
        const int w = static_cast<int>(std::ceil(layout.viewportW));
        const int h = static_cast<int>(std::ceil(layout.viewportH));
        return SDL_Rect{ x, y, std::max(1, w), std::max(1, h) };
    }

    void RenderScrollbar(SDL_Renderer* renderer, const LogLayout& layout, Uint8 alpha) const {
        const float maxScrollY = MaxScrollY(layout);
        if (!renderer || maxScrollY <= 1.0f || m_contentHeight <= 1.0f) {
            return;
        }

        const float trackH = std::max(1.0f, layout.viewportH);
        const float thumbH = std::clamp((layout.viewportH / m_contentHeight) * trackH, 36.0f, trackH);
        const float scrollT = std::clamp(m_scrollY / maxScrollY, 0.0f, 1.0f);
        const float thumbY = layout.viewportY + (trackH - thumbH) * (1.0f - scrollT);

        const SDL_FRect track{
            layout.scrollbarX,
            layout.viewportY,
            layout.scrollbarW,
            trackH
        };
        const SDL_FRect thumb{
            layout.scrollbarX,
            thumbY,
            layout.scrollbarW,
            thumbH
        };

        SDL_Color trackColor = ui::theme::WithAlpha(ui::theme::kGrayGloom, ui::theme::ScaleAlpha(alpha, 0.35f));
        SDL_Color thumbColor = ui::theme::WithAlpha(ui::theme::kAccentSoft, ui::theme::ScaleAlpha(alpha, 0.78f));

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, trackColor.r, trackColor.g, trackColor.b, trackColor.a);
        SDL_RenderFillRect(renderer, &track);
        SDL_SetRenderDrawColor(renderer, thumbColor.r, thumbColor.g, thumbColor.b, thumbColor.a);
        SDL_RenderFillRect(renderer, &thumb);
    }

    void RenderText(SDL_Renderer* renderer, const std::string& text, float x, float y, SDL_Color color) {
        (void)RenderTextCached(renderer, text, x, y, color, 0.0f, true);
    }

    float RenderTextWrapped(SDL_Renderer* renderer,
                            const std::string& text,
                            float x,
                            float y,
                            float maxW,
                            SDL_Color color) {
        return RenderTextCached(renderer, text, x, y, color, maxW, true);
    }

    float MeasureTextWrapped(SDL_Renderer* renderer,
                             const std::string& text,
                             float maxW) {
        return RenderTextCached(renderer, text, 0.0f, 0.0f, SDL_Color{ 255, 255, 255, 0 }, maxW, false);
    }

    float RenderTextCached(SDL_Renderer* renderer,
                           const std::string& text,
                           float x,
                           float y,
                           SDL_Color color,
                           float wrapWidth,
                           bool draw) {
        if (!m_font || !renderer || text.empty()) return 0.0f;

        if (m_assets && !m_fontPath.empty()) {
            auto tex = m_assets->GetTextTexture(
                text,
                m_font,
                m_fontPath,
                m_fontPointSize,
                SDL_Color{ 255, 255, 255, 255 },
                1,
                wrapWidth);
            if (!tex) return 0.0f;

            float w = 0.0f;
            float h = 0.0f;
            if (!SDL_GetTextureSize(tex.get(), &w, &h)) {
                return 0.0f;
            }

            if (draw) {
                SDL_SetTextureColorMod(tex.get(), color.r, color.g, color.b);
                SDL_SetTextureAlphaMod(tex.get(), color.a);
                SDL_FRect dst = { x, y, w, h };
                SDL_RenderTexture(renderer, tex.get(), nullptr, &dst);
                SDL_SetTextureColorMod(tex.get(), 255, 255, 255);
                SDL_SetTextureAlphaMod(tex.get(), 255);
            }
            return h;
        }

        SDL_Surface* surf = nullptr;
        if (wrapWidth > 0.0f) {
            surf = TTF_RenderText_Blended_Wrapped(
                m_font,
                text.c_str(),
                0,
                SDL_Color{ 255, 255, 255, 255 },
                wrapWidth
            );
        } else {
            surf = TTF_RenderText_Blended(m_font, text.c_str(), 0, SDL_Color{ 255, 255, 255, 255 });
        }

        if (!surf) return 0.0f;

        const float w = static_cast<float>(surf->w);
        const float h = static_cast<float>(surf->h);
        SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
        SDL_DestroySurface(surf);
        if (!tex) return 0.0f;

        if (draw) {
            SDL_SetTextureColorMod(tex, color.r, color.g, color.b);
            SDL_SetTextureAlphaMod(tex, color.a);
            SDL_FRect dst = { x, y, w, h };
            SDL_RenderTexture(renderer, tex, nullptr, &dst);
        }
        SDL_DestroyTexture(tex);
        return h;
    }

    TTF_Font* m_font = nullptr;
    vn::AssetManager* m_assets = nullptr;
    std::string m_fontPath;
    int m_fontPointSize = 22;
    int m_W = 0;
    int m_H = 0;
    float m_scrollY = 0.0f;
    float m_targetScrollY = 0.0f;
    float m_scrollVelocity = 0.0f;
    float m_contentHeight = 0.0f;
    float m_scrollWheelImpulse = 120.0f;
    float m_keyboardScrollStep = 72.0f;
    float m_pageScrollRatio = 0.82f;
    float m_scrollOverscroll = 120.0f;
    float m_scrollSpringTension = 36.0f;
    float m_scrollSpringFriction = 9.0f;
    float m_scrollFollowSpeed = 15.0f;
    float m_alphaVal = 0.0f;
    Tween<float> m_alpha;
};
