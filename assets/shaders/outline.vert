// Outline vertex shader — inverted hull method.
// Extrudes each vertex along its normal by uOutlineWidth, then projects.
// Supports optional skeletal animation via uSkinned + uJoints[].

#version 410 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;  // unused, but must match layout
layout(location = 3) in vec4 aBoneIds;
layout(location = 4) in vec4 aBoneWeights;

uniform mat4  uMVP;
uniform float uOutlineWidth;
uniform bool  uSkinned;
uniform mat4  uJoints[128];

void main() {
    vec3 pos;
    vec3 norm;

    if (uSkinned) {
        mat4 skin = aBoneWeights.x * uJoints[int(aBoneIds.x)]
                  + aBoneWeights.y * uJoints[int(aBoneIds.y)]
                  + aBoneWeights.z * uJoints[int(aBoneIds.z)]
                  + aBoneWeights.w * uJoints[int(aBoneIds.w)];
        pos  = vec3(skin * vec4(aPos, 1.0));
        norm = normalize(mat3(skin) * aNormal);
    } else {
        pos  = aPos;
        norm = normalize(aNormal);
    }

    pos += norm * uOutlineWidth;
    gl_Position = uMVP * vec4(pos, 1.0);
}
