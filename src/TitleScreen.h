#pragma once

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <vector>
#include <string>
#include <memory>
#include <functional>

#include "AssetManager.h"
#include "Input.h"
#include "UITheme.h"

class TitleScreen {
public:
    enum class Action {
        None,
        Continue,
        NewGame,
        LoadGame,
        OpenSettings,
        Exit,
    };

    void SetContinueEnabled(bool enabled) {
        m_continueEnabled = enabled;
    }

    void SetContinueThumbnail(vn::AssetManager::TextureHandle thumb) {
        m_continueThumb = std::move(thumb);
    }

    void SetTranslator(std::function<std::string(const std::string&)> translator) {
        m_translate = std::move(translator);
        if (m_screenW > 0 && m_screenH > 0) {
            BuildLayout();
        }
    }

    void RefreshStrings() {
        if (m_screenW > 0 && m_screenH > 0) {
            BuildLayout();
        }
    }

    void Init(SDL_Renderer* renderer,
              TTF_Font* font,
              int screenW,
              int screenH,
              const std::string& bgPath,
              vn::AssetManager* assets = nullptr,
              const std::string& fontPath = "",
              int fontPointSize = 22) {
        m_renderer = renderer;
        m_font = font;
        m_screenW = screenW;
        m_screenH = screenH;
        m_assets = assets;
        m_fontPath = fontPath;
        m_fontPointSize = fontPointSize;
        BuildLayout();
        LoadBackground(bgPath);
    }

    void Shutdown() {
        if (m_bgTex) {
            SDL_DestroyTexture(m_bgTex);
            m_bgTex = nullptr;
        }
        m_font = nullptr;
        m_assets = nullptr;
        m_renderer = nullptr;
    }

    void ResetHover() {
        m_hovered = -1;
        m_focused = -1;
    }

    int GetHoveredIndex() const {
        return m_hovered;
    }

    Action Update(float mouseX, float mouseY, bool clicked) {
        return Update(mouseX, mouseY, clicked, {});
    }

    Action Update(float mouseX, float mouseY, bool clicked, const Input::FocusInput& focus) {
        if (focus.active) {
            EnsureValidFocus();

            if (focus.up) {
                MoveFocus(-1);
            }
            if (focus.down) {
                MoveFocus(1);
            }

            m_hovered = m_focused;
            if (focus.confirm && IsButtonEnabled(m_focused)) {
                return m_buttons[m_focused].action;
            }
            return Action::None;
        }

        m_hovered = -1;
        for (int i = 0; i < static_cast<int>(m_buttons.size()); ++i) {
            const SDL_FRect& r = m_buttons[i].hitbox;
            if (mouseX >= r.x - 20.0f && mouseX <= r.x + r.w + 60.0f &&
                mouseY >= r.y - 10.0f && mouseY <= r.y + r.h + 10.0f) {
                if (m_buttons[i].action == Action::Continue && !m_continueEnabled) {
                    continue;
                }
                m_hovered = i;
                m_focused = i;
                if (clicked) return m_buttons[i].action;
                break;
            }
        }
        return Action::None;
    }

