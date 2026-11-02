#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <string>
#include <tuple>
#include <utility>
#include <unordered_map>
#include <variant>
#include <vector>

#if defined(VN_ENGINE_HAS_LUA) && VN_ENGINE_HAS_LUA
#include <sol/sol.hpp>
#endif

namespace vn {

struct ObserveHotspot {
    SDL_FRect rect{ 0.0f, 0.0f, 0.0f, 0.0f };
    std::string targetId;
    std::string hoverText;
};

struct ParallelActionSpec {
    std::string kind;
    std::unordered_map<std::string, double> numericArgs;
    std::unordered_map<std::string, std::string> stringArgs;
    std::unordered_map<std::string, bool> boolArgs;
};

// Explicit dependency bundle consumed by the Lua runtime.
struct ScriptHostServices {
    std::function<std::string(const std::string&)> translate;
    std::function<bool()> isReplayActive;
    std::function<void(const std::string&, bool)> loadBG;
    std::function<void(const std::string&, int, bool)> loadSpriteBySlot;
    std::function<bool(const std::string&)> preloadImage;
    std::function<void(int, float, float, float, float, float, float, float, bool, float, float)> setSpriteTransform;
    std::function<void(int)> clearSpriteTransform;
    std::function<void(int, float, float, float, float, bool)> setSpriteClipRect;
    std::function<void(int)> clearSpriteClipRect;
    std::function<void(float, float, bool)> setLetterbox;
    std::function<void(int, const std::string&, const std::string&, const std::string&)> setSpriteInteraction;
    std::function<void(int)> clearSpriteInteraction;
    std::function<void(const std::string&)> playBGM;
    std::function<void()> stopBGM;
    std::function<void(const std::string&)> playSFX;
    std::function<void(const std::string&)> playVoice;
    std::function<void()> stopVoice;
    std::function<void(const std::string&, bool)> setFlag;
    std::function<bool(const std::string&)> getFlag;
    std::function<void(const std::string&, double)> setNumber;
    std::function<double(const std::string&)> getNumber;
    std::function<void(const std::string&)> setChapterName;
    std::function<void(const std::string&)> setBackgroundTransitionEffect;
    std::function<void(float, float)> startScreenShake;
    std::function<bool(const std::string&, const std::string&, const std::string&, const std::string&)> queueLuaDialogue;
    std::function<bool(float)> queueLuaWait;
    std::function<bool(float, float)> queueLuaShake;
    std::function<bool(const std::string&, const std::string&, bool, bool)> queueLuaVideo;
    std::function<bool(const std::vector<ParallelActionSpec>&)> queueParallelActions;
    std::function<std::tuple<int, bool>(const std::vector<std::string>&, const std::string&)> queueLuaChoice;
    std::function<std::tuple<std::string, bool>(std::vector<ObserveHotspot>, float, bool, const std::string&)> queueLuaObserve;
    std::function<void(const std::string&)> onScriptRuntimeError;
    std::function<void()> onScriptCoroutineEnded;

    [[nodiscard]] bool IsBound() const noexcept {
        return static_cast<bool>(translate) && static_cast<bool>(loadBG);
    }

