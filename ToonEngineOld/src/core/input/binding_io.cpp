#include "core/input/binding_io.h"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <unordered_map>

using json = nlohmann::json;

namespace {

// Key name → enum maps. Populated on first use.
struct KeyNameTable {
    std::unordered_map<std::string, Key> byName;
    std::unordered_map<int, std::string> byCode;

    KeyNameTable() {
        auto add = [&](const char* n, Key k) {
            byName[n] = k;
            byCode[static_cast<int>(k)] = n;
        };
        add("Space", Key::Space);
        add("Apostrophe", Key::Apostrophe);
        add("Comma", Key::Comma); add("Minus", Key::Minus);
        add("Period", Key::Period); add("Slash", Key::Slash);
        for (int i = 0; i <= 9; ++i) {
            char buf[4]; std::snprintf(buf, sizeof(buf), "%d", i);
            add(buf, static_cast<Key>(static_cast<int>(Key::D0) + i));
        }
        add("Semicolon", Key::Semicolon); add("Equal", Key::Equal);
        for (int i = 0; i < 26; ++i) {
            char buf[2] = {static_cast<char>('A' + i), '\0'};
            add(buf, static_cast<Key>(static_cast<int>(Key::A) + i));
        }
        add("LeftBracket", Key::LeftBracket); add("Backslash", Key::Backslash);
        add("RightBracket", Key::RightBracket); add("GraveAccent", Key::GraveAccent);
        add("Escape", Key::Escape); add("Enter", Key::Enter);
        add("Tab", Key::Tab); add("Backspace", Key::Backspace);
        add("Insert", Key::Insert); add("Delete", Key::Delete);
        add("Right", Key::Right); add("Left", Key::Left);
        add("Down", Key::Down); add("Up", Key::Up);
        add("PageUp", Key::PageUp); add("PageDown", Key::PageDown);
        add("Home", Key::Home); add("End", Key::End);
        add("CapsLock", Key::CapsLock); add("ScrollLock", Key::ScrollLock);
        add("NumLock", Key::NumLock); add("PrintScreen", Key::PrintScreen);
        add("Pause", Key::Pause);
        for (int i = 1; i <= 12; ++i) {
            char buf[4]; std::snprintf(buf, sizeof(buf), "F%d", i);
            add(buf, static_cast<Key>(static_cast<int>(Key::F1) + i - 1));
        }
        add("LeftShift", Key::LeftShift); add("LeftControl", Key::LeftControl);
        add("LeftAlt", Key::LeftAlt); add("LeftSuper", Key::LeftSuper);
        add("RightShift", Key::RightShift); add("RightControl", Key::RightControl);
        add("RightAlt", Key::RightAlt); add("RightSuper", Key::RightSuper);
        add("Menu", Key::Menu);
    }
};

const KeyNameTable& Table() {
    static KeyNameTable t;
    return t;
}

const char* GamepadButtonName(GamepadButton b) {
    switch (b) {
    case GamepadButton::A: return "A"; case GamepadButton::B: return "B";
    case GamepadButton::X: return "X"; case GamepadButton::Y: return "Y";
    case GamepadButton::LeftBumper: return "LeftBumper";
    case GamepadButton::RightBumper: return "RightBumper";
    case GamepadButton::Back: return "Back"; case GamepadButton::Start: return "Start";
    case GamepadButton::Guide: return "Guide";
    case GamepadButton::LeftThumb: return "LeftThumb";
    case GamepadButton::RightThumb: return "RightThumb";
    case GamepadButton::DPadUp: return "DPadUp"; case GamepadButton::DPadRight: return "DPadRight";
    case GamepadButton::DPadDown: return "DPadDown"; case GamepadButton::DPadLeft: return "DPadLeft";
    default: return "?";
    }
}

const char* GamepadAxisName(GamepadAxis a) {
    switch (a) {
    case GamepadAxis::LeftX: return "LeftX"; case GamepadAxis::LeftY: return "LeftY";
    case GamepadAxis::RightX: return "RightX"; case GamepadAxis::RightY: return "RightY";
    case GamepadAxis::LeftTrigger: return "LeftTrigger";
    case GamepadAxis::RightTrigger: return "RightTrigger";
    default: return "?";
    }
}

const char* MouseAxisName(MouseAxis a) {
    switch (a) {
    case MouseAxis::X: return "X"; case MouseAxis::Y: return "Y";
    case MouseAxis::ScrollX: return "ScrollX"; case MouseAxis::ScrollY: return "ScrollY";
    default: return "?";
    }
}

const char* MouseButtonName(MouseButton b) {
    switch (b) {
    case MouseButton::Left: return "Left"; case MouseButton::Right: return "Right";
    case MouseButton::Middle: return "Middle";
    default: return "?";
    }
}

// -- Serialize a single Binding to JSON ------------------------------------

json BindingToJson(const Binding& b) {
    return std::visit([](auto&& v) -> json {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, KeyBinding>)
            return {{"type", "key"}, {"key", Table().byCode.at(static_cast<int>(v.key))}};
        else if constexpr (std::is_same_v<T, MouseButtonBinding>)
            return {{"type", "mouse_button"}, {"button", MouseButtonName(v.button)}};
        else if constexpr (std::is_same_v<T, GamepadButtonBinding>)
            return {{"type", "gamepad_button"}, {"pad", v.pad}, {"button", GamepadButtonName(v.button)}};
        else if constexpr (std::is_same_v<T, GamepadAxisBinding>)
            return {{"type", "gamepad_axis"}, {"pad", v.pad}, {"axis", GamepadAxisName(v.axis)},
                    {"negative", v.negative}, {"deadzone", v.deadzone}};
        else if constexpr (std::is_same_v<T, MouseAxisBinding>)
            return {{"type", "mouse_axis"}, {"axis", MouseAxisName(v.axis)}, {"scale", v.scale}};
        else return {};
    }, b);
}

