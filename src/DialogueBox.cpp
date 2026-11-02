#include "DialogueBox.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <string_view>

#include "RendererUtils.h"
#include "UITheme.h"

namespace {

struct Utf8Glyph {
    std::size_t nextOffset = 0;
    char32_t codePoint = 0;
};

Utf8Glyph StepUTF8Glyph(std::string_view text, std::size_t offset) {
    if (offset >= text.size()) {
        return { text.size(), 0 };
    }

    const char* const start = text.data();
    const char* cursor = start + offset;
    std::size_t remaining = text.size() - offset;
    const Uint32 codePoint = SDL_StepUTF8(&cursor, &remaining);
    std::size_t nextOffset = static_cast<std::size_t>(cursor - start);
    if (nextOffset <= offset) {
        nextOffset = std::min(text.size(), offset + 1);
    }

    return { nextOffset, static_cast<char32_t>(codePoint) };
}

std::size_t PrevUTF8CharOffset(std::string_view text, std::size_t offsetExclusive) {
    if (offsetExclusive == 0 || text.empty()) {
        return 0;
    }

    const char* const start = text.data();
    const char* cursor = start + std::min(offsetExclusive, text.size());
    (void)SDL_StepBackUTF8(start, &cursor);
    const std::size_t result = static_cast<std::size_t>(cursor - start);
    return result < offsetExclusive ? result : offsetExclusive - 1;
}

char32_t UTF8CodePointAt(std::string_view text, std::size_t offset) {
    return StepUTF8Glyph(text, offset).codePoint;
}

std::size_t ClampToUTF8Boundary(std::string_view text, std::size_t start, std::size_t length) {
    const std::size_t target = std::min(text.size(), start + length);
    std::size_t boundary = start;
    while (boundary < target) {
        const std::size_t next = StepUTF8Glyph(text, boundary).nextOffset;
        if (next > target || next <= boundary) {
            break;
        }
        boundary = next;
    }
    return boundary - start;
}

bool EqualsAsciiInsensitive(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }

    for (std::size_t i = 0; i < a.size(); ++i) {
        const unsigned char lhs = static_cast<unsigned char>(a[i]);
        const unsigned char rhs = static_cast<unsigned char>(b[i]);
        if (std::tolower(lhs) != std::tolower(rhs)) {
            return false;
        }
    }

    return true;
}

std::string_view TrimAscii(std::string_view value) {
    const auto isSpace = [](unsigned char c) {
        return std::isspace(c) != 0;
    };

    while (!value.empty() && isSpace(static_cast<unsigned char>(value.front()))) {
        value.remove_prefix(1);
    }
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.back()))) {
        value.remove_suffix(1);
    }
    return value;
}

bool TryParseHexColor(std::string_view spec, SDL_Color& outColor) {
    if (!spec.empty() && spec.front() == '#') {
        spec.remove_prefix(1);
    }

    if (spec.size() != 6) {
        return false;
    }

    const std::string hex(spec);
    char* end = nullptr;
    const unsigned long value = std::strtoul(hex.c_str(), &end, 16);
    if (!end || *end != '\0') {
        return false;
    }

    outColor = SDL_Color{
        static_cast<Uint8>((value >> 16) & 0xFFu),
        static_cast<Uint8>((value >> 8) & 0xFFu),
        static_cast<Uint8>(value & 0xFFu),
        255
    };
    return true;
}

bool TryParseNamedColor(std::string_view spec, SDL_Color& outColor) {
    if (EqualsAsciiInsensitive(spec, "white")) { outColor = SDL_Color{ 255, 255, 255, 255 }; return true; }
    if (EqualsAsciiInsensitive(spec, "black")) { outColor = SDL_Color{ 0, 0, 0, 255 }; return true; }
    if (EqualsAsciiInsensitive(spec, "red")) { outColor = SDL_Color{ 235, 82, 82, 255 }; return true; }
    if (EqualsAsciiInsensitive(spec, "green")) { outColor = SDL_Color{ 92, 201, 113, 255 }; return true; }
    if (EqualsAsciiInsensitive(spec, "blue")) { outColor = SDL_Color{ 96, 164, 255, 255 }; return true; }
    if (EqualsAsciiInsensitive(spec, "yellow")) { outColor = SDL_Color{ 255, 214, 92, 255 }; return true; }
    if (EqualsAsciiInsensitive(spec, "cyan")) { outColor = SDL_Color{ 94, 229, 255, 255 }; return true; }
    if (EqualsAsciiInsensitive(spec, "magenta")) { outColor = SDL_Color{ 255, 112, 228, 255 }; return true; }
    if (EqualsAsciiInsensitive(spec, "orange")) { outColor = SDL_Color{ 255, 164, 87, 255 }; return true; }
    return false;
}

bool TryParseColorSpec(std::string_view spec, SDL_Color& outColor) {
    const std::string_view trimmed = TrimAscii(spec);
    return TryParseHexColor(trimmed, outColor) || TryParseNamedColor(trimmed, outColor);
}

float GetAutoPauseSecondsForPunctuation(char32_t cp) {
    switch (cp) {
    case U',':
    case U';':
    case U':':
    case 0x3001: // 、
    case 0xFF0C: // ，
    case 0xFF1B: // ；
    case 0xFF1A: // ：
        return 0.035f;
    case U'.':
    case U'!':
    case U'?':
    case 0x3002: // 。
    case 0xFF01: // ！
    case 0xFF1F: // ？
    case 0x2026: // …
        return 0.085f;
    default:
        return 0.0f;
    }
}

bool IsInstantRevealWhitespace(char32_t cp) {
    switch (cp) {
    case U' ':
    case U'\t':
    case U'\r':
    case U'\n':
    case 0x3000: // full-width ideographic space
        return true;
    default:
        return false;
    }
}

bool StartsWithInlineWaitCommand(std::string_view text, std::size_t offset) {
    if (offset >= text.size() || text[offset] != '{') {
        return false;
    }
    const std::size_t close = text.find('}', offset + 1);
    if (close == std::string::npos) {
        return false;
    }

    const std::string_view command = TrimAscii(text.substr(offset + 1, close - (offset + 1)));
    return command.rfind("w=", 0) == 0 || command.rfind("W=", 0) == 0;
}

