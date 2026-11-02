#pragma once

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

#include "AssetManager.h"
#include "ActionQueue.h"
#include "AudioManager.h"
#include "DialogueLine.h"
#include "EngineContext.h"
#include "GameSettings.h"
#include "Input.h"
#include "I18nManager.h"
#include "LuaScriptContext.h"
#include "ObserveManager.h"
#include "SaveManager.h"
#include "InteractionSystem.h"
#include "RenderPipeline.h"
#include "SceneManager.h"
#include "Profiling.h"
#include "SystemInitializer.h"
#include "UIManager.h"

class Engine {
public:
    ~Engine() {
        Shutdown();
    }

    bool Init(const char* title, int w, int h) {
        return SystemInitializer::InitAll(*this, title, w, h);
    }

    void Run() {
        Uint64 prev = SDL_GetTicks();

        while (!m_input.WantsQuit()) {
            const Uint64 now = SDL_GetTicks();
            float dt = static_cast<float>(now - prev) / 1000.0f;
            prev = now;
            dt = std::min(dt, 0.05f);

            m_input.BeginFrame();
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                m_input.ProcessEvent(event);
            }
            m_input.EndFrame();
            HandleInput();
            m_assets.ProcessAsyncLoads();
            m_cachePruneTimer += dt;
            if (m_cachePruneTimer >= 2.0f) {
                m_assets.ClearUnused();
                m_cachePruneTimer = 0.0f;
            }
            Update(dt);
            Render();
        }
    }

    void Shutdown() {
        if (m_shutdown) {
            return;
        }
        m_shutdown = true;

        if (m_appState == AppState::IN_GAME) {
            TryAutoSaveForContinue();
        }

        SaveGlobalData();

        // Stop script runtime first so no coroutine can touch subsystems during teardown.
        m_scene.ResetScriptRuntime();
        // Defensive flush in case future reset paths stop flushing queued actions.
        m_actionQueue.FlushAll();

        m_scene.ReleaseAssets();
        m_uiManager.ReleaseResources();
        m_observe.ReleaseResources();

        // Ensure mixer channels are halted before chunk/music caches are dropped.
        m_audio.StopVoice(0);
        m_audio.StopAllSFX();
        m_audio.StopBGM(0);
        m_audio.Shutdown();

        if (m_cursorArrow) {
            SDL_DestroyCursor(m_cursorArrow);
            m_cursorArrow = nullptr;
        }
        if (m_cursorHand) {
            SDL_DestroyCursor(m_cursorHand);
            m_cursorHand = nullptr;
        }
        m_isHandCursorActive = false;

        // Explicitly detach scene from managers/components before clearing caches.
        m_scene.SetAudioManager(nullptr);
        m_scene.SetAssetManager(nullptr);
        m_scene.SetActionQueue(nullptr);
        m_scene.ClearUICallbacks();
        
        // Clear all font handles before shutting down SDL_ttf so FontDeleter can close fonts safely.
        m_font = nullptr;
        m_fontHandle.reset();
        m_assets.ClearAll(false);

    #if defined(IMG_INIT_PNG) && defined(IMG_INIT_JPG)
        IMG_Quit();
    #endif
        if (TTF_WasInit() > 0) {
            TTF_Quit();
        }
        
        // Clear renderer references last before destroying it
        m_input.Shutdown();
        m_assets.SetRenderer(nullptr);
        m_input.SetRenderer(nullptr);
        
        if (m_renderer) {
            SDL_DestroyRenderer(m_renderer);
            m_renderer = nullptr;
        }
        if (m_window) {
            SDL_DestroyWindow(m_window);
            m_window = nullptr;
        }
        SDL_Quit();
    }

