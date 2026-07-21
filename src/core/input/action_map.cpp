//============================================================================
//  core/input/action_map.cpp: binding evaluation, the query API, the context
//  stack, and the default editor bindings.
//============================================================================
#include "core/input/action_map.h"
#include "core/input/input_system.h"

#include <algorithm>
#include <type_traits>
#include <vector>

namespace toon {
    namespace Input {

        namespace {

            // --- Binding evaluation ---------------------------------------------------------

            bool IsBindingDown(const Binding& b) {
                return std::visit([](auto&& v) -> bool {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, KeyBinding>) {
                        return RawKeyboard().IsDown(v.key);
                    } else if constexpr (std::is_same_v<T, MouseButtonBinding>) {
                        return RawMouse().IsDown(v.button);
                    } else if constexpr (std::is_same_v<T, GamepadButtonBinding>) {
                        return GetGamepad(v.pad).IsButtonDown(v.button);
                    } else if constexpr (std::is_same_v<T, GamepadAxisBinding>) {
                        const float val = GetGamepad(v.pad).GetAxis(v.axis);
                        return v.negative ? (val < -v.deadzone) : (val > v.deadzone);
                    } else {
                        return false;   // MouseAxisBinding has no sensible "is it down"
                    }
                }, b);
            }

            bool WasBindingPressed(const Binding& b) {
                return std::visit([](auto&& v) -> bool {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, KeyBinding>) {
                        return RawKeyboard().WasPressed(v.key);
                    } else if constexpr (std::is_same_v<T, MouseButtonBinding>) {
                        return RawMouse().WasPressed(v.button);
                    } else if constexpr (std::is_same_v<T, GamepadButtonBinding>) {
                        return GetGamepad(v.pad).WasButtonPressed(v.button);
                    } else {
                        return false;   // axis bindings have no press/release edge
                    }
                }, b);
            }

