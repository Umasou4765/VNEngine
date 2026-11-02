#include "UIManager.h"

#include "ChoiceMenu.h"
#include "DialogueBox.h"
#include "Input.h"
#include "LogScreen.h"
#include "QuickMenu.h"
#include "SystemMenu.h"
#include "TitleScreen.h"
#include "UITheme.h"

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <deque>
#include <utility>

namespace vn {

namespace {
constexpr float kWheelAdvanceCooldown = 0.25f;

[[nodiscard]] std::uint32_t Flag(UIManager::CommandFlag flag) noexcept {
    return static_cast<std::uint32_t>(flag);
}
} // namespace

struct UIManager::Impl {
    enum class Surface {
        Title,
        InGame,
        SystemMenu,
    };

    DialogueBox dlg;
    ChoiceMenu menu;
    QuickMenu quickMenu;
    LogScreen logScreen;
    SystemMenu systemMenu;
    TitleScreen titleScreen;

    RuntimeConfig config;
    std::function<std::string(const std::string&)> translator;

    SDL_Renderer* renderer = nullptr;
    TTF_Font* font = nullptr;
    AssetManager* assets = nullptr;
    SaveManager* saveManager = nullptr;
    std::string fontPath;
    int fontPointSize = 22;
    int width = 1280;
    int height = 720;

    Surface surface = Surface::Title;
    Surface returnSurface = Surface::Title;
    bool canSave = false;
    bool currentLineSeen = true;
    bool voicePlaying = false;
    bool observeHandlesCancel = false;
    bool uiHiddenByUser = false;
    bool blockAdvanceThisFrame = false;
    bool autoModeEnabled = false;
    bool skipModeEnabled = false;
    float skipTimer = 0.0f;
    std::uint64_t lastWheelAdvanceTick = 0;
    int lastTitleHoverIndex = -1;
    SystemMenu::HoverSignature lastSystemHoverSignature{};

    void ApplyStoredTranslator() {
        if (!translator) {
            return;
        }

        titleScreen.SetTranslator(translator);
        quickMenu.SetTranslator(translator);
        systemMenu.SetTranslator(translator);
        systemMenu.SetLocaleCode(config.localeCode);
    }

    void ApplyRuntimeConfigToWidgets() {
        dlg.charsPerSecond = config.textSpeed;
        dlg.instantText = config.instantText;
        dlg.autoDelayBase = 0.8f;
        dlg.autoDelayPerChar = 0.015f;
        systemMenu.SetConfigValues(config.textSpeed,
                                   config.bgmVolume,
                                   config.sfxVolume,
                                   config.animatedSpritesEnabled,
                                   config.skipUnseenEnabled,
                                   config.wheelAdvanceEnabled,
                                   config.voiceBleedEnabled,
                                   config.fullscreenEnabled,
                                   config.localeCode);
    }

    void ResetDialogueModes() {
        skipModeEnabled = false;
        autoModeEnabled = false;
        skipTimer = 0.0f;
        dlg.autoMode = false;
        dlg.ResetAutoAdvanceTimer();
    }

    void OpenMenu(SystemMenu::Page page, bool inGame) {
        returnSurface = surface == Surface::SystemMenu
            ? returnSurface
            : surface;
        surface = Surface::SystemMenu;
        ApplyRuntimeConfigToWidgets();
        systemMenu.SetSaveEnabled(inGame && canSave);
        systemMenu.Open(page, inGame);
    }

    void OpenExit(bool inGame) {
        returnSurface = surface == Surface::SystemMenu
            ? returnSurface
            : surface;
        surface = Surface::SystemMenu;
        ApplyRuntimeConfigToWidgets();
        systemMenu.SetSaveEnabled(inGame && canSave);
        systemMenu.OpenExitConfirm(inGame);
    }

    [[nodiscard]] bool IsAdvanceBlocked() const noexcept {
        return blockAdvanceThisFrame;
    }

    [[nodiscard]] bool CanSkipCurrentLine() const noexcept {
        return currentLineSeen || config.skipUnseenEnabled;
    }

    [[nodiscard]] bool IsDialogueInteractable() const noexcept {
        return !uiHiddenByUser && dlg.visible && !logScreen.visible;
    }

