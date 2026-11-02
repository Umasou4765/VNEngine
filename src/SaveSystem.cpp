#include "SaveSystem.h"

#include "ScriptIdentity.h"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

vn::SaveSnapshot SaveSystem::Build(const SceneManager& scene) {
    vn::SaveSnapshot out;
    const vn::ScriptDirector::ScriptState scriptState = scene.ScriptContextRef().ExportState();
    out.scriptPath = scene.m_scriptPath;
    out.scriptContentHash = vn::ComputeFileContentHash(out.scriptPath);
    out.luaEntryPoint = scene.m_luaEntryPoint;
    out.luaResumeId = scriptState.currentYieldId;
    out.luaWaitReason = vn::ScriptDirector::WaitReasonToString(scriptState.waitReason);
    out.luaYieldSequence = scriptState.currentSuspension.sequence;
    out.luaYieldOccurrence = scriptState.currentSuspension.occurrence;
    out.luaChoiceCursor = scriptState.currentSuspension.choiceCursor;
    out.luaObserveCursor = scriptState.currentSuspension.observeCursor;
    out.luaChoiceHistory.reserve(scriptState.choiceHistory.size());
    for (const auto& choice : scriptState.choiceHistory) {
        out.luaChoiceHistory.push_back({ choice.id, choice.selectedIndex });
    }
    out.luaObserveHistory.reserve(scriptState.observeHistory.size());
    for (const auto& observe : scriptState.observeHistory) {
        out.luaObserveHistory.push_back({ observe.id, observe.selectedTarget });
    }
    out.luaDeterministicSeed = scriptState.deterministicSeed;
    out.luaDeterministicCounter = scriptState.deterministicCounter;
    out.chapterName = scene.GameStateRef().chapterName;

    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::tm parts{};
#ifdef _WIN32
    localtime_s(&parts, &now_c);
#else
    localtime_r(&now_c, &parts);
#endif
    std::stringstream ss;
    ss << std::put_time(&parts, "%Y-%m-%d %H:%M");
    out.saveTime = ss.str();

    out.bgPath = scene.m_bgPath;
    for (const auto& [slot, node] : scene.m_spriteLayers) {
        if (node.path.empty()) continue;

        vn::SaveSnapshot::SaveSpriteState s;
        s.slot = slot;
        s.path = node.path;
        s.x = node.transform.position.x;
        s.y = node.transform.position.y;
        s.scaleX = node.transform.scale.x;
        s.scaleY = node.transform.scale.y;
        s.rotation = node.transform.rotationDeg;
        s.anchorX = node.transform.anchor.x;
        s.anchorY = node.transform.anchor.y;
        s.useAbsolute = node.transform.useAbsolute;
        s.r = node.transform.tintColor.r;
        s.g = node.transform.tintColor.g;
        s.b = node.transform.tintColor.b;
        s.alpha = node.transform.opacity;
        out.spriteStates.push_back(s);
    }
    out.flags = scene.GameStateRef().flags;
    out.numVars = scene.GameStateRef().numbers;
    out.currentBgmPath = scene.GameStateRef().currentBgmPath;
    out.cameraX = scene.m_camera.targetX;
    out.cameraY = scene.m_camera.targetY;
    out.cameraZoom = scene.m_camera.targetZoom;
    // Engine owns UI state and injects dialogueLog immediately before saving.
    return out;
}