private:
    friend class SystemInitializer;

    void PrewarmImageDecoders() {
        // Force first image decode on main thread to avoid lazy init races.
        constexpr const char* candidates[] = {
            "assets/bg/classroom.png",
            "assets/sprites/aria_normal.png"
        };

        for (const char* path : candidates) {
            SDL_Surface* surface = IMG_Load(path);
            if (surface) {
                SDL_DestroySurface(surface);
                return;
            }
        }
    }

    enum class AppState {
        TITLE_MENU,
        IN_GAME,
        SYSTEM_MENU,
    };

    struct IAppStateHandler {
        virtual ~IAppStateHandler() = default;
        virtual void Update(Engine& engine, float dt) = 0;
        virtual void Render(Engine& engine) = 0;
    };

    struct TitleMenuStateHandler final : IAppStateHandler {
        void Update(Engine& engine, float dt) override {
            (void)dt;
            engine.UpdateTitleScreen();
        }

        void Render(Engine& engine) override {
            engine.RenderTitleScreen();
        }
    };

    struct InGameStateHandler final : IAppStateHandler {
        void Update(Engine& engine, float dt) override {
            engine.UpdateInGame(dt);
        }

        void Render(Engine& engine) override {
            engine.RenderInGame();
        }
    };

    struct SystemMenuStateHandler final : IAppStateHandler {
        void Update(Engine& engine, float dt) override {
            engine.UpdateSystemMenu(dt);
        }

        void Render(Engine& engine) override {
            engine.RenderSystemMenu();
        }
    };

    class StateManager {
    public:
        void Register(AppState state, IAppStateHandler* handler) {
            m_handlers[Index(state)] = handler;
        }

        void SetCurrent(AppState state) {
            m_current = state;
        }

        void Update(Engine& engine, float dt) const {
            if (IAppStateHandler* handler = CurrentHandler()) {
                handler->Update(engine, dt);
            }
        }

        void Render(Engine& engine) const {
            if (IAppStateHandler* handler = CurrentHandler()) {
                handler->Render(engine);
            }
        }

    private:
        static constexpr std::size_t Index(AppState state) {
            return static_cast<std::size_t>(state);
        }

        IAppStateHandler* CurrentHandler() const {
            return m_handlers[Index(m_current)];
        }

        std::array<IAppStateHandler*, 3> m_handlers{};
        AppState m_current = AppState::TITLE_MENU;
    };

    void InitializeStateManager() {
        m_stateManager.Register(AppState::TITLE_MENU, &m_titleMenuStateHandler);
        m_stateManager.Register(AppState::IN_GAME, &m_inGameStateHandler);
        m_stateManager.Register(AppState::SYSTEM_MENU, &m_systemMenuStateHandler);
        m_stateManager.SetCurrent(m_appState);
    }

    void ChangeAppState(AppState next) {
        m_appState = next;
        m_stateManager.SetCurrent(next);
    }

    void RefreshEngineContext() noexcept {
        m_context.renderer = m_renderer;
        m_context.assets = &m_assets;
        m_context.audio = &m_audio;
        m_context.ui = &m_uiManager;
        m_context.actions = &m_actionQueue;
        m_context.saves = &m_saveManager;
        m_context.scene = &m_scene;
        m_context.input = &m_input;
        m_context.observe = &m_observe;
        m_context.renderPipeline = &m_renderPipeline;
        m_context.interactions = &m_interactionSystem;
    }

    bool LoadFonts() {
        const std::vector<std::string> candidates = {
            "assets/fonts/NotoSansCJK-Regular.ttc",
            "assets/fonts/NotoSansCJKsc-Regular.otf",
            "assets/fonts/NotoSansSC-Regular.ttf",
            "C:/Windows/Fonts/msyh.ttc",
            "C:/Windows/Fonts/simhei.ttf",
            "/System/Library/Fonts/PingFang.ttc",
            "assets/fonts/NotoSans-Regular.ttf",
            "assets/fonts/Noto_Sans/static/NotoSans-Regular.ttf",
            "assets/fonts/Noto_Sans/NotoSans-VariableFont_wdth,wght.ttf"
        };

        for (const auto& path : candidates) {
            m_fontHandle = m_assets.GetFont(path, 22.0f);
            if (m_fontHandle) {
                m_font = m_fontHandle.get();
                m_fontPath = path;
                return true;
            }
        }
        return false;
    }

    void ApplyConfig() {
        m_uiManager.ApplyConfig(BuildUIRuntimeConfig());
        m_scene.m_enableBreathingEffect = m_settings.animatedSprites;
        m_scene.m_enableParallaxEffect = m_settings.animatedSprites;
        m_audio.SetBGMVolume(m_settings.bgmVolume);
        m_audio.SetSFXVolume(m_settings.sfxVolume);
        m_audio.SetVoiceBleedEnabled(m_settings.voiceBleedEnabled);
    }

    [[nodiscard]] vn::UIManager::RuntimeConfig BuildUIRuntimeConfig() const {
        vn::UIManager::RuntimeConfig config;
        config.textSpeed = m_settings.textSpeed;
        config.bgmVolume = m_settings.bgmVolume;
        config.sfxVolume = m_settings.sfxVolume;
        config.localeCode = m_settings.locale;
        config.animatedSpritesEnabled = m_settings.animatedSprites;
        config.skipUnseenEnabled = m_settings.skipUnseen;
        config.wheelAdvanceEnabled = m_settings.wheelAdvanceEnabled;
        config.voiceBleedEnabled = m_settings.voiceBleedEnabled;
        config.fullscreenEnabled = m_settings.fullscreen;
        config.instantText = m_settings.instantText;
        return config;
    }

    void ApplyUIRuntimeConfig(const vn::UIManager::RuntimeConfig& config) {
        m_settings.textSpeed = config.textSpeed;
        m_settings.bgmVolume = config.bgmVolume;
        m_settings.sfxVolume = config.sfxVolume;
        m_settings.animatedSprites = config.animatedSpritesEnabled;
        m_settings.skipUnseen = config.skipUnseenEnabled;
        m_settings.wheelAdvanceEnabled = config.wheelAdvanceEnabled;
        m_settings.voiceBleedEnabled = config.voiceBleedEnabled;
        m_settings.instantText = config.instantText;

        if (config.localeCode != m_settings.locale) {
            (void)ApplyLocaleByCode(config.localeCode);
        }

        if (config.fullscreenEnabled != m_settings.fullscreen) {
            ToggleFullscreen();
        }

        ApplyConfig();
        SaveGlobalData();
    }

    void InitializeI18n() {
        (void)ApplyLocaleByCode(m_settings.locale);
    }

    void BindLocalizationCallbacks() {
        auto tr = [this](const std::string& key) {
            return m_i18n.Get(key);
        };

        m_uiManager.SetTranslator(tr);
        m_observe.SetTranslator(tr);
        m_scene.SetTranslationResolver(tr);
    }

    void RefreshAllUIStrings() {
        BindLocalizationCallbacks();
        m_uiManager.RefreshStrings();
    }

    std::string LocaleFileByCode(const std::string& code) const {
        if (code == "en") return "assets/locales/en.lua";
        if (code == "ja") return "assets/locales/ja.lua";
        return "assets/locales/zh.lua";
    }

    bool ApplyLocaleByCode(const std::string& requestedCode) {
        std::string code = requestedCode;
        if (code != "zh" && code != "en" && code != "ja") {
            code = "zh";
        }

#if defined(VN_ENGINE_HAS_LUA) && VN_ENGINE_HAS_LUA
        sol::state lua;
        lua.open_libraries(sol::lib::base,
                           sol::lib::string,
                           sol::lib::table,
                           sol::lib::utf8);

        std::string localePath = LocaleFileByCode(code);
        if (!m_i18n.Init(lua, localePath)) {
            localePath = LocaleFileByCode("en");
            if (!m_i18n.Init(lua, localePath)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "I18n locale load failed for requested and fallback locales");
                return false;
            }
            code = "en";
        }

        m_settings.locale = code;
        m_localeFile = localePath;
        RefreshAllUIStrings();
        return true;
