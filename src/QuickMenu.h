#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "AssetManager.h"
#include "Input.h"
#include "RendererUtils.h"
#include "Tween.h"
#include "UITheme.h"

class QuickMenu {
public:
    enum Action { NONE, HISTORY, SKIP, AUTO, SAVE, LOAD, PREFS, QSAVE, QLOAD, HIDE_UI };

    void Init(TTF_Font* font,
              int screenW,
              int screenH,
              vn::AssetManager* assets = nullptr,
              const std::string& fontPath = "",
              int fontPointSize = 22) {
        m_font = font;
        m_W = screenW;
        m_H = screenH;
        m_assets = assets;
        m_fontPath = fontPath;
        m_fontPointSize = fontPointSize;

        ApplyTextStyleFromLayout();
        ResetVisibilityImmediate(false);
        RebuildButtons();
    }

    void SetTranslator(std::function<std::string(const std::string&)> translator) {
        m_translate = std::move(translator);
        RebuildButtons();
    }

    [[nodiscard]] int GetHoveredIndex() const noexcept {
        return m_hoveredIdx;
    }

    [[nodiscard]] int GetFocusedIndex() const noexcept {
        return m_focusedIdx;
    }

    [[nodiscard]] bool IsRendering() const noexcept {
        return m_state != VisibilityState::Hidden || m_alpha > 0.01f;
    }

    void RebuildButtons() {
        m_buttons.clear();

        const float boxW = static_cast<float>(m_W) - m_layout.dialogBoxOuterMarginX * 2.0f;
        const float boxX = m_layout.dialogBoxOuterMarginX;
        const float boxH = m_layout.dialogBoxHeight;
        const float boxY = static_cast<float>(m_H) - boxH - m_layout.dialogBoxOuterBottom;

        const std::vector<std::pair<std::string, Action>> items = {
            { Tr("ui_log", "Log"), HISTORY },
            { Tr("ui_skip", "Skip"), SKIP },
            { Tr("ui_auto", "Auto"), AUTO },
            { Tr("ui_qsave", "Q.Save"), QSAVE },
            { Tr("ui_qload", "Q.Load"), QLOAD },
            { Tr("ui_save", "Save"), SAVE },
            { Tr("ui_load", "Load"), LOAD },
            { Tr("ui_config", "Config"), PREFS },
            { Tr("ui_hide", "Hide"), HIDE_UI }
        };

        const float gap = m_layout.quickButtonGap;
        const float buttonW = m_layout.quickButtonWidth;
        const float totalW = static_cast<float>(items.size()) * buttonW +
            static_cast<float>(items.empty() ? 0 : items.size() - 1) * gap;

        float currentX = boxX + boxW - totalW - m_layout.quickPanelRightPadding;
        const float startY = boxY + boxH - m_layout.quickPanelBottomPadding;

        m_buttons.reserve(items.size());
        for (const auto& item : items) {
            Button btn;
            btn.label = item.first;
            btn.action = item.second;
            btn.hitbox = {
                currentX - m_layout.quickHitPaddingX,
                startY - m_layout.quickHitPaddingTop,
                buttonW + m_layout.quickHitPaddingX * 2.0f,
                m_layout.quickHitHeight
            };
            btn.texture = GetCachedLabel(item.first);
            m_buttons.push_back(std::move(btn));
            currentX += buttonW + gap;
        }

        m_motion.assign(m_buttons.size(), ButtonMotion{});
        if (m_focusedIdx >= static_cast<int>(m_buttons.size())) {
            m_focusedIdx = -1;
        }
    }

    Action Update(float dt, Input& input, bool targetVisible) {
        return Update(dt, input, targetVisible, input.GetFocusInput(), false, false);
    }

    Action Update(float dt, Input& input, bool targetVisible, const Input::FocusInput& focus) {
        return Update(dt, input, targetVisible, focus, false, false);
    }

    Action Update(float dt,
                  Input& input,
                  bool targetVisible,
                  const Input::FocusInput& focus,
                  bool isAutoMode,
                  bool isSkipMode) {
        const float safeDt = std::max(0.0f, dt);
        SetTargetVisible(targetVisible);

        m_alphaTween.Update(safeDt);
        m_offsetTween.Update(safeDt);
        EnsureMotionSize();

        if (m_state == VisibilityState::Hidden) {
            ResetHotControls();
            UpdateButtonMotion(safeDt, -1, isAutoMode, isSkipMode);
            return NONE;
        }

        InteractionSample sample;
        if (AcceptsInput()) {
            sample = SampleInput(input, focus);
        } else {
            m_hoveredIdx = -1;
        }

        m_hoveredIdx = sample.hotIndex;
        Action triggered = NONE;
        if (sample.confirmed && sample.hotIndex >= 0) {
            triggered = Activate(sample.hotIndex, input, sample.fromMouse);
        }

        UpdateButtonMotion(safeDt, m_hoveredIdx, isAutoMode, isSkipMode);
        return triggered;
    }