Uint8 UnitAlpha(float value) {
    return static_cast<Uint8>(std::clamp(value, 0.0f, 1.0f) * 255.0f);
}

SDL_Color WithScaledAlpha(SDL_Color color, Uint8 alpha, float scale) {
    color.a = UnitAlpha((static_cast<float>(alpha) / 255.0f) * scale);
    return color;
}

SDL_FColor ToFColor(SDL_Color color) {
    return SDL_FColor{
        static_cast<float>(color.r) / 255.0f,
        static_cast<float>(color.g) / 255.0f,
        static_cast<float>(color.b) / 255.0f,
        static_cast<float>(color.a) / 255.0f
    };
}

SDL_FRect InflateRect(SDL_FRect rect, float x, float y) {
    rect.x -= x;
    rect.y -= y;
    rect.w += x * 2.0f;
    rect.h += y * 2.0f;
    return rect;
}

SDL_Rect ToBoundedRect(const SDL_FRect& rect, int screenW, int screenH) {
    const int x0 = std::clamp(static_cast<int>(std::floor(rect.x)), 0, std::max(0, screenW));
    const int y0 = std::clamp(static_cast<int>(std::floor(rect.y)), 0, std::max(0, screenH));
    const int x1 = std::clamp(static_cast<int>(std::ceil(rect.x + rect.w)), 0, std::max(0, screenW));
    const int y1 = std::clamp(static_cast<int>(std::ceil(rect.y + rect.h)), 0, std::max(0, screenH));
    return SDL_Rect{ x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0) };
}

bool SameRect(const SDL_Rect& a, const SDL_Rect& b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

void FillRect(SDL_Renderer* renderer, const SDL_FRect& rect, SDL_Color color) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderFillRect(renderer, &rect);
}

void StrokeRect(SDL_Renderer* renderer, const SDL_FRect& rect, SDL_Color color) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderRect(renderer, &rect);
}

void DrawLine(SDL_Renderer* renderer, float x1, float y1, float x2, float y2, SDL_Color color) {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    SDL_RenderLine(renderer, x1, y1, x2, y2);
}

void DrawVerticalGradient(SDL_Renderer* renderer, const SDL_FRect& rect, SDL_Color top, SDL_Color bottom) {
    if (!renderer || rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }

    const SDL_FColor topF = ToFColor(top);
    const SDL_FColor bottomF = ToFColor(bottom);
    const SDL_Vertex vertices[4] = {
        { { rect.x, rect.y }, topF, { 0.0f, 0.0f } },
        { { rect.x + rect.w, rect.y }, topF, { 1.0f, 0.0f } },
        { { rect.x + rect.w, rect.y + rect.h }, bottomF, { 1.0f, 1.0f } },
        { { rect.x, rect.y + rect.h }, bottomF, { 0.0f, 1.0f } }
    };
    const int indices[6] = { 0, 1, 2, 0, 2, 3 };

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(renderer, nullptr, vertices, 4, indices, 6);
}

} // namespace

int DialogueBox::CountUTF8Chars(const std::string& text) {
    int count = 0;
    for (std::size_t i = 0; i < text.size();) {
        i = NextUTF8CharBytes(text, i);
        ++count;
    }
    return count;
}

std::size_t DialogueBox::NextUTF8CharBytes(const std::string& text, std::size_t offset) {
    return StepUTF8Glyph(text, offset).nextOffset;
}

bool DialogueBox::IsWhitespaceByte(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r';
}

void DialogueBox::BuildRevealMetrics(RenderFragment& fragment) const {
    fragment.revealClipWidths.clear();
    fragment.revealClipWidths.push_back(0);
    fragment.width = 0.0f;
    fragment.height = 0.0f;
    fragment.charCount = 0;

    if (!m_font || fragment.text.empty()) {
        return;
    }

    const int outlineThickness = std::max(0, m_layout.dialogTextOutline);
    const int previousOutline = TTF_GetFontOutline(m_font);
    if (previousOutline != outlineThickness) {
        (void)TTF_SetFontOutline(m_font, outlineThickness);
    }

    std::size_t offset = 0;
    while (offset < fragment.text.size()) {
        offset = NextUTF8CharBytes(fragment.text, offset);

        int prefixWidth = 0;
        int prefixHeight = 0;
        (void)TTF_GetStringSize(m_font, fragment.text.c_str(), offset, &prefixWidth, &prefixHeight);
        fragment.revealClipWidths.push_back(std::max(0, prefixWidth));
        fragment.height = std::max(fragment.height, static_cast<float>(prefixHeight));
    }

    fragment.charCount = std::max(0, static_cast<int>(fragment.revealClipWidths.size()) - 1);
    fragment.width = fragment.revealClipWidths.empty() ? 0.0f : static_cast<float>(fragment.revealClipWidths.back());

    if (previousOutline != outlineThickness) {
        (void)TTF_SetFontOutline(m_font, previousOutline);
    }
}

