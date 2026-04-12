// Toon / cel-shading fragment shader.
// Quantizes the diffuse lighting into discrete bands for a cartoon look.
// All band thresholds and intensities are driven by uniforms so the debug
// overlay can tweak them at runtime.

#version 410 core

in vec3 vNormal;
in vec2 vTexCoord;

uniform sampler2D uTexture;
uniform vec4 uBaseColor;

// Toon parameters (set from CPU each frame).
uniform vec3  uLightDir;
uniform float uBandHigh;     // NdotL threshold for bright band
uniform float uBandLow;      // NdotL threshold for mid band
uniform float uBrightness;   // intensity of bright band
uniform float uMid;          // intensity of mid band
uniform float uShadow;       // intensity of shadow band

out vec4 FragColor;

void main() {
    vec3 lightDir = normalize(uLightDir);
    vec3 normal   = normalize(vNormal);
    float NdotL   = dot(normal, lightDir);

    // Three-band cel shading.
    float light;
    if      (NdotL > uBandHigh) light = uBrightness;
    else if (NdotL > uBandLow)  light = uMid;
    else                         light = uShadow;

    vec4 color = texture(uTexture, vTexCoord) * uBaseColor;
    FragColor  = vec4(color.rgb * light, color.a);
}