    struct InGameInputSignals {
        int scrollDelta = 0;
        bool leftClick = false;
        bool rightClick = false;
        bool middleClick = false;
        bool advanceAction = false;
        bool openLog = false;
        bool openSystemMenu = false;
        bool skipHeld = false;
        bool wheelUp = false;
        bool wheelDown = false;
        bool advance = false;
        bool toggleHide = false;
    };

    [[nodiscard]] InGameInputSignals CollectInGameInputSignals(Input& input) {
        InGameInputSignals signals;
        signals.scrollDelta = input.PopScroll();
        signals.leftClick = input.JustClicked();
        signals.rightClick = input.JustRightClicked();
        signals.middleClick = input.JustMiddleClicked();
        signals.advanceAction = input.IsActionJustPressed(Input::Action::Advance);
        signals.openLog = input.IsActionJustPressed(Input::Action::OpenLog);
        signals.openSystemMenu = input.IsActionJustPressed(Input::Action::OpenSystemMenu);
        signals.skipHeld = input.IsActionHeld(Input::Action::SkipModifier);
        signals.wheelUp = signals.scrollDelta > 0;

        const bool rawWheelDown = signals.scrollDelta < 0;
        const std::uint64_t nowTicks = SDL_GetTicks();
        const bool wheelReady = (lastWheelAdvanceTick == 0) ||
            (static_cast<float>(nowTicks - lastWheelAdvanceTick) / 1000.0f >= kWheelAdvanceCooldown);
        if (config.wheelAdvanceEnabled && rawWheelDown && wheelReady) {
            signals.wheelDown = true;
            lastWheelAdvanceTick = nowTicks;
        }

        signals.advance = signals.wheelDown || signals.advanceAction;
        signals.toggleHide = input.IsActionJustPressed(Input::Action::ToggleHideUI) || signals.middleClick;
        return signals;
    }

    void DrawText(SDL_Renderer* outRenderer, const std::string& text, float x, float y, SDL_Color color) {
        if (!assets || !font || !outRenderer || text.empty()) {
            return;
        }

        auto texHandle = assets->GetTextTexture(text, font, fontPath, fontPointSize, color);
        SDL_Texture* tex = texHandle.get();
        if (!tex) {
            return;
        }

        float texW = 0.0f;
        float texH = 0.0f;
        if (SDL_GetTextureSize(tex, &texW, &texH)) {
            const SDL_FRect dst{ x, y, texW, texH };
            SDL_RenderTexture(outRenderer, tex, nullptr, &dst);
        }
    }

    void ShowDialogue(const std::string& speaker,
                      const std::string& text,
                      const std::string& lineId,
                      const std::string& voicePath) {
        dlg.SetLine(speaker, text, lineId, voicePath);
    }

    void ShowChoices(const std::vector<std::string>& choices) {
        std::vector<Choice> options;
        options.reserve(choices.size());
        for (int i = 0; i < static_cast<int>(choices.size()); ++i) {
            options.push_back({ choices[static_cast<std::size_t>(i)], i + 1 });
        }
        menu.Show(options);
    }

    void ClearDialogue() {
        dlg.Clear();
    }

    void HideChoices() {
        menu.Hide();
    }

    UpdateResult UpdateTitle(float dt, Input& input) {
        (void)dt;

        UpdateResult result;
        result.config = config;

        if (input.IsActionJustPressed(Input::Action::OpenSystemMenu)) {
            OpenExit(false);
            result.commands |= Flag(CommandOpenSystemMenu);
            return result;
        }

        const TitleScreen::Action action = titleScreen.Update(
            input.GetMouseX(),
            input.GetMouseY(),
            input.JustClicked(),
            input.GetFocusInput());

        lastTitleHoverIndex = titleScreen.GetHoveredIndex();

        switch (action) {
            case TitleScreen::Action::NewGame:
                result.commands |= Flag(CommandStartNewGame);
                break;
            case TitleScreen::Action::Continue:
                result.commands |= Flag(CommandContinueGame);
                break;
            case TitleScreen::Action::LoadGame:
                OpenMenu(SystemMenu::Page::Load, false);
                result.commands |= Flag(CommandOpenLoadMenu);
                break;
            case TitleScreen::Action::OpenSettings:
                OpenMenu(SystemMenu::Page::Settings, false);
                result.commands |= Flag(CommandOpenSystemMenu);
                break;
            case TitleScreen::Action::Exit:
                OpenExit(false);
                result.commands |= Flag(CommandOpenSystemMenu);
                break;
            case TitleScreen::Action::None:
                break;
        }

        return result;
    }

