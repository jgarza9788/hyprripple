#pragma once

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/helpers/Color.hpp>
#include <hyprland/src/helpers/math/Math.hpp>
#include <hyprland/src/desktop/DesktopTypes.hpp>

#include <chrono>
#include <string>
#include <string_view>

inline constexpr std::string_view PLUGIN_NAME        = "hyprripple";
inline constexpr std::string_view PLUGIN_DESCRIPTION = "Material Design ink ripple in your theme's accent color";
inline constexpr std::string_view PLUGIN_AUTHOR      = "jgarza";
inline constexpr std::string_view PLUGIN_VERSION     = "0.1.0";

inline HANDLE PHANDLE = nullptr;

// Live settings. Filled from the plugin:hyprripple:* config values on every
// config reload, and poked directly by `hyprctl hyprripple set ...` so the
// Omarchy panel can change things without a reload.
struct SSettings {
    bool        enabled         = true;
    std::string color           = "accent"; // a colors.toml key, or #rrggbb / #rrggbbaa / rgba(...)
    float       opacity         = 0.28f;
    int         duration        = 550;      // ms
    float       radius          = 140.f;    // logical px, click ripples
    std::string workspaceRadius = "screen"; // "screen" or a number of logical px
    float       softness        = 2.0f;     // logical px
    std::string style           = "classic"; // classic | ring | sparkle
    float       ringWidth       = 6.0f;     // logical px, style = ring
    std::string onClick         = "left";   // none | left | any
    bool        onFocus         = true;
    bool        focusHover      = false;    // also ripple on focus-follows-mouse
    bool        onWorkspace     = true;
    std::string focusOrigin     = "cursor"; // cursor | center
    bool        bounded         = true;     // focus ripples clipped to the focused window
    int         maxRipples      = 16;
};

struct SRipple {
    Vector2D                              center;    // global logical
    float                                 maxRadius; // logical
    std::chrono::steady_clock::time_point start;
    float                                 duration;  // seconds
    float                                 opacity;
    CHyprColor                            color;
    int                                   mode      = 0;    // 0 classic, 1 ring, 2 sparkle
    float                                 seed      = 0.f;  // per-ripple noise offset
    float                                 ringWidth = 0.f;
    float                                 softness  = 2.f;
    bool                                  clip      = false;
    CBox                                  clipBox;          // global logical
    float                                 clipRounding = 0.f;

    // Window the ripple rides on. While it's alive the ripple keeps the same
    // offset from the window's top-left and (if clipped) its live box.
    PHLWINDOWREF                          anchor;
    Vector2D                              anchorRel; // center - window box top-left

    // 0..1 over the ripple's life
    float progress(std::chrono::steady_clock::time_point now) const;
    // Material motion: fast decelerating spread, then a separate fade out.
    float currentRadius(float t) const;
    float currentAlpha(float t) const;
    // Everything the ripple can touch this frame, global logical.
    CBox  bounds() const;
};
