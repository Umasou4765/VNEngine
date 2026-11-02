#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <functional>
#include <algorithm>
#include <cmath>
#include "AssetManager.h"
#include "Input.h"
#include "RendererUtils.h"
#include "SaveManager.h"
#include "Tween.h"
#include "UITheme.h"

// Modern unified system menu for AVG: integrates save/load, settings, and other system functions
// Replaces fragmented PauseMenu, ConfigScreen, SaveLoadMenu components
class SystemMenu {
public:
    struct HoverSignature {
        enum class Type { 
            None, Nav, Slot, Slider, Toggle, Pager, DeleteSlot, ModalYes, ModalNo 
        };
        Type type = Type::None;
        int index = -1;

        bool operator==(const HoverSignature& o) const {
            return type == o.type && index == o.index;
        }
        bool operator!=(const HoverSignature& o) const {
            return !(*this == o);
        }
        bool IsValid() const {
            return type != Type::None;
        }
    };
    enum class Page { Settings, Save, Load };
    
    struct Result {
        enum Action { None, Resume, ReturnToTitle, Quit, SaveSlot, LoadSlot, DeleteSlot, ConfigChanged };
        Action action = None;
        int slotIndex = -1;
    };

    struct Layout {
        float navPanelWidth = 280.0f;
        float navStartY = 150.0f;
        float navGap = 65.0f;
        float navItemWidth = 280.0f;
        float navItemHeight = 50.0f;
        float navTitleOffset = 4.5f;
        float navQuitOffset = 5.5f;
        float titleX = 340.0f;
        float titleY = 40.0f;
        float slotStartX = 340.0f;
        float slotStartY = 120.0f;
        float slotWidth = 400.0f;
        float slotHeight = 140.0f;
        float slotGapX = 40.0f;
        float slotGapY = 30.0f;
        float slotLabelX = 16.0f;
        float slotLabelY = 6.0f;
        float thumbX = 16.0f;
        float thumbY = 36.0f;
        float thumbWidth = 160.0f;
        float thumbHeight = 90.0f;
        float infoX = 190.0f;
        float chapterY = 36.0f;
        float timeY = 80.0f;
        float emptyY = 60.0f;
        float sliderX = 400.0f;
        float sliderY = 200.0f;
        float sliderGapY = 120.0f;
        float sliderWidth = 500.0f;
        float sliderHeight = 6.0f;
        float sliderLabelYOffset = -40.0f;
        float sliderThumbWidth = 10.0f;
        float sliderThumbHeight = 26.0f;
        float modalMaxMessageWidth = 500.0f;
        float modalPadding = 40.0f;
        float modalButtonAreaHeight = 70.0f;
        float modalButtonWidth = 100.0f;
        float modalButtonHeight = 40.0f;
        float modalButtonGap = 50.0f;
        int systemTextPointSize = 20;
        int systemTextOutline = 1;
        std::string systemPanelTexturePath;
        float systemPanelSliceCorner = 18.0f;
        float systemOpenSlidePixels = 44.0f;
    };

    ~SystemMenu() = default;

    void Init(TTF_Font* font,
              int w,
              int h,
              SaveManager* saveMgr,
              vn::AssetManager* assets = nullptr,
              const std::string& fontPath = "",
              int fontPointSize = 22) {
        m_font = font;
        m_screenW = w;
        m_screenH = h;
        m_saveMgr = saveMgr;
        m_assets = assets;
        m_fontPath = fontPath;
        m_fontPointSize = fontPointSize;
        ApplyTextStyleFromLayout();
        RebuildLayout();
    }

    void Open(Page page, bool inGame) {
        if (!m_saveEnabled && page == Page::Save) {
            page = Page::Settings;
        }
        m_activePage = page;
        m_inGame = inGame;
        m_standaloneExitConfirm = false;
        m_currentPage = 0;
        m_menuClosing = false;
        m_pendingMenuResult = {Result::None, -1};
        
        m_alpha = 0.0f;
        m_slideOffsetX = std::max(0.0f, m_layout.systemOpenSlidePixels);
        m_slideTween.Start(&m_slideOffsetX,
                           m_slideOffsetX,
                           0.0f,
                           0.26f,
                           Ease::OutBack);
        m_modalType = ModalType::None;
        m_modalButtonRectsValid = false;
        m_capturedSlider = -1;
        SyncSliderMomentumFromConfig();
        RefreshSlots();
    }

    void SetSaveEnabled(bool enabled) {
        m_saveEnabled = enabled;
        if (!m_saveEnabled && m_activePage == Page::Save) {
            m_activePage = Page::Settings;
        }
    }

    void OpenExitConfirm(bool inGame) {
        Open(Page::Settings, inGame);
        m_standaloneExitConfirm = true;
        ShowModalInternal(ModalType::Quit, Tr("ui_exit_confirm", "Exit the game?"));
    }

    void SetTranslator(std::function<std::string(const std::string&)> translator) {
        m_translate = std::move(translator);
        if (m_screenW > 0 && m_screenH > 0) {
            RebuildLayout();
        }
    }

    void RefreshStrings() {
        if (m_screenW > 0 && m_screenH > 0) {
            RebuildLayout();
        }

        if (m_modalType == ModalType::OverwriteSave) {
            m_modalMsg = Tr("ui_overwrite_save_confirm", "Overwrite this save data?");
        } else if (m_modalType == ModalType::LoadGame) {
            m_modalMsg = Tr("ui_load_confirm", "Load game? Unsaved progress will be lost.");
        } else if (m_modalType == ModalType::DeleteSlot) {
            m_modalMsg = Tr("ui_delete_save_confirm", "Delete this save data? This cannot be undone.");
        } else if (m_modalType == ModalType::ReturnTitle) {
            m_modalMsg = Tr("ui_return_title_confirm", "Return to title? Unsaved progress will be lost.");
        } else if (m_modalType == ModalType::Quit) {
            m_modalMsg = Tr("ui_exit_confirm", "Exit the game?");
        }

        m_cachedModalMsg.clear();
        m_cachedModalText = {};
    }

    void Close() {
        m_hoveredNav = -1;
        m_hoveredSlot = -1;
        m_hoveredSlider = -1;
        m_capturedSlider = -1;
        m_hoveredDeleteSlot = -1;
        m_modalType = ModalType::None;
        m_standaloneExitConfirm = false;
        m_modalMsg.clear();
        m_modalButtonRectsValid = false;
        m_cachedModalMsg.clear();
        m_cachedModalWrapWidth = 0.0f;
        m_cachedModalText = {};
        m_cachedPanelTexture.reset();
    }

    void ReleaseResources() {
        Close();
        m_styleFontHandle.reset();
        m_font = nullptr;
        m_assets = nullptr;
        m_saveMgr = nullptr;
    }

    void SetConfigValues(float textSpeed,
                         float bgm,
                         float sfx,
                         bool animatedSpritesEnabled,
                         bool skipUnseenEnabled,
                         bool wheelAdvanceEnabled,
                         bool voiceBleedEnabled,
                         bool fullscreenEnabled,
                         const std::string& localeCode = "") {
        m_textSpeed = textSpeed;
        m_bgmVolume = bgm;
        m_sfxVolume = sfx;
        m_animatedSpritesEnabled = animatedSpritesEnabled;
        m_skipUnseenEnabled = skipUnseenEnabled;
        m_wheelAdvanceEnabled = wheelAdvanceEnabled;
        m_voiceBleedEnabled = voiceBleedEnabled;
        m_fullscreenEnabled = fullscreenEnabled;
        if (!localeCode.empty()) {
            m_localeCode = localeCode;
        }
        SyncSliderMomentumFromConfig();
    }

    void SetLocaleCode(const std::string& localeCode) {
        if (!localeCode.empty()) {
            m_localeCode = localeCode;
        }
    }

    float GetTextSpeed() const { return m_textSpeed; }
    float GetBGM() const { return m_bgmVolume; }
    float GetSFX() const { return m_sfxVolume; }
    bool GetAnimatedSpritesEnabled() const { return m_animatedSpritesEnabled; }
    bool GetSkipUnseenEnabled() const { return m_skipUnseenEnabled; }
    bool GetWheelAdvanceEnabled() const { return m_wheelAdvanceEnabled; }
    bool GetVoiceBleedEnabled() const { return m_voiceBleedEnabled; }
    bool GetFullscreenEnabled() const { return m_fullscreenEnabled; }
    const std::string& GetLocaleCode() const { return m_localeCode; }
    bool HasActiveModal() const { return m_modalType != ModalType::None; }
    HoverSignature GetHoverSignature(float mx, float my) const {
        if (m_usingFocusNavigation) {
            SDL_FRect unused{};
            if (GetFocusRect(m_focusedControl, unused)) {
                return m_focusedControl;
            }
        }

        if (m_modalType != ModalType::None) {
            SDL_FRect yesRect{};
            SDL_FRect noRect{};
            GetModalButtonRects(yesRect, noRect);
            if (Draw::Contains(yesRect, mx, my)) {
                return { HoverSignature::Type::ModalYes, 0 };
            }
            if (Draw::Contains(noRect, mx, my)) {
                return { HoverSignature::Type::ModalNo, 0 };
            }
            return {};
        }

        mx -= m_slideOffsetX;

        if (m_hoveredNav >= 0) return { HoverSignature::Type::Nav, m_hoveredNav };
        if (m_hoveredSlot >= 0) return { HoverSignature::Type::Slot, m_hoveredSlot };
        if (m_hoveredSlider >= 0) return { HoverSignature::Type::Slider, m_hoveredSlider };
        if (m_hoveredDeleteSlot >= 0) return { HoverSignature::Type::DeleteSlot, m_hoveredDeleteSlot };

        if (Draw::Contains(m_animatedToggleRect, mx, my)) {
            return { HoverSignature::Type::Toggle, 0 };
        }
        if (Draw::Contains(m_skipToggleRect, mx, my)) {
            return { HoverSignature::Type::Toggle, 1 };
        }
        if (Draw::Contains(m_voiceBleedToggleRect, mx, my)) {
            return { HoverSignature::Type::Toggle, 2 };
        }
        if (Draw::Contains(m_wheelAdvanceToggleRect, mx, my)) {
            return { HoverSignature::Type::Toggle, 3 };
        }
        if (Draw::Contains(m_fullscreenToggleRect, mx, my)) {
            return { HoverSignature::Type::Toggle, 4 };
        }
        if (Draw::Contains(m_localeRect, mx, my)) {
            return { HoverSignature::Type::Toggle, 5 };
        }

        if (m_activePage == Page::Save || m_activePage == Page::Load) {
            const int pageCount = GetVisiblePageCount();
            const bool canPrevPage = m_currentPage > 0;
            const bool canNextPage = m_currentPage < (pageCount - 1);
            if (canPrevPage &&
                Draw::Contains(m_prevPageRect, mx, my)) {
                return { HoverSignature::Type::Pager, 0 };
            }
            if (canNextPage &&
                Draw::Contains(m_nextPageRect, mx, my)) {
                return { HoverSignature::Type::Pager, 1 };
            }
        }

        return {};
    }

