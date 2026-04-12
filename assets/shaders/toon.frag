// Toon / cel-shading fragment shader.
// Quantizes the diffuse lighting into discrete bands for a cartoon look.
// Base color uniform allows per-material tinting.

#version 410 core

in vec3 vNormal;
in vec2 vTexCoord;

uniform sampler2D uTexture;
uniform vec4 uBaseColor;

out vec4 FragColor;

void main() {
    vec3 lightDir = normalize(vec3(0.3, 1.0, 0.5));
    vec3 normal   = normalize(vNormal);
    float NdotL   = dot(normal, lightDir);

    // Three-band cel shading: bright, mid, shadow.
    float light;
    if      (NdotL > 0.5)  light = 1.0;
    else if (NdotL > 0.0)  light = 0.55;
    else                    light = 0.15;

    vec4 color = texture(uTexture, vTexCoord) * uBaseColor;
    FragColor  = vec4(color.rgb * light, color.a);
}
