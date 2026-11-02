#pragma once

#include <SDL3/SDL.h>

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <cmath>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

#include "ActionQueue.h"
#include "AssetManager.h"
#include "AudioManager.h"
#include "DialogueLine.h"
#include "GameState.h"
#include "IScriptContext.h"
#include "RenderPipeline.h"
#include "SceneMode.h"
#include "Profiling.h"
#include "InteractionSystem.h"
#include "ScriptDirector.h"
#include "Tween.h"
#include "VideoPlayer.h"

struct RenderStateAllocationProfile {
    std::uint64_t callCount = 0;
    std::uint64_t spriteVectorCapacityGrows = 0;
    std::size_t lastSpriteCount = 0;
    std::size_t lastSpriteCapacity = 0;
    std::size_t peakSpriteCapacity = 0;
};

struct Camera {
    float x = 0.0f;
    float y = 0.0f;
    float zoom = 1.0f;
    float targetX = 0.0f;
    float targetY = 0.0f;
    float targetZoom = 1.0f;
    
    Tween<float> tweenX, tweenY, tweenZoom;
    
    void Update(float dt) {
        tweenX.Update(dt);
        tweenY.Update(dt);
        tweenZoom.Update(dt);
    }
    
    bool IsMoving() const {
        return tweenX.IsRunning() || tweenY.IsRunning() || tweenZoom.IsRunning();
    }
    
    void Stop() {
        tweenX.Stop();
        tweenY.Stop();
        tweenZoom.Stop();
    }
};

class SceneManager {
    struct SpriteNode;

public:
    SceneManager();
    ~SceneManager();

    friend class EngineScriptHost;
    friend class SaveSystem;

    struct InitContext {
        struct UICallbacks {
            std::function<void(const std::string& speaker,
                               const std::string& text,
                               const std::string& lineId,
                               const std::string& voicePath)> showDialogue;
            std::function<void(const std::vector<std::string>& labels)> showChoices;
            std::function<void()> hideChoices;
            std::function<void()> clearDialogue;
        };

        SDL_Renderer* renderer = nullptr;
        int width = 0;
        int height = 0;
        vn::GameState* gameState = nullptr;
        vn::IScriptContext* scriptContext = nullptr;
        UICallbacks ui;
        vn::AudioManager* audio = nullptr;
        vn::AssetManager* assets = nullptr;
        ActionQueue* actionQueue = nullptr;
    };

    enum class BgTransitionEffect {
        Crossfade,
        BlockDissolve,
        WipeLeft,
        WipeRight,
        WipeUp,
        WipeDown,
        Blinds,
    };

    struct SpriteTransformState {
        bool useAbsolute = false;
        float x = 0.0f;
        float y = 0.0f;
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        float rotationDeg = 0.0f;
        float anchorX = 0.5f;
        float anchorY = 1.0f;
        float width = -1.0f;
        float height = -1.0f;
    };

    enum class RestoreMode {
        Replay,
        SnapshotOnly,
    };

    using ObserveHotspot = vn::ObserveHotspot;
    using SpriteHitBox = vn::SpriteHitBox;

    SceneMode state = SceneMode::DIALOGUE;

    // Visual effect toggles
    bool m_enableBreathingEffect = true;  // Sprite breathing/bobbing animation
    bool m_enableParallaxEffect = true;   // Mouse-following parallax on sprites and background

    [[nodiscard]] std::size_t CurrentScriptLineNo() const {
        return ParseLuaLineNumber(ScriptContextRef().GetState().currentYieldId);
    }

    bool IsCurrentLineSeen() const {
        const std::string& yieldId = ScriptContextRef().GetState().currentYieldId;
        if (yieldId.empty()) return false;
        return GameStateRef().seenLines.count(yieldId) > 0;
    }

    void MarkCurrentLineSeen() {
        const std::string& yieldId = ScriptContextRef().GetState().currentYieldId;
        if (!yieldId.empty()) {
            GameStateRef().seenLines.insert(yieldId);
        }
    }

    [[nodiscard]] vn::ScriptDirector::WaitReason GetScriptWaitReason() const noexcept {
        return ScriptContextRef().GetState().waitReason;
    }

    [[nodiscard]] const vn::ScriptDirector::ScriptState& GetScriptState() const noexcept {
        return ScriptContextRef().GetState();
    }

    [[nodiscard]] const RenderStateAllocationProfile& GetRenderStateAllocationProfile() const noexcept {
        return m_renderStateProfile;
    }

    bool ResumeLuaCoroutine();
    bool ResumeLuaCoroutine(int resumeValue);
    bool ResumeLuaCoroutine(const std::string& resumeValue);

    void Init(const InitContext& ctx) {
        m_audio = ctx.audio;
        m_assets = ctx.assets;
        m_actionQueue = ctx.actionQueue;
        m_gameState = ctx.gameState;
        m_scriptContext = ctx.scriptContext;
        m_ui = ctx.ui;
        m_W = ctx.width;
        m_H = ctx.height;
        m_videoPlayer.Init(ctx.renderer, m_W, m_H);
    }

    void Init(SDL_Renderer* renderer,
              int w,
              int h,
              vn::AudioManager* audio = nullptr,
              vn::AssetManager* assets = nullptr,
              ActionQueue* actionQueue = nullptr) {
        Init(InitContext{ renderer, w, h, m_gameState, m_scriptContext, m_ui, audio, assets, actionQueue });
    }

    void SetGameState(vn::GameState& gameState) noexcept {
        m_gameState = &gameState;
    }

    void SetScriptContext(vn::IScriptContext& scriptContext) noexcept {
        m_scriptContext = &scriptContext;
    }

    void SetAudioManager(vn::AudioManager* audio) {
        m_audio = audio;
    }

    void SetAssetManager(vn::AssetManager* assets) {
        m_assets = assets;
    }

    void SetActionQueue(ActionQueue* actionQueue) {
        m_actionQueue = actionQueue;
    }

    void SetUICallbacks(InitContext::UICallbacks ui) {
        m_ui = std::move(ui);
    }

    void ClearUICallbacks() {
        m_ui = {};
    }

    void SetTranslationResolver(std::function<std::string(const std::string&)> resolver) {
        m_translate = std::move(resolver);
    }

    void ResetScriptRuntime() {
        ResetScriptState();
    }

    void SetTransitionDurations(float bgSeconds, float spriteSeconds) noexcept {
        m_bgTransitionSeconds = std::max(0.0f, bgSeconds);
        m_spriteTransitionSeconds = std::max(0.0f, spriteSeconds);
    }

    void SetBackgroundTransitionEffect(const std::string& effect) noexcept {
        std::string normalized;
        normalized.reserve(effect.size());
        for (char c : effect) {
            const unsigned char uc = static_cast<unsigned char>(c);
            normalized.push_back(static_cast<char>(std::tolower(uc)));
        }

        if (normalized == "dissolve" || normalized == "block_dissolve") {
            m_bgTransitionEffect = BgTransitionEffect::BlockDissolve;
        } else if (normalized == "wipe_left" || normalized == "wipeleft" || normalized == "wipe") {
            m_bgTransitionEffect = BgTransitionEffect::WipeLeft;
        } else if (normalized == "wipe_right" || normalized == "wiperight") {
            m_bgTransitionEffect = BgTransitionEffect::WipeRight;
        } else if (normalized == "wipe_up" || normalized == "wipeup") {
            m_bgTransitionEffect = BgTransitionEffect::WipeUp;
        } else if (normalized == "wipe_down" || normalized == "wipedown") {
            m_bgTransitionEffect = BgTransitionEffect::WipeDown;
        } else if (normalized == "blinds") {
            m_bgTransitionEffect = BgTransitionEffect::Blinds;
        } else {
            m_bgTransitionEffect = BgTransitionEffect::Crossfade;
        }
    }

