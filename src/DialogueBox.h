#pragma once

#include <SDL3/SDL.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#include "AssetManager.h"
#include "DialogueLine.h"
#include "Tween.h"

class DialogueBox {
private:
    enum class VisibilityState {
        Hidden,
        Entering,
        Idle,
        Exiting
    };

public:
    float charsPerSecond = 30.0f;
    bool autoMode = false;
    float autoDelayBase = 0.8f;  // Base delay in seconds
    float autoDelayPerChar = 0.015f;  // Small per-character compensation after typewriter completes
    bool instantText = false;  // When true, text appears instantly (charsPerSecond is ignored)
    bool visible = true;

    bool isDone = false;  // Set to true when text is fully displayed (controls CTC indicator rendering)
    bool readyToAutoAdvance = false;  // Set to true in autoMode when timer reaches threshold (controls auto page advance)
    bool isEmpty = false;

    // Initialize dialogue box and set up fonts, dimensions, and text cache hooks.
    void Init(SDL_Renderer* renderer,
              TTF_Font* font,
              int screenW,
              int screenH,
              vn::AssetManager* assets = nullptr,
              const std::string& fontPath = "",
              int fontPointSize = 22) {
        m_renderer = renderer;
        m_font = font;
        m_screenW = screenW;
        m_screenH = screenH;
        m_assets = assets;
        m_fontPath = fontPath;
        m_fontPointSize = fontPointSize;
        m_boxAlphaTween.Stop();
        m_boxTargetVisible = visible;
        m_boxAlpha = visible ? 1.0f : 0.0f;
        m_boxVisibilityState = visible ? VisibilityState::Idle : VisibilityState::Hidden;
        ApplyTextStyleFromLayout();
    }

    void SetLine(const std::string& speaker,
                 const std::string& text,
                 const std::string& lineId = "",
                 const std::string& voicePath = "");

    void SetLine(const vn::DialogueLine& line) {
        SetLine(line.speaker,
                line.rawText.empty() ? line.text : line.rawText,
                line.lineId,
                line.voicePath);
    }

    [[nodiscard]] bool HasMorePages() const {
        return m_currentPageIdx < static_cast<int>(m_pages.size()) - 1;
    }

    void ResetAutoAdvanceTimer() {
        m_autoTimer = 0.0f;
        readyToAutoAdvance = false;
    }

    void Clear() {
        isEmpty = true;
        isDone = false;
        readyToAutoAdvance = false;
        m_speaker.clear();
        m_speakerTexture.reset();
        m_text.clear();
        m_lineId.clear();
        m_voicePath.clear();
        m_totalChars = 0;
        m_visibleChars = 0.0f;
        m_autoTimer = 0.0f;
        m_boxAlphaTween.Stop();
        m_boxTargetVisible = visible;
        m_boxAlpha = visible ? 1.0f : 0.0f;
        m_boxVisibilityState = visible ? VisibilityState::Idle : VisibilityState::Hidden;
        m_renderLines.clear();
        m_pages.clear();
        m_styleSpans.clear();
        m_allInlineEvents.clear();
        m_allInstantRevealChars.clear();
        m_activeInlineEvents.clear();
        m_activeInstantRevealChars.clear();
        m_nextInlineEventIndex = 0;
        m_currentCharsPerSecond = charsPerSecond;
        m_inlinePauseRemaining = 0.0f;
        m_waitingForInlineClick = false;
        m_pageStartCharIndex = 0;
        m_currentPageIdx = 0;
        m_lineAdvance = 0;
        m_autoAdvanceCharCount = 0;
    }

