// Sobel edge detection post-process fragment shader.
//
// Samples the depth buffer in a 3x3 neighborhood, applies Sobel operators
// in X and Y, and draws edges where the gradient exceeds uEdgeThreshold.
// Depth is linearized before differentiation so edges are consistent
// across the depth range.

#version 410 core

in vec2 vTexCoord;

uniform sampler2D uSceneColor;
uniform sampler2D uSceneDepth;

uniform vec3  uEdgeColor;
uniform float uEdgeThreshold;
uniform float uEdgeWidth;
uniform vec2  uTexelSize;   // 1.0 / framebuffer resolution
uniform float uNear;
uniform float uFar;

out vec4 FragColor;

float linearizeDepth(float d) {
    return uNear * uFar / (uFar - d * (uFar - uNear));
}

void main() {
    vec2 step = uTexelSize * uEdgeWidth;

    // Sample 3x3 depth neighborhood (linearized).
    //  0 1 2
    //  3 4 5
    //  6 7 8
    float d0 = linearizeDepth(texture(uSceneDepth, vTexCoord + vec2(-step.x,  step.y)).r);
    float d1 = linearizeDepth(texture(uSceneDepth, vTexCoord + vec2(    0.0,  step.y)).r);
    float d2 = linearizeDepth(texture(uSceneDepth, vTexCoord + vec2( step.x,  step.y)).r);
    float d3 = linearizeDepth(texture(uSceneDepth, vTexCoord + vec2(-step.x,     0.0)).r);
    // d4 (center) not needed for Sobel
    float d5 = linearizeDepth(texture(uSceneDepth, vTexCoord + vec2( step.x,     0.0)).r);
    float d6 = linearizeDepth(texture(uSceneDepth, vTexCoord + vec2(-step.x, -step.y)).r);
    float d7 = linearizeDepth(texture(uSceneDepth, vTexCoord + vec2(    0.0, -step.y)).r);
    float d8 = linearizeDepth(texture(uSceneDepth, vTexCoord + vec2( step.x, -step.y)).r);

    // Sobel kernels.
    float gx = d2 + 2.0 * d5 + d8 - d0 - 2.0 * d3 - d6;
    float gy = d6 + 2.0 * d7 + d8 - d0 - 2.0 * d1 - d2;
    float edge = sqrt(gx * gx + gy * gy);

    vec3 scene = texture(uSceneColor, vTexCoord).rgb;
    float mask = smoothstep(uEdgeThreshold * 0.5, uEdgeThreshold, edge);
    FragColor = vec4(mix(scene, uEdgeColor, mask), 1.0);
}
