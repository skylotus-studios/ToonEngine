// Outline vertex shader — inverted hull method.
//
// Two modes controlled by uScreenSpace:
//   false: object-space extrusion (width in model units, thins with distance)
//   true:  clip-space extrusion (constant pixel width regardless of distance)
//
// Both use smooth normals to avoid faceted gaps at hard edges.

#version 410 core

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aBoneIds;
layout(location = 4) in vec4 aBoneWeights;
layout(location = 5) in vec3 aSmoothNormal;

uniform mat4  uMVP;
uniform float uOutlineWidth;
uniform bool  uSkinned;
uniform bool  uScreenSpace;
uniform vec2  uViewportSize;
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
        norm = normalize(mat3(skin) * aSmoothNormal);
    } else {
        pos  = aPos;
        norm = normalize(aSmoothNormal);
    }

    if (uScreenSpace) {
        // Clip-space extrusion: constant screen-pixel thickness.
        vec4 clipPos  = uMVP * vec4(pos, 1.0);
        vec3 clipNorm = normalize(mat3(uMVP) * norm);

        // Offset in NDC, scaled by clipPos.w to cancel perspective divide.
        vec2 dir = normalize(clipNorm.xy);
        float aspect = uViewportSize.x / uViewportSize.y;
        dir.x /= aspect;
        clipPos.xy += dir * uOutlineWidth * 0.01 * clipPos.w;

        gl_Position = clipPos;
    } else {
        // Object-space extrusion: width in model units.
        pos += norm * uOutlineWidth;
        gl_Position = uMVP * vec4(pos, 1.0);
    }
}
