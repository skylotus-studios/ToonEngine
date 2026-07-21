#pragma once

#include "core/input/keycodes.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <array>
#include <cstring>

// Raw per-device state. Updated by GLFW callbacks (keyboard, mouse) or
// polled each frame (gamepad). Previous/current arrays give WasPressed /
// WasReleased edge detection.

struct Keyboard {
    std::array<uint8_t, kMaxKeys> current{};
    std::array<uint8_t, kMaxKeys> previous{};

    void BeginFrame()           { previous = current; }
    void OnKey(int glfwKey, int action) {
        if (glfwKey < 0 || glfwKey >= kMaxKeys) return;
        current[glfwKey] = (action != GLFW_RELEASE) ? 1 : 0;
    }

    bool IsDown(Key k)        const { int i = GlfwFromKey(k); return i >= 0 && i < kMaxKeys && current[i]; }
    bool WasPressed(Key k)    const { int i = GlfwFromKey(k); return i >= 0 && i < kMaxKeys && current[i] && !previous[i]; }
    bool WasReleased(Key k)   const { int i = GlfwFromKey(k); return i >= 0 && i < kMaxKeys && !current[i] && previous[i]; }
};

struct Mouse {
    glm::dvec2 position{0.0};
    glm::dvec2 previousPosition{0.0};
    glm::dvec2 delta{0.0};
    glm::dvec2 scrollDelta{0.0};
    glm::dvec2 scrollAccum{0.0};
    bool       insideWindow = false;

    std::array<uint8_t, kMaxMouseButtons> current{};
    std::array<uint8_t, kMaxMouseButtons> previous{};

    void BeginFrame() {
        previous = current;
        previousPosition = position;
        delta = {0.0, 0.0};
        scrollDelta = scrollAccum;
        scrollAccum = {0.0, 0.0};
    }
    void OnMove(double x, double y) {
        position = {x, y};
        delta = position - previousPosition;
    }
    void OnButton(int button, int action) {
        if (button < 0 || button >= kMaxMouseButtons) return;
        current[button] = (action != GLFW_RELEASE) ? 1 : 0;
    }
    void OnScroll(double xoff, double yoff) {
        scrollAccum.x += xoff;
        scrollAccum.y += yoff;
    }
    void OnEnter(bool entered) { insideWindow = entered; }

    bool IsDown(MouseButton b)      const { int i = GlfwFromMouseButton(b); return i >= 0 && i < kMaxMouseButtons && current[i]; }
    bool WasPressed(MouseButton b)  const { int i = GlfwFromMouseButton(b); return i >= 0 && i < kMaxMouseButtons && current[i] && !previous[i]; }
    bool WasReleased(MouseButton b) const { int i = GlfwFromMouseButton(b); return i >= 0 && i < kMaxMouseButtons && !current[i] && previous[i]; }
};

struct Gamepad {
    bool connected = false;
    int  jid       = -1;

    std::array<uint8_t, kMaxGamepadButtons> current{};
    std::array<uint8_t, kMaxGamepadButtons> previous{};
    std::array<float,   kMaxGamepadAxes>    axes{};
    std::array<float,   kMaxGamepadAxes>    previousAxes{};

    float deadzone = 0.15f;

    void BeginFrame() {
        previous = current;
        previousAxes = axes;
    }

    void Poll() {
        if (!connected) return;
        GLFWgamepadstate state{};
        if (glfwGetGamepadState(jid, &state)) {
            for (int i = 0; i < kMaxGamepadButtons; ++i)
                current[i] = state.buttons[i];
            for (int i = 0; i < kMaxGamepadAxes; ++i)
                axes[i] = state.axes[i];
        }
    }

    bool  IsButtonDown(GamepadButton b)   const { int i = static_cast<int>(b); return i >= 0 && i < kMaxGamepadButtons && current[i]; }
    bool  WasButtonPressed(GamepadButton b) const { int i = static_cast<int>(b); return i >= 0 && i < kMaxGamepadButtons && current[i] && !previous[i]; }
    bool  WasButtonReleased(GamepadButton b) const { int i = static_cast<int>(b); return i >= 0 && i < kMaxGamepadButtons && !current[i] && previous[i]; }
    float GetAxis(GamepadAxis a) const {
        int i = static_cast<int>(a);
        if (i < 0 || i >= kMaxGamepadAxes) return 0.0f;
        float v = axes[i];
        if (v > -deadzone && v < deadzone) return 0.0f;
        return v;
    }
};