    void RefreshSlots() {
        if (!m_saveMgr) return;
        m_currentPage = std::clamp(m_currentPage, 0, GetVisiblePageCount() - 1);
        m_slots = m_saveMgr->ListSlots(kSlotsPerPage, m_currentPage);
    }

    Result Update(float dt, float mx, float my, bool clicked, bool rightClicked, bool escPressed, bool isHeld, bool deletePressed) {
        return Update(dt, mx, my, clicked, rightClicked, escPressed, isHeld, deletePressed, {});
    }

    Result Update(float dt,
                  float mx,
                  float my,
                  bool clicked,
                  bool rightClicked,
                  bool escPressed,
                  bool isHeld,
                  bool deletePressed,
                  const Input::FocusInput& focus) {
        int focusedSliderStep = 0;
        m_usingFocusNavigation = focus.active;
        if (focus.active) {
            UpdateFocusNavigation(focus, focusedSliderStep);
            SDL_FRect focusRect{};
            if (GetFocusRect(m_focusedControl, focusRect)) {
                const bool modalFocus =
                    m_focusedControl.type == HoverSignature::Type::ModalYes ||
                    m_focusedControl.type == HoverSignature::Type::ModalNo;
                mx = focusRect.x + focusRect.w * 0.5f + (modalFocus ? 0.0f : m_slideOffsetX);
                my = focusRect.y + focusRect.h * 0.5f;
            }
            clicked = clicked || focus.confirm;
            rightClicked = rightClicked || focus.cancel;
            deletePressed = deletePressed || focus.deletePressed;
        }

        m_lastMouseX = mx;
        m_lastMouseY = my;
        m_isLeftHeld = isHeld; 

        if (m_menuClosing) {
            m_alpha -= (1.0f / 0.15f) * dt; 
            if (m_alpha < 0.0f) m_alpha = 0.0f;
        } else {
            m_alpha += (1.0f - m_alpha) * 15.0f * dt;
        }
        
        m_slideTween.Update(std::max(0.0f, dt));
        m_modalAlphaTween.Update(std::max(0.0f, dt));
        m_modalOffsetTween.Update(std::max(0.0f, dt));
        
        Result res;

        if (m_menuClosing) {
            if (m_alpha <= 0.0f && !m_slideTween.IsRunning()) {
                m_menuClosing = false;
                Result finalRes = m_pendingMenuResult;
                m_pendingMenuResult = {Result::None, -1};
                return finalRes; 
            }
            return res; 
        }

        if ((rightClicked || escPressed) && m_modalType == ModalType::None) {
            CloseMenuWithAnim({Result::Resume, -1});
            return res;
        }

        if (m_modalType != ModalType::None) {
            if (m_modalClosing) {
             
                if (!m_modalAlphaTween.IsRunning()) {
                    m_modalType = ModalType::None;
                    m_modalClosing = false;
                    Result finalRes = m_pendingModalResult;
                    m_pendingModalResult = {Result::None, -1};
                    
                    if (m_standaloneExitConfirm && finalRes.action == Result::None) {
                        m_standaloneExitConfirm = false;
                        CloseMenuWithAnim({Result::Resume, -1});
                        return res; 
                    } 
                    else if (finalRes.action == Result::LoadSlot || finalRes.action == Result::ReturnToTitle || finalRes.action == Result::Quit) {
                        CloseMenuWithAnim(finalRes);
                        return res;
                    }
                    return finalRes; 
                }
                return res; 
            }

            if (rightClicked || escPressed) {
                CloseModalWithAnim({Result::None, -1});
                return res;
            }
            if (clicked) {
                SDL_FRect btnYes{}, btnNo{};
                GetModalButtonRects(btnYes, btnNo);

                if (UI::Button(btnYes, mx, my, clicked).activated) {
                    Result actionRes{Result::None, -1};
                    if (m_modalType == ModalType::OverwriteSave) actionRes = {Result::SaveSlot, m_pendingSlot};
                    else if (m_modalType == ModalType::LoadGame) actionRes = {Result::LoadSlot, m_pendingSlot};
                    else if (m_modalType == ModalType::DeleteSlot) actionRes = {Result::DeleteSlot, m_pendingSlot};
                    else if (m_modalType == ModalType::ReturnTitle) actionRes = {Result::ReturnToTitle, -1};
                    else if (m_modalType == ModalType::Quit) actionRes = {Result::Quit, -1};
                    
                    CloseModalWithAnim(actionRes);
                    return res;
                }
                if (UI::Button(btnNo, mx, my, clicked).activated) {
                    CloseModalWithAnim({Result::None, -1});
                    return res;
                }
            }
            return res;
        }

        mx -= m_slideOffsetX;

        if (!m_saveEnabled && m_activePage == Page::Save) {
            m_activePage = Page::Settings;
        }

        m_hoveredNav = -1;
        m_hoveredSlot = -1;
        m_hoveredSlider = -1;
        m_hoveredDeleteSlot = -1;

        if (!isHeld) {
            m_capturedSlider = -1;
        }

        for (int i = 0; i < static_cast<int>(m_navItems.size()); ++i) {
            auto& nav = m_navItems[i];
            const bool saveDisabled = (m_inGame && nav.kind == NavKind::Save && !m_saveEnabled);
            
            if (!m_inGame && (nav.kind == NavKind::Save || nav.kind == NavKind::Continue)) {
                continue;
            }

            if (saveDisabled) {
                continue;
            }

            const UI::ControlSample navSample = UI::Button(nav.rect, mx, my, clicked);
            if (navSample.hovered) {
                m_hoveredNav = i;
                if (navSample.activated) {
                    if (nav.directAction == Result::ReturnToTitle && m_inGame) {
                        ShowModalInternal(ModalType::ReturnTitle, Tr("ui_return_title_confirm", "Return to title? Unsaved progress will be lost."));
                        return res;
                    } else if (nav.directAction == Result::Quit) {
                        ShowModalInternal(ModalType::Quit, Tr("ui_exit_confirm", "Exit the game?"));
                        return res;
                    } else if (nav.directAction != Result::None) {
                        CloseMenuWithAnim({nav.directAction, -1});
                        return res;
                    } else {
                        m_activePage = nav.targetPage;
                        if (m_activePage == Page::Save || m_activePage == Page::Load) {
                            m_currentPage = std::clamp(m_currentPage, 0, GetVisiblePageCount() - 1);
                            RefreshSlots();
                        }
                    }
                }
            }
        }

        if (m_activePage == Page::Save || m_activePage == Page::Load) {
            const int pageCount = GetVisiblePageCount();
            const bool canPrevPage = m_currentPage > 0;
            const bool canNextPage = m_currentPage < (pageCount - 1);
            if (clicked) {
                if (UI::Button(m_prevPageRect, mx, my, clicked).activated) {
                    if (canPrevPage) {
                        --m_currentPage;
                        RefreshSlots();
                    }
                    return res;
                }
                if (UI::Button(m_nextPageRect, mx, my, clicked).activated) {
                    if (canNextPage) {
                        ++m_currentPage;
                        RefreshSlots();
                    }
                    return res;
                }
            }

            for (int i = 0; i < kSlotsPerPage; ++i) {
                const auto& r = m_slotRects[i];
                const bool exists = (i < static_cast<int>(m_slots.size()) && m_slots[i].exists);
                const int absoluteSlot = m_currentPage * kSlotsPerPage + i + 1;
                const bool isAutoSaveSlot = (absoluteSlot == 1);

                const float delSize = 28.0f;
                const SDL_FRect delRect = { r.x + r.w - delSize - 12.0f, r.y + 12.0f, delSize, delSize };

                const UI::ControlSample deleteSample = UI::Button(delRect, mx, my, clicked, !exists || isAutoSaveSlot);
                const UI::ControlSample slotSample = UI::Button(r, mx, my, clicked);

                if (deleteSample.hovered) {
                    m_hoveredDeleteSlot = i;
                    if (deleteSample.activated) {
                        ShowModalInternal(ModalType::DeleteSlot, Tr("ui_delete_save_confirm", "Delete this save data? This cannot be undone."), absoluteSlot);
                    }
                } else if (slotSample.hovered) {
                    m_hoveredSlot = i;
                    if (slotSample.activated) {
                        if (m_activePage == Page::Load && !exists) {
                            continue;
                        }

                        if (m_activePage == Page::Save) {
                            if (isAutoSaveSlot) {
                                continue;
                            }
                            if (exists) {
                                ShowModalInternal(ModalType::OverwriteSave, Tr("ui_overwrite_save_confirm", "Overwrite this save data?"), absoluteSlot);
                            } else {
                                res.action = Result::SaveSlot;
                                res.slotIndex = absoluteSlot;
                                return res;
                            }
                        } else if (m_activePage == Page::Load && exists) {
                            if (!m_inGame) {
                                res.action = Result::LoadSlot;
                                res.slotIndex = absoluteSlot;
                                return res;
                            } else {
                                ShowModalInternal(ModalType::LoadGame, Tr("ui_load_confirm", "Load game? Unsaved progress will be lost."), absoluteSlot);
                            }
                        }
                    }
                }
            }

            if (deletePressed && m_hoveredSlot >= 0 && m_hoveredSlot < static_cast<int>(m_slots.size()) && m_slots[m_hoveredSlot].exists) {
                const int absoluteSlot = m_currentPage * kSlotsPerPage + m_hoveredSlot + 1;
                if (absoluteSlot != 1) {
                    ShowModalInternal(ModalType::DeleteSlot, Tr("ui_delete_save_confirm", "Delete this save data? This cannot be undone."), absoluteSlot);
                }
            }
        } else if (m_activePage == Page::Settings) {
            bool configChanged = false;
            if (focus.active && focusedSliderStep != 0 &&
                m_focusedControl.type == HoverSignature::Type::Slider &&
                m_focusedControl.index >= 0 && m_focusedControl.index < 3) {
                const int slider = m_focusedControl.index;
                const float next = std::clamp(
                    m_sliderMomentumT[static_cast<std::size_t>(slider)] + focusedSliderStep * 0.05f,
                    0.0f,
                    1.0f);
                m_sliderVelocity[static_cast<std::size_t>(slider)] = 0.0f;
                m_sliderMomentumT[static_cast<std::size_t>(slider)] = next;
                ApplySliderNormalized(slider, next);
                configChanged = true;
            }

            for (int i = 0; i < 3; ++i) {
                const auto& r = m_sliders[i].track;
                float sampledT = m_sliderMomentumT[static_cast<std::size_t>(i)];
                const UI::ControlSample sliderSample = UI::Slider(r,
                                                                  mx,
                                                                  my,
                                                                  clicked,
                                                                  isHeld,
                                                                  m_capturedSlider == i,
                                                                  sampledT);
                if (sliderSample.hovered) {
                    m_hoveredSlider = i;
                    if (clicked) {
                        m_capturedSlider = i;
                    }
                }

                if (m_capturedSlider == i && isHeld) {
                    const float t = sampledT;
                    const float safeDt = std::max(0.0001f, dt);
                    m_sliderVelocity[static_cast<std::size_t>(i)] =
                        (t - m_sliderMomentumT[static_cast<std::size_t>(i)]) / safeDt;
                    m_sliderMomentumT[static_cast<std::size_t>(i)] = t;
                    ApplySliderNormalized(i, t);
                    configChanged = true;
                } else {
                    float velocity = m_sliderVelocity[static_cast<std::size_t>(i)];
                    if (std::fabs(velocity) > 0.001f) {
                        float t = m_sliderMomentumT[static_cast<std::size_t>(i)] + velocity * dt;
                        if (t < 0.0f || t > 1.0f) {
                            t = std::clamp(t, 0.0f, 1.0f);
                            velocity *= -0.18f;
                        }
                        velocity *= std::exp(-m_sliderInertiaFriction * std::max(0.0f, dt));
                        if (std::fabs(velocity) < 0.01f) {
                            velocity = 0.0f;
                        }
                        m_sliderVelocity[static_cast<std::size_t>(i)] = velocity;
                        m_sliderMomentumT[static_cast<std::size_t>(i)] = t;
                        ApplySliderNormalized(i, t);
                        configChanged = true;
                    }
                }
            }

            configChanged = UI::Toggle(m_animatedToggleRect, mx, my, clicked, m_animatedSpritesEnabled).activated || configChanged;
            configChanged = UI::Toggle(m_skipToggleRect, mx, my, clicked, m_skipUnseenEnabled).activated || configChanged;
            configChanged = UI::Toggle(m_voiceBleedToggleRect, mx, my, clicked, m_voiceBleedEnabled).activated || configChanged;
            configChanged = UI::Toggle(m_wheelAdvanceToggleRect, mx, my, clicked, m_wheelAdvanceEnabled).activated || configChanged;
            configChanged = UI::Toggle(m_fullscreenToggleRect, mx, my, clicked, m_fullscreenEnabled).activated || configChanged;

            if (UI::Button(m_localeRect, mx, my, clicked).activated) {
                CycleLocaleCode();
                configChanged = true;
            }

            if (configChanged) res.action = Result::ConfigChanged;
        }

        for (int i = 0; i < static_cast<int>(m_navHoverScales.size()); ++i) {
            const float target = 1.0f;
            m_navHoverScales[static_cast<std::size_t>(i)] +=
                (target - m_navHoverScales[static_cast<std::size_t>(i)]) * std::min(1.0f, dt * 16.0f);
        }

        return res;
    }

