#include "Logic.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <format>
#include <initializer_list>
#include <limits>
#include <vector>

namespace Logic {

    // ── settings ─────────────────────────────────────────────────────────────

    std::optional<bool> parseBool(std::string_view v) {
        if (v == "1" || v == "true" || v == "yes" || v == "on")
            return true;
        if (v == "0" || v == "false" || v == "no" || v == "off")
            return false;
        return std::nullopt;
    }

    std::optional<float> parseFloat(std::string_view v) {
        if (v.empty() || v.size() > 32)
            return std::nullopt;
        float f = 0;
        // from_chars rejects leading '+'/spaces and never touches locale
        auto [ptr, ec] = std::from_chars(v.data(), v.data() + v.size(), f);
        if (ec != std::errc{} || ptr != v.data() + v.size() || !std::isfinite(f))
            return std::nullopt;
        return f;
    }

    int styleMode(const std::string& style) {
        if (style == "ring")
            return 1;
        if (style == "sparkle")
            return 2;
        if (style == "negative")
            return MODE_NEGATIVE;
        if (style == "water")
            return MODE_WATER;
        if (style == "prism")
            return MODE_PRISM;
        return 0;
    }

    bool isSamplingMode(int mode) {
        return mode >= MODE_NEGATIVE && mode <= MODE_PRISM;
    }

    bool isWaveMode(int mode) {
        return mode == MODE_WATER || mode == MODE_PRISM;
    }

    float waveReach(int mode, float maxRadius, float distortion) {
        if (!isWaveMode(mode))
            return 0.f;
        // must match the shader's amplitude (ripple.frag: waveAmp)
        // prism's blue channel samples at 2x the offset
        return std::clamp(distortion, 0.f, 1.f) * std::clamp(maxRadius * 0.12f, 6.f, 48.f) * (mode == MODE_PRISM ? 2.0f : 1.f);
    }

    bool isColorKey(std::string_view v) {
        if (v.empty() || v.size() > 64 || !(std::isalpha((unsigned char)v[0]) || v[0] == '_'))
            return false;
        return std::ranges::all_of(v, [](unsigned char c) { return std::isalnum(c) || c == '_'; });
    }

    static bool validExcludeList(std::string_view v) {
        if (v.size() > MAX_EXCLUDE_LEN)
            return false;
        return std::ranges::all_of(v, [](unsigned char c) { return std::isalnum(c) || c == '.' || c == '_' || c == '-' || c == '*' || c == '?' || c == ',' || c == ' '; });
    }

    std::string applySetting(Settings& s, const std::string& key, const std::string& value) {
        if (value.size() > MAX_VALUE_LEN && key != "exclude")
            return std::format("{}: value too long", key);

        auto asBool = [&](bool& out) -> std::string {
            auto b = parseBool(value);
            if (!b)
                return std::format("{}: expected true/false", key);
            out = *b;
            return "";
        };
        auto asFloat = [&](float& out, float lo, float hi) -> std::string {
            auto f = parseFloat(value);
            if (!f)
                return std::format("{}: expected a number", key);
            out = std::clamp(*f, lo, hi);
            return "";
        };
        auto asInt = [&](int& out, int lo, int hi) -> std::string {
            auto f = parseFloat(value);
            if (!f)
                return std::format("{}: expected a number", key);
            out = (int)std::lround(std::clamp(*f, (float)lo, (float)hi));
            return "";
        };
        auto asEnum = [&](std::string& out, std::initializer_list<std::string_view> allowed) -> std::string {
            if (std::ranges::find(allowed, value) == allowed.end())
                return std::format("{}: unknown value", key);
            out = value;
            return "";
        };

        if (key == "enabled")
            return asBool(s.enabled);
        if (key == "on_touch")
            return asBool(s.onTouch);
        if (key == "hold")
            return asBool(s.hold);
        if (key == "on_focus")
            return asBool(s.onFocus);
        if (key == "focus_hover")
            return asBool(s.focusHover);
        if (key == "on_workspace")
            return asBool(s.onWorkspace);
        if (key == "bounded")
            return asBool(s.bounded);
        if (key == "skip_fullscreen")
            return asBool(s.skipFullscreen);
        if (key == "pause_when_sharing")
            return asBool(s.pauseWhenSharing);
        if (key == "opacity")
            return asFloat(s.opacity, 0.f, 1.f);
        if (key == "radius")
            return asFloat(s.radius, 4.f, 4000.f);
        if (key == "softness")
            return asFloat(s.softness, 0.f, 200.f);
        if (key == "ring_width")
            return asFloat(s.ringWidth, 1.f, 200.f);
        if (key == "sparkle_amount")
            return asFloat(s.sparkleAmount, 0.f, 1.f);
        if (key == "distortion")
            return asFloat(s.distortion, 0.f, 1.f);
        if (key == "duration")
            return asInt(s.duration, 50, 5000);
        if (key == "max_ripples")
            return asInt(s.maxRipples, 1, 64);
        if (key == "on_click")
            return asEnum(s.onClick, {"none", "left", "any"});
        if (key == "style") {
            if (value == "fill") { // pre-preset name
                s.style = "classic";
                return "";
            }
            return asEnum(s.style, {"classic", "ring", "sparkle", "negative", "water", "prism"});
        }
        if (key == "focus_origin")
            return asEnum(s.focusOrigin, {"cursor", "center"});
        if (key == "workspace_radius") {
            if (value == "screen") {
                s.workspaceRadius = value;
                return "";
            }
            auto r = parseFloat(value);
            if (!r || *r <= 0)
                return "workspace_radius: expected 'screen' or a positive number";
            s.workspaceRadius = std::to_string((int)std::lround(std::clamp(*r, 10.f, 20000.f)));
            return "";
        }
        if (key == "color") {
            if (value.empty()) {
                s.color = "accent";
                return "";
            }
            if (!isColorKey(value) && !parseColorLiteral(value))
                return "color: expected a theme color name or #rrggbb";
            s.color = value;
            return "";
        }
        if (key == "exclude") {
            if (!validExcludeList(value))
                return "exclude: only letters, digits, . _ - * ? , and spaces (max 512)";
            s.exclude = value;
            return "";
        }
        return "unknown key";
    }

