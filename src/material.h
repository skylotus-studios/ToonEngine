// Per-mesh material: base color multiplier and optional texture.
// When no texture is loaded (texture.id == 0), bind the default white
// texture so the shader always has something to sample.

#pragma once

#include "texture.h"

#include <glm/glm.hpp>

struct Material {
    glm::vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};
    Texture   texture{};  // 0 = no texture loaded, use default white
};
