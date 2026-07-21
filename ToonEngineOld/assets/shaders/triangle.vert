// Vertex shader: transforms positions by the model-view-projection matrix
// and passes per-vertex color and texture coordinates to the fragment stage.

#version 410 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aColor;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uMVP;  // model-view-projection matrix

out vec3 vColor;
out vec2 vTexCoord;

void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    vColor = aColor;
    vTexCoord = aTexCoord;
}
