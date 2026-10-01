#version 300 es
// HyprRipple - Material Design ink ripples.
//
//   mode 0  classic   Material 2 ink: a near-solid disc with a feathered rim
//   mode 1  ring      an expanding ring
//   mode 2  sparkle   Material 3 / Android 12: a soft radial glow with a
//                     turbulent edge and shimmering noise sparkles that
//                     trail the wave front and dissolve as it fades
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
uniform int   mode;        // 0 classic, 1 ring, 2 sparkle
uniform float progress;    // 0..1 over the ripple's life
uniform float time;        // seconds since the ripple spawned
uniform float pxScale;     // monitor scale, keeps sparkle grain size logical
uniform float seed;        // per-ripple random offset
uniform vec4  clipBox;     // x, y, w, h - only used when useClip == 1
uniform float clipRadius;  // container corner rounding
uniform int   useClip;     // 1 = bounded ripple (clipped to its container)

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
    float density = mix(0.94, 0.80, front);                 // more near the front
    float sparkle = step(density, twinkle) * dotMask * body;
    // Sparkles dissolve faster than the glow fades.
    sparkle *= (1.0 - smoothstep(0.35, 0.85, progress)) * (0.35 + 0.65 * front);

    return glow + sparkle * 1.6;
}

void main() {
    vec2  px   = boxPos + v_texcoord * boxSize;
    float dist = distance(px, center);
    float soft = max(softness, 0.5);

    float ink;
    if (mode == 1)
        ink = ringInk(dist, soft);
    else if (mode == 2)
        ink = sparkleInk(px, dist, soft);
    else
        ink = classicInk(dist, soft);

    if (useClip == 1) {
        vec2  half_ = clipBox.zw * 0.5;
        float sdf   = roundedBoxSDF(px - (clipBox.xy + half_), half_, clipRadius);
        ink *= 1.0 - smoothstep(-0.75, 0.75, sdf);
    }

    float a = clamp(ink * alpha * inkColor.a, 0.0, 1.0);
    if (a <= 0.001)
        discard;

    // Hyprland blends premultiplied.
    fragColor = vec4(inkColor.rgb * a, a);
}