    void StartScreenShake(float intensity, float duration) noexcept {
        if (ScriptContextRef().GetState().replayActive) {
            return;
        }
        m_shakeIntensity = std::max(0.0f, intensity);
        m_shakeDuration = std::max(0.0f, duration);
        m_shakeRemaining = m_shakeDuration;
    }

    void TransitionCamera(float targetX, float targetY, float targetZoom, float durationSeconds) noexcept {
        m_camera.targetX = targetX;
        m_camera.targetY = targetY;
        m_camera.targetZoom = targetZoom;
        m_camera.tweenX.Start(&m_camera.x, m_camera.x, targetX, durationSeconds, Ease::InOutQuad);
        m_camera.tweenY.Start(&m_camera.y, m_camera.y, targetY, durationSeconds, Ease::InOutQuad);
        m_camera.tweenZoom.Start(&m_camera.zoom, m_camera.zoom, targetZoom, durationSeconds, Ease::InOutQuad);
    }

    void SetCameraImmediate(float x, float y, float zoom = 1.0f) noexcept {
        m_camera.x = m_camera.targetX = x;
        m_camera.y = m_camera.targetY = y;
        m_camera.zoom = m_camera.targetZoom = std::max(0.1f, zoom);
        m_camera.Stop();
    }

    void SetSpriteTransform(int slot,
                            float x,
                            float y,
                            float scaleX,
                            float scaleY,
                            float rotationDeg,
                            float anchorX,
                            float anchorY,
                            bool useAbsolute,
                            float width = -1.0f,
                            float height = -1.0f) noexcept {
        SpriteNode& node = GetOrCreateSpriteLayer(slot);
        auto& tx = node.transform;
        tx.useAbsolute = useAbsolute;
        tx.position = { x, y };
        tx.scale = { std::max(0.01f, scaleX), std::max(0.01f, scaleY) };
        tx.rotationDeg = rotationDeg;
        tx.anchor = { std::clamp(anchorX, 0.0f, 1.0f), std::clamp(anchorY, 0.0f, 1.0f) };
        tx.size = { width, height };
    }

    [[nodiscard]] SpriteTransformState GetSpriteTransformState(int slot) const noexcept {
        const SpriteNode* node = FindSpriteLayer(slot);
        if (!node) {
            return {};
        }
        const auto& tx = node->transform;
        return SpriteTransformState{
            tx.useAbsolute,
            tx.position.x,
            tx.position.y,
            tx.scale.x,
            tx.scale.y,
            tx.rotationDeg,
            tx.anchor.x,
            tx.anchor.y,
            tx.size.x,
            tx.size.y
        };
    }

    void SetSpriteTint(int slot, Uint8 r, Uint8 g, Uint8 b, float alpha) noexcept {
        SpriteNode& node = GetOrCreateSpriteLayer(slot);
        auto& tx = node.transform;
        tx.tintColor = { r, g, b, 255 };
        tx.opacity = std::clamp(alpha, 0.0f, 1.0f);
    }

    void GetSpriteTint(int slot, Uint8& r, Uint8& g, Uint8& b, float& alpha) const noexcept {
        const SpriteNode* node = FindSpriteLayer(slot);
        if (!node) {
            r = 255;
            g = 255;
            b = 255;
            alpha = 1.0f;
            return;
        }
        const auto& tx = node->transform;
        r = tx.tintColor.r;
        g = tx.tintColor.g;
        b = tx.tintColor.b;
        alpha = tx.opacity;
    }

    void ClearSpriteTransform(int slot) noexcept {
        if (SpriteNode* node = FindSpriteLayer(slot)) {
            node->transform = {};
        }
    }

    void SetSpriteClipRect(int slot, float x, float y, float w, float h, bool enabled) noexcept {
        SpriteNode& node = GetOrCreateSpriteLayer(slot);
        auto& tx = node.transform;
        tx.clipEnabled = enabled && w > 0.0f && h > 0.0f;
        tx.clipRect = { x, y, std::max(0.0f, w), std::max(0.0f, h) };
    }

    void ClearSpriteClipRect(int slot) noexcept {
        if (SpriteNode* node = FindSpriteLayer(slot)) {
            node->transform.clipEnabled = false;
            node->transform.clipRect = { 0.0f, 0.0f, 0.0f, 0.0f };
        }
    }

    void SetLetterbox(float ratio, float durationSeconds = 0.0f, bool immediate = false) noexcept {
        const float target = std::clamp(ratio, 0.0f, 0.45f);
        if (immediate || durationSeconds <= 0.0f) {
            m_letterboxRatio = target;
            m_letterboxTween.Stop();
            return;
        }

        m_letterboxTween.Start(&m_letterboxRatio,
                               m_letterboxRatio,
                               target,
                               durationSeconds,
                               Ease::InOutQuad);
    }

    void SetScreenFadeColor(Uint8 r, Uint8 g, Uint8 b) noexcept {
        m_screenFadeColor = { r, g, b, 255 };
    }

    void TransitionScreenFade(float targetAlpha, float durationSeconds) noexcept {
        const float clampedTarget = std::clamp(targetAlpha, 0.0f, 1.0f);
        m_screenFadeTween.Start(&m_screenFadeAlpha,
                                m_screenFadeAlpha,
                                clampedTarget,
                                durationSeconds,
                                Ease::InOutQuad);
    }

    void SetScreenFadeAlphaImmediate(float alpha) noexcept {
        m_screenFadeAlpha = std::clamp(alpha, 0.0f, 1.0f);
        m_screenFadeTween.Stop();
    }

    [[nodiscard]] float GetScreenFadeAlpha() const noexcept {
        return m_screenFadeAlpha;
    }

    void SetSpriteInteraction(int slot,
                              const std::string& characterTag,
                              const std::string& partTag,
                              const std::string& callbackName) {
        if (ScriptContextRef().GetState().replayActive) {
            return;
        }
        SpriteNode& node = GetOrCreateSpriteLayer(slot);
        node.interaction.enabled = true;
        node.interaction.characterTag = characterTag;
        node.interaction.partTag = partTag;
        node.interaction.callbackName = callbackName;
    }

    void ClearSpriteInteraction(int slot) {
        if (SpriteNode* node = FindSpriteLayer(slot)) {
            node->interaction = {};
        }
    }

    [[nodiscard]] std::vector<SpriteHitBox> GetInteractiveHitboxes() const {
        std::vector<SpriteHitBox> hitboxes;
        for (const auto& [slot, node] : m_spriteLayers) {
            if (!node.screenRectValid) continue;
            if (!node.interaction.enabled) continue;
            hitboxes.push_back({
                slot,
                node.screenRect,
                node.interaction.characterTag,
                node.interaction.partTag,
                node.interaction.callbackName
            });
        }
        return hitboxes;
    }

    [[nodiscard]] const Camera& GetCamera() const {
        return m_camera;
    }

    [[nodiscard]] SDL_FPoint GetCameraOffset() const noexcept {
        return m_cameraOffset;
    }

    [[nodiscard]] float GetLetterboxRatio() const noexcept {
        return m_letterboxRatio;
    }

    [[nodiscard]] const std::string& GetLuaErrorMessage() const noexcept {
        return m_luaErrorMessage;
    }

