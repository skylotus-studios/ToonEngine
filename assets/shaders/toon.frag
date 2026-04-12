// Toon / cel-shading fragment shader.
//
// Supports up to 8 lights (directional + point). Each light contributes
// independently to the three-band cel shading. Shadow tint and rim
// lighting are applied on top.

#version 410 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;

uniform sampler2D uTexture;
uniform vec4 uBaseColor;

// Toon band parameters (global, not per-light).
uniform float uBandHigh;
uniform float uBandLow;
uniform float uBrightness;
uniform float uMid;
uniform float uShadow;
uniform vec3  uShadowTint;

// Lights.  type: 0 = directional, 1 = point.
const int MAX_LIGHTS = 8;
uniform int   uLightCount;
uniform int   uLightType[MAX_LIGHTS];
uniform vec3  uLightPosOrDir[MAX_LIGHTS];  // direction (dir) or position (point)
uniform vec3  uLightColor[MAX_LIGHTS];
uniform float uLightIntensity[MAX_LIGHTS];
uniform float uLightRadius[MAX_LIGHTS];

// Rim lighting.
uniform vec3  uViewPos;
uniform vec3  uRimColor;
uniform float uRimPower;
uniform float uRimStrength;

out vec4 FragColor;

void main() {
    vec3 normal = normalize(vNormal);
    vec4 albedo = texture(uTexture, vTexCoord) * uBaseColor;

    vec3 lit = vec3(0.0);

    for (int i = 0; i < uLightCount && i < MAX_LIGHTS; ++i) {
        vec3 lightDir;
        float atten = 1.0;

        if (uLightType[i] == 0) {
            // Directional: uLightPosOrDir is the direction toward the light.
            lightDir = normalize(uLightPosOrDir[i]);
        } else {
            // Point: compute direction from fragment to light, with falloff.
            vec3 toLight = uLightPosOrDir[i] - vWorldPos;
            float dist = length(toLight);
            lightDir = toLight / max(dist, 0.001);
            float r = max(uLightRadius[i], 0.001);
            atten = clamp(1.0 - dist / r, 0.0, 1.0);
            atten *= atten;  // quadratic falloff
        }

        float NdotL = dot(normal, lightDir);

        // Three-band cel shading per light.
        float band;
        if      (NdotL > uBandHigh) band = uBrightness;
        else if (NdotL > uBandLow)  band = uMid;
        else                         band = uShadow;

        // Shadow tint for mid/shadow bands.
        vec3 tint = (NdotL > uBandHigh) ? vec3(1.0) : uShadowTint;

        lit += albedo.rgb * tint * band * uLightColor[i]
             * uLightIntensity[i] * atten;
    }

    // Fallback: if no lights, use a default forward light so the scene
    // isn't pitch black.
    if (uLightCount == 0) {
        float NdotL = dot(normal, normalize(vec3(0.3, 1.0, 0.5)));
        float band;
        if      (NdotL > uBandHigh) band = uBrightness;
        else if (NdotL > uBandLow)  band = uMid;
        else                         band = uShadow;
        vec3 tint = (NdotL > uBandHigh) ? vec3(1.0) : uShadowTint;
        lit = albedo.rgb * tint * band;
    }

    // Rim lighting.
    vec3 viewDir = normalize(uViewPos - vWorldPos);
    float rim = pow(1.0 - max(dot(normal, viewDir), 0.0), uRimPower);
    lit += uRimColor * rim * uRimStrength;

    FragColor = vec4(lit, albedo.a);
}