    UpdateResult UpdateSystem(float dt, Input& input) {
        UpdateResult result;
        result.config = config;

        const float mouseX = input.GetMouseX();
        const float mouseY = input.GetMouseY();
        const SystemMenu::Result menuResult = systemMenu.Update(
            dt,
            mouseX,
            mouseY,
            input.JustClicked(),
            input.JustRightClicked(),
            input.IsActionJustPressed(Input::Action::OpenSystemMenu),
            input.IsLeftHeld(),
            input.IsActionJustPressed(Input::Action::DeleteSaveSlot),
            input.GetFocusInput());

        lastSystemHoverSignature = systemMenu.GetHoverSignature(mouseX, mouseY);

        switch (menuResult.action) {
            case SystemMenu::Result::Resume:
                systemMenu.Close();
                surface = returnSurface;
                result.commands |= Flag(CommandResume);
                break;
            case SystemMenu::Result::ReturnToTitle:
                systemMenu.Close();
                surface = Surface::Title;
                result.commands |= Flag(CommandReturnToTitle);
                break;
            case SystemMenu::Result::Quit:
                systemMenu.Close();
                result.commands |= Flag(CommandQuit);
                break;
            case SystemMenu::Result::SaveSlot:
                result.saveSlot = menuResult.slotIndex;
                result.commands |= Flag(CommandSaveSlot);
                break;
            case SystemMenu::Result::LoadSlot:
                result.loadSlot = menuResult.slotIndex;
                result.commands |= Flag(CommandLoadSlot);
                break;
            case SystemMenu::Result::DeleteSlot:
                result.deleteSlot = menuResult.slotIndex;
                result.commands |= Flag(CommandDeleteSlot);
                break;
            case SystemMenu::Result::ConfigChanged:
                config.textSpeed = systemMenu.GetTextSpeed();
                config.bgmVolume = systemMenu.GetBGM();
                config.sfxVolume = systemMenu.GetSFX();
                config.animatedSpritesEnabled = systemMenu.GetAnimatedSpritesEnabled();
                config.skipUnseenEnabled = systemMenu.GetSkipUnseenEnabled();
                config.wheelAdvanceEnabled = systemMenu.GetWheelAdvanceEnabled();
                config.voiceBleedEnabled = systemMenu.GetVoiceBleedEnabled();
                config.fullscreenEnabled = systemMenu.GetFullscreenEnabled();
                config.localeCode = systemMenu.GetLocaleCode();
                config.instantText = config.textSpeed >= 119.0f;
                result.config = config;
                result.commands |= Flag(CommandConfigChanged);
                break;
            case SystemMenu::Result::None:
                break;
        }

        return result;
    }

