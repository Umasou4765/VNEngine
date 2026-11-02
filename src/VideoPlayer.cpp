#include "VideoPlayer.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_loadso.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <sstream>
#include <utility>
#include <vector>

namespace vn {

namespace {

struct mpv_handle;
struct mpv_render_context;

enum mpv_event_id {
    MPV_EVENT_NONE = 0,
    MPV_EVENT_SHUTDOWN = 1,
    MPV_EVENT_LOG_MESSAGE = 2,
    MPV_EVENT_COMMAND_REPLY = 5,
    MPV_EVENT_START_FILE = 6,
    MPV_EVENT_END_FILE = 7,
    MPV_EVENT_FILE_LOADED = 8,
    MPV_EVENT_VIDEO_RECONFIG = 17,
};

enum mpv_end_file_reason {
    MPV_END_FILE_REASON_EOF = 0,
    MPV_END_FILE_REASON_STOP = 2,
    MPV_END_FILE_REASON_QUIT = 3,
    MPV_END_FILE_REASON_ERROR = 4,
    MPV_END_FILE_REASON_REDIRECT = 5,
};

enum mpv_render_param_type {
    MPV_RENDER_PARAM_INVALID = 0,
    MPV_RENDER_PARAM_API_TYPE = 1,
    MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME = 12,
    MPV_RENDER_PARAM_SW_SIZE = 17,
    MPV_RENDER_PARAM_SW_FORMAT = 18,
    MPV_RENDER_PARAM_SW_STRIDE = 19,
    MPV_RENDER_PARAM_SW_POINTER = 20,
};

struct mpv_event_end_file {
    mpv_end_file_reason reason;
    int error;
    std::int64_t playlist_entry_id;
    std::int64_t playlist_insert_id;
    int playlist_insert_num_entries;
};

struct mpv_event {
    mpv_event_id event_id;
    int error;
    std::uint64_t reply_userdata;
    void* data;
};

struct mpv_render_param {
    mpv_render_param_type type;
    void* data;
};

constexpr const char* kMpvRenderApiSoftware = "sw";
constexpr std::uint64_t kMpvRenderUpdateFrame = 1u << 0;
constexpr int kBytesPerPixel = 4;
constexpr std::size_t kFrameAlignment = 64;

[[nodiscard]] std::size_t AlignUp(std::size_t value, std::size_t alignment) noexcept {
    return ((value + alignment - 1) / alignment) * alignment;
}

[[nodiscard]] bool LooksLikeUrl(const std::string& path) {
    return path.find("://") != std::string::npos;
}

[[nodiscard]] std::string ResolvePathForMpv(const std::string& path) {
    if (path.empty() || LooksLikeUrl(path)) {
        return path;
    }

    std::error_code ec;
    const std::filesystem::path fsPath(path);
    const std::filesystem::path absolute = fsPath.is_relative()
        ? std::filesystem::absolute(fsPath, ec)
        : fsPath;
    if (!ec) {
        return absolute.string();
    }
    return path;
}

[[nodiscard]] std::vector<std::string> CandidateLibraryPaths() {
#if defined(_WIN32)
    return {
        "libmpv-2.dll",
        "mpv-2.dll",
        "libmpv.dll",
        "mpv-v0.41.0-x86_64-pc-windows-msvc/libmpv-2.dll",
        "mpv-v0.41.0-x86_64-pc-windows-msvc/mpv-2.dll",
        "mpv-v0.41.0-x86_64-pc-windows-msvc/libmpv.dll",
    };
#elif defined(__APPLE__)
    return {
        "libmpv.2.dylib",
        "libmpv.dylib",
    };
#else
    return {
        "libmpv.so.2",
        "libmpv.so",
    };
#endif
}

} // namespace

struct VideoPlayer::MpvRuntime {
    using CreateFn = mpv_handle* (*)();
    using InitializeFn = int (*)(mpv_handle*);
    using TerminateDestroyFn = void (*)(mpv_handle*);
    using SetOptionStringFn = int (*)(mpv_handle*, const char*, const char*);
    using CommandFn = int (*)(mpv_handle*, const char**);
    using WaitEventFn = mpv_event* (*)(mpv_handle*, double);
    using ErrorStringFn = const char* (*)(int);
    using RenderContextCreateFn = int (*)(mpv_render_context**, mpv_handle*, mpv_render_param*);
    using RenderContextSetUpdateCallbackFn = void (*)(mpv_render_context*, void (*)(void*), void*);
    using RenderContextUpdateFn = std::uint64_t (*)(mpv_render_context*);
    using RenderContextRenderFn = int (*)(mpv_render_context*, mpv_render_param*);
    using RenderContextFreeFn = void (*)(mpv_render_context*);

