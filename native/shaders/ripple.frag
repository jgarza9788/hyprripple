#version 300 es
// HyprRipple - Material Design ink ripples.
//
//   mode 0  classic   Material 2 ink: a near-solid disc with a feathered rim
//   mode 1  ring      an expanding ring
//   mode 2  sparkle   Material 3 / Android 12: a soft radial glow with a
//                     turbulent edge and shimmering noise sparkles that
//                     trail the wave front and dissolve as it fades
//   mode 3  negative  the screen under the disc, colour-inverted
//   mode 4  water     a light ripple on calm water: a thin main ring and one
//                     faint follower refracting the screen gently, with a
//                     whisper of white on the crests
//   mode 5  prism     a single thick glass lens band (no oscillation):
//                     magnifies outward / pinches inward, splits R/G/B
//                     widely, and casts a spectral rainbow around the ring
//
// Modes 3-5 read `tex`, a copy of the frame rendered so far. They work in
// framebuffer pixel space (gl_FragCoord), so sampling is exact under any
// monitor rotation or flip: the ripple centre in that space is recovered
// from the screen-space derivatives of v_texcoord.
//
// Drawn once per ripple over a square box centred on the ripple, so the
// centre is always uv (0.5, 0.5) and v_texcoord maps straight to pixels.
// Everything here is in monitor-local *physical* pixels.
precision highp float;

in vec2 v_texcoord;
layout(location = 0) out vec4 fragColor;

uniform vec2  boxPos;      // top-left of the drawn box
uniform vec2  boxSize;     // size of the drawn box
uniform vec2  center;      // ripple origin
uniform float radius;      // current radius (already eased on the CPU)
uniform float alpha;       // current opacity (already faded on the CPU)
uniform vec4  inkColor;    // straight (non-premultiplied) rgba
uniform float softness;    // edge feather width
uniform float ringWidth;   // ring thickness, mode 1
uniform int   mode;        // 0 classic, 1 ring, 2 sparkle, 3 negative, 4 water, 5 prism
uniform float progress;    // fade-out: 0 until the ink starts fading, then -> 1
uniform float sparkleAmount; // 0..1, how many sparkles (mode 2)
uniform float time;        // seconds since the ripple spawned
uniform float pxScale;     // monitor scale, keeps sparkle grain size logical
uniform float seed;        // per-ripple random offset
uniform vec4  clipBox;     // x, y, w, h - only used when useClip == 1
uniform float clipRadius;  // container corner rounding
uniform int   useClip;     // 1 = bounded ripple (clipped to its container)
uniform sampler2D tex;     // modes 3-5: copy of the frame so far
uniform vec2  fbSize;      // its size in pixels
uniform float waveAmp;     // modes 4-5: peak displacement, physical px

// Signed distance to a rounded rectangle (negative inside).
float roundedBoxSDF(vec2 p, vec2 halfSize, float r) {
    r = min(r, min(halfSize.x, halfSize.y));
    vec2 q = abs(p) - halfSize + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

float valueNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash12(i), hash12(i + vec2(1, 0)), f.x),
               mix(hash12(i + vec2(0, 1)), hash12(i + vec2(1, 1)), f.x), f.y);
}

float classicInk(float dist, float soft) {
    float ink = 1.0 - smoothstep(radius - soft, radius + soft, dist);
    // Ink is a touch denser toward the leading edge than the centre.
    float edge = clamp(dist / max(radius, 1.0), 0.0, 1.0);
    return ink * mix(0.82, 1.0, edge * edge);
}

float ringInk(float dist, float soft) {
    float outer = 1.0 - smoothstep(radius - soft, radius + soft, dist);
    float inner = radius - ringWidth;
    return outer * smoothstep(inner - soft, inner + soft, dist);
}

float sparkleInk(vec2 px, float dist, float soft) {
    vec2 d = px - center;

    // Turbulent rim: low-frequency noise around the circumference nudges
    // the radius so the wave front isn't a perfect circle.
    float angle = atan(d.y, d.x);
    vec2  ap    = vec2(cos(angle), sin(angle)) * 2.2 + seed;
    float wobble = (valueNoise(ap + time * 0.9) - 0.5) * 0.10 * radius;
    float r      = max(radius + wobble, 1.0);

    // Material 3 soft edge: wide, at least 35% of the radius.
    float softEdge = max(soft, 0.35 * r);
    float body     = 1.0 - smoothstep(r - softEdge, r, dist);
    // Radial glow: brighter toward the centre of the wave, like M3's
    // radial-gradient ripple.
    float glow = body * mix(1.0, 0.55, clamp(dist / r, 0.0, 1.0));

    // Sparkles: a logical-pixel grid of cells, each twinkling on its own
    // clock, densest in a band just behind the wave front.
    float cell    = 3.0 * pxScale;
    vec2  cid     = floor(px / cell);
    float rnd     = hash12(cid + seed * 17.0);
    float frame   = floor(time * 30.0 + rnd * 7.0);
    float twinkle = hash12(cid + frame * 0.731 + seed);
    vec2  inCell  = fract(px / cell) - 0.5;
    float dotMask = 1.0 - smoothstep(0.18, 0.5, length(inCell));
    float front   = 1.0 - smoothstep(0.0, 0.42 * r, abs(dist - r * 0.82));
    float amount  = clamp(sparkleAmount, 0.0, 1.0);
    float density = mix(mix(0.995, 0.86, amount), mix(0.97, 0.66, amount), front); // more near the front
    float sparkle = step(density, twinkle) * dotMask * body;
    // Sparkles dissolve faster than the glow fades.
    sparkle *= (1.0 - smoothstep(0.0, 0.55, progress)) * (0.35 + 0.65 * front) * step(0.001, amount);

    return glow + sparkle * 1.6;
}