    void Update(float dt, bool ctrlHeld) {
        if (isEmpty) return;

        const float safeDt = std::max(0.0f, dt);
        UpdateBoxVisibility(safeDt);

        if (m_boxAlpha <= 0.01f) return;

        if (m_waitingForInlineClick && autoMode) {
            m_autoTimer += safeDt;
            if (m_autoTimer >= autoDelayBase) {
                m_waitingForInlineClick = false;
                m_autoTimer = 0.0f;
            }
        }

        if (ctrlHeld || instantText) {
            RevealAllTextImmediate();
        } else if (m_visibleChars < static_cast<float>(m_totalChars)) {
            SyncRevealStateAtCurrentPosition();

            float remainingDt = safeDt;
            while (remainingDt > 0.0f && m_visibleChars < static_cast<float>(m_totalChars)) {
                if (m_waitingForInlineClick) {
                    break;
                }

                if (m_inlinePauseRemaining > 0.0f) {
                    const float consumed = std::min(remainingDt, m_inlinePauseRemaining);
                    m_inlinePauseRemaining -= consumed;
                    remainingDt -= consumed;
                    if (m_inlinePauseRemaining > 0.0f) {
                        break;
                    }

                    SyncRevealStateAtCurrentPosition();
                    continue;
                }

                const float cps = std::max(0.01f, m_currentCharsPerSecond);
                const int nextEventChar = (m_nextInlineEventIndex < m_activeInlineEvents.size())
                    ? m_activeInlineEvents[m_nextInlineEventIndex].charIndex
                    : m_totalChars;
                const float potentialVisible = m_visibleChars + cps * remainingDt;

                if (m_nextInlineEventIndex < m_activeInlineEvents.size() &&
                    static_cast<float>(nextEventChar) > m_visibleChars &&
                    potentialVisible >= static_cast<float>(nextEventChar)) {
                    const float timeToEvent = (static_cast<float>(nextEventChar) - m_visibleChars) / cps;
                    m_visibleChars = static_cast<float>(nextEventChar);
                    remainingDt = std::max(0.0f, remainingDt - timeToEvent);
                    SyncRevealStateAtCurrentPosition();
                    continue;
                }

                m_visibleChars = std::min(static_cast<float>(m_totalChars), potentialVisible);
                SyncRevealStateAtCurrentPosition();
                remainingDt = 0.0f;
            }

            if (m_visibleChars >= static_cast<float>(m_totalChars)) {
                SyncRevealStateAtCurrentPosition();
            }
        }

        const bool typing = m_visibleChars < static_cast<float>(m_totalChars) ||
                            m_waitingForInlineClick ||
                            m_inlinePauseRemaining > 0.0f;
        if (typing) {
            isDone = false;
            readyToAutoAdvance = false;
        } else {
            isDone = true;

            if (autoMode) {
                m_autoTimer += safeDt;
                const int autoReadableChars = std::max(0, m_autoAdvanceCharCount);
                float calculatedAutoDelay = autoDelayBase + (autoReadableChars * autoDelayPerChar);
                if (m_autoTimer >= calculatedAutoDelay) {
                    readyToAutoAdvance = true;
                }
            } else {
                readyToAutoAdvance = false;
            }
        }
    }

    bool HandleAdvance(bool userAdvanceTrigger) {
        if (isEmpty || m_boxAlpha < 0.9f) return false;

        if (m_waitingForInlineClick) {
            if (userAdvanceTrigger) {
                m_waitingForInlineClick = false;
                m_autoTimer = 0.0f;
            }
            return false;
        }

        if (m_inlinePauseRemaining > 0.0f) {
            if (userAdvanceTrigger) {
                m_inlinePauseRemaining = 0.0f;
                m_autoTimer = 0.0f;
            } else {
                return false;
            }
        }

        if (m_visibleChars < static_cast<float>(m_totalChars)) {
            if (userAdvanceTrigger) {
                RevealAllTextImmediate();
            }
            return false;
        }

        if (userAdvanceTrigger && m_currentPageIdx < static_cast<int>(m_pages.size()) - 1) {
            ++m_currentPageIdx;
            LoadCurrentPage();
            return false;
        }

        if (userAdvanceTrigger) {
            m_autoTimer = 0.0f;
            readyToAutoAdvance = false;
        }

        return userAdvanceTrigger;
    }

    void Render(SDL_Renderer* renderer);

    // Get dialogue history log.
    [[nodiscard]] const std::deque<vn::DialogueLine>& GetLog() const {
        return m_log;
    }

