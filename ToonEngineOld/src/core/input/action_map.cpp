#include "core/input/action_map.h"
#include "core/input/input_system.h"

#include <algorithm>
#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
// Binding evaluation helpers
// ---------------------------------------------------------------------------

namespace {

bool IsBindingDown(const Binding& b) {
    return std::visit([](auto&& v) -> bool {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, KeyBinding>)
            return Input::RawKeyboard().IsDown(v.key);
        else if constexpr (std::is_same_v<T, MouseButtonBinding>)
            return Input::RawMouse().IsDown(v.button);
        else if constexpr (std::is_same_v<T, GamepadButtonBinding>)
            return Input::GetGamepad(v.pad).IsButtonDown(v.button);
        else if constexpr (std::is_same_v<T, GamepadAxisBinding>) {
            float val = Input::GetGamepad(v.pad).GetAxis(v.axis);
            return v.negative ? (val < -v.deadzone) : (val > v.deadzone);
        }
        else return false;
    }, b);
}

bool WasBindingPressed(const Binding& b) {
    return std::visit([](auto&& v) -> bool {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, KeyBinding>)
            return Input::RawKeyboard().WasPressed(v.key);
        else if constexpr (std::is_same_v<T, MouseButtonBinding>)
            return Input::RawMouse().WasPressed(v.button);
        else if constexpr (std::is_same_v<T, GamepadButtonBinding>)
            return Input::GetGamepad(v.pad).WasButtonPressed(v.button);
        else return false;
    }, b);
}

bool WasBindingReleased(const Binding& b) {
    return std::visit([](auto&& v) -> bool {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, KeyBinding>)
            return Input::RawKeyboard().WasReleased(v.key);
        else if constexpr (std::is_same_v<T, MouseButtonBinding>)
            return Input::RawMouse().WasReleased(v.button);
        else if constexpr (std::is_same_v<T, GamepadButtonBinding>)
            return Input::GetGamepad(v.pad).WasButtonReleased(v.button);
        else return false;
    }, b);
}

float BindingAxisValue(const Binding& b) {
    return std::visit([](auto&& v) -> float {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, KeyBinding>)
            return Input::RawKeyboard().IsDown(v.key) ? 1.0f : 0.0f;
        else if constexpr (std::is_same_v<T, MouseButtonBinding>)
            return Input::RawMouse().IsDown(v.button) ? 1.0f : 0.0f;
        else if constexpr (std::is_same_v<T, GamepadButtonBinding>)
            return Input::GetGamepad(v.pad).IsButtonDown(v.button) ? 1.0f : 0.0f;
        else if constexpr (std::is_same_v<T, GamepadAxisBinding>) {
            float val = Input::GetGamepad(v.pad).GetAxis(v.axis);
            return v.negative ? std::min(val, 0.0f) : std::max(val, 0.0f);
        }
        else if constexpr (std::is_same_v<T, MouseAxisBinding>) {
            glm::dvec2 d = Input::RawMouse().delta;
            glm::dvec2 s = Input::RawMouse().scrollDelta;
            switch (v.axis) {
            case MouseAxis::X:       return static_cast<float>(d.x) * v.scale;
            case MouseAxis::Y:       return static_cast<float>(d.y) * v.scale;
            case MouseAxis::ScrollX: return static_cast<float>(s.x) * v.scale;
            case MouseAxis::ScrollY: return static_cast<float>(s.y) * v.scale;
            default: return 0.0f;
            }
        }
        else return 0.0f;
    }, b);
}

// Context stack — searched top-down so later-pushed contexts win.
std::vector<InputContext> gContextStack;

const Action* FindAction(ActionId id) {
    for (int i = static_cast<int>(gContextStack.size()) - 1; i >= 0; --i) {
        auto it = gContextStack[i].map.actions.find(id);
        if (it != gContextStack[i].map.actions.end()) return &it->second;
    }
    return nullptr;
}

