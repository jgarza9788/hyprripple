#pragma once

// Pure logic for HyprRipple: no Hyprland headers, so native/tests can link it
// on its own. Settings validation, ripple timing, geometry, class matching,
// color parsing and JSON escaping all live here.

#include <array>
#include <optional>
#include <string>
#include <string_view>

namespace Logic {

    // ── settings ─────────────────────────────────────────────────────────────

    struct Settings {
        bool        enabled          = true;
        std::string color            = "accent";  // a colors.toml key, or #rrggbb / #rrggbbaa / rgba(...) / 0xaarrggbb
        float       opacity          = 0.28f;
        int         duration         = 550;       // ms
        float       radius           = 140.f;     // logical px, click ripples
        std::string workspaceRadius  = "screen";  // "screen" or a number of logical px
        float       softness         = 2.0f;      // logical px
        std::string style            = "classic"; // classic | ring | sparkle
        float       ringWidth        = 6.0f;      // logical px, style = ring
        float       sparkleAmount    = 0.5f;      // 0..1, style = sparkle
        std::string onClick          = "left";    // none | left | any
        bool        onTouch          = true;      // touchscreen / tablet taps
        bool        hold             = true;      // ink grows while held, fades on release
        bool        onFocus          = true;
        bool        focusHover       = false;     // also ripple on focus-follows-mouse
        bool        onWorkspace      = true;
        std::string focusOrigin      = "cursor";  // cursor | center
        bool        bounded          = true;      // focus ripples clipped to the focused window
        int         maxRipples       = 16;
        bool        skipFullscreen   = true;      // no ripples on a monitor showing a fullscreen window
        std::string exclude          = "";        // comma-separated window class globs
        bool        pauseWhenSharing = false;     // no ripples while the screen is being shared
    };

    inline constexpr size_t MAX_VALUE_LEN   = 256;
    inline constexpr size_t MAX_EXCLUDE_LEN = 512;

    // Validate and store one setting. Returns an error message, "" on success.
    // Numbers are clamped to sane ranges; strings are checked against a
    // whitelist so nothing odd can reach JSON output or the hyprctl batch.
    std::string applySetting(Settings& s, const std::string& key, const std::string& value);

    std::optional<bool>  parseBool(std::string_view v);
    std::optional<float> parseFloat(std::string_view v);

    // 0 classic, 1 ring, 2 sparkle
    int styleMode(const std::string& style);

    // ── timing ───────────────────────────────────────────────────────────────

    // Longest a held ripple waits for its release before fading anyway, so a
    // lost release event can't leave ink on screen.
    inline constexpr float MAX_HOLD_S = 8.f;

    struct Life {
        int   mode       = 0;     // styleMode
        float duration   = 0.55f; // seconds
        float elapsed    = 0.f;   // seconds since spawn
        bool  held       = false; // waits for a release before fading
        float releasedAt = -1.f;  // seconds since spawn, < 0 = not released
    };

    float spreadFraction(const Life& l); // current radius / max radius
    float fade(const Life& l);           // 0 until the fade starts, then -> 1
    float alpha(const Life& l, float opacity);
    bool  finished(const Life& l);

    // Material 3 standard easing, cubic-bezier(0.2, 0, 0, 1).
    float m3Ease(float x);

    // ── geometry ─────────────────────────────────────────────────────────────

    struct Vec {
        double x = 0, y = 0;
    };
    struct Rect {
        double x = 0, y = 0, w = 0, h = 0;
    };

    // Distance from p to the farthest corner of r: a radius that floods it.
    double coverRadius(Vec p, Rect r);
    // The point on r's border nearest p (p clamped into r first): where the
    // mouse crossed into a window it just entered.
    Vec entryPoint(Rect r, Vec p);

    // ── window class exclusion ───────────────────────────────────────────────

    // Case-insensitive glob: * any run, ? one char. Iterative, no regex, so a
    // hostile pattern can't blow up matching time.
    bool globMatch(std::string_view pattern, std::string_view text);
    // `list` is the comma-separated `exclude` setting.
    bool classExcluded(std::string_view list, std::string_view cls);

    // ── colors / output ──────────────────────────────────────────────────────

    // rgba in 0..1, or nullopt if `value` isn't a literal color.
    std::optional<std::array<double, 4>> parseColorLiteral(std::string_view value);
    // A colors.toml key: [A-Za-z_][A-Za-z0-9_]*, at most 64 chars.
    bool isColorKey(std::string_view value);

    std::string jsonEscape(std::string_view s);
}