    [[nodiscard]] std::string Translate(const std::string& key) const {
        return translate ? translate(key) : std::string("[") + key + "]";
    }
    [[nodiscard]] bool IsReplayActive() const { return isReplayActive ? isReplayActive() : false; }
    void LoadBG(const std::string& path, bool immediate) const { if (loadBG) loadBG(path, immediate); }
    void LoadSpriteBySlot(const std::string& path, int slot, bool immediate) const { if (loadSpriteBySlot) loadSpriteBySlot(path, slot, immediate); }
    [[nodiscard]] bool PreloadImage(const std::string& path) const { return preloadImage ? preloadImage(path) : false; }
    void SetSpriteTransform(int slot, float x, float y, float scaleX, float scaleY, float rotationDeg, float anchorX, float anchorY, bool useAbsolute, float width, float height) const {
        if (setSpriteTransform) setSpriteTransform(slot, x, y, scaleX, scaleY, rotationDeg, anchorX, anchorY, useAbsolute, width, height);
    }
    void ClearSpriteTransform(int slot) const { if (clearSpriteTransform) clearSpriteTransform(slot); }
    void SetSpriteClipRect(int slot, float x, float y, float w, float h, bool enabled) const { if (setSpriteClipRect) setSpriteClipRect(slot, x, y, w, h, enabled); }
    void ClearSpriteClipRect(int slot) const { if (clearSpriteClipRect) clearSpriteClipRect(slot); }
    void SetLetterbox(float ratio, float durationSeconds, bool immediate) const { if (setLetterbox) setLetterbox(ratio, durationSeconds, immediate); }
    void SetSpriteInteraction(int slot, const std::string& characterTag, const std::string& partTag, const std::string& callbackName) const {
        if (setSpriteInteraction) setSpriteInteraction(slot, characterTag, partTag, callbackName);
    }
    void ClearSpriteInteraction(int slot) const { if (clearSpriteInteraction) clearSpriteInteraction(slot); }
    void PlayBGM(const std::string& path) const { if (playBGM) playBGM(path); }
    void StopBGM() const { if (stopBGM) stopBGM(); }
    void PlaySFX(const std::string& path) const { if (playSFX) playSFX(path); }
    void PlayVoice(const std::string& path) const { if (playVoice) playVoice(path); }
    void StopVoice() const { if (stopVoice) stopVoice(); }
    void SetFlag(const std::string& name, bool value) const { if (setFlag) setFlag(name, value); }
    [[nodiscard]] bool GetFlag(const std::string& name) const { return getFlag ? getFlag(name) : false; }
    void SetNumber(const std::string& name, double value) const { if (setNumber) setNumber(name, value); }
    [[nodiscard]] double GetNumber(const std::string& name) const { return getNumber ? getNumber(name) : 0.0; }
    void SetChapterName(const std::string& chapterName) const { if (setChapterName) setChapterName(chapterName); }
    void SetBackgroundTransitionEffect(const std::string& effect) const { if (setBackgroundTransitionEffect) setBackgroundTransitionEffect(effect); }
    void StartScreenShake(float intensity, float duration) const { if (startScreenShake) startScreenShake(intensity, duration); }
    [[nodiscard]] bool QueueLuaDialogue(const std::string& speaker, const std::string& text, const std::string& voicePath, const std::string& lineId) const {
        return queueLuaDialogue ? queueLuaDialogue(speaker, text, voicePath, lineId) : false;
    }
    [[nodiscard]] bool QueueLuaWait(float seconds) const { return queueLuaWait ? queueLuaWait(seconds) : false; }
    [[nodiscard]] bool QueueLuaShake(float intensity, float duration) const { return queueLuaShake ? queueLuaShake(intensity, duration) : false; }
    [[nodiscard]] bool QueueLuaVideo(const std::string& path, const std::string& videoId, bool skippable, bool audioEnabled) const {
        return queueLuaVideo ? queueLuaVideo(path, videoId, skippable, audioEnabled) : false;
    }
    [[nodiscard]] bool QueueParallelActions(const std::vector<ParallelActionSpec>& actions) const {
        return queueParallelActions ? queueParallelActions(actions) : false;
    }
    [[nodiscard]] std::tuple<int, bool> QueueLuaChoice(const std::vector<std::string>& labels, const std::string& choiceId) const {
        return queueLuaChoice ? queueLuaChoice(labels, choiceId) : std::tuple<int, bool>{ 0, false };
    }
    [[nodiscard]] std::tuple<std::string, bool> QueueLuaObserve(std::vector<ObserveHotspot> hotspots, float panScale, bool allowCancel, const std::string& observeId) const {
        return queueLuaObserve ? queueLuaObserve(std::move(hotspots), panScale, allowCancel, observeId) : std::tuple<std::string, bool>{ {}, false };
    }
    void OnScriptRuntimeError(const std::string& message) const { if (onScriptRuntimeError) onScriptRuntimeError(message); }
    void OnScriptCoroutineEnded() const { if (onScriptCoroutineEnded) onScriptCoroutineEnded(); }
};

class ScriptDirector {
public:
    using ResumeValue = std::variant<std::monostate, int, std::string>;

    enum class WaitReason {
        None,
        Dialogue,
        Choice,
        Observe,
        ActionQueue,
        Video,
    };

    struct ChoiceRecord {
        std::string id;
        int selectedIndex = 0;
    };

    struct ObserveRecord {
        std::string id;
        std::string selectedTarget;
    };