    void Render(SDL_Renderer* renderer) {
        if (!m_font || !renderer) return;
        m_renderOffsetX = m_slideOffsetX;
        Uint8 alphaU8 = static_cast<Uint8>(m_alpha * 255);
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

        SDL_SetRenderDrawColor(renderer,
                       ui::theme::kBlackAbyss.r,
                       ui::theme::kBlackAbyss.g,
                       ui::theme::kBlackAbyss.b,
                       static_cast<Uint8>(m_alpha * 220));
        SDL_FRect screen = { 0, 0, static_cast<float>(m_screenW), static_cast<float>(m_screenH) };
        SDL_RenderFillRect(renderer, &screen);

        if (!m_standaloneExitConfirm) {
            SDL_FRect leftPanel = ShiftRect(SDL_FRect{ 0, 0, m_layout.navPanelWidth, static_cast<float>(m_screenH) });
            bool usedTexturedPanel = false;
            if (m_assets && !m_layout.systemPanelTexturePath.empty()) {
                // Load panel texture once and cache it to avoid per-frame disk access
                if (!m_cachedPanelTexture) {
                    m_cachedPanelTexture = m_assets->GetTexture(m_layout.systemPanelTexturePath);
                }
                if (m_cachedPanelTexture) {
                    usedTexturedPanel = Draw::Texture9Slice(
                        renderer,
                        m_cachedPanelTexture.get(),
                        leftPanel,
                        m_layout.systemPanelSliceCorner,
                        static_cast<Uint8>(230.0f * m_alpha));
                }
            }
            if (!usedTexturedPanel) {
                SDL_SetRenderDrawColor(renderer,
                                       ui::theme::kBlackBase.r,
                                       ui::theme::kBlackBase.g,
                                       ui::theme::kBlackBase.b,
                                       alphaU8);
                SDL_RenderFillRect(renderer, &leftPanel);
            }
            SDL_SetRenderDrawColor(renderer,
                                   ui::theme::kGrayGloom.r,
                                   ui::theme::kGrayGloom.g,
                                   ui::theme::kGrayGloom.b,
                                   static_cast<Uint8>(alphaU8 * 0.85f));
            SDL_RenderLine(renderer,
                           leftPanel.x + leftPanel.w - 1.0f,
                           leftPanel.y + 18.0f,
                           leftPanel.x + leftPanel.w - 1.0f,
                           leftPanel.y + leftPanel.h - 18.0f);

            for (int i = 0; i < static_cast<int>(m_navItems.size()); ++i) {
                const auto& nav = m_navItems[i];
                if (!m_inGame && (nav.kind == NavKind::Save || nav.kind == NavKind::Continue)) {
                    continue;
                }

                const bool saveDisabled = (m_inGame && nav.kind == NavKind::Save && !m_saveEnabled);

                bool isHovered = (i == m_hoveredNav);
                bool isActive = (nav.targetPage == m_activePage && nav.directAction == Result::None);
                if (saveDisabled) {
                    isHovered = false;
                    isActive = false;
                }

                if (isActive) {
                    SDL_SetRenderDrawColor(renderer,
                                           ui::theme::kAccentPrimary.r,
                                           ui::theme::kAccentPrimary.g,
                                           ui::theme::kAccentPrimary.b,
                                           alphaU8);
                    SDL_FRect indicator = ShiftRect(SDL_FRect{ 0, nav.rect.y + 10, 4, nav.rect.h - 20 });
                    SDL_RenderFillRect(renderer, &indicator);
                }

                SDL_Color textColor = ui::theme::WithAlpha(ui::theme::kTextSecondary, alphaU8);
                float textOffsetX = 40.0f;
                if (saveDisabled) {
                    textColor = ui::theme::WithAlpha(ui::theme::kTextDisabled, static_cast<Uint8>(alphaU8 * 0.8f));
                } else if (isHovered) {
                    textColor = ui::theme::WithAlpha(ui::theme::kBluePrimary, alphaU8);
                    textOffsetX = 48.0f;
                } else if (isActive) {
                    textColor = ui::theme::WithAlpha(ui::theme::kAccentPrimary, alphaU8);
                }

                const float navScale = 1.0f;
                DrawTextScaledLeftCentered(renderer,
                                           nav.label,
                                           textOffsetX,
                                           nav.rect.y + nav.rect.h * 0.5f,
                                           navScale,
                                           textColor);
            }

            const std::string titleStr = (m_activePage == Page::Save) ? Tr("ui_save_data_upper", "SAVE DATA") :
                                         (m_activePage == Page::Load) ? Tr("ui_load_data_upper", "LOAD DATA") : Tr("ui_config_upper", "CONFIG");
            DrawTextScaled(renderer,
                           titleStr,
                           m_layout.titleX,
                           m_layout.titleY,
                           1.10f,
                           ui::theme::WithAlpha(ui::theme::kAccentSoft, static_cast<Uint8>(alphaU8 * 0.95f)));

            if (m_activePage == Page::Save || m_activePage == Page::Load) {
                RenderSlots(renderer, alphaU8);
            } else if (m_activePage == Page::Settings) {
                RenderSettings(renderer, alphaU8);
            }
        }

        if (m_modalType != ModalType::None) {
            const float savedOffsetX = m_renderOffsetX;
            m_renderOffsetX = 0.0f;
            
            SDL_SetRenderDrawColor(renderer,
                                   ui::theme::kBlackAbyss.r,
                                   ui::theme::kBlackAbyss.g,
                                   ui::theme::kBlackAbyss.b,
                                   static_cast<Uint8>(190 * m_modalAlpha));
            SDL_RenderFillRect(renderer, &screen);

            const std::string& msg = m_modalMsg;
            const float maxMsgWidth = m_layout.modalMaxMessageWidth;
            CachedText modalText = GetWrappedText(renderer, msg, maxMsgWidth);
            float msgWidth = modalText.w > 0.0f ? modalText.w : 100.0f;
            float msgHeight = modalText.h > 0.0f ? modalText.h : 30.0f;

            const float padding = m_layout.modalPadding;
            const float buttonAreaHeight = m_layout.modalButtonAreaHeight;
            const float boxW = 540.0f;
            const float boxH = 260.0f;
            
            float boxX = m_screenW / 2.0f - boxW / 2.0f;
            float boxY = m_screenH / 2.0f - boxH / 2.0f + m_modalOffsetY; 
            SDL_FRect box = { boxX, boxY, boxW, boxH };

            Uint8 globalAlpha = static_cast<Uint8>(255 * m_modalAlpha);

            Draw::Rect(renderer,
                       box,
                       ui::theme::WithAlpha(ui::theme::kBlackElevated, globalAlpha),
                       ui::theme::WithAlpha(ui::theme::kGrayMist, static_cast<Uint8>(255 * m_modalAlpha)));

            float msgX = boxX + (boxW - msgWidth) / 2.0f;
            float msgY = boxY + (boxH - buttonAreaHeight - msgHeight) / 2.0f; 

            if (modalText.texture) {
                SDL_Texture* msgTex = modalText.texture.get();
                SDL_SetTextureColorMod(msgTex, 245, 245, 245);
                SDL_SetTextureAlphaMod(msgTex, globalAlpha);
                SDL_FRect msgDst = { msgX, msgY, msgWidth, msgHeight };
                SDL_RenderTexture(renderer, msgTex, nullptr, &msgDst);
            }

            const float mx = m_lastMouseX;
            const float my = m_lastMouseY;

            const float btnWidth = m_layout.modalButtonWidth;
            const float btnHeight = m_layout.modalButtonHeight;
            const float btnGap = m_layout.modalButtonGap;
            const float totalBtnWidth = btnWidth * 2.0f + btnGap;
            float btnStartX = boxX + (boxW - totalBtnWidth) / 2.0f;
            float btnY = boxY + boxH - padding - btnHeight;

            SDL_FRect btnYes = { btnStartX, btnY, btnWidth, btnHeight };
            SDL_FRect btnNo  = { btnStartX + btnWidth + btnGap, btnY, btnWidth, btnHeight };
            
            m_modalYesRect = { btnStartX, m_screenH / 2.0f - boxH / 2.0f + boxH - padding - btnHeight, btnWidth, btnHeight };
            m_modalNoRect = { btnStartX + btnWidth + btnGap, m_screenH / 2.0f - boxH / 2.0f + boxH - padding - btnHeight, btnWidth, btnHeight };
            m_modalButtonRectsValid = true;

            bool hoverYes = Draw::Contains(m_modalYesRect, mx, my);
            bool hoverNo  = Draw::Contains(m_modalNoRect, mx, my);

            float pressOffsetYes = (hoverYes && m_isLeftHeld) ? 2.0f : 0.0f;
            float pressOffsetNo = (hoverNo && m_isLeftHeld) ? 2.0f : 0.0f;
            
            btnYes.y += pressOffsetYes;
            btnNo.y += pressOffsetNo;

            SDL_Color yesBg = hoverYes ? ui::theme::WithAlpha(ui::theme::kBluePrimary, globalAlpha) 
                                       : ui::theme::WithAlpha(ui::theme::kBlueStrong, static_cast<Uint8>(220 * m_modalAlpha));
            SDL_Color yesBorder = hoverYes ? ui::theme::WithAlpha(ui::theme::kAccentPrimary, globalAlpha) 
                                           : ui::theme::WithAlpha(ui::theme::kAccentSoft, static_cast<Uint8>(150 * m_modalAlpha));
            SDL_Color yesText = hoverYes ? SDL_Color{255, 255, 255, globalAlpha} : SDL_Color{230, 230, 230, globalAlpha};

            Draw::Rect(renderer, btnYes, yesBg, yesBorder);
            DrawTextCentered(renderer, Tr("ui_yes", "Yes"), btnYes.x + btnWidth / 2.0f, btnYes.y + btnHeight / 2.0f, yesText);

            SDL_Color noBg = hoverNo ? ui::theme::WithAlpha(ui::theme::kGrayGloom, globalAlpha) 
                                     : ui::theme::WithAlpha(ui::theme::kBlackPanel, static_cast<Uint8>(180 * m_modalAlpha));
            SDL_Color noBorder = hoverNo ? ui::theme::WithAlpha(ui::theme::kGrayMist, static_cast<Uint8>(200 * m_modalAlpha)) 
                                         : ui::theme::WithAlpha(ui::theme::kGraySteel, static_cast<Uint8>(120 * m_modalAlpha));
            SDL_Color noText = hoverNo ? SDL_Color{240, 240, 240, globalAlpha} : SDL_Color{150, 150, 150, globalAlpha};

            Draw::Rect(renderer, btnNo, noBg, noBorder);
            DrawTextCentered(renderer, Tr("ui_no", "No"), btnNo.x + btnWidth / 2.0f, btnNo.y + btnHeight / 2.0f, noText);
            
            m_renderOffsetX = savedOffsetX;
        }
    }

private:
    static constexpr int kSlotsPerPage = 6;
    static constexpr int kMaxSavePages = SaveManager::kMaxSavePages;

