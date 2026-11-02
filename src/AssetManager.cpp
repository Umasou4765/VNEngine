#include "AssetManager.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

#include "Profiling.h"

namespace vn {

namespace {

void HashCombine(std::size_t& seed, std::size_t value) {
    seed ^= value + 0x9e3779b9u + (seed << 6) + (seed >> 2);
}

template <typename Cache>
void EraseExpired(Cache& cache) {
    for (auto it = cache.begin(); it != cache.end();) {
        if (it->second.expired()) {
            it = cache.erase(it);
        } else {
            ++it;
        }
    }
}

void PushUnique(std::vector<std::string>& out, const std::string& value) {
    if (value.empty()) return;
    if (std::find(out.begin(), out.end(), value) == out.end()) {
        out.push_back(value);
    }
}

[[nodiscard]] std::filesystem::path PathFromUtf8(const char* utf8Path) {
#if defined(_WIN32)
    return std::filesystem::u8path(utf8Path ? utf8Path : ".");
#else
    return std::filesystem::path(utf8Path ? utf8Path : ".");
#endif
}

[[nodiscard]] std::vector<std::string> BuildImageCandidates(const std::string& path) {
    std::vector<std::string> out;
    if (path.empty()) return out;

    std::string raw = path;
    if (raw.size() >= 2 && raw.front() == '"' && raw.back() == '"') {
        raw = raw.substr(1, raw.size() - 2);
    }

    std::filesystem::path p(raw);
    std::vector<std::filesystem::path> roots{ std::filesystem::path{}, std::filesystem::current_path() };
    if (const char* base = SDL_GetBasePath()) {
        roots.push_back(PathFromUtf8(base));
    }

    auto pushWithExtensions = [&](const std::filesystem::path& candidate) {
        std::filesystem::path normalized = candidate.lexically_normal();
        PushUnique(out, normalized.generic_string());

        if (!normalized.has_extension()) {
            PushUnique(out, (normalized.string() + ".png"));
            PushUnique(out, (normalized.string() + ".jpg"));
            PushUnique(out, (normalized.string() + ".webp"));
        }
    };

    if (p.is_absolute()) {
        pushWithExtensions(p);
    } else {
        for (const auto& root : roots) {
            pushWithExtensions(root / p);
        }
    }

    return out;
}

template <typename Handle, typename Cache, typename Key>
[[nodiscard]] Handle LockCached(const Cache& cache, const Key& key) {
    const auto it = cache.find(key);
    if (it == cache.end()) return {};
    return it->second.lock();
}

}  // namespace

// ============================================================================
// Deleters
// ============================================================================

void AssetManager::TextureDeleter::operator()(SDL_Texture* texture) const noexcept {
    if (texture) SDL_DestroyTexture(texture);
}

void AssetManager::FontDeleter::operator()(TTF_Font* font) const noexcept {
    if (!font) return;

    // A late-held shared_ptr can otherwise trip FreeType during app shutdown.
    if (TTF_WasInit() > 0) {
        TTF_CloseFont(font);
    }
}

#if defined(VN_ENGINE_HAS_SDL3_MIXER) && VN_ENGINE_HAS_SDL3_MIXER

void AssetManager::ChunkDeleter::operator()(Mix_Chunk* chunk) const noexcept {
    if (chunk) Mix_FreeChunk(chunk);
}

void AssetManager::MusicDeleter::operator()(Mix_Music* music) const noexcept {
    if (music) Mix_FreeMusic(music);
}

#endif

// ============================================================================
// Constructor / Initialization
// ============================================================================

AssetManager::AssetManager(::SDL_Renderer* renderer) noexcept
    : m_renderer(renderer) {}

AssetManager::~AssetManager() {
    CancelPendingAsyncLoads(true);
}

void AssetManager::SetRenderer(::SDL_Renderer* renderer) noexcept {
    if (m_renderer == renderer) return;
    CancelPendingAsyncLoads(true);
    m_renderer = renderer;
    m_textureCache.clear();
    m_preloadedTextureCache.clear();
    m_textCache.clear();
}

bool AssetManager::PreloadAsync(const std::string& path) {
    const std::string requestKey = NormalizeKey(path);
    if (requestKey.empty()) return false;

    if (LockCached<TextureHandle>(m_textureCache, requestKey)) {
        return true;
    }
    if (m_preloadedTextureCache.find(requestKey) != m_preloadedTextureCache.end()) {
        return true;
    }

    std::vector<std::string> candidates = BuildImageCandidates(path);
    if (candidates.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_asyncMutex);
    if (m_asyncLoadsByKey.find(requestKey) != m_asyncLoadsByKey.end()) {
        return true;
    }

    EnsureAsyncWorkerLocked();

    auto load = std::make_shared<AsyncTextureLoad>();
    load->requestKey = requestKey;
    load->requestedPath = path;
    load->candidates = std::move(candidates);
    m_asyncLoadsByKey[requestKey] = load;
    m_asyncQueue.push_back(load);
    m_asyncCv.notify_one();
    return true;
}

void AssetManager::ProcessAsyncLoads() {
    std::vector<std::shared_ptr<AsyncTextureLoad>> completed;

    {
        std::lock_guard<std::mutex> lock(m_asyncMutex);
        for (auto it = m_asyncLoadsByKey.begin(); it != m_asyncLoadsByKey.end();) {
            const auto& load = it->second;
            if (load->state == AsyncLoadState::Ready || load->state == AsyncLoadState::Failed) {
                completed.push_back(load);
                it = m_asyncLoadsByKey.erase(it);
            } else {
                ++it;
            }
        }
    }

    for (const auto& load : completed) {
        if (load->state == AsyncLoadState::Ready && load->surface) {
            TextureHandle texture = SurfaceToTextureWithKey(load->surface, load->resolvedPath, load->requestKey);
            if (texture && !load->resolvedPath.empty()) {
                const std::string resolvedKey = NormalizeKey(load->resolvedPath);
                m_textureCache[resolvedKey] = texture;
                m_preloadedTextureCache[load->requestKey] = texture;
                m_preloadedTextureCache[resolvedKey] = texture;
            }
        } else if (!load->error.empty()) {
            m_lastError = load->error;
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", m_lastError.c_str());
        }

        ReleaseAsyncLoadSurface(load);
    }
}

// Convert SDL_Surface to SDL_Texture.
// MAIN THREAD ONLY.
// Creates texture from a preloaded surface and stores it in cache.
AssetManager::TextureHandle AssetManager::SurfaceToTexture(
    SDL_Surface* surface,
    const std::string& path) {
    return SurfaceToTextureWithKey(surface, path, NormalizeKey(path));
}

AssetManager::TextureHandle AssetManager::SurfaceToTextureWithKey(
    SDL_Surface* surface,
    const std::string& path,
    const std::string& key) {
    if (!surface || !m_renderer) {
        return {};
    }

    if (key.empty()) {
        return {};
    }

    if (TextureHandle cached = LockCached<TextureHandle>(m_textureCache, key)) {
        return cached;
    }

    TextureHandle texture(SDL_CreateTextureFromSurface(m_renderer, surface), TextureDeleter{});
    if (!texture) {
        m_lastError = "failed to create texture from surface: " + path;
        return {};
    }

    m_textureCache[key] = texture;

    return texture;
}

// Texture load with cache and synchronous fallback.
AssetManager::TextureHandle AssetManager::GetTexture(const std::string& path) {
    m_lastError.clear();

    if (!m_renderer) {
        m_lastError = "AssetManager requires a valid SDL_Renderer before loading textures";
        return {};
    }

    const std::string requestKey = NormalizeKey(path);
    if (requestKey.empty()) {
        m_lastError = "texture path is empty";
        return {};
    }

    // Check cache first
    if (TextureHandle cached = LockCached<TextureHandle>(m_textureCache, requestKey)) {
        return cached;
    }
    if (auto it = m_preloadedTextureCache.find(requestKey); it != m_preloadedTextureCache.end()) {
        return it->second;
    }

    // Try with image extension variants
    for (const std::string& candidate : BuildImageCandidates(path)) {
        const std::string candidateKey = NormalizeKey(candidate);
        
        if (TextureHandle cached = LockCached<TextureHandle>(m_textureCache, candidateKey)) {
            m_textureCache[requestKey] = cached;
            return cached;
        }
        if (auto it = m_preloadedTextureCache.find(candidateKey); it != m_preloadedTextureCache.end()) {
            m_textureCache[requestKey] = it->second;
            m_preloadedTextureCache[requestKey] = it->second;
            return it->second;
        }

        // Load synchronously as fallback
        SDL_Surface* surface = IMG_Load(candidate.c_str());
        if (!surface) {
            continue;
        }

        TextureHandle texture(SDL_CreateTextureFromSurface(m_renderer, surface), TextureDeleter{});
        SDL_DestroySurface(surface);

        if (!texture) {
            m_lastError = "failed to create texture: " + candidate + " (" + SDL_GetError() + ")";
            return {};
        }

        m_textureCache[requestKey] = texture;
        m_textureCache[candidateKey] = texture;
        return texture;
    }

    // All load attempts failed; create a fallback placeholder.
    const std::string sdlError = SDL_GetError();
    m_lastError = "texture not found: " + path + " | SDL_Error: " + sdlError;
    if (sdlError.find("Unsupported image format") != std::string::npos) {
        m_lastError += " | hint: ensure SDL3_image.dll and codec DLLs (e.g. libpng/libwebp) are beside VNEngine.exe";
    }
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", m_lastError.c_str());
    
    // Pick placeholder color by asset type.
    Uint8 r = 40, g = 40, b = 40;  // Default dark gray.
    if (path.find("bg") != std::string::npos || path.find("background") != std::string::npos) {
        r = 20; g = 20; b = 40;  // Dark blue for backgrounds.
    } else if (path.find("sprite") != std::string::npos || path.find("character") != std::string::npos) {
        r = 60; g = 20; b = 60;  // Dark magenta for sprites.
    }
    
    TextureHandle placeholder = CreateSolidColorTexture(r, g, b);
    m_textureCache[requestKey] = placeholder;
    return placeholder;
}

bool AssetManager::WaitForAsync(const std::string& path, int timeoutMs) {
    const std::string requestKey = NormalizeKey(path);
    if (requestKey.empty()) {
        return false;
    }

    if (LockCached<TextureHandle>(m_textureCache, requestKey)) {
        return true;
    }
    if (m_preloadedTextureCache.find(requestKey) != m_preloadedTextureCache.end()) {
        return true;
    }

    if (!IsPendingAsync(path)) {
        if (!PreloadAsync(path)) {
            return false;
        }
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(std::max(0, timeoutMs));
    do {
        ProcessAsyncLoads();
        if (LockCached<TextureHandle>(m_textureCache, requestKey)) {
            return true;
        }
        if (m_preloadedTextureCache.find(requestKey) != m_preloadedTextureCache.end()) {
            return true;
        }
        if (!IsPendingAsync(path)) {
            break;
        }
        SDL_Delay(1);
    } while (std::chrono::steady_clock::now() < deadline);

    ProcessAsyncLoads();
    return static_cast<bool>(LockCached<TextureHandle>(m_textureCache, requestKey)) ||
           m_preloadedTextureCache.find(requestKey) != m_preloadedTextureCache.end();
}

bool AssetManager::IsPendingAsync(const std::string& path) const {
    const std::string requestKey = NormalizeKey(path);
    if (requestKey.empty()) {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_asyncMutex);
    return m_asyncLoadsByKey.find(requestKey) != m_asyncLoadsByKey.end();
}

// ============================================================================
// Font Loading
// ============================================================================

AssetManager::FontHandle AssetManager::GetFont(const std::string& path, float pointSize) {
    m_lastError.clear();

    const FontKey key{ NormalizeKey(path), QuantizePointSize(pointSize) };
    if (key.path.empty()) {
        m_lastError = "font path is empty";
        return {};
    }

    if (FontHandle cached = LockCached<FontHandle>(m_fontCache, key)) {
        return cached;
    }

    FontHandle font(TTF_OpenFont(path.c_str(), pointSize), FontDeleter{});
    if (!font) {
        m_lastError = "failed to open font: " + path + " (" + SDL_GetError() + ")";
        return {};
    }

    m_fontCache[key] = font;
    return font;
}

// ============================================================================
// Text Texture Caching
// ============================================================================

namespace {

[[nodiscard]] int CompareTextKeyParts(AssetManager::TextKeyView lhs,
                                      AssetManager::TextKeyView rhs) noexcept {
    if (lhs.fontIdentity != rhs.fontIdentity) return lhs.fontIdentity < rhs.fontIdentity ? -1 : 1;
    if (lhs.pointSize != rhs.pointSize) return lhs.pointSize < rhs.pointSize ? -1 : 1;
    if (lhs.outlineThickness != rhs.outlineThickness) return lhs.outlineThickness < rhs.outlineThickness ? -1 : 1;
    if (lhs.wrapWidthPx != rhs.wrapWidthPx) return lhs.wrapWidthPx < rhs.wrapWidthPx ? -1 : 1;
    if (lhs.color.r != rhs.color.r) return lhs.color.r < rhs.color.r ? -1 : 1;
    if (lhs.color.g != rhs.color.g) return lhs.color.g < rhs.color.g ? -1 : 1;
    if (lhs.color.b != rhs.color.b) return lhs.color.b < rhs.color.b ? -1 : 1;
    if (lhs.color.a != rhs.color.a) return lhs.color.a < rhs.color.a ? -1 : 1;
    if (lhs.fontPath != rhs.fontPath) return lhs.fontPath < rhs.fontPath ? -1 : 1;
    if (lhs.text != rhs.text) return lhs.text < rhs.text ? -1 : 1;
    return 0;
}

[[nodiscard]] AssetManager::TextKeyView ToTextKeyView(const AssetManager::TextKey& key) noexcept {
    return AssetManager::TextKeyView{
        key.text,
        key.fontPath,
        key.fontIdentity,
        key.pointSize,
        key.outlineThickness,
        key.wrapWidthPx,
        key.color
    };
}

} // namespace

bool AssetManager::TextKeyLess::operator()(const TextKey& lhs, const TextKey& rhs) const noexcept {
    return CompareTextKeyParts(ToTextKeyView(lhs), ToTextKeyView(rhs)) < 0;
}

bool AssetManager::TextKeyLess::operator()(const TextKey& lhs, TextKeyView rhs) const noexcept {
    return CompareTextKeyParts(ToTextKeyView(lhs), rhs) < 0;
}

bool AssetManager::TextKeyLess::operator()(TextKeyView lhs, const TextKey& rhs) const noexcept {
    return CompareTextKeyParts(lhs, ToTextKeyView(rhs)) < 0;
}

AssetManager::TextureHandle AssetManager::GetTextTexture(
    std::string_view text,
    TTF_Font* font,
    const std::string& fontPath,
    int pointSize,
    SDL_Color color,
    int outlineThickness,
    float wrapWidth) {
    ZoneScoped;
    m_lastError.clear();

    if (text.empty() || !font || !m_renderer) return {};
    outlineThickness = std::max(0, outlineThickness);

    const int wrapWidthPx = std::max(0, static_cast<int>(std::lround(wrapWidth)));
    const TextKeyView lookupKey{
        text,
        fontPath,
        reinterpret_cast<std::uintptr_t>(font),
        pointSize,
        outlineThickness,
        wrapWidthPx,
        color
    };

    ++m_textCacheTick;

    if (auto it = m_textCache.find(lookupKey); it != m_textCache.end()) {
        it->second.lastUsedTick = m_textCacheTick;
        return it->second.texture;
    }

    TextKey key{
        std::string(text),
        fontPath,
        lookupKey.fontIdentity,
        pointSize,
        outlineThickness,
        wrapWidthPx,
        color
    };

    const int previousOutline = TTF_GetFontOutline(font);

    SDL_Surface* outlineSurf = nullptr;
    if (outlineThickness > 0) {
        (void)TTF_SetFontOutline(font, outlineThickness);
        const SDL_Color outlineColor{ 0, 0, 0, color.a };
        if (wrapWidthPx > 0) {
            outlineSurf = TTF_RenderText_Blended_Wrapped(font, key.text.c_str(), 0, outlineColor, static_cast<float>(wrapWidthPx));
        } else {
            outlineSurf = TTF_RenderText_Blended(font, key.text.c_str(), 0, outlineColor);
        }
        (void)TTF_SetFontOutline(font, previousOutline);
    }

    SDL_Surface* mainSurf = nullptr;
    if (wrapWidthPx > 0) {
        mainSurf = TTF_RenderText_Blended_Wrapped(font, key.text.c_str(), 0, color, static_cast<float>(wrapWidthPx));
    } else {
        mainSurf = TTF_RenderText_Blended(font, key.text.c_str(), 0, color);
    }
    if (!mainSurf && !outlineSurf) {
        m_lastError = "failed to render text: " + key.text + " (" + SDL_GetError() + ")";
        return {};
    }

    SDL_Surface* finalSurf = nullptr;
    if (outlineSurf && mainSurf) {
        finalSurf = SDL_CreateSurface(outlineSurf->w, outlineSurf->h, SDL_PIXELFORMAT_RGBA32);
        if (!finalSurf) {
            SDL_DestroySurface(outlineSurf);
            SDL_DestroySurface(mainSurf);
            m_lastError = "failed to create text surface: " + key.text + " (" + SDL_GetError() + ")";
            return {};
        }

        // Ensure transparent pixels are initialized before alpha blits.
        SDL_FillSurfaceRect(finalSurf, nullptr, 0x00000000u);

        SDL_Rect dstOutline{ 0, 0, outlineSurf->w, outlineSurf->h };
        SDL_BlitSurface(outlineSurf, nullptr, finalSurf, &dstOutline);

        const int centeredX = (outlineSurf->w - mainSurf->w) / 2;
        const int centeredY = (outlineSurf->h - mainSurf->h) / 2;
        SDL_Rect dstMain{ centeredX, centeredY, mainSurf->w, mainSurf->h };
        SDL_BlitSurface(mainSurf, nullptr, finalSurf, &dstMain);

        SDL_DestroySurface(outlineSurf);
        SDL_DestroySurface(mainSurf);
    } else if (mainSurf) {
        finalSurf = mainSurf;
    } else {
        finalSurf = outlineSurf;
    }

    TextureHandle texture(SDL_CreateTextureFromSurface(m_renderer, finalSurf), TextureDeleter{});
    SDL_DestroySurface(finalSurf);

    if (!texture) {
        m_lastError = "failed to create texture from text: " + key.text + " (" + SDL_GetError() + ")";
        return {};
    }

    SDL_SetTextureBlendMode(texture.get(), SDL_BLENDMODE_BLEND);
    SDL_SetTextureColorMod(texture.get(), 255, 255, 255);
    SDL_SetTextureAlphaMod(texture.get(), 255);

    m_textCache.emplace(std::move(key), CachedTextTexture{ texture, m_textCacheTick });
    TrimTextCache();
    return texture;
}

#if defined(VN_ENGINE_HAS_SDL3_MIXER) && VN_ENGINE_HAS_SDL3_MIXER

AssetManager::AudioChunkHandle AssetManager::GetAudioChunk(const std::string& path) {
    m_lastError.clear();

    const std::string key = NormalizeKey(path);
    if (key.empty()) {
        m_lastError = "audio chunk path is empty";
        return {};
    }

    if (AudioChunkHandle cached = LockCached<AudioChunkHandle>(m_chunkCache, key)) {
        return cached;
    }

    AudioChunkHandle chunk(Mix_LoadWAV(path.c_str()), ChunkDeleter{});
    if (!chunk) {
        m_lastError = "failed to load chunk: " + path;
        return {};
    }

    m_chunkCache[key] = chunk;
    return chunk;
}

AssetManager::MusicHandle AssetManager::GetMusic(const std::string& path) {
    m_lastError.clear();

    const std::string key = NormalizeKey(path);
    if (key.empty()) {
        m_lastError = "music path is empty";
        return {};
    }

    if (MusicHandle cached = LockCached<MusicHandle>(m_musicCache, key)) {
        return cached;
    }

    MusicHandle music(Mix_LoadMUS(path.c_str()), MusicDeleter{});
    if (!music) {
        m_lastError = "failed to load music: " + path;
        return {};
    }

    m_musicCache[key] = music;
    return music;
}

#endif

// ============================================================================
// Cache Management
// ============================================================================

void AssetManager::ClearUnused() {
    EraseExpired(m_textureCache);
    EraseExpired(m_fontCache);
    TrimTextCache();

#if defined(VN_ENGINE_HAS_SDL3_MIXER) && VN_ENGINE_HAS_SDL3_MIXER
    EraseExpired(m_chunkCache);
    EraseExpired(m_musicCache);
#endif

}

void AssetManager::ClearAll(bool waitForPendingAsyncLoads) {
    CancelPendingAsyncLoads(waitForPendingAsyncLoads);

    m_textureCache.clear();
    m_preloadedTextureCache.clear();
    m_fontCache.clear();
    m_textCache.clear();
    m_textCacheTick = 0;

#if defined(VN_ENGINE_HAS_SDL3_MIXER) && VN_ENGINE_HAS_SDL3_MIXER
    m_chunkCache.clear();
    m_musicCache.clear();
#endif
}

void AssetManager::TrimTextCache() {
    if (m_textCache.empty()) return;

    for (auto it = m_textCache.begin(); it != m_textCache.end();) {
        if (!it->second.texture || (m_textCacheTick > it->second.lastUsedTick &&
            (m_textCacheTick - it->second.lastUsedTick) > kTextCacheIdleTicks)) {
            it = m_textCache.erase(it);
        } else {
            ++it;
        }
    }

    while (m_textCache.size() > kTextCacheMaxEntries) {
        auto lruIt = m_textCache.begin();
        for (auto it = m_textCache.begin(); it != m_textCache.end(); ++it) {
            if (it->second.lastUsedTick < lruIt->second.lastUsedTick) {
                lruIt = it;
            }
        }
        m_textCache.erase(lruIt);
    }
}

void AssetManager::CancelPendingAsyncLoads(bool waitForCompletion) {
    {
        std::lock_guard<std::mutex> lock(m_asyncMutex);
        m_asyncStopRequested = true;
        if (!waitForCompletion) {
            m_asyncQueue.clear();
        }
    }
    m_asyncCv.notify_all();

    if (m_asyncWorker.joinable()) {
        m_asyncWorker.join();
    }

    std::lock_guard<std::mutex> lock(m_asyncMutex);
    for (const auto& kv : m_asyncLoadsByKey) {
        ReleaseAsyncLoadSurface(kv.second);
    }
    m_asyncLoadsByKey.clear();
    m_asyncQueue.clear();
    m_asyncStopRequested = false;
}

// ============================================================================
// Helpers
// ============================================================================

std::string AssetManager::NormalizeKey(const std::string& path) {
    if (path.empty()) return {};

    std::filesystem::path fsPath(path);
    fsPath = fsPath.lexically_normal();
    const std::string normalized = fsPath.generic_string();
    return normalized.empty() ? path : normalized;
}

int AssetManager::QuantizePointSize(float pointSize) noexcept {
    return static_cast<int>(std::lround(pointSize * 100.0f));
}

std::size_t AssetManager::FontKeyHasher::operator()(const FontKey& key) const noexcept {
    const std::size_t pathHash = std::hash<std::string>{}(key.path);
    const std::size_t sizeHash = std::hash<int>{}(key.pointSizeTimes100);
    return pathHash ^ (sizeHash + 0x9e3779b9u + (pathHash << 6) + (pathHash >> 2));
}

// Create a solid color placeholder texture for missing assets.
AssetManager::TextureHandle AssetManager::CreateSolidColorTexture(Uint8 r, Uint8 g, Uint8 b, int w, int h) {
    if (!m_renderer) return {};

    SDL_Surface* surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_ARGB8888);
    if (!surface) return {};

    Uint32 color = SDL_MapSurfaceRGB(surface, r, g, b);
    SDL_FillSurfaceRect(surface, nullptr, color);

    TextureHandle texture(SDL_CreateTextureFromSurface(m_renderer, surface), TextureDeleter{});
    SDL_DestroySurface(surface);
    
    return texture;
}

void AssetManager::EnsureAsyncWorkerLocked() {
    if (m_asyncWorker.joinable()) {
        return;
    }

    m_asyncStopRequested = false;
    m_asyncWorker = std::thread(&AssetManager::AsyncWorkerLoop, this);
}

void AssetManager::AsyncWorkerLoop() {
    for (;;) {
        std::shared_ptr<AsyncTextureLoad> load;

        {
            std::unique_lock<std::mutex> lock(m_asyncMutex);
            m_asyncCv.wait(lock, [this]() {
                return m_asyncStopRequested || !m_asyncQueue.empty();
            });

            if (m_asyncStopRequested && m_asyncQueue.empty()) {
                return;
            }

            load = m_asyncQueue.front();
            m_asyncQueue.pop_front();
            load->state = AsyncLoadState::Loading;
        }

        SDL_Surface* surface = nullptr;
        std::string resolvedPath;
        std::string lastError;

        for (const std::string& candidate : load->candidates) {
            surface = IMG_Load(candidate.c_str());
            if (surface) {
                resolvedPath = candidate;
                break;
            }
            lastError = SDL_GetError();
        }

        {
            std::lock_guard<std::mutex> lock(m_asyncMutex);
            if (surface) {
                load->surface = surface;
                load->resolvedPath = std::move(resolvedPath);
                load->state = AsyncLoadState::Ready;
            } else {
                load->error = "async texture preload failed: " + load->requestedPath;
                if (!lastError.empty()) {
                    load->error += " (" + lastError + ")";
                }
                load->state = AsyncLoadState::Failed;
            }
        }
    }
}

void AssetManager::ReleaseAsyncLoadSurface(const std::shared_ptr<AsyncTextureLoad>& load) noexcept {
    if (load && load->surface) {
        SDL_DestroySurface(load->surface);
        load->surface = nullptr;
    }
}

}  // namespace vn