    void Render(SDL_Renderer* renderer) {
        if (m_bgTex) {
            SDL_FRect dst = { 0.0f, 0.0f, static_cast<float>(m_screenW), static_cast<float>(m_screenH) };
            SDL_RenderTexture(renderer, m_bgTex, nullptr, &dst);
        } else {
            SDL_SetRenderDrawColor(renderer,
                                   ui::theme::kBlackPanel.r,
                                   ui::theme::kBlackPanel.g,
                                   ui::theme::kBlackPanel.b,
                                   255);
            SDL_RenderClear(renderer);
        }

        // Left navigation mask for better readability.
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_FRect leftPanel = { 0.0f, 0.0f, static_cast<float>(m_screenW) * 0.35f, static_cast<float>(m_screenH) };
        SDL_SetRenderDrawColor(renderer,
                       ui::theme::kBlackBase.r,
                       ui::theme::kBlackBase.g,
                       ui::theme::kBlackBase.b,
                       190);
        SDL_RenderFillRect(renderer, &leftPanel);
        SDL_SetRenderDrawColor(renderer,
                               ui::theme::kGrayGloom.r,
                               ui::theme::kGrayGloom.g,
                               ui::theme::kGrayGloom.b,
                               190);
        SDL_RenderLine(renderer,
                       leftPanel.x + leftPanel.w - 1.0f,
                       leftPanel.y,
                       leftPanel.x + leftPanel.w - 1.0f,
                       leftPanel.y + leftPanel.h);

        DrawCachedText(renderer, Tr("ui_engine_name", "VN ENGINE"), 60.0f, 80.0f, ui::theme::kAccentSoft);
        DrawCachedText(renderer, Tr("ui_engine_version", "Version 1.0"), 62.0f, 130.0f, ui::theme::kTextSecondary);

        for (int i = 0; i < static_cast<int>(m_buttons.size()); ++i) {
            const bool hovered = (i == m_hovered);
            const MenuButton& btn = m_buttons[i];
            const bool isDisabled = (btn.action == Action::Continue && !m_continueEnabled);

            SDL_Color textColor = hovered && !isDisabled ? ui::theme::kAccentPrimary :
                                  isDisabled ? ui::theme::kTextDisabled :
                                  ui::theme::kTextPrimary;
            const float textX = btn.hitbox.x + ((hovered && !isDisabled) ? 15.0f : 0.0f);

            if (hovered && !isDisabled) {
                DrawCachedText(renderer, ">", btn.hitbox.x - 10.0f, btn.hitbox.y, ui::theme::kBluePrimary);
            }
            DrawCachedText(renderer, btn.label, textX, btn.hitbox.y, textColor);

            if (hovered && !isDisabled && btn.action == Action::Continue && m_continueThumb) {
                const float thumbW = 213.0f;
                const float thumbH = 120.0f;
                const float thumbX = btn.hitbox.x + 10.0f;
                const float thumbY = btn.hitbox.y - thumbH - 15.0f;

                SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
                const SDL_FRect shadowRect = { thumbX + 2.0f, thumbY + 3.0f, thumbW, thumbH };
                SDL_SetRenderDrawColor(renderer,
                                       ui::theme::kBlackAbyss.r,
                                       ui::theme::kBlackAbyss.g,
                                       ui::theme::kBlackAbyss.b,
                                       120);
                SDL_RenderFillRect(renderer, &shadowRect);

                const SDL_FRect cardRect = { thumbX - 4.0f, thumbY - 4.0f, thumbW + 8.0f, thumbH + 8.0f };
                SDL_SetRenderDrawColor(renderer,
                                       ui::theme::kBlackElevated.r,
                                       ui::theme::kBlackElevated.g,
                                       ui::theme::kBlackElevated.b,
                                       225);
                SDL_RenderFillRect(renderer, &cardRect);
                SDL_SetRenderDrawColor(renderer,
                                       ui::theme::kGrayMist.r,
                                       ui::theme::kGrayMist.g,
                                       ui::theme::kGrayMist.b,
                                       235);
                SDL_RenderRect(renderer, &cardRect);

                const SDL_FRect thumbRect = { thumbX, thumbY, thumbW, thumbH };
                SDL_RenderTexture(renderer, m_continueThumb.get(), nullptr, &thumbRect);

                const SDL_FRect textBg = { thumbX, thumbY + thumbH - 26.0f, thumbW, 26.0f };
                SDL_SetRenderDrawColor(renderer,
                                       ui::theme::kGraySteel.r,
                                       ui::theme::kGraySteel.g,
                                       ui::theme::kGraySteel.b,
                                       225);
                SDL_RenderFillRect(renderer, &textBg);

                if (m_assets && !m_fontPath.empty()) {
                    int labelPointSize = m_fontPointSize - 6;
                    if (labelPointSize < 12) {
                        labelPointSize = 12;
                    }

                    auto labelFont = m_assets->GetFont(m_fontPath, static_cast<float>(labelPointSize));
                    if (labelFont) {
                        auto labelTex = m_assets->GetTextTexture(Tr("ui_save_data_upper", "SAVE DATA"), labelFont.get(), m_fontPath, labelPointSize, ui::theme::kTextPrimary, 1);
                        if (labelTex) {
                            float labelW = 0.0f;
                            float labelH = 0.0f;
                            SDL_GetTextureSize(labelTex.get(), &labelW, &labelH);
                            SDL_FRect labelDst = {
                                textBg.x + (textBg.w - labelW) * 0.5f,
                                textBg.y + (textBg.h - labelH) * 0.5f - 1.0f,
                                labelW,
                                labelH
                            };
                            SDL_RenderTexture(renderer, labelTex.get(), nullptr, &labelDst);
                    }
                } else {
                    DrawCachedText(renderer, Tr("ui_save_data_upper", "SAVE DATA"), textBg.x + 48.0f, textBg.y + 4.0f, ui::theme::kTextPrimary);
                }
            }
        }
    }
    }

private:
    struct MenuButton {
        std::string label;
        Action action = Action::None;
        SDL_FRect hitbox{};
    };