    void Render(SDL_Renderer* renderer, bool isAutoMode, bool isSkipMode) {
        (void)isAutoMode;
        (void)isSkipMode;

        if (!renderer || !IsRendering()) {
            return;
        }

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        for (int i = 0; i < static_cast<int>(m_buttons.size()); ++i) {
            RenderButton(renderer, i);
        }
    }

    void ReleaseResources() {
        ResetVisibilityImmediate(false);
        ResetHotControls();
        m_buttons.clear();
        m_motion.clear();
        m_styleFontHandle.reset();
        m_font = nullptr;
        m_assets = nullptr;
    }

private:
    enum class VisibilityState {
        Hidden,
        Entering,
        Idle,
        Exiting
    };

    using TexturePtr = std::shared_ptr<SDL_Texture>;

    struct Button {
        std::string label;
        Action action = NONE;
        SDL_FRect hitbox{};
        TexturePtr texture;
    };

    struct ButtonMotion {
        float hoverT = 0.0f;
        float activeT = 0.0f;
        float pressT = 0.0f;
    };

    struct InteractionSample {
        int hotIndex = -1;
        bool confirmed = false;
        bool fromMouse = false;
    };

    struct Layout {
        float dialogBoxOuterMarginX = 40.0f;
        float dialogBoxOuterBottom = 20.0f;
        float dialogBoxHeight = 200.0f;
        float quickButtonGap = 15.0f;
        float quickButtonWidth = 45.0f;
        float quickHitPaddingX = 5.0f;
        float quickHitPaddingTop = 10.0f;
        float quickHitHeight = 30.0f;
        float quickPanelRightPadding = 30.0f;
        float quickPanelBottomPadding = 35.0f;
        float quickIconScale = 0.75f;
        float quickHoverOffsetX = 6.0f;
        float quickHoverScale = 1.08f;
        float quickHoverAnimSpeed = 16.0f;
        float quickEnterDuration = 0.18f;
        float quickExitDuration = 0.12f;
        float quickEnterOffsetY = 10.0f;
        float quickPressScale = 0.94f;
        float quickPressOffsetY = 2.0f;
        float quickPressAnimSpeed = 28.0f;
        float quickIdleTextAlpha = 0.70f;
        int systemTextPointSize = 20;
        int systemTextOutline = 1;
    };

    [[nodiscard]] std::string Tr(const std::string& key, const char* fallback) const {
        if (m_translate) {
            const std::string translated = m_translate(key);
            if (translated != "[" + key + "]") {
                return translated;
            }
        }
        return fallback;
    }

    [[nodiscard]] TexturePtr GetCachedLabel(const std::string& text) const {
        if (!m_assets || !m_font || m_fontPath.empty() || text.empty()) {
            return nullptr;
        }

        return m_assets->GetTextTexture(
            text,
            m_font,
            m_fontPath,
            m_fontPointSize,
            SDL_Color{ 255, 255, 255, 255 },
            std::max(0, m_layout.systemTextOutline)
        );
    }

    void SetTargetVisible(bool targetVisible) {
        if (targetVisible) {
            if (m_state == VisibilityState::Hidden || m_state == VisibilityState::Exiting) {
                m_state = VisibilityState::Entering;
                m_alphaTween.Start(&m_alpha,
                                   m_alpha,
                                   1.0f,
                                   std::max(0.0f, m_layout.quickEnterDuration),
                                   Ease::OutQuad,
                                   [this]() {
                                       if (m_state == VisibilityState::Entering) {
                                           m_state = VisibilityState::Idle;
                                       }
                                   });
                m_offsetTween.Start(&m_revealOffsetY,
                                    m_revealOffsetY,
                                    0.0f,
                                    std::max(0.0f, m_layout.quickEnterDuration),
                                    Ease::OutBack);
            }
            return;
        }

        if (m_state == VisibilityState::Entering || m_state == VisibilityState::Idle) {
            m_state = VisibilityState::Exiting;
            ResetHotControls();
            m_alphaTween.Start(&m_alpha,
                               m_alpha,
                               0.0f,
                               std::max(0.0f, m_layout.quickExitDuration),
                               Ease::InQuad,
                               [this]() {
                                   if (m_state == VisibilityState::Exiting) {
                                       m_state = VisibilityState::Hidden;
                                       ResetHotControls();
                                   }
                               });
            m_offsetTween.Start(&m_revealOffsetY,
                                m_revealOffsetY,
                                m_layout.quickEnterOffsetY,
                                std::max(0.0f, m_layout.quickExitDuration),
                                Ease::InQuad);
        }
    }

