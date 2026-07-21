// Vertex shader for loaded 3D models.
// Supports optional skeletal animation via uSkinned + uJoints[].

#version 410 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aBoneIds;
layout(location = 4) in vec4 aBoneWeights;

uniform mat4 uMVP;
uniform mat4 uModel;
uniform bool uSkinned;
uniform mat4 uJoints[128];

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoord;

void main() {
    vec4 pos;
    vec3 norm;

    if (uSkinned) {
        mat4 skin = aBoneWeights.x * uJoints[int(aBoneIds.x)]
                  + aBoneWeights.y * uJoints[int(aBoneIds.y)]
                  + aBoneWeights.z * uJoints[int(aBoneIds.z)]
                  + aBoneWeights.w * uJoints[int(aBoneIds.w)];
        pos  = skin * vec4(aPos, 1.0);
        norm = mat3(skin) * aNormal;
    } else {
        pos  = vec4(aPos, 1.0);
        norm = aNormal;
    }

    gl_Position = uMVP * pos;
    vWorldPos   = vec3(uModel * pos);
    vNormal     = mat3(uModel) * norm;
    vTexCoord   = aTexCoord;
}
