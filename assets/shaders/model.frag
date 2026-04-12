// Fragment shader for loaded 3D models.
// Basic Lambertian diffuse lighting with a hardcoded directional light.
// Texture is sampled and modulated by the light intensity.

#version 410 core

in vec3 vNormal;
in vec2 vTexCoord;

uniform sampler2D uTexture;

out vec4 FragColor;

void main() {
    vec3 lightDir = normalize(vec3(0.3, 1.0, 0.5));
    vec3 normal   = normalize(vNormal);

    float ambient = 0.15;
    float diffuse = max(dot(normal, lightDir), 0.0);
    float light   = ambient + diffuse;

    vec4 texColor = texture(uTexture, vTexCoord);
    FragColor = vec4(texColor.rgb * light, texColor.a);
}
