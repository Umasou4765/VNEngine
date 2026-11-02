#include "ScriptDirector.h"
#include "LuaRuntimeSafety.h"
#include "ReplayBindingSafety.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>

#include <SDL3/SDL_log.h>

#if defined(VN_ENGINE_HAS_LUA) && VN_ENGINE_HAS_LUA
#include <sol/sol.hpp>
#endif

namespace vn {

namespace {

#if defined(VN_ENGINE_HAS_LUA) && VN_ENGINE_HAS_LUA
bool ExecuteLuaFile(sol::state_view lua, sol::environment& sandbox, const std::string& path) {
    return vn::lua_safety::RunLuaWithInstructionBudget(lua.lua_state(), path.c_str(), [&]() {
        sol::load_result loaded = lua.load_file(path);
        if (!loaded.valid()) {
            sol::error err = loaded;
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Lua load failed for %s: %s", path.c_str(), err.what());
            return false;
        }

        sol::protected_function chunk = loaded;
        sol::set_environment(sandbox, chunk);
        sol::protected_function_result exec = chunk();
        if (!exec.valid()) {
            sol::error err = exec;
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Lua execution failed for %s: %s", path.c_str(), err.what());
            return false;
        }

        return true;
    });
}
#endif

} // namespace

bool ScriptDirector::LoadScript(const std::string& scriptPath,
                                const std::string& entryPoint,
                                bool autoStart) {
    return LoadScriptFromFile(scriptPath, entryPoint, autoStart);
}

bool ScriptDirector::LoadScriptFromFile(const std::string& scriptPath,
                                        const std::string& entryPoint,
                                        bool autoStart) {
    m_scriptPath = scriptPath;
    m_entryPoint = entryPoint.empty() ? "main" : entryPoint;
    m_runtimeError.clear();
    m_scriptState = {};
    ResetDeterministicRng(1);

    if (!m_context || !m_context->IsBound()) {
        m_runtimeError = "ScriptDirector has no ScriptHostServices";
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", m_runtimeError.c_str());
        return false;
    }

#if !defined(VN_ENGINE_HAS_LUA) || !VN_ENGINE_HAS_LUA
    (void)autoStart;
    m_runtimeError = "Lua support disabled at compile time";
    return false;
#else
    if (scriptPath.empty()) {
        m_runtimeError = "Lua load failed: empty script path";
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", m_runtimeError.c_str());
        return false;
    }

    const std::filesystem::path requestedPath(scriptPath);
    if (requestedPath.extension() != ".lua") {
        m_runtimeError = "Lua load failed: only .lua scripts are supported";
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s (%s)", m_runtimeError.c_str(), scriptPath.c_str());
        return false;
    }

    std::filesystem::path normalizedScriptPath;
    if (!vn::lua_safety::TryNormalizeTrustedScriptPath(requestedPath, normalizedScriptPath)) {
        m_runtimeError = "Lua load failed: script path is outside assets/scripts";
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s (%s)", m_runtimeError.c_str(), scriptPath.c_str());
        return false;
    }

    if (!std::filesystem::exists(normalizedScriptPath)) {
        m_runtimeError = "Lua load failed: file not found";
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s (%s)", m_runtimeError.c_str(), scriptPath.c_str());
        return false;
    }

    m_scriptPath = normalizedScriptPath.string();

    if (!InitializeLuaState()) {
        return false;
    }

    if (!CreateLuaCoroutine()) {
        return false;
    }

    if (autoStart) {
        return Resume();
    }
    return true;
#endif
}

bool ScriptDirector::Resume() {
    return ResumeImpl(ResumeValue{ std::monostate{} });
}

bool ScriptDirector::Resume(int value) {
    return ResumeImpl(ResumeValue{ value });
}

bool ScriptDirector::Resume(const std::string& value) {
    return ResumeImpl(ResumeValue{ value });
}

ScriptDirector::ScriptState ScriptDirector::ExportState() const {
    return m_scriptState;
}

void ScriptDirector::ImportState(const ScriptState& state, bool enableReplay) {
    m_scriptState = state;
    m_scriptState.currentSuspension = {};
    m_scriptState.nextYieldSequence = 0;
    m_scriptState.yieldAnchorOccurrences.clear();
    m_scriptState.replayChoiceCursor = 0;
    m_scriptState.replayObserveCursor = 0;
    m_scriptState.replayStepCounter = 0;
    m_scriptState.replayDiverged = false;
    if (!enableReplay) {
        m_scriptState.replayTargetId.clear();
        m_scriptState.replayTargetSuspension = {};
        m_scriptState.replayActive = false;
        m_scriptState.replayFlags.reset();
        m_scriptState.replayNumbers.reset();
    } else {
        m_scriptState.replayActive =
            (m_scriptState.replayTargetSuspension.sequence != 0) ||
            !m_scriptState.replayTargetId.empty();
    }

    ResetDeterministicRng(m_scriptState.deterministicSeed);
    if (m_scriptState.deterministicCounter > 0) {
        std::uniform_real_distribution<double> warmup(0.0, 1.0);
        for (std::uint64_t i = 0; i < m_scriptState.deterministicCounter; ++i) {
            (void)warmup(m_deterministicRng);
        }
    }
}

ScriptDirector::SuspensionPoint ScriptDirector::RecordSuspension(WaitReason kind,
                                                                 const std::string& anchor) {
    SuspensionPoint point;
    point.sequence = ++m_scriptState.nextYieldSequence;
    point.kind = kind;
    point.anchor = anchor;
    point.choiceCursor = m_scriptState.choiceHistory.size();
    point.observeCursor = m_scriptState.observeHistory.size();

    const std::string occurrenceKey = std::string(WaitReasonToString(kind)) + "\n" + anchor;
    point.occurrence = ++m_scriptState.yieldAnchorOccurrences[occurrenceKey];

    m_scriptState.currentYieldId = anchor;
    m_scriptState.currentSuspension = point;
    return point;
}

bool ScriptDirector::HasStructuredReplayTarget() const {
    return m_scriptState.replayTargetSuspension.sequence != 0;
}

bool ScriptDirector::CurrentSuspensionMatchesReplayTarget() const {
    const SuspensionPoint& target = m_scriptState.replayTargetSuspension;
    const SuspensionPoint& current = m_scriptState.currentSuspension;

    if (target.sequence == 0) {
        return !m_scriptState.replayTargetId.empty() &&
               m_scriptState.currentYieldId == m_scriptState.replayTargetId;
    }

    return current.sequence == target.sequence &&
           current.kind == target.kind &&
           current.anchor == target.anchor &&
           current.occurrence == target.occurrence;
}

void ScriptDirector::CompleteReplayAtCurrentSuspension() {
    m_scriptState.replayActive = false;
    m_scriptState.replayStepCounter = 0;
}

const char* ScriptDirector::WaitReasonToString(WaitReason reason) {
    switch (reason) {
    case WaitReason::Dialogue: return "dialogue";
    case WaitReason::Choice: return "choice";
    case WaitReason::Observe: return "observe";
    case WaitReason::ActionQueue: return "action_queue";
    case WaitReason::Video: return "video";
    case WaitReason::None:
    default:
        return "none";
    }
}

ScriptDirector::WaitReason ScriptDirector::WaitReasonFromString(const std::string& raw) {
    if (raw == "dialogue") return WaitReason::Dialogue;
    if (raw == "choice") return WaitReason::Choice;
    if (raw == "observe") return WaitReason::Observe;
    if (raw == "action_queue") return WaitReason::ActionQueue;
    if (raw == "video") return WaitReason::Video;
    return WaitReason::None;
}

bool ScriptDirector::TriggerInteractionCallback(const std::string& callbackName,
                                                const std::string& characterTag,
                                                const std::string& partTag,
                                                const std::string& eventName) {
#if !defined(VN_ENGINE_HAS_LUA) || !VN_ENGINE_HAS_LUA
    (void)callbackName;
    (void)characterTag;
    (void)partTag;
    (void)eventName;
    return false;
#else
    if (!m_context || !m_context->IsBound() || !m_luaRuntime || !m_luaRuntime->state || callbackName.empty()) {
        return false;
    }

    if (m_luaRuntime->interactionCoroutine) {
        if (m_luaRuntime->interactionCoroutine->runnable()) {
            return false;
        }
        m_luaRuntime->interactionCoroutine.reset();
        m_luaRuntime->interactionThread.reset();
    }

    sol::thread runner = sol::thread::create(*m_luaRuntime->state);
    sol::protected_function cb = m_luaRuntime->m_sandbox[callbackName];
    if (!cb.valid()) {
        return false;
    }

    sol::coroutine interactionCo(cb);
    sol::protected_function_result result = interactionCo(characterTag, partTag, eventName);
    if (!result.valid()) {
        sol::error err = result;
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Sprite interaction callback '%s' failed: %s",
                    callbackName.c_str(),
                    err.what());
        return false;
    }

