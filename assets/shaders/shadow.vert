// Shadow map depth-only vertex shader.
// Transforms positions by the light-space MVP. Supports skeletal animation.

#version 410 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;       // unused
layout(location = 2) in vec2 aTexCoord;      // unused
layout(location = 3) in vec4 aBoneIds;
layout(location = 4) in vec4 aBoneWeights;

uniform mat4 uLightMVP;
uniform bool uSkinned;
uniform mat4 uJoints[128];

void main() {
    vec3 pos;
    if (uSkinned) {
        mat4 skin = aBoneWeights.x * uJoints[int(aBoneIds.x)]
                  + aBoneWeights.y * uJoints[int(aBoneIds.y)]
                  + aBoneWeights.z * uJoints[int(aBoneIds.z)]
                  + aBoneWeights.w * uJoints[int(aBoneIds.w)];
        pos = vec3(skin * vec4(aPos, 1.0));
    } else {
        pos = aPos;
    }
    gl_Position = uLightMVP * vec4(pos, 1.0);
}