    void ResetVisibilityImmediate(bool visible) {
        m_alphaTween.Stop();
        m_offsetTween.Stop();
        m_state = visible ? VisibilityState::Idle : VisibilityState::Hidden;
        m_alpha = visible ? 1.0f : 0.0f;
        m_revealOffsetY = visible ? 0.0f : m_layout.quickEnterOffsetY;
    }

    [[nodiscard]] bool AcceptsInput() const noexcept {
        return m_state == VisibilityState::Entering || m_state == VisibilityState::Idle;
    }

    [[nodiscard]] InteractionSample SampleInput(Input& input, const Input::FocusInput& focus) {
        if (input.JustClicked()) {
            return SampleMouseInput(input);
        }

        const bool focusRouteActive = focus.active ||
            focus.up || focus.down || focus.left || focus.right ||
            focus.confirm || focus.cancel ||
            m_focusedIdx >= 0;
        if (focusRouteActive) {
            return SampleFocusInput(focus);
        }
        return SampleMouseInput(input);
    }

    [[nodiscard]] InteractionSample SampleFocusInput(const Input::FocusInput& focus) {
        InteractionSample sample;

        if (focus.cancel) {
            m_focusedIdx = -1;
        }

        int focusStep = 0;
        if (focus.left || focus.up) {
            focusStep = -1;
        }
        if (focus.right || focus.down) {
            focusStep = 1;
        }

        const int count = static_cast<int>(m_buttons.size());
        if (count > 0 && focusStep != 0) {
            if (m_focusedIdx < 0 || m_focusedIdx >= count) {
                m_focusedIdx = focusStep > 0 ? 0 : count - 1;
            } else {
                m_focusedIdx = (m_focusedIdx + focusStep + count) % count;
            }
        }

        if (m_focusedIdx >= 0 && m_focusedIdx < count) {
            sample.hotIndex = m_focusedIdx;
            sample.confirmed = focus.confirm;
        }

        return sample;
    }

    [[nodiscard]] InteractionSample SampleMouseInput(Input& input) {
        InteractionSample sample;
        m_focusedIdx = -1;

        const float mx = input.GetMouseX();
        const float my = input.GetMouseY();
        for (int i = 0; i < static_cast<int>(m_buttons.size()); ++i) {
            if (!Draw::Contains(m_buttons[static_cast<std::size_t>(i)].hitbox, mx, my)) {
                continue;
            }

            sample.hotIndex = i;
            sample.confirmed = input.JustClicked();
            sample.fromMouse = sample.confirmed;
            break;
        }

        return sample;
    }

    Action Activate(int index, Input& input, bool fromMouse) {
        if (index < 0 || index >= static_cast<int>(m_buttons.size())) {
            return NONE;
        }

        if (fromMouse) {
            input.ConsumeLeftClick();
        } else {
            m_focusedIdx = -1;
        }

        EnsureMotionSize();
        m_motion[static_cast<std::size_t>(index)].pressT = 1.0f;
        return m_buttons[static_cast<std::size_t>(index)].action;
    }

    void UpdateButtonMotion(float dt, int hotIndex, bool isAutoMode, bool isSkipMode) {
        const float hoverK = DampFactor(m_layout.quickHoverAnimSpeed, dt);
        const float pressK = DampFactor(m_layout.quickPressAnimSpeed, dt);

        for (int i = 0; i < static_cast<int>(m_motion.size()); ++i) {
            ButtonMotion& motion = m_motion[static_cast<std::size_t>(i)];
            const Action action = m_buttons[static_cast<std::size_t>(i)].action;
            const float targetHover = (i == hotIndex) ? 1.0f : 0.0f;
            const float targetActive = IsActiveAction(action, isAutoMode, isSkipMode) ? 1.0f : 0.0f;

            motion.hoverT += (targetHover - motion.hoverT) * hoverK;
            motion.activeT += (targetActive - motion.activeT) * hoverK;
            motion.pressT += (0.0f - motion.pressT) * pressK;
        }
    }

    [[nodiscard]] static bool IsActiveAction(Action action, bool isAutoMode, bool isSkipMode) noexcept {
        return (action == AUTO && isAutoMode) || (action == SKIP && isSkipMode);
    }

