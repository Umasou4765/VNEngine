#include "SceneManager.h"
#include "SaveSystem.h"
#include "AssetManager.h"
#include "AudioManager.h"
#include "ActionQueue.h"

#include <utility>

SceneManager::SceneManager() = default;
SceneManager::~SceneManager() = default;

vn::SaveSnapshot SceneManager::BuildSaveData() const {
    return SaveSystem::Build(*this);
}

void SceneManager::RestoreFromSave(const vn::SaveSnapshot& data, RestoreMode mode) {
    (void)SaveSystem::Restore(*this, data, mode);
}


void SceneManager::ResetScriptState() {
    if (m_actionQueue) m_actionQueue->FlushAll();
    if (m_assets) m_assets->ClearUnused();
    HideChoicesUI();
    ClearDialogueUI();

    m_ended = false;
    m_luaErrorMessage.clear();
    m_scriptPath.clear();
    m_luaEntryPoint = "main";
    GameStateRef().ResetForNewScript();
    m_observeRevision = 0;
    state = SceneMode::DIALOGUE;
    ResetVisualState();

    ScriptContextRef().Reset();
}

void SceneManager::OnScriptRuntimeError(const std::string& message) {
    m_luaErrorMessage = message;
    m_ended = true;
    m_videoPlayer.Stop();
    ScriptContextRef().MutableState().waitReason = vn::ScriptDirector::WaitReason::None;
    state = SceneMode::DIALOGUE;
    HideChoicesUI();
    ClearDialogueUI();
}

void SceneManager::OnScriptCoroutineEnded() {
    m_ended = true;
    m_videoPlayer.Stop();
    ScriptContextRef().MutableState().waitReason = vn::ScriptDirector::WaitReason::None;
    state = SceneMode::DIALOGUE;
    HideChoicesUI();
    ClearDialogueUI();
}

bool SceneManager::ResumeLuaCoroutine() {
    return ScriptContextRef().Resume();
}

bool SceneManager::ResumeLuaCoroutine(int resumeValue) {
    return ScriptContextRef().Resume(resumeValue);
}

bool SceneManager::ResumeLuaCoroutine(const std::string& resumeValue) {
    return ScriptContextRef().Resume(resumeValue);
}
