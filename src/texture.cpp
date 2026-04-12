#include "texture.h"

#include <cstdio>

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

bool LoadTexture(Texture& tex, const char* path, bool flipY) {
    stbi_set_flip_vertically_on_load(flipY ? 1 : 0);

    int channels = 0;
    unsigned char* data = stbi_load(path, &tex.width, &tex.height, &channels, 4);
    if (!data) {
        std::fprintf(stderr, "Failed to load texture: %s\n", path);
        return false;
    }

    glGenTextures(1, &tex.id);
    glBindTexture(GL_TEXTURE_2D, tex.id);

    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 tex.width, tex.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);
    glGenerateMipmap(GL_TEXTURE_2D);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    stbi_image_free(data);
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
