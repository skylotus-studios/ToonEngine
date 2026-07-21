//============================================================================
//  core/input/input_system.cpp: GLFW callback wiring + polling implementation.
//============================================================================
#include "core/input/input_system.h"

#include <GLFW/glfw3.h>

namespace toon {
    namespace Input {

        namespace {

            GLFWwindow* gWindow = nullptr;

            Keyboard gKeyboard;
            Mouse    gMouse;
            Gamepad  gGamepads[kMaxGamepads];

            bool gMouseCaptured    = false;
            bool gKeyboardCaptured = false;

            // --- GLFW callbacks -----------------------------------------------------------

            void KeyCallback(GLFWwindow*, int key, int /*scancode*/, int action, int /*mods*/) {
                gKeyboard.OnKey(key, action);
            }

            void MouseButtonCallback(GLFWwindow*, int button, int action, int /*mods*/) {
                gMouse.OnButton(button, action);
            }

            void CursorPosCallback(GLFWwindow*, double x, double y) {
                gMouse.OnMove(x, y);
            }

            void ScrollCallback(GLFWwindow*, double xoffset, double yoffset) {
                gMouse.OnScroll(xoffset, yoffset);
            }

            void CursorEnterCallback(GLFWwindow*, int entered) {
                gMouse.OnEnter(entered != 0);
            }

            // Gamepad hot-plug: keep the connected flag accurate so GamepadCount()/GetGamepad() reflect
            // reality. (The reference also pushed a connect/disconnect event here; this engine has no
            // event queue, see input_system.h's banner.)
            void JoystickCallback(int jid, int event) {
                if (jid < 0 || jid >= kMaxGamepads) { return; }
                if (event == GLFW_CONNECTED && glfwJoystickIsGamepad(jid)) {
                    gGamepads[jid].connected = true;
                    gGamepads[jid].jid       = jid;
                } else if (event == GLFW_DISCONNECTED) {
                    gGamepads[jid].connected = false;
                }
            }

        } // namespace

        // --- Lifecycle ------------------------------------------------------------------

        void Init(GLFWwindow* window) {
            gWindow = window;

            glfwSetKeyCallback(window, KeyCallback);
            glfwSetMouseButtonCallback(window, MouseButtonCallback);
            glfwSetCursorPosCallback(window, CursorPosCallback);
            glfwSetScrollCallback(window, ScrollCallback);
            glfwSetCursorEnterCallback(window, CursorEnterCallback);
            glfwSetJoystickCallback(JoystickCallback);

            // Detect gamepads already connected at startup (the joystick callback above only fires
            // on a hot-plug event, not for devices already present when it's installed).
            for (int jid = 0; jid < kMaxGamepads; ++jid) {
                if (glfwJoystickPresent(jid) && glfwJoystickIsGamepad(jid)) {
                    gGamepads[jid].connected = true;
                    gGamepads[jid].jid       = jid;
                }
            }

            // Seed the mouse position so the first frame's delta isn't a jump from (0,0).
            double mx, my;
            glfwGetCursorPos(window, &mx, &my);
            gMouse.OnMove(mx, my);
            gMouse.previousPosition = gMouse.position;
        }

        void BeginFrame() {
            gKeyboard.BeginFrame();
            gMouse.BeginFrame();
            for (Gamepad& gp : gGamepads) {
                gp.BeginFrame();
                gp.Poll();   // glfwGetGamepadState reads the OS device live: safe to call before PollEvents.
            }
        }

        void Shutdown() { gWindow = nullptr; }

        // --- Capture gate -----------------------------------------------------------------

        void SetCaptured(bool mouseCaptured, bool keyboardCaptured) {
            gMouseCaptured    = mouseCaptured;
            gKeyboardCaptured = keyboardCaptured;
        }

        bool IsMouseCaptured()    { return gMouseCaptured; }
        bool IsKeyboardCaptured() { return gKeyboardCaptured; }

        // --- Keyboard ---------------------------------------------------------------------

        bool IsKeyDown(Key k)      { return !gKeyboardCaptured && gKeyboard.IsDown(k); }
        bool WasKeyPressed(Key k)  { return !gKeyboardCaptured && gKeyboard.WasPressed(k); }
        bool WasKeyReleased(Key k) { return !gKeyboardCaptured && gKeyboard.WasReleased(k); }

        // --- Mouse ------------------------------------------------------------------------

        bool IsMouseDown(MouseButton b)      { return !gMouseCaptured && gMouse.IsDown(b); }
        bool WasMousePressed(MouseButton b)  { return !gMouseCaptured && gMouse.WasPressed(b); }
        bool WasMouseReleased(MouseButton b) { return !gMouseCaptured && gMouse.WasReleased(b); }

        void MouseDelta(float& dx, float& dy) {
            if (gMouseCaptured) { dx = 0.0f; dy = 0.0f; return; }
            dx = gMouse.delta.x;
            dy = gMouse.delta.y;
        }

        float ScrollDelta() { return gMouseCaptured ? 0.0f : gMouse.scrollDelta.y; }

        // --- Gamepad ----------------------------------------------------------------------

        int GamepadCount() {
            int n = 0;
            for (const Gamepad& gp : gGamepads) { if (gp.connected) { ++n; } }
            return n;
        }

        const Gamepad& GetGamepad(int index) {
            static const Gamepad sEmpty{};
            return (index >= 0 && index < kMaxGamepads) ? gGamepads[index] : sEmpty;
        }

        // --- Raw access -------------------------------------------------------------------

        const Keyboard& RawKeyboard() { return gKeyboard; }
        const Mouse&    RawMouse()    { return gMouse; }

    } // namespace Input
} // namespace toon