DialogueBox::ParsedDialogueContent DialogueBox::ParseDialogueContent(const std::string& rawText) const {
    ParsedDialogueContent parsed;
    if (rawText.empty()) {
        return parsed;
    }

    const std::string_view rawTextView(rawText);
    const SDL_Color defaultColor{
        static_cast<Uint8>(std::clamp(m_layout.dialogTextTintR, 0, 255)),
        static_cast<Uint8>(std::clamp(m_layout.dialogTextTintG, 0, 255)),
        static_cast<Uint8>(std::clamp(m_layout.dialogTextTintB, 0, 255)),
        255
    };

    std::vector<SDL_Color> colorStack{ defaultColor };
    std::string buffer;
    std::size_t bufferStartByte = 0;
    int visibleCharCount = 0;

    const auto flushBuffer = [&]() {
        if (buffer.empty()) {
            return;
        }

        StyledSpan span;
        span.startByte = bufferStartByte;
        span.endByte = parsed.plainText.size();
        span.color = colorStack.empty() ? defaultColor : colorStack.back();

        if (!parsed.styleSpans.empty()) {
            StyledSpan& previous = parsed.styleSpans.back();
            if (previous.endByte == span.startByte &&
                previous.color.r == span.color.r &&
                previous.color.g == span.color.g &&
                previous.color.b == span.color.b &&
                previous.color.a == span.color.a) {
                previous.endByte = span.endByte;
                buffer.clear();
                return;
            }
        }

        parsed.styleSpans.push_back(span);
        buffer.clear();
    };

    for (std::size_t i = 0; i < rawTextView.size();) {
        if (rawTextView.substr(i, 8) == "</color>") {
            flushBuffer();
            if (colorStack.size() > 1) {
                colorStack.pop_back();
            }
            i += 8;
            continue;
        }

        if (rawTextView.substr(i, 7) == "<color=") {
            const std::size_t close = rawTextView.find('>', i + 7);
            if (close != std::string_view::npos) {
                SDL_Color parsedColor{};
                if (TryParseColorSpec(rawTextView.substr(i + 7, close - (i + 7)), parsedColor)) {
                    flushBuffer();
                    colorStack.push_back(parsedColor);
                    i = close + 1;
                    continue;
                }
            }
        }

        if (rawTextView[i] == '{') {
            const std::size_t close = rawTextView.find('}', i + 1);
            if (close != std::string_view::npos) {
                const std::string_view command = TrimAscii(rawTextView.substr(i + 1, close - (i + 1)));
                bool handled = false;

                if (EqualsAsciiInsensitive(command, "click")) {
                    flushBuffer();
                    parsed.inlineEvents.push_back({ InlineEventType::Click, visibleCharCount, 0.0f });
                    handled = true;
                } else if (command.rfind("w=", 0) == 0 || command.rfind("W=", 0) == 0) {
                    const std::string valueText(command.substr(2));
                    char* end = nullptr;
                    const float seconds = std::strtof(valueText.c_str(), &end);
                    if (end && *end == '\0') {
                        flushBuffer();
                        parsed.inlineEvents.push_back({ InlineEventType::Wait, visibleCharCount, std::max(0.0f, seconds) });
                        handled = true;
                    }
                } else if (command.rfind("cps=", 0) == 0 || command.rfind("CPS=", 0) == 0 ||
                           command.rfind("Cps=", 0) == 0) {
                    const std::string valueText(command.substr(4));
                    char* end = nullptr;
                    const float cps = std::strtof(valueText.c_str(), &end);
                    if (end && *end == '\0' && cps > 0.0f) {
                        flushBuffer();
                        parsed.inlineEvents.push_back({ InlineEventType::SetCps, visibleCharCount, cps });
                        handled = true;
                    }
                }

                if (handled) {
                    i = close + 1;
                    continue;
                }
            }
        }

        if (buffer.empty()) {
            bufferStartByte = parsed.plainText.size();
        }

        const Utf8Glyph glyphInfo = StepUTF8Glyph(rawText, i);
        const std::size_t nextOffset = glyphInfo.nextOffset;
        const std::string_view glyph(rawTextView.data() + i, nextOffset - i);
        const char32_t codePoint = glyphInfo.codePoint;
        parsed.plainText.append(glyph.data(), glyph.size());
        buffer.append(glyph.data(), glyph.size());
        parsed.instantRevealChars.push_back(IsInstantRevealWhitespace(codePoint));
        ++visibleCharCount;

        const float autoPauseSeconds = GetAutoPauseSecondsForPunctuation(codePoint);
        if (autoPauseSeconds > 0.0f && !StartsWithInlineWaitCommand(rawText, nextOffset)) {
            // Insert punctuation auto-pause only when more visible content follows.
            bool hasMoreText = false;
            for (std::size_t j = nextOffset; j < rawText.size(); ++j) {
                if (!IsWhitespaceByte(static_cast<unsigned char>(rawText[j]))) {
                    hasMoreText = true;
                    break;
                }
            }
            if (hasMoreText) {
                flushBuffer();
                parsed.inlineEvents.push_back({ InlineEventType::Wait, visibleCharCount, autoPauseSeconds });
            }
        }

        i = nextOffset;
    }

    flushBuffer();
    return parsed;
}

void DialogueBox::PopulateFragmentsForLine(RenderLine& line) const {
    line.fragments.clear();
    if (line.isManualBreak || line.text.empty()) {
        return;
    }

    const std::size_t lineStartByte = line.globalStartByte;
    const std::size_t lineEndByte = lineStartByte + line.text.size();

    for (const StyledSpan& span : m_styleSpans) {
        if (span.endByte <= lineStartByte || span.startByte >= lineEndByte) {
            continue;
        }

        const std::size_t fragmentStart = std::max(span.startByte, lineStartByte);
        const std::size_t fragmentEnd = std::min(span.endByte, lineEndByte);
        if (fragmentEnd <= fragmentStart) {
            continue;
        }

        RenderFragment fragment;
        fragment.text = m_text.substr(fragmentStart, fragmentEnd - fragmentStart);
        fragment.color = span.color;
        BuildRevealMetrics(fragment);
        if (m_assets && m_font) {
            fragment.texture = m_assets->GetTextTexture(
                fragment.text,
                m_font,
                m_fontPath,
                m_fontPointSize,
                fragment.color,
                std::max(0, m_layout.dialogTextOutline)
            );
        }
        line.height = std::max(line.height, fragment.height);
        line.fragments.push_back(std::move(fragment));
    }

    if (line.fragments.empty()) {
        RenderFragment fragment;
        fragment.text = line.text;
        fragment.color = SDL_Color{
            static_cast<Uint8>(std::clamp(m_layout.dialogTextTintR, 0, 255)),
            static_cast<Uint8>(std::clamp(m_layout.dialogTextTintG, 0, 255)),
            static_cast<Uint8>(std::clamp(m_layout.dialogTextTintB, 0, 255)),
            255
        };
        BuildRevealMetrics(fragment);
        if (m_assets && m_font) {
            fragment.texture = m_assets->GetTextTexture(
                fragment.text,
                m_font,
                m_fontPath,
                m_fontPointSize,
                fragment.color,
                std::max(0, m_layout.dialogTextOutline)
            );
        }
        line.height = std::max(line.height, fragment.height);
        line.fragments.push_back(std::move(fragment));
    }
}