    [[nodiscard]] bool HasLuaError() const noexcept {
        return !m_luaErrorMessage.empty();
    }

    [[nodiscard]] bool HasRestoreWarning() const noexcept {
        return !m_restoreWarningMessage.empty();
    }

    [[nodiscard]] vn::SceneRenderState GetRenderState(float parallaxNormX, float parallaxNormY) const {
        ++m_renderStateProfile.callCount;

        const std::size_t previousCapacity = m_renderStateProfile.lastSpriteCapacity;

        vn::SceneRenderState state;
        state.bgLayer = { ActiveTexture(m_bgLayer), m_bgLayer.previous.get(), m_bgLayer.crossfadeMix };
        state.bgTransitionEffect = static_cast<int>(m_bgTransitionEffect);
        state.bgDissolveCellSizePx = m_bgDissolveCellSizePx;
        state.camera = { m_camera.x, m_camera.y, m_camera.zoom };
        state.cameraOffset = m_cameraOffset;
        state.letterboxRatio = m_letterboxRatio;
        state.screenFadeColor = m_screenFadeColor;
        state.screenFadeAlpha = m_screenFadeAlpha;
        state.parallaxNormX = parallaxNormX;
        state.parallaxNormY = parallaxNormY;
        state.enableParallaxEffect = m_enableParallaxEffect;
        state.screenW = m_W;
        state.screenH = m_H;
        state.sprites.reserve(m_spriteLayers.size());

        if (state.sprites.capacity() > previousCapacity) {
            ++m_renderStateProfile.spriteVectorCapacityGrows;
        }

        for (const auto& [slot, node] : m_spriteLayers) {
            vn::SceneSpriteRenderCommand command;
            command.slot = slot;
            command.current = node.state.current ? node.state.current.get() : nullptr;
            command.previous = node.state.previous ? node.state.previous.get() : nullptr;
            command.crossfadeMix = node.state.crossfadeMix;
            command.screenRect = node.screenRect;
            command.screenRectValid = node.screenRectValid;
            command.tintColor = node.transform.tintColor;
            command.opacity = node.transform.opacity;
            command.rotationDeg = node.transform.rotationDeg;
            command.anchor = node.transform.anchor;
            command.clipEnabled = node.transform.clipEnabled;
            command.clipRect = node.transform.clipRect;
            state.sprites.push_back(command);
        }

        m_renderStateProfile.lastSpriteCount = state.sprites.size();
        m_renderStateProfile.lastSpriteCapacity = state.sprites.capacity();
        m_renderStateProfile.peakSpriteCapacity =
            std::max(m_renderStateProfile.peakSpriteCapacity, state.sprites.capacity());

        return state;
    }

    std::string ConsumeRestoreWarning() {
        std::string warning = std::move(m_restoreWarningMessage);
        m_restoreWarningMessage.clear();
        return warning;
    }

    [[nodiscard]] bool IsUsingLua() const noexcept {
        return true;
    }

    [[nodiscard]] const std::vector<ObserveHotspot>& GetObserveHotspots() const noexcept {
        return m_observeHotspots;
    }

    [[nodiscard]] float GetObservePanScale() const noexcept {
        return m_observePanScale;
    }

    [[nodiscard]] bool ObserveAllowsCancel() const noexcept {
        return m_observeAllowCancel;
    }

    [[nodiscard]] bool VideoAllowsSkip() const noexcept {
        return state == SceneMode::VIDEO && m_videoPlayer.IsSkippable();
    }

    void SkipVideo() {
        if (!VideoAllowsSkip()) {
            return;
        }
        FinishVideoPlayback();
    }

    [[nodiscard]] std::uint64_t GetObserveRevision() const noexcept {
        return m_observeRevision;
    }

    void OnObserveTargetSelected(const std::string& targetId) {
        if (!m_pendingLuaObserveId.empty()) {
            ScriptContextRef().MutableState().observeHistory.push_back({ m_pendingLuaObserveId, targetId });
        }

        m_pendingLuaObserveId.clear();
        ScriptContextRef().MutableState().pendingObserveId.clear();
        ClearObserveBindings();
        state = SceneMode::DIALOGUE;
        ScriptContextRef().MutableState().waitReason = vn::ScriptDirector::WaitReason::None;
        (void)ResumeLuaCoroutine(targetId);
    }

    void OnSpriteInteraction(const std::string& callbackName,
                             const std::string& characterTag,
                             const std::string& partTag,
                             const std::string& eventName) {
        if (ScriptContextRef().GetState().replayActive) return;
        (void)ScriptContextRef().TriggerInteractionCallback(callbackName, characterTag, partTag, eventName);
    }

    bool LoadLuaScript(const std::string& scriptPath,
                       const std::string& entryPoint = "main",
                       bool autoStart = true) {
#if defined(VN_ENGINE_HAS_LUA) && VN_ENGINE_HAS_LUA
        if (scriptPath.empty()) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Lua load failed: empty script path");
            return false;
        }

        const std::filesystem::path requestedPath(scriptPath);
        if (requestedPath.extension() != ".lua") {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Lua load failed for %s: only .lua scripts are supported",
                         scriptPath.c_str());
            return false;
        }

        if (!std::filesystem::exists(requestedPath)) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Lua load failed for %s: file not found",
                         scriptPath.c_str());
            return false;
        }

        ResetScriptState();

        m_scriptPath = scriptPath;
        m_luaEntryPoint = entryPoint.empty() ? "main" : entryPoint;

        if (!ScriptContextRef().LoadScriptFromFile(m_scriptPath, m_luaEntryPoint, autoStart)) {
            m_ended = true;
            return false;
        }
        return true;
#else
        (void)scriptPath;
        (void)entryPoint;
        (void)autoStart;
        return false;