    if (interactionCo.runnable()) {
        m_luaRuntime->interactionThread = std::make_unique<sol::thread>(std::move(runner));
        m_luaRuntime->interactionCoroutine = std::make_unique<sol::coroutine>(std::move(interactionCo));
    }
    return true;
#endif
}

void ScriptDirector::Reset() {
    m_runtimeError.clear();
    m_scriptPath.clear();
    m_entryPoint = "main";
    m_luaRuntime.reset();
    m_scriptState = {};
    ResetDeterministicRng(1);
}

bool ScriptDirector::RegisterDeterministicLuaLibraries() {
#if !defined(VN_ENGINE_HAS_LUA) || !VN_ENGINE_HAS_LUA
    return false;
#else
    if (!m_luaRuntime || !m_luaRuntime->state) {
        return false;
    }

    sol::table mathTable = m_luaRuntime->m_sandbox["math"];
    mathTable.set_function("randomseed", [this](double seed) {
        const std::uint64_t deterministicSeed = static_cast<std::uint64_t>(std::llround(seed));
        ResetDeterministicRng(std::max<std::uint64_t>(1, deterministicSeed));
    });
    mathTable.set_function("random", [this](sol::this_state ts, sol::variadic_args va) {
        sol::state_view lua(ts);
        const std::size_t argc = std::distance(va.begin(), va.end());

        if (argc == 0) {
            return sol::make_object(lua, NextDeterministicRandom01());
        }

        double first = 0.0;
        auto it = va.begin();
        if (it != va.end()) {
            first = it->get<double>();
        }

        if (argc == 1) {
            const int upper = std::max(1, static_cast<int>(std::floor(first)));
            const int value = 1 + static_cast<int>(std::floor(NextDeterministicRandom01() * upper));
            return sol::make_object(lua, value);
        }

        ++it;
        const double second = (it != va.end()) ? it->get<double>() : first;
        int lower = static_cast<int>(std::floor(first));
        int upper = static_cast<int>(std::floor(second));
        if (lower > upper) {
            std::swap(lower, upper);
        }

        const int range = std::max(1, upper - lower + 1);
        const int value = lower + static_cast<int>(std::floor(NextDeterministicRandom01() * range));
        return sol::make_object(lua, value);
    });

    sol::table osTable = m_luaRuntime->state->create_table();
    osTable.set_function("time", [this]() {
        return NextDeterministicTimeTick();
    });
    osTable.set_function("clock", [this]() {
        return static_cast<double>(m_scriptState.deterministicCounter) / 1000.0;
    });
    m_luaRuntime->m_sandbox["os"] = osTable;
    return true;
#endif
}

