#include "RipplePassElement.hpp"

#include <GLES3/gl32.h>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/OpenGL.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/render/Shader.hpp>
#include <hyprland/src/render/Framebuffer.hpp>
#include <hyprland/src/render/gl/GLFramebuffer.hpp>

#include <cmath>

namespace {
    const char* RIPPLE_FRAG =
#include "ripple.frag.inc"
        ;

    struct SUniforms {
        GLint boxPos = -1, boxSize = -1, center = -1, radius = -1, alpha = -1, inkColor = -1, softness = -1, ringWidth = -1, mode = -1, progress = -1, time = -1, pxScale = -1, seed = -1, sparkleAmount = -1, tex = -1, fbSize = -1, waveAmp = -1, clipBox = -1, clipRadius = -1, useClip = -1;
    };

    SP<CShader>              s_shader;
    SUniforms                s_u;
    bool                     s_failed = false;
    SP<Render::IFramebuffer> s_sample; // copy of the frame, for negative / water / prism

    Render::GL::CGLFramebuffer* asGL(const SP<Render::IFramebuffer>& fb) {
        return fb ? dynamic_cast<Render::GL::CGLFramebuffer*>(fb.get()) : nullptr;
    }

    // Copy everything rendered so far into s_sample, then hand the target
    // back. Whole-frame copy: one blit, and the shader can sample at
    // gl_FragCoord without any coordinate juggling.
    bool copyFrame() {
        const auto src   = g_pHyprRenderer->m_renderData.currentFB;
        auto*      srcGL = asGL(src);
        if (!srcGL || src->m_size.x < 1 || src->m_size.y < 1)
            return false;

        if (!s_sample)
            s_sample = g_pHyprRenderer->createFB("hyprripple-sample");
        if (s_sample->m_size != src->m_size || s_sample->m_drmFormat != src->m_drmFormat)
            s_sample->alloc((int)src->m_size.x, (int)src->m_size.y, src->m_drmFormat);
        auto* dstGL = asGL(s_sample);
        if (!dstGL)
            return false;

        const int w = (int)src->m_size.x, h = (int)src->m_size.y;
        // The pass scissors elements to their damage; that would clip the blit.
        Render::GL::g_pHyprOpenGL->setCapStatus(GL_SCISSOR_TEST, false);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, srcGL->getFBID());
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, dstGL->getFBID());
        glBlitFramebuffer(0, 0, w, h, 0, 0, w, h, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_FRAMEBUFFER, srcGL->getFBID());
        return true;
    }

    bool ensureShader() {
        if (s_shader)
            return true;
        if (s_failed)
            return false;

        auto shader = makeShared<CShader>();
        if (!shader->createProgram(Render::GL::g_pHyprOpenGL->m_shaders->TEXVERTSRC, RIPPLE_FRAG, true)) {
            s_failed = true;
            HyprlandAPI::addNotification(PHANDLE, std::format("[{}] ripple shader failed to compile", PLUGIN_NAME), CHyprColor{1.0, 0.2, 0.2, 1.0}, 6000);
            return false;
        }

        const auto p = shader->program();
        s_u.boxPos     = glGetUniformLocation(p, "boxPos");
        s_u.boxSize    = glGetUniformLocation(p, "boxSize");
        s_u.center     = glGetUniformLocation(p, "center");
        s_u.radius     = glGetUniformLocation(p, "radius");
        s_u.alpha      = glGetUniformLocation(p, "alpha");
        s_u.inkColor   = glGetUniformLocation(p, "inkColor");
        s_u.softness   = glGetUniformLocation(p, "softness");
        s_u.ringWidth  = glGetUniformLocation(p, "ringWidth");
        s_u.mode       = glGetUniformLocation(p, "mode");
        s_u.progress   = glGetUniformLocation(p, "progress");
        s_u.time       = glGetUniformLocation(p, "time");
        s_u.pxScale    = glGetUniformLocation(p, "pxScale");
        s_u.seed       = glGetUniformLocation(p, "seed");
        s_u.sparkleAmount = glGetUniformLocation(p, "sparkleAmount");
        s_u.tex        = glGetUniformLocation(p, "tex");
        s_u.fbSize     = glGetUniformLocation(p, "fbSize");
        s_u.waveAmp    = glGetUniformLocation(p, "waveAmp");
        s_u.clipBox    = glGetUniformLocation(p, "clipBox");
        s_u.clipRadius = glGetUniformLocation(p, "clipRadius");
        s_u.useClip    = glGetUniformLocation(p, "useClip");
        s_shader       = shader;
        return true;
    }
}

void RippleShader::destroy() {
    if (s_shader)
        s_shader->destroy();
    s_shader.reset();
    s_sample.reset(); // frees the frame copy's GL objects
    s_failed = false;
}