void DialogueBox::LoadCurrentPage() {
    m_totalChars = 0;
    m_visibleChars = 0.0f;
    m_autoTimer = 0.0f;
    m_inlinePauseRemaining = 0.0f;
    m_waitingForInlineClick = false;
    m_nextInlineEventIndex = 0;
    m_currentCharsPerSecond = std::max(0.01f, charsPerSecond);
    m_autoAdvanceCharCount = 0;
    isDone = false;
    readyToAutoAdvance = false;

    if (m_pages.empty()) {
        m_renderLines.clear();
        m_activeInlineEvents.clear();
        m_activeInstantRevealChars.clear();
        m_pageStartCharIndex = 0;
        return;
    }

    m_currentPageIdx = std::clamp(m_currentPageIdx, 0, static_cast<int>(m_pages.size()) - 1);
    m_renderLines = m_pages[static_cast<std::size_t>(m_currentPageIdx)];
    m_pageStartCharIndex = m_renderLines.empty() ? 0 : m_renderLines.front().globalStartCharIndex;

    for (const RenderLine& line : m_renderLines) {
        m_totalChars += line.charCount;
    }

    const int pageEndCharIndex = m_pageStartCharIndex + m_totalChars;
    m_activeInlineEvents.clear();
    m_activeInlineEvents.reserve(m_allInlineEvents.size());
    for (const InlineEvent& event : m_allInlineEvents) {
        if (event.charIndex < m_pageStartCharIndex || event.charIndex > pageEndCharIndex) {
            continue;
        }

        InlineEvent localized = event;
        localized.charIndex -= m_pageStartCharIndex;
        m_activeInlineEvents.push_back(localized);
    }

    const std::size_t maskStart = static_cast<std::size_t>(std::clamp(m_pageStartCharIndex, 0, static_cast<int>(m_allInstantRevealChars.size())));
    const std::size_t maskEnd = static_cast<std::size_t>(std::clamp(pageEndCharIndex, 0, static_cast<int>(m_allInstantRevealChars.size())));
    m_activeInstantRevealChars.clear();
    if (maskStart < maskEnd) {
        m_activeInstantRevealChars.assign(m_allInstantRevealChars.begin() + maskStart,
                                          m_allInstantRevealChars.begin() + maskEnd);
    }
    if (m_activeInstantRevealChars.size() < static_cast<std::size_t>(m_totalChars)) {
        m_activeInstantRevealChars.resize(static_cast<std::size_t>(m_totalChars), false);
    }

    m_autoAdvanceCharCount = 0;
    for (bool instantReveal : m_activeInstantRevealChars) {
        if (!instantReveal) {
            ++m_autoAdvanceCharCount;
        }
    }

    SyncRevealStateAtCurrentPosition();
}

void DialogueBox::UpdateBoxVisibility(float dt) {
    if (m_boxTargetVisible != visible) {
        m_boxTargetVisible = visible;

        if (m_boxTargetVisible) {
            m_boxVisibilityState = VisibilityState::Entering;
            m_boxAlphaTween.Start(&m_boxAlpha,
                                  m_boxAlpha,
                                  1.0f,
                                  std::max(0.0f, m_layout.dialogPanelEnterDuration),
                                  Ease::OutQuad,
                                  [this]() {
                                      if (m_boxTargetVisible) {
                                          m_boxVisibilityState = VisibilityState::Idle;
                                      }
                                  });
        } else {
            m_boxVisibilityState = VisibilityState::Exiting;
            m_boxAlphaTween.Start(&m_boxAlpha,
                                  m_boxAlpha,
                                  0.0f,
                                  std::max(0.0f, m_layout.dialogPanelExitDuration),
                                  Ease::InQuad,
                                  [this]() {
                                      if (!m_boxTargetVisible) {
                                          m_boxVisibilityState = VisibilityState::Hidden;
                                      }
                                  });
        }
    }

    m_boxAlphaTween.Update(std::max(0.0f, dt));
    m_boxAlpha = std::clamp(m_boxAlpha, 0.0f, 1.0f);
}

namespace {

bool IsLeadingPunctuation(char32_t cp) {
    if (cp == U',' || cp == U'.' || cp == U'!' || cp == U'?' || cp == U':' || cp == U';' ||
        cp == U')' || cp == U']' || cp == U'}') {
        return true;
    }

    switch (cp) {
    case 0x3001:
    case 0x3002:
    case 0xFF0C:
    case 0xFF0E:
    case 0xFF1A:
    case 0xFF1B:
    case 0xFF01:
    case 0xFF1F:
    case 0x300D:
    case 0x300F:
    case 0xFF09:
    case 0x3011:
    case 0x2019:
    case 0x201D:
        return true;
    default:
        return false;
    }
}

} // namespace

void DialogueBox::SetLine(const std::string& speaker,
                          const std::string& text,
                          const std::string& lineId,
                          const std::string& voicePath) {
    m_speaker = speaker;
    m_lineId = lineId;
    m_voicePath = voicePath;
    m_speakerTexture = GetSpeakerNameTexture(m_speaker);

    const ParsedDialogueContent parsed = ParseDialogueContent(text);
    m_text = parsed.plainText;
    m_styleSpans = parsed.styleSpans;
    m_allInlineEvents = parsed.inlineEvents;
    m_allInstantRevealChars = parsed.instantRevealChars;

    isEmpty = false;
    isDone = false;
    readyToAutoAdvance = false;
    m_visibleChars = 0.0f;
    m_autoTimer = 0.0f;

    BuildTextLayout();

    vn::DialogueLine logEntry;
    logEntry.speaker = speaker;
    logEntry.text = m_text;
    logEntry.voicePath = voicePath;
    logEntry.lineId = lineId;

    bool isDuplicate = false;
    if (!m_log.empty()) {
        const vn::DialogueLine& last = m_log.back();
        if (!lineId.empty() && last.lineId == lineId) {
            isDuplicate = true;
        } else if (last.speaker == logEntry.speaker &&
                   last.text == logEntry.text &&
                   last.voicePath == logEntry.voicePath) {
            isDuplicate = true;
        }
    }

    if (!isDuplicate) {
        m_log.push_back(logEntry);
        if (static_cast<int>(m_log.size()) > MaxLog) {
            m_log.pop_front();
        }
    }
}

