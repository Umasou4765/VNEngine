#pragma once
#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "AssetManager.h"
#include "Input.h"
#include "UITheme.h"

struct Hotspot {
    SDL_FRect rect;            // Hot area in panorama-space coordinates.
    std::string targetBranch;  // Branch/event key to trigger on click.
    std::string hoverText;     // Tooltip shown while hovering.
};

// Handles observe-mode camera panning and hotspot interaction.
class ObserveManager {
public:
    void SetTranslator(std::function<std::string(const std::string&)> translator) {
        m_translate = std::move(translator);
    }

    ~ObserveManager() {
        if (m_defaultCursor) SDL_DestroyCursor(m_defaultCursor);
        if (m_hotspotCursor) SDL_DestroyCursor(m_hotspotCursor);
        m_defaultCursor = nullptr;
        m_hotspotCursor = nullptr;
    }

    void Init(SDL_Renderer* renderer,
              TTF_Font* font,
              int w,
              int h,
              vn::AssetManager* assets = nullptr,
              const std::string& fontPath = "",
              int fontPointSize = 22) {
        m_renderer = renderer;
        m_font = font;
        m_W = w;
        m_H = h;
        m_assets = assets;
        m_fontPath = fontPath;
        m_fontPointSize = fontPointSize;

        if (!m_defaultCursor) m_defaultCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
        if (!m_hotspotCursor) m_hotspotCursor = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    }

    // panScale controls panorama width relative to screen width (1.5 = 150%).
    void Start(SDL_Texture* bgTex,
               const std::vector<Hotspot>& hotspots,
               float panScale = 1.5f,
               bool allowCancel = false) {
        if (m_bgTex != bgTex) {
            m_panX = 0.0f;
            m_targetPanX = 0.0f;
        }
        m_bgTex = bgTex;
        m_hotspots = hotspots;
        m_panScale = std::max(1.0f, panScale);
        m_allowCancel = allowCancel;
        m_hoveredIdx = -1;
        m_mouseX = 0.0f;
        m_mouseY = 0.0f;
        RefreshCursor();
    }

    void Stop() {
        m_hotspots.clear();
        m_panScale = 1.0f;
        m_hoveredIdx = -1;
        m_allowCancel = false;
        if (m_cursorOnHotspot && m_defaultCursor) {
            SDL_SetCursor(m_defaultCursor);
            m_cursorOnHotspot = false;
        }
    }

    void ReleaseResources() {
        Stop();
        m_bgTex = nullptr;
        m_panX = 0.0f;
        m_targetPanX = 0.0f;
        m_font = nullptr;
        m_assets = nullptr;
        m_renderer = nullptr;
    }

    void SetCameraTransform(float cameraX, float cameraY, float cameraZoom, const SDL_FPoint& cameraOffset) {
        m_cameraX = cameraX;
        m_cameraY = cameraY;
        m_cameraZoom = std::max(0.1f, cameraZoom);
        m_cameraOffset = cameraOffset;
    }

    void Update(float dt, Input& input) {
        m_mouseX = input.GetMouseX();
        m_mouseY = input.GetMouseY();

        // Keyboard/arrow key panning
        const float keyboardPanSpeed = 400.0f * dt;
        if (input.IsHeld(SDL_SCANCODE_A) || input.IsHeld(SDL_SCANCODE_LEFT)) {
            m_targetPanX += keyboardPanSpeed;
        }
        if (input.IsHeld(SDL_SCANCODE_D) || input.IsHeld(SDL_SCANCODE_RIGHT)) {
            m_targetPanX -= keyboardPanSpeed;
        }

        // Edge panning (mouse-based).
        const float edgePanSpeed = 600.0f * dt;
        const float edgeZone = m_W * 0.15f;

        if (m_mouseX < edgeZone) {
            m_targetPanX += edgePanSpeed;
        } else if (m_mouseX > m_W - edgeZone) {
            m_targetPanX -= edgePanSpeed;
        }

        const float panoW = m_W * m_panScale;
        const float minPan = -(panoW - m_W);
        const float maxPan = 0.0f;
        m_targetPanX = std::clamp(m_targetPanX, minPan, maxPan);

        // Smooth camera movement.
        m_panX += (m_targetPanX - m_panX) * 10.0f * dt;

        // Hover and click test.
        m_hoveredIdx = -1;
        for (int i = static_cast<int>(m_hotspots.size()) - 1; i >= 0; --i) {
            SDL_FRect screenRect = m_hotspots[i].rect;
            screenRect.x += m_panX;
            screenRect = ApplyCameraToRect(screenRect);

            if (m_mouseX >= screenRect.x && m_mouseX <= screenRect.x + screenRect.w &&
                m_mouseY >= screenRect.y && m_mouseY <= screenRect.y + screenRect.h) {
                m_hoveredIdx = i;
                break;
            }
        }

        RefreshCursor();
    }