    // ── timing ───────────────────────────────────────────────────────────────

    float m3Ease(float x) {
        x = std::clamp(x, 0.f, 1.f);
        // Solve bezier x(u) = x by Newton, then return y(u).
        constexpr float x1 = 0.2f, y1 = 0.f, x2 = 0.f, y2 = 1.f;
        auto            bx = [&](float u) { return 3 * (1 - u) * (1 - u) * u * x1 + 3 * (1 - u) * u * u * x2 + u * u * u; };
        auto            by = [&](float u) { return 3 * (1 - u) * (1 - u) * u * y1 + 3 * (1 - u) * u * u * y2 + u * u * u; };
        float           u  = x;
        for (int i = 0; i < 8; ++i) {
            const float dx = 3 * (1 - u) * (1 - u) * x1 + 6 * (1 - u) * u * (x2 - x1) + 3 * u * u * (1 - x2);
            if (std::abs(dx) < 1e-5f)
                break;
            u = std::clamp(u - (bx(u) - x) / dx, 0.f, 1.f);
        }
        return by(u);
    }

    static float duration(const Life& l) {
        return std::max(l.duration, 0.05f);
    }

    // Fraction of the life spent spreading / when the fade would start for an
    // ordinary tap. Sparkle follows Material 3's longer spread.
    static float spreadLen(const Life& l) {
        return duration(l) * (l.mode == 2 ? 0.75f : 0.7f);
    }
    static float nominalFadeStart(const Life& l) {
        return duration(l) * (l.mode == 2 ? 0.55f : 0.45f);
    }
    static float fadeLen(const Life& l) {
        return duration(l) - nominalFadeStart(l);
    }

    // When the fade begins. A held ripple waits for its release (but never
    // longer than MAX_HOLD_S); a quick tap fades on the normal schedule.
    static float fadeStart(const Life& l) {
        float release = 0.f;
        if (l.held) {
            if (l.releasedAt >= 0.f)
                release = std::min(l.releasedAt, MAX_HOLD_S);
            else if (l.elapsed >= MAX_HOLD_S)
                release = MAX_HOLD_S;
            else
                return std::numeric_limits<float>::infinity();
        }
        return std::max(nominalFadeStart(l), release);
    }

    float spreadFraction(const Life& l) {
        const float e = std::clamp(l.elapsed / spreadLen(l), 0.f, 1.f);
        if (l.mode == 2) // M3: start at 20% of the final size
            return 0.2f + 0.8f * m3Ease(e);
        if (isWaveMode(l.mode)) // waves travel at a near-constant speed
            return 0.04f + 0.96f * (1.f - (1.f - e) * (1.f - e) * 0.35f - (1.f - e) * 0.65f);
        // Material's standard decelerate, from a small seed
        return 0.08f + 0.92f * (1.f - std::pow(1.f - e, 3.f));
    }

    float fade(const Life& l) {
        const float start = fadeStart(l);
        if (!std::isfinite(start))
            return 0.f;
        return std::clamp((l.elapsed - start) / fadeLen(l), 0.f, 1.f);
    }

    float alpha(const Life& l, float opacity) {
        const float in  = std::clamp(l.elapsed / (duration(l) * (l.mode == 2 ? 0.04f : 0.06f)), 0.f, 1.f);
        const float x   = fade(l);
        const float out = 1.f - x * x * (3.f - 2.f * x);
        return std::clamp(opacity, 0.f, 1.f) * in * out;
    }

    bool finished(const Life& l) {
        return fade(l) >= 1.f;
    }

    // ── geometry ─────────────────────────────────────────────────────────────

    double coverRadius(Vec p, Rect r) {
        const double dx = std::max(std::abs(p.x - r.x), std::abs(p.x - (r.x + r.w)));
        const double dy = std::max(std::abs(p.y - r.y), std::abs(p.y - (r.y + r.h)));
        return std::sqrt(dx * dx + dy * dy);
    }