void DialogueBox::BuildTextLayout() {
    m_renderLines.clear();
    m_pages.clear();
    m_currentPageIdx = 0;
    m_totalChars = 0;
    m_lineAdvance = (m_font != nullptr)
        ? std::max(1, static_cast<int>(std::round(TTF_GetFontLineSkip(m_font) * std::max(0.8f, m_layout.dialogLineSpacing))))
        : 0;

    if (!m_font || m_text.empty()) {
        return;
    }

    const int wrapWidth = std::max(
        1,
        static_cast<int>(m_screenW - m_layout.dialogBoxOuterMarginX * 2.0f - m_layout.dialogTextPaddingX * 2.0f));

    std::vector<RenderLine> allLines;
    int globalCharIndex = 0;

    std::size_t paragraphStart = 0;
    while (paragraphStart <= m_text.size()) {
        const std::size_t newlinePos = m_text.find('\n', paragraphStart);
        const std::string paragraph = (newlinePos == std::string::npos)
            ? m_text.substr(paragraphStart)
            : m_text.substr(paragraphStart, newlinePos - paragraphStart);

        std::size_t cursor = 0;
        while (cursor < paragraph.size()) {
            int measuredWidth = 0;
            std::size_t measuredLength = 0;
            const bool measured = TTF_MeasureString(
                m_font,
                paragraph.c_str() + cursor,
                paragraph.size() - cursor,
                wrapWidth,
                &measuredWidth,
                &measuredLength
            );

            measuredLength = ClampToUTF8Boundary(paragraph, cursor, measuredLength);

            if (!measured || measuredLength == 0) {
                measuredLength = NextUTF8CharBytes(paragraph, cursor) - cursor;
            }

            std::size_t lineLength = measuredLength;
            if (cursor + lineLength < paragraph.size()) {
                std::size_t lastWhitespace = std::string::npos;
                for (std::size_t i = cursor; i < cursor + lineLength; ++i) {
                    if (IsWhitespaceByte(static_cast<unsigned char>(paragraph[i]))) {
                        lastWhitespace = i;
                    }
                }
                if (lastWhitespace != std::string::npos && lastWhitespace >= cursor) {
                    lineLength = lastWhitespace - cursor + 1;
                }
            }

            if (lineLength == 0) {
                lineLength = NextUTF8CharBytes(paragraph, cursor) - cursor;
            }

            if (cursor + lineLength < paragraph.size()) {
                const char32_t nextCodePoint = UTF8CodePointAt(paragraph, cursor + lineLength);
                if (IsLeadingPunctuation(nextCodePoint) && lineLength > 1) {
                    const std::size_t prevOffset = PrevUTF8CharOffset(paragraph, cursor + lineLength);
                    if (prevOffset > cursor) {
                        lineLength = prevOffset - cursor;
                    }
                }
            }

            std::string lineText = paragraph.substr(cursor, lineLength);

            RenderLine line;
            line.text = lineText;
            line.globalStartByte = paragraphStart + cursor;
            line.globalStartCharIndex = globalCharIndex;
            line.charCount = CountUTF8Chars(line.text);

            PopulateFragmentsForLine(line);

            const int outlineThickness = std::max(0, m_layout.dialogTextOutline);
            const int previousOutline = TTF_GetFontOutline(m_font);
            if (previousOutline != outlineThickness) {
                (void)TTF_SetFontOutline(m_font, outlineThickness);
            }

            int lineWidth = 0;
            int lineHeight = 0;
            (void)TTF_GetStringSize(m_font, line.text.c_str(), line.text.size(), &lineWidth, &lineHeight);
            line.height = std::max(line.height, static_cast<float>(lineHeight));

            if (previousOutline != outlineThickness) {
                (void)TTF_SetFontOutline(m_font, previousOutline);
            }

            line.advanceY = std::max(line.height, static_cast<float>(m_lineAdvance));
            globalCharIndex += line.charCount;
            allLines.push_back(std::move(line));

            cursor += lineLength;
        }

        if (newlinePos == std::string::npos) {
            break;
        }

        RenderLine breakLine;
        breakLine.isManualBreak = true;
        breakLine.charCount = 1;
        breakLine.globalStartByte = newlinePos;
        breakLine.globalStartCharIndex = globalCharIndex;
        breakLine.advanceY = static_cast<float>(std::max(1, m_lineAdvance));
        allLines.push_back(std::move(breakLine));
        ++globalCharIndex;

        paragraphStart = newlinePos + 1;
    }

    const float maxPageHeight = std::max(
        1.0f,
        m_layout.dialogBoxHeight - m_layout.dialogTextPaddingY * 2.0f
    );

    std::vector<RenderLine> currentPage;
    float pageHeight = 0.0f;

    for (RenderLine& line : allLines) {
        if (!currentPage.empty() && pageHeight + line.advanceY > maxPageHeight) {
            m_pages.push_back(std::move(currentPage));
            currentPage = {};
            pageHeight = 0.0f;
        }

        pageHeight += line.advanceY;
        currentPage.push_back(std::move(line));
    }

    if (!currentPage.empty()) {
        m_pages.push_back(std::move(currentPage));
    }

    LoadCurrentPage();
}

std::shared_ptr<SDL_Texture> DialogueBox::GetSpeakerNameTexture(const std::string& name) const {
    if (!m_assets || m_fontPath.empty() || !m_font || name.empty()) {
        return nullptr;
    }
    return m_assets->GetTextTexture(
        name,
        m_font,
        m_fontPath,
        m_fontPointSize,
        SDL_Color{ 255, 255, 255, 255 },
        std::max(0, m_layout.dialogTextOutline)
    );
}