    UpdateResult UpdateInGame(float dt, Input& input, SceneMode state) {
        UpdateResult result;
        result.config = config;

        const InGameInputSignals signals = CollectInGameInputSignals(input);
        const int scrollDelta = signals.scrollDelta;
        const bool sigLeftClick = signals.leftClick;
        const bool sigRightClick = signals.rightClick;
        const bool sigAdvance = signals.advance;
        const bool sigOpenLog = signals.openLog;
        const bool sigEsc = signals.openSystemMenu;
        const bool sigSkipHeld = signals.skipHeld;
        const bool sigWheelUp = signals.wheelUp;
        const bool sigWheelDown = signals.wheelDown;
        const bool sigToggleHide = signals.toggleHide;
        const bool inChoiceState = state == SceneMode::CHOICE;
        const bool inObserveState = state == SceneMode::OBSERVE;
        const bool inVideoState = state == SceneMode::VIDEO;
        bool uiRestoredThisFrame = false;

        if (inVideoState) {
            ResetDialogueModes();
            if (sigAdvance || sigLeftClick || sigRightClick || sigEsc) {
                result.commands |= Flag(CommandSkipVideo);
            }
            blockAdvanceThisFrame = false;
            return result;
        }

        if (!inChoiceState && sigToggleHide && !uiHiddenByUser) {
            uiHiddenByUser = true;
            dlg.visible = false;
            blockAdvanceThisFrame = true;
        } else if (uiHiddenByUser && (sigLeftClick || sigRightClick || sigWheelDown || sigWheelUp)) {
            uiHiddenByUser = false;
            dlg.visible = true;
            input.ConsumeLeftClick();
            uiRestoredThisFrame = true;
            blockAdvanceThisFrame = true;
        }

        if (!logScreen.visible && !uiHiddenByUser && !uiRestoredThisFrame && !inChoiceState &&
            !observeHandlesCancel && (sigEsc || sigRightClick)) {
            OpenMenu(SystemMenu::Page::Settings, true);
            result.commands |= Flag(CommandOpenSystemMenu);
            return result;
        }

        if (canSave && input.IsActionJustPressed(Input::Action::OpenSaveMenu)) {
            OpenMenu(SystemMenu::Page::Save, true);
            result.commands |= Flag(CommandOpenSaveMenu);
            return result;
        }

        if (canSave && input.IsActionJustPressed(Input::Action::OpenLoadMenu)) {
            OpenMenu(SystemMenu::Page::Load, true);
            result.commands |= Flag(CommandOpenLoadMenu);
            return result;
        }

        if (input.IsActionJustPressed(Input::Action::QuickSave)) {
            result.commands |= Flag(CommandQuickSave);
            return result;
        }

        if (input.IsActionJustPressed(Input::Action::QuickLoad)) {
            result.commands |= Flag(CommandQuickLoad);
            return result;
        }

        if (sigWheelUp && !uiHiddenByUser && !logScreen.visible) {
            result.commands |= Flag(CommandRollback);
            blockAdvanceThisFrame = false;
            return result;
        }

        if (logScreen.visible) {
            logScreen.Update(dt, input, scrollDelta, dlg);
            blockAdvanceThisFrame = false;
            return result;
        }

        if (sigOpenLog) {
            logScreen.Show();
            blockAdvanceThisFrame = false;
            return result;
        }

        if ((state == SceneMode::CHOICE || state == SceneMode::OBSERVE) && uiHiddenByUser) {
            uiHiddenByUser = false;
            dlg.visible = true;
        }

        if (state == SceneMode::DIALOGUE) {
            const QuickMenu::Action action = quickMenu.Update(dt,
                                                              input,
                                                              dlg.visible,
                                                              input.GetFocusInput(),
                                                              autoModeEnabled || dlg.autoMode,
                                                              skipModeEnabled);
            if (action != QuickMenu::NONE) {
                blockAdvanceThisFrame = true;
            }

            if (action == QuickMenu::HISTORY) {
                logScreen.Show();
                blockAdvanceThisFrame = false;
                return result;
            }

            if (action == QuickMenu::AUTO) {
                autoModeEnabled = !autoModeEnabled;
                dlg.autoMode = autoModeEnabled;
                skipModeEnabled = false;
                dlg.ResetAutoAdvanceTimer();
            }

            if (action == QuickMenu::SKIP) {
                skipModeEnabled = !skipModeEnabled;
                dlg.autoMode = false;
                autoModeEnabled = false;
                dlg.ResetAutoAdvanceTimer();
            }

            if (action == QuickMenu::QSAVE) {
                result.commands |= Flag(CommandQuickSave);
                return result;
            }

            if (action == QuickMenu::QLOAD) {
                result.commands |= Flag(CommandQuickLoad);
                return result;
            }

            if (action == QuickMenu::HIDE_UI) {
                uiHiddenByUser = true;
                dlg.visible = false;
                blockAdvanceThisFrame = true;
            }

            if (action == QuickMenu::SAVE) {
                OpenMenu(SystemMenu::Page::Save, true);
                result.commands |= Flag(CommandOpenSaveMenu);
                return result;
            }

            if (action == QuickMenu::LOAD) {
                OpenMenu(SystemMenu::Page::Load, true);
                result.commands |= Flag(CommandOpenLoadMenu);
                return result;
            }

            if (action == QuickMenu::PREFS) {
                OpenMenu(SystemMenu::Page::Settings, true);
                result.commands |= Flag(CommandOpenSystemMenu);
                return result;
            }

            bool isSkipping = (skipModeEnabled || sigSkipHeld) && CanSkipCurrentLine();
            if ((skipModeEnabled || sigSkipHeld) && !CanSkipCurrentLine()) {
                skipModeEnabled = false;
                isSkipping = false;
            }

            if (IsDialogueInteractable()) {
                if (isSkipping) {
                    const float skipDt = dt * 2.5f;
                    dlg.Update(skipDt, true);
                    skipTimer += skipDt;
                    const bool tickAdvance = skipTimer >= 0.08f;
                    if (tickAdvance && !IsAdvanceBlocked() && dlg.HandleAdvance(true)) {
                        result.commands |= Flag(CommandAdvanceDialogue);
                    }
                    if (tickAdvance) {
                        skipTimer = 0.0f;
                    }

                    if ((sigLeftClick || sigRightClick || scrollDelta != 0) && !IsAdvanceBlocked()) {
                        skipModeEnabled = false;
                        input.ConsumeLeftClick();
                    }
                } else if (autoModeEnabled || dlg.autoMode) {
                    dlg.Update(dt, false);
                    if ((sigLeftClick || sigRightClick || scrollDelta != 0) && !IsAdvanceBlocked()) {
                        autoModeEnabled = false;
                        dlg.autoMode = false;
                        dlg.ResetAutoAdvanceTimer();
                        input.ConsumeLeftClick();
                    }

                    const bool autoCanAdvance = dlg.readyToAutoAdvance &&
                        (dlg.HasMorePages() || !voicePlaying);
                    if (autoCanAdvance && dlg.HandleAdvance(true)) {
                        result.commands |= Flag(CommandAdvanceDialogue);
                    }
                } else {
                    dlg.Update(dt, false);
                    const bool advanceSignal = sigAdvance || sigLeftClick;
                    if (dlg.HandleAdvance(advanceSignal && !IsAdvanceBlocked())) {
                        result.commands |= Flag(CommandAdvanceDialogue);
                    }
                }
            } else {
                dlg.Update(dt, false);
            }
        } else if (state == SceneMode::CHOICE) {
            ResetDialogueModes();

            const int selection = menu.Update(dt,
                                              input.GetMouseX(),
                                              input.GetMouseY(),
                                              input.JustClicked() && !IsAdvanceBlocked(),
                                              input.GetFocusInput());
            if (selection >= 0) {
                input.ConsumeLeftClick();
                result.selectedChoice = selection;
                result.commands |= Flag(CommandChoiceSelected);
            }
        } else if (inObserveState) {
            ResetDialogueModes();
        }

        blockAdvanceThisFrame = false;
        return result;
    }
};

UIManager::UIManager()
    : m_impl(std::make_unique<Impl>()) {}

UIManager::~UIManager() = default;
UIManager::UIManager(UIManager&&) noexcept = default;
UIManager& UIManager::operator=(UIManager&&) noexcept = default;

void UIManager::Init(const InitContext& context) {
    auto& impl = *m_impl;
    impl.renderer = context.renderer;
    impl.font = context.font;
    impl.width = context.width;
    impl.height = context.height;
    impl.assets = context.assets;
    impl.saveManager = context.saveManager;
    impl.fontPath = context.fontPath;
    impl.fontPointSize = context.fontPointSize;

    impl.dlg.Init(context.renderer, context.font, context.width, context.height, context.assets, context.fontPath, context.fontPointSize);
    impl.menu.Init(context.font, context.width, context.height, context.assets, context.fontPath, context.fontPointSize);
    impl.quickMenu.Init(context.font, context.width, context.height, context.assets, context.fontPath, context.fontPointSize);
    impl.logScreen.Init(context.font, context.width, context.height, context.assets, context.fontPath, context.fontPointSize);
    impl.titleScreen.Init(context.renderer,
                          context.font,
                          context.width,
                          context.height,
                          context.titleBackgroundPath,
                          context.assets,
                          context.fontPath,
                          context.fontPointSize);
    impl.systemMenu.Init(context.font, context.width, context.height, context.saveManager, context.assets, context.fontPath, context.fontPointSize);
    impl.ApplyRuntimeConfigToWidgets();
    impl.ApplyStoredTranslator();
}

void UIManager::ReleaseResources() {
    auto& impl = *m_impl;
    impl.dlg.ReleaseResources();
    impl.menu.ReleaseResources();
    impl.quickMenu.ReleaseResources();
    impl.logScreen.ReleaseResources();
    impl.systemMenu.ReleaseResources();
    impl.titleScreen.Shutdown();
}

void UIManager::SetTranslator(std::function<std::string(const std::string&)> translator) {
    auto& impl = *m_impl;
    impl.translator = std::move(translator);
    impl.ApplyStoredTranslator();
}

void UIManager::RefreshStrings() {
    auto& impl = *m_impl;
    impl.ApplyStoredTranslator();
    impl.titleScreen.RefreshStrings();
    impl.quickMenu.RebuildButtons();
    impl.systemMenu.RefreshStrings();
}

void UIManager::ApplyConfig(const RuntimeConfig& config) {
    auto& impl = *m_impl;
    impl.config = config;
    impl.ApplyRuntimeConfigToWidgets();
}

UIManager::UpdateResult UIManager::Update(float dt, Input& input, SceneMode state) {
    auto& impl = *m_impl;
    if (impl.surface == Impl::Surface::Title) {
        return impl.UpdateTitle(dt, input);
    }
    if (impl.surface == Impl::Surface::SystemMenu) {
        return impl.UpdateSystem(dt, input);
    }
    return impl.UpdateInGame(dt, input, state);
}

void UIManager::Render(SDL_Renderer* renderer, SceneMode state) {
    auto& impl = *m_impl;
    if (impl.surface == Impl::Surface::Title) {
        impl.titleScreen.Render(renderer);
        return;
    }

    if (impl.surface == Impl::Surface::InGame) {
        if (state == SceneMode::DIALOGUE || state == SceneMode::CHOICE) {
            impl.dlg.Render(renderer);
            if (state == SceneMode::DIALOGUE && impl.quickMenu.IsRendering()) {
                impl.quickMenu.Render(renderer, impl.autoModeEnabled || impl.dlg.autoMode, impl.skipModeEnabled);
            }
        }

        if (state == SceneMode::CHOICE) {
            SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 140);
            const SDL_FRect focusDim{ 0.0f, 0.0f, static_cast<float>(impl.width), static_cast<float>(impl.height) };
            SDL_RenderFillRect(renderer, &focusDim);
            impl.menu.Render(renderer);
        }

        impl.logScreen.Render(renderer, impl.dlg);

        if (state == SceneMode::DIALOGUE) {
            if (impl.skipModeEnabled) {
                impl.DrawText(renderer, ">> SKIP MODE", impl.width - 190.0f, 20.0f, ui::theme::kAccentPrimary);
            } else if (impl.autoModeEnabled || impl.dlg.autoMode) {
                impl.DrawText(renderer, ">> AUTO MODE", impl.width - 190.0f, 20.0f, ui::theme::kBluePrimary);
            }
        }

        if (impl.uiHiddenByUser) {
            impl.DrawText(renderer,
                          "Right-click or Left-click to return",
                          impl.width - 340.0f,
                          impl.height - 34.0f,
                          ui::theme::WithAlpha(ui::theme::kTextSecondary, 60));
        }
        return;
    }

