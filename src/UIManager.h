#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "DialogueLine.h"
#include "SceneMode.h"

struct SDL_Renderer;
struct TTF_Font;
class Input;
class SaveManager;

namespace vn {
class AssetManager;

class UIManager {
public:
    enum CommandFlag : std::uint32_t {
        CommandNone = 0u,
        CommandAdvanceDialogue = 1u << 0,
        CommandChoiceSelected = 1u << 1,
        CommandOpenSystemMenu = 1u << 2,
        CommandOpenSaveMenu = 1u << 3,
        CommandOpenLoadMenu = 1u << 4,
        CommandQuickSave = 1u << 5,
        CommandQuickLoad = 1u << 6,
        CommandReturnToTitle = 1u << 7,
        CommandQuit = 1u << 8,
        CommandConfigChanged = 1u << 9,
        CommandToggleFullscreen = 1u << 10,
        CommandStartNewGame = 1u << 11,
        CommandContinueGame = 1u << 12,
        CommandResume = 1u << 13,
        CommandSaveSlot = 1u << 14,
        CommandLoadSlot = 1u << 15,
        CommandDeleteSlot = 1u << 16,
        CommandRollback = 1u << 17,
        CommandSkipVideo = 1u << 18,
    };

    struct InitContext {
        SDL_Renderer* renderer = nullptr;
        TTF_Font* font = nullptr;
        int width = 0;
        int height = 0;
        AssetManager* assets = nullptr;
        SaveManager* saveManager = nullptr;
        std::string fontPath;
        int fontPointSize = 22;
        std::string titleBackgroundPath = "assets/bg/classroom.png";
    };

    struct RuntimeConfig {
        float textSpeed = 30.0f;
        float bgmVolume = 0.8f;
        float sfxVolume = 0.9f;
        std::string localeCode = "zh";
        bool animatedSpritesEnabled = true;
        bool skipUnseenEnabled = false;
        bool wheelAdvanceEnabled = false;
        bool voiceBleedEnabled = false;
        bool fullscreenEnabled = false;
        bool instantText = false;
    };

    struct UpdateResult {
        std::uint32_t commands = CommandNone;
        int selectedChoice = -1;
        int saveSlot = -1;
        int loadSlot = -1;
        int deleteSlot = -1;
        RuntimeConfig config;

        [[nodiscard]] bool Has(CommandFlag flag) const noexcept {
            return (commands & flag) != 0;
        }
    };

    UIManager();
    ~UIManager();

    UIManager(const UIManager&) = delete;
    UIManager& operator=(const UIManager&) = delete;
    UIManager(UIManager&&) noexcept;
    UIManager& operator=(UIManager&&) noexcept;

    void Init(const InitContext& context);
    void ReleaseResources();

    void SetTranslator(std::function<std::string(const std::string&)> translator);
    void RefreshStrings();
    void ApplyConfig(const RuntimeConfig& config);

    UpdateResult Update(float dt, Input& input, SceneMode state);
    void Render(SDL_Renderer* renderer, SceneMode state);

    void EnterTitleMenu(bool continueEnabled);
    void EnterInGame();
    void SetContinueEnabled(bool enabled);
    void SetContinueThumbnailFromPath(AssetManager& assets, const std::string& thumbnailPath);
    void ClearContinueThumbnail();

    void OpenSystemMenu(bool inGame);
    void OpenSaveMenu(bool inGame);
    void OpenLoadMenu(bool inGame);
    void OpenExitConfirm(bool inGame);
    void CloseSystemMenu();
    void RefreshSaveSlots();

    void SetDialogueLog(const std::vector<DialogueLine>& log);
    [[nodiscard]] std::vector<DialogueLine> DialogueLogSnapshot() const;
    void ShowDialogue(const std::string& speaker,
                      const std::string& text,
                      const std::string& lineId,
                      const std::string& voicePath);
    void ShowChoices(const std::vector<std::string>& choices);
    void ClearDialogue();
    void HideChoices();
    void ShowLog();
    void HideLog();

    void SetDialogueVisible(bool visible);
    void SetCanSave(bool canSave);
    void SetCurrentLineSeen(bool seen);
    void SetVoicePlaying(bool voicePlaying);
    void SetObserveHandlesCancel(bool observeHandlesCancel);
    void BlockAdvanceThisFrame();

    [[nodiscard]] bool IsDialogueVisible() const;
    [[nodiscard]] bool IsUIHiddenByUser() const;
    [[nodiscard]] bool IsLogVisible() const;
    [[nodiscard]] bool AreChoicesVisible() const;
    [[nodiscard]] bool IsSystemMenuOpen() const;
    [[nodiscard]] bool IsAutoModeEnabled() const;
    [[nodiscard]] bool IsSkipModeEnabled() const;
    [[nodiscard]] bool WantsHandCursor(SceneMode state, float mouseX, float mouseY) const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace vn
