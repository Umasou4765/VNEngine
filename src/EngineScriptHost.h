#pragma once

#include <string>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#include "AudioManager.h"
#include "GameState.h"
#include "SceneManager.h"
#include "ScriptDirector.h"

class EngineScriptHost final {
public:
    [[nodiscard]] static vn::ScriptHostServices Build(SceneManager& scene,
                                                       vn::GameState& gameState,
                                                       vn::AudioManager& audio) {
        vn::ScriptHostServices services;

        const auto replayStoresActive = [&scene]() {
            const auto& scriptState = scene.ScriptContextRef().GetState();
            return scriptState.replayActive &&
                   scriptState.replayFlags.has_value() &&
                   scriptState.replayNumbers.has_value();
        };

        const auto activeFlags = [&scene, &gameState, replayStoresActive]() -> const std::unordered_map<std::string, bool>& {
            if (replayStoresActive()) {
                return *scene.ScriptContextRef().GetState().replayFlags;
            }
            return gameState.flags;
        };

        const auto mutableFlags = [&scene, &gameState, replayStoresActive]() -> std::unordered_map<std::string, bool>& {
            if (replayStoresActive()) {
                return *scene.ScriptContextRef().MutableState().replayFlags;
            }
            return gameState.flags;
        };

        const auto activeNumbers = [&scene, &gameState, replayStoresActive]() -> const std::unordered_map<std::string, double>& {
            if (replayStoresActive()) {
                return *scene.ScriptContextRef().GetState().replayNumbers;
            }
            return gameState.numbers;
        };

        const auto mutableNumbers = [&scene, &gameState, replayStoresActive]() -> std::unordered_map<std::string, double>& {
            if (replayStoresActive()) {
                return *scene.ScriptContextRef().MutableState().replayNumbers;
            }
            return gameState.numbers;
        };

        services.translate = [&scene](const std::string& key) {
            return scene.Translate(key);
        };
        services.isReplayActive = [&scene]() {
            return scene.IsReplayActive();
        };
        services.loadBG = [&scene](const std::string& path, bool immediate) {
            scene.LoadBG(path, immediate);
        };
        services.loadSpriteBySlot = [&scene](const std::string& path, int slot, bool immediate) {
            scene.LoadSpriteBySlot(path, slot, immediate);
        };
        services.preloadImage = [&scene](const std::string& path) {
            return scene.PreloadImage(path);
        };
        services.setSpriteTransform = [&scene](int slot,
                                                float x,
                                                float y,
                                                float scaleX,
                                                float scaleY,
                                                float rotationDeg,
                                                float anchorX,
                                                float anchorY,
                                                bool useAbsolute,
                                                float width,
                                                float height) {
            scene.SetSpriteTransform(slot,
                                     x,
                                     y,
                                     scaleX,
                                     scaleY,
                                     rotationDeg,
                                     anchorX,
                                     anchorY,
                                     useAbsolute,
                                     width,
                                     height);
        };
        services.clearSpriteTransform = [&scene](int slot) {
            scene.ClearSpriteTransform(slot);
        };
        services.setSpriteClipRect = [&scene](int slot, float x, float y, float w, float h, bool enabled) {
            scene.SetSpriteClipRect(slot, x, y, w, h, enabled);
        };
        services.clearSpriteClipRect = [&scene](int slot) {
            scene.ClearSpriteClipRect(slot);
        };
        services.setLetterbox = [&scene](float ratio, float durationSeconds, bool immediate) {
            scene.SetLetterbox(ratio, durationSeconds, immediate);
        };
        services.setSpriteInteraction = [&scene](int slot,
                                                  const std::string& characterTag,
                                                  const std::string& partTag,
                                                  const std::string& callbackName) {
            scene.SetSpriteInteraction(slot, characterTag, partTag, callbackName);
        };
        services.clearSpriteInteraction = [&scene](int slot) {
            scene.ClearSpriteInteraction(slot);
        };

        services.playBGM = [&scene, &gameState, &audio](const std::string& path) {
            if (scene.IsReplayActive()) {
                return;
            }
            gameState.currentBgmPath = path;
            audio.PlayBGM(path);
        };
        services.stopBGM = [&scene, &gameState, &audio]() {
            if (scene.IsReplayActive()) {
                return;
            }
            gameState.currentBgmPath.clear();
            audio.StopBGM();
        };
        services.playSFX = [&scene, &audio](const std::string& path) {
            if (!scene.IsReplayActive()) {
                audio.PlaySFX(path);
            }
        };
        services.playVoice = [&scene, &audio](const std::string& path) {
            if (!scene.IsReplayActive()) {
                audio.PlayVoice(path);
            }
        };
        services.stopVoice = [&audio]() {
            audio.StopVoice();
        };

        services.setFlag = [mutableFlags](const std::string& name, bool value) {
            mutableFlags()[name] = value;
        };
        services.getFlag = [activeFlags](const std::string& name) {
            const auto& store = activeFlags();
            const auto it = store.find(name);
            return it != store.end() && it->second;
        };
        services.setNumber = [mutableNumbers](const std::string& name, double value) {
            mutableNumbers()[name] = value;
        };
        services.getNumber = [activeNumbers](const std::string& name) {
            const auto& store = activeNumbers();
            const auto it = store.find(name);
            return it != store.end() ? it->second : 0.0;
        };
        services.setChapterName = [&scene, &gameState](const std::string& chapterName) {
            if (!scene.IsReplayActive()) {
                gameState.chapterName = chapterName;
            }
        };

        services.setBackgroundTransitionEffect = [&scene](const std::string& effect) {
            scene.SetBackgroundTransitionEffect(effect);
        };
        services.startScreenShake = [&scene](float intensity, float duration) {
            scene.StartScreenShake(intensity, duration);
        };
        services.queueLuaDialogue = [&scene](const std::string& speaker,
                                              const std::string& text,
                                              const std::string& voicePath,
                                              const std::string& lineId) {
            return scene.QueueLuaDialogue(speaker, text, voicePath, lineId);
        };
        services.queueLuaWait = [&scene](float seconds) {
            return scene.QueueLuaWait(seconds);
        };
        services.queueLuaShake = [&scene](float intensity, float duration) {
            return scene.QueueLuaShake(intensity, duration);
        };
        services.queueLuaVideo = [&scene](const std::string& path,
                                           const std::string& videoId,
                                           bool skippable,
                                           bool audioEnabled) {
            return scene.QueueLuaVideo(path, videoId, skippable, audioEnabled);
        };
        services.queueParallelActions = [&scene](const std::vector<vn::ParallelActionSpec>& actions) {
            return scene.QueueParallelActions(actions);
        };
        services.queueLuaChoice = [&scene](const std::vector<std::string>& labels,
                                            const std::string& choiceId) {
            return scene.QueueLuaChoice(labels, choiceId);
        };
        services.queueLuaObserve = [&scene](std::vector<vn::ObserveHotspot> hotspots,
                                             float panScale,
                                             bool allowCancel,
                                             const std::string& observeId) {
            return scene.QueueLuaObserve(std::move(hotspots), panScale, allowCancel, observeId);
        };
        services.onScriptRuntimeError = [&scene](const std::string& message) {
            scene.OnScriptRuntimeError(message);
        };
        services.onScriptCoroutineEnded = [&scene]() {
            scene.OnScriptCoroutineEnded();
        };

        return services;
    }
};
