// Fragment shader: samples the texture and multiplies by vertex color.
// When bound to the default 1x1 white texture, this produces pure
// vertex-colored output (white * color = color).

#version 410 core

in vec3 vColor;
in vec2 vTexCoord;

uniform sampler2D uTexture;

out vec4 FragColor;

void main() {
    FragColor = texture(uTexture, vTexCoord) * vec4(vColor, 1.0);
}
