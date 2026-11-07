// Glass mode's liquid glass: up to four rounded shapes on one surface, joined
// by a smooth union (pieces near each other flow together like droplets and
// pull apart with a neck). The look, settled with the user:
//  - a light blur of what is behind (it stays recognisable);
//  - a smooth turbulence ripple (the "liquid"), stronger while a piece moves;
//  - a soft lens at the rim;
//  - a light white tint, the same everywhere;
//  - thin inner highlights: top-left bright, bottom-right fainter.
// An "inset" shape (a selected tab) is a soft white fill inside the surface
// (as the drafts have it), which bends like a lens only while it moves or
// is pressed. Rebuild glass.frag.qsb with build.sh after changing it.
#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 size;          // this item, pixels
    vec4 area;          // this item within the backdrop (x, y, w, h; 0..1)
    vec2 texel;         // one backdrop pixel
    vec4 s0; vec4 s1; vec4 s2; vec4 s3;   // shapes: x, y, w, h (pixels, in this item)
    vec4 radii;         // their corner radii
    vec4 lift;          // 0..1 per shape: pressed, hovered or moving
    vec4 inset;         // 1 = a lens inside the surface
    float count;        // shapes in use
    float merge;        // smooth-union reach, pixels
    float blurPx;       // light blur, backdrop pixels
    float wave;         // ripple strength, pixels
    float waveScale;    // ripple size, pixels per wave
    float seed;         // moves the ripple field
    float tint;         // how much tint
    vec4 tintColor;     // its colour: white for the bars; the theme's surface for menus and sheets
    float bend;         // rim lens, pixels
    float rimWidth;     // rim width, pixels
};
layout(binding = 1) uniform sampler2D backdrop;

float box(vec2 p, vec4 s, float r) {
    vec2 c = s.xy + s.zw * 0.5;
    r = min(r, min(s.z, s.w) * 0.5);
    vec2 q = abs(p - c) - (s.zw * 0.5 - vec2(r));
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float smin(float a, float b, float k) {
    float h = clamp(0.5 + 0.5 * (b - a) / max(k, 0.0001), 0.0, 1.0);
    return mix(b, a, h) - k * h * (1.0 - h);
}

float surface(vec2 p) {
    float d = 1e5;
    if (count > 0.5 && inset.x < 0.5) d = smin(d, box(p, s0, radii.x), merge);
    if (count > 1.5 && inset.y < 0.5) d = smin(d, box(p, s1, radii.y), merge);
    if (count > 2.5 && inset.z < 0.5) d = smin(d, box(p, s2, radii.z), merge);
    if (count > 3.5 && inset.w < 0.5) d = smin(d, box(p, s3, radii.w), merge);
    return d;
}

float lens(vec2 p) {
    float d = 1e5;
    if (count > 0.5 && inset.x > 0.5) d = min(d, box(p, s0, radii.x));
    if (count > 1.5 && inset.y > 0.5) d = min(d, box(p, s1, radii.y));
    if (count > 2.5 && inset.z > 0.5) d = min(d, box(p, s2, radii.z));
    if (count > 3.5 && inset.w > 0.5) d = min(d, box(p, s3, radii.w));
    return d;
}

// The lift here: the nearest shape's, fading over 30 px.
float liftAt(vec2 p) {
    float l = 0.0;
    if (count > 0.5) l = max(l, lift.x * clamp(1.0 - max(box(p, s0, radii.x), 0.0) / 30.0, 0.0, 1.0));
    if (count > 1.5) l = max(l, lift.y * clamp(1.0 - max(box(p, s1, radii.y), 0.0) / 30.0, 0.0, 1.0));
    if (count > 2.5) l = max(l, lift.z * clamp(1.0 - max(box(p, s2, radii.z), 0.0) / 30.0, 0.0, 1.0));
    if (count > 3.5) l = max(l, lift.w * clamp(1.0 - max(box(p, s3, radii.w), 0.0) / 30.0, 0.0, 1.0));
    return l;
}

vec2 normalOf(vec2 p, bool inner) {
    const float e = 1.0;
    vec2 g = inner
        ? vec2(lens(p + vec2(e, 0.0)) - lens(p - vec2(e, 0.0)), lens(p + vec2(0.0, e)) - lens(p - vec2(0.0, e)))
        : vec2(surface(p + vec2(e, 0.0)) - surface(p - vec2(e, 0.0)), surface(p + vec2(0.0, e)) - surface(p - vec2(0.0, e)));
    return length(g) > 0.0001 ? normalize(g) : vec2(0.0, -1.0);
}

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), u.x), mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), u.x), u.y);
}
float fbm(vec2 p) { return 0.65 * noise(p) + 0.35 * noise(p * 2.03 + 17.0); }

