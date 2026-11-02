#pragma once

#include <array>
#include <memory>
#include <string>

#include "AssetManager.h"

#if defined(VN_ENGINE_HAS_SDL3_MIXER) && VN_ENGINE_HAS_SDL3_MIXER
#include <SDL3_mixer/SDL_mixer.h>
#endif

namespace vn {

class AudioManager {
public:
    AudioManager() = default;
    ~AudioManager();

    bool Init();
    void Shutdown();
    void SetAssetManager(AssetManager* assets) noexcept { m_assets = assets; }

    bool PlayBGM(const std::string& path, int loops = -1, int fadeInMs = 300);
    void StopBGM(int fadeOutMs = 300);

    bool PlaySFX(const std::string& path);
    bool PlayVoice(const std::string& path);
    void StopVoice(int fadeOutMs = 80);
        [[nodiscard]] bool IsVoicePlaying() const {
    #if defined(VN_ENGINE_HAS_SDL3_MIXER) && VN_ENGINE_HAS_SDL3_MIXER
        return m_initialized && Mix_Playing(kVoiceChannel) != 0;
    #else
        return false;
    #endif
        }
    void StopAllSFX();
    void Tick(float dt);

    // Call when dialogue advances to avoid voice overlap.
    // If allowBleed=true, voices can continue playing across dialogue transitions
    void OnDialogueAdvance(bool allowBleed = false);

    void SetBGMVolume(float normalized01);
    void SetSFXVolume(float normalized01);
    void SetVoiceVolume(float normalized01);
    void SetVoiceBleedEnabled(bool enabled) { m_voiceBleedEnabled = enabled; }
    [[nodiscard]] bool GetVoiceBleedEnabled() const { return m_voiceBleedEnabled; }

    [[nodiscard]] float GetBGMVolume() const { return m_bgmVolume; }
    [[nodiscard]] float GetSFXVolume() const { return m_sfxVolume; }
    [[nodiscard]] float GetVoiceVolume() const { return m_voiceVolume; }

    [[nodiscard]] const std::string& LastError() const { return m_lastError; }

private:
#if defined(VN_ENGINE_HAS_SDL3_MIXER) && VN_ENGINE_HAS_SDL3_MIXER
    [[nodiscard]] AssetManager::MusicHandle GetOrLoadMusic(const std::string& path);
    [[nodiscard]] AssetManager::AudioChunkHandle GetOrLoadChunk(const std::string& path);
    static int ToMixerVolume(float normalized01);
    void PruneFinishedChannelRefs();
#endif

    float m_bgmVolume = 0.8f;
    float m_sfxVolume = 0.9f;
    float m_voiceVolume = 1.0f;
    bool m_voiceBleedEnabled = false;  // Allow voices to continue across dialogue transitions

    std::string m_currentBgmPath;
    std::string m_lastError;
    bool m_initialized = false;
    AssetManager* m_assets = nullptr;

#if defined(VN_ENGINE_HAS_SDL3_MIXER) && VN_ENGINE_HAS_SDL3_MIXER
    AssetManager::MusicHandle m_activeMusic;
    AssetManager::AudioChunkHandle m_activeVoiceChunk;
    float m_channelRefPruneTimer = 0.0f;

    static constexpr int kVoiceChannel = 1;
    static constexpr int kTotalChannels = 16;

    std::array<AssetManager::AudioChunkHandle, kTotalChannels> m_activeSfxChunksByChannel;
#endif
};

} // namespace vn