void DialogueBox::ConsumeInlineEventsAtCurrentPosition() {
    while (m_nextInlineEventIndex < m_activeInlineEvents.size()) {
        const auto& event = m_activeInlineEvents[m_nextInlineEventIndex];

        if (m_visibleChars < static_cast<float>(event.charIndex)) {
            break;
        }

        switch (event.type) {
        case InlineEventType::Wait:
            m_inlinePauseRemaining = event.value;
            break;
        case InlineEventType::Click:
            m_waitingForInlineClick = true;
            break;
        case InlineEventType::SetCps:
            m_currentCharsPerSecond = event.value;
            break;
        }

        ++m_nextInlineEventIndex;

        if (m_inlinePauseRemaining > 0.0f || m_waitingForInlineClick) {
            break;
        }
    }
}

void DialogueBox::SkipInstantRevealCharsAtCurrentPosition() {
    const float visibleCharsFloor = std::floor(m_visibleChars);
    if (std::fabs(m_visibleChars - visibleCharsFloor) > 0.0001f) {
        return;
    }

    int localCharIndex = static_cast<int>(visibleCharsFloor);
    while (localCharIndex >= 0 &&
           localCharIndex < m_totalChars &&
           localCharIndex < static_cast<int>(m_activeInstantRevealChars.size()) &&
           m_activeInstantRevealChars[static_cast<std::size_t>(localCharIndex)]) {
        ++localCharIndex;
        m_visibleChars = static_cast<float>(localCharIndex);
    }
}

void DialogueBox::SyncRevealStateAtCurrentPosition() {
    while (true) {
        const float visibleBefore = m_visibleChars;
        const std::size_t eventIndexBefore = m_nextInlineEventIndex;

        ConsumeInlineEventsAtCurrentPosition();
        if (m_waitingForInlineClick || m_inlinePauseRemaining > 0.0f) {
            return;
        }

        SkipInstantRevealCharsAtCurrentPosition();

        if (std::fabs(m_visibleChars - visibleBefore) <= 0.0001f &&
            m_nextInlineEventIndex == eventIndexBefore) {
            return;
        }
    }
}

void DialogueBox::RevealAllTextImmediate() {
    m_visibleChars = static_cast<float>(m_totalChars);
    m_inlinePauseRemaining = 0.0f;
    m_waitingForInlineClick = false;
    m_nextInlineEventIndex = m_activeInlineEvents.size();
}

bool DialogueBox::RenderFrostedBackdrop(SDL_Renderer* renderer, const SDL_FRect& rect, Uint8 alpha) {
    if (!renderer || rect.w <= 1.0f || rect.h <= 1.0f || alpha == 0) {
        return false;
    }

    const SDL_Rect captureRect = ToBoundedRect(rect, m_screenW, m_screenH);
    if (captureRect.w <= 1 || captureRect.h <= 1) {
        return false;
    }

    const Uint64 now = SDL_GetTicks();
    const Uint64 captureIntervalMs = static_cast<Uint64>(
        std::lround(std::max(0.0f, m_layout.dialogFrostRefreshSeconds) * 1000.0f)
    );
    const bool needsRefresh = !m_frostedBackdropTexture ||
                              !SameRect(captureRect, m_frostedBackdropRect) ||
                              now - m_lastFrostedCaptureTick >= captureIntervalMs;

    if (needsRefresh) {
        SDL_Surface* captured = SDL_RenderReadPixels(renderer, &captureRect);
        if (captured) {
            const int downscale = std::max(1, static_cast<int>(std::lround(m_layout.dialogFrostDownscale)));
            const int blurW = std::max(1, captureRect.w / downscale);
            const int blurH = std::max(1, captureRect.h / downscale);
            SDL_Surface* downscaled = SDL_ScaleSurface(captured, blurW, blurH, SDL_SCALEMODE_LINEAR);
            SDL_Surface* upscaled = downscaled
                ? SDL_ScaleSurface(downscaled, captureRect.w, captureRect.h, SDL_SCALEMODE_LINEAR)
                : nullptr;

            SDL_Surface* sourceForTexture = upscaled ? upscaled : captured;
            vn::AssetManager::TextureHandle blurredTexture(
                SDL_CreateTextureFromSurface(renderer, sourceForTexture),
                vn::AssetManager::TextureDeleter{}
            );

            if (blurredTexture) {
                SDL_SetTextureBlendMode(blurredTexture.get(), SDL_BLENDMODE_BLEND);
                m_frostedBackdropTexture = std::move(blurredTexture);
                m_frostedBackdropRect = captureRect;
                m_lastFrostedCaptureTick = now;
            }

            if (upscaled) {
                SDL_DestroySurface(upscaled);
            }
            if (downscaled) {
                SDL_DestroySurface(downscaled);
            }
            SDL_DestroySurface(captured);
        }
    }

    if (!m_frostedBackdropTexture || !SameRect(captureRect, m_frostedBackdropRect)) {
        return false;
    }

    SDL_Texture* texture = m_frostedBackdropTexture.get();
    SDL_Color backdropTint = ui::theme::kTextPrimary;
    SDL_SetTextureColorMod(texture, backdropTint.r, backdropTint.g, backdropTint.b);
    SDL_SetTextureAlphaMod(texture, UnitAlpha((static_cast<float>(alpha) / 255.0f) * m_layout.dialogPanelAlphaTop));

    const SDL_FRect dst{
        static_cast<float>(captureRect.x),
        static_cast<float>(captureRect.y),
        static_cast<float>(captureRect.w),
        static_cast<float>(captureRect.h)
    };
    SDL_RenderTexture(renderer, texture, nullptr, &dst);
    SDL_SetTextureColorMod(texture, 255, 255, 255);
    SDL_SetTextureAlphaMod(texture, 255);
    return true;
}