// The background at a framebuffer pixel.
vec3 bgAt(vec2 fbPx) {
    return texture(tex, clamp(fbPx / fbSize, vec2(0.0), vec2(1.0))).rgb;
}

// This fragment's offset from the ripple centre, in framebuffer pixels.
// v_texcoord is affine across the quad and the centre is uv (0.5, 0.5), so
// inverting d(uv)/d(fragcoord) maps (uv - 0.5) into framebuffer space -
// correct whatever rotation/flip the monitor transform applies.
vec2 fbOffsetFromCenter() {
    mat2 duv = mat2(dFdx(v_texcoord), dFdy(v_texcoord));
    return inverse(duv) * (v_texcoord - 0.5);
}

// Water: a thin main wavelet and one faint follower, 1 : 0.4.
// Returns (displacement factor, envelope).
vec2 waterWaves(float dist) {
    float w    = max(0.06 * radius, 5.0 * pxScale);
    float k    = 6.2831853 / (w * 1.1);
    float wave = 0.0;
    float env  = 0.0;
    for (int i = 0; i < 2; i++) {
        float amp = i == 0 ? 1.0 : 0.4;
        float d   = dist - radius + float(i) * 2.2 * w;
        float e   = amp * exp(-(d / w) * (d / w));
        wave += sin(d * k) * e;
        env   = max(env, e);
    }
    return vec2(wave, env);
}

// Prism: one smooth lens band - the derivative of a gaussian, so content is
// pushed outward on the outer half and pulled inward on the inner half
// (a bulge), with no ringing. Returns (displacement factor, envelope).
vec2 prismLens(float dist) {
    float w = max(0.22 * radius, 14.0 * pxScale);
    float s = (dist - radius) / w;
    float e = exp(-s * s);
    return vec2(-s * e * 2.3, e); // 2.3 ~ 1/max of s*exp(-s^2): peak ~1
}

vec3 spectrum(float h) {
    return clamp(abs(mod(h * 6.0 + vec3(0.0, 4.0, 2.0), 6.0) - 3.0) - 1.0, 0.0, 1.0);
}

void main() {
    vec2  px   = boxPos + v_texcoord * boxSize;
    float dist = distance(px, center);
    float soft = max(softness, 0.5);

    float clipMask = 1.0;
    if (useClip == 1) {
        vec2  half_ = clipBox.zw * 0.5;
        float sdf   = roundedBoxSDF(px - (clipBox.xy + half_), half_, clipRadius);
        clipMask    = 1.0 - smoothstep(-0.75, 0.75, sdf);
    }

    // ---- screen-sampling modes ------------------------------------------
    if (mode >= 3) {
        vec2  frag = gl_FragCoord.xy;
        vec2  rel  = fbOffsetFromCenter();
        vec2  dir  = rel / max(length(rel), 0.0001);
        vec3  color;
        float mask;

        if (mode == 3) {
            mask  = 1.0 - smoothstep(radius - soft, radius + soft, dist);
            color = vec3(1.0) - clamp(bgAt(frag), 0.0, 1.0);
        } else if (mode == 4) {
            vec2  wp     = waterWaves(dist);
            // a gentle bend: 60% of the wave reach budget
            vec2  offset = dir * waveAmp * 0.6 * wp.x;
            float crest  = max(wp.x, 0.0);
            mask         = clamp(wp.y * 1.3, 0.0, 1.0);
            // colourless water: just a whisper of light on the crests
            color = bgAt(frag - offset) + vec3(crest * crest * 0.08);
        } else {
            vec2 lp     = prismLens(dist);
            vec2 offset = dir * waveAmp * lp.x;
            mask        = clamp(lp.y * 1.4, 0.0, 1.0);
            color = vec3(bgAt(frag - offset).r, bgAt(frag - offset * 1.5).g, bgAt(frag - offset * 2.0).b);
            // spectral light spread around the band, slowly rotating, so it
            // reads as a prism even over a dark screen
            float ang  = atan(rel.y, rel.x) / 6.2831853;
            float band = (dist - radius) / max(0.22 * radius, 14.0 * pxScale);
            vec3  rainbow = spectrum(fract(ang + band * 0.25 + time * 0.15));
            color += rainbow * lp.y * 0.28;
        }

        float a = clamp(mask * clipMask * alpha, 0.0, 1.0);
        if (a <= 0.001)
            discard;
        fragColor = vec4(clamp(color, 0.0, 1.0) * a, a);
        return;
    }

    // ---- ink modes --------------------------------------------------------
    float ink;
    if (mode == 1)
        ink = ringInk(dist, soft);
    else if (mode == 2)
        ink = sparkleInk(px, dist, soft);
    else
        ink = classicInk(dist, soft);

    float a = clamp(ink * clipMask * alpha * inkColor.a, 0.0, 1.0);
    if (a <= 0.001)
        discard;

    // Hyprland blends premultiplied.
    fragColor = vec4(inkColor.rgb * a, a);
}