    if (impl.returnSurface == Impl::Surface::Title) {
        impl.titleScreen.Render(renderer);
    }
    impl.systemMenu.Render(renderer);
}

void UIManager::EnterTitleMenu(bool continueEnabled) {
    auto& impl = *m_impl;
    impl.surface = Impl::Surface::Title;
    impl.returnSurface = Impl::Surface::Title;
    impl.dlg.SetLog({});
    impl.logScreen.Hide();
    impl.menu.Hide();
    impl.systemMenu.Close();
    impl.ResetDialogueModes();
    impl.uiHiddenByUser = false;
    impl.dlg.visible = true;
    impl.titleScreen.ResetHover();
    impl.titleScreen.SetContinueEnabled(continueEnabled);
}

void UIManager::EnterInGame() {
    auto& impl = *m_impl;
    impl.surface = Impl::Surface::InGame;
    impl.returnSurface = Impl::Surface::InGame;
    impl.logScreen.Hide();
    impl.systemMenu.Close();
    impl.ResetDialogueModes();
    impl.uiHiddenByUser = false;
    impl.dlg.visible = true;
    impl.blockAdvanceThisFrame = true;
}

void UIManager::SetContinueEnabled(bool enabled) {
    m_impl->titleScreen.SetContinueEnabled(enabled);
}

