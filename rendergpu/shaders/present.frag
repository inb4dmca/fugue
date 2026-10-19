#version 440
layout(binding = 0, std140) uniform View {
    vec4 imageRect; // physical pixels, logical top-left orientation
    vec4 target;    // output width/height, native framebuffer Y-up flag, smooth sampling
} u;
layout(binding = 1) uniform sampler2D scene;
layout(binding = 2) uniform sampler2D overlay;
layout(location = 0) out vec4 fragColor;
void main() {
    vec2 pos = gl_FragCoord.xy;
    if (u.target.z != 0.0) pos.y = u.target.y - pos.y;
    vec2 uv = (pos - u.imageRect.xy) / u.imageRect.zw;
    vec3 rgb = vec3(0.0);
    if (all(greaterThanEqual(uv, vec2(0.0))) && all(lessThan(uv, vec2(1.0)))) {
        // Executor row zero is logical top even on GL: no additional texture-coordinate flip.
        // QPainter's point resampler chooses the lower texel at exact pixel-centre ties (odd scaled sizes).
        if (u.target.w == 0.0) uv -= vec2(0.000001) / vec2(textureSize(scene, 0));
        vec4 s = texture(scene, uv);
        rgb = s.rgb * s.a;
    }
    vec4 o = texture(overlay, pos / u.target.xy); // premultiplied Qt diagnostics, not part of map screenshots
    fragColor = vec4(o.rgb + rgb * (1.0 - o.a), 1.0);
}