    SDL_SharedObject* library = nullptr;
    mpv_handle* handle = nullptr;
    mpv_render_context* renderContext = nullptr;
    std::string libraryPath;

    CreateFn mpv_create = nullptr;
    InitializeFn mpv_initialize = nullptr;
    TerminateDestroyFn mpv_terminate_destroy = nullptr;
    SetOptionStringFn mpv_set_option_string = nullptr;
    CommandFn mpv_command = nullptr;
    WaitEventFn mpv_wait_event = nullptr;
    ErrorStringFn mpv_error_string = nullptr;
    RenderContextCreateFn mpv_render_context_create = nullptr;
    RenderContextSetUpdateCallbackFn mpv_render_context_set_update_callback = nullptr;
    RenderContextUpdateFn mpv_render_context_update = nullptr;
    RenderContextRenderFn mpv_render_context_render = nullptr;
    RenderContextFreeFn mpv_render_context_free = nullptr;

    ~MpvRuntime() {
        if (renderContext) {
            mpv_render_context_free(renderContext);
            renderContext = nullptr;
        }
        if (handle) {
            mpv_terminate_destroy(handle);
            handle = nullptr;
        }
        if (library) {
            SDL_UnloadObject(library);
            library = nullptr;
        }
    }

    template <typename Fn>
    bool LoadSymbol(Fn& target, const char* name, std::string& error) {
        SDL_FunctionPointer symbol = SDL_LoadFunction(library, name);
        if (!symbol) {
            error = std::string("libmpv missing symbol '") + name + "': " + SDL_GetError();
            return false;
        }
        target = reinterpret_cast<Fn>(symbol);
        return true;
    }