const Axis* FindAxis(ActionId id) {
    for (int i = static_cast<int>(gContextStack.size()) - 1; i >= 0; --i) {
        auto it = gContextStack[i].map.axes.find(id);
        if (it != gContextStack[i].map.axes.end()) return &it->second;
    }
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// ActionMap
// ---------------------------------------------------------------------------

void ActionMap::RegisterAction(const char* name) {
    ActionId id = HashAction(name);
    if (actions.find(id) != actions.end()) return;
    actions[id] = Action{id, name, {}};
}

void ActionMap::RegisterAxis(const char* name) {
    ActionId id = HashAction(name);
    if (axes.find(id) != axes.end()) return;
    axes[id] = Axis{id, name, {}, {}};
}

void ActionMap::BindAction(const char* name, Binding b) {
    ActionId id = HashAction(name);
    auto it = actions.find(id);
    if (it == actions.end()) { RegisterAction(name); it = actions.find(id); }
    it->second.bindings.push_back(std::move(b));
}

void ActionMap::BindAxisPositive(const char* name, Binding b) {
    ActionId id = HashAction(name);
    auto it = axes.find(id);
    if (it == axes.end()) { RegisterAxis(name); it = axes.find(id); }
    it->second.positive.push_back(std::move(b));
}

void ActionMap::BindAxisNegative(const char* name, Binding b) {
    ActionId id = HashAction(name);
    auto it = axes.find(id);
    if (it == axes.end()) { RegisterAxis(name); it = axes.find(id); }
    it->second.negative.push_back(std::move(b));
}

void ActionMap::UnbindAllAction(const char* name) {
    ActionId id = HashAction(name);
    auto it = actions.find(id);
    if (it != actions.end()) it->second.bindings.clear();
}

void ActionMap::UnbindAllAxis(const char* name) {
    ActionId id = HashAction(name);
    auto it = axes.find(id);
    if (it != axes.end()) { it->second.positive.clear(); it->second.negative.clear(); }
}

// ---------------------------------------------------------------------------
// Public query API
// ---------------------------------------------------------------------------

bool IsActionDown(const char* name) {
    const Action* a = FindAction(HashAction(name));
    if (!a) return false;
    for (auto& b : a->bindings)
        if (IsBindingDown(b)) return true;
    return false;
}

bool WasActionPressed(const char* name) {
    const Action* a = FindAction(HashAction(name));
    if (!a) return false;
    for (auto& b : a->bindings)
        if (WasBindingPressed(b)) return true;
    return false;
}

bool WasActionReleased(const char* name) {
    const Action* a = FindAction(HashAction(name));
    if (!a) return false;
    for (auto& b : a->bindings)
        if (WasBindingReleased(b)) return true;
    return false;
}

float GetAxis(const char* name) {
    const Axis* ax = FindAxis(HashAction(name));
    if (!ax) return 0.0f;
    float pos = 0.0f, neg = 0.0f;
    for (auto& b : ax->positive) pos += BindingAxisValue(b);
    for (auto& b : ax->negative) neg += BindingAxisValue(b);
    return std::clamp(pos - neg, -1.0f, 1.0f);
}

// ---------------------------------------------------------------------------
// Context stack
// ---------------------------------------------------------------------------

void PushContext(InputContext ctx) {
    gContextStack.push_back(std::move(ctx));
}

void PopContext(const char* name) {
    auto it = std::find_if(gContextStack.begin(), gContextStack.end(),
        [name](const InputContext& c) { return c.name == name; });
    if (it != gContextStack.end()) gContextStack.erase(it);
}

void ClearContexts() {
    gContextStack.clear();
}

// ---------------------------------------------------------------------------
// Default editor bindings
// ---------------------------------------------------------------------------

void RegisterDefaultEditorBindings() {
    InputContext editor;
    editor.name = "editor";
    auto& m = editor.map;

    // Camera fly axes.
    m.BindAxisPositive("camera.fly.forward", KeyBinding{Key::W});
    m.BindAxisNegative("camera.fly.forward", KeyBinding{Key::S});
    m.BindAxisPositive("camera.fly.right",   KeyBinding{Key::D});
    m.BindAxisNegative("camera.fly.right",   KeyBinding{Key::A});
    m.BindAxisPositive("camera.fly.up",      KeyBinding{Key::Space});
    m.BindAxisNegative("camera.fly.up",      KeyBinding{Key::LeftShift});

    // Gamepad camera fly (left stick).
    m.BindAxisPositive("camera.fly.forward",
        GamepadAxisBinding{0, GamepadAxis::LeftY, true, 0.15f});
    m.BindAxisNegative("camera.fly.forward",
        GamepadAxisBinding{0, GamepadAxis::LeftY, false, 0.15f});
    m.BindAxisPositive("camera.fly.right",
        GamepadAxisBinding{0, GamepadAxis::LeftX, false, 0.15f});
    m.BindAxisNegative("camera.fly.right",
        GamepadAxisBinding{0, GamepadAxis::LeftX, true, 0.15f});

    // Gamepad camera orbit (right stick).
    m.BindAxisPositive("camera.orbit.x",
        GamepadAxisBinding{0, GamepadAxis::RightX, false, 0.15f});
    m.BindAxisNegative("camera.orbit.x",
        GamepadAxisBinding{0, GamepadAxis::RightX, true, 0.15f});
    m.BindAxisPositive("camera.orbit.y",
        GamepadAxisBinding{0, GamepadAxis::RightY, false, 0.15f});
    m.BindAxisNegative("camera.orbit.y",
        GamepadAxisBinding{0, GamepadAxis::RightY, true, 0.15f});

    // Button actions.
    m.BindAction("camera.focus",    KeyBinding{Key::F});
    m.BindAction("gizmo.translate", KeyBinding{Key::W});
    m.BindAction("gizmo.rotate",    KeyBinding{Key::E});
    m.BindAction("gizmo.scale",     KeyBinding{Key::R});
    m.BindAction("app.quit",        KeyBinding{Key::Escape});

    PushContext(std::move(editor));
}
