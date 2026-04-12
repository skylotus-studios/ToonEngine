// Toon / cel-shading fragment shader.
//
// Features:
//   - Three-band quantized diffuse (bright / mid / shadow)
//   - Shadow color ramp: shadow bands are tinted by uShadowTint
//   - Rim lighting: Fresnel-based edge highlight facing the camera
//
// All parameters are driven by uniforms for live tweaking via the overlay.

#version 410 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;

uniform sampler2D uTexture;
uniform vec4 uBaseColor;

// Toon parameters.
uniform vec3  uLightDir;
uniform float uBandHigh;
uniform float uBandLow;
uniform float uBrightness;
uniform float uMid;
uniform float uShadow;

// Shadow color ramp — multiplied into shadow/mid bands.
// (1,1,1) = no tinting, (0.6, 0.6, 1.0) = cool blue shadows.
uniform vec3 uShadowTint;

// Rim lighting.
uniform vec3  uViewPos;
uniform vec3  uRimColor;
uniform float uRimPower;
uniform float uRimStrength;

out vec4 FragColor;

void main() {
    vec3 lightDir = normalize(uLightDir);
    vec3 normal   = normalize(vNormal);
    float NdotL   = dot(normal, lightDir);

    vec4 albedo = texture(uTexture, vTexCoord) * uBaseColor;

    // Three-band cel shading with shadow tint.
    vec3 lit;
    if      (NdotL > uBandHigh) lit = albedo.rgb * uBrightness;
    else if (NdotL > uBandLow)  lit = albedo.rgb * uShadowTint * uMid;
    else                         lit = albedo.rgb * uShadowTint * uShadow;

    // Rim lighting — highlights silhouette edges facing the camera.
    vec3 viewDir = normalize(uViewPos - vWorldPos);
    float rim = pow(1.0 - max(dot(normal, viewDir), 0.0), uRimPower);
    lit += uRimColor * rim * uRimStrength;

    FragColor = vec4(lit, albedo.a);
}