bool ScriptDirector::RegisterVnApiBindings() {
#if !defined(VN_ENGINE_HAS_LUA) || !VN_ENGINE_HAS_LUA
    return false;
#else
    if (!m_context || !m_context->IsBound() || !m_luaRuntime || !m_luaRuntime->state) {
        return false;
    }

    const auto isReplayActive = [this]() {
        return m_context && m_context->IsReplayActive();
    };

    sol::table vnTable = m_luaRuntime->state->create_table();
    m_luaRuntime->m_sandbox["vn"] = vnTable;
    vnTable.set_function("t", [this](const std::string& key) {
        return m_context->Translate(key);
    });
    vnTable.set_function("set_rng_seed", [this, isReplayActive](std::uint64_t seed) {
        if (isReplayActive()) {
            return;
        }
        ResetDeterministicRng(seed);
    });
    vnTable.set_function("get_rng_seed", [this]() {
        return m_scriptState.deterministicSeed;
    });
    vnTable.set_function("load_bg", [this](const std::string& path, bool immediate) {
        m_context->LoadBG(path, immediate || m_context->IsReplayActive());
    });
    vnTable.set_function("cut_bg", [this](const std::string& path) {
        m_context->LoadBG(path, true);
    });
    vnTable.set_function("preload_image", [this, isReplayActive](const std::string& path) {
        if (isReplayActive()) {
            return false;
        }
        return m_context->PreloadImage(path);
    });
    vnTable.set_function("load_sprite", [this](const std::string& path, int slot, bool immediate) {
        m_context->LoadSpriteBySlot(path, slot, immediate || m_context->IsReplayActive());
    });
    vnTable.set_function("cut_sprite", [this](const std::string& path, int slot) {
        m_context->LoadSpriteBySlot(path, slot, true);
    });
    vnTable.set_function("set_sprite_transform",
                         [this](int slot,
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
                             m_context->SetSpriteTransform(slot,
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
                         });
    vnTable.set_function("clear_sprite_transform", [this](int slot) {
        m_context->ClearSpriteTransform(slot);
    });
    vnTable.set_function("set_sprite_clip", [this](int slot, float x, float y, float w, float h, bool enabled) {
        m_context->SetSpriteClipRect(slot, x, y, w, h, enabled);
    });
    vnTable.set_function("clear_sprite_clip", [this](int slot) {
        m_context->ClearSpriteClipRect(slot);
    });
    vnTable.set_function("set_letterbox", [this](float ratio, float duration, bool immediate) {
        m_context->SetLetterbox(ratio, duration, immediate);
    });
    vnTable.set_function("set_sprite_interaction",
                         [this, isReplayActive](int slot,
                                                const std::string& characterTag,
                                                const std::string& partTag,
                                                const std::string& callbackName) {
                             if (isReplayActive()) {
                                 return;
                             }
                             m_context->SetSpriteInteraction(slot, characterTag, partTag, callbackName);
                         });
    vnTable.set_function("clear_sprite_interaction", [this](int slot) {
        m_context->ClearSpriteInteraction(slot);
    });
    vnTable.set_function("play_bgm", [this, isReplayActive](const std::string& path) {
        if (isReplayActive()) {
            return;
        }
        m_context->PlayBGM(path);
    });
    vnTable.set_function("stop_bgm", [this, isReplayActive]() {
        if (isReplayActive()) {
            return;
        }
        m_context->StopBGM();
    });
    vnTable.set_function("play_sfx", [this, isReplayActive](const std::string& path) {
        if (isReplayActive()) {
            return;
        }
        m_context->PlaySFX(path);
    });
    vnTable.set_function("play_voice", [this, isReplayActive](const std::string& path) {
        if (isReplayActive()) {
            return;
        }
        m_context->PlayVoice(path);
    });
    vnTable.set_function("stop_voice", [this, isReplayActive]() {
        if (isReplayActive()) {
            return;
        }
        m_context->StopVoice();
    });
    vnTable.set_function("set_flag", [this](const std::string& name, bool value) {
        m_context->SetFlag(name, value);
    });
    vnTable.set_function("get_flag", [this](const std::string& name) {
        return m_context->GetFlag(name);
    });
    vnTable.set_function("set_num", [this](const std::string& name, double value) {
        m_context->SetNumber(name, value);
    });
    vnTable.set_function("get_num", [this](const std::string& name) {
        return m_context->GetNumber(name);
    });
    vnTable.set_function("set_chapter", [this, isReplayActive](const std::string& name) {
        if (isReplayActive()) {
            return;
        }
        m_context->SetChapterName(name);
    });
    vnTable.set_function("set_bg_transition", [this](const std::string& effect) {
        m_context->SetBackgroundTransitionEffect(effect);
    });
    vnTable.set_function("screen_shake", [this, isReplayActive](float intensity, float duration) {
        if (isReplayActive()) {
            return;
        }
        m_context->StartScreenShake(intensity, duration);
    });
    vnTable.set_function("begin_dialogue",
                         [this](const std::string& speaker,
                                const std::string& text,
                                const std::string& voicePath,
                                const std::string& lineId) {
                             return m_context->QueueLuaDialogue(speaker, text, voicePath, lineId);
                         });
    vnTable.set_function("begin_wait", [this](float seconds) {
        return m_context->QueueLuaWait(seconds);
    });
    vnTable.set_function("begin_parallel", [this](const sol::table& actions) {
        std::vector<ParallelActionSpec> specs;
        specs.reserve(actions.size());

        for (const auto& kv : actions) {
            if (!kv.second.is<sol::table>()) {
                continue;
            }

            sol::table action = kv.second.as<sol::table>();
            ParallelActionSpec spec;
            spec.kind = action.get_or("kind", std::string{});

            for (const auto& field : action) {
                if (!field.first.is<std::string>()) {
                    continue;
                }
                const std::string key = field.first.as<std::string>();
                if (key == "kind") {
                    continue;
                }

                if (field.second.is<bool>()) {
                    spec.boolArgs[key] = field.second.as<bool>();
                } else if (field.second.is<int>()) {
                    spec.numericArgs[key] = static_cast<double>(field.second.as<int>());
                } else if (field.second.is<double>()) {
                    spec.numericArgs[key] = field.second.as<double>();
                } else if (field.second.is<float>()) {
                    spec.numericArgs[key] = static_cast<double>(field.second.as<float>());
                } else if (field.second.is<std::string>()) {
                    spec.stringArgs[key] = field.second.as<std::string>();
                }
            }

            if (!spec.kind.empty()) {
                specs.push_back(std::move(spec));
            }
        }

        return m_context->QueueParallelActions(specs);
    });
    vnTable.set_function("begin_shake", [this](float intensity, float duration) {
        return m_context->QueueLuaShake(intensity, duration);
    });
    auto beginVideo = [this](const std::string& path,
                             sol::optional<std::string> videoId,
                             sol::optional<bool> skippable,
                             sol::optional<bool> audioEnabled) {
        return m_context->QueueLuaVideo(path,
                                        videoId.value_or(std::string{}),
                                        skippable.value_or(true),
                                        audioEnabled.value_or(true));
    };
    vnTable.set_function("begin_video", beginVideo);
    vnTable.set_function("play_video", beginVideo);
    vnTable.set_function("begin_choice",
                         [this](sol::this_state ts, const sol::table& options, const std::string& choiceId) {
                             sol::state_view lua(ts);
                             std::vector<std::string> labels;
                             labels.reserve(options.size());

                             for (const auto& kv : options) {
                                 if (kv.second.is<std::string>()) {
                                     labels.push_back(kv.second.as<std::string>());
                                 } else if (kv.second.is<sol::table>()) {
                                     sol::table entry = kv.second.as<sol::table>();
                                     labels.push_back(entry.get_or("label", entry.get_or(1, std::string{})));
                                 }
                             }

                             const auto [selected, shouldWait] = m_context->QueueLuaChoice(labels, choiceId);
                             if (shouldWait) {
                                 return std::make_tuple(sol::make_object(lua, 0), true);
                             }
                             return std::make_tuple(sol::make_object(lua, selected), false);
                         });
    vnTable.set_function("begin_observe",
                         [this](sol::this_state ts, const sol::table& config, const std::string& observeId) {
                             sol::state_view lua(ts);
                             std::vector<ObserveHotspot> hotspots;

                             const float panScale = config.get_or("pan_scale", 1.5f);
                             const bool allowCancel = config.get_or("allow_cancel", false);

                             const sol::object hotspotsObj = config["hotspots"];
                             if (hotspotsObj.valid() && hotspotsObj.is<sol::table>()) {
                                 sol::table hotspotsTable = hotspotsObj.as<sol::table>();
                                 hotspots.reserve(hotspotsTable.size());

                                 for (const auto& kv : hotspotsTable) {
                                     if (!kv.second.is<sol::table>()) {
                                         continue;
                                     }

                                     sol::table entry = kv.second.as<sol::table>();
                                     const std::string targetId = entry.get_or("id", entry.get_or("target", std::string{}));
                                     if (targetId.empty()) {
                                         continue;
                                     }

                                     hotspots.push_back({
                                         SDL_FRect{
                                             entry.get_or("x", 0.0f),
                                             entry.get_or("y", 0.0f),
                                             entry.get_or("w", 0.0f),
                                             entry.get_or("h", 0.0f)
                                         },
                                         targetId,
                                         entry.get_or("hover", entry.get_or("label", std::string{}))
                                     });
                                 }
                             }

                             const auto [selectedTarget, shouldWait] = m_context->QueueLuaObserve(std::move(hotspots),
                                                                                                     panScale,
                                                                                                     allowCancel,
                                                                                                     observeId);
                             if (shouldWait) {
                                 return std::make_tuple(sol::make_object(lua, std::string{}), true);
                             }
                             return std::make_tuple(sol::make_object(lua, selectedTarget), false);
                         });
    return true;
#endif
}

bool ScriptDirector::InitializeLuaState() {
#if !defined(VN_ENGINE_HAS_LUA) || !VN_ENGINE_HAS_LUA
    return false;
#else
    m_luaRuntime = std::make_unique<LuaRuntime>();
    m_luaRuntime->state = std::make_unique<sol::state>();
    m_luaRuntime->state->open_libraries(sol::lib::base,
                                        sol::lib::coroutine,
                                        sol::lib::math,
                                        sol::lib::string,
                                        sol::lib::table,
                                        sol::lib::utf8);

    m_luaRuntime->m_sandbox = sol::environment(*m_luaRuntime->state, sol::create);
    sol::state& luaState = *m_luaRuntime->state;

    constexpr const char* kAllowedGlobals[] = {
        "assert",
        "error",
        "ipairs",
        "next",
        "pairs",
        "pcall",
        "select",
        "tonumber",
        "tostring",
        "type",
        "unpack",
    };

    for (const char* name : kAllowedGlobals) {
        sol::object value = luaState[name];
        if (value.valid() && value.get_type() != sol::type::nil) {
            m_luaRuntime->m_sandbox[name] = value;
        }
    }

    constexpr const char* kAllowedModules[] = {
        "math",
        "string",
        "table",
        "coroutine",
        "utf8",
    };

    for (const char* name : kAllowedModules) {
        sol::object value = luaState[name];
        if (value.valid() && value.get_type() == sol::type::table) {
            m_luaRuntime->m_sandbox[name] = value;
        }
    }

    sol::table tableModule = m_luaRuntime->m_sandbox["table"];
    if (m_luaRuntime->m_sandbox["unpack"].get_type() == sol::type::nil) {
        sol::object unpack = tableModule["unpack"];
        if (unpack.valid() && unpack.get_type() != sol::type::nil) {
            m_luaRuntime->m_sandbox["unpack"] = unpack;
        }
    }

    m_luaRuntime->m_sandbox["load"] = sol::nil;
    m_luaRuntime->m_sandbox["loadfile"] = sol::nil;
    m_luaRuntime->m_sandbox["dofile"] = sol::nil;
    m_luaRuntime->m_sandbox["collectgarbage"] = sol::nil;
    m_luaRuntime->m_sandbox["require"] = sol::nil;
    m_luaRuntime->m_sandbox["debug"] = sol::nil;

    if (!RegisterDeterministicLuaLibraries()) {
        return false;
    }

    if (!RegisterVnApiBindings()) {
        return false;
    }
    const std::filesystem::path runtimePath = std::filesystem::path(m_scriptPath).parent_path() / "runtime.lua";
    if (std::filesystem::exists(runtimePath)) {
        return ExecuteLuaFile(*m_luaRuntime->state, m_luaRuntime->m_sandbox, runtimePath.string());
    }

    return true;
#endif
}

bool ScriptDirector::CreateLuaCoroutine() {
#if !defined(VN_ENGINE_HAS_LUA) || !VN_ENGINE_HAS_LUA
    return false;
#else
    if (!m_luaRuntime || !m_luaRuntime->state) {
        return false;
    }

    if (!ExecuteLuaFile(*m_luaRuntime->state, m_luaRuntime->m_sandbox, m_scriptPath)) {
        return false;
    }

    m_luaRuntime->mainThread = std::make_unique<sol::thread>(sol::thread::create(*m_luaRuntime->state));
    sol::protected_function entry = m_luaRuntime->m_sandbox[m_entryPoint];
    if (!entry.valid()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Lua entry point '%s' not found in %s",
                     m_entryPoint.c_str(),
                     m_scriptPath.c_str());
        return false;
    }

    m_luaRuntime->mainCoroutine = std::make_unique<sol::coroutine>(entry);
    return true;
#endif
}