    using TexturePtr = std::shared_ptr<SDL_Texture>;

    [[nodiscard]] bool IsButtonEnabled(int index) const {
        if (index < 0 || index >= static_cast<int>(m_buttons.size())) {
            return false;
        }
        return !(m_buttons[index].action == Action::Continue && !m_continueEnabled);
    }

    void EnsureValidFocus() {
        if (IsButtonEnabled(m_focused)) {
            return;
        }

        for (int i = 0; i < static_cast<int>(m_buttons.size()); ++i) {
            if (IsButtonEnabled(i)) {
                m_focused = i;
                return;
            }
        }
        m_focused = -1;
    }

    void MoveFocus(int delta) {
        if (m_buttons.empty()) {
            m_focused = -1;
            return;
        }

        EnsureValidFocus();
        if (m_focused < 0) {
            return;
        }

        const int n = static_cast<int>(m_buttons.size());
        int next = m_focused;
        for (int step = 0; step < n; ++step) {
            next = (next + delta + n) % n;
            if (IsButtonEnabled(next)) {
                m_focused = next;
                return;
            }
        }
    }

    std::string Tr(const std::string& key, const char* fallback) const {
        if (m_translate) {
            const std::string translated = m_translate(key);
            if (translated != "[" + key + "]") {
                return translated;
            }
        }
        return fallback;
    }

    void BuildLayout() {
        const float startX = 60.0f;
        const float startY = static_cast<float>(m_screenH) * 0.5f - 25.0f;
        const float gap = 50.0f;

        m_buttons = {
            { Tr("ui_continue", "Continue"),   Action::Continue,     SDL_FRect{ startX, startY + gap * 0.0f, 220.0f, 30.0f } },
            { Tr("ui_start_game", "Start Game"), Action::NewGame,      SDL_FRect{ startX, startY + gap * 1.0f, 220.0f, 30.0f } },
            { Tr("ui_load", "Load Game"),  Action::LoadGame,     SDL_FRect{ startX, startY + gap * 2.0f, 220.0f, 30.0f } },
            { Tr("ui_config", "Config"),     Action::OpenSettings, SDL_FRect{ startX, startY + gap * 3.0f, 220.0f, 30.0f } },
            { Tr("ui_exit", "Exit"),       Action::Exit,         SDL_FRect{ startX, startY + gap * 4.0f, 220.0f, 30.0f } }
        };
    }

    void LoadBackground(const std::string& path) {
        if (m_bgTex) {
            SDL_DestroyTexture(m_bgTex);
            m_bgTex = nullptr;
        }

        if (!m_renderer || path.empty()) return;

        SDL_Surface* surface = IMG_Load(path.c_str());
        if (!surface) return;

        m_bgTex = SDL_CreateTextureFromSurface(m_renderer, surface);
        SDL_DestroySurface(surface);
    }

    void DrawCachedText(SDL_Renderer* renderer, const std::string& text, float x, float y, SDL_Color color) {
        if (!m_font || !renderer) return;
        if (!m_assets || m_fontPath.empty()) return;

        auto texPtr = m_assets->GetTextTexture(text, m_font, m_fontPath, m_fontPointSize, color, 1);
        if (!texPtr) return;

        SDL_Texture* tex = texPtr.get();
        float w, h;
        SDL_GetTextureSize(tex, &w, &h);
        
        if (w > 0.0f && h > 0.0f) {
            SDL_FRect dst = { x, y, static_cast<float>(w), static_cast<float>(h) };
            SDL_RenderTexture(renderer, tex, nullptr, &dst);
        }
    }

    SDL_Renderer* m_renderer = nullptr;
    TTF_Font* m_font = nullptr;
    vn::AssetManager* m_assets = nullptr;
    std::string m_fontPath;
    int m_fontPointSize = 22;
    SDL_Texture* m_bgTex = nullptr;

    int m_screenW = 1280;
    int m_screenH = 720;

    int m_hovered = -1;
    int m_focused = -1;
    bool m_continueEnabled = false;
    vn::AssetManager::TextureHandle m_continueThumb;
    std::vector<MenuButton> m_buttons;
    std::function<std::string(const std::string&)> m_translate;
};
