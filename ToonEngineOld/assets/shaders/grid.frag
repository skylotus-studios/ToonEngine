// Sky gradient + infinite grid fragment shader.
//
// Rendered as a fullscreen pass before the scene. Each pixel:
//   1. Compute camera ray from inverse view-projection
//   2. Intersect ray with Y=0 plane → draw grid lines + write correct depth
//      only where a line is visible (keeps the plane transparent between
//      lines so scene geometry isn't clipped and Sobel sees no bogus
//      depth gradient across the whole plane)
//   3. Otherwise → draw sky gradient + write far depth

#version 410 core

in vec2 vTexCoord;

uniform mat4  uInvViewProj;
uniform vec3  uCameraPos;
uniform vec3  uSkyTop;
uniform vec3  uSkyBottom;
uniform vec3  uGridColor;
uniform float uGridScale;
uniform float uGridFade;
uniform float uNear;
uniform float uFar;

out vec4 FragColor;

void main() {
    // NDC: vTexCoord is [0,1] → map to [-1,1].
    vec2 ndc = vTexCoord * 2.0 - 1.0;

    // Reconstruct world-space ray.
    vec4 nearPt = uInvViewProj * vec4(ndc, -1.0, 1.0);
    vec4 farPt  = uInvViewProj * vec4(ndc,  1.0, 1.0);
    nearPt /= nearPt.w;
    farPt  /= farPt.w;
    vec3 rayDir = normalize(farPt.xyz - nearPt.xyz);

    // Sky gradient based on ray Y direction.
    float t = clamp(rayDir.y * 0.5 + 0.5, 0.0, 1.0);
    vec3 sky = mix(uSkyBottom, uSkyTop, t);

    // Default: sky at max depth.
    FragColor = vec4(sky, 1.0);
    gl_FragDepth = 1.0;

    // Intersect ray with Y=0 plane.
    if (abs(rayDir.y) > 0.0001 && uCameraPos.y * rayDir.y < 0.0) {
        float hitT = -uCameraPos.y / rayDir.y;
        vec3 hitPos = uCameraPos + rayDir * hitT;

        // Grid lines via abs(fract) — two scales for major/minor lines.
        vec2 coord = hitPos.xz * uGridScale;
        vec2 grid  = abs(fract(coord - 0.5) - 0.5) / fwidth(coord);
        float line = min(grid.x, grid.y);

        // Thicker lines at major axes (every 5th line).
        vec2 coord5 = hitPos.xz * uGridScale * 0.2;
        vec2 grid5  = abs(fract(coord5 - 0.5) - 0.5) / fwidth(coord5);
        float major = min(grid5.x, grid5.y);

        float gridMask = 1.0 - min(line, 1.0);
        float majorMask = 1.0 - min(major, 1.0);

        // Fade with distance from camera.
        float dist = length(hitPos.xz - uCameraPos.xz);
        float fade = 1.0 - smoothstep(uGridFade * 0.3, uGridFade, dist);

        // Blend: minor lines at 30% opacity, major lines at 60%.
        float alpha = (majorMask * 0.6 + gridMask * 0.3) * fade;

        // Only cover the pixel where a line is actually visible. Between
        // lines the plane stays transparent (sky shows through and scene
        // geometry below Y=0 is not occluded by an invisible depth layer).
        if (alpha > 0.001) {
            FragColor = vec4(mix(sky, uGridColor, alpha), 1.0);
            float depthNDC = (uFar + uNear - 2.0 * uNear * uFar / hitT)
                           / (uFar - uNear);
            gl_FragDepth = depthNDC * 0.5 + 0.5;
        }
    }
}