// How far past the radius this ripple can draw or sample (logical px).
static float overreach(const SRipple& rp, float radius) {
    return radius * (rp.mode == 2 ? 0.08f : 0.f) + Logic::waveReach(rp.mode, rp.maxRadius, rp.distortion) + rp.softness + 2.f;
}

bool CRipplePassElement::disableSimplification() {
    return Logic::isSamplingMode(m_data.ripple.mode);
}

CRipplePassElement::CRipplePassElement(const SData& data) : m_data(data) {}

std::optional<CBox> CRipplePassElement::boundingBox() {
    const auto mon = g_pHyprRenderer->m_renderData.pMonitor.lock();
    if (!mon)
        return std::nullopt;

    const float r = m_data.radius + overreach(m_data.ripple, m_data.radius);
    CBox        box{m_data.ripple.center.x - r, m_data.ripple.center.y - r, r * 2.0, r * 2.0};
    if (m_data.ripple.clip)
        box = box.intersection(m_data.ripple.clipBox.copy().expand(2));
    box.translate(-mon->m_position);
    return box;
}

std::vector<UP<IPassElement>> CRipplePassElement::draw() {
    const auto mon = g_pHyprRenderer->m_renderData.pMonitor.lock();
    if (!mon || m_data.alpha <= 0.001f || m_data.radius <= 0.f || !ensureShader())
        return {};

    const auto& rp    = m_data.ripple;
    const float scale = mon->m_scale;

    // Square box around the ripple, monitor-local physical pixels. Keeping the
    // box centred on the ripple makes the uv -> pixel mapping orientation-free.
    const Vector2D centerPx = (rp.center - mon->m_position) * scale;
    const float    half     = (m_data.radius + overreach(rp, m_data.radius)) * scale;
    CBox           box{centerPx.x - half, centerPx.y - half, half * 2.0, half * 2.0};
    box.round();

    const auto transform = Math::wlTransformToHyprutils(Math::invertTransform(mon->m_transform));
    Mat3x3     proj      = g_pHyprRenderer->projectBoxToTarget(box, transform);
    proj.transpose();

    const bool sampling = Logic::isSamplingMode(rp.mode);
    if (sampling && !copyFrame())
        return {};

    Render::GL::g_pHyprOpenGL->blend(true);
    auto shader = Render::GL::g_pHyprOpenGL->useShader(s_shader);

    if (sampling) {
        glActiveTexture(GL_TEXTURE0);
        s_sample->getTexture()->bind();
        glUniform1i(s_u.tex, 0);
        glUniform2f(s_u.fbSize, s_sample->m_size.x, s_sample->m_size.y);
        // Base amplitude; prism's widest channel reaches 2x (see waveReach).
        glUniform1f(s_u.waveAmp, Logic::waveReach(Logic::MODE_WATER, rp.maxRadius, rp.distortion) * scale);
    }

    shader->setUniformMatrix3fv(SHADER_PROJ, 1, GL_FALSE, proj.getMatrix());
    glUniform2f(s_u.boxPos, box.x, box.y);
    glUniform2f(s_u.boxSize, box.w, box.h);
    glUniform2f(s_u.center, centerPx.x, centerPx.y);
    glUniform1f(s_u.radius, m_data.radius * scale);
    glUniform1f(s_u.alpha, m_data.alpha);
    glUniform4f(s_u.inkColor, rp.color.r, rp.color.g, rp.color.b, rp.color.a);
    glUniform1f(s_u.softness, rp.softness * scale);
    glUniform1f(s_u.ringWidth, std::max(rp.ringWidth, 1.f) * scale);
    glUniform1i(s_u.mode, rp.mode);
    glUniform1f(s_u.progress, m_data.fade);
    glUniform1f(s_u.sparkleAmount, rp.sparkleAmount);
    glUniform1f(s_u.time, m_data.time);
    glUniform1f(s_u.pxScale, scale);
    glUniform1f(s_u.seed, rp.seed);

    if (rp.clip) {
        const CBox clip = rp.clipBox.copy().translate(-mon->m_position).scale(scale);
        glUniform4f(s_u.clipBox, clip.x, clip.y, clip.w, clip.h);
        glUniform1f(s_u.clipRadius, rp.clipRounding * scale);
        glUniform1i(s_u.useClip, 1);
    } else
        glUniform1i(s_u.useClip, 0);

    glBindVertexArray(shader->getUniformLocation(SHADER_SHADER_VAO));
    Render::GL::g_pHyprOpenGL->scissor(box);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    Render::GL::g_pHyprOpenGL->scissor(nullptr);
    glBindVertexArray(0);

    return {};
}
