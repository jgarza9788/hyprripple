#pragma once

// Hyprland 0.56 / 0.57 compatibility layer.
//
// 0.57 threads a Render::CRenderContext through the render pass, moved the
// window/workspace headers, replaced the hyprctl/socket2 APIs and wrapped some
// window fields behind accessors. Everything that differs lives here so the
// plugin source can be written once against the 0.57 shape.
//
// HL_VERSION_MINOR comes from the Makefile (pkg-config --modversion hyprland).

#ifndef HL_VERSION_MINOR
#error "HL_VERSION_MINOR is not defined; build with the Makefile (it reads pkg-config --modversion hyprland)"
#endif

#if HL_VERSION_MINOR >= 57
#define HLC_57 1
#else
#define HLC_57 0
#endif

#include <functional>
#include <optional>
#include <string>

#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/desktop/view/LayerSurface.hpp>

#if HLC_57
#include <hyprland/src/workspace/HLWorkspace.hpp>
#include <hyprland/src/desktop/view/window/Window.hpp>
#include <hyprland/src/desktop/view/window/WindowPresentation.hpp>
#include <hyprland/src/render/WindowRenderPresentation.hpp>
#include <hyprland/src/ipc/s2/S2.hpp>
#else
#include <hyprland/src/desktop/Workspace.hpp>
#include <hyprland/src/desktop/view/Window.hpp>
#include <hyprland/src/managers/EventManager.hpp>
#endif

namespace hlc {

#if HLC_57
    using Ctx = Render::CRenderContext;
#else
    // 0.56 keeps the render state on the renderer itself; this gives it the
    // 0.57 context's member names so call sites read the same.
    struct Ctx {
        Render::SRenderData& m_data              = g_pHyprRenderer->m_renderData;
        Render::CRenderPass& m_pass              = g_pHyprRenderer->m_renderPass;
        bool&                m_renderingSnapshot = g_pHyprRenderer->m_bRenderingSnapshot;
    };
    inline Ctx& ctx() {
        static Ctx c;
        return c;
    }
#endif

// Parameter list for IPassElement overrides: HLC_CTX_PARAM(ctx) is
// `Render::CRenderContext& ctx` on 0.57 and nothing on 0.56. A body that uses
// the context starts with HLC_CTX_BIND(ctx), which on 0.56 declares it.
#if HLC_57
#define HLC_CTX_PARAM(name) Render::CRenderContext& name
#define HLC_CTX_BIND(name)  (void)name
#else
#define HLC_CTX_PARAM(name)
#define HLC_CTX_BIND(name) auto& name = ::hlc::ctx()
#endif

// IHyprWindowDecoration::draw parameter list.
#if HLC_57
#define HLC_DECO_DRAW_PARAMS(ctx, mon, a) Render::CRenderContext& ctx, PHLMONITOR mon, float const& a, const Render::SWindowRenderPresentation&
#else
#define HLC_DECO_DRAW_PARAMS(ctx, mon, a) PHLMONITOR mon, float const& a
#endif

    inline Mat3x3 projectBox(Ctx& ctx, const CBox& box, std::optional<eTransform> transform = std::nullopt) {
#if HLC_57
        return g_pHyprRenderer->projectBoxToTarget(ctx, box, transform);
#else
        (void)ctx;
        return g_pHyprRenderer->projectBoxToTarget(box, transform);
#endif
    }

    inline void scissor(Ctx& ctx, const CBox& box) {
#if HLC_57
        Render::GL::g_pHyprOpenGL->scissor(ctx, box);
#else
        (void)ctx;
        Render::GL::g_pHyprOpenGL->scissor(box);
#endif
    }

    inline void disableScissor() {
#if HLC_57
        Render::GL::g_pHyprOpenGL->disableScissor();
#else
        Render::GL::g_pHyprOpenGL->scissor(static_cast<const pixman_box32*>(nullptr));
#endif
    }

    // ── render stage event ───────────────────────────────────────────────────
#if HLC_57
    using RenderStageEvent = Event::SRenderStageEvent;
#else
    struct RenderStageEvent {
        eRenderStage                              stage;
        PHLMONITOR                                monitor;
        std::optional<std::reference_wrapper<Ctx>> context;
    };
#endif