bool SaveSystem::Restore(SceneManager& scene,
                         const vn::SaveSnapshot& data,
                         SceneManager::RestoreMode mode) {
#if defined(VN_ENGINE_HAS_LUA) && VN_ENGINE_HAS_LUA
    const bool allowReplay = (mode == SceneManager::RestoreMode::Replay);
    vn::ScriptDirector::ScriptState scriptState = scene.ScriptContextRef().ExportState();
    scriptState.currentYieldId = data.luaResumeId;
    scriptState.pendingChoiceId.clear();
    scriptState.pendingObserveId.clear();
    scriptState.waitReason = vn::ScriptDirector::WaitReasonFromString(data.luaWaitReason);
    scriptState.currentSuspension = {};
    scriptState.currentSuspension.sequence = data.luaYieldSequence;
    scriptState.currentSuspension.kind = scriptState.waitReason;
    scriptState.currentSuspension.anchor = data.luaResumeId;
    scriptState.currentSuspension.occurrence = data.luaYieldOccurrence;
    scriptState.currentSuspension.choiceCursor = data.luaChoiceCursor;
    scriptState.currentSuspension.observeCursor = data.luaObserveCursor;
    scriptState.choiceHistory.clear();
    scriptState.choiceHistory.reserve(data.luaChoiceHistory.size());
    for (const auto& choice : data.luaChoiceHistory) {
        scriptState.choiceHistory.push_back({ choice.id, choice.selectedIndex });
    }
    scriptState.observeHistory.clear();
    scriptState.observeHistory.reserve(data.luaObserveHistory.size());
    for (const auto& observe : data.luaObserveHistory) {
        scriptState.observeHistory.push_back({ observe.id, observe.selectedTarget });
    }
    if (data.luaDeterministicSeed != 0) {
        scriptState.deterministicSeed = data.luaDeterministicSeed;
    }
    scriptState.deterministicCounter = data.luaDeterministicCounter;
    scriptState.replayTargetId = data.luaResumeId;
    scriptState.replayTargetSuspension = scriptState.currentSuspension;
    scriptState.replayFlags = data.flags;
    scriptState.replayNumbers = data.numVars;
    scene.ScriptContextRef().ImportState(scriptState, allowReplay);
    scriptState = scene.ScriptContextRef().ExportState();

    scene.m_restoreWarningMessage.clear();
    scene.GameStateRef().rollbackHistory.clear();
    scene.GameStateRef().flags.clear();
    scene.GameStateRef().numbers.clear();
    scene.ScriptContextRef().MutableState().pendingChoiceId.clear();
    scene.m_ended = false;
    scene.state = SceneMode::DIALOGUE;

    if (scene.m_actionQueue) scene.m_actionQueue->FlushAll();

    scene.ApplySaveSnapshot(data);

    if (allowReplay && !scriptState.replayTargetId.empty()) {
        scene.ResumeLuaCoroutine();
        scriptState = scene.ScriptContextRef().ExportState();

        if (scriptState.replayActive || scriptState.replayDiverged) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "Lua replay restore diverged at target '%s' (last yield '%s'). "
                        "Fallback to snapshot-only state. For complex Lua gameplay state, "
                        "prefer explicit Lua table serialization over replay.",
                        scriptState.replayTargetId.c_str(),
                        scriptState.currentYieldId.c_str());
            scriptState.replayActive = false;
            scriptState.replayTargetId.clear();
            scriptState.replayStepCounter = 0;
            scriptState.replayFlags.reset();
            scriptState.replayNumbers.reset();
            scriptState.waitReason = vn::ScriptDirector::WaitReason::None;
            scene.ScriptContextRef().ImportState(scriptState, false);
        }
    }

    if (!allowReplay && !data.luaResumeId.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "RestoreFromSave: replay disabled for persistent load at '%s'; using snapshot-only state.",
                    data.luaResumeId.c_str());
    }

    const vn::ScriptDirector::WaitReason savedWaitReason = scriptState.waitReason;
    if (!allowReplay &&
        (savedWaitReason == vn::ScriptDirector::WaitReason::Choice ||
         savedWaitReason == vn::ScriptDirector::WaitReason::Observe ||
         savedWaitReason == vn::ScriptDirector::WaitReason::ActionQueue ||
         savedWaitReason == vn::ScriptDirector::WaitReason::Video)) {
        scene.ForceClearUI();
        scene.ScriptContextRef().MutableState().pendingChoiceId.clear();
        scene.ClearObserveBindings();
        scene.state = SceneMode::DIALOGUE;
        scene.ScriptContextRef().MutableState().waitReason = vn::ScriptDirector::WaitReason::Dialogue;

        if (savedWaitReason == vn::ScriptDirector::WaitReason::Choice) {
            scene.m_restoreWarningMessage = "Incompatible save state detected (CHOICE). Fallback to stable dialogue mode.";
        } else if (savedWaitReason == vn::ScriptDirector::WaitReason::Observe) {
            scene.m_restoreWarningMessage = "Incompatible save state detected (OBSERVE). Fallback to stable dialogue mode.";
        } else if (savedWaitReason == vn::ScriptDirector::WaitReason::Video) {
            scene.m_restoreWarningMessage = "Transient video state detected on load. Fallback to stable dialogue mode.";
        } else {
            scene.m_restoreWarningMessage = "Transient action state detected on load. Fallback to stable dialogue mode.";
        }

        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "RestoreFromSave SnapshotOnly fallback: %s (resumeId='%s').",
                    scene.m_restoreWarningMessage.c_str(),
                    data.luaResumeId.c_str());
        return true;
    }

    if (savedWaitReason == vn::ScriptDirector::WaitReason::Dialogue) {
        scene.state = SceneMode::DIALOGUE;
        scene.ScriptContextRef().MutableState().waitReason = vn::ScriptDirector::WaitReason::Dialogue;
    } else if (savedWaitReason == vn::ScriptDirector::WaitReason::ActionQueue) {
        scene.state = SceneMode::DIALOGUE;
        if (allowReplay) {
            if (scene.m_actionQueue && scene.m_actionQueue->IsBlocked()) {
                scene.ScriptContextRef().MutableState().waitReason = vn::ScriptDirector::WaitReason::ActionQueue;
            } else {
                scene.ScriptContextRef().MutableState().waitReason = vn::ScriptDirector::WaitReason::None;
                (void)scene.ResumeLuaCoroutine();
            }
        } else {
            scene.ScriptContextRef().MutableState().waitReason = vn::ScriptDirector::WaitReason::Dialogue;
        }
    } else if (savedWaitReason == vn::ScriptDirector::WaitReason::Observe) {
        if (!scene.m_pendingLuaObserveId.empty() || !scene.m_observeHotspots.empty()) {
            scene.state = SceneMode::OBSERVE;
            scene.ScriptContextRef().MutableState().waitReason = vn::ScriptDirector::WaitReason::Observe;
        } else {
            scene.state = SceneMode::DIALOGUE;
            scene.ScriptContextRef().MutableState().waitReason = vn::ScriptDirector::WaitReason::Dialogue;
        }
    } else if (savedWaitReason == vn::ScriptDirector::WaitReason::Choice) {
        // Replay may have queued the last ShowDialogue event; hide only the menu.
        scene.HideChoicesUI();
        scene.state = SceneMode::DIALOGUE;
        scene.ScriptContextRef().MutableState().waitReason = vn::ScriptDirector::WaitReason::Dialogue;
    } else if (savedWaitReason == vn::ScriptDirector::WaitReason::Video) {
        scene.state = SceneMode::DIALOGUE;
        scene.ScriptContextRef().MutableState().waitReason = vn::ScriptDirector::WaitReason::Dialogue;
    }
    return true;
#else
    (void)scene;
    (void)data;
    (void)mode;
    return false;
#endif
}

bool SaveSystem::QuickSave(SaveManager& saveManager, const SceneManager& scene, int slot) {
    return saveManager.SaveSlot(slot, Build(scene));
}

bool SaveSystem::QuickLoad(const SaveManager& saveManager, SceneManager& scene, int slot) {
    vn::SaveSnapshot outData;
    if (!saveManager.LoadSlot(slot, outData)) {
        return false;
    }
    return Restore(scene, outData, SceneManager::RestoreMode::Replay);
}
