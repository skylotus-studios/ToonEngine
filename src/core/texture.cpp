// Texture loading: stb_image decodes the file, then we upload to GL
// with mipmaps and standard filtering. The STB_IMAGE_IMPLEMENTATION
// define must appear in exactly one .cpp — this is that file.

#include "texture.h"

#include <cmath>
#include <cstdio>
#include <vector>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

Texture CreateWhiteTexture() {
    Texture tex{};
    tex.width  = 1;
    tex.height = 1;

    unsigned char pixel[] = {255, 255, 255, 255};

    glGenTextures(1, &tex.id);
    glBindTexture(GL_TEXTURE_2D, tex.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixel);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return tex;
}

Texture CreateCheckerTexture(int size, int cells) {
    Texture tex{};
    tex.width  = size;
    tex.height = size;

    std::vector<unsigned char> pixels(size * size * 4);
    int cellSize = size / cells;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            bool white = ((x / cellSize) + (y / cellSize)) % 2 == 0;
            unsigned char c = white ? 220 : 50;
            int i = (y * size + x) * 4;
            pixels[i] = c; pixels[i+1] = c; pixels[i+2] = c; pixels[i+3] = 255;
        }
    }

    glGenTextures(1, &tex.id);
    glBindTexture(GL_TEXTURE_2D, tex.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return tex;
}

Texture CreateTestNormalMap(int size) {
    Texture tex{};
    tex.width  = size;
    tex.height = size;

    std::vector<unsigned char> pixels(size * size * 4);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            // Wavy bumps via sine pattern.
            float u = static_cast<float>(x) / size;
            float v = static_cast<float>(y) / size;
            float freq = 6.2832f * 4.0f;  // 4 waves
            float nx = std::cos(u * freq) * 0.4f;
            float ny = std::cos(v * freq) * 0.4f;
            float nz = 1.0f;
            // Normalize.
            float len = std::sqrt(nx*nx + ny*ny + nz*nz);
            nx /= len; ny /= len; nz /= len;
            // Encode to [0,255]: normal * 0.5 + 0.5 → [0,1] → [0,255]
            int i = (y * size + x) * 4;
            pixels[i]   = static_cast<unsigned char>((nx * 0.5f + 0.5f) * 255.0f);
            pixels[i+1] = static_cast<unsigned char>((ny * 0.5f + 0.5f) * 255.0f);
            pixels[i+2] = static_cast<unsigned char>((nz * 0.5f + 0.5f) * 255.0f);
            pixels[i+3] = 255;
        }
    }

    glGenTextures(1, &tex.id);
    glBindTexture(GL_TEXTURE_2D, tex.id);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    return tex;
}

// Upload decoded RGBA pixels to a new GL texture with mipmaps.
static void UploadTexture(Texture& tex, unsigned char* pixels) {
    glGenTextures(1, &tex.id);
    glBindTexture(GL_TEXTURE_2D, tex.id);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 tex.width, tex.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

bool LoadTextureFromMemory(Texture& tex, const unsigned char* data, int dataSize,
                           bool flipY) {
    stbi_set_flip_vertically_on_load(flipY ? 1 : 0);

    int channels = 0;
    unsigned char* pixels = stbi_load_from_memory(data, dataSize,
                                                  &tex.width, &tex.height, &channels, 4);
    if (!pixels) {
        std::fprintf(stderr, "Failed to decode texture from memory\n");
        return false;
    }

    UploadTexture(tex, pixels);
    stbi_image_free(pixels);
    return true;
}

bool LoadTexture(Texture& tex, const char* path, bool flipY) {
    stbi_set_flip_vertically_on_load(flipY ? 1 : 0);

    // Force 4 channels (RGBA) regardless of source format.
    int channels = 0;
    unsigned char* pixels = stbi_load(path, &tex.width, &tex.height, &channels, 4);
    if (!pixels) {
        std::fprintf(stderr, "Failed to load texture: %s\n", path);
        return false;
    }

    UploadTexture(tex, pixels);
    stbi_image_free(pixels);
    return true;
}

void BindTexture(const Texture& tex, GLuint unit) {
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex.id);
}

void DestroyTexture(Texture& tex) {
    if (tex.id) {
        glDeleteTextures(1, &tex.id);
        tex = {};
    }
}