    struct SuspensionPoint {
        std::uint64_t sequence = 0;
        WaitReason kind = WaitReason::None;
        std::string anchor;
        std::uint32_t occurrence = 0;
        std::size_t choiceCursor = 0;
        std::size_t observeCursor = 0;
    };

    struct ScriptState {
        std::string currentYieldId;
        std::string pendingChoiceId;
        std::string pendingObserveId;
        WaitReason waitReason = WaitReason::None;
        std::vector<ChoiceRecord> choiceHistory;
        std::vector<ObserveRecord> observeHistory;
        SuspensionPoint currentSuspension;
        std::uint64_t nextYieldSequence = 0;
        std::unordered_map<std::string, std::uint32_t> yieldAnchorOccurrences;

        std::string replayTargetId;
        SuspensionPoint replayTargetSuspension;
        bool replayActive = false;
        bool replayDiverged = false;
        std::size_t replayStepCounter = 0;
        std::size_t replayChoiceCursor = 0;
        std::size_t replayObserveCursor = 0;
        std::optional<std::unordered_map<std::string, bool>> replayFlags;
        std::optional<std::unordered_map<std::string, double>> replayNumbers;

        std::uint64_t deterministicSeed = 1;
        std::uint64_t deterministicCounter = 0;
    };

#if defined(VN_ENGINE_HAS_LUA) && VN_ENGINE_HAS_LUA
    struct LuaRuntime {
        std::unique_ptr<sol::state> state;
        std::unique_ptr<sol::thread> mainThread;
        std::unique_ptr<sol::coroutine> mainCoroutine;
        std::unique_ptr<sol::thread> interactionThread;
        std::unique_ptr<sol::coroutine> interactionCoroutine;
        sol::environment m_sandbox;
    };
#else
    struct LuaRuntime {};
#endif

    ScriptDirector() = default;
    explicit ScriptDirector(ScriptHostServices services)
        : m_services(std::move(services)), m_context(&m_services) {}

    void SetServices(ScriptHostServices services) {
        m_services = std::move(services);
        m_context = &m_services;
    }

    [[nodiscard]] const ScriptHostServices* GetServices() const {
        return m_context;
    }

    // Runtime lifecycle.
    bool LoadScript(const std::string& scriptPath,
                    const std::string& entryPoint = "main",
                    bool autoStart = true);
    bool LoadScriptFromFile(const std::string& scriptPath,
                            const std::string& entryPoint = "main",
                            bool autoStart = true);
    bool Resume();
    bool Resume(int value);
    bool Resume(const std::string& value);
    bool TriggerInteractionCallback(const std::string& callbackName,
                                    const std::string& characterTag,
                                    const std::string& partTag,
                                    const std::string& eventName);
    void Reset();

    [[nodiscard]] ScriptState ExportState() const;
    void ImportState(const ScriptState& state, bool enableReplay);
    [[nodiscard]] const ScriptState& GetState() const { return m_scriptState; }
    [[nodiscard]] ScriptState& MutableState() { return m_scriptState; }
    SuspensionPoint RecordSuspension(WaitReason kind, const std::string& anchor);
    [[nodiscard]] bool HasStructuredReplayTarget() const;
    [[nodiscard]] bool CurrentSuspensionMatchesReplayTarget() const;
    void CompleteReplayAtCurrentSuspension();

    static const char* WaitReasonToString(WaitReason reason);
    static WaitReason WaitReasonFromString(const std::string& raw);

    [[nodiscard]] bool HasRuntimeError() const {
        return !m_runtimeError.empty();
    }

    [[nodiscard]] const std::string& GetRuntimeError() const {
        return m_runtimeError;
    }

private:
    bool InitializeLuaState();
    bool CreateLuaCoroutine();
    bool RegisterDeterministicLuaLibraries();
    bool RegisterVnApiBindings();
    bool ResumeImpl(const ResumeValue& value);
    void ResetDeterministicRng(std::optional<std::uint64_t> seed = std::nullopt);
    double NextDeterministicRandom01();
    std::int64_t NextDeterministicTimeTick();

    ScriptHostServices m_services;
    ScriptHostServices* m_context = nullptr;
    std::unique_ptr<LuaRuntime> m_luaRuntime;
    ScriptState m_scriptState;
    std::string m_scriptPath;
    std::string m_entryPoint = "main";
    std::string m_runtimeError;
    std::mt19937_64 m_deterministicRng{ 1 };
};

} // namespace vn
