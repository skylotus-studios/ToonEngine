// Vertex shader for loaded 3D models.
// Transforms positions by MVP, passes world-space normal and texcoords
// to the fragment stage for lighting.

#version 410 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;

uniform mat4 uMVP;
uniform mat4 uModel;

out vec3 vNormal;
out vec2 vTexCoord;

void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);

    // Transform normal to world space. mat3(uModel) is correct for
    // uniform scale; use transpose(inverse(mat3(uModel))) if non-uniform
    // scale is needed later.
    vNormal = mat3(uModel) * aNormal;

    vTexCoord = aTexCoord;
}
