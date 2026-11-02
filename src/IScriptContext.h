#pragma once

#include <string>

#include "ScriptDirector.h"

namespace vn {

class IScriptContext {
public:
    virtual ~IScriptContext() = default;

    virtual void SetServices(ScriptHostServices services) = 0;
    virtual bool LoadScriptFromFile(const std::string& scriptPath,
                                    const std::string& entryPoint = "main",
                                    bool autoStart = true) = 0;
    virtual bool Resume() = 0;
    virtual bool Resume(int value) = 0;
    virtual bool Resume(const std::string& value) = 0;
    virtual bool TriggerInteractionCallback(const std::string& callbackName,
                                            const std::string& characterTag,
                                            const std::string& partTag,
                                            const std::string& eventName) = 0;
    virtual void Reset() = 0;

    [[nodiscard]] virtual ScriptDirector::ScriptState ExportState() const = 0;
    virtual void ImportState(const ScriptDirector::ScriptState& state, bool enableReplay) = 0;
    [[nodiscard]] virtual const ScriptDirector::ScriptState& GetState() const = 0;
    [[nodiscard]] virtual ScriptDirector::ScriptState& MutableState() = 0;
    virtual ScriptDirector::SuspensionPoint RecordSuspension(ScriptDirector::WaitReason kind,
                                                             const std::string& anchor) = 0;
    [[nodiscard]] virtual bool HasStructuredReplayTarget() const = 0;
    [[nodiscard]] virtual bool CurrentSuspensionMatchesReplayTarget() const = 0;
    virtual void CompleteReplayAtCurrentSuspension() = 0;
    [[nodiscard]] virtual bool HasRuntimeError() const = 0;
    [[nodiscard]] virtual const std::string& GetRuntimeError() const = 0;
};

} // namespace vn
