#pragma once

#include <glm/glm.hpp>

struct GLFWwindow;

struct Camera {
    glm::vec3 position{0.0f, 0.0f, 3.0f};
    float yaw   = -90.0f;
    float pitch  = 0.0f;
    float fovY   = 45.0f;
    float nearPlane = 0.1f;
    float farPlane  = 100.0f;
    float moveSpeed = 3.0f;
    float lookSensitivity = 0.1f;
};

glm::vec3 CameraFront(const Camera& cam);
glm::mat4 CameraViewMatrix(const Camera& cam);
glm::mat4 CameraProjectionMatrix(const Camera& cam, float aspectRatio);
void      CameraProcessMouse(Camera& cam, float dx, float dy);
void      CameraProcessKeyboard(Camera& cam, GLFWwindow* window, float dt);