    [[nodiscard]] const char* ErrorString(int error) const {
        if (mpv_error_string) {
            return mpv_error_string(error);
        }
        return "unknown libmpv error";
    }
};

VideoPlayer::VideoPlayer() = default;

VideoPlayer::~VideoPlayer() {
    Stop();
    DestroyTexture();
    m_mpv.reset();
}

void VideoPlayer::Init(SDL_Renderer* renderer, int width, int height) {
    m_renderer = renderer;
    if (m_width != width || m_height != height) {
        Stop();
        DestroyTexture();
        m_width = width;
        m_height = height;
    }
}

bool VideoPlayer::Play(const std::string& path, const Options& options) {
    Stop();

    m_options = options;
    m_currentPath = path;
    m_lastError.clear();
    m_finished = false;
    m_hasFrame = false;
    m_renderUpdateRequested.store(false);

    if (path.empty()) {
        SetError("Video path is empty");
        return false;
    }
    if (LooksLikeUrl(path)) {
        SetError("Remote video URLs are disabled for security reasons.");
        return false;
    }
    if (!m_renderer || m_width <= 0 || m_height <= 0) {
        SetError("VideoPlayer is not initialized with a renderer and viewport size");
        return false;
    }
    if (!EnsureRuntimeLoaded()) {
        return false;
    }
    if (!EnsureTexture()) {
        return false;
    }
    if (!CreateMpvHandle(path)) {
        DestroyMpvHandle();
        return false;
    }

    m_playing = true;
    m_renderUpdateRequested.store(true);
    return true;
}

void VideoPlayer::Stop() {
    DestroyMpvHandle();
    m_playing = false;
    m_finished = false;
    m_renderUpdateRequested.store(false);
}

void VideoPlayer::Skip() {
    if (!m_playing && !m_mpv) {
        return;
    }
    DestroyMpvHandle();
    m_playing = false;
    m_finished = true;
    m_renderUpdateRequested.store(false);
}

void VideoPlayer::Update(float dt) {
    (void)dt;

    if (!m_mpv || !m_mpv->handle) {
        return;
    }

    DrainEvents();
    if (m_finished || !m_mpv || !m_mpv->renderContext) {
        return;
    }

    bool shouldRender = m_renderUpdateRequested.exchange(false);
    if (!m_hasFrame) {
        shouldRender = true;
    }
    if (!shouldRender) {
        return;
    }

    const std::uint64_t flags = m_mpv->mpv_render_context_update(m_mpv->renderContext);
    if ((flags & kMpvRenderUpdateFrame) || !m_hasFrame) {
        RenderFrameToTexture();
    }
}

void VideoPlayer::Render(SDL_Renderer* renderer) const {
    if (!renderer) {
        return;
    }

    if (!m_texture || !m_hasFrame) {
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        const SDL_FRect fill{ 0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height) };
        SDL_RenderFillRect(renderer, &fill);
        return;
    }

    const SDL_FRect dst{ 0.0f, 0.0f, static_cast<float>(m_width), static_cast<float>(m_height) };
    SDL_RenderTexture(renderer, m_texture, nullptr, &dst);
}

bool VideoPlayer::EnsureRuntimeLoaded() {
    if (m_mpv && m_mpv->library) {
        return true;
    }

    auto runtime = std::make_unique<MpvRuntime>();
    std::ostringstream attempted;

    for (const std::string& candidate : CandidateLibraryPaths()) {
        const bool explicitRelativePath =
            candidate.find('/') != std::string::npos || candidate.find('\\') != std::string::npos;
        if (explicitRelativePath && !std::filesystem::exists(candidate)) {
            continue;
        }

        runtime->library = SDL_LoadObject(candidate.c_str());
        if (runtime->library) {
            runtime->libraryPath = candidate;
            break;
        }

        if (attempted.tellp() > 0) {
            attempted << "; ";
        }
        attempted << candidate << " (" << SDL_GetError() << ")";
    }

    if (!runtime->library) {
        const std::string suffix = attempted.tellp() > 0
            ? std::string(": ") + attempted.str()
            : std::string{};
        SetError("libmpv runtime not found; expected mpv-2.dll/libmpv in PATH or project mpv folder" + suffix);
        return false;
    }

    std::string error;
    if (!runtime->LoadSymbol(runtime->mpv_create, "mpv_create", error) ||
        !runtime->LoadSymbol(runtime->mpv_initialize, "mpv_initialize", error) ||
        !runtime->LoadSymbol(runtime->mpv_terminate_destroy, "mpv_terminate_destroy", error) ||
        !runtime->LoadSymbol(runtime->mpv_set_option_string, "mpv_set_option_string", error) ||
        !runtime->LoadSymbol(runtime->mpv_command, "mpv_command", error) ||
        !runtime->LoadSymbol(runtime->mpv_wait_event, "mpv_wait_event", error) ||
        !runtime->LoadSymbol(runtime->mpv_error_string, "mpv_error_string", error) ||
        !runtime->LoadSymbol(runtime->mpv_render_context_create, "mpv_render_context_create", error) ||
        !runtime->LoadSymbol(runtime->mpv_render_context_set_update_callback, "mpv_render_context_set_update_callback", error) ||
        !runtime->LoadSymbol(runtime->mpv_render_context_update, "mpv_render_context_update", error) ||
        !runtime->LoadSymbol(runtime->mpv_render_context_render, "mpv_render_context_render", error) ||
        !runtime->LoadSymbol(runtime->mpv_render_context_free, "mpv_render_context_free", error)) {
        SetError(error);
        return false;
    }

    SDL_Log("Loaded libmpv runtime from %s", runtime->libraryPath.c_str());
    m_mpv = std::move(runtime);
    return true;
}

