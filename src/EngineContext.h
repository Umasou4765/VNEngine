#pragma once

#include <SDL3/SDL.h>

class ActionQueue;
class Input;
class ObserveManager;
class SaveManager;
class SceneManager;

namespace vn {

class AssetManager;
class AudioManager;
class InteractionSystem;
class RenderPipeline;
class UIManager;

struct EngineContext {
    SDL_Renderer* renderer = nullptr;
    AssetManager* assets = nullptr;
    AudioManager* audio = nullptr;
    UIManager* ui = nullptr;
    ActionQueue* actions = nullptr;
    SaveManager* saves = nullptr;
    SceneManager* scene = nullptr;
    Input* input = nullptr;
    ObserveManager* observe = nullptr;
    RenderPipeline* renderPipeline = nullptr;
    InteractionSystem* interactions = nullptr;
};

} // namespace vn
