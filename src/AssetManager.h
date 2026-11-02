#pragma once

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <cstdint>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

#if defined(VN_ENGINE_HAS_SDL3_MIXER) && VN_ENGINE_HAS_SDL3_MIXER
#include <SDL3_mixer/SDL_mixer.h>
#endif

namespace vn {

// Asset manager with synchronous loads plus background image decode preloading.
class AssetManager {
public:
    struct TextureDeleter {
        void operator()(SDL_Texture* texture) const noexcept;
    };

    struct FontDeleter {
        void operator()(TTF_Font* font) const noexcept;
    };

    using TextureHandle = std::shared_ptr<SDL_Texture>;
    using FontHandle = std::shared_ptr<TTF_Font>;

#if defined(VN_ENGINE_HAS_SDL3_MIXER) && VN_ENGINE_HAS_SDL3_MIXER
    struct ChunkDeleter {
        void operator()(Mix_Chunk* chunk) const noexcept;
    };

    struct MusicDeleter {
        void operator()(Mix_Music* music) const noexcept;
    };

    using AudioChunkHandle = std::shared_ptr<Mix_Chunk>;
    using MusicHandle = std::shared_ptr<Mix_Music>;
#endif

    AssetManager() = default;
    explicit AssetManager(SDL_Renderer* renderer) noexcept;
    ~AssetManager();

    void SetRenderer(SDL_Renderer* renderer) noexcept;
    [[nodiscard]] SDL_Renderer* GetRenderer() const noexcept { return m_renderer; }

    // Load a texture from cache or disk.
    [[nodiscard]] TextureHandle GetTexture(const std::string& path);

    // Load a font (kept synchronous).
    [[nodiscard]] FontHandle GetFont(const std::string& path, float pointSize);

    // Cache key for rendered text textures.
    struct TextKey {
        std::string text;
        std::string fontPath;
        std::uintptr_t fontIdentity = 0;
        int pointSize = 0;
        int outlineThickness = 0;
        int wrapWidthPx = 0;
        SDL_Color color = { 0, 0, 0, 255 };

        [[nodiscard]] bool operator==(const TextKey& o) const noexcept {
            return fontIdentity == o.fontIdentity &&
                   pointSize == o.pointSize &&
                   outlineThickness == o.outlineThickness &&
                   wrapWidthPx == o.wrapWidthPx &&
                   text == o.text &&
                   fontPath == o.fontPath &&
                   color.r == o.color.r && color.g == o.color.g && 
                   color.b == o.color.b && color.a == o.color.a;
        }
    };

    struct TextKeyView {
        std::string_view text;
        std::string_view fontPath;
        std::uintptr_t fontIdentity = 0;
        int pointSize = 0;
        int outlineThickness = 0;
        int wrapWidthPx = 0;
        SDL_Color color = { 0, 0, 0, 255 };
    };

    struct TextKeyLess {
        using is_transparent = void;

        [[nodiscard]] bool operator()(const TextKey& lhs, const TextKey& rhs) const noexcept;
        [[nodiscard]] bool operator()(const TextKey& lhs, TextKeyView rhs) const noexcept;
        [[nodiscard]] bool operator()(TextKeyView lhs, const TextKey& rhs) const noexcept;
    };

    [[nodiscard]] TextureHandle GetTextTexture(std::string_view text, TTF_Font* font,
                                                const std::string& fontPath, int pointSize, 
                                                SDL_Color color,
                                                int outlineThickness = 0,
                                                float wrapWidth = 0.0f);

#if defined(VN_ENGINE_HAS_SDL3_MIXER) && VN_ENGINE_HAS_SDL3_MIXER
    [[nodiscard]] AudioChunkHandle GetAudioChunk(const std::string& path);
    [[nodiscard]] MusicHandle GetMusic(const std::string& path);
#endif

    // Queue a background image decode. ProcessAsyncLoads creates the SDL texture on the main thread.
    [[nodiscard]] bool PreloadAsync(const std::string& path);