vec3 lightBlur(vec2 uv) {
    vec3 sum = texture(backdrop, uv).rgb * 2.0;
    float w = 2.0;
    for (int i = 0; i < 8; ++i) {
        float a = float(i) * 0.785398;
        vec2 o = vec2(cos(a), sin(a)) * blurPx * texel;
        sum += texture(backdrop, uv + o).rgb;
        sum += texture(backdrop, uv + o * 0.5).rgb;
        w += 2.0;
    }
    return sum / w;
}

void main() {
    vec2 p = qt_TexCoord0 * size;
    float d = surface(p);
    float coverage = clamp(0.5 - d, 0.0, 1.0);
    if (coverage <= 0.0) { fragColor = vec4(0.0); return; }

    float l = liftAt(p);
    vec2 n = normalOf(p, false);

    // The ripple (stronger while lifted: a moving piece wobbles) and the rim lens.
    vec2 q = p / waveScale + vec2(seed * 0.13, seed * 0.07);
    vec2 ripple = vec2(fbm(q), fbm(q + vec2(5.2, 1.3))) - 0.5;
    float rw = rimWidth * (1.0 + 0.5 * l);
    float rim = clamp(1.0 + d / rw, 0.0, 1.0);
    float rimLens = smoothstep(0.0, 1.0, rim);
    vec2 offset = ripple * 2.0 * wave * (1.0 + 1.6 * l) - n * rimLens * rimLens * bend * (1.0 + 0.6 * l);

    // An inset lens bends again inside its own rim.
    float dl = lens(p);
    float inner = 0.0;
    vec2 nl = vec2(0.0, -1.0);
    if (dl < 0.5) {
        nl = normalOf(p, true);
        inner = clamp(1.0 + dl / (rimWidth * 0.8), 0.0, 1.0);
        float il = smoothstep(0.0, 1.0, inner);
        offset += -nl * il * il * bend * 0.8 * l;
    }

    vec2 uv = area.xy + clamp((p + offset) / size, 0.0, 1.0) * area.zw;
    vec3 c = lightBlur(uv);
    c = mix(c, tintColor.rgb, tint);
    c = mix(c, vec3(1.0), 0.05 * l + (dl < 0.5 ? 0.16 : 0.0));

    // Thin inner highlights (inset 2px 2px 1px and -1px -1px 1px, white .5).
    float inside = -d;
    float topLeft = smoothstep(3.5, 1.0, inside) * clamp(dot(n, vec2(-0.7071, -0.7071)) * 1.4, 0.0, 1.0);
    float bottomRight = smoothstep(2.5, 0.8, inside) * clamp(dot(n, vec2(0.7071, 0.7071)) * 1.4, 0.0, 1.0);
    c = mix(c, vec3(1.0), (0.5 * topLeft + 0.45 * bottomRight) * (1.0 + 0.4 * l));
    if (dl < 0.5) {
        float innerIn = -dl;
        float tl = smoothstep(3.0, 0.8, innerIn) * clamp(dot(nl, vec2(-0.7071, -0.7071)) * 1.4, 0.0, 1.0);
        float br = smoothstep(2.2, 0.6, innerIn) * clamp(dot(nl, vec2(0.7071, 0.7071)) * 1.4, 0.0, 1.0);
        c = mix(c, vec3(1.0), (0.18 * tl + 0.12 * br) * (1.0 + l));
    }

    fragColor = vec4(clamp(c, 0.0, 1.0), 1.0) * coverage * qt_Opacity;
}