    Vec entryPoint(Rect r, Vec p) {
        const double x  = std::clamp(p.x, r.x, r.x + r.w);
        const double y  = std::clamp(p.y, r.y, r.y + r.h);
        const double dl = x - r.x, dr = r.x + r.w - x, dt = y - r.y, db = r.y + r.h - y;
        const double m  = std::min({dl, dr, dt, db});
        if (m == dl)
            return {r.x, y};
        if (m == dr)
            return {r.x + r.w, y};
        if (m == dt)
            return {x, r.y};
        return {x, r.y + r.h};
    }

    // ── class exclusion ──────────────────────────────────────────────────────

    bool globMatch(std::string_view pat, std::string_view text) {
        // Classic two-pointer wildcard match with single backtrack point:
        // O(len(pat) * len(text)) worst case.
        size_t p = 0, t = 0, starP = std::string_view::npos, starT = 0;
        auto   eq = [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); };
        while (t < text.size()) {
            if (p < pat.size() && (pat[p] == '?' || (pat[p] != '*' && eq(pat[p], text[t])))) {
                ++p;
                ++t;
            } else if (p < pat.size() && pat[p] == '*') {
                starP = p++;
                starT = t;
            } else if (starP != std::string_view::npos) {
                p = starP + 1;
                t = ++starT;
            } else
                return false;
        }
        while (p < pat.size() && pat[p] == '*')
            ++p;
        return p == pat.size();
    }

    bool classExcluded(std::string_view list, std::string_view cls) {
        if (cls.empty())
            return false;
        size_t start = 0;
        while (start <= list.size()) {
            size_t end = list.find(',', start);
            if (end == std::string_view::npos)
                end = list.size();
            std::string_view item = list.substr(start, end - start);
            while (!item.empty() && item.front() == ' ')
                item.remove_prefix(1);
            while (!item.empty() && item.back() == ' ')
                item.remove_suffix(1);
            if (!item.empty() && globMatch(item, cls))
                return true;
            start = end + 1;
        }
        return false;
    }

    // ── rate limiting ────────────────────────────────────────────────────────

    bool RateLimiter::allow(float now) {
        if (last < 0.f || now < last) // first call, or the clock went backwards
            last = now;
        tokens = std::min(burst, tokens + (now - last) * perSecond);
        last   = now;
        if (tokens < 1.f)
            return false;
        tokens -= 1.f;
        return true;
    }

    // ── colors / output ──────────────────────────────────────────────────────

    static std::optional<uint32_t> parseHex(std::string_view hex) {
        if (hex.empty() || hex.size() > 8)
            return std::nullopt;
        uint32_t out = 0;
        auto [ptr, ec] = std::from_chars(hex.data(), hex.data() + hex.size(), out, 16);
        if (ec != std::errc{} || ptr != hex.data() + hex.size())
            return std::nullopt;
        return out;
    }

    std::optional<std::array<double, 4>> parseColorLiteral(std::string_view raw) {
        std::string value;
        for (char c : raw)
            if (!std::isspace((unsigned char)c))
                value += (char)std::tolower((unsigned char)c);
        if (value.size() > 32)
            return std::nullopt;

        auto rgba = [](uint32_t r, uint32_t g, uint32_t b, uint32_t a) { return std::array<double, 4>{r / 255.0, g / 255.0, b / 255.0, a / 255.0}; };

        // rgba(rrggbbaa) / rgb(rrggbb), Hyprland style
        if (value.starts_with("rgba(") && value.ends_with(")"))
            value = "#" + value.substr(5, value.size() - 6);
        else if (value.starts_with("rgb(") && value.ends_with(")"))
            value = "#" + value.substr(4, value.size() - 5);

        if (value.starts_with("0x") && value.size() == 10) { // 0xaarrggbb
            auto v = parseHex(std::string_view{value}.substr(2));
            if (!v)
                return std::nullopt;
            return rgba((*v >> 16) & 0xff, (*v >> 8) & 0xff, *v & 0xff, (*v >> 24) & 0xff);
        }

        if (!value.starts_with("#"))
            return std::nullopt;

        const std::string_view hex = std::string_view{value}.substr(1);
        auto                   v   = parseHex(hex);
        if (!v)
            return std::nullopt;
        switch (hex.size()) {
            case 3: return rgba(((*v >> 8) & 0xf) * 17, ((*v >> 4) & 0xf) * 17, (*v & 0xf) * 17, 255);
            case 6: return rgba((*v >> 16) & 0xff, (*v >> 8) & 0xff, *v & 0xff, 255);
            case 8: return rgba((*v >> 24) & 0xff, (*v >> 16) & 0xff, (*v >> 8) & 0xff, *v & 0xff);
            default: return std::nullopt;
        }
    }

    std::string jsonEscape(std::string_view s) {
        std::string out;
        out.reserve(s.size());
        for (unsigned char c : s) {
            switch (c) {
                case '"': out += "\\\""; break;
                case '\\': out += "\\\\"; break;
                case '\n': out += "\\n"; break;
                case '\r': out += "\\r"; break;
                case '\t': out += "\\t"; break;
                default:
                    if (c < 0x20)
                        out += std::format("\\u{:04x}", c);
                    else
                        out += (char)c;
            }
        }
        return out;
    }
}
