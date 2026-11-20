#version 410 core
// sky_vertex.glsl — full-screen triangle generated from gl_VertexID (no buffers).
out vec2 vNdc;

void main() {
    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2) * 2.0 - 1.0;
    vNdc = pos;
    gl_Position = vec4(pos, 1.0, 1.0);
}
