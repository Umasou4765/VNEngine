#pragma once
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <string>
#include <vector>
#include <memory>
#include "AssetManager.h"
#include "Input.h"
#include "Tween.h"
#include "RendererUtils.h"
#include "UITheme.h"

struct Choice {
    std::string label;
    int value;
};

class ChoiceMenu {
public:
    bool visible = false;

    void Init(TTF_Font* font,
              int screenW,
              int screenH,
              vn::AssetManager* assets = nullptr,
              const std::string& fontPath = "",
              int fontPointSize = 22) {
        m_font = font;
        m_screenW = screenW;
        m_screenH = screenH;
        m_assets = assets;
        m_fontPath = fontPath;
        m_fontPointSize = fontPointSize;
        ApplyTextStyleFromLayout();
    }

    void Show(const std::vector<Choice>& choices) {
        m_choices = choices;
        m_hovered = -1;
        m_focused = m_choices.empty() ? -1 : 0;
        m_selected = -1;
        visible = true;

        m_hoverOffsets.assign(m_choices.size(), 0.0f);
        m_hoverScales.assign(m_choices.size(), 1.0f);

        m_choiceTextures.clear();
        m_choiceTextures.reserve(m_choices.size());
        for (int i = 0; i < static_cast<int>(m_choices.size()); ++i) {
            m_choiceTextures.push_back(GetCachedChoiceTexture(i));
        }

        m_alpha = 0.0f;
        m_alphaTween.Start(&m_alpha, 0.0f, 1.0f, 0.25f, Ease::OutQuad);
    }

    void Hide() {
        visible = false;
        m_selected = -1;
        m_focused = -1;
        m_choices.clear();
        m_choiceTextures.clear();
    }

    int GetHoveredIndex() const {
        return m_hovered;
    }

    void ReleaseResources() {
        Hide();
        m_styleFontHandle.reset();
        m_font = nullptr;
        m_assets = nullptr;
    }

    int Update(float dt, float mouseX, float mouseY, bool clicked) {
        return Update(dt, mouseX, mouseY, clicked, {});
    }

    int Update(float dt, float mouseX, float mouseY, bool clicked, const Input::FocusInput& focus) {
        if (!visible) return -1;
        m_alphaTween.Update(dt);

        const float btnW = m_layout.choiceButtonWidth;
        const float btnH = m_layout.choiceButtonHeight;
        const float gap = m_layout.choiceButtonGap;
        const int n = static_cast<int>(m_choices.size());
        const float totalH = n * btnH + (n - 1) * gap;
        const float startY = (m_screenH - totalH) * 0.5f;
        const float startX = (m_screenW - btnW) * 0.5f;

        if (m_hoverOffsets.size() != m_choices.size()) {
            m_hoverOffsets.assign(m_choices.size(), 0.0f);
        }
        if (m_hoverScales.size() != m_choices.size()) {
            m_hoverScales.assign(m_choices.size(), 1.0f);
        }

        if (focus.active && n > 0) {
            if (m_focused < 0 || m_focused >= n) {
                m_focused = 0;
            }
            if (focus.up) {
                m_focused = (m_focused - 1 + n) % n;
            }
            if (focus.down) {
                m_focused = (m_focused + 1) % n;
            }
            m_hovered = m_focused;
            if (focus.confirm) {
                m_selected = m_choices[m_focused].value;
                return m_selected;
            }
        } else {
            m_hovered = -1;
            for (int i = 0; i < n; ++i) {
                const float localAlpha = std::clamp((m_alpha * 1.5f) - (i * 0.15f), 0.0f, 1.0f);
                const float slideOffset = (1.0f - Ease::OutBack(localAlpha)) * 40.0f;
                const float bx = startX + slideOffset;
                const float by = startY + i * (btnH + gap);
                const SDL_FRect hoverRect = { bx, by, btnW, btnH };
                if (Draw::Contains(hoverRect, mouseX, mouseY)) {
                    m_hovered = i;
                    m_focused = i;
                    if (clicked) {
                        m_selected = m_choices[i].value;
                        return m_selected;
                    }
                }
            }
        }

        const float smoothing = std::clamp(std::max(0.0f, dt) * std::max(0.0f, m_layout.choiceHoverAnimSpeed), 0.0f, 1.0f);
        for (int i = 0; i < n; ++i) {
            const bool isHovered = (i == m_hovered);
            const float targetOffset = isHovered ? m_layout.choiceHoverOffsetX : 0.0f;
            const float targetScale = isHovered ? m_layout.choiceHoverScale : 1.0f;
            m_hoverOffsets[i] += (targetOffset - m_hoverOffsets[i]) * smoothing;
            m_hoverScales[i] += (targetScale - m_hoverScales[i]) * smoothing;
        }

        return -1;
    }