    enum class NavKind {
        Continue,
        Save,
        Load,
        Config,
        Title,
        Exit,
    };

    std::string Tr(const std::string& key, const char* fallback) const {
        if (m_translate) {
            const std::string translated = m_translate(key);
            if (translated != "[" + key + "]") {
                return translated;
            }
        }
        return fallback;
    }

    std::string LocaleDisplayName() const {
        if (m_localeCode == "en") return Tr("lang_en", "English");
        if (m_localeCode == "ja") return Tr("lang_ja", "Japanese");
        return Tr("lang_zh", "Chinese");
    }

    void CycleLocaleCode() {
        if (m_localeCode == "zh") {
            m_localeCode = "en";
        } else if (m_localeCode == "en") {
            m_localeCode = "ja";
        } else {
            m_localeCode = "zh";
        }
    }

    enum class ModalType { None, OverwriteSave, LoadGame, DeleteSlot, ReturnTitle, Quit };

    void ShowModalInternal(ModalType type, const std::string& msg, int slot = -1) {
        m_modalType = type;
        m_modalMsg = msg;
        m_pendingSlot = slot;
        m_modalButtonRectsValid = false;
        
        m_modalClosing = false;
        m_pendingModalResult = {Result::None, -1};
        
        m_modalAlpha = 0.0f;
        m_modalOffsetY = 30.0f; 
        
        m_modalAlphaTween.Start(&m_modalAlpha, 0.0f, 1.0f, 0.15f, Ease::OutQuad);
        m_modalOffsetTween.Start(&m_modalOffsetY, 30.0f, 0.0f, 0.25f, Ease::OutBack);
    }

    struct NavItem {
        NavKind kind = NavKind::Continue;
        std::string label;
        Page targetPage;
        Result::Action directAction;
        SDL_FRect rect;
    };

    struct SliderUI {
        std::string label;
        SDL_FRect track;
    };

    int GetVisiblePageCount() const {
        if (!m_saveMgr) {
            return 1;
        }
        return kMaxSavePages;
    }

    float GetSliderNormalized(int sliderIndex) const {
        if (sliderIndex == 0) return std::clamp((m_textSpeed - 5.0f) / 115.0f, 0.0f, 1.0f);
        if (sliderIndex == 1) return std::clamp(m_bgmVolume, 0.0f, 1.0f);
        if (sliderIndex == 2) return std::clamp(m_sfxVolume, 0.0f, 1.0f);
        return 0.0f;
    }

    void ApplySliderNormalized(int sliderIndex, float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        if (sliderIndex == 0) m_textSpeed = 5.0f + t * 115.0f;
        if (sliderIndex == 1) m_bgmVolume = t;
        if (sliderIndex == 2) m_sfxVolume = t;
    }

    void SyncSliderMomentumFromConfig() {
        for (int i = 0; i < 3; ++i) {
            m_sliderMomentumT[static_cast<std::size_t>(i)] = GetSliderNormalized(i);
            m_sliderVelocity[static_cast<std::size_t>(i)] = 0.0f;
        }
    }

    void RebuildLayout() {
        m_navItems = {
            { NavKind::Continue, Tr("ui_continue", "Continue"), Page::Settings, Result::Resume, SDL_FRect{ 0.0f, m_layout.navStartY + m_layout.navGap * 0.0f, m_layout.navItemWidth, m_layout.navItemHeight } },
            { NavKind::Save, Tr("ui_save", "Save"), Page::Save, Result::None, SDL_FRect{ 0.0f, m_layout.navStartY + m_layout.navGap * 1.0f, m_layout.navItemWidth, m_layout.navItemHeight } },
            { NavKind::Load, Tr("ui_load", "Load"), Page::Load, Result::None, SDL_FRect{ 0.0f, m_layout.navStartY + m_layout.navGap * 2.0f, m_layout.navItemWidth, m_layout.navItemHeight } },
            { NavKind::Config, Tr("ui_config", "Config"), Page::Settings, Result::None, SDL_FRect{ 0.0f, m_layout.navStartY + m_layout.navGap * 3.0f, m_layout.navItemWidth, m_layout.navItemHeight } },
            { NavKind::Title, Tr("ui_title", "Title"), Page::Settings, Result::ReturnToTitle, SDL_FRect{ 0.0f, m_layout.navStartY + m_layout.navGap * m_layout.navTitleOffset, m_layout.navItemWidth, m_layout.navItemHeight } },
            { NavKind::Exit, Tr("ui_exit", "Exit"), Page::Settings, Result::Quit, SDL_FRect{ 0.0f, m_layout.navStartY + m_layout.navGap * m_layout.navQuitOffset, m_layout.navItemWidth, m_layout.navItemHeight } }
        };

        for (int i = 0; i < kSlotsPerPage; ++i) {
            const int col = i % 2;
            const int row = i / 2;
            m_slotRects[i] = SDL_FRect{
                m_layout.slotStartX + col * (m_layout.slotWidth + m_layout.slotGapX),
                m_layout.slotStartY + row * (m_layout.slotHeight + m_layout.slotGapY),
                m_layout.slotWidth,
                m_layout.slotHeight
            };
        }

        const float sliderX = m_layout.sliderX;
        const float sliderY = m_layout.sliderY;
        const float sliderGapY = m_layout.sliderGapY;
        const float sliderWidth = m_layout.sliderWidth;

        m_sliders[0] = { Tr("ui_text_speed", "Text Speed"), SDL_FRect{ sliderX, sliderY, sliderWidth, m_layout.sliderHeight } };
        m_sliders[1] = { Tr("ui_bgm_volume", "BGM Volume"), SDL_FRect{ sliderX, sliderY + sliderGapY, sliderWidth, m_layout.sliderHeight } };
        m_sliders[2] = { Tr("ui_sfx_volume", "SFX Volume"), SDL_FRect{ sliderX, sliderY + sliderGapY * 2.0f, sliderWidth, m_layout.sliderHeight } };

        const float toggleRowTop = m_sliders[2].track.y + 80.0f;
        const float toggleRowHeight = 40.0f;
        const float toggleRowGap = 55.0f;
        const float toggleColumnGap = 40.0f;
        const float toggleColumnWidth = (sliderWidth - toggleColumnGap) * 0.5f;
        const float rightColumnX = sliderX + toggleColumnWidth + toggleColumnGap;

        m_skipToggleRect = { sliderX, toggleRowTop, toggleColumnWidth, toggleRowHeight };
        m_voiceBleedToggleRect = { sliderX, toggleRowTop + toggleRowGap, toggleColumnWidth, toggleRowHeight };
        m_localeRect = { sliderX, toggleRowTop + toggleRowGap * 2.0f, toggleColumnWidth, toggleRowHeight };
        m_animatedToggleRect = { rightColumnX, toggleRowTop, toggleColumnWidth, toggleRowHeight };
        m_wheelAdvanceToggleRect = { rightColumnX, toggleRowTop + toggleRowGap, toggleColumnWidth, toggleRowHeight };
        m_fullscreenToggleRect = { rightColumnX, toggleRowTop + toggleRowGap * 2.0f, toggleColumnWidth, toggleRowHeight };
    }