    [[nodiscard]] std::string GetHoveredTargetId() const {
        if (m_hoveredIdx < 0 || m_hoveredIdx >= static_cast<int>(m_hotspots.size())) {
            return {};
        }
        return m_hotspots[m_hoveredIdx].targetBranch;
    }

    void RenderBackground() {
        if (!m_renderer) return;

        const float panoW = m_W * m_panScale;
        if (m_bgTex) {
            SDL_FRect bgDst = { m_panX, 0.0f, panoW, static_cast<float>(m_H) };
            bgDst = ApplyCameraToRect(bgDst);
            SDL_RenderTexture(m_renderer, m_bgTex, nullptr, &bgDst);
        } else {
            // Keep the mode usable even when the image failed to load.
            SDL_SetRenderDrawColor(m_renderer,
                                   ui::theme::kBlackBase.r,
                                   ui::theme::kBlackBase.g,
                                   ui::theme::kBlackBase.b,
                                   255);
            SDL_FRect fallback = { 0.0f, 0.0f, static_cast<float>(m_W), static_cast<float>(m_H) };
            fallback = ApplyCameraToRect(fallback);
            SDL_RenderFillRect(m_renderer, &fallback);
            RenderHintText(Tr("ui_observe_bg_missing", "Background texture missing"),
                           24.0f,
                           24.0f,
                           ui::theme::kAccentPrimary.r,
                           ui::theme::kAccentPrimary.g,
                           ui::theme::kAccentPrimary.b,
                           255);
        }
    }

    void RenderOverlay() {
        if (!m_renderer) return;

        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        const float pulse = 0.5f + 0.5f * SDL_sinf(static_cast<float>(SDL_GetTicks()) * 0.006f);
        const Uint8 idleAlpha = static_cast<Uint8>(90 + pulse * 70);

        for (int i = 0; i < static_cast<int>(m_hotspots.size()); ++i) {
            SDL_FRect sr = m_hotspots[i].rect;
            sr.x += m_panX;
            sr = ApplyCameraToRect(sr);

            if (i == m_hoveredIdx) {
                SDL_SetRenderDrawColor(m_renderer,
                                       ui::theme::kBlueStrong.r,
                                       ui::theme::kBlueStrong.g,
                                       ui::theme::kBlueStrong.b,
                                       110);
                SDL_RenderFillRect(m_renderer, &sr);
                SDL_SetRenderDrawColor(m_renderer,
                                       ui::theme::kAccentPrimary.r,
                                       ui::theme::kAccentPrimary.g,
                                       ui::theme::kAccentPrimary.b,
                                       255);
                SDL_RenderRect(m_renderer, &sr);
                RenderHoverText(m_hotspots[i].hoverText);
            } else {
                SDL_SetRenderDrawColor(m_renderer,
                                       ui::theme::kGrayMist.r,
                                       ui::theme::kGrayMist.g,
                                       ui::theme::kGrayMist.b,
                                       idleAlpha);
                SDL_RenderRect(m_renderer, &sr);
            }
        }

        const std::string hint = m_allowCancel
            ? Tr("ui_observe_hint_cancel", "OBSERVE: Move mouse to screen edges to pan. Click a hotspot. Right-click / ESC to leave.")
            : Tr("ui_observe_hint_inspect", "OBSERVE: Move mouse to screen edges to pan. Click a hotspot to inspect.");
        RenderHintText(hint,
                       24.0f,
                       static_cast<float>(m_H - 42),
                       ui::theme::kTextPrimary.r,
                       ui::theme::kTextPrimary.g,
                       ui::theme::kTextPrimary.b,
                       255);
    }

    void Render() {
        RenderBackground();
        RenderOverlay();
    }

private:
    [[nodiscard]] SDL_FRect ApplyCameraToRect(SDL_FRect rect) const {
        const float zoom = std::max(0.1f, m_cameraZoom);
        rect.w *= zoom;
        rect.h *= zoom;
        rect.x = (m_W / 2.0f) + (rect.x - m_W / 2.0f) * zoom - m_cameraX * zoom + m_cameraOffset.x;
        rect.y = (m_H / 2.0f) + (rect.y - m_H / 2.0f) * zoom - m_cameraY * zoom + m_cameraOffset.y;
        return rect;
    }

