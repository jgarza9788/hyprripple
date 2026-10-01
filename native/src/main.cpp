// HyprRipple - Material Design ink ripples for Hyprland.
//
// Triggers (each optional): mouse button press, window focus change,
// workspace switch. Ripples are drawn with a custom GLSL pass element right
// after windows (under bars/overlays) in the active Omarchy theme's accent.

#include "Globals.hpp"
#include "RipplePassElement.hpp"
#include "Theme.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/config/ConfigManager.hpp>
#include <hyprland/src/config/values/ConfigValues.hpp>
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/state/FocusState.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopTimer.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprutils/signal/Listener.hpp>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <algorithm>
#include <cmath>
#include <deque>
#include <format>
#include <sstream>

using namespace std::chrono;
namespace CV = Config::Values;

// ── ripple timing ────────────────────────────────────────────────────────────

float SRipple::progress(steady_clock::time_point now) const {
    const float t = std::chrono::duration<float>(now - start).count() / std::max(duration, 0.016f);
    return std::clamp(t, 0.f, 1.f);
}

// Material 3's standard easing, cubic-bezier(0.2, 0, 0, 1).
static float m3Ease(float x) {
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

float SRipple::currentRadius(float t) const {
    if (mode == 2) {
        // M3: start at 20% of the final size, grow over 75% of the life.
        return maxRadius * (0.2f + 0.8f * m3Ease(std::min(t / 0.75f, 1.f)));
    }
    // Spread over the first 70% of the life with a strong ease-out
    // (Material's "standard decelerate"), starting from a small seed.
    const float e    = std::min(t / 0.7f, 1.f);
    const float ease = 1.f - std::pow(1.f - e, 3.f);
    return maxRadius * (0.08f + 0.92f * ease);
}

float SRipple::currentAlpha(float t) const {
    if (mode == 2) {
        const float in  = std::min(t / 0.04f, 1.f);
        const float x   = std::clamp((t - 0.55f) / 0.45f, 0.f, 1.f);
        const float out = 1.f - x * x * (3.f - 2.f * x);
        return opacity * in * out;
    }
    // Quick fade-in, hold, then fade out over the back half.
    const float in  = std::min(t / 0.06f, 1.f);
    const float x   = std::clamp((t - 0.45f) / 0.55f, 0.f, 1.f);
    const float out = 1.f - x * x * (3.f - 2.f * x);
    return opacity * in * out;
}

CBox SRipple::bounds() const {
    // sparkle's turbulent rim can reach ~5% past the radius
    const float r = maxRadius * (mode == 2 ? 1.08f : 1.f) + softness + 4.f;
    CBox        box{center.x - r, center.y - r, r * 2.0, r * 2.0};
    if (clip)
        box = box.intersection(clipBox.copy().expand(4));
    return box;
}

// ── state ────────────────────────────────────────────────────────────────────

namespace {
    struct SConfigValues {
        SP<CV::Bool>   enabled, onFocus, focusHover, onWorkspace, bounded;
        SP<CV::String> color, workspaceRadius, style, onClick, focusOrigin;
        SP<CV::Float>  opacity, radius, softness, ringWidth;
        SP<CV::Int>    duration, maxRipples;
    };

    struct SState {
        SSettings                                       settings;
        SConfigValues                                   cfg;
        std::deque<SRipple>                             ripples;
        std::vector<Hyprutils::Signal::CHyprSignalListener> listeners;
        SP<CEventLoopTimer>                             timer;
        SP<SHyprCtlCommand>                             ctl;
        steady_clock::time_point                        lastClick{}, lastWorkspace{};
        // spawn counters for `hyprctl hyprripple stats`
        uint64_t                                        nClick = 0, nFocus = 0, nWorkspace = 0, nManual = 0;
        int                                             lastFocusReason = -1;
    };

    std::unique_ptr<SState> g_state;

    constexpr auto TICK = milliseconds(8);

    // ── settings ─────────────────────────────────────────────────────────────

    void loadFromConfig() {
        auto& s = g_state->settings;
        auto& c = g_state->cfg;
        s.enabled         = c.enabled->value();
        s.color           = c.color->value();
        s.opacity         = c.opacity->value();
        s.duration        = (int)c.duration->value();
        s.radius          = c.radius->value();
        s.workspaceRadius = c.workspaceRadius->value();
        s.softness        = c.softness->value();
        s.style           = c.style->value() == "fill" ? "classic" : c.style->value();
        s.ringWidth       = c.ringWidth->value();
        s.onClick         = c.onClick->value();
        s.onFocus         = c.onFocus->value();
        s.focusHover      = c.focusHover->value();
        s.onWorkspace     = c.onWorkspace->value();
        s.focusOrigin     = c.focusOrigin->value();
        s.bounded         = c.bounded->value();
        s.maxRipples      = (int)c.maxRipples->value();
    }

    std::optional<bool> parseBool(const std::string& v) {
        if (v == "1" || v == "true" || v == "yes" || v == "on")
            return true;
        if (v == "0" || v == "false" || v == "no" || v == "off")
            return false;
        return std::nullopt;
    }

    std::optional<float> parseFloat(const std::string& v) {
        try {
            size_t idx = 0;
            float  f   = std::stof(v, &idx);
            if (idx != v.size() || !std::isfinite(f))
                return std::nullopt;
            return f;
        } catch (...) { return std::nullopt; }
    }

    // Returns an error message, or "" on success.
    std::string setSetting(const std::string& key, const std::string& value) {
        auto& s = g_state->settings;

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
        auto asEnum = [&](std::string& out, std::initializer_list<std::string_view> allowed) -> std::string {
            if (std::ranges::find(allowed, value) == allowed.end())
                return std::format("{}: unknown value '{}'", key, value);
            out = value;
            return "";
        };

        if (key == "enabled")
            return asBool(s.enabled);
        if (key == "on_focus")
            return asBool(s.onFocus);
        if (key == "focus_hover")
            return asBool(s.focusHover);
        if (key == "on_workspace")
            return asBool(s.onWorkspace);
        if (key == "bounded")
            return asBool(s.bounded);
        if (key == "opacity")
            return asFloat(s.opacity, 0.f, 1.f);
        if (key == "radius")
            return asFloat(s.radius, 4.f, 4000.f);
        if (key == "softness")
            return asFloat(s.softness, 0.f, 200.f);
        if (key == "ring_width")
            return asFloat(s.ringWidth, 1.f, 200.f);
        if (key == "duration") {
            float d = 0;
            auto  err = asFloat(d, 50.f, 5000.f);
            s.duration = (int)d;
            return err;
        }
        if (key == "max_ripples") {
            float m = 0;
            auto  err = asFloat(m, 1.f, 64.f);
            s.maxRipples = (int)m;
            return err;
        }
        if (key == "on_click")
            return asEnum(s.onClick, {"none", "left", "any"});
        if (key == "style") {
            if (value == "fill") { // pre-preset name
                s.style = "classic";
                return "";
            }
            return asEnum(s.style, {"classic", "ring", "sparkle"});
        }
        if (key == "focus_origin")
            return asEnum(s.focusOrigin, {"cursor", "center"});
        if (key == "workspace_radius") {
            if (value != "screen" && !parseFloat(value))
                return "workspace_radius: expected 'screen' or a number";
            s.workspaceRadius = value;
            return "";
        }
        if (key == "color") {
            s.color = value.empty() ? "accent" : value;
            return "";
        }
        return std::format("unknown key '{}'", key);
    }

    std::string settingsJson() {
        const auto& s = g_state->settings;
        const auto  c = Theme::resolve(s.color);
        return std::format(R"({{"enabled":{},"color":"{}","resolved_color":"#{:02x}{:02x}{:02x}","opacity":{},"duration":{},"radius":{},"workspace_radius":"{}","softness":{},"style":"{}","ring_width":{},"on_click":"{}","on_focus":{},"focus_hover":{},"on_workspace":{},"focus_origin":"{}","bounded":{},"max_ripples":{},"active":{}}})",
                           s.enabled, s.color, (int)std::lround(c.r * 255), (int)std::lround(c.g * 255), (int)std::lround(c.b * 255), s.opacity, s.duration, s.radius,
                           s.workspaceRadius, s.softness, s.style, s.ringWidth, s.onClick, s.onFocus, s.focusHover, s.onWorkspace, s.focusOrigin, s.bounded, s.maxRipples,
                           g_state->ripples.size());
    }

    // ── spawning / animation ─────────────────────────────────────────────────

    void armTimer() {
        if (g_state->timer && !g_state->timer->armed())
            g_state->timer->updateTimeout(TICK);
    }

    void spawn(const Vector2D& center, float maxRadius, std::optional<CBox> clip = std::nullopt, float clipRounding = 0.f) {
        if (!g_state || !g_state->settings.enabled)
            return;

        const auto& s = g_state->settings;
        SRipple     r{
                .center    = center,
                .maxRadius = std::max(maxRadius, 4.f),
                .start     = steady_clock::now(),
                .duration  = s.duration / 1000.f,
                .opacity   = s.opacity,
                .color     = Theme::resolve(s.color),
                .mode      = s.style == "ring" ? 1 : s.style == "sparkle" ? 2 : 0,
                .seed      = (float)(std::rand() % 1000) * 0.137f,
                .ringWidth = s.ringWidth,
                .softness  = s.softness,
        };
        if (clip) {
            r.clip         = true;
            r.clipBox      = *clip;
            r.clipRounding = clipRounding;
        }

        while ((int)g_state->ripples.size() >= std::max(s.maxRipples, 1)) {
            g_pHyprRenderer->damageBox(g_state->ripples.front().bounds());
            g_state->ripples.pop_front();
        }

        g_state->ripples.push_back(r);
        g_pHyprRenderer->damageBox(r.bounds());
        armTimer();
    }

    // Damage live ripples each tick so the monitor keeps producing frames,
    // and drop finished ones (with one last damage to wipe them).
    void onTick(SP<CEventLoopTimer> self, void*) {
        if (!g_state)
            return;
        const auto now = steady_clock::now();
        for (auto it = g_state->ripples.begin(); it != g_state->ripples.end();) {
            g_pHyprRenderer->damageBox(it->bounds());
            if (it->progress(now) >= 1.f)
                it = g_state->ripples.erase(it);
            else
                ++it;
        }
        if (!g_state->ripples.empty())
            self->updateTimeout(TICK);
    }

    // Distance from p to the farthest corner of box: a radius that floods it.
    float coverRadius(const Vector2D& p, const CBox& box) {
        const float dx = std::max(std::abs(p.x - box.x), std::abs(p.x - (box.x + box.w)));
        const float dy = std::max(std::abs(p.y - box.y), std::abs(p.y - (box.y + box.h)));
        return std::sqrt(dx * dx + dy * dy);
    }

    // ── triggers ─────────────────────────────────────────────────────────────

    void onMouseButton(const IPointer::SButtonEvent& ev) {
        const auto& s = g_state->settings;
        if (ev.state != WL_POINTER_BUTTON_STATE_PRESSED || s.onClick == "none")
            return;
        if (s.onClick == "left" && ev.button != BTN_LEFT)
            return;
        g_state->lastClick = steady_clock::now();
        g_state->nClick++;
        spawn(g_pInputManager->getMouseCoordsInternal(), s.radius);
    }

    void onWindowActive(PHLWINDOW window, Desktop::eFocusReason reason) {
        const auto& s = g_state->settings;
        if (!s.onFocus || !window || !window->m_isMapped || window->isHidden())
            return;

        switch (reason) {
            // the click / workspace triggers already cover these
            case Desktop::FOCUS_REASON_CLICK:
            case Desktop::FOCUS_REASON_WORKSPACE_CHANGE:
            case Desktop::FOCUS_REASON_TOGGLE_SPECIAL_WORKSPACE:
            case Desktop::FOCUS_REASON_GHOSTS: return;
            // hovering across windows with follow_mouse would ripple constantly
            case Desktop::FOCUS_REASON_FFM:
                if (!s.focusHover)
                    return;
                break;
            default: break;
        }

        // A click or a workspace switch already rippled for this focus change.
        const auto now = steady_clock::now();
        if (now - g_state->lastClick < milliseconds(300) || now - g_state->lastWorkspace < milliseconds(300))
            return;

        g_state->nFocus++;
        g_state->lastFocusReason = (int)reason;

        const CBox     box    = window->getWindowMainSurfaceBox();
        const Vector2D cursor = g_pInputManager->getMouseCoordsInternal();
        const Vector2D origin = s.focusOrigin == "cursor" && box.containsPoint(cursor) ? cursor : box.middle();

        if (s.bounded)
            spawn(origin, coverRadius(origin, box), box, window->rounding());
        else
            spawn(origin, std::max<float>(s.radius, std::min(box.w, box.h) * 0.5f));
    }

    void onWorkspaceActive(PHLWORKSPACE ws) {
        const auto& s = g_state->settings;
        if (!s.onWorkspace || !ws)
            return;
        const auto mon = ws->m_monitor.lock();
        if (!mon)
            return;

        g_state->lastWorkspace = steady_clock::now();
        g_state->nWorkspace++;

        const CBox     monBox{mon->m_position, mon->m_size};
        const Vector2D cursor = g_pInputManager->getMouseCoordsInternal();
        const Vector2D origin = monBox.containsPoint(cursor) ? cursor : monBox.middle();

        float radius = coverRadius(origin, monBox);
        if (s.workspaceRadius != "screen")
            if (auto r = parseFloat(s.workspaceRadius))
                radius = *r;
        spawn(origin, radius);
    }

    void onRenderStage(eRenderStage stage) {
        if (stage != RENDER_POST_WINDOWS || !g_state || g_state->ripples.empty() || g_pHyprRenderer->m_bRenderingSnapshot)
            return;
        const auto mon = g_pHyprRenderer->m_renderData.pMonitor.lock();
        if (!mon)
            return;

        const CBox monBox{mon->m_position, mon->m_size};
        const auto now = steady_clock::now();
        for (const auto& r : g_state->ripples) {
            if (!r.bounds().overlaps(monBox))
                continue;
            const float t = r.progress(now);
            g_pHyprRenderer->m_renderPass.add(makeUnique<CRipplePassElement>(CRipplePassElement::SData{
                .ripple = r,
                .radius = r.currentRadius(t),
                .alpha  = r.currentAlpha(t),
                .t      = t,
                .time   = std::chrono::duration<float>(now - r.start).count(),
            }));
        }
    }

    // ── manual trigger (hyprctl / Lua) ───────────────────────────────────────

    void manualTrigger(std::optional<Vector2D> at, std::optional<float> radius) {
        g_state->nManual++;
        spawn(at.value_or(g_pInputManager->getMouseCoordsInternal()), radius.value_or(g_state->settings.radius));
    }

    std::string onHyprctl(eHyprCtlOutputFormat, std::string request) {
        if (!g_state)
            return "hyprripple: not loaded";

        std::vector<std::string> args;
        {
            std::istringstream in(request);
            for (std::string tok; in >> tok;)
                args.push_back(tok);
        }
        // the request may or may not include our own command name / hyprctl flags
        while (!args.empty() && (args.front() == "hyprripple" || args.front().starts_with("-") || args.front().starts_with("/")))
            args.erase(args.begin());

        const std::string cmd = args.empty() ? "get" : args[0];

        if (cmd == "get" || cmd == "status")
            return settingsJson();

        if (cmd == "set") {
            if (args.size() < 3)
                return "usage: hyprctl hyprripple set <key> <value>";
            std::string value = args[2];
            for (size_t i = 3; i < args.size(); ++i)
                value += " " + args[i];
            const auto err = setSetting(args[1], value);
            return err.empty() ? "ok" : "error: " + err;
        }

        if (cmd == "trigger") {
            std::optional<Vector2D> at;
            std::optional<float>    radius;
            if (args.size() >= 3) {
                auto x = parseFloat(args[1]), y = parseFloat(args[2]);
                if (!x || !y)
                    return "usage: hyprctl hyprripple trigger [x y [radius]]";
                at = Vector2D{*x, *y};
            }
            if (args.size() >= 4)
                radius = parseFloat(args[3]);
            const bool was = g_state->settings.enabled;
            g_state->settings.enabled = true; // an explicit trigger always shows
            manualTrigger(at, radius);
            g_state->settings.enabled = was;
            return "ok";
        }

        if (cmd == "stats")
            return std::format(R"({{"click":{},"focus":{},"last_focus_reason":{},"workspace":{},"manual":{},"active":{}}})", g_state->nClick, g_state->nFocus,
                               g_state->lastFocusReason, g_state->nWorkspace, g_state->nManual, g_state->ripples.size());

        if (cmd == "reload-config") {
            loadFromConfig();
            return "ok";
        }

        return "usage: hyprctl hyprripple [get | set <key> <value> | trigger [x y [radius]] | stats | reload-config]";
    }

    // hl.plugin.hyprripple.trigger([x, y [, radius]])
    int luaTrigger(lua_State* L) {
        if (!g_state)
            return 0;
        std::optional<Vector2D> at;
        std::optional<float>    radius;
        if (lua_gettop(L) >= 2 && lua_isnumber(L, 1) && lua_isnumber(L, 2))
            at = Vector2D{lua_tonumber(L, 1), lua_tonumber(L, 2)};
        if (lua_gettop(L) >= 3 && lua_isnumber(L, 3))
            radius = (float)lua_tonumber(L, 3);
        manualTrigger(at, radius);
        return 0;
    }

    // hl.plugin.hyprripple.set(key, value) - same keys as hyprctl
    int luaSet(lua_State* L) {
        if (!g_state)
            return 0;
        const char* key = luaL_checkstring(L, 1);
        std::string value;
        if (lua_isboolean(L, 2))
            value = lua_toboolean(L, 2) ? "true" : "false";
        else
            value = luaL_checkstring(L, 2);
        if (auto err = setSetting(key, value); !err.empty())
            return luaL_error(L, "hyprripple.set: %s", err.c_str());
        return 0;
    }

    template <typename T, typename D>
    SP<T> addValue(const char* name, const char* desc, D def) {
        auto v = CV::makeConfigValue<T>(name, desc, def, CV::valueOptions_t<T>{});
        HyprlandAPI::addConfigValueV2(PHANDLE, v);
        return v;
    }
}

// ── plugin entry points ──────────────────────────────────────────────────────

APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    PHANDLE = handle;

    const std::string HASH        = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();
    if (HASH != CLIENT_HASH) {
        HyprlandAPI::addNotification(PHANDLE, std::format("[{}] built for a different Hyprland - rebuild it (hyprpm update)", PLUGIN_NAME), CHyprColor{1.0, 0.2, 0.2, 1.0},
                                     8000);
        throw std::runtime_error("[hyprripple] version mismatch");
    }

    g_state = std::make_unique<SState>();
    const SSettings d;
    auto&           c = g_state->cfg;

    c.enabled         = addValue<CV::Bool>("plugin:hyprripple:enabled", "Master switch", Config::BOOL{d.enabled});
    c.color           = addValue<CV::String>("plugin:hyprripple:color", "colors.toml key (accent) or a literal color", Config::STRING{d.color});
    c.opacity         = addValue<CV::Float>("plugin:hyprripple:opacity", "Peak ink opacity 0-1", Config::FLOAT{d.opacity});
    c.duration        = addValue<CV::Int>("plugin:hyprripple:duration", "Ripple lifetime in ms", Config::INTEGER{d.duration});
    c.radius          = addValue<CV::Float>("plugin:hyprripple:radius", "Click ripple radius (logical px)", Config::FLOAT{d.radius});
    c.workspaceRadius = addValue<CV::String>("plugin:hyprripple:workspace_radius", "'screen' or px", Config::STRING{d.workspaceRadius});
    c.softness        = addValue<CV::Float>("plugin:hyprripple:softness", "Edge feather (logical px)", Config::FLOAT{d.softness});
    c.style           = addValue<CV::String>("plugin:hyprripple:style", "classic | ring | sparkle", Config::STRING{d.style});
    c.ringWidth       = addValue<CV::Float>("plugin:hyprripple:ring_width", "Ring thickness (logical px)", Config::FLOAT{d.ringWidth});
    c.onClick         = addValue<CV::String>("plugin:hyprripple:on_click", "none | left | any", Config::STRING{d.onClick});
    c.onFocus         = addValue<CV::Bool>("plugin:hyprripple:on_focus", "Ripple on window focus change", Config::BOOL{d.onFocus});
    c.focusHover      = addValue<CV::Bool>("plugin:hyprripple:focus_hover", "Also ripple on focus-follows-mouse", Config::BOOL{d.focusHover});
    c.onWorkspace     = addValue<CV::Bool>("plugin:hyprripple:on_workspace", "Ripple on workspace switch", Config::BOOL{d.onWorkspace});
    c.focusOrigin     = addValue<CV::String>("plugin:hyprripple:focus_origin", "cursor | center", Config::STRING{d.focusOrigin});
    c.bounded         = addValue<CV::Bool>("plugin:hyprripple:bounded", "Clip focus ripples to the window", Config::BOOL{d.bounded});
    c.maxRipples      = addValue<CV::Int>("plugin:hyprripple:max_ripples", "Max simultaneous ripples", Config::INTEGER{d.maxRipples});

    HyprlandAPI::addLuaFunction(PHANDLE, "hyprripple", "trigger", luaTrigger);
    HyprlandAPI::addLuaFunction(PHANDLE, "hyprripple", "set", luaSet);

    g_state->ctl = HyprlandAPI::registerHyprCtlCommand(PHANDLE, SHyprCtlCommand{.name = "hyprripple", .exact = false, .fn = onHyprctl});

    auto& ev = Event::bus()->m_events;
    g_state->listeners.push_back(ev.input.mouse.button.listen([](const IPointer::SButtonEvent& e, Event::SCallbackInfo&) {
        if (g_state)
            onMouseButton(e);
    }));
    g_state->listeners.push_back(ev.window.active.listen([](PHLWINDOW w, Desktop::eFocusReason reason) {
        if (g_state)
            onWindowActive(w, reason);
    }));
    g_state->listeners.push_back(ev.workspace.active.listen([](PHLWORKSPACE ws) {
        if (g_state)
            onWorkspaceActive(ws);
    }));
    g_state->listeners.push_back(ev.render.stage.listen([](eRenderStage stage) { onRenderStage(stage); }));
    g_state->listeners.push_back(ev.config.reloaded.listen([]() {
        if (g_state)
            loadFromConfig();
    }));

    g_state->timer = makeShared<CEventLoopTimer>(std::nullopt, onTick, nullptr);
    g_pEventLoopManager->addTimer(g_state->timer);

    // Values set in hyprripple.lua were skipped when the config was parsed
    // before this plugin existed; a reload picks them up.
    HyprlandAPI::reloadConfig();

    return {std::string(PLUGIN_NAME), std::string(PLUGIN_DESCRIPTION), std::string(PLUGIN_AUTHOR), std::string(PLUGIN_VERSION)};
}

APICALL EXPORT void PLUGIN_EXIT() {
    if (!g_state)
        return;

    g_state->listeners.clear();

    if (g_state->timer) {
        g_state->timer->cancel();
        g_pEventLoopManager->removeTimer(g_state->timer);
    }
    if (g_state->ctl)
        HyprlandAPI::unregisterHyprCtlCommand(PHANDLE, g_state->ctl);

    for (const auto& r : g_state->ripples)
        g_pHyprRenderer->damageBox(r.bounds());

    g_pHyprRenderer->m_renderPass.removeAllOfType("CRipplePassElement");
    RippleShader::destroy();
    g_state.reset();
}