void UIManager::SetContinueThumbnailFromPath(AssetManager& assets, const std::string& thumbnailPath) {
    m_impl->titleScreen.SetContinueThumbnail(thumbnailPath.empty() ? AssetManager::TextureHandle{} : assets.GetTexture(thumbnailPath));
}

void UIManager::ClearContinueThumbnail() {
    m_impl->titleScreen.SetContinueThumbnail(nullptr);
}

void UIManager::OpenSystemMenu(bool inGame) {
    m_impl->OpenMenu(SystemMenu::Page::Settings, inGame);
}

void UIManager::OpenSaveMenu(bool inGame) {
    m_impl->OpenMenu(SystemMenu::Page::Save, inGame);
}

void UIManager::OpenLoadMenu(bool inGame) {
    m_impl->OpenMenu(SystemMenu::Page::Load, inGame);
}

void UIManager::OpenExitConfirm(bool inGame) {
    m_impl->OpenExit(inGame);
}

void UIManager::CloseSystemMenu() {
    auto& impl = *m_impl;
    impl.systemMenu.Close();
    impl.surface = impl.returnSurface;
}

void UIManager::RefreshSaveSlots() {
    m_impl->systemMenu.RefreshSlots();
}

void UIManager::SetDialogueLog(const std::vector<DialogueLine>& log) {
    m_impl->dlg.SetLog(log);
}

