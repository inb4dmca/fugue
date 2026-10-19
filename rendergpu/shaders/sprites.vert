#version 440

layout(location = 0) in vec4 instanceRect;
layout(location = 1) in vec4 instanceSource;
layout(location = 2) in vec4 instanceTint;
layout(location = 3) in vec4 instanceView;
layout(location = 0) flat out vec4 source;
layout(location = 1) flat out vec4 tint;

void main() {
    // One ordered triangle strip per instance. Clip integer rectangles on the CPU, not with per-sprite scissors.
    vec2 corner = vec2(gl_VertexIndex & 1, (gl_VertexIndex >> 1) & 1);
    vec2 pos = mix(instanceRect.xy, instanceRect.zw, corner);
    vec2 ndc = pos * instanceView.xy - 1.0;
    ndc.y *= instanceView.z;
    gl_Position = vec4(ndc, 0.0, 1.0);
    source = instanceSource;
    tint = instanceTint;
}
