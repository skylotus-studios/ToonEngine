#version 410 core

in vec2 vTexCoord;

uniform sampler2D uTexture;
uniform vec4      uTintColor;
uniform vec4      uUVRect;  // xy = offset, zw = scale

out vec4 FragColor;

void main() {
    vec2 uv  = vTexCoord * uUVRect.zw + uUVRect.xy;
    vec4 tex = texture(uTexture, uv);
    vec4 col = tex * uTintColor;
    if (col.a < 0.01) discard;
    FragColor = col;
}
