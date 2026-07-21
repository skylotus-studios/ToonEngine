// Per-mesh material: base color multiplier and optional texture.

#pragma once

#include "core/renderer.h"

#include <glm/glm.hpp>

struct Material {
    glm::vec4     baseColor{1.0f, 1.0f, 1.0f, 1.0f};
    TextureHandle texture{};    // 0 = no texture, use default white
    TextureHandle normalMap{};  // 0 = no normal map, use vertex normal
};
