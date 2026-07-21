// Outline fragment shader — outputs a solid color for the silhouette.

#version 410 core

uniform vec4 uOutlineColor;

out vec4 FragColor;

void main() {
    FragColor = uOutlineColor;
}