            bool WasBindingReleased(const Binding& b) {
                return std::visit([](auto&& v) -> bool {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, KeyBinding>) {
                        return RawKeyboard().WasReleased(v.key);
                    } else if constexpr (std::is_same_v<T, MouseButtonBinding>) {
                        return RawMouse().WasReleased(v.button);
                    } else if constexpr (std::is_same_v<T, GamepadButtonBinding>) {
                        return GetGamepad(v.pad).WasButtonReleased(v.button);
                    } else {
                        return false;
                    }
                }, b);
            }

            // Evaluate one side (positive or negative) of an axis binding to a signed contribution.
            float BindingAxisValue(const Binding& b) {
                return std::visit([](auto&& v) -> float {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, KeyBinding>) {
                        return RawKeyboard().IsDown(v.key) ? 1.0f : 0.0f;
                    } else if constexpr (std::is_same_v<T, MouseButtonBinding>) {
                        return RawMouse().IsDown(v.button) ? 1.0f : 0.0f;
                    } else if constexpr (std::is_same_v<T, GamepadButtonBinding>) {
                        return GetGamepad(v.pad).IsButtonDown(v.button) ? 1.0f : 0.0f;
                    } else if constexpr (std::is_same_v<T, GamepadAxisBinding>) {
                        const float val = GetGamepad(v.pad).GetAxis(v.axis);
                        return v.negative ? std::min(val, 0.0f) : std::max(val, 0.0f);
                    } else if constexpr (std::is_same_v<T, MouseAxisBinding>) {
                        const Mouse& m = RawMouse();
                        switch (v.axis) {
                            case MouseAxis::X:       return m.delta.x * v.scale;
                            case MouseAxis::Y:       return m.delta.y * v.scale;
                            case MouseAxis::ScrollX: return m.scrollDelta.x * v.scale;
                            case MouseAxis::ScrollY: return m.scrollDelta.y * v.scale;
                            default:                 return 0.0f;
                        }
                    } else {
                        return 0.0f;
                    }
                }, b);
            }

            // Context stack: searched top-down so later-pushed contexts win.
            std::vector<InputContext> gContextStack;

            const Action* FindAction(ActionId id) {
                for (int i = static_cast<int>(gContextStack.size()) - 1; i >= 0; --i) {
                    const auto it = gContextStack[i].map.actions.find(id);
                    if (it != gContextStack[i].map.actions.end()) { return &it->second; }
                }
                return nullptr;
            }

            const Axis* FindAxis(ActionId id) {
                for (int i = static_cast<int>(gContextStack.size()) - 1; i >= 0; --i) {
                    const auto it = gContextStack[i].map.axes.find(id);
                    if (it != gContextStack[i].map.axes.end()) { return &it->second; }
                }
                return nullptr;
            }

        } // namespace

        // --- ActionMap ------------------------------------------------------------------

        void ActionMap::RegisterAction(const char* name) {
            const ActionId id = HashAction(name);
            if (actions.find(id) != actions.end()) { return; }
            actions[id] = Action{id, name, {}};
        }

        void ActionMap::RegisterAxis(const char* name) {
            const ActionId id = HashAction(name);
            if (axes.find(id) != axes.end()) { return; }
            axes[id] = Axis{id, name, {}, {}};
        }

        void ActionMap::BindAction(const char* name, Binding b) {
            const ActionId id = HashAction(name);
            auto it = actions.find(id);
            if (it == actions.end()) { RegisterAction(name); it = actions.find(id); }
            it->second.bindings.push_back(std::move(b));
        }

        void ActionMap::BindAxisPositive(const char* name, Binding b) {
            const ActionId id = HashAction(name);
            auto it = axes.find(id);
            if (it == axes.end()) { RegisterAxis(name); it = axes.find(id); }
            it->second.positive.push_back(std::move(b));
        }

        void ActionMap::BindAxisNegative(const char* name, Binding b) {
            const ActionId id = HashAction(name);
            auto it = axes.find(id);
            if (it == axes.end()) { RegisterAxis(name); it = axes.find(id); }
            it->second.negative.push_back(std::move(b));
        }

        void ActionMap::UnbindAllAction(const char* name) {
            const auto it = actions.find(HashAction(name));
            if (it != actions.end()) { it->second.bindings.clear(); }
        }

        void ActionMap::UnbindAllAxis(const char* name) {
            const auto it = axes.find(HashAction(name));
            if (it != axes.end()) { it->second.positive.clear(); it->second.negative.clear(); }
        }

        // --- Query API ------------------------------------------------------------------

        bool IsActionDown(const char* name) {
            const Action* a = FindAction(HashAction(name));
            if (!a) { return false; }
            for (const Binding& b : a->bindings) { if (IsBindingDown(b)) { return true; } }
            return false;
        }

        bool WasActionPressed(const char* name) {
            const Action* a = FindAction(HashAction(name));
            if (!a) { return false; }
            for (const Binding& b : a->bindings) { if (WasBindingPressed(b)) { return true; } }
            return false;
        }

        bool WasActionReleased(const char* name) {
            const Action* a = FindAction(HashAction(name));
            if (!a) { return false; }
            for (const Binding& b : a->bindings) { if (WasBindingReleased(b)) { return true; } }
            return false;
        }

        float GetAxis(const char* name) {
            const Axis* ax = FindAxis(HashAction(name));
            if (!ax) { return 0.0f; }
            float pos = 0.0f, neg = 0.0f;
            for (const Binding& b : ax->positive) { pos += BindingAxisValue(b); }
            for (const Binding& b : ax->negative) { neg += BindingAxisValue(b); }
            return std::clamp(pos - neg, -1.0f, 1.0f);
        }

        // --- Context stack ----------------------------------------------------------------

        void PushContext(InputContext ctx) { gContextStack.push_back(std::move(ctx)); }

        void PopContext(const char* name) {
            const auto it = std::find_if(gContextStack.begin(), gContextStack.end(),
                                          [name](const InputContext& c) { return c.name == name; });
            if (it != gContextStack.end()) { gContextStack.erase(it); }
        }

        void ClearContexts() { gContextStack.clear(); }

        InputContext* GetContext(const char* name) {
            const auto it = std::find_if(gContextStack.begin(), gContextStack.end(),
                                          [name](const InputContext& c) { return c.name == name; });
            return it != gContextStack.end() ? &*it : nullptr;
        }

        // --- Default editor bindings --------------------------------------------------------

        void RegisterDefaultEditorBindings() {
            InputContext editor;
            editor.name = "editor";
            ActionMap& m = editor.map;

            // Camera fly axes (active while right-drag orbits, see main.cpp). Up/down is E/Q,
            // matching this engine's existing scheme (NOT the old engine's Space/LeftShift), so
            // porting the action map doesn't change today's feel.
            m.BindAxisPositive("camera.fly.forward", KeyBinding{Key::W});
            m.BindAxisNegative("camera.fly.forward", KeyBinding{Key::S});
            m.BindAxisPositive("camera.fly.right",   KeyBinding{Key::D});
            m.BindAxisNegative("camera.fly.right",   KeyBinding{Key::A});
            m.BindAxisPositive("camera.fly.up",      KeyBinding{Key::E});
            m.BindAxisNegative("camera.fly.up",      KeyBinding{Key::Q});

            // Gamepad camera fly (left stick): the same named axes, so keyboard and gamepad both
            // drive them without main.cpp caring which is live.
            m.BindAxisPositive("camera.fly.forward", GamepadAxisBinding{0, GamepadAxis::LeftY, true,  0.15f});
            m.BindAxisNegative("camera.fly.forward", GamepadAxisBinding{0, GamepadAxis::LeftY, false, 0.15f});
            m.BindAxisPositive("camera.fly.right",   GamepadAxisBinding{0, GamepadAxis::LeftX, false, 0.15f});
            m.BindAxisNegative("camera.fly.right",   GamepadAxisBinding{0, GamepadAxis::LeftX, true,  0.15f});

            // Gamepad camera orbit (right stick): a new capability; the mouse-drag orbit stays on
            // raw polling (see main.cpp), so this is gamepad-only.
            m.BindAxisPositive("camera.orbit.x", GamepadAxisBinding{0, GamepadAxis::RightX, false, 0.15f});
            m.BindAxisNegative("camera.orbit.x", GamepadAxisBinding{0, GamepadAxis::RightX, true,  0.15f});
            m.BindAxisPositive("camera.orbit.y", GamepadAxisBinding{0, GamepadAxis::RightY, false, 0.15f});
            m.BindAxisNegative("camera.orbit.y", GamepadAxisBinding{0, GamepadAxis::RightY, true,  0.15f});

            // Button action.
            m.BindAction("camera.focus", KeyBinding{Key::F});

            // NOTE: the reference also bound gizmo.translate/rotate/scale and app.quit here. Neither
            // has a consumer in this engine yet (gizmo hotkeys stay on ImGui's own key routing, see
            // main.cpp; nothing calls WasActionPressed("app.quit")), so they're left out rather than
            // shipped as dead bindings in the generated assets/input.json.

            PushContext(std::move(editor));
        }

    } // namespace Input
} // namespace toon
