#pragma once

#include <SDL3/SDL.h>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "SaveSnapshot.h"

class SaveManager {
public:
    // Maximum number of save pages to allow for UI pagination
    // UI should allow players to freely navigate to any page up to this limit,
    // even if no saves exist yet on higher pages
    static constexpr int kMaxSavePages = 99;

    struct SlotInfo {
        int slot = 0;
        bool exists = false;
        std::string path;
        std::string chapterName;
        std::string saveTime;
        std::string bgPath;
        bool hasThumbnail = false;
        std::string thumbnailPath;
    };

    struct GlobalSaveData {
        std::unordered_set<std::string> seenLines;
        std::string locale = "zh";
        float textSpeed = 30.0f;
        float bgmVolume = 0.8f;
        float sfxVolume = 0.9f;
        bool fullscreen = false;
        bool skipUnseen = false;  // Skip mode: false = only seen lines, true = all lines
        bool wheelAdvanceEnabled = false;  // Mouse wheel down advances dialogue when enabled
        bool voiceBleedEnabled = false;  // Voice bleed: allow voices to continue across dialogue transitions
        bool instantText = false;  // Instant text mode: text appears immediately
        bool animatedSprites = true;  // Toggle breathing/parallax visual effects
        std::unordered_map<std::string, bool> unlockFlags;
    };

    explicit SaveManager(std::string saveDir = "saves");

    bool SaveSlot(int slot, const vn::SaveSnapshot& data);
    bool SaveSlotThumbnailFromSurface(int slot, SDL_Surface* surface);
    bool LoadSlot(int slot, vn::SaveSnapshot& outData) const;

    // Quick save/load use a special slot (slot 0) for seamless saving
    bool QuickSave(const vn::SaveSnapshot& data);
    bool QuickLoad(vn::SaveSnapshot& outData) const;
    bool QuickSaveExists() const;

    bool SaveGlobal(const GlobalSaveData& data);
    bool LoadGlobal(GlobalSaveData& outData) const;
    bool DeleteSlot(int slot);

    // List save slots with pagination support
    // maxSlots: number of slots per page (default 6)
    // page: page number (0-based)
    // Returns slots for the specified page
    std::vector<SlotInfo> ListSlots(int maxSlots = 6, int page = 0) const;
    
    // Get total number of save pages
    int GetSavePageCount() const;

    const std::string& LastError() const { return m_lastError; }

private:
    std::string SlotPath(int slot) const;
    std::string SlotThumbnailPath(int slot) const;
    std::string GlobalPath() const;
    bool EnsureSaveDir() const;

    std::string m_saveDir;
    mutable std::string m_lastError;
};
