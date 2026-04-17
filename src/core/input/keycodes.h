#pragma once

#include <GLFW/glfw3.h>

#include <cstdint>

// Unified key/button/axis enums that isolate GLFW from the rest of the engine.

enum class Key : int {
    Unknown = -1,
    Space = 32, Apostrophe = 39, Comma = 44, Minus = 45, Period = 46,
    Slash = 47,
    D0 = 48, D1, D2, D3, D4, D5, D6, D7, D8, D9,
    Semicolon = 59, Equal = 61,
    A = 65, B, C, D, E, F, G, H, I, J, K, L, M,
    N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    LeftBracket = 91, Backslash = 92, RightBracket = 93, GraveAccent = 96,
    Escape = 256, Enter, Tab, Backspace, Insert, Delete,
    Right, Left, Down, Up,
    PageUp, PageDown, Home, End,
    CapsLock = 280, ScrollLock, NumLock, PrintScreen, Pause,
    F1 = 290, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
    KP0 = 320, KP1, KP2, KP3, KP4, KP5, KP6, KP7, KP8, KP9,
    KPDecimal, KPDivide, KPMultiply, KPSubtract, KPAdd, KPEnter, KPEqual,
    LeftShift = 340, LeftControl, LeftAlt, LeftSuper,
    RightShift, RightControl, RightAlt, RightSuper, Menu,
    Count
};

enum class MouseButton : int {
    Left   = 0,
    Right  = 1,
    Middle = 2,
    B4 = 3, B5 = 4, B6 = 5, B7 = 6, B8 = 7,
    Count
};

enum class GamepadButton : int {
    A = 0, B, X, Y,
    LeftBumper, RightBumper,
    Back, Start, Guide,
    LeftThumb, RightThumb,
    DPadUp, DPadRight, DPadDown, DPadLeft,
    Count
};

enum class GamepadAxis : int {
    LeftX = 0, LeftY, RightX, RightY,
    LeftTrigger, RightTrigger,
    Count
};

enum class MouseAxis : int {
    X = 0, Y, ScrollX, ScrollY,
    Count
};

// Key values intentionally mirror GLFW key codes so conversion is a cast.
inline int   GlfwFromKey(Key k)            { return static_cast<int>(k); }
inline Key   KeyFromGlfw(int glfwKey)      { return static_cast<Key>(glfwKey); }
inline int   GlfwFromMouseButton(MouseButton b) { return static_cast<int>(b); }
inline MouseButton MouseButtonFromGlfw(int b)   { return static_cast<MouseButton>(b); }

constexpr int kMaxKeys         = 512;
constexpr int kMaxMouseButtons = 8;
constexpr int kMaxGamepadButtons = static_cast<int>(GamepadButton::Count);
constexpr int kMaxGamepadAxes    = static_cast<int>(GamepadAxis::Count);
constexpr int kMaxGamepads       = 4;