#else
        (void)code;
        return false;
#endif
    }

    void LoadGlobalData() {
        SaveManager::GlobalSaveData global;
        if (!m_saveManager.LoadGlobal(global)) {
            return;
        }

        m_settings.textSpeed = global.textSpeed;
        m_settings.bgmVolume = global.bgmVolume;
        m_settings.sfxVolume = global.sfxVolume;
        m_settings.locale = global.locale;
        m_settings.skipUnseen = global.skipUnseen;
        m_settings.wheelAdvanceEnabled = global.wheelAdvanceEnabled;
        m_settings.voiceBleedEnabled = global.voiceBleedEnabled;
        m_settings.instantText = global.instantText;
        m_settings.animatedSprites = global.animatedSprites;
        m_settings.fullscreen = global.fullscreen;
        m_settings.unlockFlags = global.unlockFlags;
        m_gameState.seenLines = global.seenLines;

        m_audio.SetVoiceBleedEnabled(m_settings.voiceBleedEnabled);

        (void)ApplyLocaleByCode(m_settings.locale);

        if (m_window) {
            SDL_SetWindowFullscreen(m_window, m_settings.fullscreen);
        }
    }

    void SaveGlobalData() {
        SaveManager::GlobalSaveData global;
        global.textSpeed = m_settings.textSpeed;
        global.locale = m_settings.locale;
        global.bgmVolume = m_settings.bgmVolume;
        global.sfxVolume = m_settings.sfxVolume;
        global.fullscreen = m_settings.fullscreen;
        global.skipUnseen = m_settings.skipUnseen;
        global.wheelAdvanceEnabled = m_settings.wheelAdvanceEnabled;
        global.voiceBleedEnabled = m_settings.voiceBleedEnabled;
        global.instantText = m_settings.instantText;
        global.animatedSprites = m_settings.animatedSprites;
        global.unlockFlags = m_settings.unlockFlags;
        global.seenLines = m_gameState.seenLines;
        (void)m_saveManager.SaveGlobal(global);
    }



    bool LoadScriptFromFile(const std::string& path) {
        const std::filesystem::path requestedPath(path);
        if (requestedPath.extension() != ".lua") {
            std::cerr << "[Load] Lua-only mode requires .lua scripts: " << path << "\n";
            return false;
        }
        const bool loaded = m_scene.LoadLuaScript(requestedPath.string());
        return loaded;
    }

    void StartNewGame() {
        m_audio.StopBGM(300);
        m_audio.StopVoice(0);
        m_audio.StopAllSFX();
        m_uiManager.SetDialogueLog({});

        (void)LoadScriptFromFile("assets/scripts/demo.lua");

        m_uiManager.EnterInGame();
        ChangeAppState(AppState::IN_GAME);
    }

    bool LoadFromSlot(int slot) {
        m_interactionSystem.Reset();
        vn::SaveSnapshot save;
        if (!m_saveManager.LoadSlot(slot, save)) {
            m_toastMessage = "Load Failed: Invalid or Missing Save";
            m_toastTimer = 2.2f;
            return false;
        }

        m_uiManager.SetDialogueLog(save.dialogueLog);

        if (!m_scene.LoadLuaScript(save.scriptPath, save.luaEntryPoint, false)) {
            m_toastMessage = "Load Failed: Script Invalid";
            m_toastTimer = 2.2f;
            return false;
        }

        // Prefer replay restore so Lua coroutine state can continue from the saved resume id.
        // SceneManager already falls back to snapshot-only when replay diverges.
        m_scene.RestoreFromSave(save, SceneManager::RestoreMode::Replay);
        if (m_scene.HasRestoreWarning()) {
            m_toastMessage = m_scene.ConsumeRestoreWarning();
            m_toastTimer = 3.2f;
        }

        m_uiManager.EnterInGame();
        m_uiManager.BlockAdvanceThisFrame();
        ChangeAppState(AppState::IN_GAME);
        return true;
    }

    bool HasSaveInSlot(int slot) {
        vn::SaveSnapshot dummy;
        return m_saveManager.LoadSlot(slot, dummy);
    }

    bool CanAutoSaveCurrentState() const {
        return m_scene.state != SceneMode::CHOICE &&
               m_scene.state != SceneMode::OBSERVE &&
               m_scene.state != SceneMode::VIDEO;
    }

    void TryAutoSaveForContinue() {
        if (!CanAutoSaveCurrentState()) {
            return;
        }

        if (m_saveManager.SaveSlot(1, BuildSaveDataWithDialogueLog())) {
            SaveSlotThumbnailFromGameplay(1);
            SaveGlobalData();
        }
    }

    [[nodiscard]] vn::SaveSnapshot BuildSaveDataWithDialogueLog() const {
        vn::SaveSnapshot data = m_scene.BuildSaveData();
        data.dialogueLog = m_uiManager.DialogueLogSnapshot();
        return data;
    }

    void EnterTitleMenu() {
        m_interactionSystem.Reset();
        m_scene.ResetScriptRuntime();
        m_uiManager.SetDialogueLog({});
        ChangeAppState(AppState::TITLE_MENU);
        m_audio.StopVoice(0);
        m_audio.StopAllSFX();
        m_observe.Stop();
        m_observeStarted = false;
        m_observeStartedRevision = 0;
        const bool hasAutoSave = HasSaveInSlot(1);
        m_uiManager.EnterTitleMenu(hasAutoSave);

        if (hasAutoSave) {
            auto slots = m_saveManager.ListSlots(1, 0);
            if (!slots.empty() && slots[0].hasThumbnail && !slots[0].thumbnailPath.empty()) {
                m_uiManager.SetContinueThumbnailFromPath(m_assets, slots[0].thumbnailPath);
            } else {
                m_uiManager.ClearContinueThumbnail();
            }
        } else {
            m_uiManager.ClearContinueThumbnail();
        }

        m_audio.PlayBGM("assets/audio/bgm_theme.ogg", -1, 300);
    }

    void HandleInput() {
        if (m_input.IsActionJustPressed(Input::Action::ToggleDebugOverlay)) {
            m_showDebugOverlay = !m_showDebugOverlay;
        }
    }

    void Update(float dt) {
        ZoneScoped;
        if (m_input.IsActionJustPressed(Input::Action::ToggleFullscreen)) {
            ToggleFullscreen();
        }

        m_audio.Tick(dt);

        // Update toast timer
        if (m_toastTimer > 0.0f) {
            m_toastTimer -= dt;
            if (m_toastTimer < 0.0f) m_toastTimer = 0.0f;
        }

        m_stateManager.Update(*this, dt);

        UpdateGlobalCursor();
    }

    void UpdateGlobalCursor() {
        if (!m_cursorArrow || !m_cursorHand) {
            return;
        }

        // Observe mode manages its own cursor state internally.
        if (m_appState == AppState::IN_GAME && m_scene.state == SceneMode::OBSERVE) {
            if (m_isHandCursorActive) {
                SDL_SetCursor(m_cursorArrow);
                m_isHandCursorActive = false;
            }
            return;
        }

        bool wantsHandCursor = false;

        wantsHandCursor = m_uiManager.WantsHandCursor(m_scene.state, m_input.GetMouseX(), m_input.GetMouseY());
        if (m_appState == AppState::IN_GAME &&
            m_scene.state == SceneMode::DIALOGUE &&
            m_uiManager.IsDialogueVisible() &&
            !m_uiManager.IsLogVisible()) {
            wantsHandCursor = wantsHandCursor || (m_interactionSystem.GetHoveredSprite() >= 0);
        }

        if (wantsHandCursor != m_isHandCursorActive) {
            SDL_SetCursor(wantsHandCursor ? m_cursorHand : m_cursorArrow);
            m_isHandCursorActive = wantsHandCursor;
        }
    }

    void UpdateTitleScreen() {
        m_uiManager.SetCanSave(false);
        auto result = m_uiManager.Update(0.0f, m_input, m_scene.state);

        if (result.Has(vn::UIManager::CommandStartNewGame)) {
            StartNewGame();
            return;
        }

        if (result.Has(vn::UIManager::CommandContinueGame)) {
            if (LoadFromSlot(1)) {
                return;
            }
            m_uiManager.SetContinueEnabled(false);
            return;
        }

        if (result.Has(vn::UIManager::CommandOpenLoadMenu) ||
            result.Has(vn::UIManager::CommandOpenSystemMenu)) {
            m_returnState = AppState::TITLE_MENU;
            ChangeAppState(AppState::SYSTEM_MENU);
            return;
        }
    }

    void UpdateInGame(float dt) {
        if (m_input.IsActionJustPressed(Input::Action::CopyWorldCoords)) {
            const float mx = m_input.GetMouseX();
            const float my = m_input.GetMouseY();
            const Camera& cam = m_scene.GetCamera();
            const float zoom = std::max(0.1f, cam.zoom);
            const SDL_FPoint camOffset = m_scene.GetCameraOffset();

            const float worldX = (mx - camOffset.x - m_W * 0.5f) / zoom + m_W * 0.5f + cam.x;
            const float worldY = (my - camOffset.y - m_H * 0.5f) / zoom + m_H * 0.5f + cam.y;

            char coords[128];
            SDL_snprintf(coords, sizeof(coords), "%.1f, %.1f", worldX, worldY);
            if (SDL_SetClipboardText(coords)) {
                m_toastMessage = std::string("[Debug] Copied: ") + coords;
            } else {
                m_toastMessage = "[Debug] Clipboard copy failed";
            }
            m_toastTimer = 2.0f;
            m_uiManager.BlockAdvanceThisFrame();
        }

        if (m_showDebugOverlay) {
            m_hotReloadTimer += dt;
            if (m_hotReloadTimer >= 0.5f) {
                m_hotReloadTimer = 0.0f;

                const vn::SaveSnapshot snapshot = BuildSaveDataWithDialogueLog();
                if (!snapshot.scriptPath.empty()) {
                    std::error_code ec;
                    const auto modTime = std::filesystem::last_write_time(snapshot.scriptPath, ec);
                    if (!ec) {
                        if (m_hotReloadTrackedScript != snapshot.scriptPath) {
                            m_hotReloadTrackedScript = snapshot.scriptPath;
                            m_lastScriptTime = modTime;
                        } else if (m_lastScriptTime != std::filesystem::file_time_type{} && modTime > m_lastScriptTime) {
                            m_lastScriptTime = modTime;

                            if (m_scene.LoadLuaScript(snapshot.scriptPath, snapshot.luaEntryPoint, false)) {
                                m_scene.RestoreFromSave(snapshot);
                                m_toastMessage = "[Debug] Script Hot Reloaded";
                                m_toastTimer = 2.0f;
                                SDL_Log("Hot reloaded script: %s", snapshot.scriptPath.c_str());
                            } else {
                                m_toastMessage = "[Debug] Script Hot Reload Failed";
                                m_toastTimer = 2.0f;
                            }
                        } else if (m_lastScriptTime == std::filesystem::file_time_type{}) {
                            m_lastScriptTime = modTime;
                        }
                    }
                }
            }
        }

        if (m_scene.HasLuaError()) {
            if (m_input.IsActionJustPressed(Input::Action::Advance) || m_input.JustClicked()) {
                EnterTitleMenu();
            }
            return;
        }

        const SceneMode initialState = m_scene.state;
        const bool initialCanSave = initialState != SceneMode::CHOICE &&
                                    initialState != SceneMode::OBSERVE &&
                                    initialState != SceneMode::VIDEO;
        m_uiManager.SetCanSave(initialCanSave);
        m_uiManager.SetCurrentLineSeen(m_scene.IsCurrentLineSeen());
        m_uiManager.SetVoicePlaying(m_audio.IsVoicePlaying());
        m_uiManager.SetObserveHandlesCancel(initialState == SceneMode::OBSERVE && m_scene.ObserveAllowsCancel());

        if (initialState == SceneMode::VIDEO) {
            auto uiResult = m_uiManager.Update(dt, m_input, initialState);
            if (uiResult.Has(vn::UIManager::CommandSkipVideo) && m_scene.VideoAllowsSkip()) {
                m_scene.SkipVideo();
                m_uiManager.BlockAdvanceThisFrame();
                return;
            }
            m_scene.Update(dt, false);
            return;
        }

        bool skipIntent = initialState == SceneMode::DIALOGUE &&
            (m_uiManager.IsSkipModeEnabled() || m_input.IsActionHeld(Input::Action::SkipModifier));
        if (skipIntent && !m_scene.IsCurrentLineSeen() && !m_settings.skipUnseen) {
            skipIntent = false;
        }
        const float sceneDt = skipIntent ? dt * 2.5f : dt;
        m_scene.Update(sceneDt, skipIntent);

        const float parallaxNormX = std::clamp((m_input.GetMouseX() / static_cast<float>(m_W)) * 2.0f - 1.0f, -1.0f, 1.0f);
        const float parallaxNormY = std::clamp((m_input.GetMouseY() / static_cast<float>(m_H)) * 2.0f - 1.0f, -1.0f, 1.0f);
        m_scene.UpdateSpriteScreenRects(parallaxNormX, parallaxNormY);

        const SceneMode state = m_scene.state;
        const bool canSave = state != SceneMode::CHOICE &&
                             state != SceneMode::OBSERVE &&
                             state != SceneMode::VIDEO;
        const bool observeHandlesCancel = state == SceneMode::OBSERVE && m_scene.ObserveAllowsCancel();
        m_uiManager.SetCanSave(canSave);
        m_uiManager.SetCurrentLineSeen(m_scene.IsCurrentLineSeen());
        m_uiManager.SetVoicePlaying(m_audio.IsVoicePlaying());
        m_uiManager.SetObserveHandlesCancel(observeHandlesCancel);

        if (m_observeStarted && state != SceneMode::OBSERVE) {
            m_observe.Stop();
            m_observeStarted = false;
            m_observeStartedRevision = 0;
        }

        if (state == SceneMode::DIALOGUE &&
            !m_uiManager.IsUIHiddenByUser() &&
            m_uiManager.IsDialogueVisible() &&
            !m_uiManager.IsLogVisible()) {
            m_interactionSystem.Update(
                m_input.GetMouseX(),
                m_input.GetMouseY(),
                m_input.JustClicked(),
                m_scene.GetInteractiveHitboxes(),
                [this](const std::string& callbackName,
                       const std::string& characterTag,
                       const std::string& partTag,
                       const std::string& eventName) {
                    m_scene.OnSpriteInteraction(callbackName, characterTag, partTag, eventName);
                });
        }

        auto uiResult = m_uiManager.Update(dt, m_input, state);

        if (uiResult.Has(vn::UIManager::CommandOpenSystemMenu) ||
            uiResult.Has(vn::UIManager::CommandOpenSaveMenu) ||
            uiResult.Has(vn::UIManager::CommandOpenLoadMenu)) {
            m_returnState = AppState::IN_GAME;
            ChangeAppState(AppState::SYSTEM_MENU);
            return;
        }

        if (uiResult.Has(vn::UIManager::CommandQuickSave)) {
            if (canSave) {
                if (m_saveManager.QuickSave(BuildSaveDataWithDialogueLog())) {
                    SaveSlotThumbnailFromGameplay(0);
                    SaveGlobalData();
                    m_toastMessage = "Quick Saved";
                    m_toastTimer = 2.0f;
                }
            } else {
                m_toastMessage = "Cannot save right now";
                m_toastTimer = 2.0f;
            }
            return;
        }

        if (uiResult.Has(vn::UIManager::CommandQuickLoad)) {
            if (canSave && LoadFromSlot(0)) {
                m_toastMessage = "Quick Loaded";
                m_toastTimer = 2.0f;
            } else if (canSave) {
                m_toastMessage = "No Quick Save data found";
                m_toastTimer = 2.0f;
            }
            return;
        }

        if (uiResult.Has(vn::UIManager::CommandRollback)) {
            if (m_scene.CanRollback() && m_scene.Rollback()) {
                m_uiManager.BlockAdvanceThisFrame();
                return;
            }
            m_uiManager.ShowLog();
            return;
        }

        if (uiResult.Has(vn::UIManager::CommandAdvanceDialogue)) {
            if (!m_scene.AdvanceDialogue()) {
                EnterTitleMenu();
                return;
            }
        }

        if (uiResult.Has(vn::UIManager::CommandChoiceSelected) && uiResult.selectedChoice >= 0) {
            m_scene.OnChoiceSelected(uiResult.selectedChoice);
        }

        if (state == SceneMode::OBSERVE) {
            const Camera& cam = m_scene.GetCamera();
            m_observe.SetCameraTransform(cam.x, cam.y, cam.zoom, m_scene.GetCameraOffset());

            if (!m_observeStarted || m_observeStartedRevision != m_scene.GetObserveRevision()) {
                std::vector<Hotspot> hotspots;
                hotspots.reserve(m_scene.GetObserveHotspots().size());
                for (const auto& hotspot : m_scene.GetObserveHotspots()) {
                    hotspots.push_back({ hotspot.rect, hotspot.targetId, hotspot.hoverText });
                }

                m_observe.Start(
                    m_scene.GetBackgroundTexture(),
                    hotspots,
                    m_scene.GetObservePanScale(),
                    m_scene.ObserveAllowsCancel());

                m_observeStarted = true;
                m_observeStartedRevision = m_scene.GetObserveRevision();
            }

            const bool mouseClicked = m_input.JustClicked();
            m_observe.Update(dt, m_input);

            if (mouseClicked) {
                const std::string clickedTarget = m_observe.GetHoveredTargetId();
                if (!clickedTarget.empty()) {
                    m_scene.OnObserveTargetSelected(clickedTarget);
                }
            } else if (observeHandlesCancel &&
                       (m_input.IsActionJustPressed(Input::Action::OpenSystemMenu) || m_input.JustRightClicked())) {
                m_scene.OnObserveTargetSelected("CANCEL");
            }
        }
    }

    void UpdateSystemMenu(float dt) {
        const SceneMode currentState = m_scene.state;
        const bool canSaveInMenu = currentState != SceneMode::CHOICE &&
                                   currentState != SceneMode::OBSERVE &&
                                   currentState != SceneMode::VIDEO;
        m_uiManager.SetCanSave(canSaveInMenu);

        auto result = m_uiManager.Update(dt, m_input, currentState);

        if (result.Has(vn::UIManager::CommandResume)) {
            ChangeAppState(m_returnState);
            return;
        }

        if (result.Has(vn::UIManager::CommandReturnToTitle)) {
            if (m_returnState == AppState::IN_GAME) {
                TryAutoSaveForContinue();
            }
            EnterTitleMenu();
            return;
        }

        if (result.Has(vn::UIManager::CommandQuit)) {
            if (m_returnState == AppState::IN_GAME) {
                TryAutoSaveForContinue();
            }
            m_input.RequestQuit();
            return;
        }

        if (result.Has(vn::UIManager::CommandSaveSlot)) {
            if (!canSaveInMenu) {
                m_toastMessage = "Cannot save in current mode";
                m_toastTimer = 2.0f;
                return;
            }

            const bool saved = m_saveManager.SaveSlot(result.saveSlot, BuildSaveDataWithDialogueLog());
            if (saved) {
                SaveSlotThumbnailFromGameplay(result.saveSlot);
                SaveGlobalData();
            }
            m_uiManager.RefreshSaveSlots();
            return;
        }

        if (result.Has(vn::UIManager::CommandLoadSlot)) {
            if (!canSaveInMenu) {
                m_toastMessage = "Cannot load in current mode";
                m_toastTimer = 2.0f;
                return;
            }

            if (LoadFromSlot(result.loadSlot)) {
                ChangeAppState(AppState::IN_GAME);
            }
            return;
        }

        if (result.Has(vn::UIManager::CommandDeleteSlot)) {
            (void)m_saveManager.DeleteSlot(result.deleteSlot);
            m_uiManager.RefreshSaveSlots();
            return;
        }

        if (result.Has(vn::UIManager::CommandConfigChanged)) {
            ApplyUIRuntimeConfig(result.config);
        }
    }
    void Render() {
        ZoneScoped;
        SDL_SetRenderDrawColor(m_renderer, 10, 10, 20, 255);
        SDL_RenderClear(m_renderer);

        m_stateManager.Render(*this);

        // Always render the debug overlay on top.
        RenderDebugOverlay();

        SDL_RenderPresent(m_renderer);
    }

    void RenderSystemMenu() {
        if (m_returnState == AppState::IN_GAME) {
            RenderInGame();
        }
        m_uiManager.Render(m_renderer, m_scene.state);
    }

    void RenderInGame() {
        const SceneMode state = m_scene.state;
        
        // Check for Lua runtime errors
        if (m_scene.HasLuaError()) {
            // Render error overlay
            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 200);
            SDL_FRect errorBg = { 0.0f, 0.0f, static_cast<float>(m_W), static_cast<float>(m_H) };
            SDL_RenderFillRect(m_renderer, &errorBg);
            
            DrawText("SCRIPT ERROR", m_W / 2.0f - 100.0f, m_H / 2.0f - 80.0f, { 255, 100, 100, 255 });
            
            // Draw error message (wrapped if needed)
            const std::string& errMsg = m_scene.GetLuaErrorMessage();
            DrawText(errMsg, 40.0f, m_H / 2.0f - 20.0f, { 220, 220, 220, 255 });
            
            DrawText("[Any key to return to title]", m_W / 2.0f - 150.0f, m_H / 2.0f + 80.0f, { 150, 150, 150, 255 });
            return;
        }
        
        if (state == SceneMode::OBSERVE) {
            m_observe.RenderBackground();
            m_observe.RenderOverlay();
            DrawLetterboxOverlay(m_scene.GetLetterboxRatio());
        } else {
            const float parallaxNormX = std::clamp((m_input.GetMouseX() / static_cast<float>(m_W)) * 2.0f - 1.0f, -1.0f, 1.0f);
            const float parallaxNormY = std::clamp((m_input.GetMouseY() / static_cast<float>(m_H)) * 2.0f - 1.0f, -1.0f, 1.0f);
            const vn::SceneRenderState sceneRenderState = m_scene.GetRenderState(parallaxNormX, parallaxNormY);
            m_renderPipeline.RenderScene(m_renderer, sceneRenderState, m_settings.animatedSprites);
            m_actionQueue.Render(m_renderer);
        }

        if (m_appState == AppState::IN_GAME) {
            m_uiManager.Render(m_renderer, state);
        }

        // Render toast notification at top-left to avoid overlapping dialogue UI.
        if (m_toastTimer > 0.0f && !m_toastMessage.empty()) {
            float alpha = 255.0f;
            float yOffset = 0.0f;
            if (m_toastTimer < 0.5f) {
                const float t = m_toastTimer / 0.5f;
                alpha = t * 255.0f;
                yOffset = (1.0f - t) * -10.0f;
            }

            const float toastX = 30.0f;
            const float toastY = 30.0f + yOffset;

            SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
            SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, static_cast<Uint8>(alpha * 0.6f));
            const SDL_FRect toastBg = { toastX - 10.0f, toastY - 6.0f, 220.0f, 36.0f };
            SDL_RenderFillRect(m_renderer, &toastBg);

            SDL_Color toastColor = { 240, 240, 245, static_cast<Uint8>(alpha) };
            DrawText(m_toastMessage, toastX, toastY, toastColor);
        }

    }

    void DrawLetterboxOverlay(float ratio) {
        const float clampedRatio = std::clamp(ratio, 0.0f, 0.45f);
        if (clampedRatio <= 0.0f) {
            return;
        }

        const float barHeight = static_cast<float>(m_H) * clampedRatio;
        if (barHeight <= 0.5f) {
            return;
        }

        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 245);

        const SDL_FRect top{ 0.0f, 0.0f, static_cast<float>(m_W), barHeight };
        const SDL_FRect bottom{ 0.0f, static_cast<float>(m_H) - barHeight, static_cast<float>(m_W), barHeight };
        SDL_RenderFillRect(m_renderer, &top);
        SDL_RenderFillRect(m_renderer, &bottom);
    }

    void RenderTitleScreen() {
        m_uiManager.Render(m_renderer, m_scene.state);
    }

    // Developer debug panel for engine state and hotkey hints.
    void RenderDebugOverlay() {
        if (!m_showDebugOverlay) return;

        // Draw a translucent black panel.
        SDL_FRect bg = { 10.0f, 10.0f, 420.0f, 460.0f };
        SDL_SetRenderDrawBlendMode(m_renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(m_renderer, 0, 0, 0, 220);
        SDL_RenderFillRect(m_renderer, &bg);
        SDL_SetRenderDrawColor(m_renderer, 100, 255, 100, 100);
        SDL_RenderRect(m_renderer, &bg);

        float y = 20.0f;
        auto print = [&](const std::string& txt, SDL_Color col = {255, 255, 255, 255}) {
            DrawText(txt, 25.0f, y, col);
            y += 22.0f;
        };

        print("--- ENGINE DEBUG (F2 Toggle) ---", {100, 255, 100, 255});
        print("AppState: " + std::to_string(static_cast<int>(m_appState)), {200, 200, 200, 255});
        print("Scene: " + std::to_string(static_cast<int>(m_scene.state)), {200, 200, 200, 255});
        print("UI Visible: " + std::string(m_uiManager.IsDialogueVisible() ? "TRUE" : "FALSE"),
              m_uiManager.IsDialogueVisible() ? SDL_Color{150, 255, 150, 255} : SDL_Color{255, 150, 150, 255});
        print("Skip: " + std::string(m_uiManager.IsSkipModeEnabled() ? "ON" : "OFF"),
              m_uiManager.IsSkipModeEnabled() ? SDL_Color{255, 200, 100, 255} : SDL_Color{200, 200, 200, 255});
        print("Auto: " + std::string(m_uiManager.IsAutoModeEnabled() ? "ON" : "OFF"),
              m_uiManager.IsAutoModeEnabled() ? SDL_Color{255, 200, 100, 255} : SDL_Color{200, 200, 200, 255});
        
        y += 8.0f;
        print("--- KEYBINDINGS ---", {100, 200, 255, 255});
        print("[Enter/Space/Click] Advance", {200, 220, 255, 255});
        print("[L-Ctrl Hold] Skip Text", {200, 220, 255, 255});
        print("[PgUp/Scroll Up] Log", {200, 220, 255, 255});
        print("[H/Mid-Click] Hide UI", {200, 220, 255, 255});
        print("[ESC/Right-Click] Menu", {200, 220, 255, 255});
        print("[F5] Save  [F9] Load", {200, 220, 255, 255});
        print("[F6/F8 or Alt+S/L] Quick S/L", {200, 220, 255, 255});
        print("[F11] Fullscreen", {200, 220, 255, 255});
        print("[F2] Toggle Debug", {100, 255, 100, 255});
        print("[F3] Copy World Coords", {100, 255, 100, 255});
    }





    void DrawText(const std::string& text, float x, float y, SDL_Color color) {
        if (!m_font || text.empty()) return;

        // Use AssetManager's text texture cache instead of creating/destroying every frame
        // This avoids GPU memory thrashing and significantly improves performance
        auto texHandle = m_assets.GetTextTexture(text, m_font, m_fontPath, m_fontPointSize, color);
        if (!texHandle) return;

        SDL_Texture* tex = texHandle.get();
        if (!tex) return;

        // Get texture dimensions for rendering
        float w = 0.0f, h = 0.0f;
        if (SDL_GetTextureSize(tex, &w, &h)) {
            SDL_FRect dst = { x, y, w, h };
            SDL_RenderTexture(m_renderer, tex, nullptr, &dst);
        }
    }

    void ToggleFullscreen() {
        if (!m_window) return;
        m_settings.fullscreen = !m_settings.fullscreen;
        SDL_SetWindowFullscreen(m_window, m_settings.fullscreen);
        SaveGlobalData();
    }

    void SaveSlotThumbnailFromGameplay(int slot) {
        if (!m_renderer || slot < 0) return;

        SDL_Texture* prevTarget = SDL_GetRenderTarget(m_renderer);
        SDL_Texture* captureTarget = SDL_CreateTexture(
            m_renderer,
            SDL_PIXELFORMAT_RGBA32,
            SDL_TEXTUREACCESS_TARGET,
            m_W,
            m_H
        );
        if (!captureTarget) {
            return;
        }

        if (!SDL_SetRenderTarget(m_renderer, captureTarget)) {
            SDL_DestroyTexture(captureTarget);
            return;
        }

        SDL_SetRenderDrawColor(m_renderer, 10, 10, 20, 255);
        SDL_RenderClear(m_renderer);
        RenderInGame();

        SDL_Surface* fullSurface = SDL_RenderReadPixels(m_renderer, nullptr);

        (void)SDL_SetRenderTarget(m_renderer, prevTarget);
        SDL_DestroyTexture(captureTarget);

        if (!fullSurface) {
            return;
        }

        SDL_Surface* thumbSurface = SDL_ScaleSurface(fullSurface, 320, 180, SDL_SCALEMODE_LINEAR);
        SDL_DestroySurface(fullSurface);
        if (!thumbSurface) {
            return;
        }

        (void)m_saveManager.SaveSlotThumbnailFromSurface(slot, thumbSurface);
        SDL_DestroySurface(thumbSurface);
    }

    SDL_Window* m_window = nullptr;
    SDL_Renderer* m_renderer = nullptr;
    TTF_Font* m_font = nullptr;
    vn::AssetManager::FontHandle m_fontHandle;
    std::string m_fontPath;
    int m_fontPointSize = 22;
    int m_W = 1280;
    int m_H = 720;

    vn::EngineContext m_context;
    vn::AssetManager m_assets;
    Input m_input;
    vn::UIManager m_uiManager;
    ObserveManager m_observe;
    vn::GameState m_gameState;
    vn::LuaScriptContext m_scriptContext;
    SceneManager m_scene;
    vn::InteractionSystem m_interactionSystem;
    vn::RenderPipeline m_renderPipeline;
    ActionQueue m_actionQueue;

    vn::AudioManager m_audio;
    SaveManager m_saveManager;
    I18nManager m_i18n;
    std::string m_localeFile;

    AppState m_appState = AppState::TITLE_MENU;
    TitleMenuStateHandler m_titleMenuStateHandler;
    InGameStateHandler m_inGameStateHandler;
    SystemMenuStateHandler m_systemMenuStateHandler;
    StateManager m_stateManager;
    AppState m_returnState = AppState::TITLE_MENU;

    float m_cachePruneTimer = 0.0f;
    float m_hotReloadTimer = 0.0f;

    std::string m_hotReloadTrackedScript;
    std::filesystem::file_time_type m_lastScriptTime{};

    vn::GameSettings m_settings;

    bool m_observeStarted = false;
    std::uint64_t m_observeStartedRevision = 0;
    bool m_showDebugOverlay = false;  // F2 debug panel toggle.
    bool m_shutdown = false;

    SDL_Cursor* m_cursorArrow = nullptr;
    SDL_Cursor* m_cursorHand = nullptr;
    bool m_isHandCursorActive = false;

    // Toast notification for quick save/load feedback
    std::string m_toastMessage;
    float m_toastTimer = 0.0f;
};
