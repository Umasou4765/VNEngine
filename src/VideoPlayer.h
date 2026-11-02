#pragma once

#include <SDL3/SDL.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace vn {

class VideoPlayer {
public:
    struct Options {
        bool audioEnabled = true;
        bool skippable = true;
    };

    VideoPlayer();
    ~VideoPlayer();

    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;

    void Init(SDL_Renderer* renderer, int width, int height);

    [[nodiscard]] bool Play(const std::string& path, const Options& options = {});
    void Stop();
    void Skip();

    void Update(float dt);
    void Render(SDL_Renderer* renderer) const;

    [[nodiscard]] bool IsPlaying() const noexcept { return m_playing; }
    [[nodiscard]] bool IsFinished() const noexcept { return m_finished; }
    [[nodiscard]] bool IsSkippable() const noexcept { return m_options.skippable; }
    [[nodiscard]] const std::string& LastError() const noexcept { return m_lastError; }

private:
    struct MpvRuntime;

    struct AlignedBufferDeleter {
        void operator()(std::uint8_t* ptr) const noexcept {
            SDL_aligned_free(ptr);
        }
    };

    using PixelBuffer = std::unique_ptr<std::uint8_t, AlignedBufferDeleter>;

    [[nodiscard]] bool EnsureRuntimeLoaded();
    [[nodiscard]] bool EnsureTexture();
    [[nodiscard]] bool CreateMpvHandle(const std::string& path);
    void DestroyMpvHandle();
    void DestroyTexture();
    void DrainEvents();
    void RenderFrameToTexture();
    void SetError(const std::string& message);

    static void OnRenderUpdate(void* userdata);

    std::unique_ptr<MpvRuntime> m_mpv;
    SDL_Renderer* m_renderer = nullptr;
    SDL_Texture* m_texture = nullptr;
    PixelBuffer m_pixels;
    std::size_t m_pixelBufferSize = 0;
    std::size_t m_stride = 0;
    int m_width = 0;
    int m_height = 0;

    Options m_options{};
    std::string m_lastError;
    std::string m_currentPath;

    std::atomic_bool m_renderUpdateRequested{ false };
    bool m_playing = false;
    bool m_finished = false;
    bool m_hasFrame = false;
};

} // namespace vn
