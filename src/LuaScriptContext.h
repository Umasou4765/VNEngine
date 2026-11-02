#pragma once

#include <utility>

#include "IScriptContext.h"

namespace vn {

class LuaScriptContext final : public IScriptContext {
public:
    LuaScriptContext() = default;
    explicit LuaScriptContext(ScriptHostServices services)
        : m_director(std::move(services)) {}

    void SetServices(ScriptHostServices services) override {
        m_director.SetServices(std::move(services));
    }

    bool LoadScriptFromFile(const std::string& scriptPath,
                            const std::string& entryPoint = "main",
                            bool autoStart = true) override {
        return m_director.LoadScriptFromFile(scriptPath, entryPoint, autoStart);
    }

    bool Resume() override {
        return m_director.Resume();
    }

    bool Resume(int value) override {
        return m_director.Resume(value);
    }

    bool Resume(const std::string& value) override {
        return m_director.Resume(value);
    }

    bool TriggerInteractionCallback(const std::string& callbackName,
                                    const std::string& characterTag,
                                    const std::string& partTag,
                                    const std::string& eventName) override {
        return m_director.TriggerInteractionCallback(callbackName, characterTag, partTag, eventName);
    }

    void Reset() override {
        m_director.Reset();
    }

    [[nodiscard]] ScriptDirector::ScriptState ExportState() const override {
        return m_director.ExportState();
    }

    void ImportState(const ScriptDirector::ScriptState& state, bool enableReplay) override {
        m_director.ImportState(state, enableReplay);
    }

    [[nodiscard]] const ScriptDirector::ScriptState& GetState() const override {
        return m_director.GetState();
    }

    [[nodiscard]] ScriptDirector::ScriptState& MutableState() override {
        return m_director.MutableState();
    }

    ScriptDirector::SuspensionPoint RecordSuspension(ScriptDirector::WaitReason kind,
                                                     const std::string& anchor) override {
        return m_director.RecordSuspension(kind, anchor);
    }

    [[nodiscard]] bool HasStructuredReplayTarget() const override {
        return m_director.HasStructuredReplayTarget();
    }

    [[nodiscard]] bool CurrentSuspensionMatchesReplayTarget() const override {
        return m_director.CurrentSuspensionMatchesReplayTarget();
    }

    void CompleteReplayAtCurrentSuspension() override {
        m_director.CompleteReplayAtCurrentSuspension();
    }

    [[nodiscard]] bool HasRuntimeError() const override {
        return m_director.HasRuntimeError();
    }

    [[nodiscard]] const std::string& GetRuntimeError() const override {
        return m_director.GetRuntimeError();
    }

private:
    ScriptDirector m_director;
};

} // namespace vn
