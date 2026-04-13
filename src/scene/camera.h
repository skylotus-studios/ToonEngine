// editor camera: orbit, pan, zoom.
//
// Controls:
//   Right-click + drag  — orbit (rotate around pivot)
//   Middle-click + drag — pan (strafe in screen plane)
//   Scroll wheel        — zoom (dolly toward/away from pivot)
//   Right-click + WASD  — fly-through (FPS style)
//   F                   — focus on a world-space point

#pragma once

#include <glm/glm.hpp>

struct GLFWwindow;

struct Camera {
    glm::vec3 position{ 0.0f, 0.0f, 3.0f };
    glm::vec3 pivot{ 0.0f };           // orbit center
    float yaw = -90.0f;            // degrees
    float pitch = 0.0f;             // degrees, clamped +/-89
    float fovY = 45.0f;
    float nearPlane = 0.1f;
    float farPlane = 10000.0f;
    float moveSpeed = 2.0f;
    float lookSensitivity = 0.15f;    // degrees per pixel
    float panSensitivity = 0.001f;   // world units per pixel
    float zoomSpeed = 0.15f;          // world units per scroll tick
};

// Input state — tracks all mouse/scroll state for one frame.
// Lives alongside the camera; updated by GLFW callbacks.
struct InputState {
    bool   rightHeld = false;
    bool   middleHeld = false;
    bool   firstRight = false;   // suppress first-frame delta
    bool   firstMiddle = false;
    double lastX = 0.0;
    double lastY = 0.0;
    float  scrollY = 0.0f;      // accumulated scroll delta this frame
};

glm::vec3 CameraFront(const Camera& cam);
glm::vec3 CameraRight(const Camera& cam);
glm::vec3 CameraUp(const Camera& cam);

glm::mat4 CameraViewMatrix(const Camera& cam);
glm::mat4 CameraProjectionMatrix(const Camera& cam, float aspectRatio);

// Orbit: rotate yaw/pitch around the pivot point.
void CameraOrbit(Camera& cam, float dx, float dy);

// Pan: strafe camera + pivot together in the screen plane.
void CameraPan(Camera& cam, float dx, float dy);

// Zoom: dolly along the view direction (scroll).
void CameraZoom(Camera& cam, float scroll);

// Focus: move the pivot to a world point and position the camera to frame it.
void CameraFocus(Camera& cam, const glm::vec3& target, float distance = 0.0f);

// Fly-through: WASD + Space/Shift while right-click is held.
void CameraFly(Camera& cam, GLFWwindow* window, float dt);
