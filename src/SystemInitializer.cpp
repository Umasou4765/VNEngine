#include "SystemInitializer.h"

#include "Engine.h"
#include "EngineScriptHost.h"

bool SystemInitializer::InitAll(Engine& engine, const char* title, int w, int h) {
    engine.m_W = w;
    engine.m_H = h;

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) return false;
    if (!TTF_Init()) return false;

#if defined(IMG_INIT_PNG) && defined(IMG_INIT_JPG)
    const int imgMask = IMG_INIT_PNG | IMG_INIT_JPG;
    if ((IMG_Init(imgMask) & imgMask) == 0) {
        std::cerr << "IMG_Init failed: " << SDL_GetError() << "\n";
    }
#endif

    engine.m_window = SDL_CreateWindow(title, w, h, 0);
    if (!engine.m_window) return false;

    engine.m_renderer = SDL_CreateRenderer(engine.m_window, nullptr);
    if (!engine.m_renderer) return false;

    engine.m_cursorArrow = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_DEFAULT);
    engine.m_cursorHand = SDL_CreateSystemCursor(SDL_SYSTEM_CURSOR_POINTER);
    if (engine.m_cursorArrow) {
        SDL_SetCursor(engine.m_cursorArrow);
        engine.m_isHandCursorActive = false;
    }

    engine.RefreshEngineContext();

    engine.m_input.SetRenderer(engine.m_context.renderer);
    engine.m_input.InitializeGamepads();
    engine.m_assets.SetRenderer(engine.m_context.renderer);
    engine.PrewarmImageDecoders();
    if (!SDL_SetRenderLogicalPresentation(engine.m_context.renderer, w, h, SDL_LOGICAL_PRESENTATION_LETTERBOX)) {
        return false;
    }

    if (!engine.LoadFonts()) return false;

    engine.m_audio.SetAssetManager(engine.m_context.assets);
    engine.m_audio.Init();
    engine.m_scriptContext.SetServices(EngineScriptHost::Build(engine.m_scene,
                                                               engine.m_gameState,
                                                               engine.m_audio));

    engine.m_uiManager.Init({
        engine.m_context.renderer,
        engine.m_font,
        w,
        h,
        engine.m_context.assets,
        engine.m_context.saves,
        engine.m_fontPath,
        engine.m_fontPointSize,
        "assets/bg/classroom.png"
    });
    engine.InitializeI18n();
    engine.BindLocalizationCallbacks();

    SceneManager::InitContext::UICallbacks uiCallbacks;
    uiCallbacks.showDialogue = [&engine](const std::string& speaker,
                                         const std::string& text,
                                         const std::string& lineId,
                                         const std::string& voicePath) {
        engine.m_uiManager.ShowDialogue(speaker, text, lineId, voicePath);
    };
    uiCallbacks.showChoices = [&engine](const std::vector<std::string>& labels) {
        engine.m_uiManager.ShowChoices(labels);
    };
    uiCallbacks.hideChoices = [&engine]() {
        engine.m_uiManager.HideChoices();
    };
    uiCallbacks.clearDialogue = [&engine]() {
        engine.m_uiManager.ClearDialogue();
    };

    const SceneManager::InitContext sceneCtx{
        engine.m_context.renderer,
        w,
        h,
        &engine.m_gameState,
        &engine.m_scriptContext,
        std::move(uiCallbacks),
        engine.m_context.audio,
        engine.m_context.assets,
        engine.m_context.actions
    };
    engine.m_scene.Init(sceneCtx);
    engine.m_observe.Init(engine.m_context.renderer,
                          engine.m_font,
                          w,
                          h,
                          engine.m_context.assets,
                          engine.m_fontPath,
                          engine.m_fontPointSize);

    engine.LoadGlobalData();

    engine.ApplyConfig();

    engine.InitializeStateManager();
    engine.EnterTitleMenu();
    return true;
}