    void GetModalButtonRects(SDL_FRect& outYes, SDL_FRect& outNo) const {
        if (m_modalButtonRectsValid) {
            outYes = m_modalYesRect;
            outNo = m_modalNoRect;
            return;
        }

        const float boxW = 540.0f;
        const float boxH = 260.0f;
        const float boxX = m_screenW * 0.5f - boxW * 0.5f;
        const float boxY = m_screenH * 0.5f - boxH * 0.5f;
        
        const float totalBtnWidth = m_layout.modalButtonWidth * 2.0f + m_layout.modalButtonGap;
        const float btnStartX = boxX + (boxW - totalBtnWidth) * 0.5f;
        const float btnY = boxY + boxH - m_layout.modalPadding - m_layout.modalButtonHeight;
        
        outYes = { btnStartX, btnY, m_layout.modalButtonWidth, m_layout.modalButtonHeight };
        outNo = { btnStartX + m_layout.modalButtonWidth + m_layout.modalButtonGap, btnY, m_layout.modalButtonWidth, m_layout.modalButtonHeight };
    }

    [[nodiscard]] bool IsNavItemEnabled(int index) const {
        if (index < 0 || index >= static_cast<int>(m_navItems.size())) {
            return false;
        }

        const NavItem& nav = m_navItems[index];
        if (!m_inGame && (nav.kind == NavKind::Save || nav.kind == NavKind::Continue)) {
            return false;
        }
        if (m_inGame && nav.kind == NavKind::Save && !m_saveEnabled) {
            return false;
        }
        return true;
    }

    [[nodiscard]] int ActiveNavIndex() const {
        for (int i = 0; i < static_cast<int>(m_navItems.size()); ++i) {
            if (!IsNavItemEnabled(i)) {
                continue;
            }
            const NavItem& nav = m_navItems[i];
            if (nav.directAction == Result::None && nav.targetPage == m_activePage) {
                return i;
            }
        }

        for (int i = 0; i < static_cast<int>(m_navItems.size()); ++i) {
            if (IsNavItemEnabled(i)) {
                return i;
            }
        }
        return -1;
    }

    [[nodiscard]] bool IsSlotFocusable(int index) const {
        if (index < 0 || index >= kSlotsPerPage) {
            return false;
        }

        const int absoluteSlot = m_currentPage * kSlotsPerPage + index + 1;
        const bool exists = (index < static_cast<int>(m_slots.size()) && m_slots[index].exists);
        if (m_activePage == Page::Load) {
            return exists;
        }
        if (m_activePage == Page::Save) {
            return absoluteSlot != 1;
        }
        return false;
    }

    [[nodiscard]] SDL_FRect ToggleRectForIndex(int index) const {
        switch (index) {
        case 0: return m_animatedToggleRect;
        case 1: return m_skipToggleRect;
        case 2: return m_voiceBleedToggleRect;
        case 3: return m_wheelAdvanceToggleRect;
        case 4: return m_fullscreenToggleRect;
        case 5: return m_localeRect;
        default: return {};
        }
    }

    [[nodiscard]] bool GetFocusRect(const HoverSignature& focus, SDL_FRect& out) const {
        switch (focus.type) {
        case HoverSignature::Type::Nav:
            if (IsNavItemEnabled(focus.index)) {
                out = m_navItems[focus.index].rect;
                return true;
            }
            break;
        case HoverSignature::Type::Slot:
            if (IsSlotFocusable(focus.index)) {
                out = m_slotRects[focus.index];
                return true;
            }
            break;
        case HoverSignature::Type::Slider:
            if (focus.index >= 0 && focus.index < 3) {
                const SDL_FRect& track = m_sliders[focus.index].track;
                out = { track.x - 20.0f, track.y - 30.0f, track.w + 40.0f, track.h + 60.0f };
                return true;
            }
            break;
        case HoverSignature::Type::Toggle:
            if (focus.index >= 0 && focus.index <= 5) {
                out = ToggleRectForIndex(focus.index);
                return out.w > 0.0f && out.h > 0.0f;
            }
            break;
        case HoverSignature::Type::Pager:
            if (m_activePage == Page::Save || m_activePage == Page::Load) {
                const int pageCount = GetVisiblePageCount();
                if (focus.index == 0 && m_currentPage > 0) {
                    out = m_prevPageRect;
                    return true;
                }
                if (focus.index == 1 && m_currentPage < pageCount - 1) {
                    out = m_nextPageRect;
                    return true;
                }
            }
            break;
        case HoverSignature::Type::DeleteSlot:
            if (focus.index >= 0 && focus.index < kSlotsPerPage) {
                const auto& slotRect = m_slotRects[focus.index];
                const float delSize = 28.0f;
                out = { slotRect.x + slotRect.w - delSize - 12.0f, slotRect.y + 12.0f, delSize, delSize };
                return true;
            }
            break;
        case HoverSignature::Type::ModalYes:
        case HoverSignature::Type::ModalNo: {
            SDL_FRect yes{}, no{};
            GetModalButtonRects(yes, no);
            out = (focus.type == HoverSignature::Type::ModalYes) ? yes : no;
            return true;
        }
        case HoverSignature::Type::None:
        default:
            break;
        }
        return false;
    }

    [[nodiscard]] std::vector<HoverSignature> ContentFocusItems() const {
        std::vector<HoverSignature> items;
        if (m_activePage == Page::Settings) {
            items.push_back({ HoverSignature::Type::Slider, 0 });
            items.push_back({ HoverSignature::Type::Slider, 1 });
            items.push_back({ HoverSignature::Type::Slider, 2 });
            items.push_back({ HoverSignature::Type::Toggle, 1 });
            items.push_back({ HoverSignature::Type::Toggle, 2 });
            items.push_back({ HoverSignature::Type::Toggle, 5 });
            items.push_back({ HoverSignature::Type::Toggle, 0 });
            items.push_back({ HoverSignature::Type::Toggle, 3 });
            items.push_back({ HoverSignature::Type::Toggle, 4 });
            return items;
        }

        if (m_activePage == Page::Save || m_activePage == Page::Load) {
            for (int i = 0; i < kSlotsPerPage; ++i) {
                if (IsSlotFocusable(i)) {
                    items.push_back({ HoverSignature::Type::Slot, i });
                }
            }
            if (m_currentPage > 0) {
                items.push_back({ HoverSignature::Type::Pager, 0 });
            }
            if (m_currentPage < GetVisiblePageCount() - 1) {
                items.push_back({ HoverSignature::Type::Pager, 1 });
            }
        }

        return items;
    }

    [[nodiscard]] HoverSignature FirstContentFocus() const {
        const auto items = ContentFocusItems();
        if (!items.empty()) {
            return items.front();
        }
        const int nav = ActiveNavIndex();
        return nav >= 0 ? HoverSignature{ HoverSignature::Type::Nav, nav } : HoverSignature{};
    }

    [[nodiscard]] bool IsFocusedControlValid() const {
        SDL_FRect unused{};
        return GetFocusRect(m_focusedControl, unused);
    }

    void EnsureFocusedControl() {
        if (m_modalType != ModalType::None) {
            if (m_focusedControl.type != HoverSignature::Type::ModalYes &&
                m_focusedControl.type != HoverSignature::Type::ModalNo) {
                m_focusedControl = { HoverSignature::Type::ModalNo, 0 };
            }
            return;
        }

        if (IsFocusedControlValid()) {
            return;
        }

        const int nav = ActiveNavIndex();
        if (nav >= 0) {
            m_focusedControl = { HoverSignature::Type::Nav, nav };
        } else {
            m_focusedControl = FirstContentFocus();
        }
    }

    void MoveNavFocus(int delta) {
        if (m_navItems.empty()) {
            return;
        }

        int current = (m_focusedControl.type == HoverSignature::Type::Nav)
            ? m_focusedControl.index
            : ActiveNavIndex();
        if (current < 0) {
            current = 0;
        }

        const int count = static_cast<int>(m_navItems.size());
        int next = current;
        for (int step = 0; step < count; ++step) {
            next = (next + delta + count) % count;
            if (IsNavItemEnabled(next)) {
                m_focusedControl = { HoverSignature::Type::Nav, next };
                return;
            }
        }
    }

    void MoveContentFocus(int delta) {
        const auto items = ContentFocusItems();
        if (items.empty()) {
            const int nav = ActiveNavIndex();
            if (nav >= 0) {
                m_focusedControl = { HoverSignature::Type::Nav, nav };
            }
            return;
        }

        int currentIndex = -1;
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            if (items[i] == m_focusedControl) {
                currentIndex = i;
                break;
            }
        }

        if (currentIndex < 0) {
            m_focusedControl = items.front();
            return;
        }

