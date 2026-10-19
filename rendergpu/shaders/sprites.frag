#version 440

layout(binding = 0) uniform sampler2D atlas;
layout(location = 0) flat in vec4 source; // logical placement x,y, atlas origin x,y
layout(location = 1) flat in vec4 tint;
layout(location = 0) out vec4 fragColor;

vec4 quantize(vec4 v) {
    return floor(clamp(v, 0.0, 255.0) + 0.5);
}

void main() {
    // Same byte operations as composite.frag::SpriteCopy without a matrix. Integer texel addressing
    // cannot sample an adjacent atlas cell, including when the destination was clipped at a viewport edge.
    ivec2 texel = ivec2(gl_FragCoord.xy) - ivec2(source.xy) + ivec2(source.zw);
    vec4 s = quantize(texelFetch(atlas, texel, 0) * 255.0);
    s = quantize(s * (tint / 255.0));
    if (s.a == 0.0) discard;
    fragColor = s / 255.0;
}
