#version 300 es
// HyprRipple - Material Design ink ripple.
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
uniform float radius;      // current ink radius (already eased on the CPU)
uniform float alpha;       // current ink opacity (already faded on the CPU)
uniform vec4  inkColor;    // straight (non-premultiplied) rgba
uniform float softness;    // edge feather width
uniform float ringWidth;   // > 0 draws a ring of this width instead of a disc
uniform vec4  clipBox;     // x, y, w, h - only used when useClip == 1
uniform float clipRadius;  // container corner rounding
uniform int   useClip;     // 1 = bounded ripple (clipped to its container)

// Signed distance to a rounded rectangle (negative inside).
float roundedBoxSDF(vec2 p, vec2 halfSize, float r) {
    r = min(r, min(halfSize.x, halfSize.y));
    vec2 q = abs(p) - halfSize + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    vec2  px   = boxPos + v_texcoord * boxSize;
    float dist = distance(px, center);
    float soft = max(softness, 0.5);

    // Disc with a feathered rim.
    float ink = 1.0 - smoothstep(radius - soft, radius + soft, dist);

    if (ringWidth > 0.0) {
        float inner = radius - ringWidth;
        ink *= smoothstep(inner - soft, inner + soft, dist);
    } else {
        // Material ink is a touch denser toward the leading edge than the
        // centre, which reads as the "splash" rather than a flat disc.
        float edge = clamp(dist / max(radius, 1.0), 0.0, 1.0);
        ink *= mix(0.82, 1.0, edge * edge);
    }

    if (useClip == 1) {
        vec2  half_  = clipBox.zw * 0.5;
        float sdf    = roundedBoxSDF(px - (clipBox.xy + half_), half_, clipRadius);
        ink *= 1.0 - smoothstep(-0.75, 0.75, sdf);
    }

    float a = clamp(ink * alpha * inkColor.a, 0.0, 1.0);
    if (a <= 0.001)
        discard;

    // Hyprland blends premultiplied.
    fragColor = vec4(inkColor.rgb * a, a);
}
