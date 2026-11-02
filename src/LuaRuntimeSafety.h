#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#if defined(VN_ENGINE_HAS_LUA) && VN_ENGINE_HAS_LUA
#include <algorithm>
#include <lua.hpp>
#endif

namespace vn::lua_safety {

[[nodiscard]] std::filesystem::path PathFromUtf8(const char* utf8Path);
[[nodiscard]] std::vector<std::filesystem::path> BuildTrustedScriptRoots();
[[nodiscard]] bool TryNormalizeTrustedScriptPath(const std::filesystem::path& requestedPath,
                                                 std::filesystem::path& outNormalizedPath);

#if defined(VN_ENGINE_HAS_LUA) && VN_ENGINE_HAS_LUA

inline constexpr int kLuaInstructionBudgetHookCount = 2000;
inline constexpr std::size_t kDefaultLuaInstructionBudgetHookEvents = 500;
inline constexpr std::size_t kSaveFileLuaInstructionBudgetHookEvents = 200;

class LuaInstructionBudgetGuard {
public:
    LuaInstructionBudgetGuard(lua_State* state,
                              const char* context,
                              std::size_t maxHookEvents = kDefaultLuaInstructionBudgetHookEvents)
        : m_state(state),
          m_context(context),
          m_remainingHookEvents(maxHookEvents) {
        if (!m_state) {
            return;
        }

        m_previousHook = lua_gethook(m_state);
        m_previousMask = lua_gethookmask(m_state);
        m_previousCount = lua_gethookcount(m_state);
        ActiveGuards().push_back(this);
        lua_sethook(m_state, &LuaInstructionBudgetGuard::HookThunk, LUA_MASKCOUNT, kLuaInstructionBudgetHookCount);
    }

    LuaInstructionBudgetGuard(const LuaInstructionBudgetGuard&) = delete;
    LuaInstructionBudgetGuard& operator=(const LuaInstructionBudgetGuard&) = delete;

    ~LuaInstructionBudgetGuard() {
        Deactivate();
    }

private:
    static std::vector<LuaInstructionBudgetGuard*>& ActiveGuards() {
        static thread_local std::vector<LuaInstructionBudgetGuard*> guards;
        return guards;
    }

    static LuaInstructionBudgetGuard* FindActiveGuard(lua_State* state) {
        auto& guards = ActiveGuards();
        for (auto it = guards.rbegin(); it != guards.rend(); ++it) {
            if ((*it)->m_state == state) {
                return *it;
            }
        }
        return nullptr;
    }

    static void HookThunk(lua_State* state, lua_Debug*) {
        if (LuaInstructionBudgetGuard* guard = FindActiveGuard(state)) {
            guard->OnHook();
        }
    }

    void OnHook() {
        if (m_remainingHookEvents == 0) {
            luaL_error(m_state,
                       "Lua instruction budget exceeded while executing %s",
                       m_context ? m_context : "Lua chunk");
        }

        --m_remainingHookEvents;
    }

    void Deactivate() {
        if (!m_state) {
            return;
        }

        lua_sethook(m_state, m_previousHook, m_previousMask, m_previousCount);

        auto& guards = ActiveGuards();
        auto it = std::find(guards.begin(), guards.end(), this);
        if (it != guards.end()) {
            guards.erase(it);
        }

        m_state = nullptr;
    }

    lua_State* m_state = nullptr;
    const char* m_context = nullptr;
    std::size_t m_remainingHookEvents = kDefaultLuaInstructionBudgetHookEvents;
    lua_Hook m_previousHook = nullptr;
    int m_previousMask = 0;
    int m_previousCount = 0;
};

template <typename Fn>
auto RunLuaWithInstructionBudget(lua_State* state,
                                 const char* context,
                                 Fn&& fn,
                                 std::size_t maxHookEvents = kDefaultLuaInstructionBudgetHookEvents) {
    LuaInstructionBudgetGuard guard(state, context, maxHookEvents);
    return std::forward<Fn>(fn)();
}

#endif

} // namespace vn::lua_safety
