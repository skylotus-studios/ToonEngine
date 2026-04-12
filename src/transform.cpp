#include "transform.h"

#include <glm/gtc/matrix_transform.hpp>

glm::mat4 TransformMatrix(const Transform& t) {
    glm::mat4 m = glm::translate(glm::mat4(1.0f), t.position);
    m = glm::rotate(m, glm::radians(t.rotation.x), {1.0f, 0.0f, 0.0f});
    m = glm::rotate(m, glm::radians(t.rotation.y), {0.0f, 1.0f, 0.0f});
    m = glm::rotate(m, glm::radians(t.rotation.z), {0.0f, 0.0f, 1.0f});
    m = glm::scale(m, t.scale);
    return m;
}