std::vector<DialogueLine> UIManager::DialogueLogSnapshot() const {
    const auto& log = m_impl->dlg.GetLog();
    return std::vector<DialogueLine>(log.begin(), log.end());
}

void UIManager::ShowDialogue(const std::string& speaker,
                             const std::string& text,
                             const std::string& lineId,
                             const std::string& voicePath) {
    m_impl->ShowDialogue(speaker, text, lineId, voicePath);
}

void UIManager::ShowChoices(const std::vector<std::string>& choices) {
    m_impl->ShowChoices(choices);
}

void UIManager::ClearDialogue() {
    m_impl->ClearDialogue();
}

void UIManager::HideChoices() {
    m_impl->HideChoices();
}

void UIManager::ShowLog() {
    m_impl->logScreen.Show();
}

void UIManager::HideLog() {
    m_impl->logScreen.Hide();
}

void UIManager::SetDialogueVisible(bool visible) {
    auto& impl = *m_impl;
    impl.uiHiddenByUser = !visible;
    impl.dlg.visible = visible;
}

void UIManager::SetCanSave(bool canSave) {
    m_impl->canSave = canSave;
}

void UIManager::SetCurrentLineSeen(bool seen) {
    m_impl->currentLineSeen = seen;
}

void UIManager::SetVoicePlaying(bool voicePlaying) {
    m_impl->voicePlaying = voicePlaying;
}

void UIManager::SetObserveHandlesCancel(bool observeHandlesCancel) {
    m_impl->observeHandlesCancel = observeHandlesCancel;
}

void UIManager::BlockAdvanceThisFrame() {
    m_impl->blockAdvanceThisFrame = true;
}

bool UIManager::IsDialogueVisible() const {
    return m_impl->dlg.visible;
}

bool UIManager::IsUIHiddenByUser() const {
    return m_impl->uiHiddenByUser;
}

bool UIManager::IsLogVisible() const {
    return m_impl->logScreen.visible;
}

bool UIManager::AreChoicesVisible() const {
    return m_impl->menu.visible;
}

bool UIManager::IsSystemMenuOpen() const {
    return m_impl->surface == Impl::Surface::SystemMenu;
}

bool UIManager::IsAutoModeEnabled() const {
    return m_impl->autoModeEnabled || m_impl->dlg.autoMode;
}

bool UIManager::IsSkipModeEnabled() const {
    return m_impl->skipModeEnabled;
}

bool UIManager::WantsHandCursor(SceneMode state, float mouseX, float mouseY) const {
    const auto& impl = *m_impl;
    if (impl.surface == Impl::Surface::Title) {
        return impl.titleScreen.GetHoveredIndex() >= 0;
    }
    if (impl.surface == Impl::Surface::SystemMenu) {
        return impl.systemMenu.GetHoverSignature(mouseX, mouseY).IsValid();
    }
    if (state == SceneMode::CHOICE) {
        return impl.menu.GetHoveredIndex() >= 0;
    }
    if (state == SceneMode::DIALOGUE && impl.quickMenu.IsRendering() && !impl.logScreen.visible) {
        return impl.quickMenu.GetHoveredIndex() >= 0;
    }
    return false;
}

} // namespace vn