    void RefreshCursor() {
        const bool wantsHotspotCursor = (m_hoveredIdx >= 0);
        if (wantsHotspotCursor == m_cursorOnHotspot) {
            return;
        }

        if (wantsHotspotCursor) {
            if (m_hotspotCursor) {
                SDL_SetCursor(m_hotspotCursor);
                m_cursorOnHotspot = true;
            }
        } else if (m_defaultCursor) {
            SDL_SetCursor(m_defaultCursor);
            m_cursorOnHotspot = false;
        }
    }

    struct CachedText {
        std::shared_ptr<SDL_Texture> texture;
        float w = 0.0f;
        float h = 0.0f;
    };

    [[nodiscard]] std::string Tr(const std::string& key, const std::string& fallback) const {
        if (m_translate) {
            const std::string translated = m_translate(key);
            if (!translated.empty() && translated != "[" + key + "]") {
                return translated;
            }
        }
        return fallback;
    }

    CachedText GetOrCreateText(const std::string& text) {
        if (!m_renderer || !m_font || text.empty() || !m_assets || m_fontPath.empty()) return {};

        CachedText entry;
        entry.texture = m_assets->GetTextTexture(
            text,
            m_font,
            m_fontPath,
            m_fontPointSize,
            SDL_Color{ 255, 255, 255, 255 },
            1
        );
        if (!entry.texture) return {};
        SDL_GetTextureSize(entry.texture.get(), &entry.w, &entry.h);
        return entry;
    }

    void RenderHintText(const std::string& text, float x, float y, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
        if (!m_renderer || !m_font || text.empty()) return;

        CachedText entry = GetOrCreateText(text);
        if (!entry.texture) return;

        SDL_Texture* tex = entry.texture.get();
        SDL_SetTextureColorMod(tex, r, g, b);
        SDL_SetTextureAlphaMod(tex, a);
        SDL_FRect dst = { x, y, entry.w, entry.h };
            SDL_FRect bg = { dst.x - 6.0f, dst.y - 4.0f, dst.w + 12.0f, dst.h + 8.0f };
            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(m_renderer,
                                   ui::theme::kBlackAbyss.r,
                                   ui::theme::kBlackAbyss.g,
                                   ui::theme::kBlackAbyss.b,
                                   170);
            SDL_RenderFillRect(m_renderer, &bg);
            SDL_RenderTexture(m_renderer, tex, nullptr, &dst);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
        SDL_SetTextureAlphaMod(tex, 255);
    }

    void RenderHoverText(const std::string& text) {
        if (!m_renderer || !m_font || text.empty()) return;

        CachedText entry = GetOrCreateText(text);
        if (!entry.texture) return;

        SDL_Texture* tex = entry.texture.get();
        SDL_SetTextureColorMod(tex,
                               ui::theme::kTextPrimary.r,
                               ui::theme::kTextPrimary.g,
                               ui::theme::kTextPrimary.b);
        SDL_SetTextureAlphaMod(tex, 255);
        SDL_FRect dst = { m_mouseX + 15.0f, m_mouseY + 15.0f, entry.w, entry.h };
            SDL_FRect bg = { dst.x - 4.0f, dst.y - 4.0f, dst.w + 8.0f, dst.h + 8.0f };

            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(m_renderer,
                                   ui::theme::kBlackAbyss.r,
                                   ui::theme::kBlackAbyss.g,
                                   ui::theme::kBlackAbyss.b,
                                   180);
            SDL_RenderFillRect(m_renderer, &bg);
            SDL_RenderTexture(m_renderer, tex, nullptr, &dst);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
        SDL_SetTextureAlphaMod(tex, 255);
    }

    SDL_Renderer* m_renderer = nullptr;
    TTF_Font* m_font = nullptr;
    vn::AssetManager* m_assets = nullptr;
    std::string m_fontPath;
    int m_fontPointSize = 22;
    int m_W = 0;
    int m_H = 0;

    SDL_Texture* m_bgTex = nullptr;
    std::vector<Hotspot> m_hotspots;

    SDL_Cursor* m_defaultCursor = nullptr;
    SDL_Cursor* m_hotspotCursor = nullptr;
    bool m_cursorOnHotspot = false;
    bool m_allowCancel = false;
    std::function<std::string(const std::string&)> m_translate;

    float m_panScale = 1.5f;
    float m_panX = 0.0f;
    float m_targetPanX = 0.0f;
    int m_hoveredIdx = -1;

    float m_cameraX = 0.0f;
    float m_cameraY = 0.0f;
    float m_cameraZoom = 1.0f;
    SDL_FPoint m_cameraOffset{ 0.0f, 0.0f };

    float m_mouseX = 0.0f;
    float m_mouseY = 0.0f;
};
