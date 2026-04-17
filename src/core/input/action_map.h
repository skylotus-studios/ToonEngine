#pragma once

#include "core/input/keycodes.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

// FNV-1a hash of an action/axis name for O(1) lookup.
using ActionId = uint32_t;

constexpr ActionId HashAction(const char* s) {
    uint32_t h = 2166136261u;
    for (; *s; ++s)
        h = (h ^ static_cast<uint8_t>(*s)) * 16777619u;
    return h;
}

// Physical-input bindings. Each variant knows how to evaluate itself.

struct KeyBinding          { Key key; };
struct MouseButtonBinding  { MouseButton button; };
struct GamepadButtonBinding { int pad; GamepadButton button; };
struct GamepadAxisBinding   { int pad; GamepadAxis axis; bool negative; float deadzone; };
struct MouseAxisBinding     { MouseAxis axis; float scale; };

using Binding = std::variant<
    KeyBinding,
    MouseButtonBinding,
    GamepadButtonBinding,
    GamepadAxisBinding,
    MouseAxisBinding
>;

// Button-style action: evaluates to on/off.
struct Action {
    ActionId             id{};
    std::string          name;
    std::vector<Binding> bindings;
};

// Analog-style axis: evaluates to [-1, 1] or raw device value.
struct Axis {
    ActionId             id{};
    std::string          name;
    std::vector<Binding> positive;
    std::vector<Binding> negative;
};

// An ActionMap holds a set of named actions and axes.
struct ActionMap {
    std::unordered_map<ActionId, Action> actions;
    std::unordered_map<ActionId, Axis>   axes;

    void RegisterAction(const char* name);
    void RegisterAxis(const char* name);

    void BindAction(const char* name, Binding b);
    void BindAxisPositive(const char* name, Binding b);
    void BindAxisNegative(const char* name, Binding b);

    void UnbindAllAction(const char* name);
    void UnbindAllAxis(const char* name);
};

// A Context is a named overlay that can be pushed/popped. The topmost
// context with a matching action wins.
struct InputContext {
    std::string name;
    ActionMap   map;
};

// Query the action/axis system (searches the context stack top-down).
bool  IsActionDown(const char* name);
bool  WasActionPressed(const char* name);
bool  WasActionReleased(const char* name);
float GetAxis(const char* name);

// Context stack management.
void PushContext(InputContext ctx);
void PopContext(const char* name);
void ClearContexts();

// Register default editor bindings (camera, gizmo, quit).
void RegisterDefaultEditorBindings();
