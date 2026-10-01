#pragma once

#include <hyprland/src/helpers/Color.hpp>

#include <optional>
#include <string>

namespace Theme {
    // Resolve a color setting: a key in the active Omarchy theme's colors.toml
    // ("accent", "foreground", "color4", ...) or a literal #rrggbb, #rrggbbaa,
    // 0xaarrggbb or rgba(rrggbbaa). colors.toml is re-read whenever its mtime
    // changes, so a theme switch recolors the next ripple with no reload.
    CHyprColor resolve(const std::string& setting);

    // Parse a literal color; nullopt if it isn't one.
    std::optional<CHyprColor> parseLiteral(const std::string& value);

    // Path of the active theme's colors.toml (for diagnostics).
    std::string colorsPath();
}
