// Per-object transform: position, euler rotation (degrees), and scale.
// TransformMatrix() builds the model matrix using TRS order
// (translate -> rotate XYZ -> scale) for use in MVP computation.

#pragma once

#include <glm/glm.hpp>

struct Transform {
    glm::vec3 position{0.0f};
    glm::vec3 rotation{0.0f};  // euler angles in degrees (X, Y, Z)
    glm::vec3 scale{1.0f};
};

// Build a 4x4 model matrix: translate, then rotate (X then Y then Z), then scale.
glm::mat4 TransformMatrix(const Transform& t);
