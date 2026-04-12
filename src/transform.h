#pragma once

#include <glm/glm.hpp>

struct Transform {
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f};  // euler angles in degrees (X, Y, Z)
    glm::vec3 scale{1.0f};
};

glm::mat4 TransformMatrix(const Transform& t);
