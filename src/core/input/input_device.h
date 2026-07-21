#pragma once
//============================================================================
//  core/input/input_device.h: raw per-device state.
//
//  Keyboard/Mouse update via GLFW callbacks (input_system.cpp); Gamepad is polled once a
//  frame since glfwGetGamepadState has no callback form. Every device keeps current/previous
//  per-frame snapshots so WasPressed/WasReleased can detect edges.
//
//  Ported from ToonEngineOld/src/core/input/input_device.h: glm::dvec2 replaced by
//  toon::Vec2 (this engine has no glm dependency), and Mouse's scroll bookkeeping collapsed
//  from a two-field latch to a single live accumulator: BeginFrame() clears it (like
//  `delta`), OnScroll() accumulates into it directly, and it's read live at query time. The
//  old two-field scrollAccum/scrollDelta split only existed to survive a BeginFrame-after-
//  PollEvents order; this engine's loop runs BeginFrame BEFORE glfwPollEvents (see
//  input_system.h), so one field is both correct and simpler.
//============================================================================
#include "core/input/keycodes.h"
#include "core/math.h"

#include <GLFW/glfw3.h>

#include <array>
#include <cstdint>

namespace toon {
    namespace Input {

        struct Keyboard {
            std::array<uint8_t, kMaxKeys> current{};
            std::array<uint8_t, kMaxKeys> previous{};

            void BeginFrame() { previous = current; }
            void OnKey(int glfwKey, int action) {
                if (glfwKey < 0 || glfwKey >= kMaxKeys) { return; }
                current[glfwKey] = (action != GLFW_RELEASE) ? 1 : 0;
            }

            bool IsDown(Key k)      const { const int i = GlfwFromKey(k); return i >= 0 && i < kMaxKeys && current[i]; }
            bool WasPressed(Key k)  const { const int i = GlfwFromKey(k); return i >= 0 && i < kMaxKeys && current[i] && !previous[i]; }
            bool WasReleased(Key k) const { const int i = GlfwFromKey(k); return i >= 0 && i < kMaxKeys && !current[i] && previous[i]; }
        };

        struct Mouse {
            Vec2 position{};         // cursor position in pixels
            Vec2 previousPosition{};
            Vec2 delta{};             // pixels moved this frame (0 unless the cursor actually moved)
            Vec2 scrollDelta{};       // scroll notches this frame: live accumulator, see BeginFrame
            bool insideWindow = false;

            std::array<uint8_t, kMaxMouseButtons> current{};
            std::array<uint8_t, kMaxMouseButtons> previous{};

            // Snapshot for edge detection and reset this frame's deltas. Called BEFORE
            // glfwPollEvents so OnMove/OnScroll (fired during the poll) accumulate into a clean
            // frame and are visible to this SAME frame's queries: no one-frame lag.
            void BeginFrame() {
                previous = current;
                previousPosition = position;
                delta = {0.0f, 0.0f};
                scrollDelta = {0.0f, 0.0f};
            }
            void OnMove(double x, double y) {
                position = {static_cast<float>(x), static_cast<float>(y)};
                delta.x = position.x - previousPosition.x;
                delta.y = position.y - previousPosition.y;
            }
            void OnButton(int button, int action) {
                if (button < 0 || button >= kMaxMouseButtons) { return; }
                current[button] = (action != GLFW_RELEASE) ? 1 : 0;
            }
            void OnScroll(double xoffset, double yoffset) {
                scrollDelta.x += static_cast<float>(xoffset);
                scrollDelta.y += static_cast<float>(yoffset);
            }
            void OnEnter(bool entered) { insideWindow = entered; }

            bool IsDown(MouseButton b)      const { const int i = GlfwFromMouseButton(b); return i >= 0 && i < kMaxMouseButtons && current[i]; }
            bool WasPressed(MouseButton b)  const { const int i = GlfwFromMouseButton(b); return i >= 0 && i < kMaxMouseButtons && current[i] && !previous[i]; }
            bool WasReleased(MouseButton b) const { const int i = GlfwFromMouseButton(b); return i >= 0 && i < kMaxMouseButtons && !current[i] && previous[i]; }
        };

        struct Gamepad {
            bool connected = false;
            int  jid       = -1;

            std::array<uint8_t, kMaxGamepadButtons> current{};
            std::array<uint8_t, kMaxGamepadButtons> previous{};
            std::array<float,   kMaxGamepadAxes>    axes{};
            std::array<float,   kMaxGamepadAxes>    previousAxes{};

            // Applied by GetAxis. This is the ONLY deadzone the analog axis path gets. A bound
            // action's own per-binding deadzone (action_map.h's GamepadAxisBinding::deadzone) only
            // gates the digital is-this-axis-held-like-a-button query, not GetAxis, so don't drop
            // this thinking it's redundant with that one.
            float deadzone = 0.15f;

            void BeginFrame() {
                previous = current;
                previousAxes = axes;
            }

            // Poll the OS device directly. glfwGetGamepadState has no callback/event form, so this
            // must run once a frame (input_system.cpp's BeginFrame) rather than react to a callback.
            void Poll() {
                if (!connected) { return; }
                GLFWgamepadstate state{};
                if (glfwGetGamepadState(jid, &state)) {
                    for (int i = 0; i < kMaxGamepadButtons; ++i) { current[i] = state.buttons[i]; }
                    for (int i = 0; i < kMaxGamepadAxes; ++i) { axes[i] = state.axes[i]; }
                }
            }

            bool IsButtonDown(GamepadButton b)      const { const int i = static_cast<int>(b); return i >= 0 && i < kMaxGamepadButtons && current[i]; }
            bool WasButtonPressed(GamepadButton b)  const { const int i = static_cast<int>(b); return i >= 0 && i < kMaxGamepadButtons && current[i] && !previous[i]; }
            bool WasButtonReleased(GamepadButton b) const { const int i = static_cast<int>(b); return i >= 0 && i < kMaxGamepadButtons && !current[i] && previous[i]; }

            float GetAxis(GamepadAxis a) const {
                const int i = static_cast<int>(a);
                if (i < 0 || i >= kMaxGamepadAxes) { return 0.0f; }
                const float v = axes[i];
                return (v > -deadzone && v < deadzone) ? 0.0f : v;
            }
        };

    } // namespace Input
} // namespace toon