    void Render(SDL_Renderer* renderer) {
        if (!visible || m_choices.empty()) return;

        const float btnW = m_layout.choiceButtonWidth;
        const float btnH = m_layout.choiceButtonHeight;
        const float gap = m_layout.choiceButtonGap;
        const int n = static_cast<int>(m_choices.size());
        const float totalH = n * btnH + (n - 1) * gap;
        const float startY = (m_screenH - totalH) * 0.5f;
        const float startX = (m_screenW - btnW) * 0.5f;

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        for (int i = 0; i < n; ++i) {
            const float localAlpha = std::clamp((m_alpha * 1.5f) - (i * 0.15f), 0.0f, 1.0f);
            const float slideOffset = (1.0f - Ease::OutBack(localAlpha)) * 40.0f;
            const float hoverOffset = i < static_cast<int>(m_hoverOffsets.size()) ? m_hoverOffsets[i] : 0.0f;
            const float scale = i < static_cast<int>(m_hoverScales.size()) ? m_hoverScales[i] : 1.0f;
            const float bx = startX + slideOffset + hoverOffset;
            const float by = startY + i * (btnH + gap);
            const bool hovered = (m_hovered == i);

            const float scaledW = btnW * scale;
            const float scaledH = btnH * scale;
            SDL_FRect rect = {
                bx - (scaledW - btnW) * 0.5f,
                by - (scaledH - btnH) * 0.5f,
                scaledW,
                scaledH
            };

            const Uint8 a = static_cast<Uint8>(localAlpha * 255.0f);
            const SDL_Color fillColor = hovered
                ? ui::theme::WithAlpha(ui::theme::kGraySteel, static_cast<Uint8>(localAlpha * 235.0f))
                : ui::theme::WithAlpha(ui::theme::kBlackElevated, static_cast<Uint8>(localAlpha * 210.0f));
            const SDL_Color borderColor = hovered
                ? ui::theme::WithAlpha(ui::theme::kBluePrimary, static_cast<Uint8>(localAlpha * 230.0f))
                : ui::theme::WithAlpha(ui::theme::kGrayGloom, static_cast<Uint8>(localAlpha * 150.0f));

            SDL_SetRenderDrawColor(renderer, fillColor.r, fillColor.g, fillColor.b, fillColor.a);
            SDL_RenderFillRect(renderer, &rect);

            SDL_SetRenderDrawColor(renderer, borderColor.r, borderColor.g, borderColor.b, borderColor.a);
            SDL_RenderRect(renderer, &rect);

            if (m_font) {
                auto texPtr = (i < static_cast<int>(m_choiceTextures.size())) ? m_choiceTextures[i] : nullptr;
                if (texPtr) {
                    SDL_Texture* tex = texPtr.get();
                    float w, h;
                    SDL_GetTextureSize(tex, &w, &h);
                    
                    // Set alpha modulation
                    SDL_SetTextureAlphaMod(tex, a);
                    
                    // Keep strong contrast on highlighted option.
                    if (hovered) {
                        SDL_SetTextureColorMod(tex,
                                               ui::theme::kAccentSoft.r,
                                               ui::theme::kAccentSoft.g,
                                               ui::theme::kAccentSoft.b);
                    } else {
                        SDL_SetTextureColorMod(tex,
                                               ui::theme::kTextPrimary.r,
                                               ui::theme::kTextPrimary.g,
                                               ui::theme::kTextPrimary.b);
                    }
                    
                    const float tx = rect.x + (rect.w - w) * 0.5f + (hovered ? m_layout.choiceHoverTextOffsetX : 0.0f);
                    const float ty = rect.y + (rect.h - h) * 0.5f;
                    SDL_FRect dst = { tx, ty, static_cast<float>(w), static_cast<float>(h) };
                    SDL_RenderTexture(renderer, tex, nullptr, &dst);
                    
                    // Reset mods
                    SDL_SetTextureAlphaMod(tex, 255);
                    SDL_SetTextureColorMod(tex, 255, 255, 255);
                }
            }
        }
    }

private:
    using TexturePtr = std::shared_ptr<SDL_Texture>;

    struct Layout {
        float choiceButtonWidth = 480.0f;
        float choiceButtonHeight = 52.0f;
        float choiceButtonGap = 14.0f;
        float choiceHoverTextOffsetX = 8.0f;
        float choiceHoverOffsetX = 10.0f;
        float choiceHoverScale = 1.02f;
        float choiceHoverAnimSpeed = 14.0f;
        int choiceTextPointSize = 24;
        int choiceTextOutline = 2;
    };

    TexturePtr GetCachedChoiceTexture(int choiceIdx) {
        if (choiceIdx < 0 || choiceIdx >= static_cast<int>(m_choices.size())) return nullptr;

        if (m_assets && m_font && !m_fontPath.empty()) {
            return m_assets->GetTextTexture(
                m_choices[choiceIdx].label,
                m_font,
                m_fontPath,
                m_fontPointSize,
                SDL_Color{ 255, 255, 255, 255 },
                std::max(0, m_layout.choiceTextOutline)
            );
        }

        return nullptr;
    }

    TTF_Font* m_font = nullptr;
    vn::AssetManager* m_assets = nullptr;
    std::string m_fontPath;
    int m_fontPointSize = 22;
    vn::AssetManager::FontHandle m_styleFontHandle;
    int m_screenW = 1280;
    int m_screenH = 720;
    Layout m_layout{};

    std::vector<Choice> m_choices;
    std::vector<TexturePtr> m_choiceTextures;
    int m_hovered = -1;
    int m_focused = -1;
    int m_selected = -1;
    std::vector<float> m_hoverOffsets;
    std::vector<float> m_hoverScales;

    float m_alpha = 0.0f;
    Tween<float> m_alphaTween;

    void ApplyTextStyleFromLayout() {
        const int targetPointSize = std::max(8, m_layout.choiceTextPointSize);
        m_fontPointSize = targetPointSize;

        if (!m_assets || m_fontPath.empty()) {
            return;
        }

        m_styleFontHandle = m_assets->GetFont(m_fontPath, static_cast<float>(targetPointSize));
        if (m_styleFontHandle) {
            m_font = m_styleFontHandle.get();
        }
    }
};