void DialogueBox::RenderSpeakerPlate(SDL_Renderer* renderer, const SDL_FRect& rect, Uint8 alpha) const {
    if (!renderer || rect.w <= 1.0f || rect.h <= 1.0f || alpha == 0) {
        return;
    }

    DrawVerticalGradient(renderer,
                         rect,
                         WithScaledAlpha(ui::theme::kBlackElevated, alpha, m_layout.dialogSpeakerPlateAlphaTop),
                         WithScaledAlpha(ui::theme::kBlackBase, alpha, m_layout.dialogSpeakerPlateAlphaBottom));
    StrokeRect(renderer, rect, WithScaledAlpha(ui::theme::kBluePrimary, alpha, m_layout.dialogSpeakerPlateBorderAlpha));

    const float accentW = std::min(std::max(0.0f, m_layout.dialogSpeakerAccentWidth), rect.w);
    const float accentInsetY = std::min(rect.h * 0.5f, accentW * 0.75f);
    FillRect(renderer,
             SDL_FRect{ rect.x, rect.y + accentInsetY, accentW, std::max(1.0f, rect.h - accentInsetY * 2.0f) },
             WithScaledAlpha(ui::theme::kAccentSoft, alpha, m_layout.dialogSpeakerAccentAlpha));
}

void DialogueBox::RenderDialoguePanel(SDL_Renderer* renderer, const SDL_FRect& box, Uint8 alpha) {
    if (!renderer || box.w <= 1.0f || box.h <= 1.0f || alpha == 0) {
        return;
    }

    const float focusPadY = std::max(0.0f, m_layout.dialogFocusBandPaddingY);
    const float focusY = std::max(0.0f, box.y - focusPadY);
    const float focusBottom = std::min(static_cast<float>(m_screenH), box.y + box.h + focusPadY);
    const SDL_FRect focusBand{
        0.0f,
        focusY,
        static_cast<float>(m_screenW),
        std::max(1.0f, focusBottom - focusY)
    };
    DrawVerticalGradient(renderer,
                         focusBand,
                         WithScaledAlpha(ui::theme::kBlackAbyss, alpha, m_layout.dialogFocusBandTopAlpha),
                         WithScaledAlpha(ui::theme::kBlackAbyss, alpha, m_layout.dialogFocusBandAlpha));

    const SDL_FRect frostedRect = InflateRect(box,
                                              std::max(0.0f, m_layout.dialogFrostPaddingX),
                                              std::max(0.0f, m_layout.dialogFrostPaddingY));
    const bool usedFrostedBackdrop = RenderFrostedBackdrop(renderer, frostedRect, alpha);
    if (!usedFrostedBackdrop) {
        FillRect(renderer, frostedRect, WithScaledAlpha(ui::theme::kBlackPanel, alpha, m_layout.dialogPanelAlphaTop));
    }

    bool usedTexturedPanel = false;
    if (m_assets && !m_layout.dialogBoxTexturePath.empty()) {
        if (m_lastCachedPanelPath != m_layout.dialogBoxTexturePath) {
            m_cachedPanelTexture.reset();
            m_lastCachedPanelPath = m_layout.dialogBoxTexturePath;
        }
        if (!m_cachedPanelTexture) {
            m_cachedPanelTexture = m_assets->GetTexture(m_layout.dialogBoxTexturePath);
        }
        if (m_cachedPanelTexture) {
            usedTexturedPanel = Draw::Texture9Slice(
                renderer,
                m_cachedPanelTexture.get(),
                box,
                m_layout.dialogBoxSliceCorner,
                UnitAlpha((static_cast<float>(alpha) / 255.0f) * m_layout.dialogPanelTextureAlpha)
            );
        }
    }

    if (!usedTexturedPanel) {
        const SDL_FRect shadow = InflateRect(box,
                                             std::max(0.0f, m_layout.dialogPanelShadowPadding),
                                             std::max(0.0f, m_layout.dialogPanelShadowPadding));
        FillRect(renderer, shadow, WithScaledAlpha(ui::theme::kBlackAbyss, alpha, m_layout.dialogPanelShadowAlpha));

        DrawVerticalGradient(renderer,
                             box,
                             WithScaledAlpha(ui::theme::kBlackPanel, alpha, m_layout.dialogPanelAlphaTop),
                             WithScaledAlpha(ui::theme::kBlackBase, alpha, m_layout.dialogPanelAlphaBottom));

        const SDL_FRect innerGlow{
            box.x + 1.0f,
            box.y + 1.0f,
            std::max(1.0f, box.w - 2.0f),
            std::min(std::max(0.0f, m_layout.dialogPanelInnerGlowHeight), std::max(1.0f, box.h))
        };
        DrawVerticalGradient(renderer,
                             innerGlow,
                             WithScaledAlpha(ui::theme::kAccentSoft, alpha, m_layout.dialogPanelInnerGlowAlpha),
                             WithScaledAlpha(ui::theme::kAccentSoft, alpha, 0.00f));

        StrokeRect(renderer, box, WithScaledAlpha(ui::theme::kGraySteel, alpha, m_layout.dialogPanelBorderAlpha));
        const float highlightInsetX = std::min(box.w * 0.5f, std::max(0.0f, m_layout.dialogPanelHighlightInsetX));
        DrawLine(renderer,
                 box.x + highlightInsetX,
                 box.y + 1.0f,
                 box.x + box.w - highlightInsetX,
                 box.y + 1.0f,
                 WithScaledAlpha(ui::theme::kAccentSoft, alpha, m_layout.dialogPanelHighlightAlpha));
        DrawLine(renderer,
                 box.x + 1.0f,
                 box.y + box.h - 1.0f,
                 box.x + box.w - 1.0f,
                 box.y + box.h - 1.0f,
                 WithScaledAlpha(ui::theme::kBlackAbyss, alpha, m_layout.dialogPanelShadowAlpha));
    }
}