        const int next = (currentIndex + delta + static_cast<int>(items.size())) % static_cast<int>(items.size());
        m_focusedControl = items[next];
    }

    void UpdateFocusNavigation(const Input::FocusInput& focus, int& focusedSliderStep) {
        EnsureFocusedControl();

        if (m_modalType != ModalType::None) {
            if (focus.left || focus.right || focus.up || focus.down) {
                m_focusedControl = (m_focusedControl.type == HoverSignature::Type::ModalYes)
                    ? HoverSignature{ HoverSignature::Type::ModalNo, 0 }
                    : HoverSignature{ HoverSignature::Type::ModalYes, 0 };
            }
            return;
        }

        if (m_focusedControl.type == HoverSignature::Type::Slider && (focus.left || focus.right)) {
            focusedSliderStep = focus.right ? 1 : -1;
            return;
        }

        if (m_focusedControl.type == HoverSignature::Type::Nav) {
            if (focus.up) {
                MoveNavFocus(-1);
            } else if (focus.down) {
                MoveNavFocus(1);
            } else if (focus.right) {
                m_focusedControl = FirstContentFocus();
            }
            return;
        }

        if (focus.left) {
            const int nav = ActiveNavIndex();
            if (nav >= 0) {
                m_focusedControl = { HoverSignature::Type::Nav, nav };
            }
        } else if (focus.up) {
            MoveContentFocus(-1);
        } else if (focus.down || focus.right) {
            MoveContentFocus(1);
        }
    }

    TTF_Font* m_font = nullptr;
    int m_screenW = 0, m_screenH = 0;
    SaveManager* m_saveMgr = nullptr;
    vn::AssetManager* m_assets = nullptr;
    std::string m_fontPath;
    int m_fontPointSize = 22;
    vn::AssetManager::FontHandle m_styleFontHandle;
    Layout m_layout{};
    std::vector<SaveManager::SlotInfo> m_slots;
    int m_currentPage = 0;  // Save slot page for pagination support

    struct CachedText {
        std::shared_ptr<SDL_Texture> texture;
        float w = 0.0f;
        float h = 0.0f;
    };

    std::string m_cachedModalMsg;
    CachedText m_cachedModalText;
    float m_cachedModalWrapWidth = 0.0f;
    // Cache the system panel texture to avoid reloading it every frame
    vn::AssetManager::TextureHandle m_cachedPanelTexture;

    bool m_inGame = true;
    bool m_saveEnabled = true;
    bool m_standaloneExitConfirm = false;
    Page m_activePage = Page::Settings;
    float m_alpha = 0.0f;
    float m_slideOffsetX = 0.0f;
    float m_renderOffsetX = 0.0f;
    float m_lastMouseX = -1.0f;
    float m_lastMouseY = -1.0f;
    Tween<float> m_slideTween;

    std::vector<NavItem> m_navItems;
    std::array<SDL_FRect, 6> m_slotRects{};
    std::array<SliderUI, 3> m_sliders{};

    int m_hoveredNav = -1;
    int m_hoveredSlot = -1;
    int m_hoveredSlider = -1;
    int m_capturedSlider = -1;
    int m_hoveredDeleteSlot = -1;
    HoverSignature m_focusedControl{};
    bool m_usingFocusNavigation = false;
    std::array<float, 6> m_navHoverScales{ 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

    float m_textSpeed = 30.0f;
    float m_bgmVolume = 0.8f;
    float m_sfxVolume = 0.9f;
    std::array<float, 3> m_sliderMomentumT{ 0.217391f, 0.8f, 0.9f };
    std::array<float, 3> m_sliderVelocity{ 0.0f, 0.0f, 0.0f };
    float m_sliderInertiaFriction = 10.5f;
    bool m_animatedSpritesEnabled = true;
    bool m_skipUnseenEnabled = false;
    bool m_wheelAdvanceEnabled = false;
    bool m_voiceBleedEnabled = false;
    bool m_fullscreenEnabled = false;
    std::string m_localeCode = "zh";
    SDL_FRect m_animatedToggleRect{};
    SDL_FRect m_skipToggleRect{};
    SDL_FRect m_voiceBleedToggleRect{};
    SDL_FRect m_wheelAdvanceToggleRect{};
    SDL_FRect m_fullscreenToggleRect{};
    SDL_FRect m_localeRect{};

    ModalType m_modalType = ModalType::None;
    std::string m_modalMsg;
    int m_pendingSlot = -1;
    SDL_FRect m_modalYesRect{};
    SDL_FRect m_modalNoRect{};
    bool m_modalButtonRectsValid = false;
    SDL_FRect m_prevPageRect{};
    SDL_FRect m_nextPageRect{};

    float m_modalAlpha = 0.0f;
    float m_modalOffsetY = 0.0f;
    Tween<float> m_modalAlphaTween;
    Tween<float> m_modalOffsetTween;
    bool m_isLeftHeld = false;

    bool m_modalClosing = false;
    Result m_pendingModalResult{Result::None, -1};

    bool m_menuClosing = false;
    Result m_pendingMenuResult{Result::None, -1};
    std::function<std::string(const std::string&)> m_translate;

    void CloseModalWithAnim(Result resultToReturn) {
        m_modalClosing = true;
        m_pendingModalResult = resultToReturn;
        m_modalAlphaTween.Start(&m_modalAlpha, m_modalAlpha, 0.0f, 0.12f, Ease::Linear);
        m_modalOffsetTween.Start(&m_modalOffsetY, m_modalOffsetY, 20.0f, 0.15f, Ease::InQuad); 
    }

    void CloseMenuWithAnim(Result resultToReturn) {
        m_menuClosing = true;
        m_pendingMenuResult = resultToReturn;
        m_slideTween.Start(&m_slideOffsetX, m_slideOffsetX, -80.0f, 0.15f, Ease::InQuad);
    }

    void RenderSlots(SDL_Renderer* renderer, Uint8 alpha) {
        for (int i = 0; i < kSlotsPerPage; ++i) {
            const SDL_FRect& rRaw = m_slotRects[i];
            SDL_FRect r = ShiftRect(rRaw);
            bool hovered = (i == m_hoveredSlot);
            bool exists = (i < static_cast<int>(m_slots.size()) && m_slots[i].exists);
            const int absoluteSlot = m_currentPage * kSlotsPerPage + i + 1;
            const bool isAutoSaveSlot = (absoluteSlot == 1);
            const bool isDisabledSaveSlot = (m_activePage == Page::Save && isAutoSaveSlot);

            SDL_SetRenderDrawColor(renderer,
                                   ui::theme::kBlackPanel.r,
                                   ui::theme::kBlackPanel.g,
                                   ui::theme::kBlackPanel.b,
                                   static_cast<Uint8>(alpha * 0.9f));
            SDL_RenderFillRect(renderer, &r);

            SDL_Color border = ui::theme::WithAlpha(ui::theme::kGraySteel, static_cast<Uint8>(alpha * 0.6f));
            if (isDisabledSaveSlot) {
                border = ui::theme::WithAlpha(ui::theme::kTextDisabled, static_cast<Uint8>(alpha * 0.9f));
            }
            if (hovered) {
                if (isDisabledSaveSlot) {
                    border = ui::theme::WithAlpha(ui::theme::kGrayMist, alpha);
                } else if (m_activePage == Page::Save && exists) {
                    border = ui::theme::WithAlpha(ui::theme::kAccentPrimary, alpha);
                } else if (m_activePage == Page::Save && !exists) {
                    border = ui::theme::WithAlpha(ui::theme::kBluePrimary, alpha);
                } else {
                    border = ui::theme::WithAlpha(ui::theme::kBluePrimary, alpha);
                }
            }
            SDL_SetRenderDrawColor(renderer, border.r, border.g, border.b, border.a);
            SDL_RenderRect(renderer, &r);

            DrawText(renderer,
                     isAutoSaveSlot ? Tr("ui_auto_save", "AUTO SAVE") : (Tr("ui_data_prefix", "DATA") + " " + std::to_string(absoluteSlot)),
                     rRaw.x + m_layout.slotLabelX,
                     rRaw.y + m_layout.slotLabelY,
                     ui::theme::WithAlpha(isDisabledSaveSlot ? ui::theme::kTextDisabled : ui::theme::kTextSecondary, alpha));

            SDL_FRect thumbRectRaw = { rRaw.x + m_layout.thumbX, rRaw.y + m_layout.thumbY, m_layout.thumbWidth, m_layout.thumbHeight };
            SDL_FRect thumbRect = ShiftRect(thumbRectRaw);
            const bool hasThumb = exists && m_slots[i].hasThumbnail && !m_slots[i].thumbnailPath.empty();
            const bool hasBg = exists && !m_slots[i].bgPath.empty();
            bool renderedPreview = false;

            if (hasThumb && m_assets) {
                auto thumbTexPtr = m_assets->GetTexture(m_slots[i].thumbnailPath);
                if (thumbTexPtr) {
                    SDL_Texture* tex = thumbTexPtr.get();
                    SDL_SetTextureAlphaMod(tex, isDisabledSaveSlot ? ui::theme::ScaleAlpha(alpha, 0.38f) : alpha);
                    SDL_RenderTexture(renderer, tex, nullptr, &thumbRect);
                    SDL_SetTextureAlphaMod(tex, 255);
                    SDL_SetRenderDrawColor(renderer,
                                           (isDisabledSaveSlot ? ui::theme::kTextDisabled : ui::theme::kBluePrimary).r,
                                           (isDisabledSaveSlot ? ui::theme::kTextDisabled : ui::theme::kBluePrimary).g,
                                           (isDisabledSaveSlot ? ui::theme::kTextDisabled : ui::theme::kBluePrimary).b,
                                           static_cast<Uint8>(alpha * 0.25f));
                    SDL_RenderRect(renderer, &thumbRect);
                    renderedPreview = true;
                }
            }

            if (!renderedPreview && hasBg && m_assets) {
                auto bgTexPtr = m_assets->GetTexture(m_slots[i].bgPath);
                if (bgTexPtr) {
                    SDL_Texture* tex = bgTexPtr.get();
                    SDL_SetTextureAlphaMod(tex, isDisabledSaveSlot ? ui::theme::ScaleAlpha(alpha, 0.38f) : alpha);

                    float tw = 0.0f;
                    float th = 0.0f;
                    SDL_GetTextureSize(tex, &tw, &th);
                    if (tw > 0.0f && th > 0.0f) {
                        const float scale = std::max(thumbRect.w / tw, thumbRect.h / th);
                        const float cropW = thumbRect.w / scale;
                        const float cropH = thumbRect.h / scale;
                        SDL_FRect src = {
                            (tw - cropW) * 0.5f,
                            (th - cropH) * 0.5f,
                            cropW,
                            cropH
                        };

                        SDL_RenderTexture(renderer, tex, &src, &thumbRect);
                        SDL_SetTextureAlphaMod(tex, 255);
                        SDL_SetRenderDrawColor(renderer,
                                               (isDisabledSaveSlot ? ui::theme::kTextDisabled : ui::theme::kGrayMist).r,
                                               (isDisabledSaveSlot ? ui::theme::kTextDisabled : ui::theme::kGrayMist).g,
                                               (isDisabledSaveSlot ? ui::theme::kTextDisabled : ui::theme::kGrayMist).b,
                                               static_cast<Uint8>(alpha * 0.25f));
                        SDL_RenderRect(renderer, &thumbRect);
                        renderedPreview = true;
                    }
                    SDL_SetTextureAlphaMod(tex, 255);
                } else {
                    SDL_SetRenderDrawColor(renderer,
                                           ui::theme::kGrayGloom.r,
                                           ui::theme::kGrayGloom.g,
                                           ui::theme::kGrayGloom.b,
                                           static_cast<Uint8>(alpha * 0.9f));
                    SDL_RenderFillRect(renderer, &thumbRect);
                    DrawTextCentered(renderer,
                                     Tr("ui_no_bg", "NO BG"),
                                     thumbRect.x + thumbRect.w * 0.5f,
                                     thumbRect.y + thumbRect.h * 0.5f,
                                     ui::theme::WithAlpha(ui::theme::kTextMuted, alpha));
                    renderedPreview = true;
                }
            }

            if (!renderedPreview) {
                SDL_SetRenderDrawColor(renderer,
                                       ui::theme::kGrayGloom.r,
                                       ui::theme::kGrayGloom.g,
                                       ui::theme::kGrayGloom.b,
                                       static_cast<Uint8>(alpha * 0.9f));
                SDL_RenderFillRect(renderer, &thumbRect);
                DrawTextCentered(renderer,
                                 exists ? Tr("ui_no_bg", "NO BG") : Tr("ui_no_image", "NO IMAGE"),
                                 thumbRectRaw.x + thumbRectRaw.w * 0.5f,
                                 thumbRectRaw.y + thumbRectRaw.h * 0.5f,
                                 ui::theme::WithAlpha(ui::theme::kTextMuted, alpha));
            }

            const float infoX = rRaw.x + m_layout.infoX;
            if (exists) {
                const auto& slot = m_slots[i];
                const std::string chapter = slot.chapterName.empty() ? Tr("ui_unknown_chapter", "Unknown Chapter") : slot.chapterName;
                const std::string timeStr = slot.saveTime.empty() ? "----/--/-- --:--" : slot.saveTime;

                DrawText(renderer,
                         chapter,
                         infoX,
                         rRaw.y + m_layout.chapterY,
                         ui::theme::WithAlpha(isDisabledSaveSlot ? ui::theme::kTextDisabled : ui::theme::kAccentSoft, alpha));
                DrawText(renderer,
                         timeStr,
                         infoX,
                         rRaw.y + m_layout.timeY,
                         ui::theme::WithAlpha(isDisabledSaveSlot ? ui::theme::kTextMuted : ui::theme::kTextSecondary, alpha));

                if (!isAutoSaveSlot) {
                    const float delSize = 28.0f;
                    const SDL_FRect delRectRaw = { rRaw.x + rRaw.w - delSize - 12.0f, rRaw.y + 12.0f, delSize, delSize };
                    const SDL_FRect delRect = ShiftRect(delRectRaw);
                    const bool hoverDel = (i == m_hoveredDeleteSlot);

                    SDL_SetRenderDrawColor(renderer,
                                           ui::theme::kAccentPrimary.r,
                                           ui::theme::kAccentPrimary.g,
                                           ui::theme::kAccentPrimary.b,
                                           static_cast<Uint8>(alpha * (hoverDel ? 1.0f : 0.65f)));
                    SDL_RenderFillRect(renderer, &delRect);

                    SDL_SetRenderDrawColor(renderer,
                                           ui::theme::kTextPrimary.r,
                                           ui::theme::kTextPrimary.g,
                                           ui::theme::kTextPrimary.b,
                                           alpha);
                    SDL_RenderRect(renderer, &delRect);

                    DrawTextCentered(renderer,
                                     Tr("ui_delete_mark", "X"),
                                     delRectRaw.x + delSize * 0.5f,
                                     delRectRaw.y + delSize * 0.5f,
                                     { 255, 255, 255, alpha });
                }
            } else {
                DrawText(renderer, Tr("ui_no_data", "NO DATA"), infoX, rRaw.y + m_layout.emptyY, ui::theme::WithAlpha(ui::theme::kTextDisabled, alpha));
            }
        }

        const int pageCount = GetVisiblePageCount();
        const bool canPrevPage = m_currentPage > 0;
        const bool canNextPage = m_currentPage < (pageCount - 1);

        const float contentCenterX = m_layout.slotStartX + m_layout.slotWidth + (m_layout.slotGapX * 0.5f);
        const float slotBottom = m_layout.slotStartY + (m_layout.slotHeight * 3.0f) + (m_layout.slotGapY * 2.0f);
        const float pagerY = slotBottom + 30.0f;

        const float buttonW = 120.0f;
        const float buttonH = 30.0f;
        const float labelW = 156.0f;
        const float gap = 20.0f;
        const float totalW = buttonW * 2.0f + labelW + gap * 2.0f;
        const float startX = contentCenterX - totalW * 0.5f;

        m_prevPageRect = { startX, pagerY, buttonW, buttonH };
        m_nextPageRect = { startX + buttonW + gap + labelW + gap, pagerY, buttonW, buttonH };

        SDL_Color prevFill = canPrevPage ? ui::theme::WithAlpha(ui::theme::kBlackElevated, static_cast<Uint8>(alpha * 0.95f))
                         : ui::theme::WithAlpha(ui::theme::kBlackPanel, static_cast<Uint8>(alpha * 0.7f));
        SDL_Color nextFill = canNextPage ? ui::theme::WithAlpha(ui::theme::kBlackElevated, static_cast<Uint8>(alpha * 0.95f))
                         : ui::theme::WithAlpha(ui::theme::kBlackPanel, static_cast<Uint8>(alpha * 0.7f));
        SDL_Color prevText = canPrevPage ? ui::theme::WithAlpha(ui::theme::kTextPrimary, alpha)
                         : ui::theme::WithAlpha(ui::theme::kTextDisabled, alpha);
        SDL_Color nextText = canNextPage ? ui::theme::WithAlpha(ui::theme::kTextPrimary, alpha)
                         : ui::theme::WithAlpha(ui::theme::kTextDisabled, alpha);

        Draw::Rect(renderer,
               ShiftRect(m_prevPageRect),
               prevFill,
               ui::theme::WithAlpha(ui::theme::kGraySteel, alpha));
        Draw::Rect(renderer,
               ShiftRect(m_nextPageRect),
               nextFill,
               ui::theme::WithAlpha(ui::theme::kGraySteel, alpha));

        DrawTextCentered(renderer,
                 Tr("ui_prev", "Prev"),
                 m_prevPageRect.x + m_prevPageRect.w * 0.5f,
                 m_prevPageRect.y + m_prevPageRect.h * 0.5f,
                 prevText);
        DrawTextCentered(renderer,
                 Tr("ui_next", "Next"),
                 m_nextPageRect.x + m_nextPageRect.w * 0.5f,
                 m_nextPageRect.y + m_nextPageRect.h * 0.5f,
                 nextText);
        DrawTextCentered(renderer,
                 Tr("ui_page", "Page") + " " + std::to_string(m_currentPage + 1) + " / " + std::to_string(pageCount),
                 contentCenterX,
                 pagerY + buttonH * 0.5f,
                 ui::theme::WithAlpha(ui::theme::kTextSecondary, alpha));
    }

    void RenderSettings(SDL_Renderer* renderer, Uint8 alpha) {
        for (int i = 0; i < 3; ++i) {
            const auto& slider = m_sliders[i];
            const bool hovered = (i == m_hoveredSlider || i == m_capturedSlider);

            float t = std::clamp(m_sliderMomentumT[static_cast<std::size_t>(i)], 0.0f, 1.0f);
            int percent = static_cast<int>(t * 100.0f);
            std::string displayLabel = slider.label + "  [" + std::to_string(percent) + "%]";
            if (i == 0 && percent > 95) {
                displayLabel = slider.label + "  [" + Tr("ui_instant", "Instant") + "]";
            }

            DrawText(renderer,
                     displayLabel,
                     slider.track.x,
                     slider.track.y + m_layout.sliderLabelYOffset,
                     ui::theme::WithAlpha(ui::theme::kTextPrimary, alpha));

            const SDL_FRect track = ShiftRect(slider.track);
            SDL_SetRenderDrawColor(renderer,
                                   ui::theme::kGrayGloom.r,
                                   ui::theme::kGrayGloom.g,
                                   ui::theme::kGrayGloom.b,
                                   alpha);
            SDL_RenderFillRect(renderer, &track);
            SDL_SetRenderDrawColor(renderer,
                                   ui::theme::kGraySteel.r,
                                   ui::theme::kGraySteel.g,
                                   ui::theme::kGraySteel.b,
                                   static_cast<Uint8>(alpha * 0.85f));
            SDL_RenderRect(renderer, &track);

            SDL_FRect fill = track;
            fill.w = track.w * t;
            SDL_SetRenderDrawColor(renderer,
                                   ui::theme::kBlueStrong.r,
                                   ui::theme::kBlueStrong.g,
                                   ui::theme::kBlueStrong.b,
                                   alpha);
            SDL_RenderFillRect(renderer, &fill);

            const float thumbScale = hovered ? 1.3f : 1.0f;
            const float thumbW = m_layout.sliderThumbWidth * thumbScale;
            const float thumbH = m_layout.sliderThumbHeight * thumbScale;
            SDL_FRect thumb = {
                track.x + fill.w - thumbW * 0.5f,
                track.y + (track.h - thumbH) * 0.5f,
                thumbW,
                thumbH
            };
            SDL_SetRenderDrawColor(renderer,
                                   hovered ? ui::theme::kAccentSoft.r : ui::theme::kAccentPrimary.r,
                                   hovered ? ui::theme::kAccentSoft.g : ui::theme::kAccentPrimary.g,
                                   hovered ? ui::theme::kAccentSoft.b : ui::theme::kAccentPrimary.b,
                                   alpha);
            SDL_RenderFillRect(renderer, &thumb);
        }

        const float mx = m_lastMouseX;
        const float my = m_lastMouseY;
        const float localMx = mx - m_renderOffsetX;

        auto renderToggle = [&](const SDL_FRect& rowRect, const std::string& label, bool enabled) {
            const bool hovered = Draw::Contains(rowRect, localMx, my);
            const SDL_Color rowFill = hovered
                ? ui::theme::WithAlpha(ui::theme::kGrayGloom, static_cast<Uint8>(alpha * 0.95f))
                : ui::theme::WithAlpha(ui::theme::kBlackPanel, static_cast<Uint8>(alpha * 0.9f));
            const SDL_Color rowBorder = hovered
                ? ui::theme::WithAlpha(ui::theme::kGrayMist, alpha)
                : ui::theme::WithAlpha(ui::theme::kGraySteel, alpha);
            Draw::Rect(renderer, ShiftRect(rowRect), rowFill, rowBorder);

            const float boxSize = 18.0f;
            SDL_FRect checkBox = {
                rowRect.x + 10.0f,
                rowRect.y + (rowRect.h - boxSize) * 0.5f,
                boxSize,
                boxSize
            };
            SDL_FRect shiftedCheckBox = ShiftRect(checkBox);
            const SDL_Color checkFill = enabled
                ? ui::theme::WithAlpha(ui::theme::kAccentPrimary, alpha)
                : ui::theme::WithAlpha(ui::theme::kGrayGloom, alpha);
            const SDL_Color checkBorder = enabled
                ? ui::theme::WithAlpha(ui::theme::kAccentSoft, alpha)
                : ui::theme::WithAlpha(ui::theme::kGraySteel, alpha);
            Draw::Rect(renderer, shiftedCheckBox, checkFill, checkBorder);

            if (enabled) {
                SDL_SetRenderDrawColor(renderer,
                                       ui::theme::kBlackBase.r,
                                       ui::theme::kBlackBase.g,
                                       ui::theme::kBlackBase.b,
                                       alpha);
                for (float offset = -0.5f; offset <= 0.5f; offset += 0.5f) {
                    SDL_RenderLine(renderer,
                                   shiftedCheckBox.x + 4.0f + offset,
                                   shiftedCheckBox.y + 9.0f,
                                   shiftedCheckBox.x + 8.0f + offset,
                                   shiftedCheckBox.y + 13.0f);
                    SDL_RenderLine(renderer,
                                   shiftedCheckBox.x + 8.0f + offset,
                                   shiftedCheckBox.y + 13.0f,
                                   shiftedCheckBox.x + 14.0f + offset,
                                   shiftedCheckBox.y + 5.0f);
                }
            }

            DrawTextLeftCentered(renderer,
                                 label,
                                 rowRect.x + 38.0f,
                                 rowRect.y + rowRect.h * 0.5f,
                                                                 ui::theme::WithAlpha(ui::theme::kTextPrimary, alpha));
        };

        auto renderSelector = [&](const SDL_FRect& rowRect, const std::string& label, const std::string& value) {
            const bool hovered = Draw::Contains(rowRect, localMx, my);
            const SDL_Color rowFill = hovered
                ? ui::theme::WithAlpha(ui::theme::kGrayGloom, static_cast<Uint8>(alpha * 0.95f))
                : ui::theme::WithAlpha(ui::theme::kBlackPanel, static_cast<Uint8>(alpha * 0.9f));
            const SDL_Color rowBorder = hovered
                ? ui::theme::WithAlpha(ui::theme::kGrayMist, alpha)
                : ui::theme::WithAlpha(ui::theme::kGraySteel, alpha);
            Draw::Rect(renderer, ShiftRect(rowRect), rowFill, rowBorder);

            DrawTextLeftCentered(renderer,
                                 label,
                                 rowRect.x + 12.0f,
                                 rowRect.y + rowRect.h * 0.5f,
                                 ui::theme::WithAlpha(ui::theme::kTextPrimary, alpha));
            DrawTextLeftCentered(renderer,
                                 value,
                                 rowRect.x + rowRect.w * 0.56f,
                                 rowRect.y + rowRect.h * 0.5f,
                                 ui::theme::WithAlpha(ui::theme::kAccentSoft, alpha));
        };

                DrawText(renderer,
                                 Tr("ui_gameplay", "GAMEPLAY"),
                                 m_skipToggleRect.x,
                                 m_skipToggleRect.y - 32.0f,
                                 ui::theme::WithAlpha(ui::theme::kBluePrimary, alpha));
        renderToggle(m_skipToggleRect, Tr("ui_skip_unseen", "Skip Unseen"), m_skipUnseenEnabled);
        renderToggle(m_voiceBleedToggleRect, Tr("ui_voice_bleed", "Voice Bleed"), m_voiceBleedEnabled);
        renderSelector(m_localeRect,
                   Tr("ui_language", "Language"),
                   LocaleDisplayName());

                DrawText(renderer,
                                 Tr("ui_display", "DISPLAY"),
                                 m_animatedToggleRect.x,
                                 m_animatedToggleRect.y - 32.0f,
                                 ui::theme::WithAlpha(ui::theme::kBluePrimary, alpha));
        renderToggle(m_animatedToggleRect, Tr("ui_animated_sprites", "Animated Sprites"), m_animatedSpritesEnabled);
        renderToggle(m_wheelAdvanceToggleRect, Tr("ui_wheel_advance", "Wheel Advance"), m_wheelAdvanceEnabled);
        renderToggle(m_fullscreenToggleRect, Tr("ui_fullscreen", "Fullscreen (F11)"), m_fullscreenEnabled);
    }

    void DrawTextCentered(SDL_Renderer* renderer, const std::string& text, float centerX, float centerY, SDL_Color color) {
        if (!renderer || text.empty()) return;
        CachedText entry = GetText(renderer, text);
        if (!entry.texture) return;

        SDL_Texture* tex = entry.texture.get();
        SDL_SetTextureColorMod(tex, color.r, color.g, color.b);
        SDL_SetTextureAlphaMod(tex, color.a);
        SDL_FRect dst = { centerX - entry.w * 0.5f + m_renderOffsetX, centerY - entry.h * 0.5f, entry.w, entry.h };
        SDL_RenderTexture(renderer, tex, nullptr, &dst);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
        SDL_SetTextureAlphaMod(tex, 255);
    }

    void DrawText(SDL_Renderer* renderer, const std::string& text, float x, float y, SDL_Color color) {
        if (!renderer || text.empty()) return;
        CachedText entry = GetText(renderer, text);
        if (!entry.texture) return;

        SDL_Texture* tex = entry.texture.get();
        SDL_SetTextureColorMod(tex, color.r, color.g, color.b);
        SDL_SetTextureAlphaMod(tex, color.a);
        SDL_FRect dst = { x + m_renderOffsetX, y, entry.w, entry.h };
        SDL_RenderTexture(renderer, tex, nullptr, &dst);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
        SDL_SetTextureAlphaMod(tex, 255);
    }

    void DrawTextScaled(SDL_Renderer* renderer,
                        const std::string& text,
                        float x,
                        float y,
                        float scale,
                        SDL_Color color) {
        if (!renderer || text.empty()) return;
        CachedText entry = GetText(renderer, text);
        if (!entry.texture) return;

        scale = std::clamp(scale, 0.75f, 1.25f);
        const float scaledW = entry.w * scale;
        const float scaledH = entry.h * scale;

        SDL_Texture* tex = entry.texture.get();
        SDL_SetTextureColorMod(tex, color.r, color.g, color.b);
        SDL_SetTextureAlphaMod(tex, color.a);
        SDL_FRect dst = { x + m_renderOffsetX, y, scaledW, scaledH };
        SDL_RenderTexture(renderer, tex, nullptr, &dst);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
        SDL_SetTextureAlphaMod(tex, 255);
    }

    void DrawTextLeftCentered(SDL_Renderer* renderer, const std::string& text, float leftX, float centerY, SDL_Color color) {
        if (!renderer || text.empty()) return;
        CachedText entry = GetText(renderer, text);
        if (!entry.texture) return;

        SDL_Texture* tex = entry.texture.get();
        SDL_SetTextureColorMod(tex, color.r, color.g, color.b);
        SDL_SetTextureAlphaMod(tex, color.a);
        SDL_FRect dst = { leftX + m_renderOffsetX, centerY - entry.h * 0.5f, entry.w, entry.h };
        SDL_RenderTexture(renderer, tex, nullptr, &dst);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
        SDL_SetTextureAlphaMod(tex, 255);
    }

    void DrawTextScaledLeftCentered(SDL_Renderer* renderer,
                                    const std::string& text,
                                    float leftX,
                                    float centerY,
                                    float scale,
                                    SDL_Color color) {
        if (!renderer || text.empty()) return;
        CachedText entry = GetText(renderer, text);
        if (!entry.texture) return;

        scale = std::clamp(scale, 0.75f, 1.25f);
        const float scaledW = entry.w * scale;
        const float scaledH = entry.h * scale;

        SDL_Texture* tex = entry.texture.get();
        SDL_SetTextureColorMod(tex, color.r, color.g, color.b);
        SDL_SetTextureAlphaMod(tex, color.a);
        SDL_FRect dst = { leftX + m_renderOffsetX, centerY - scaledH * 0.5f, scaledW, scaledH };
        SDL_RenderTexture(renderer, tex, nullptr, &dst);
        SDL_SetTextureColorMod(tex, 255, 255, 255);
        SDL_SetTextureAlphaMod(tex, 255);
    }

    CachedText GetText(SDL_Renderer* renderer, const std::string& text) {
        if (!m_font || !renderer || text.empty() || !m_assets || m_fontPath.empty()) return {};

        CachedText entry;
        entry.texture = m_assets->GetTextTexture(
            text,
            m_font,
            m_fontPath,
            m_fontPointSize,
            SDL_Color{ 255, 255, 255, 255 },
            std::max(0, m_layout.systemTextOutline)
        );
        if (!entry.texture) return {};
        SDL_GetTextureSize(entry.texture.get(), &entry.w, &entry.h);
        return entry;
    }

    SDL_FRect ShiftRect(const SDL_FRect& rect) const {
        return SDL_FRect{ rect.x + m_renderOffsetX, rect.y, rect.w, rect.h };
    }

    CachedText GetWrappedText(SDL_Renderer* renderer, const std::string& text, float wrapWidth) {
        if (!m_font || !renderer || text.empty() || wrapWidth <= 0.0f) return {};

        if (m_cachedModalMsg == text && m_cachedModalText.texture && m_cachedModalWrapWidth == wrapWidth) {
            return m_cachedModalText;
        }

        SDL_Surface* surf = TTF_RenderText_Blended_Wrapped(
            m_font,
            text.c_str(),
            0,
            SDL_Color{ 255, 255, 255, 255 },
            wrapWidth
        );
        if (!surf) return {};

        CachedText entry;
        entry.texture = std::shared_ptr<SDL_Texture>(
            SDL_CreateTextureFromSurface(renderer, surf),
            [](SDL_Texture* t) {
                if (t) SDL_DestroyTexture(t);
            }
        );
        entry.w = static_cast<float>(surf->w);
        entry.h = static_cast<float>(surf->h);
        SDL_DestroySurface(surf);

        if (!entry.texture) return {};

        m_cachedModalMsg = text;
        m_cachedModalWrapWidth = wrapWidth;
        m_cachedModalText = entry;
        return entry;
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
};
