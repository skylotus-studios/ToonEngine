// Outline vertex shader — inverted hull method.
// Extrudes each vertex along its normal by uOutlineWidth, then projects.
// Rendered with front-face culling so only back-facing edges are visible,
// producing a silhouette outline around the model.

#version 410 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;  // unused, but must match layout

uniform mat4  uMVP;
uniform float uOutlineWidth;

void main() {
    vec3 pos = aPos + normalize(aNormal) * uOutlineWidth;
    gl_Position = uMVP * vec4(pos, 1.0);
}