bool VideoPlayer::EnsureTexture() {
    if (m_texture && m_pixels && m_width > 0 && m_height > 0) {
        return true;
    }

    DestroyTexture();

    m_stride = AlignUp(static_cast<std::size_t>(m_width) * kBytesPerPixel, kFrameAlignment);
    m_pixelBufferSize = AlignUp(m_stride * static_cast<std::size_t>(m_height), kFrameAlignment);
    m_pixels.reset(static_cast<std::uint8_t*>(SDL_aligned_alloc(kFrameAlignment, m_pixelBufferSize)));
    if (!m_pixels) {
        SetError("Failed to allocate software video frame buffer");
        return false;
    }

    std::fill_n(m_pixels.get(), m_pixelBufferSize, std::uint8_t{ 0 });

    m_texture = SDL_CreateTexture(
        m_renderer,
        SDL_PIXELFORMAT_XRGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        m_width,
        m_height
    );
    if (!m_texture) {
        SetError(std::string("SDL_CreateTexture failed for video: ") + SDL_GetError());
        m_pixels.reset();
        return false;
    }

    SDL_SetTextureScaleMode(m_texture, SDL_SCALEMODE_LINEAR);
    return true;
}

bool VideoPlayer::CreateMpvHandle(const std::string& path) {
    if (!m_mpv) {
        SetError("libmpv runtime is not loaded");
        return false;
    }

    m_mpv->handle = m_mpv->mpv_create();
    if (!m_mpv->handle) {
        SetError("mpv_create failed");
        return false;
    }

    auto setOption = [&](const char* name, const char* value) {
        const int result = m_mpv->mpv_set_option_string(m_mpv->handle, name, value);
        if (result < 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "libmpv option %s=%s failed: %s",
                        name,
                        value,
                        m_mpv->ErrorString(result));
        }
    };

    setOption("terminal", "no");
    setOption("config", "no");
    setOption("input-default-bindings", "no");
    setOption("input-vo-keyboard", "no");
    setOption("osc", "no");
    setOption("osd-level", "0");
    setOption("force-window", "no");
    setOption("keep-open", "no");
    setOption("idle", "yes");
    setOption("sw-fast", "yes");
    setOption("video-timing-offset", "0");

    if (!m_options.audioEnabled) {
        setOption("audio", "no");
    }

    int result = m_mpv->mpv_initialize(m_mpv->handle);
    if (result < 0) {
        SetError(std::string("mpv_initialize failed: ") + m_mpv->ErrorString(result));
        return false;
    }

    char apiType[] = "sw";
    std::array<mpv_render_param, 2> initParams{ {
        { MPV_RENDER_PARAM_API_TYPE, apiType },
        { MPV_RENDER_PARAM_INVALID, nullptr },
    } };

    result = m_mpv->mpv_render_context_create(&m_mpv->renderContext, m_mpv->handle, initParams.data());
    if (result < 0 || !m_mpv->renderContext) {
        SetError(std::string("mpv_render_context_create(sw) failed: ") + m_mpv->ErrorString(result));
        return false;
    }

    m_mpv->mpv_render_context_set_update_callback(m_mpv->renderContext, &VideoPlayer::OnRenderUpdate, this);

    const std::string resolvedPath = ResolvePathForMpv(path);
    const char* command[] = { "loadfile", resolvedPath.c_str(), "replace", nullptr };
    result = m_mpv->mpv_command(m_mpv->handle, command);
    if (result < 0) {
        SetError(std::string("mpv loadfile failed for '") + path + "': " + m_mpv->ErrorString(result));
        return false;
    }

    return true;
}