void DialogueBox::Render(SDL_Renderer* renderer) {
    if (m_boxVisibilityState == VisibilityState::Hidden || m_boxAlpha <= 0.01f || !renderer) {
        return;
    }

    const Uint8 alpha = static_cast<Uint8>(m_boxAlpha * 255.0f);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    const float boxW = static_cast<float>(m_screenW) - m_layout.dialogBoxOuterMarginX * 2.0f;
    const float boxX = m_layout.dialogBoxOuterMarginX;
    const float boxH = m_layout.dialogBoxHeight;
    const float boxY = static_cast<float>(m_screenH) - boxH - m_layout.dialogBoxOuterBottom;
    const SDL_FRect dialoguePanel{ boxX, boxY, boxW, boxH };
    RenderDialoguePanel(renderer, dialoguePanel, alpha);

    if (!m_speaker.empty()) {
        auto nameTex = m_speakerTexture;
        if (nameTex) {
            float nw = 0.0f;
            float nh = 0.0f;
            SDL_GetTextureSize(nameTex.get(), &nw, &nh);

            const SDL_FRect namePlate = {
                boxX + m_layout.dialogTextPaddingX - m_layout.dialogNamePaddingX,
                boxY - m_layout.dialogNameOffsetY - m_layout.dialogNamePaddingY,
                nw + m_layout.dialogNamePaddingX * 2.0f,
                nh + m_layout.dialogNamePaddingY * 2.0f
            };
            RenderSpeakerPlate(renderer, namePlate, alpha);

            const SDL_FRect nameDst = {
                boxX + m_layout.dialogTextPaddingX,
                boxY - m_layout.dialogNameOffsetY,
                nw,
                nh
            };

            SDL_SetTextureAlphaMod(nameTex.get(), alpha);
            SDL_RenderTexture(renderer, nameTex.get(), nullptr, &nameDst);
        }
    }

    if (m_font && !m_renderLines.empty()) {
        float currentY = boxY + m_layout.dialogTextPaddingY;
        int charsDrawn = 0;

        const int fullyVisibleChars = static_cast<int>(std::floor(m_visibleChars));
        const float revealFraction = m_visibleChars - static_cast<float>(fullyVisibleChars);

        for (const auto& line : m_renderLines) {
            if (charsDrawn > fullyVisibleChars) {
                break;
            }

            float currentX = boxX + m_layout.dialogTextPaddingX;

            for (const auto& fragment : line.fragments) {
                if (charsDrawn > fullyVisibleChars) {
                    break;
                }

                auto texHandle = fragment.texture;
                if (texHandle) {
                    SDL_Texture* tex = texHandle.get();
                    float texW = 0.0f;
                    float texH = 0.0f;
                    SDL_GetTextureSize(tex, &texW, &texH);

                    if (charsDrawn + fragment.charCount <= fullyVisibleChars) {
                        // Fully revealed fragment.
                        int clipW = fragment.revealClipWidths.empty() ? 0 : fragment.revealClipWidths.back();
                        if (clipW > 0) {
                            SDL_SetTextureAlphaMod(tex, alpha);
                            SDL_FRect clipDst = { currentX, currentY, static_cast<float>(clipW), texH };
                            SDL_FRect srcClip = { 0.0f, 0.0f, static_cast<float>(clipW), texH };
                            SDL_RenderTexture(renderer, tex, &srcClip, &clipDst);
                        }
                    } else {
                        int localFullChars = fullyVisibleChars - charsDrawn;
                        if (localFullChars < 0) {
                            localFullChars = 0;
                        }

                        int baseW = 0;
                        if (localFullChars < static_cast<int>(fragment.revealClipWidths.size())) {
                            baseW = fragment.revealClipWidths[localFullChars];
                        }

                        // Draw fully visible glyphs in this fragment.
                        if (baseW > 0) {
                            SDL_SetTextureAlphaMod(tex, alpha);
                            SDL_FRect clipDst = { currentX, currentY, static_cast<float>(baseW), texH };
                            SDL_FRect srcClip = { 0.0f, 0.0f, static_cast<float>(baseW), texH };
                            SDL_RenderTexture(renderer, tex, &srcClip, &clipDst);
                        }

                        // Fade in only the currently revealing glyph.
                        if (revealFraction > 0.0f &&
                            localFullChars + 1 < static_cast<int>(fragment.revealClipWidths.size())) {
                            int nextW = fragment.revealClipWidths[localFullChars + 1];
                            int charW = nextW - baseW;
                            if (charW > 0) {
                                Uint8 fadeAlpha = static_cast<Uint8>(std::clamp(
                                    static_cast<float>(alpha) * revealFraction,
                                    0.0f,
                                    255.0f
                                ));
                                SDL_SetTextureAlphaMod(tex, fadeAlpha);

                                SDL_FRect clipDst = {
                                    currentX + static_cast<float>(baseW),
                                    currentY,
                                    static_cast<float>(charW),
                                    texH
                                };
                                SDL_FRect srcClip = {
                                    static_cast<float>(baseW),
                                    0.0f,
                                    static_cast<float>(charW),
                                    texH
                                };
                                SDL_RenderTexture(renderer, tex, &srcClip, &clipDst);
                            }
                        }
                    }
                }
                currentX += fragment.width;
                charsDrawn += fragment.charCount;
            }
            currentY += line.advanceY;
        }
    }

    if (isDone && !autoMode && !m_waitingForInlineClick) {
        const float ctcX = boxX + boxW - m_layout.dialogCtcRight;
        const float ctcY = boxY + boxH - m_layout.dialogCtcBottom;

        const float bounce = std::sin(SDL_GetTicks() / 150.0f) * 4.0f;
        SDL_SetRenderDrawColor(
            renderer,
            ui::theme::kAccentPrimary.r,
            ui::theme::kAccentPrimary.g,
            ui::theme::kAccentPrimary.b,
            alpha
        );

        const float alphaF = static_cast<float>(alpha) / 255.0f;
        const SDL_Vertex vertices[3] = {
            { { ctcX - 6.0f, ctcY + bounce }, { 1.0f, 1.0f, 1.0f, alphaF }, { 0.0f, 0.0f } },
            { { ctcX + 6.0f, ctcY + bounce }, { 1.0f, 1.0f, 1.0f, alphaF }, { 0.0f, 0.0f } },
            { { ctcX, ctcY + 6.0f + bounce }, { 1.0f, 1.0f, 1.0f, alphaF }, { 0.0f, 0.0f } }
        };
        SDL_RenderGeometry(renderer, nullptr, vertices, 3, nullptr, 0);
    }
}
