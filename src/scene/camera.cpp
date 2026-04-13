// editor camera implementation.

#include "camera.h"

#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

glm::vec3 CameraFront(const Camera& cam) {
    float yr = glm::radians(cam.yaw);
    float pr = glm::radians(cam.pitch);
    return glm::normalize(glm::vec3{
        std::cos(yr) * std::cos(pr),
        std::sin(pr),
        std::sin(yr) * std::cos(pr)
        });
}

glm::vec3 CameraRight(const Camera& cam) {
    return glm::normalize(glm::cross(CameraFront(cam), glm::vec3{ 0, 1, 0 }));
}

glm::vec3 CameraUp(const Camera& cam) {
    return glm::normalize(glm::cross(CameraRight(cam), CameraFront(cam)));
}

glm::mat4 CameraViewMatrix(const Camera& cam) {
    return glm::lookAt(cam.position, cam.position + CameraFront(cam),
        glm::vec3{ 0, 1, 0 });
}

glm::mat4 CameraProjectionMatrix(const Camera& cam, float aspectRatio) {
    return glm::perspective(glm::radians(cam.fovY), aspectRatio,
        cam.nearPlane, cam.farPlane);
}

void CameraOrbit(Camera& cam, float dx, float dy) {
    cam.yaw += dx * cam.lookSensitivity;
    cam.pitch += dy * cam.lookSensitivity;
    cam.pitch = std::clamp(cam.pitch, -89.0f, 89.0f);

    // Recompute position: pivot + orbit distance along -front.
    float dist = glm::length(cam.position - cam.pivot);
    cam.position = cam.pivot - CameraFront(cam) * dist;
}

void CameraPan(Camera& cam, float dx, float dy) {
    glm::vec3 right = CameraRight(cam);
    glm::vec3 up = CameraUp(cam);

    // Scale pan speed by distance from pivot (feels natural at any zoom).
    float dist = glm::length(cam.position - cam.pivot);
    float scale = dist * cam.panSensitivity;

    glm::vec3 offset = (-dx * right - dy * up) * scale;
    cam.position += offset;
    cam.pivot += offset;
}

void CameraZoom(Camera& cam, float scroll) {
    glm::vec3 front = CameraFront(cam);
    float dist = glm::length(cam.position - cam.pivot);

    // Scale zoom by current distance (feels natural).
    float amount = scroll * dist * cam.zoomSpeed;

    // Don't zoom through the pivot.
    float newDist = dist - amount;
    if (newDist < 0.01f) newDist = 0.01f;

    cam.position = cam.pivot - front * newDist;
}

void CameraFocus(Camera& cam, const glm::vec3& target, float distance) {
    cam.pivot = target;
    if (distance <= 0.0f)
        distance = glm::length(cam.position - target);
    if (distance < 0.1f) distance = 2.0f;
    cam.position = cam.pivot - CameraFront(cam) * distance;
}

void CameraFly(Camera& cam, GLFWwindow* window, float dt) {
    float velocity = cam.moveSpeed * dt;
    glm::vec3 front = CameraFront(cam);
    glm::vec3 right = CameraRight(cam);

    glm::vec3 move{ 0.0f };
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS)          move += front;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS)          move -= front;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS)          move -= right;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS)          move += right;
    if (glfwGetKey(window, GLFW_KEY_SPACE) == GLFW_PRESS)      move.y += 1.0f;
    if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) move.y -= 1.0f;

    if (glm::length(move) > 0.001f) {
        move = glm::normalize(move) * velocity;
        cam.position += move;
        cam.pivot += move;  // keep orbit distance stable
    }
}
