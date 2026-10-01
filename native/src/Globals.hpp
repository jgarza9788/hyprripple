#pragma once

#include "Logic.hpp"

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

inline constexpr std::string_view PLUGIN_NAME        = "hyprripple";
inline constexpr std::string_view PLUGIN_DESCRIPTION = "Material Design ink ripple in your theme's accent color";
inline constexpr std::string_view PLUGIN_AUTHOR      = "jgarza";
inline constexpr std::string_view PLUGIN_VERSION     = "0.2.0";

inline HANDLE PHANDLE = nullptr;

// Live settings. Filled from the plugin:hyprripple:* config values on every
// config reload, and poked directly by `hyprctl hyprripple set ...` so the
// Omarchy panel can change things without a reload.
using SSettings = Logic::Settings;

struct SRipple {
    Vector2D                              center;    // global logical
    float                                 maxRadius; // logical
    std::chrono::steady_clock::time_point start;
    float                                 duration;  // seconds
    float                                 opacity;
    CHyprColor                            color;
    int                                   mode          = 0;   // 0 classic, 1 ring, 2 sparkle
    float                                 seed          = 0.f; // per-ripple noise offset
    float                                 ringWidth     = 0.f;
    float                                 sparkleAmount = 0.5f;
    float                                 softness      = 2.f;
    bool                                  clip          = false;
    CBox                                  clipBox;              // global logical
    float                                 clipRounding = 0.f;
    bool                                  above        = false; // drawn above bars/launchers (pressed on one)

    // Press-and-hold: a held ripple keeps its ink until the matching release.
    bool                                                 held    = false;
    uint64_t                                             holdKey = 0; // which button / touch point
    std::optional<std::chrono::steady_clock::time_point> released;

    // Window the ripple rides on. While it's alive the ripple keeps the same
    // offset from the window's top-left and (if clipped) its live box.
    PHLWINDOWREF anchor;
    Vector2D     anchorRel; // center - window box top-left

    // Damage last drawn, so a ripple that moves also clears where it was.
    std::optional<CBox> lastBounds;

    Logic::Life life(std::chrono::steady_clock::time_point now) const;
    // Everything the ripple can touch at full size, global logical.
    CBox bounds() const;
};

// Hold keys: mouse buttons use their evdev code as-is.
inline constexpr uint64_t HOLD_TOUCH  = 1ull << 32;
inline constexpr uint64_t HOLD_TABLET = 2ull << 32;
