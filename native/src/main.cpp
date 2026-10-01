// HyprRipple - Material Design ink ripples for Hyprland.
//
// Triggers (each optional): mouse button press, touch / tablet tap, window
// focus change, workspace switch. Presses grow while held and fade on
// release. Ripples are drawn with a custom GLSL pass element right after
// windows (under bars/overlays) in the active Omarchy theme's accent, and
// animate in step with each monitor's own frames.
//
// Validation, timing and geometry live in Logic.cpp (unit tested); this file
// is the Hyprland glue.

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
#include <hyprland/src/managers/fullscreen/FullscreenController.hpp>
#include <hyprland/src/managers/input/InputManager.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/state/MonitorState.hpp>
#include <hyprutils/signal/Listener.hpp>

extern "C" {
#include <lauxlib.h>
#include <lua.h>
}

#include <algorithm>
#include <cmath>
#include <deque>
#include <format>
#include <random>
#include <sstream>

using namespace std::chrono;
namespace CV = Config::Values;

// ── ripple ───────────────────────────────────────────────────────────────────

Logic::Life SRipple::life(steady_clock::time_point now) const {
    return Logic::Life{
        .mode       = mode,
        .duration   = duration,
        .elapsed    = std::chrono::duration<float>(now - start).count(),
        .held       = held,
        .releasedAt = released ? std::chrono::duration<float>(*released - start).count() : -1.f,
    };
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
        SP<CV::Bool>   enabled, onTouch, hold, onFocus, focusHover, onWorkspace, bounded, skipFullscreen, pauseWhenSharing;
        SP<CV::String> color, workspaceRadius, style, onClick, focusOrigin, exclude;
        SP<CV::Float>  opacity, radius, softness, ringWidth, sparkleAmount;
        SP<CV::Int>    duration, maxRipples;
    };

    struct SState {
        SSettings                                           settings;
        SConfigValues                                       cfg;
        std::deque<SRipple>                                 ripples;
        std::vector<Hyprutils::Signal::CHyprSignalListener> listeners;
        SP<CEventLoopTimer>                                 sweep;
        SP<SHyprCtlCommand>                                 ctl;
        steady_clock::time_point                            lastClick{}, lastWorkspace{};
        int                                                 sharing = 0; // active screenshares
        std::minstd_rand                                    rng{std::random_device{}()};
        // counters for `hyprctl hyprripple stats`
        uint64_t nClick = 0, nTouch = 0, nFocus = 0, nWorkspace = 0, nManual = 0, nSkipped = 0, nFrames = 0, nPre = 0;
        int      lastFocusReason = -1;
    };

    std::unique_ptr<SState> g_state;

    // Safety net only: frames normally come from render.pre. This sweep
    // clears finished ripples if a monitor stops rendering (DPMS, lock).
    constexpr auto SWEEP = milliseconds(100);

    // ── geometry helpers ─────────────────────────────────────────────────────

    Logic::Rect rect(const CBox& b) {
        return {b.x, b.y, b.w, b.h};
    }
    Logic::Vec vec(const Vector2D& v) {
        return {v.x, v.y};
    }

    // The window's box as it is drawn right now: animated position/size plus
    // the workspace slide offset (same as the renderer applies).
    CBox liveBox(const PHLWINDOW& w) {
        CBox box = w->getWindowMainSurfaceBox();
        if (w->m_workspace && !w->m_pinned)
            box.translate(w->m_workspace->m_renderOffset->value());
        box.translate(w->m_floatingOffset);
        return box;
    }

    PHLMONITOR monitorAt(const Vector2D& p) {
        for (const auto& m : State::monitorState()->monitors())
            if (m && CBox{m->m_position, m->m_size}.containsPoint(p))
                return m;
        return Desktop::focusState()->monitor();
    }

    bool excluded(const PHLWINDOW& w) {
        const auto& list = g_state->settings.exclude;
        return w && !list.empty() && Logic::classExcluded(list, w->m_class);
    }

    // ── settings ─────────────────────────────────────────────────────────────

    // Config values go through the same validation as hyprctl/Lua, so a typo
    // in hyprripple.lua can't put an out-of-range or unescaped value live.
    void loadFromConfig() {
        auto&       s = g_state->settings;
        const auto& c = g_state->cfg;
        auto        b = [](bool v) { return std::string(v ? "true" : "false"); };
        auto        f = [](double v) { return std::format("{}", v); };
        const std::pair<const char*, std::string> values[] = {
            {"enabled", b(c.enabled->value())},
            {"color", c.color->value()},
            {"opacity", f(c.opacity->value())},
            {"duration", f(c.duration->value())},
            {"radius", f(c.radius->value())},
            {"workspace_radius", c.workspaceRadius->value()},
            {"softness", f(c.softness->value())},
            {"style", c.style->value()},
            {"ring_width", f(c.ringWidth->value())},
            {"sparkle_amount", f(c.sparkleAmount->value())},
            {"on_click", c.onClick->value()},
            {"on_touch", b(c.onTouch->value())},
            {"hold", b(c.hold->value())},
            {"on_focus", b(c.onFocus->value())},
            {"focus_hover", b(c.focusHover->value())},
            {"on_workspace", b(c.onWorkspace->value())},
            {"focus_origin", c.focusOrigin->value()},
            {"bounded", b(c.bounded->value())},
            {"max_ripples", f(c.maxRipples->value())},
            {"skip_fullscreen", b(c.skipFullscreen->value())},
            {"exclude", c.exclude->value()},
            {"pause_when_sharing", b(c.pauseWhenSharing->value())},
        };
        for (const auto& [key, value] : values)
            Logic::applySetting(s, key, value); // invalid values keep the previous setting
    }

    std::string settingsJson() {
        const auto& s = g_state->settings;
        const auto  c = Theme::resolve(s.color);
        auto        e = [](const std::string& v) { return Logic::jsonEscape(v); };
        return std::format(
            R"({{"enabled":{},"color":"{}","resolved_color":"#{:02x}{:02x}{:02x}","opacity":{},"duration":{},"radius":{},"workspace_radius":"{}","softness":{},"style":"{}","ring_width":{},"sparkle_amount":{},"on_click":"{}","on_touch":{},"hold":{},"on_focus":{},"focus_hover":{},"on_workspace":{},"focus_origin":"{}","bounded":{},"max_ripples":{},"skip_fullscreen":{},"exclude":"{}","pause_when_sharing":{},"sharing":{},"active":{}}})",
            s.enabled, e(s.color), (int)std::lround(c.r * 255), (int)std::lround(c.g * 255), (int)std::lround(c.b * 255), s.opacity, s.duration, s.radius,
            e(s.workspaceRadius), s.softness, e(s.style), s.ringWidth, s.sparkleAmount, e(s.onClick), s.onTouch, s.hold, s.onFocus, s.focusHover, s.onWorkspace,
            e(s.focusOrigin), s.bounded, s.maxRipples, s.skipFullscreen, e(s.exclude), s.pauseWhenSharing, g_state->sharing > 0, g_state->ripples.size());
    }

    // ── spawning ─────────────────────────────────────────────────────────────

    void armSweep() {
        if (g_state->sweep && !g_state->sweep->armed())
            g_state->sweep->updateTimeout(SWEEP);
    }

    void damage(SRipple& r) {
        const CBox b = r.bounds();
        if (r.lastBounds && *r.lastBounds != b)
            g_pHyprRenderer->damageBox(*r.lastBounds);
        g_pHyprRenderer->damageBox(b);
        r.lastBounds = b;
    }

    // Move an anchored ripple with its window.
    void follow(SRipple& r) {
        const auto w = r.anchor.lock();
        if (!w || !w->m_isMapped)
            return;
        const CBox box = liveBox(w);
        r.center       = box.pos() + r.anchorRel;
        if (r.clip)
            r.clipBox = box;
    }

    void anchorTo(SRipple& r, const PHLWINDOW& w) {
        if (!w)
            return;
        r.anchor    = w;
        r.anchorRel = r.center - liveBox(w).pos();
    }

    struct SSpawn {
        Vector2D            center;
        float               radius;
        std::optional<CBox> clip;
        float               clipRounding = 0.f;
        PHLWINDOW           anchor;
        bool                held    = false;
        uint64_t            holdKey = 0;
        bool                force   = false; // manual trigger: ignore exclusions
    };

    // Shared gate for every automatic trigger.
    bool allowed(const Vector2D& at, const PHLWINDOW& window) {
        const auto& s = g_state->settings;
        if (!s.enabled)
            return false;
        if (s.pauseWhenSharing && g_state->sharing > 0)
            return false;
        if (excluded(window))
            return false;
        if (s.skipFullscreen) {
            const auto mon = monitorAt(at);
            if (mon && Fullscreen::controller()->hasFullscreen(mon))
                return false;
        }
        return true;
    }

    SRipple* spawn(const SSpawn& sp) {
        if (!g_state)
            return nullptr;
        if (!sp.force && !allowed(sp.center, sp.anchor)) {
            g_state->nSkipped++;
            return nullptr;
        }

        const auto& s = g_state->settings;
        SRipple     r{
                .center        = sp.center,
                .maxRadius     = std::clamp(sp.radius, 4.f, 20000.f),
                .start         = steady_clock::now(),
                .duration      = s.duration / 1000.f,
                .opacity       = s.opacity,
                .color         = Theme::resolve(s.color),
                .mode          = Logic::styleMode(s.style),
                .seed          = std::uniform_real_distribution<float>(0.f, 137.f)(g_state->rng),
                .ringWidth     = s.ringWidth,
                .sparkleAmount = s.sparkleAmount,
                .softness      = s.softness,
                .held          = sp.held && s.hold,
                .holdKey       = sp.holdKey,
        };
        if (sp.clip) {
            r.clip         = true;
            r.clipBox      = *sp.clip;
            r.clipRounding = sp.clipRounding;
        }

        while ((int)g_state->ripples.size() >= std::max(s.maxRipples, 1)) {
            g_pHyprRenderer->damageBox(g_state->ripples.front().bounds());
            g_state->ripples.pop_front();
        }

        anchorTo(r, sp.anchor);
        g_state->ripples.push_back(r);
        auto& added = g_state->ripples.back();
        damage(added); // kicks the first frame; render.pre keeps them coming
        armSweep();
        return &added;
    }

    void release(uint64_t key) {
        const auto now = steady_clock::now();
        for (auto& r : g_state->ripples)
            if (r.held && !r.released && r.holdKey == key)
                r.released = now;
    }

    // Drop finished ripples, wiping where they were drawn.
    void prune(steady_clock::time_point now) {
        std::erase_if(g_state->ripples, [&](SRipple& r) {
            if (!Logic::finished(r.life(now)))
                return false;
            g_pHyprRenderer->damageBox(r.lastBounds.value_or(r.bounds()));
            return true;
        });
    }

    // ── animation ────────────────────────────────────────────────────────────

    // Runs before every frame of every monitor: move ripples with their
    // windows and damage them, which schedules the next frame - so ripples
    // animate at each monitor's own refresh rate (60, 144, VRR...).
    void onPreRender(PHLMONITOR mon) {
        if (!g_state || g_state->ripples.empty() || !mon)
            return;
        g_state->nPre++;
        const auto now = steady_clock::now();
        prune(now);
        const CBox monBox{mon->m_position, mon->m_size};
        for (auto& r : g_state->ripples) {
            follow(r);
            if (r.bounds().overlaps(monBox) || (r.lastBounds && r.lastBounds->overlaps(monBox)))
                damage(r);
        }
    }

    // After a frame that drew ripples: damage them again and ask this monitor
    // for its next frame, so the animation runs at the monitor's refresh
    // instead of whenever something else happens to redraw.
    void onPostRender(const PHLMONITOR& mon) {
        const CBox monBox{mon->m_position, mon->m_size};
        bool       any = false;
        for (auto& r : g_state->ripples) {
            if (!r.bounds().overlaps(monBox))
                continue;
            damage(r);
            any = true;
        }
        if (any)
            mon->scheduleFrame(Aquamarine::IOutput::AQ_SCHEDULE_ANIMATION);
    }

    void onSweep(SP<CEventLoopTimer> self, void*) {
        if (!g_state)
            return;
        const auto now = steady_clock::now();
        prune(now);
        for (auto& r : g_state->ripples) {
            follow(r);
            damage(r);
        }
        if (!g_state->ripples.empty())
            self->updateTimeout(SWEEP);
    }

    void onRenderStage(eRenderStage stage) {
        if ((stage != RENDER_POST_WINDOWS && stage != RENDER_POST) || !g_state || g_state->ripples.empty() || g_pHyprRenderer->m_bRenderingSnapshot)
            return;
        const auto mon = g_pHyprRenderer->m_renderData.pMonitor.lock();
        if (!mon)
            return;
        if (stage == RENDER_POST) {
            onPostRender(mon);
            return;
        }

        const CBox monBox{mon->m_position, mon->m_size};
        const auto now = steady_clock::now();
        bool       drew = false;
        for (auto& r : g_state->ripples) {
            if (!r.bounds().overlaps(monBox))
                continue;
            drew            = true;
            const auto life = r.life(now);
            g_pHyprRenderer->m_renderPass.add(makeUnique<CRipplePassElement>(CRipplePassElement::SData{
                .ripple = r,
                .radius = r.maxRadius * Logic::spreadFraction(life),
                .alpha  = Logic::alpha(life, r.opacity),
                .fade   = Logic::fade(life),
                .time   = life.elapsed,
            }));
        }
        if (drew)
            g_state->nFrames++;
    }

    // ── triggers ─────────────────────────────────────────────────────────────

    // The focused window, if the point is on it: clicks ride along with it.
    PHLWINDOW focusedAt(const Vector2D& p) {
        const auto w = Desktop::focusState()->window();
        return w && w->m_isMapped && liveBox(w).containsPoint(p) ? w : nullptr;
    }

    // Bound a ripple to its window (Material's bounded ripple) when "Clip to
    // window" is on: it's clipped to the window's live, rounded box.
    void clipTo(SRipple& r, const PHLWINDOW& w) {
        if (!w || !g_state->settings.bounded)
            return;
        r.clip         = true;
        r.clipBox      = liveBox(w);
        r.clipRounding = w->rounding();
    }

    // Press-style triggers (mouse, touch, pen) share this: held ink that
    // rides on - and is clipped to - the window under the point.
    void spawnPress(const Vector2D& at, uint64_t holdKey) {
        const auto w = focusedAt(at);
        if (auto* r = spawn({.center = at, .radius = g_state->settings.radius, .anchor = w, .held = true, .holdKey = holdKey}))
            clipTo(*r, w);
    }

    void onMouseButton(const IPointer::SButtonEvent& ev) {
        const auto& s = g_state->settings;
        if (ev.state == WL_POINTER_BUTTON_STATE_RELEASED) {
            release(ev.button);
            return;
        }
        if (s.onClick == "none" || (s.onClick == "left" && ev.button != BTN_LEFT))
            return;
        g_state->lastClick = steady_clock::now();
        g_state->nClick++;
        const Vector2D cursor = g_pInputManager->getMouseCoordsInternal();
        spawnPress(cursor, ev.button);
    }

    void onTouchDown(const ITouch::SDownEvent& ev) {
        if (!g_state->settings.onTouch)
            return;
        // Touch reports 0..1 across the output the device is bound to.
        PHLMONITOR mon;
        if (ev.device && !ev.device->m_boundOutput.empty())
            for (const auto& m : State::monitorState()->monitors())
                if (m && m->m_name == ev.device->m_boundOutput)
                    mon = m;
        if (!mon)
            mon = Desktop::focusState()->monitor();
        if (!mon)
            return;
        const Vector2D at = mon->m_position + ev.pos * mon->m_size;
        g_state->lastClick = steady_clock::now();
        g_state->nTouch++;
        spawnPress(at, HOLD_TOUCH | (uint32_t)ev.touchID);
    }

    void onTabletTip(const CTablet::STipEvent& ev) {
        if (!ev.in) {
            release(HOLD_TABLET);
            return;
        }
        if (!g_state->settings.onTouch)
            return;
        // A tablet drives the cursor, so the cursor is where the pen landed.
        const Vector2D at = g_pInputManager->getMouseCoordsInternal();
        g_state->lastClick = steady_clock::now();
        g_state->nTouch++;
        spawnPress(at, HOLD_TABLET);
    }

    void onWindowActive(PHLWINDOW window, Desktop::eFocusReason reason) {
        const auto& s = g_state->settings;
        if (!window || !window->m_isMapped || window->isHidden())
            return;

        // Click-to-focus: the click ripple spawned a moment ago (before focus
        // moved) belongs to this window - ride along with it, or drop it if
        // the window is excluded.
        if (reason == Desktop::FOCUS_REASON_CLICK && !g_state->ripples.empty()) {
            auto& last = g_state->ripples.back();
            if (steady_clock::now() - last.start < milliseconds(300) && last.anchor.lock() != window && liveBox(window).containsPoint(last.center)) {
                if (excluded(window)) {
                    g_pHyprRenderer->damageBox(last.lastBounds.value_or(last.bounds()));
                    g_state->ripples.pop_back();
                } else {
                    anchorTo(last, window);
                    clipTo(last, window);
                }
            }
        }

        if (!s.onFocus)
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

        // A click, tap or workspace switch already rippled for this focus change.
        const auto now = steady_clock::now();
        if (now - g_state->lastClick < milliseconds(300) || now - g_state->lastWorkspace < milliseconds(300))
            return;

        g_state->nFocus++;
        g_state->lastFocusReason = (int)reason;

        const CBox     box    = liveBox(window);
        const Vector2D cursor = g_pInputManager->getMouseCoordsInternal();
        Vector2D       origin = s.focusOrigin == "cursor" && box.containsPoint(cursor) ? cursor : box.middle();
        // Hover focus: the mouse just slid in across an edge, so start on that
        // edge, at the point it crossed (the edge nearest the cursor).
        if (reason == Desktop::FOCUS_REASON_FFM) {
            const auto e = Logic::entryPoint(rect(box), vec(cursor));
            origin       = {e.x, e.y};
        }

        if (s.bounded)
            spawn({.center = origin, .radius = (float)Logic::coverRadius(vec(origin), rect(box)), .clip = box, .clipRounding = window->rounding(), .anchor = window});
        else
            spawn({.center = origin, .radius = std::max<float>(s.radius, std::min(box.w, box.h) * 0.5f), .anchor = window});
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

        float radius = (float)Logic::coverRadius(vec(origin), rect(monBox));
        if (s.workspaceRadius != "screen")
            if (auto r = Logic::parseFloat(s.workspaceRadius))
                radius = *r;
        spawn({.center = origin, .radius = radius});
    }

    // ── manual trigger (hyprctl / Lua) ───────────────────────────────────────

    void manualTrigger(std::optional<Vector2D> at, std::optional<float> radius) {
        g_state->nManual++;
        // Caller-supplied numbers: keep them in a sane layout range.
        if (at)
            at = Vector2D{std::clamp(at->x, -1e5, 1e5), std::clamp(at->y, -1e5, 1e5)};
        spawn({.center = at.value_or(g_pInputManager->getMouseCoordsInternal()), .radius = radius.value_or(g_state->settings.radius), .force = true});
    }

    std::string onHyprctl(eHyprCtlOutputFormat, std::string request) {
        if (!g_state)
            return "hyprripple: not loaded";
        if (request.size() > 2048)
            return "error: request too long";

        std::vector<std::string> args;
        {
            std::istringstream in(request);
            for (std::string tok; in >> tok && args.size() < 64;)
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
            const auto err = Logic::applySetting(g_state->settings, args[1], value);
            return err.empty() ? "ok" : "error: " + err;
        }

        if (cmd == "trigger") {
            std::optional<Vector2D> at;
            std::optional<float>    radius;
            if (args.size() >= 3) {
                auto x = Logic::parseFloat(args[1]), y = Logic::parseFloat(args[2]);
                if (!x || !y)
                    return "usage: hyprctl hyprripple trigger [x y [radius]]";
                at = Vector2D{*x, *y};
            }
            if (args.size() >= 4) {
                radius = Logic::parseFloat(args[3]);
                if (!radius)
                    return "usage: hyprctl hyprripple trigger [x y [radius]]";
            }
            manualTrigger(at, radius);
            return "ok";
        }

        if (cmd == "stats") {
            const auto  now = steady_clock::now();
            std::string list;
            for (const auto& r : g_state->ripples) {
                list += std::format(R"({}{{"x":{:.0f},"y":{:.0f},"anchored":{},"held":{},"released":{},"fade":{:.2f},"clip":[{:.0f},{:.0f},{:.0f},{:.0f}]}})",
                                    list.empty() ? "" : ",", r.center.x, r.center.y, !r.anchor.expired(), r.held, r.released.has_value(), Logic::fade(r.life(now)),
                                    r.clip ? r.clipBox.x : 0, r.clip ? r.clipBox.y : 0, r.clip ? r.clipBox.w : 0, r.clip ? r.clipBox.h : 0);
            }
            return std::format(
                R"({{"click":{},"touch":{},"focus":{},"last_focus_reason":{},"workspace":{},"manual":{},"skipped":{},"frames":{},"pre":{},"sharing":{},"active":{},"ripples":[{}]}})",
                g_state->nClick, g_state->nTouch, g_state->nFocus, g_state->lastFocusReason, g_state->nWorkspace, g_state->nManual, g_state->nSkipped, g_state->nFrames, g_state->nPre,
                g_state->sharing, g_state->ripples.size(), list);
        }

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
        if (lua_gettop(L) >= 2 && lua_isnumber(L, 1) && lua_isnumber(L, 2)) {
            const double x = lua_tonumber(L, 1), y = lua_tonumber(L, 2);
            if (!std::isfinite(x) || !std::isfinite(y))
                return luaL_error(L, "hyprripple.trigger: x and y must be finite");
            at = Vector2D{x, y};
        }
        if (lua_gettop(L) >= 3 && lua_isnumber(L, 3)) {
            const double r = lua_tonumber(L, 3);
            if (std::isfinite(r))
                radius = (float)r;
        }
        manualTrigger(at, radius);
        return 0;
    }

    // hl.plugin.hyprripple.set(key, value) - same keys and validation as hyprctl
    int luaSet(lua_State* L) {
        if (!g_state)
            return 0;
        const char* key = luaL_checkstring(L, 1);
        std::string value;
        if (lua_isboolean(L, 2))
            value = lua_toboolean(L, 2) ? "true" : "false";
        else
            value = luaL_checkstring(L, 2);
        if (auto err = Logic::applySetting(g_state->settings, key, value); !err.empty())
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

    c.enabled          = addValue<CV::Bool>("plugin:hyprripple:enabled", "Master switch", Config::BOOL{d.enabled});
    c.color            = addValue<CV::String>("plugin:hyprripple:color", "colors.toml key (accent) or a literal color", Config::STRING{d.color});
    c.opacity          = addValue<CV::Float>("plugin:hyprripple:opacity", "Peak ink opacity 0-1", Config::FLOAT{d.opacity});
    c.duration         = addValue<CV::Int>("plugin:hyprripple:duration", "Ripple lifetime in ms", Config::INTEGER{d.duration});
    c.radius           = addValue<CV::Float>("plugin:hyprripple:radius", "Click ripple radius (logical px)", Config::FLOAT{d.radius});
    c.workspaceRadius  = addValue<CV::String>("plugin:hyprripple:workspace_radius", "'screen' or px", Config::STRING{d.workspaceRadius});
    c.softness         = addValue<CV::Float>("plugin:hyprripple:softness", "Edge feather (logical px)", Config::FLOAT{d.softness});
    c.style            = addValue<CV::String>("plugin:hyprripple:style", "classic | ring | sparkle", Config::STRING{d.style});
    c.ringWidth        = addValue<CV::Float>("plugin:hyprripple:ring_width", "Ring thickness (logical px)", Config::FLOAT{d.ringWidth});
    c.sparkleAmount    = addValue<CV::Float>("plugin:hyprripple:sparkle_amount", "Sparkle density 0-1", Config::FLOAT{d.sparkleAmount});
    c.onClick          = addValue<CV::String>("plugin:hyprripple:on_click", "none | left | any", Config::STRING{d.onClick});
    c.onTouch          = addValue<CV::Bool>("plugin:hyprripple:on_touch", "Ripple on touchscreen / tablet taps", Config::BOOL{d.onTouch});
    c.hold             = addValue<CV::Bool>("plugin:hyprripple:hold", "Ink grows while held, fades on release", Config::BOOL{d.hold});
    c.onFocus          = addValue<CV::Bool>("plugin:hyprripple:on_focus", "Ripple on window focus change", Config::BOOL{d.onFocus});
    c.focusHover       = addValue<CV::Bool>("plugin:hyprripple:focus_hover", "Also ripple on focus-follows-mouse", Config::BOOL{d.focusHover});
    c.onWorkspace      = addValue<CV::Bool>("plugin:hyprripple:on_workspace", "Ripple on workspace switch", Config::BOOL{d.onWorkspace});
    c.focusOrigin      = addValue<CV::String>("plugin:hyprripple:focus_origin", "cursor | center", Config::STRING{d.focusOrigin});
    c.bounded          = addValue<CV::Bool>("plugin:hyprripple:bounded", "Clip click and focus ripples to their window", Config::BOOL{d.bounded});
    c.maxRipples       = addValue<CV::Int>("plugin:hyprripple:max_ripples", "Max simultaneous ripples", Config::INTEGER{d.maxRipples});
    c.skipFullscreen   = addValue<CV::Bool>("plugin:hyprripple:skip_fullscreen", "No ripples over fullscreen windows", Config::BOOL{d.skipFullscreen});
    c.exclude          = addValue<CV::String>("plugin:hyprripple:exclude", "Comma-separated window class globs", Config::STRING{d.exclude});
    c.pauseWhenSharing = addValue<CV::Bool>("plugin:hyprripple:pause_when_sharing", "No ripples while screen sharing", Config::BOOL{d.pauseWhenSharing});

    HyprlandAPI::addLuaFunction(PHANDLE, "hyprripple", "trigger", luaTrigger);
    HyprlandAPI::addLuaFunction(PHANDLE, "hyprripple", "set", luaSet);

    g_state->ctl = HyprlandAPI::registerHyprCtlCommand(PHANDLE, SHyprCtlCommand{.name = "hyprripple", .exact = false, .fn = onHyprctl});

    auto& ev = Event::bus()->m_events;
    g_state->listeners.push_back(ev.input.mouse.button.listen([](const IPointer::SButtonEvent& e, Event::SCallbackInfo&) {
        if (g_state)
            onMouseButton(e);
    }));
    g_state->listeners.push_back(ev.input.touch.down.listen([](const ITouch::SDownEvent& e, Event::SCallbackInfo&) {
        if (g_state)
            onTouchDown(e);
    }));
    g_state->listeners.push_back(ev.input.touch.up.listen([](const ITouch::SUpEvent& e, Event::SCallbackInfo&) {
        if (g_state)
            release(HOLD_TOUCH | (uint32_t)e.touchID);
    }));
    g_state->listeners.push_back(ev.input.touch.cancel.listen([](const ITouch::SCancelEvent&, Event::SCallbackInfo&) {
        if (!g_state)
            return;
        const auto now = steady_clock::now();
        for (auto& r : g_state->ripples)
            if (r.held && !r.released && (r.holdKey & HOLD_TOUCH))
                r.released = now;
    }));
    g_state->listeners.push_back(ev.input.tablet.tip.listen([](const CTablet::STipEvent& e, Event::SCallbackInfo&) {
        if (g_state)
            onTabletTip(e);
    }));
    g_state->listeners.push_back(ev.window.active.listen([](PHLWINDOW w, Desktop::eFocusReason reason) {
        if (g_state)
            onWindowActive(w, reason);
    }));
    g_state->listeners.push_back(ev.workspace.active.listen([](PHLWORKSPACE ws) {
        if (g_state)
            onWorkspaceActive(ws);
    }));
    g_state->listeners.push_back(ev.screenshare.state.listen([](bool start, uint8_t, const std::string&) {
        if (g_state)
            g_state->sharing = std::max(0, g_state->sharing + (start ? 1 : -1));
    }));
    g_state->listeners.push_back(ev.render.pre.listen([](PHLMONITOR mon) { onPreRender(mon); }));
    g_state->listeners.push_back(ev.render.stage.listen([](eRenderStage stage) { onRenderStage(stage); }));
    g_state->listeners.push_back(ev.config.reloaded.listen([]() {
        if (g_state)
            loadFromConfig();
    }));

    g_state->sweep = makeShared<CEventLoopTimer>(std::nullopt, onSweep, nullptr);
    g_pEventLoopManager->addTimer(g_state->sweep);

    // Values set in hyprripple.lua were skipped when the config was parsed
    // before this plugin existed; a reload picks them up.
    HyprlandAPI::reloadConfig();

    return {std::string(PLUGIN_NAME), std::string(PLUGIN_DESCRIPTION), std::string(PLUGIN_AUTHOR), std::string(PLUGIN_VERSION)};
}

APICALL EXPORT void PLUGIN_EXIT() {
    if (!g_state)
        return;

    g_state->listeners.clear();

    if (g_state->sweep) {
        g_state->sweep->cancel();
        g_pEventLoopManager->removeTimer(g_state->sweep);
    }
    if (g_state->ctl)
        HyprlandAPI::unregisterHyprCtlCommand(PHANDLE, g_state->ctl);

    for (const auto& r : g_state->ripples)
        g_pHyprRenderer->damageBox(r.lastBounds.value_or(r.bounds()));

    g_pHyprRenderer->m_renderPass.removeAllOfType("CRipplePassElement");
    RippleShader::destroy();
    g_state.reset();
}