bool ScriptDirector::ResumeImpl(const ResumeValue& value) {
#if !defined(VN_ENGINE_HAS_LUA) || !VN_ENGINE_HAS_LUA
    (void)value;
    return false;
#else
    if (!m_context || !m_context->IsBound() || !m_luaRuntime || !m_luaRuntime->mainCoroutine) {
        return false;
    }

    auto resumeCoroutine = [&](sol::coroutine& co, const ResumeValue& resumeValue) -> sol::protected_function_result {
        if (std::holds_alternative<int>(resumeValue)) {
            return co(std::get<int>(resumeValue));
        }
        if (std::holds_alternative<std::string>(resumeValue)) {
            return co(std::get<std::string>(resumeValue));
        }
        return co();
    };

    const bool hasExplicitResumeValue = !std::holds_alternative<std::monostate>(value);

    if (!hasExplicitResumeValue && m_luaRuntime->interactionCoroutine) {
        if (!m_luaRuntime->interactionCoroutine->runnable()) {
            m_luaRuntime->interactionCoroutine.reset();
            m_luaRuntime->interactionThread.reset();
        } else {
            lua_State* interactionState = m_luaRuntime->interactionThread ? m_luaRuntime->interactionThread->state() : nullptr;
            sol::protected_function_result interactionResult = vn::lua_safety::RunLuaWithInstructionBudget(
                interactionState,
                "Lua interaction coroutine",
                [&]() { return resumeCoroutine(*m_luaRuntime->interactionCoroutine, value); });
            if (!interactionResult.valid()) {
                sol::error err = interactionResult;
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "Lua interaction coroutine error: %s",
                            err.what());
                m_luaRuntime->interactionCoroutine.reset();
                m_luaRuntime->interactionThread.reset();
                return true;
            }

            if (!m_luaRuntime->interactionCoroutine->runnable()) {
                m_luaRuntime->interactionCoroutine.reset();
                m_luaRuntime->interactionThread.reset();
            } else {
                return true;
            }
        }
    }

    if (!m_luaRuntime->mainCoroutine->runnable()) {
        m_context->OnScriptCoroutineEnded();
        return false;
    }

    lua_State* mainState = m_luaRuntime->mainThread ? m_luaRuntime->mainThread->state() : nullptr;
    sol::protected_function_result result = vn::lua_safety::RunLuaWithInstructionBudget(
        mainState,
        "Lua main coroutine",
        [&]() { return resumeCoroutine(*m_luaRuntime->mainCoroutine, value); });

    if (!result.valid()) {
        sol::error err = result;
        m_runtimeError = err.what();
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Lua coroutine error: %s", m_runtimeError.c_str());
        m_context->OnScriptRuntimeError(m_runtimeError);
        return false;
    }

    if (!m_luaRuntime->mainCoroutine->runnable()) {
        m_context->OnScriptCoroutineEnded();
        return false;
    }

    return true;
#endif
}

void ScriptDirector::ResetDeterministicRng(std::optional<std::uint64_t> seed) {
    if (seed.has_value()) {
        m_scriptState.deterministicSeed = std::max<std::uint64_t>(1, *seed);
    } else {
        std::random_device rd;
        const std::uint64_t high = static_cast<std::uint64_t>(rd()) << 32;
        const std::uint64_t low = static_cast<std::uint64_t>(rd());
        m_scriptState.deterministicSeed = std::max<std::uint64_t>(1, high ^ low);
    }

    m_scriptState.deterministicCounter = 0;
    m_deterministicRng.seed(m_scriptState.deterministicSeed);
}

double ScriptDirector::NextDeterministicRandom01() {
    std::uniform_real_distribution<double> dist(0.0, 1.0);
    ++m_scriptState.deterministicCounter;
    return dist(m_deterministicRng);
}

std::int64_t ScriptDirector::NextDeterministicTimeTick() {
    return static_cast<std::int64_t>(m_scriptState.deterministicCounter);
}

} // namespace vn
