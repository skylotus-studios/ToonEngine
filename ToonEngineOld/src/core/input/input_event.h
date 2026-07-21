#pragma once

#include "core/input/keycodes.h"

#include <cstdint>

struct InputEvent {
    enum class Kind : uint8_t {
        KeyDown, KeyUp, Char,
        MouseMove, MouseButtonDown, MouseButtonUp, Scroll,
        GamepadConnected, GamepadDisconnected,
        GamepadButtonDown, GamepadButtonUp,
    };

    Kind kind{};

    union {
        struct { Key key; int mods; }                    key;
        struct { uint32_t codepoint; }                   character;
        struct { double x, y, dx, dy; }                  mouseMove;
        struct { MouseButton button; int mods; }         mouseButton;
        struct { double xOffset, yOffset; }              scroll;
        struct { int jid; }                              gamepad;
        struct { int jid; GamepadButton button; }        gamepadButton;
    };
};