#endif
    }

    // [MOVE TO SaveSystem] Snapshot serialization boundary.
    [[nodiscard]] vn::SaveSnapshot BuildSaveData() const;

    void ForceClearUI() {
        HideChoicesUI();
        ClearDialogueUI();
        ClearObserveBindings();
        state = SceneMode::DIALOGUE;
    }

    [[nodiscard]] bool CanRollback() const noexcept {
        return !GameStateRef().rollbackHistory.empty();
    }

    void PushHistory() {
        if (m_scriptPath.empty()) {
            return;
        }

        auto& history = GameStateRef().rollbackHistory;
        history.push_back(BuildSaveData());
        if (history.size() > kMaxHistorySteps) {
            history.pop_front();
        }
    }

    bool Rollback() {
        auto& history = GameStateRef().rollbackHistory;
        if (history.empty()) {
            return false;
        }

        vn::SaveSnapshot snapshot = std::move(history.back());
        history.pop_back();

        if (m_actionQueue) m_actionQueue->FlushAll();
        m_camera.Stop();
        m_shakeRemaining = 0.0f;

        RestoreFromSave(snapshot);
        return true;
    }

    void ClearHistory() {
        GameStateRef().rollbackHistory.clear();
    }

    // [MOVE TO SaveSystem] Snapshot restore boundary.
    void RestoreFromSave(const vn::SaveSnapshot& data, RestoreMode mode = RestoreMode::Replay);

    void ReleaseAssets() {
        if (m_actionQueue) m_actionQueue->FlushAll();
        ResetVisualState();
        if (m_assets) m_assets->ClearUnused();
    }

    void Update(float dt, bool fastForwardActions = false) {
        ZoneScoped;
        if (state == SceneMode::VIDEO) {
            m_videoPlayer.Update(dt);
            if (m_videoPlayer.IsFinished()) {
                FinishVideoPlayback();
            }
            return;
        }

        UpdateLayerTransition(m_bgLayer, dt);
        for (auto& [slot, node] : m_spriteLayers) {
            UpdateLayerTransition(node.state, dt);
        }
        UpdateScreenShake(dt);
        m_camera.Update(dt);
        m_letterboxTween.Update(dt);
        m_screenFadeTween.Update(dt);

        if (m_actionQueue) {
            m_actionQueue->Update(dt, fastForwardActions);
            if (!m_actionQueue->IsBlocked() &&
                ScriptContextRef().GetState().waitReason == vn::ScriptDirector::WaitReason::ActionQueue) {
                ScriptContextRef().MutableState().waitReason = vn::ScriptDirector::WaitReason::None;
                (void)ResumeLuaCoroutine();
            }
        }
    }

    void UpdateSpriteScreenRects(float parallaxNormX, float parallaxNormY) {
        const SDL_FPoint shake = m_cameraOffset;
        const float zoom = m_camera.zoom;

        float timeSec = SDL_GetTicks() / 1000.0f;

        // Traverse all sprite layers by map ID
        for (auto& [slot, node] : m_spriteLayers) {
            node.screenRectValid = false;
            // Skip empty sprite slots.
            if (!node.state.current && !node.state.previous) continue;

            const SpriteTransform& transform = node.transform;
            SDL_FRect sprRect = SpriteRect(slot);
            sprRect = ApplyTransformBaseRect(sprRect, transform);
            
            // Breathing animation.
            float phase = slot * 1.5f;
            float breathe = (m_enableBreathingEffect && transform.breathingEnabled)
                ? 1.0f + std::sin(timeSec * 2.5f + phase) * 0.015f 
                : 1.0f;

            sprRect = ScaleRectAroundAnchor(sprRect, 1.0f, breathe, transform.anchor);
            sprRect = ScaleRectAroundAnchor(sprRect, transform.scale.x, transform.scale.y, transform.anchor);
            
            // Camera transform (zoom and pan).
            sprRect.w *= zoom;
            sprRect.h *= zoom;
            sprRect.x = (m_W / 2.0f) + (sprRect.x - m_W / 2.0f) * zoom - m_camera.x * zoom;
            sprRect.y = (m_H / 2.0f) + (sprRect.y - m_H / 2.0f) * zoom - m_camera.y * zoom;

            // Apply parallax.
            if (m_enableParallaxEffect) {
                sprRect.x += parallaxNormX * 15.0f + shake.x;
                sprRect.y += parallaxNormY * 5.0f + shake.y;
            } else {
                sprRect.x += shake.x;
                sprRect.y += shake.y;
            }

            node.screenRect = sprRect;
            node.screenRectValid = true;
        }
    }

    bool AdvanceDialogue() {
        if (m_ended) return false;

        if (state == SceneMode::DIALOGUE) {
            MarkCurrentLineSeen();
        }

        if (state == SceneMode::DIALOGUE &&
            ScriptContextRef().GetState().waitReason == vn::ScriptDirector::WaitReason::Dialogue) {
            PushHistory();
        }

        if (m_audio) {
            // Allow voice bleed when advancing dialogue
            m_audio->OnDialogueAdvance(true);
        }
        if (ScriptContextRef().GetState().waitReason == vn::ScriptDirector::WaitReason::Dialogue) {
            ScriptContextRef().MutableState().waitReason = vn::ScriptDirector::WaitReason::None;
            return ResumeLuaCoroutine();
        }
        return !m_ended;
    }

    void OnChoiceSelected(int value) {
        PushHistory();
        HideChoicesUI();
        state = SceneMode::DIALOGUE;
        if (!ScriptContextRef().GetState().pendingChoiceId.empty()) {
            ScriptContextRef().MutableState().choiceHistory.push_back({ ScriptContextRef().GetState().pendingChoiceId, value });
        }
        ScriptContextRef().MutableState().pendingChoiceId.clear();
        ScriptContextRef().MutableState().waitReason = vn::ScriptDirector::WaitReason::None;
        (void)ResumeLuaCoroutine(value);
    }

    [[nodiscard]] SDL_Texture* GetBackgroundTexture() const {
        if (!m_bgLayer.current) return nullptr;
        return m_bgLayer.current.get();
    }

    void LoadBG(const std::string& path, bool immediate = false) {
        m_bgPath = path;

        vn::AssetManager::TextureHandle texture;
        if (!path.empty() && m_assets) {
            texture = m_assets->GetTexture(path);
        }

        BeginCrossfade(m_bgLayer, std::move(texture), m_bgTransitionSeconds, immediate);
    }

    void LoadSpriteBySlot(const std::string& path, int slot, bool immediate = false) {
        const bool clearSprite = path.empty() || path == "none" || path == "NONE";
        vn::AssetManager::TextureHandle texture;

        SpriteNode& node = GetOrCreateSpriteLayer(slot);

        if (!clearSprite && m_assets) {
            texture = m_assets->GetTexture(path);
            if (texture) {
                node.path = path;
            } else {
                node.path.clear();
            }
        } else {
            node.path.clear();
        }

        BeginCrossfade(node.state, std::move(texture), m_spriteTransitionSeconds, immediate);
    }

    [[nodiscard]] std::string Translate(const std::string& key) const {
        if (m_translate) {
            return m_translate(key);
        }
        return std::string("[") + key + "]";
    }

    [[nodiscard]] bool IsReplayActive() const {
        return ScriptContextRef().GetState().replayActive;
    }

    bool PreloadImage(const std::string& path) {
        if (!m_assets) {
            return false;
        }
        (void)m_assets->PreloadAsync(path);
        return true;
    }

    [[nodiscard]] bool QueueParallelActions(const std::vector<vn::ParallelActionSpec>& actions) {
        if (ScriptContextRef().GetState().replayActive) {
            if (!ConsumeLuaReplayStepBudget("parallel", "")) {
                return true;
            }
            return false;
        }
        if (!m_actionQueue) {
            return false;
        }

        std::vector<std::unique_ptr<ActionCommand>> children;
        children.reserve(actions.size());

        for (const auto& action : actions) {
            const auto getNum = [&](const char* key, double fallback) {
                auto it = action.numericArgs.find(key);
                return (it != action.numericArgs.end()) ? it->second : fallback;
            };
            const auto getBool = [&](const char* key, bool fallback) {
                auto it = action.boolArgs.find(key);
                return (it != action.boolArgs.end()) ? it->second : fallback;
            };

            if (action.kind == "wait") {
                const float duration = static_cast<float>(getNum("duration", 0.0));
                if (duration > 0.0f) {
                    children.push_back(std::make_unique<ExplicitWaitAction>(duration));
                }
            } else if (action.kind == "shake") {
                const float intensity = static_cast<float>(getNum("intensity", 8.0));
                const float duration = static_cast<float>(getNum("duration", 0.18));
                children.push_back(std::make_unique<ScreenShakeAction>(this, intensity, duration));
            } else if (action.kind == "camera") {
                const float x = static_cast<float>(getNum("x", m_camera.targetX));
                const float y = static_cast<float>(getNum("y", m_camera.targetY));
                const float zoom = static_cast<float>(getNum("zoom", m_camera.targetZoom));
                const float duration = static_cast<float>(getNum("duration", 0.0));
                children.push_back(std::make_unique<CameraTweenAction>(this, x, y, zoom, duration));
            } else if (action.kind == "letterbox") {
                const float ratio = static_cast<float>(getNum("ratio", 0.0));
                const float duration = static_cast<float>(getNum("duration", 0.0));
                children.push_back(std::make_unique<LetterboxTweenAction>(this, ratio, duration));
            } else if (action.kind == "sprite_transform") {
                const int slot = static_cast<int>(getNum("slot", 1.0));
                const float x = static_cast<float>(getNum("x", 0.0));
                const float y = static_cast<float>(getNum("y", 0.0));
                const float scaleX = static_cast<float>(getNum("scale_x", 1.0));
                const float scaleY = static_cast<float>(getNum("scale_y", 1.0));
                const float rotation = static_cast<float>(getNum("rotation", 0.0));
                const float anchorX = static_cast<float>(getNum("anchor_x", 0.5));
                const float anchorY = static_cast<float>(getNum("anchor_y", 1.0));
                const bool absolute = getBool("absolute", false);
                const float width = static_cast<float>(getNum("width", -1.0));
                const float height = static_cast<float>(getNum("height", -1.0));
                SetSpriteTransform(slot, x, y, scaleX, scaleY, rotation, anchorX, anchorY, absolute, width, height);
            } else if (action.kind == "sprite_tween") {
                const int slot = static_cast<int>(getNum("slot", 1.0));
                const float duration = static_cast<float>(getNum("duration", 0.0));

                auto toOptional = [&](const char* key) -> std::optional<float> {
                    auto it = action.numericArgs.find(key);
                    if (it == action.numericArgs.end()) {
                        return std::nullopt;
                    }
                    return static_cast<float>(it->second);
                };

                children.push_back(std::make_unique<SpriteTweenAction>(this,
                                                                       slot,
                                                                       duration,
                                                                       toOptional("x"),
                                                                       toOptional("y"),
                                                                       toOptional("scale_x"),
                                                                       toOptional("scale_y"),
                                                                       toOptional("rotation")));
            } else if (action.kind == "screen_fade") {
                const int r = static_cast<int>(getNum("r", 0.0));
                const int g = static_cast<int>(getNum("g", 0.0));
                const int b = static_cast<int>(getNum("b", 0.0));
                const float alpha = static_cast<float>(getNum("alpha", 1.0));
                const float duration = static_cast<float>(getNum("duration", 0.0));
                children.push_back(std::make_unique<ScreenFadeAction>(this,
                                                                       static_cast<Uint8>(std::clamp(r, 0, 255)),
                                                                       static_cast<Uint8>(std::clamp(g, 0, 255)),
                                                                       static_cast<Uint8>(std::clamp(b, 0, 255)),
                                                                       alpha,
                                                                       duration));
            } else if (action.kind == "sprite_tint") {
                const int slot = static_cast<int>(getNum("slot", 1.0));
                const float duration = static_cast<float>(getNum("duration", 0.0));

                auto toOptional = [&](const char* key) -> std::optional<float> {
                    auto it = action.numericArgs.find(key);
                    if (it == action.numericArgs.end()) {
                        return std::nullopt;
                    }
                    return static_cast<float>(it->second);
                };

                children.push_back(std::make_unique<SpriteTintAction>(this,
                                                                      slot,
                                                                      duration,
                                                                      toOptional("r"),
                                                                      toOptional("g"),
                                                                      toOptional("b"),
                                                                      toOptional("alpha")));
            }
        }

        if (children.empty()) {
            return false;
        }

        m_actionQueue->Enqueue(std::make_unique<ActionGroupCommand>(std::move(children)));
        ScriptContextRef().MutableState().waitReason = vn::ScriptDirector::WaitReason::ActionQueue;
        return true;
    }

