#include "LuaRuntimeSafety.h"

#include <algorithm>
#include <system_error>

#include <SDL3/SDL.h>

namespace vn::lua_safety {

std::filesystem::path PathFromUtf8(const char* utf8Path) {
#if defined(_WIN32)
    return std::filesystem::u8path(utf8Path ? utf8Path : ".");
#else
    return std::filesystem::path(utf8Path ? utf8Path : ".");
#endif
}

std::vector<std::filesystem::path> BuildTrustedScriptRoots() {
    std::vector<std::filesystem::path> roots;
    roots.push_back(std::filesystem::absolute(std::filesystem::path("assets") / "scripts"));

    if (const char* basePath = SDL_GetBasePath()) {
        roots.push_back(std::filesystem::absolute(PathFromUtf8(basePath) / "assets" / "scripts"));
    }

    std::sort(roots.begin(), roots.end());
    roots.erase(std::unique(roots.begin(), roots.end()), roots.end());
    return roots;
}

bool TryNormalizeTrustedScriptPath(const std::filesystem::path& requestedPath,
                                   std::filesystem::path& outNormalizedPath) {
    std::error_code ec;
    const std::filesystem::path normalizedRequested = std::filesystem::weakly_canonical(requestedPath, ec);
    if (ec) {
        return false;
    }

    for (const auto& root : BuildTrustedScriptRoots()) {
        ec.clear();
        const std::filesystem::path normalizedRoot = std::filesystem::weakly_canonical(root, ec);
        if (ec) {
            continue;
        }

        ec.clear();
        const std::filesystem::path relative = std::filesystem::relative(normalizedRequested, normalizedRoot, ec);
        const std::string relativeStr = relative.generic_string();
        if (ec || relative.empty() || relative.is_absolute() ||
            relativeStr == ".." || relativeStr.rfind("../", 0) == 0) {
            continue;
        }

        outNormalizedPath = normalizedRequested;
        return true;
    }

    return false;
}

} // namespace vn::lua_safety