    void ProcessAsyncLoads();

    [[nodiscard]] bool WaitForAsync(const std::string& path, int timeoutMs = 5000);

    [[nodiscard]] bool IsPendingAsync(const std::string& path) const;

    // Remove expired cached assets.
    void ClearUnused();

    // Clear all caches.
    void ClearAll(bool waitForPendingAsyncLoads = true);

    // Cancel queued background decodes and release decoded surfaces.
    void CancelPendingAsyncLoads(bool waitForCompletion = true);

    [[nodiscard]] const std::string& LastError() const noexcept { return m_lastError; }

private:
    enum class AsyncLoadState {
        Queued,
        Loading,
        Ready,
        Failed,
    };

    struct AsyncTextureLoad {
        std::string requestKey;
        std::string requestedPath;
        std::vector<std::string> candidates;
        std::string resolvedPath;
        std::string error;
        SDL_Surface* surface = nullptr;
        AsyncLoadState state = AsyncLoadState::Queued;
    };

    struct FontKey {
        std::string path;
        int pointSizeTimes100 = 0;

        [[nodiscard]] bool operator==(const FontKey& other) const noexcept {
            return pointSizeTimes100 == other.pointSizeTimes100 && path == other.path;
        }
    };

    struct FontKeyHasher {
        [[nodiscard]] std::size_t operator()(const FontKey& key) const noexcept;
    };

    struct CachedTextTexture {
        TextureHandle texture;
        std::uint64_t lastUsedTick = 0;
    };

    [[nodiscard]] static std::string NormalizeKey(const std::string& path);
    [[nodiscard]] static int QuantizePointSize(float pointSize) noexcept;

    // Fallback placeholder texture generation for missing files.
    [[nodiscard]] TextureHandle CreateSolidColorTexture(Uint8 r, Uint8 g, Uint8 b, int w = 1280, int h = 720);

    SDL_Renderer* m_renderer = nullptr;
    std::string m_lastError;

    // Texture caching - main thread only
    std::unordered_map<std::string, std::weak_ptr<SDL_Texture>> m_textureCache;
    std::unordered_map<std::string, TextureHandle> m_preloadedTextureCache;
    std::map<TextKey, CachedTextTexture, TextKeyLess> m_textCache;
    std::unordered_map<FontKey, std::weak_ptr<TTF_Font>, FontKeyHasher> m_fontCache;
    std::uint64_t m_textCacheTick = 0;

    mutable std::mutex m_asyncMutex;
    std::condition_variable m_asyncCv;
    std::thread m_asyncWorker;
    bool m_asyncStopRequested = false;
    std::deque<std::shared_ptr<AsyncTextureLoad>> m_asyncQueue;
    std::unordered_map<std::string, std::shared_ptr<AsyncTextureLoad>> m_asyncLoadsByKey;

#if defined(VN_ENGINE_HAS_SDL3_MIXER) && VN_ENGINE_HAS_SDL3_MIXER
    std::unordered_map<std::string, std::weak_ptr<Mix_Chunk>> m_chunkCache;
    std::unordered_map<std::string, std::weak_ptr<Mix_Music>> m_musicCache;
#endif

    [[nodiscard]] TextureHandle SurfaceToTexture(SDL_Surface* surface, const std::string& path);
    [[nodiscard]] TextureHandle SurfaceToTextureWithKey(SDL_Surface* surface, const std::string& path, const std::string& key);
    void EnsureAsyncWorkerLocked();
    void AsyncWorkerLoop();
    void ReleaseAsyncLoadSurface(const std::shared_ptr<AsyncTextureLoad>& load) noexcept;
    void TrimTextCache();

    static constexpr std::size_t kTextCacheMaxEntries = 8192;
    static constexpr std::uint64_t kTextCacheIdleTicks = 4096;
};

} // namespace vn