    void SetLog(const std::vector<vn::DialogueLine>& log) {
        m_log.assign(log.begin(), log.end());
        while (static_cast<int>(m_log.size()) > MaxLog) {
            m_log.pop_front();
        }
    }

    void ReleaseResources() {
        Clear();
        m_boxAlphaTween.Stop();
        m_styleFontHandle.reset();
        m_cachedPanelTexture.reset();
        m_frostedBackdropTexture.reset();
        m_lastCachedPanelPath.clear();
        m_frostedBackdropRect = {};
        m_lastFrostedCaptureTick = 0;
        m_font = nullptr;
        m_assets = nullptr;
        m_renderer = nullptr;
    }

private:
    enum class InlineEventType {
        Wait,
        Click,
        SetCps
    };

    struct InlineEvent {
        InlineEventType type = InlineEventType::Wait;
        int charIndex = 0;
        float value = 0.0f;
    };

    struct StyledSpan {
        std::size_t startByte = 0;
        std::size_t endByte = 0;
        SDL_Color color{ 255, 255, 255, 255 };
    };

    struct RenderFragment {
        std::string text;
        SDL_Color color{ 255, 255, 255, 255 };
        float width = 0.0f;
        float height = 0.0f;
        int charCount = 0;
        std::vector<int> revealClipWidths;
        vn::AssetManager::TextureHandle texture;
    };

    struct RenderLine {
        std::string text;
        float height = 0.0f;
        float advanceY = 0.0f;
        int charCount = 0;
        bool isManualBreak = false;
        int globalStartCharIndex = 0;
        std::size_t globalStartByte = 0;
        std::vector<RenderFragment> fragments;
    };

    struct ParsedDialogueContent {
        std::string plainText;
        std::vector<StyledSpan> styleSpans;
        std::vector<InlineEvent> inlineEvents;
        std::vector<bool> instantRevealChars;
    };

    struct Layout {
        float dialogBoxOuterMarginX = 40.0f;
        float dialogBoxOuterBottom = 20.0f;
        float dialogBoxHeight = 200.0f;
        float dialogTextPaddingX = 24.0f;
        float dialogTextPaddingY = 24.0f;
        float dialogNameOffsetY = 40.0f;
        float dialogNamePaddingX = 20.0f;
        float dialogNamePaddingY = 8.0f;
        float dialogCtcRight = 22.0f;
        float dialogCtcBottom = 16.0f;
        int dialogTextPointSize = 22;
        int dialogTextOutline = 1;
        float dialogLineSpacing = 1.12f;
        int dialogTextTintR = 215;
        int dialogTextTintG = 225;
        int dialogTextTintB = 235;
        std::string dialogBoxTexturePath;
        float dialogBoxSliceCorner = 18.0f;
        float dialogFrostPaddingX = 12.0f;
        float dialogFrostPaddingY = 10.0f;
        float dialogFrostDownscale = 5.0f;
        float dialogFrostRefreshSeconds = 0.066f;
        float dialogFocusBandPaddingY = 30.0f;
        float dialogFocusBandTopAlpha = 0.02f;
        float dialogFocusBandAlpha = 0.34f;
        float dialogPanelShadowPadding = 7.0f;
        float dialogPanelShadowAlpha = 0.26f;
        float dialogPanelAlphaTop = 0.72f;
        float dialogPanelAlphaBottom = 0.88f;
        float dialogPanelBorderAlpha = 0.48f;
        float dialogPanelHighlightInsetX = 18.0f;
        float dialogPanelHighlightAlpha = 0.55f;
        float dialogPanelInnerGlowHeight = 32.0f;
        float dialogPanelInnerGlowAlpha = 0.12f;
        float dialogPanelTextureAlpha = 0.92f;
        float dialogPanelEnterDuration = 0.18f;
        float dialogPanelExitDuration = 0.12f;
        float dialogSpeakerPlateAlphaTop = 0.74f;
        float dialogSpeakerPlateAlphaBottom = 0.88f;
        float dialogSpeakerPlateBorderAlpha = 0.45f;
        float dialogSpeakerAccentWidth = 4.0f;
        float dialogSpeakerAccentAlpha = 0.58f;
    };