Binding BindingFromJson(const json& j) {
    std::string type = j.value("type", "");
    if (type == "key") {
        std::string name = j.value("key", "");
        auto& tbl = Table();
        auto it = tbl.byName.find(name);
        Key k = (it != tbl.byName.end()) ? it->second : Key::Unknown;
        return KeyBinding{k};
    }
    if (type == "mouse_button") {
        std::string name = j.value("button", "");
        MouseButton b = MouseButton::Left;
        if (name == "Right") b = MouseButton::Right;
        else if (name == "Middle") b = MouseButton::Middle;
        return MouseButtonBinding{b};
    }
    if (type == "gamepad_button") {
        int pad = j.value("pad", 0);
        std::string name = j.value("button", "");
        GamepadButton btn = GamepadButton::A;
        // Simple linear search among known names.
        for (int i = 0; i < static_cast<int>(GamepadButton::Count); ++i) {
            if (GamepadButtonName(static_cast<GamepadButton>(i)) == name) {
                btn = static_cast<GamepadButton>(i); break;
            }
        }
        return GamepadButtonBinding{pad, btn};
    }
    if (type == "gamepad_axis") {
        int pad = j.value("pad", 0);
        std::string name = j.value("axis", "");
        GamepadAxis ax = GamepadAxis::LeftX;
        for (int i = 0; i < static_cast<int>(GamepadAxis::Count); ++i) {
            if (GamepadAxisName(static_cast<GamepadAxis>(i)) == name) {
                ax = static_cast<GamepadAxis>(i); break;
            }
        }
        return GamepadAxisBinding{pad, ax, j.value("negative", false),
                                  j.value("deadzone", 0.15f)};
    }
    if (type == "mouse_axis") {
        std::string name = j.value("axis", "");
        MouseAxis ax = MouseAxis::X;
        for (int i = 0; i < static_cast<int>(MouseAxis::Count); ++i) {
            if (MouseAxisName(static_cast<MouseAxis>(i)) == name) {
                ax = static_cast<MouseAxis>(i); break;
            }
        }
        return MouseAxisBinding{ax, j.value("scale", 1.0f)};
    }
    return KeyBinding{Key::Unknown};
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

namespace BindingIO {

const char* KeyName(Key k) {
    auto& tbl = Table();
    auto it = tbl.byCode.find(static_cast<int>(k));
    return (it != tbl.byCode.end()) ? it->second.c_str() : "?";
}

Key KeyByName(const char* name) {
    auto& tbl = Table();
    auto it = tbl.byName.find(name);
    return (it != tbl.byName.end()) ? it->second : Key::Unknown;
}

bool Save(const char* path, const InputContext& ctx) {
    json root;
    root["context"] = ctx.name;

    json actionsJ = json::object();
    for (auto& [id, action] : ctx.map.actions) {
        json arr = json::array();
        for (auto& b : action.bindings) arr.push_back(BindingToJson(b));
        actionsJ[action.name] = arr;
    }
    root["actions"] = actionsJ;

    json axesJ = json::object();
    for (auto& [id, axis] : ctx.map.axes) {
        json pos = json::array(), neg = json::array();
        for (auto& b : axis.positive) pos.push_back(BindingToJson(b));
        for (auto& b : axis.negative) neg.push_back(BindingToJson(b));
        axesJ[axis.name] = {{"positive", pos}, {"negative", neg}};
    }
    root["axes"] = axesJ;

    std::ofstream f(path);
    if (!f.is_open()) {
        std::fprintf(stderr, "Failed to save bindings: %s\n", path);
        return false;
    }
    f << root.dump(2) << "\n";
    std::printf("Bindings saved: %s\n", path);
    return true;
}

bool Load(const char* path, InputContext& ctx) {
    std::ifstream f(path);
    if (!f.is_open()) return false;

    json root;
    try { root = json::parse(f); }
    catch (...) {
        std::fprintf(stderr, "Failed to parse bindings: %s\n", path);
        return false;
    }

    ctx.name = root.value("context", ctx.name);
    ctx.map.actions.clear();
    ctx.map.axes.clear();

    if (root.contains("actions")) {
        for (auto& [name, arr] : root["actions"].items()) {
            for (auto& bj : arr)
                ctx.map.BindAction(name.c_str(), BindingFromJson(bj));
        }
    }

    if (root.contains("axes")) {
        for (auto& [name, obj] : root["axes"].items()) {
            if (obj.contains("positive"))
                for (auto& bj : obj["positive"])
                    ctx.map.BindAxisPositive(name.c_str(), BindingFromJson(bj));
            if (obj.contains("negative"))
                for (auto& bj : obj["negative"])
                    ctx.map.BindAxisNegative(name.c_str(), BindingFromJson(bj));
        }
    }

    std::printf("Bindings loaded: %s\n", path);
    return true;
}

} // namespace BindingIO
