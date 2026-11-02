#include "AudioManager.h"

#include <algorithm>

namespace vn {

AudioManager::~AudioManager() {
    Shutdown();
}

bool AudioManager::Init() {
    m_lastError.clear();

#if defined(VN_ENGINE_HAS_SDL3_MIXER) && VN_ENGINE_HAS_SDL3_MIXER
    if (m_initialized) return true;

    if (!Mix_OpenAudio(0, nullptr)) {
        m_lastError = "Mix_OpenAudio failed";
        return false;
    }

    const int allocatedChannels = Mix_AllocateChannels(kTotalChannels);
    if (allocatedChannels < kTotalChannels) {
        m_lastError = "Mix_AllocateChannels failed";
        Mix_CloseAudio();
        return false;
    }

    // Keep channels 0 and 1 out of auto-allocation to avoid voice clobbering.
    Mix_ReserveChannels(2);

    SetBGMVolume(m_bgmVolume);
    SetSFXVolume(m_sfxVolume);
    SetVoiceVolume(m_voiceVolume);

    m_initialized = true;
    return true;
#else
    m_lastError = "SDL3_mixer not available at build time";
    return false;
#endif
}

void AudioManager::Shutdown() {
#if defined(VN_ENGINE_HAS_SDL3_MIXER) && VN_ENGINE_HAS_SDL3_MIXER
    if (!m_initialized) return;

    StopVoice(0);
    StopBGM(0);
    StopAllSFX();

    m_activeVoiceChunk.reset();
    for (auto& chunk : m_activeSfxChunksByChannel) {
        chunk.reset();
    }
    m_activeMusic.reset();
    m_channelRefPruneTimer = 0.0f;
    if (m_assets) m_assets->ClearUnused();

    Mix_CloseAudio();
    m_initialized = false;
#endif
}

bool AudioManager::PlayBGM(const std::string& path, int loops, int fadeInMs) {
#if defined(VN_ENGINE_HAS_SDL3_MIXER) && VN_ENGINE_HAS_SDL3_MIXER
    if (!m_initialized) {
        m_lastError = "AudioManager not initialized";
        return false;
    }

    if (m_currentBgmPath == path && Mix_PlayingMusic()) {
        return true;
    }

    AssetManager::MusicHandle music = GetOrLoadMusic(path);
    if (!music) return false;

    m_activeMusic = music;
    m_currentBgmPath = path;
    SetBGMVolume(m_bgmVolume);
    const bool ok = (fadeInMs > 0)
        ? Mix_FadeInMusic(music.get(), loops, fadeInMs)
        : Mix_PlayMusic(music.get(), loops);

    if (!ok) {
        m_lastError = "failed to play BGM: " + path;
        return false;
    }

    return true;
#else
    (void)path;
    (void)loops;
    (void)fadeInMs;
    m_lastError = "SDL3_mixer not available at build time";
    return false;
#endif
}

void AudioManager::StopBGM(int fadeOutMs) {
#if defined(VN_ENGINE_HAS_SDL3_MIXER) && VN_ENGINE_HAS_SDL3_MIXER
    if (!m_initialized) return;

    m_currentBgmPath.clear();
    if (fadeOutMs > 0) {
        Mix_FadeOutMusic(fadeOutMs);
    } else {
        Mix_HaltMusic();
    }
#else
    (void)fadeOutMs;
#endif
}

bool AudioManager::PlaySFX(const std::string& path) {
#if defined(VN_ENGINE_HAS_SDL3_MIXER) && VN_ENGINE_HAS_SDL3_MIXER
    if (!m_initialized) {
        m_lastError = "AudioManager not initialized";
        return false;
    }

    AssetManager::AudioChunkHandle chunk = GetOrLoadChunk(path);
    if (!chunk) return false;

    PruneFinishedChannelRefs();

    int channel = -1;
    for (int c = 2; c < kTotalChannels; ++c) {
        if (!Mix_Playing(c)) {
            channel = c;
            break;
        }
    }

    if (channel < 0) {
        m_lastError = "failed to play SFX: " + path;
        return false;
    }

    Mix_Volume(channel, ToMixerVolume(m_sfxVolume));
    if (!Mix_PlayChannel(channel, chunk.get(), 0)) {
        m_lastError = "failed to play SFX: " + path;
        return false;
    }

    // CRITICAL: Store strong reference to prevent Use-After-Free
    // The audio thread will keep playing from the raw pointer we passed to Mix_PlayChannel
    // We must maintain a shared_ptr in the corresponding slot until the channel finishes playing
    if (channel < kTotalChannels) {
        m_activeSfxChunksByChannel[channel] = chunk;
    }

    return true;
#else
    (void)path;
    m_lastError = "SDL3_mixer not available at build time";
    return false;
#endif
}

bool AudioManager::PlayVoice(const std::string& path) {
#if defined(VN_ENGINE_HAS_SDL3_MIXER) && VN_ENGINE_HAS_SDL3_MIXER
    if (!m_initialized) {
        m_lastError = "AudioManager not initialized";
        return false;
    }

    StopVoice(0);

    AssetManager::AudioChunkHandle chunk = GetOrLoadChunk(path);
    if (!chunk) return false;

    m_activeVoiceChunk = chunk;
    Mix_Volume(kVoiceChannel, ToMixerVolume(m_voiceVolume));
    if (!Mix_PlayChannel(kVoiceChannel, chunk.get(), 0)) {
        m_activeVoiceChunk.reset();
        m_lastError = "failed to play voice: " + path;
        return false;
    }

    return true;
#else
    (void)path;
    m_lastError = "SDL3_mixer not available at build time";
    return false;
#endif
}

void AudioManager::StopVoice(int fadeOutMs) {
#if defined(VN_ENGINE_HAS_SDL3_MIXER) && VN_ENGINE_HAS_SDL3_MIXER
    if (!m_initialized) return;

    if (fadeOutMs > 0) Mix_FadeOutChannel(kVoiceChannel, fadeOutMs);
    else Mix_HaltChannel(kVoiceChannel);
    PruneFinishedChannelRefs();
#else
    (void)fadeOutMs;
#endif
}

void AudioManager::StopAllSFX() {
#if defined(VN_ENGINE_HAS_SDL3_MIXER) && VN_ENGINE_HAS_SDL3_MIXER
    if (!m_initialized) return;
    for (int channel = 0; channel < kTotalChannels; ++channel) {
        if (channel == kVoiceChannel) continue;
        Mix_HaltChannel(channel);
    }
    PruneFinishedChannelRefs();
#endif
}

void AudioManager::Tick(float dt) {
#if defined(VN_ENGINE_HAS_SDL3_MIXER) && VN_ENGINE_HAS_SDL3_MIXER
    if (!m_initialized) return;

    m_channelRefPruneTimer += std::max(0.0f, dt);
    if (m_channelRefPruneTimer >= 1.0f) {
        PruneFinishedChannelRefs();
        m_channelRefPruneTimer = 0.0f;
    }
#else
    (void)dt;
#endif
}

void AudioManager::OnDialogueAdvance(bool allowBleed) {
    // If voice bleed is enabled and allowed, don't stop the voice - let it continue
    if (m_voiceBleedEnabled && allowBleed) {
        return;
    }
    // Otherwise, hard stop the voice to avoid overlap and UAF
    StopVoice(0);
#if defined(VN_ENGINE_HAS_SDL3_MIXER) && VN_ENGINE_HAS_SDL3_MIXER
    PruneFinishedChannelRefs();
#endif
}

void AudioManager::SetBGMVolume(float normalized01) {
    m_bgmVolume = std::clamp(normalized01, 0.0f, 1.0f);
#if defined(VN_ENGINE_HAS_SDL3_MIXER) && VN_ENGINE_HAS_SDL3_MIXER
    if (m_initialized) Mix_VolumeMusic(ToMixerVolume(m_bgmVolume));
#endif
}

void AudioManager::SetSFXVolume(float normalized01) {
    m_sfxVolume = std::clamp(normalized01, 0.0f, 1.0f);
#if defined(VN_ENGINE_HAS_SDL3_MIXER) && VN_ENGINE_HAS_SDL3_MIXER
    if (m_initialized) {
        const int volume = ToMixerVolume(m_sfxVolume);
        for (int channel = 0; channel < kTotalChannels; ++channel) {
            if (channel == kVoiceChannel) continue;
            Mix_Volume(channel, volume);
        }
    }
#endif
}

void AudioManager::SetVoiceVolume(float normalized01) {
    m_voiceVolume = std::clamp(normalized01, 0.0f, 1.0f);
#if defined(VN_ENGINE_HAS_SDL3_MIXER) && VN_ENGINE_HAS_SDL3_MIXER
    if (m_initialized) Mix_Volume(kVoiceChannel, ToMixerVolume(m_voiceVolume));
#endif
}

#if defined(VN_ENGINE_HAS_SDL3_MIXER) && VN_ENGINE_HAS_SDL3_MIXER

AssetManager::MusicHandle AudioManager::GetOrLoadMusic(const std::string& path) {
    if (!m_assets) {
        m_lastError = "AudioManager requires an AssetManager before loading music";
        return {};
    }

    AssetManager::MusicHandle music = m_assets->GetMusic(path);
    if (!music) {
        m_lastError = m_assets->LastError();
        return {};
    }

    return music;
}

AssetManager::AudioChunkHandle AudioManager::GetOrLoadChunk(const std::string& path) {
    if (!m_assets) {
        m_lastError = "AudioManager requires an AssetManager before loading chunks";
        return {};
    }

    AssetManager::AudioChunkHandle chunk = m_assets->GetAudioChunk(path);
    if (!chunk) {
        m_lastError = m_assets->LastError();
        return {};
    }

    return chunk;
}

int AudioManager::ToMixerVolume(float normalized01) {
    const float clamped = std::clamp(normalized01, 0.0f, 1.0f);
    return static_cast<int>(clamped * 128.0f);
}

void AudioManager::PruneFinishedChannelRefs() {
    // Keep a strong ref only while mixer reports the channel is still active.
    if (!Mix_Playing(kVoiceChannel)) {
        m_activeVoiceChunk.reset();
    }

    for (int channel = 0; channel < kTotalChannels; ++channel) {
        if (channel == kVoiceChannel) {
            continue;
        }
        if (!Mix_Playing(channel)) {
            m_activeSfxChunksByChannel[channel].reset();
        }
    }
}

#endif

} // namespace vn