    SDL_Renderer* m_renderer = nullptr;
    TTF_Font* m_font = nullptr;
    vn::AssetManager* m_assets = nullptr;
    std::string m_fontPath;
    int m_fontPointSize = 22;
    vn::AssetManager::FontHandle m_styleFontHandle;
    int m_screenW = 0;
    int m_screenH = 0;
    Layout m_layout{};

    std::string m_speaker;
    vn::AssetManager::TextureHandle m_speakerTexture;
    std::string m_text;
    std::string m_lineId;
    std::string m_voicePath;

    int m_totalChars = 0;
    float m_visibleChars = 0.0f;
    float m_boxAlpha = 1.0f;
    bool m_boxTargetVisible = true;
    VisibilityState m_boxVisibilityState = VisibilityState::Idle;
    Tween<float> m_boxAlphaTween;
    std::vector<RenderLine> m_renderLines;
    std::vector<std::vector<RenderLine>> m_pages;
    std::vector<StyledSpan> m_styleSpans;
    std::vector<InlineEvent> m_allInlineEvents;
    std::vector<bool> m_allInstantRevealChars;
    std::vector<InlineEvent> m_activeInlineEvents;
    std::vector<bool> m_activeInstantRevealChars;
    std::size_t m_nextInlineEventIndex = 0;
    float m_currentCharsPerSecond = 30.0f;
    float m_inlinePauseRemaining = 0.0f;
    bool m_waitingForInlineClick = false;
    int m_pageStartCharIndex = 0;
    int m_currentPageIdx = 0;
    int m_lineAdvance = 0;
    int m_autoAdvanceCharCount = 0;

    float m_autoTimer = 0.0f;

    std::deque<vn::DialogueLine> m_log;
    static constexpr int MaxLog = 500;

    // Cached UI textures (persistent across frames to avoid per-frame reload overhead)
    vn::AssetManager::TextureHandle m_cachedPanelTexture;
    vn::AssetManager::TextureHandle m_frostedBackdropTexture;
    std::string m_lastCachedPanelPath;  // Track which path was cached
    SDL_Rect m_frostedBackdropRect{};
    Uint64 m_lastFrostedCaptureTick = 0;

    [[nodiscard]] static int CountUTF8Chars(const std::string& text);
    [[nodiscard]] static std::size_t NextUTF8CharBytes(const std::string& text, std::size_t offset);
    [[nodiscard]] static bool IsWhitespaceByte(unsigned char c);
    [[nodiscard]] std::shared_ptr<SDL_Texture> GetSpeakerNameTexture(const std::string& name) const;
    [[nodiscard]] ParsedDialogueContent ParseDialogueContent(const std::string& rawText) const;
    void BuildRevealMetrics(RenderFragment& fragment) const;
    void PopulateFragmentsForLine(RenderLine& line) const;
    void BuildTextLayout();
    void LoadCurrentPage();
    void UpdateBoxVisibility(float dt);
    void ConsumeInlineEventsAtCurrentPosition();
    void SkipInstantRevealCharsAtCurrentPosition();
    void SyncRevealStateAtCurrentPosition();
    void RevealAllTextImmediate();
    void RenderDialoguePanel(SDL_Renderer* renderer, const SDL_FRect& box, Uint8 alpha);
    void RenderSpeakerPlate(SDL_Renderer* renderer, const SDL_FRect& rect, Uint8 alpha) const;
    bool RenderFrostedBackdrop(SDL_Renderer* renderer, const SDL_FRect& rect, Uint8 alpha);
    void ApplyTextStyleFromLayout() {
        const int targetPointSize = std::max(8, m_layout.dialogTextPointSize);
        m_fontPointSize = targetPointSize;

        if (!m_assets || m_fontPath.empty()) {
            return;
        }

        m_styleFontHandle = m_assets->GetFont(m_fontPath, static_cast<float>(targetPointSize));
        if (m_styleFontHandle) {
            m_font = m_styleFontHandle.get();
        }
    }
};
