#pragma once

#include <SDL3/SDL_log.h>

#include <string>
#include <unordered_map>

#if defined(VN_ENGINE_HAS_LUA) && VN_ENGINE_HAS_LUA
#include <sol/sol.hpp>
#endif

class I18nManager {
public:
#if defined(VN_ENGINE_HAS_LUA) && VN_ENGINE_HAS_LUA
    bool Init(sol::state_view lua, const std::string& localeFile) {
        m_strings.clear();
        m_currentLocale = localeFile;

        sol::load_result loaded = lua.load_file(localeFile);
        if (!loaded.valid()) {
            sol::error err = loaded;
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Failed to load locale %s: %s",
                         localeFile.c_str(),
                         err.what());
            return false;
        }

        sol::protected_function chunk = loaded;
        sol::protected_function_result result = chunk();
        if (!result.valid()) {
            sol::error err = result;
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Failed to execute locale %s: %s",
                         localeFile.c_str(),
                         err.what());
            return false;
        }

        if (result.get_type() != sol::type::table) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "Locale file %s did not return a table",
                         localeFile.c_str());
            return false;
        }

        sol::table locale = result;
        for (const auto& kv : locale) {
            if (kv.first.is<std::string>() && kv.second.is<std::string>()) {
                m_strings[kv.first.as<std::string>()] = kv.second.as<std::string>();
            }
        }

        return true;
    }
#else
    bool Init(void*, const std::string&) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Lua i18n is unavailable in this build");
        m_strings.clear();
        m_currentLocale.clear();
        return false;
    }
#endif

    [[nodiscard]] std::string Get(const std::string& key) const {
        const auto it = m_strings.find(key);
        if (it != m_strings.end()) {
            return it->second;
        }
        return "[" + key + "]";
    }

    [[nodiscard]] std::string GetOrDefault(const std::string& key,
                                           const std::string& fallback) const {
        const auto it = m_strings.find(key);
        if (it != m_strings.end()) {
            return it->second;
        }
        return fallback;
    }

    [[nodiscard]] const std::string& CurrentLocale() const {
        return m_currentLocale;
    }

private:
    std::string m_currentLocale;
    std::unordered_map<std::string, std::string> m_strings;
};
