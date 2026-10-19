#version 440

// A full-screen triangle, without vertex buffers. Fragment coordinates address texture rows directly,
// so row zero is the logical top of the image even on a Y-up framebuffer backend.
void main() {
    vec2 p = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(p * 2.0 - 1.0, 0.0, 1.0);
}
