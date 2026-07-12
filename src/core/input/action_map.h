#pragma once
//============================================================================
//  core/input/action_map.h — named, rebindable actions/axes over the raw
//  device layer, plus a push/pop context stack.
//
//  Actions are hashed by name (FNV-1a) for O(1) lookup; each can carry several physical
//  bindings (first match wins). An axis combines a positive/negative binding SET into one
//  -1..1 value, so e.g. keyboard WASD and a gamepad stick can drive the same named axis
//  without the caller caring which is live (see RegisterDefaultEditorBindings). Contexts let
//  a future mode (e.g. play vs. edit) push its own bindings that shadow the ones below it on
//  the stack; today only "editor" is ever pushed.
//
//  Ported from ToonEngineOld/src/core/input/action_map.h (design unchanged); this file also
//  covers what was global-scope in the reference (RegisterDefaultEditorBindings, the query
//  API), now under toon::Input like the rest of this subsystem.
//============================================================================
#include "core/input/keycodes.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace toon { namespace Input {

// FNV-1a hash of an action/axis name for O(1) lookup.
using ActionId = uint32_t;

constexpr ActionId HashAction(const char* s) {
    uint32_t h = 2166136261u;
    for (; *s; ++s) { h = (h ^ static_cast<uint8_t>(*s)) * 16777619u; }
    return h;
}

// Physical-input bindings. Each variant knows how to evaluate itself (action_map.cpp).
struct KeyBinding            { Key key; };
struct MouseButtonBinding   { MouseButton button; };
struct GamepadButtonBinding  { int pad; GamepadButton button; };
struct GamepadAxisBinding    { int pad; GamepadAxis axis; bool negative; float deadzone; };
struct MouseAxisBinding      { MouseAxis axis; float scale; };

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

// Analog-style axis: evaluates to [-1, 1].
struct Axis {
    ActionId             id{};
    std::string          name;
    std::vector<Binding> positive;
    std::vector<Binding> negative;
};

// A set of named actions and axes.
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

// A named overlay that can be pushed/popped on the context stack. The topmost context that
// has a matching action/axis name wins the query.
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

// Find a pushed context by name — e.g. so BindingIO::Load can override its bindings from a
// saved file after RegisterDefaultEditorBindings pushes it (see main.cpp's startup). Returns
// null if no context by that name is on the stack.
InputContext* GetContext(const char* name);

// Push the default "editor" context: camera fly/orbit axes (keyboard + gamepad) and
// camera.focus. Call once at startup, before optionally overriding it from a saved bindings
// file (core/input/binding_io.h).
void RegisterDefaultEditorBindings();

}} // namespace toon::Input
