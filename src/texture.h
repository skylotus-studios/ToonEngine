// 2D texture loading via stb_image and OpenGL texture management.
//
// CreateWhiteTexture() provides a 1x1 fallback so shaders can always
// sample a texture — white * vertexColor = pure vertex color, which
// means untextured objects "just work" without a separate shader.

#pragma once

#include <glad/glad.h>

struct Texture {
    GLuint id = 0;
    int    width  = 0;
    int    height = 0;
};

// 1x1 white pixel texture used as a default when no image is loaded.
Texture CreateWhiteTexture();

// Load an image from raw bytes in memory (PNG, JPG, etc.) into a GL texture.
// Used for embedded textures in glTF/glb files.
bool LoadTextureFromMemory(Texture& tex, const unsigned char* data, int dataSize,
                           bool flipY = true);

// Load an image file (PNG, JPG, BMP, etc.) into a GL texture with mipmaps.
// flipY: true flips the image vertically (OpenGL expects bottom-left origin,
// most image formats store top-left first).
bool LoadTexture(Texture& tex, const char* path, bool flipY = true);

// Bind the texture to a given texture unit (0, 1, 2, ...).
void BindTexture(const Texture& tex, GLuint unit = 0);

void DestroyTexture(Texture& tex);
