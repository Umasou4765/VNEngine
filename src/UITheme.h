#pragma once

#include <SDL3/SDL.h>
#include <cstdint>

namespace ui::theme {

// 1) Deep cold blacks with a subtle blue tint.
inline constexpr SDL_Color kBlackAbyss{ 4, 6, 10, 255 };
inline constexpr SDL_Color kBlackBase{ 10, 14, 20, 255 };
inline constexpr SDL_Color kBlackPanel{ 16, 22, 30, 255 };
inline constexpr SDL_Color kBlackElevated{ 24, 32, 44, 255 };

// 2) Detailed gray-blue transitions for borders, hover, and fade layers.
inline constexpr SDL_Color kGrayGloom{ 34, 46, 60, 255 };
inline constexpr SDL_Color kGraySteel{ 52, 68, 86, 255 };
inline constexpr SDL_Color kGrayMist{ 76, 94, 114, 255 };

// 3) Muted cold blues for primary interactive states.
inline constexpr SDL_Color kBlueMuted{ 42, 60, 80, 255 };
inline constexpr SDL_Color kBlueStrong{ 50, 80, 110, 255 };
inline constexpr SDL_Color kBluePrimary{ 65, 105, 135, 255 };

// 4) Frosted accents for focus rings, highlights, and active controls.
inline constexpr SDL_Color kAccentPrimary{ 100, 180, 210, 255 };
inline constexpr SDL_Color kAccentSoft{ 170, 215, 235, 255 };

// 5) Frosted text colors with no warm parchment bias.
inline constexpr SDL_Color kTextPrimary{ 215, 225, 235, 255 };
inline constexpr SDL_Color kTextSecondary{ 140, 155, 175, 255 };
inline constexpr SDL_Color kTextMuted{ 90, 105, 125, 255 };
inline constexpr SDL_Color kTextDisabled{ 50, 60, 75, 255 };

inline SDL_Color WithAlpha(SDL_Color color, Uint8 alpha) {
    color.a = alpha;
    return color;
}

inline Uint8 ScaleAlpha(Uint8 alpha, float factor) {
    const float scaled = static_cast<float>(alpha) * factor;
    if (scaled <= 0.0f) {
        return 0;
    }
    if (scaled >= 255.0f) {
        return 255;
    }
    return static_cast<Uint8>(scaled);
}

} // namespace ui::theme