    void RenderButton(SDL_Renderer* renderer, int index) const {
        if (index < 0 || index >= static_cast<int>(m_buttons.size()) ||
            index >= static_cast<int>(m_motion.size())) {
            return;
        }

        const Button& btn = m_buttons[static_cast<std::size_t>(index)];
        if (!btn.texture) {
            return;
        }

        float texW = 0.0f;
        float texH = 0.0f;
        SDL_Texture* tex = btn.texture.get();
        if (!SDL_GetTextureSize(tex, &texW, &texH) || texW <= 0.0f || texH <= 0.0f) {
            return;
        }

        const ButtonMotion& motion = m_motion[static_cast<std::size_t>(index)];
        const float feedbackT = std::clamp(std::max(motion.hoverT, motion.activeT), 0.0f, 1.0f);
        const float hoverScale = Lerp(1.0f, m_layout.quickHoverScale, motion.hoverT);
        const float pressScale = Lerp(1.0f, m_layout.quickPressScale, motion.pressT);
        const float composedScale = m_layout.quickIconScale * hoverScale * pressScale;
        const float scaledW = texW * composedScale;
        const float scaledH = texH * composedScale;
        const float offsetX = m_layout.quickHoverOffsetX * motion.hoverT;
        const float offsetY = m_revealOffsetY + m_layout.quickPressOffsetY * motion.pressT;

        const SDL_FRect dst{
            btn.hitbox.x + (btn.hitbox.w - scaledW) * 0.5f + offsetX,
            btn.hitbox.y + (btn.hitbox.h - scaledH) * 0.5f + offsetY,
            scaledW,
            scaledH
        };

        SDL_Color tint = LerpColor(ui::theme::kTextSecondary, ui::theme::kBluePrimary, motion.hoverT);
        tint = LerpColor(tint, ui::theme::kAccentPrimary, motion.activeT);
        tint.a = UnitAlpha(m_alpha * Lerp(m_layout.quickIdleTextAlpha, 1.0f, feedbackT));

        SDL_SetTextureColorMod(tex, tint.r, tint.g, tint.b);
        SDL_SetTextureAlphaMod(tex, tint.a);
        SDL_RenderTexture(renderer, tex, nullptr, &dst);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
        SDL_SetTextureAlphaMod(tex, 255);
    }

    void EnsureMotionSize() {
        if (m_motion.size() != m_buttons.size()) {
            m_motion.assign(m_buttons.size(), ButtonMotion{});
        }
    }

    void ResetHotControls() noexcept {
        m_hoveredIdx = -1;
        m_focusedIdx = -1;
    }

    [[nodiscard]] static float DampFactor(float speed, float dt) noexcept {
        return 1.0f - std::exp(-std::max(0.0f, speed) * std::max(0.0f, dt));
    }

    [[nodiscard]] static float Lerp(float from, float to, float t) noexcept {
        return from + (to - from) * std::clamp(t, 0.0f, 1.0f);
    }

    [[nodiscard]] static SDL_Color LerpColor(SDL_Color from, SDL_Color to, float t) noexcept {
        const float u = std::clamp(t, 0.0f, 1.0f);
        return SDL_Color{
            static_cast<Uint8>(std::clamp(Lerp(static_cast<float>(from.r), static_cast<float>(to.r), u), 0.0f, 255.0f)),
            static_cast<Uint8>(std::clamp(Lerp(static_cast<float>(from.g), static_cast<float>(to.g), u), 0.0f, 255.0f)),
            static_cast<Uint8>(std::clamp(Lerp(static_cast<float>(from.b), static_cast<float>(to.b), u), 0.0f, 255.0f)),
            static_cast<Uint8>(std::clamp(Lerp(static_cast<float>(from.a), static_cast<float>(to.a), u), 0.0f, 255.0f))
        };
    }

    [[nodiscard]] static Uint8 UnitAlpha(float value) noexcept {
        return static_cast<Uint8>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
    }

    void ApplyTextStyleFromLayout() {
        const int targetPointSize = std::max(8, m_layout.systemTextPointSize);
        m_fontPointSize = targetPointSize;

        if (!m_assets || m_fontPath.empty()) {
            return;
        }

        m_styleFontHandle = m_assets->GetFont(m_fontPath, static_cast<float>(targetPointSize));
        if (m_styleFontHandle) {
            m_font = m_styleFontHandle.get();
        }
    }

    TTF_Font* m_font = nullptr;
    vn::AssetManager* m_assets = nullptr;
    std::string m_fontPath;
    int m_fontPointSize = 22;
    vn::AssetManager::FontHandle m_styleFontHandle;
    int m_W = 0;
    int m_H = 0;
    int m_hoveredIdx = -1;
    int m_focusedIdx = -1;
    float m_alpha = 0.0f;
    float m_revealOffsetY = 0.0f;
    Tween<float> m_alphaTween;
    Tween<float> m_offsetTween;
    VisibilityState m_state = VisibilityState::Hidden;
    std::vector<Button> m_buttons;
    std::vector<ButtonMotion> m_motion;
    Layout m_layout{};
    std::function<std::string(const std::string&)> m_translate;
};