void VideoPlayer::DestroyMpvHandle() {
    if (!m_mpv) {
        return;
    }

    if (m_mpv->renderContext) {
        m_mpv->mpv_render_context_set_update_callback(m_mpv->renderContext, nullptr, nullptr);
        m_mpv->mpv_render_context_free(m_mpv->renderContext);
        m_mpv->renderContext = nullptr;
    }

    if (m_mpv->handle) {
        m_mpv->mpv_terminate_destroy(m_mpv->handle);
        m_mpv->handle = nullptr;
    }
}

void VideoPlayer::DestroyTexture() {
    if (m_texture) {
        SDL_DestroyTexture(m_texture);
        m_texture = nullptr;
    }
    m_pixels.reset();
    m_pixelBufferSize = 0;
    m_stride = 0;
    m_hasFrame = false;
}

void VideoPlayer::DrainEvents() {
    if (!m_mpv || !m_mpv->handle) {
        return;
    }

    for (;;) {
        mpv_event* event = m_mpv->mpv_wait_event(m_mpv->handle, 0.0);
        if (!event || event->event_id == MPV_EVENT_NONE) {
            break;
        }

        switch (event->event_id) {
        case MPV_EVENT_END_FILE: {
            auto* end = static_cast<mpv_event_end_file*>(event->data);
            if (end && end->reason == MPV_END_FILE_REASON_REDIRECT) {
                break;
            }
            if (end && end->reason == MPV_END_FILE_REASON_ERROR) {
                const char* detail = m_mpv->ErrorString(end->error);
                SetError(std::string("Video playback ended with error: ") + detail);
            }
            m_playing = false;
            m_finished = true;
            break;
        }
        case MPV_EVENT_SHUTDOWN:
            m_playing = false;
            m_finished = true;
            break;
        default:
            break;
        }
    }
}

void VideoPlayer::RenderFrameToTexture() {
    if (!m_mpv || !m_mpv->renderContext || !m_texture || !m_pixels || m_width <= 0 || m_height <= 0) {
        return;
    }

    int size[] = { m_width, m_height };
    char format[] = "bgr0";
    std::size_t stride = m_stride;
    int blockForTargetTime = 0;

    std::array<mpv_render_param, 6> params{ {
        { MPV_RENDER_PARAM_SW_SIZE, size },
        { MPV_RENDER_PARAM_SW_FORMAT, format },
        { MPV_RENDER_PARAM_SW_STRIDE, &stride },
        { MPV_RENDER_PARAM_SW_POINTER, m_pixels.get() },
        { MPV_RENDER_PARAM_BLOCK_FOR_TARGET_TIME, &blockForTargetTime },
        { MPV_RENDER_PARAM_INVALID, nullptr },
    } };

    const int result = m_mpv->mpv_render_context_render(m_mpv->renderContext, params.data());
    if (result < 0) {
        SetError(std::string("mpv_render_context_render failed: ") + m_mpv->ErrorString(result));
        m_playing = false;
        m_finished = true;
        return;
    }

    if (!SDL_UpdateTexture(m_texture, nullptr, m_pixels.get(), static_cast<int>(m_stride))) {
        SetError(std::string("SDL_UpdateTexture failed for video: ") + SDL_GetError());
        m_playing = false;
        m_finished = true;
        return;
    }
    m_hasFrame = true;
}

void VideoPlayer::SetError(const std::string& message) {
    m_lastError = message;
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "%s", message.c_str());
}

void VideoPlayer::OnRenderUpdate(void* userdata) {
    auto* player = static_cast<VideoPlayer*>(userdata);
    if (player) {
        player->m_renderUpdateRequested.store(true);
    }
}

} // namespace vn
