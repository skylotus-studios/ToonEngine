// Fullscreen triangle vertex shader for post-processing.
// Generates a screen-covering triangle from gl_VertexID (no VBO needed).

#version 410 core

out vec2 vTexCoord;

void main() {
    // Vertices: (-1,-1), (3,-1), (-1,3) — covers the entire screen.
    vec2 pos = vec2((gl_VertexID << 1) & 2, gl_VertexID & 2);
    vTexCoord   = pos;
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