    // Subscribe to render stages with the 0.57 event shape on either version.
    template <typename F>
    inline auto listenRenderStage(F&& fn) {
#if HLC_57
        return Event::bus()->m_events.render.stage.listen([fn = std::forward<F>(fn)](const Event::SRenderStageEvent& e) { fn(e); });
#else
        return Event::bus()->m_events.render.stage.listen([fn = std::forward<F>(fn)](eRenderStage stage) {
            fn(RenderStageEvent{.stage = stage, .monitor = g_pHyprRenderer->m_renderData.pMonitor.lock(), .context = std::ref(ctx())});
        });
#endif
    }

    // ── windows / layers ─────────────────────────────────────────────────────
    inline bool mapped(const PHLWINDOW& w) {
#if HLC_57
        return w->mapped();
#else
        return w->m_isMapped;
#endif
    }

    inline bool mapped(const PHLLS& ls) {
#if HLC_57
        return ls->mapped();
#else
        return ls->m_mapped;
#endif
    }

    inline bool pinned(const PHLWINDOW& w) {
#if HLC_57
        return w->m_state & Desktop::View::WINDOW_STATE_PINNED;
#else
        return w->m_pinned;
#endif
    }

    inline Vector2D floatingOffset(const PHLWINDOW& w) {
#if HLC_57
        return w->presentation().floatingOffset();
#else
        return w->m_floatingOffset;
#endif
    }

    inline std::string appID(const PHLWINDOW& w) {
#if HLC_57
        return w->metadata().appID();
#else
        return w->m_class;
#endif
    }

    inline float rounding(const PHLWINDOW& w) {
#if HLC_57
        return w->presentation().rounding();
#else
        return w->rounding();
#endif
    }

    inline const auto& decorations(const PHLWINDOW& w) {
#if HLC_57
        return w->presentation().decorations();
#else
        return w->m_windowDecorations;
#endif
    }

    template <typename T>
    inline bool addDecoration(HANDLE handle, const PHLWINDOW& w) {
#if HLC_57
        return HyprlandAPI::addWindowDecoration(handle, w, makeShared<T>(w));
#else
        return HyprlandAPI::addWindowDecoration(handle, w, makeUnique<T>(w));
#endif
    }

    // ── IPC ──────────────────────────────────────────────────────────────────
#if HLC_57
    using CtlCommand = SP<IPC::Socket1::SCommand>;
#else
    using CtlCommand = SP<SHyprCtlCommand>;
#endif

    // Register a prefix-matched `hyprctl <name> ...` command.
    inline CtlCommand registerCtl(HANDLE handle, const std::string& name, std::function<std::string(std::string)> fn) {
#if HLC_57
        return HyprlandAPI::registerHyprCtlCommand(handle,
                                                   IPC::Socket1::SCommand{.name    = name,
                                                                          .match   = IPC::Socket1::COMMAND_MATCH_PREFIX,
                                                                          .handler = [fn](const IPC::Socket1::SRequest& r) { return IPC::Socket1::SResponse(fn(r.command)); }});
#else
        return HyprlandAPI::registerHyprCtlCommand(handle, SHyprCtlCommand{.name = name, .exact = false, .fn = [fn](eHyprCtlOutputFormat, std::string r) { return fn(r); }});
#endif
    }

    // Post a socket2 event.
    inline void postEvent(const std::string& event, const std::string& data) {
#if HLC_57
        if (IPC::Socket2::sock())
            IPC::Socket2::sock()->postEvent(IPC::Socket2::SEvent{event, data});
#else
        if (g_pEventManager)
            g_pEventManager->postEvent(SHyprIPCEvent{event, data});
#endif
    }

    // On 0.56 queued pass elements outlive the plugin unless removed on exit.
    inline void removePassElements(const char* passName) {
#if !HLC_57
        g_pHyprRenderer->m_renderPass.removeAllOfType(passName);
#else
        (void)passName;
#endif
    }

} // namespace hlc
