// FPS-style fly camera with yaw/pitch orientation.
//
// Controls: hold right mouse button to enter look mode, then WASD to
// move, Space/Shift for vertical, mouse to look. Release right mouse
// to get the cursor back.

#pragma once

#include <glm/glm.hpp>

struct GLFWwindow;

struct Camera {
    glm::vec3 position{0.0f, 0.0f, 3.0f};
    float yaw   = -90.0f;  // degrees; -90 faces down -Z (into screen)
    float pitch  = 0.0f;   // degrees; clamped to +/-89 to avoid gimbal lock
    float fovY   = 45.0f;  // vertical field of view in degrees
    float nearPlane = 0.1f;
    float farPlane  = 10000.0f;
    float moveSpeed = 5.0f;       // units per second
    float lookSensitivity = 0.1f; // degrees per pixel of mouse movement
};

// Unit vector pointing where the camera is looking.
glm::vec3 CameraFront(const Camera& cam);

glm::mat4 CameraViewMatrix(const Camera& cam);
glm::mat4 CameraProjectionMatrix(const Camera& cam, float aspectRatio);

// Update orientation from mouse delta (dx, dy in pixels).
void CameraProcessMouse(Camera& cam, float dx, float dy);

// Poll GLFW key state and move the camera. Call once per frame with frame dt.
void CameraProcessKeyboard(Camera& cam, GLFWwindow* window, float dt);
