// Glass mode: adaptive ink for what sits on glass (text, icons): drawn white, it is
// recoloured here, per pixel, by what is behind the glass: dark ink over
// light content, light ink over dark, blended smoothly. Coloured ink (an
// accent, a cover) keeps its colour.
#version 440

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec4 area;          // this item within the backdrop (x, y, w, h; 0..1)
    vec4 dark;          // ink over light content
    vec4 light;         // ink over dark content
    vec2 pixel;         // one pixel of this layer, texture coordinates
    float halo;         // strength of the soft halo behind the ink (0..1)
    float haloRadius;   // its reach, pixels
    float keepColour;   // 1: coloured content (an accent, a cover) keeps its colour
};
layout(binding = 1) uniform sampler2D source;
layout(binding = 2) uniform sampler2D backdrop;

void main() {
    vec4 s = texture(source, qt_TexCoord0);
    // The tone of everything behind this ink, one value for all of it (a 5x3
    // grid across its area): a word never comes out half dark, half light,
    // as it did when every pixel chose for itself.
    vec3 behind = vec3(0.0);
    for (int gx = 0; gx < 5; ++gx)
        for (int gy = 0; gy < 3; ++gy)
            behind += texture(backdrop, area.xy + vec2((float(gx) + 0.5) / 5.0, (float(gy) + 0.5) / 3.0) * area.zw).rgb;
    behind /= 15.0;
    float lum = dot(behind, vec3(0.2126, 0.7152, 0.0722));
    float overLight = smoothstep(0.42, 0.62, lum);
    vec4 ink = mix(light, dark, overLight);

    // A soft halo in the opposite tone around every glyph: dark behind light
    // ink, light behind dark ink. The glyphs keep their colour and stand
    // out from busy content.
    float around = 0.0;
    for (int i = 0; i < 12; ++i) {
        float a = float(i) * 0.5235988;
        vec2 o = vec2(cos(a), sin(a)) * pixel * haloRadius;
        around += texture(source, qt_TexCoord0 + o).a + texture(source, qt_TexCoord0 + o * 0.5).a;
    }
    around /= 24.0;
    // Behind dark ink (over light content) only a faint halo: a strong light
    // glow there reads as a doubled outline.
    float haloA = halo * mix(1.0, 0.35, overLight) * smoothstep(0.0, 0.55, around);
    vec3 haloColor = mix(vec3(0.0), vec3(1.0), overLight);
    vec4 under = vec4(haloColor * haloA, haloA);

    if (s.a <= 0.0) { fragColor = under * qt_Opacity; return; }
    // Plain ink (the usual case): only how much is drawn counts, not its
    // colour, so text drawn with coloured subpixel edges inks cleanly;
    // dimmer text (drawn with less alpha) stays dimmer.
    if (keepColour < 0.5) {
        float cover = max(s.a, max(s.r, max(s.g, s.b)));
        float a = cover * ink.a;
        fragColor = (vec4(ink.rgb * a, a) + under * (1.0 - a)) * qt_Opacity;
        return;
    }
    // Unpremultiply: how coloured is this pixel of the content?
    vec3 rgb = s.rgb / s.a;
    float chroma = max(rgb.r, max(rgb.g, rgb.b)) - min(rgb.r, min(rgb.g, rgb.b));
    // Only solid pixels count as coloured: a glyph's edges (Windows draws them
    // with coloured subpixel fringes) take the ink like the rest of it.
    float grey = 1.0 - smoothstep(0.12, 0.3, chroma) * smoothstep(0.85, 1.0, s.a);
    // Greys (white text, icons) take the ink, keeping their own brightness
    // as its strength (secondary text drawn dimmer stays dimmer).
    float strength = max(rgb.r, max(rgb.g, rgb.b));
    vec3 inked = ink.rgb;
    float inkAlpha = ink.a * mix(1.0, strength, 0.6);
    vec3 outRgb = mix(rgb, inked, grey);
    float outA = s.a * mix(1.0, inkAlpha, grey);
    // The glyph over its halo (premultiplied "over").
    vec4 glyph = vec4(outRgb * outA, outA);
    fragColor = (glyph + under * (1.0 - outA)) * qt_Opacity;
}
