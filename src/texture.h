#pragma once

#include <glad/glad.h>

struct Texture {
    GLuint id = 0;
    int    width  = 0;
    int    height = 0;
};

Texture CreateWhiteTexture();
bool    LoadTexture(Texture& tex, const char* path, bool flipY = true);
void    BindTexture(const Texture& tex, GLuint unit = 0);
void    DestroyTexture(Texture& tex);
