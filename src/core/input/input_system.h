#pragma once
//============================================================================
//  core/input/input_system.h: GLFW callback wiring + the polling API.
//
//  GLFW-based, backend-agnostic beyond that (no Diligent). Replaces the old single-file
//  core/input.{h,cpp} (mouse/keyboard level-polling only, no edge detection, no gamepad).
//
//  Ported from ToonEngineOld/src/core/input/input_system.h, trimmed to what this roadmap
//  item needs: no event queue, char callback, or file drops. input_event.h's stream API is
//  std::span-based (C++20; this project targets C++17) and has no consumer here yet. Its
//  first consumer is the asset-browser roadmap item (text input + drag-drop), which can add
//  it back then. Gamepad hot-plug bookkeeping (the joystick callback's connected flag) is
//  kept; only the old event-queue push is dropped.
//============================================================================
#include "core/input/input_device.h"
#include "core/input/keycodes.h"

struct GLFWwindow;

namespace toon {
    namespace Input {

        // Install the key/mouse-button/cursor-pos/scroll/cursor-enter/joystick callbacks. Call
        // BEFORE Renderer::InitUI so ImGui's GLFW backend (install_callbacks = true) chains them
        // instead of overwriting them. Also scans for gamepads already connected at startup (the
        // joystick callback only fires on hot-plug) and seeds the mouse position.
        void Init(GLFWwindow* window);

        // Snapshot previous state, reset this frame's mouse/scroll deltas, and poll gamepads. Call
        // BEFORE glfwPollEvents() each frame: WasPressed/WasReleased and the mouse/scroll deltas all
        // depend on this running before the callbacks that mutate "current" state fire, so the poll
        // fills in a freshly-cleared frame instead of one still holding last frame's values.
        void BeginFrame();

        void Shutdown();

        // Feed ImGui's capture flags each frame (main.cpp reads io.WantCaptureMouse/Keyboard). The
        // gated queries below return neutral while captured. RawKeyboard/RawMouse/GetGamepad (used
        // by the action-map layer, action_map.cpp) bypass the gate entirely, same as the reference.
        // Callers that read a keyboard/mouse-sourced action must apply their own capture guard where
        // that matters (see main.cpp's camera-fly/focus block).
        void SetCaptured(bool mouseCaptured, bool keyboardCaptured);
        bool IsMouseCaptured();
        bool IsKeyboardCaptured();

        // Keyboard polling (respects the capture gate).
        bool IsKeyDown(Key k);
        bool WasKeyPressed(Key k);
        bool WasKeyReleased(Key k);

        // Mouse polling (respects the capture gate).
        bool  IsMouseDown(MouseButton b);
        bool  WasMousePressed(MouseButton b);
        bool  WasMouseReleased(MouseButton b);
        void  MouseDelta(float& dx, float& dy);   // pixels since last BeginFrame (0 if captured)
        float ScrollDelta();                      // notches since last BeginFrame (0 if captured)

        // Gamepad polling (no capture gate: a physical controller is never ambiguous with UI focus).
        int            GamepadCount();
        const Gamepad& GetGamepad(int index);

        // Raw device access: bypasses the capture gate (see the SetCaptured comment above).
        const Keyboard& RawKeyboard();
        const Mouse&    RawMouse();

    } // namespace Input
} // namespace toon
