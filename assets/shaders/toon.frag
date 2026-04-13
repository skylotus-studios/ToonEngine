// Toon / cel-shading fragment shader.
//
// Multi-light cel shading with cascaded shadow maps, shadow tint,
// and Fresnel rim lighting.

#version 410 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;

uniform sampler2D uTexture;
uniform vec4 uBaseColor;

// Toon band parameters.
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
uniform vec3  uLightPosOrDir[MAX_LIGHTS];
uniform vec3  uLightColor[MAX_LIGHTS];
uniform float uLightIntensity[MAX_LIGHTS];
uniform float uLightRadius[MAX_LIGHTS];

// Cascaded shadow mapping.
const int MAX_CASCADES = 4;
uniform bool                 uShadowEnabled;
uniform sampler2DArrayShadow uShadowMap;
uniform int                  uCascadeCount;
uniform mat4                 uLightSpaceMatrices[MAX_CASCADES];
uniform float                uCascadeSplits[MAX_CASCADES];
uniform float                uShadowBias;
uniform mat4                 uViewMatrix;

// Rim lighting.
uniform vec3  uViewPos;
uniform vec3  uRimColor;
uniform float uRimPower;
uniform float uRimStrength;

out vec4 FragColor;

float ComputeShadow() {
    if (!uShadowEnabled) return 1.0;

    // Select cascade based on view-space depth.
    vec4 viewPos = uViewMatrix * vec4(vWorldPos, 1.0);
    float depth = abs(viewPos.z);

    int layer = uCascadeCount - 1;
    for (int i = 0; i < uCascadeCount; ++i) {
        if (depth < uCascadeSplits[i]) {
            layer = i;
            break;
        }
    }

    // Project into this cascade's light space.
    vec4 lsPos = uLightSpaceMatrices[layer] * vec4(vWorldPos, 1.0);
    vec3 proj = lsPos.xyz / lsPos.w * 0.5 + 0.5;

    if (proj.z > 1.0) return 1.0;

    // Scale bias with cascade (farther cascades need more).
    float bias = uShadowBias * float(1 + layer);

    // 3x3 PCF.
    float shadow = 0.0;
    vec2 texelSize = 1.0 / vec2(textureSize(uShadowMap, 0).xy);
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            vec2 off = vec2(x, y) * texelSize;
            // sampler2DArrayShadow: vec4(s, t, layer, refDepth)
            shadow += texture(uShadowMap,
                vec4(proj.xy + off, float(layer), proj.z - bias));
        }
    }
    return shadow / 9.0;
}

void main() {
    vec3 normal = normalize(vNormal);
    vec4 albedo = texture(uTexture, vTexCoord) * uBaseColor;

    float shadowFactor = ComputeShadow();

    vec3 lit = vec3(0.0);

    for (int i = 0; i < uLightCount && i < MAX_LIGHTS; ++i) {
        vec3 lightDir;
        float atten = 1.0;

        if (uLightType[i] == 0) {
            lightDir = normalize(uLightPosOrDir[i]);
        } else {
            vec3 toLight = uLightPosOrDir[i] - vWorldPos;
            float dist = length(toLight);
            lightDir = toLight / max(dist, 0.001);
            float r = max(uLightRadius[i], 0.001);
            atten = clamp(1.0 - dist / r, 0.0, 1.0);
            atten *= atten;
        }

        float NdotL = dot(normal, lightDir);

        // Directional lights: shadow map forces shadow band.
        if (uLightType[i] == 0 && shadowFactor < 0.5)
            NdotL = -1.0;

        float band;
        if      (NdotL > uBandHigh) band = uBrightness;
        else if (NdotL > uBandLow)  band = uMid;
        else                         band = uShadow;

        vec3 tint = (NdotL > uBandHigh) ? vec3(1.0) : uShadowTint;

        lit += albedo.rgb * tint * band * uLightColor[i]
             * uLightIntensity[i] * atten;
    }

    // Fallback: no lights.
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
