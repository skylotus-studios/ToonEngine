//============================================================================
//  core/input.cpp — GLFW-based input polling + capture gate.
//============================================================================
#include "core/input.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

namespace toon { namespace Input {

namespace {
GLFWwindow* g_window = nullptr;

double g_lastX = 0.0, g_lastY = 0.0;   // cursor pos last frame
float  g_dx = 0.0f, g_dy = 0.0f;       // cursor delta this frame
bool   g_haveLast = false;

double g_scrollAccum = 0.0;            // written by the scroll callback (may fire mid-frame)
float  g_scroll = 0.0f;                // latched for this frame in BeginFrame

bool   g_mouseCaptured    = false;
bool   g_keyboardCaptured = false;

// GLFW scroll callback. ImGui's GLFW backend chains this (it saved us as the prior
// callback because Input::Init runs before InitUI).
void ScrollCallback(GLFWwindow*, double /*xoffset*/, double yoffset) {
    g_scrollAccum += yoffset;
}

int GlfwButton(Mouse b) {
    switch (b) {
        case Mouse::Left:   return GLFW_MOUSE_BUTTON_LEFT;
        case Mouse::Right:  return GLFW_MOUSE_BUTTON_RIGHT;
        case Mouse::Middle: return GLFW_MOUSE_BUTTON_MIDDLE;
    }
    return GLFW_MOUSE_BUTTON_LEFT;
}

int GlfwKey(Key k) {
    switch (k) {
        case Key::W: return GLFW_KEY_W;
        case Key::A: return GLFW_KEY_A;
        case Key::S: return GLFW_KEY_S;
        case Key::D: return GLFW_KEY_D;
        case Key::Q: return GLFW_KEY_Q;
        case Key::E: return GLFW_KEY_E;
        case Key::F: return GLFW_KEY_F;
    }
    return GLFW_KEY_UNKNOWN;
}
} // namespace

void Init(GLFWwindow* window) {
    g_window = window;
    glfwSetScrollCallback(window, ScrollCallback);
    glfwGetCursorPos(window, &g_lastX, &g_lastY);
    g_haveLast = true;
}

void BeginFrame() {
    if (!g_window) return;

    double x, y;
    glfwGetCursorPos(g_window, &x, &y);
    // Always advance g_last (even when captured) so releasing capture never yields a jump.
    g_dx = g_haveLast ? static_cast<float>(x - g_lastX) : 0.0f;
    g_dy = g_haveLast ? static_cast<float>(y - g_lastY) : 0.0f;
    g_lastX = x;
    g_lastY = y;
    g_haveLast = true;

    g_scroll = static_cast<float>(g_scrollAccum);
    g_scrollAccum = 0.0;
}

void SetCaptured(bool mouseCaptured, bool keyboardCaptured) {
    g_mouseCaptured    = mouseCaptured;
    g_keyboardCaptured = keyboardCaptured;
}

bool IsMouseDown(Mouse b) {
    if (!g_window || g_mouseCaptured) return false;
    return glfwGetMouseButton(g_window, GlfwButton(b)) == GLFW_PRESS;
}

void MouseDelta(float& dx, float& dy) {
    if (g_mouseCaptured) { dx = 0.0f; dy = 0.0f; return; }
    dx = g_dx;
    dy = g_dy;
}

float ScrollDelta() {
    return g_mouseCaptured ? 0.0f : g_scroll;
}

bool IsKeyDown(Key k) {
    if (!g_window || g_keyboardCaptured) return false;
    return glfwGetKey(g_window, GlfwKey(k)) == GLFW_PRESS;
}

}} // namespace toon::Input
