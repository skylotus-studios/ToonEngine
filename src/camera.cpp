// Camera implementation: orientation from spherical coordinates (yaw/pitch),
// view matrix via glm::lookAt, perspective projection via glm::perspective.

#include "camera.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

// Convert yaw/pitch to a unit direction vector (spherical -> cartesian).
glm::vec3 CameraFront(const Camera& cam) {
    float yawRad   = glm::radians(cam.yaw);
    float pitchRad = glm::radians(cam.pitch);
    return glm::normalize(glm::vec3{
        std::cos(yawRad) * std::cos(pitchRad),
        std::sin(pitchRad),
        std::sin(yawRad) * std::cos(pitchRad)
    });
}

// World-space right vector, derived from front x world-up.
static glm::vec3 CameraRight(const Camera& cam) {
    return glm::normalize(glm::cross(CameraFront(cam), glm::vec3{0.0f, 1.0f, 0.0f}));
}

glm::mat4 CameraViewMatrix(const Camera& cam) {
    return glm::lookAt(cam.position, cam.position + CameraFront(cam),
                       glm::vec3{0.0f, 1.0f, 0.0f});
}

glm::mat4 CameraProjectionMatrix(const Camera& cam, float aspectRatio) {
    return glm::perspective(glm::radians(cam.fovY), aspectRatio,
                            cam.nearPlane, cam.farPlane);
}

void CameraProcessMouse(Camera& cam, float dx, float dy) {
    cam.yaw   += dx * cam.lookSensitivity;
    cam.pitch += dy * cam.lookSensitivity;
    // Clamp pitch to avoid flipping at the poles.
    cam.pitch  = std::clamp(cam.pitch, -89.0f, 89.0f);
}

void CameraProcessKeyboard(Camera& cam, GLFWwindow* window, float dt) {
    float velocity = cam.moveSpeed * dt;
    glm::vec3 front = CameraFront(cam);
    glm::vec3 right = CameraRight(cam);

    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)          cam.position += front * velocity;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)          cam.position -= front * velocity;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)          cam.position -= right * velocity;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)          cam.position += right * velocity;
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)      cam.position.y += velocity;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) cam.position.y -= velocity;
}