private:
    struct SpriteTransform {
        bool useAbsolute = false;
        SDL_FPoint position{ 0.0f, 0.0f };
        SDL_FPoint scale{ 1.0f, 1.0f };
        float rotationDeg = 0.0f;
        SDL_FPoint anchor{ 0.5f, 1.0f };
        SDL_FPoint size{ -1.0f, -1.0f };
        bool clipEnabled = false;
        SDL_FRect clipRect{ 0.0f, 0.0f, 0.0f, 0.0f };
        bool breathingEnabled = true;
        SDL_Color tintColor{ 255, 255, 255, 255 };
        float opacity = 1.0f;
    };

    struct SpriteInteractionBinding {
        bool enabled = false;
        std::string characterTag;
        std::string partTag;
        std::string callbackName;
    };

    [[nodiscard]] static std::size_t ParseLuaLineNumber(const std::string& id) {
        const std::size_t colon = id.find_last_of(':');
        if (colon == std::string::npos || colon + 1 >= id.size()) {
            return 0;
        }

        std::size_t value = 0;
        for (std::size_t i = colon + 1; i < id.size(); ++i) {
            const unsigned char ch = static_cast<unsigned char>(id[i]);
            if (!std::isdigit(ch)) {
                return 0;
            }
            value = (value * 10) + static_cast<std::size_t>(ch - '0');
        }
        return value;
    }

    void ResetScriptState();

    void ApplySaveSnapshot(const vn::SaveSnapshot& data) {
        GameStateRef().flags = data.flags;
        GameStateRef().numbers = data.numVars;
        GameStateRef().currentBgmPath = data.currentBgmPath;
        GameStateRef().chapterName = data.chapterName.empty() ? "Prologue" : data.chapterName;
        m_bgPath = data.bgPath;

        if (m_actionQueue) m_actionQueue->FlushAll();

        LoadBG(m_bgPath, true);

        m_spriteLayers.clear();
        for (const auto& spr : data.spriteStates) {
            LoadSpriteBySlot(spr.path, spr.slot, true);
            SetSpriteTransform(spr.slot,
                               spr.x,
                               spr.y,
                               spr.scaleX,
                               spr.scaleY,
                               spr.rotation,
                               spr.anchorX,
                               spr.anchorY,
                               spr.useAbsolute);
            SetSpriteTint(spr.slot, spr.r, spr.g, spr.b, spr.alpha);
        }

        m_camera.x = data.cameraX;
        m_camera.y = data.cameraY;
        m_camera.zoom = std::max(0.1f, data.cameraZoom);
        m_camera.targetX = m_camera.x;
        m_camera.targetY = m_camera.y;
        m_camera.targetZoom = m_camera.zoom;
        m_camera.Stop();

        if (m_audio) {
            m_audio->StopVoice(0);
            m_audio->StopAllSFX();
            if (!GameStateRef().currentBgmPath.empty()) {
                m_audio->PlayBGM(GameStateRef().currentBgmPath, -1, 200);
            } else {
                m_audio->StopBGM(200);
            }
        }
    }

    bool QueueLuaDialogue(const std::string& speaker,
                          const std::string& text,
                          const std::string& voicePath,
                          const std::string& lineId) {
        ScriptContextRef().RecordSuspension(vn::ScriptDirector::WaitReason::Dialogue, lineId);
        auto& scriptState = ScriptContextRef().MutableState();

        if (scriptState.replayActive) {
            if (ConsumeLuaReplayStepBudget("dialogue", lineId)) {
                if (ScriptContextRef().CurrentSuspensionMatchesReplayTarget()) {
                    ScriptContextRef().CompleteReplayAtCurrentSuspension();
                } else {
                    return false;
                }
            } else {
                return true;
            }
        }

        state = SceneMode::DIALOGUE;
        scriptState.waitReason = vn::ScriptDirector::WaitReason::Dialogue;

        ShowDialogueUI(speaker, text, lineId, voicePath);

        if (m_audio) {
            if (!voicePath.empty() || !m_audio->GetVoiceBleedEnabled()) {
                m_audio->StopVoice(0);
            }
            if (!voicePath.empty()) {
                m_audio->PlayVoice(voicePath);
            }
        }

        return true;
    }

    std::tuple<int, bool> QueueLuaChoice(const std::vector<std::string>& labels,
                                         const std::string& choiceId) {
        ScriptContextRef().RecordSuspension(vn::ScriptDirector::WaitReason::Choice, choiceId);
        auto& scriptState = ScriptContextRef().MutableState();

        if (scriptState.replayActive) {
            if (ConsumeLuaReplayStepBudget("choice", choiceId)) {
                if (ScriptContextRef().CurrentSuspensionMatchesReplayTarget()) {
                    ScriptContextRef().CompleteReplayAtCurrentSuspension();
                } else if (scriptState.replayChoiceCursor < scriptState.choiceHistory.size() &&
                           scriptState.choiceHistory[scriptState.replayChoiceCursor].id == choiceId) {
                    const int selected = scriptState.choiceHistory[scriptState.replayChoiceCursor].selectedIndex;
                    ++scriptState.replayChoiceCursor;
                    return { selected, false };
                } else if (!ScriptContextRef().HasStructuredReplayTarget() &&
                           !choiceId.empty() && choiceId == scriptState.replayTargetId) {
                    ScriptContextRef().CompleteReplayAtCurrentSuspension();
                } else {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "Missing replay choice for Lua choice id '%s'; defaulting to first option.",
                                choiceId.c_str());
                    scriptState.replayDiverged = true;
                    scriptState.replayActive = false;
                    return { 1, true };
                }
            } else {
                return { 1, true };
            }
        }

        state = SceneMode::CHOICE;
        scriptState.waitReason = vn::ScriptDirector::WaitReason::Choice;
        scriptState.pendingChoiceId = choiceId;

        ShowChoicesUI(labels);

        return { 0, true };
    }

    std::tuple<std::string, bool> QueueLuaObserve(std::vector<ObserveHotspot> hotspots,
                                                  float panScale,
                                                  bool allowCancel,
                                                  const std::string& observeId) {
        ScriptContextRef().RecordSuspension(vn::ScriptDirector::WaitReason::Observe, observeId);
        auto& scriptState = ScriptContextRef().MutableState();

        auto defaultTarget = [&]() -> std::string {
            if (!hotspots.empty()) {
                return hotspots.front().targetId;
            }
            if (allowCancel) {
                return "CANCEL";
            }
            return {};
        };

        if (scriptState.replayActive) {
            if (ConsumeLuaReplayStepBudget("observe", observeId)) {
                if (ScriptContextRef().CurrentSuspensionMatchesReplayTarget()) {
                    ScriptContextRef().CompleteReplayAtCurrentSuspension();
                } else if (scriptState.replayObserveCursor < scriptState.observeHistory.size() &&
                           scriptState.observeHistory[scriptState.replayObserveCursor].id == observeId) {
                    const std::string selected = scriptState.observeHistory[scriptState.replayObserveCursor].selectedTarget;
                    ++scriptState.replayObserveCursor;
                    return { selected, false };
                } else if (!ScriptContextRef().HasStructuredReplayTarget() &&
                           !observeId.empty() && observeId == scriptState.replayTargetId) {
                    ScriptContextRef().CompleteReplayAtCurrentSuspension();
                } else {
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                                "Missing replay observe id '%s'; defaulting to first target.",
                                observeId.c_str());
                    scriptState.replayDiverged = true;
                    scriptState.replayActive = false;
                    return { defaultTarget(), true };
                }
            } else {
                return { defaultTarget(), true };
            }
        }

        const bool hasInteractiveTarget = !hotspots.empty();

        if (!hasInteractiveTarget && !allowCancel) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Observe id '%s' has no hotspots and cancel is disabled.",
                        observeId.c_str());
            return { {}, false };
        }

        ClearObserveBindings();
        m_observeHotspots = std::move(hotspots);
        m_observePanScale = std::max(1.0f, panScale);
        m_observeAllowCancel = allowCancel;
        m_pendingLuaObserveId = observeId;
        ++m_observeRevision;
        state = SceneMode::OBSERVE;
        scriptState.pendingObserveId = observeId;
        scriptState.waitReason = vn::ScriptDirector::WaitReason::Observe;
        return { {}, true };
    }

    bool QueueLuaVideo(const std::string& path,
                       const std::string& videoId,
                       bool skippable,
                       bool audioEnabled) {
        ScriptContextRef().RecordSuspension(vn::ScriptDirector::WaitReason::Video, videoId);
        auto& scriptState = ScriptContextRef().MutableState();

        if (scriptState.replayActive) {
            if (!ConsumeLuaReplayStepBudget("video", videoId)) {
                return true;
            }
            if (ScriptContextRef().CurrentSuspensionMatchesReplayTarget()) {
                ScriptContextRef().CompleteReplayAtCurrentSuspension();
            } else {
                return false;
            }
        }

        vn::VideoPlayer::Options options;
        options.skippable = skippable;
        options.audioEnabled = audioEnabled;

        if (!m_videoPlayer.Play(path, options)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Failed to start video '%s': %s",
                        path.c_str(),
                        m_videoPlayer.LastError().c_str());
            state = SceneMode::DIALOGUE;
            scriptState.waitReason = vn::ScriptDirector::WaitReason::None;
            return false;
        }

        state = SceneMode::VIDEO;
        m_pendingLuaVideoId = videoId;
        scriptState.waitReason = vn::ScriptDirector::WaitReason::Video;
        return true;
    }

    bool QueueLuaWait(float seconds) {
        if (seconds <= 0.0f) {
            return false;
        }
        ScriptContextRef().RecordSuspension(vn::ScriptDirector::WaitReason::ActionQueue, "wait");
        if (ScriptContextRef().GetState().replayActive) {
            if (!ConsumeLuaReplayStepBudget("wait", "")) {
                return true;
            }
            if (!ScriptContextRef().CurrentSuspensionMatchesReplayTarget()) {
                return false;
            }
            ScriptContextRef().CompleteReplayAtCurrentSuspension();
        }
        if (!m_actionQueue) {
            return false;
        }

        m_actionQueue->Enqueue(std::make_unique<ExplicitWaitAction>(seconds));
        ScriptContextRef().MutableState().waitReason = vn::ScriptDirector::WaitReason::ActionQueue;
        return true;
    }

    bool QueueLuaShake(float intensity, float duration) {
        ScriptContextRef().RecordSuspension(vn::ScriptDirector::WaitReason::ActionQueue, "shake");
        if (ScriptContextRef().GetState().replayActive) {
            if (!ConsumeLuaReplayStepBudget("shake", "")) {
                return true;
            }
            if (!ScriptContextRef().CurrentSuspensionMatchesReplayTarget()) {
                return false;
            }
            ScriptContextRef().CompleteReplayAtCurrentSuspension();
        }
        if (!m_actionQueue) {
            StartScreenShake(intensity, duration);
            return false;
        }

        m_actionQueue->Enqueue(std::make_unique<ScreenShakeAction>(this, intensity, duration));
        ScriptContextRef().MutableState().waitReason = vn::ScriptDirector::WaitReason::ActionQueue;
        return true;
    }

    void OnScriptRuntimeError(const std::string& message);
    void OnScriptCoroutineEnded();

    struct LayerState {
        vn::AssetManager::TextureHandle current;
        vn::AssetManager::TextureHandle previous;
        float crossfadeMix = 1.0f;
        Tween<float> crossfadeTween;
    };

    [[nodiscard]] static SDL_Texture* ActiveTexture(const LayerState& layer) noexcept {
        if (layer.current) return layer.current.get();
        if (layer.previous) return layer.previous.get();
        return nullptr;
    }

    [[nodiscard]] static SDL_FRect AspectFitRect(float srcW, float srcH, float boxX, float boxY, float boxW, float boxH) noexcept {
        if (srcW <= 0.0f || srcH <= 0.0f || boxW <= 0.0f || boxH <= 0.0f) {
            return { boxX, boxY, boxW, boxH };
        }

        const float scale = std::min(boxW / srcW, boxH / srcH);
        const float w = srcW * scale;
        const float h = srcH * scale;
        const float x = boxX + (boxW - w) * 0.5f;
        const float y = boxY + (boxH - h) * 0.5f;
        return { x, y, w, h };
    }

    [[nodiscard]] static SDL_FRect AspectFillRect(float srcW, float srcH, float boxX, float boxY, float boxW, float boxH) noexcept {
        if (srcW <= 0.0f || srcH <= 0.0f || boxW <= 0.0f || boxH <= 0.0f) {
            return { boxX, boxY, boxW, boxH };
        }

        const float scale = std::max(boxW / srcW, boxH / srcH);
        const float w = srcW * scale;
        const float h = srcH * scale;
        const float x = boxX + (boxW - w) * 0.5f;
        const float y = boxY + (boxH - h) * 0.5f;
        return { x, y, w, h };
    }

    [[nodiscard]] static SDL_FRect OffsetRect(SDL_FRect rect, const SDL_FPoint& offset) noexcept {
        rect.x += offset.x;
        rect.y += offset.y;
        return rect;
    }

    void ResetVisualState() {
        m_videoPlayer.Stop();
        m_bgPath.clear();
        m_bgLayer = {};
        m_spriteLayers.clear();
        m_cameraOffset = { 0.0f, 0.0f };
        m_camera = {};
        m_shakeIntensity = 0.0f;
        m_shakeDuration = 0.0f;
        m_shakeRemaining = 0.0f;
        m_letterboxRatio = 0.0f;
        m_letterboxTween.Stop();
        m_screenFadeColor = { 0, 0, 0, 0 };
        m_screenFadeAlpha = 0.0f;
        m_screenFadeTween.Stop();
        ClearObserveBindings();
    }

    void ClearObserveBindings() {
        m_pendingLuaObserveId.clear();
        m_observeHotspots.clear();
        m_observePanScale = 1.0f;
        m_observeAllowCancel = false;
    }

    void FinishVideoPlayback() {
        const bool shouldResume =
            ScriptContextRef().GetState().waitReason == vn::ScriptDirector::WaitReason::Video;

        m_videoPlayer.Stop();
        m_pendingLuaVideoId.clear();
        state = SceneMode::DIALOGUE;

        if (shouldResume) {
            ScriptContextRef().MutableState().waitReason = vn::ScriptDirector::WaitReason::None;
            (void)ResumeLuaCoroutine();
        }
    }

    [[nodiscard]] bool ConsumeLuaReplayStepBudget(const char* stage,
                                                  const std::string& resumeId) {
        auto& scriptState = ScriptContextRef().MutableState();
        if (!scriptState.replayActive) {
            return false;
        }

        ++scriptState.replayStepCounter;
        if (scriptState.replayStepCounter <= kLuaReplayMaxContinuousSteps) {
            return true;
        }

        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Lua replay exceeded max continuous steps (%zu) at stage '%s' (target '%s', current '%s'). Marking replay diverged.",
                    kLuaReplayMaxContinuousSteps,
                    stage ? stage : "unknown",
                    scriptState.replayTargetId.c_str(),
                    resumeId.c_str());

        scriptState.replayDiverged = true;
        scriptState.replayActive = false;
        return false;
    }

    void UpdateLayerTransition(LayerState& layer, float dt) {
        layer.crossfadeTween.Update(dt);
        if (!layer.crossfadeTween.IsRunning() && layer.crossfadeMix >= 1.0f) {
            layer.previous.reset();
            layer.crossfadeMix = 1.0f;
        }
    }

    void UpdateScreenShake(float dt) {
        if (m_shakeRemaining <= 0.0f) {
            m_cameraOffset = { 0.0f, 0.0f };
            return;
        }

        m_shakeRemaining = std::max(0.0f, m_shakeRemaining - dt);
        const float normalized = (m_shakeDuration > 0.0f) ? (m_shakeRemaining / m_shakeDuration) : 0.0f;
        const float amplitude = m_shakeIntensity * normalized;
        std::uniform_real_distribution<float> dist(-amplitude, amplitude);
        m_cameraOffset = { dist(m_rng), dist(m_rng) };

        if (m_shakeRemaining <= 0.0f) {
            m_cameraOffset = { 0.0f, 0.0f };
        }
    }

    [[nodiscard]] static SDL_Rect ClipToIntRect(const SDL_FRect& rect) {
        SDL_Rect out{};
        out.x = static_cast<int>(std::lround(rect.x));
        out.y = static_cast<int>(std::lround(rect.y));
        out.w = static_cast<int>(std::lround(rect.w));
        out.h = static_cast<int>(std::lround(rect.h));
        return out;
    }

    [[nodiscard]] SDL_Rect TransformClipRectToScreen(const SDL_FRect& clipRect,
                                                     float parallaxNormX,
                                                     float parallaxNormY,
                                                     const SDL_FPoint& shake,
                                                     float zoom) const {
        SDL_FRect transformed = clipRect;

        transformed.w *= zoom;
        transformed.h *= zoom;
        transformed.x = (m_W / 2.0f) + (transformed.x - m_W / 2.0f) * zoom - m_camera.x * zoom;
        transformed.y = (m_H / 2.0f) + (transformed.y - m_H / 2.0f) * zoom - m_camera.y * zoom;

        if (m_enableParallaxEffect) {
            transformed.x += parallaxNormX * 15.0f + shake.x;
            transformed.y += parallaxNormY * 5.0f + shake.y;
        } else {
            transformed.x += shake.x;
            transformed.y += shake.y;
        }

        return ClipToIntRect(transformed);
    }

    [[nodiscard]] static SDL_FRect ScaleRectAroundAnchor(SDL_FRect rect, float scaleX, float scaleY, const SDL_FPoint& anchor) {
        const float ax = rect.x + rect.w * anchor.x;
        const float ay = rect.y + rect.h * anchor.y;
        rect.w *= scaleX;
        rect.h *= scaleY;
        rect.x = ax - rect.w * anchor.x;
        rect.y = ay - rect.h * anchor.y;
        return rect;
    }

    [[nodiscard]] SDL_FRect ApplyTransformBaseRect(SDL_FRect baseRect, const SpriteTransform& transform) const {
        if (transform.useAbsolute) {
            const float absW = transform.size.x > 0.0f ? transform.size.x : baseRect.w;
            const float absH = transform.size.y > 0.0f ? transform.size.y : baseRect.h;
            baseRect.w = absW;
            baseRect.h = absH;
            baseRect.x = transform.position.x - absW * transform.anchor.x;
            baseRect.y = transform.position.y - absH * transform.anchor.y;
        }
        return baseRect;
    }

    void BeginCrossfade(LayerState& layer,
                        vn::AssetManager::TextureHandle nextTexture,
                        float durationSeconds,
                        bool immediate) {
        if (layer.current == nextTexture && !layer.previous) {
            layer.crossfadeMix = 1.0f;
            return;
        }

        if (immediate || durationSeconds <= 0.0f) {
            layer.previous.reset();
            layer.current = std::move(nextTexture);
            layer.crossfadeMix = 1.0f;
            layer.crossfadeTween.Stop();
            return;
        }

        layer.previous = layer.current;
        layer.current = std::move(nextTexture);

        if (!layer.previous && !layer.current) {
            layer.crossfadeMix = 1.0f;
            layer.crossfadeTween.Stop();
            return;
        }

        layer.crossfadeTween.Start(
            &layer.crossfadeMix,
            0.0f,
            1.0f,
            durationSeconds,
            Ease::InOutQuad,
            [&layer]() {
                layer.previous.reset();
                layer.crossfadeMix = 1.0f;
            }
        );
    }

    [[nodiscard]] SDL_FRect SpriteRect(int slot) const {
        const float boxH = m_H * 0.9f;
        const float boxY = m_H - boxH;
        const float defaultW = m_W * 0.35f;

        float sprW = defaultW;
        float sprH = boxH;

        if (const SpriteNode* node = FindSpriteLayer(slot)) {
            if (SDL_Texture* tex = ActiveTexture(node->state)) {
                float tw = 0.0f;
                float th = 0.0f;
                SDL_GetTextureSize(tex, &tw, &th);
                SDL_FRect fit = AspectFitRect(tw, th, 0.0f, boxY, defaultW, boxH);
                sprW = fit.w;
                sprH = fit.h;
            }
        }

        float x = (m_W - sprW) * 0.5f;
        if (slot == 0) x = 0.0f;
        else if (slot == 2) x = m_W - sprW;
        
        const float y = m_H - sprH;
        return { x, y, sprW, sprH };
    }

    void ShowDialogueUI(const std::string& speaker,
                        const std::string& text,
                        const std::string& lineId,
                        const std::string& voicePath) {
        if (m_ui.showDialogue) {
            m_ui.showDialogue(speaker, text, lineId, voicePath);
        }
    }

    void ShowChoicesUI(const std::vector<std::string>& labels) {
        if (m_ui.showChoices) {
            m_ui.showChoices(labels);
        }
    }

    void HideChoicesUI() {
        if (m_ui.hideChoices) {
            m_ui.hideChoices();
        }
    }

    void ClearDialogueUI() {
        if (m_ui.clearDialogue) {
            m_ui.clearDialogue();
        }
    }

    [[nodiscard]] vn::GameState& GameStateRef() noexcept {
        assert(m_gameState && "SceneManager requires a vn::GameState before use");
        return *m_gameState;
    }

    [[nodiscard]] const vn::GameState& GameStateRef() const noexcept {
        assert(m_gameState && "SceneManager requires a vn::GameState before use");
        return *m_gameState;
    }

    [[nodiscard]] vn::IScriptContext& ScriptContextRef() noexcept {
        assert(m_scriptContext && "SceneManager requires a vn::IScriptContext before use");
        return *m_scriptContext;
    }

    [[nodiscard]] const vn::IScriptContext& ScriptContextRef() const noexcept {
        assert(m_scriptContext && "SceneManager requires a vn::IScriptContext before use");
        return *m_scriptContext;
    }

    vn::GameState* m_gameState = nullptr;
    vn::IScriptContext* m_scriptContext = nullptr;
    InitContext::UICallbacks m_ui;
    vn::AudioManager* m_audio = nullptr;
    vn::AssetManager* m_assets = nullptr;
    ActionQueue* m_actionQueue = nullptr;
    mutable RenderStateAllocationProfile m_renderStateProfile;
    int m_W = 1280;
    int m_H = 720;

    bool m_ended = false;
    std::string m_luaErrorMessage;  // Captures Lua runtime errors for UI display
    std::string m_restoreWarningMessage;
    std::string m_scriptPath;

    std::function<std::string(const std::string&)> m_translate;
    std::string m_luaEntryPoint = "main";
    static constexpr std::size_t kLuaReplayMaxContinuousSteps = 1000;

    // --- Dynamic sprite layer system using flat Z-index ordering ---
    struct SpriteNode {
        std::string path;
        LayerState state;
        SpriteTransform transform;
        SDL_FRect screenRect{ 0.0f, 0.0f, 0.0f, 0.0f };
        bool screenRectValid = false;
        SpriteInteractionBinding interaction;
    };

    struct SpriteLayerEntry {
        int slot = 0;
        SpriteNode node;
    };

    using SpriteLayerList = std::vector<SpriteLayerEntry>;

    [[nodiscard]] SpriteLayerList::iterator LowerBoundSpriteLayer(int slot) noexcept {
        return std::lower_bound(
            m_spriteLayers.begin(),
            m_spriteLayers.end(),
            slot,
            [](const SpriteLayerEntry& entry, int targetSlot) {
                return entry.slot < targetSlot;
            });
    }

    [[nodiscard]] SpriteLayerList::const_iterator LowerBoundSpriteLayer(int slot) const noexcept {
        return std::lower_bound(
            m_spriteLayers.begin(),
            m_spriteLayers.end(),
            slot,
            [](const SpriteLayerEntry& entry, int targetSlot) {
                return entry.slot < targetSlot;
            });
    }

    [[nodiscard]] SpriteNode* FindSpriteLayer(int slot) noexcept {
        auto it = LowerBoundSpriteLayer(slot);
        if (it == m_spriteLayers.end() || it->slot != slot) {
            return nullptr;
        }
        return &it->node;
    }

    [[nodiscard]] const SpriteNode* FindSpriteLayer(int slot) const noexcept {
        auto it = LowerBoundSpriteLayer(slot);
        if (it == m_spriteLayers.end() || it->slot != slot) {
            return nullptr;
        }
        return &it->node;
    }

    SpriteNode& GetOrCreateSpriteLayer(int slot) {
        auto it = LowerBoundSpriteLayer(slot);
        if (it != m_spriteLayers.end() && it->slot == slot) {
            return it->node;
        }

        it = m_spriteLayers.insert(it, SpriteLayerEntry{ slot, SpriteNode{} });
        return it->node;
    }

    std::string m_bgPath;

    LayerState m_bgLayer;
    // Flat sorted by slot ID (Z-index): lower ID = back, higher ID = front.
    SpriteLayerList m_spriteLayers;
    std::vector<ObserveHotspot> m_observeHotspots;
    std::string m_pendingLuaObserveId;
    std::string m_pendingLuaVideoId;
    float m_observePanScale = 1.0f;
    bool m_observeAllowCancel = false;
    std::uint64_t m_observeRevision = 0;

    float m_bgTransitionSeconds = 0.45f;
    float m_spriteTransitionSeconds = 0.28f;
    BgTransitionEffect m_bgTransitionEffect = BgTransitionEffect::Crossfade;
    int m_bgDissolveCellSizePx = 28;

    float m_shakeIntensity = 0.0f;
    float m_shakeDuration = 0.0f;
    float m_shakeRemaining = 0.0f;
    SDL_FPoint m_cameraOffset{ 0.0f, 0.0f };
    float m_letterboxRatio = 0.0f;
    Tween<float> m_letterboxTween;
    SDL_Color m_screenFadeColor{ 0, 0, 0, 0 };
    Tween<float> m_screenFadeTween;
    float m_screenFadeAlpha = 0.0f;
    Camera m_camera{};
    vn::VideoPlayer m_videoPlayer;
    std::mt19937 m_rng{ std::random_device{}() };

    static constexpr std::size_t kMaxHistorySteps = 50;
};
