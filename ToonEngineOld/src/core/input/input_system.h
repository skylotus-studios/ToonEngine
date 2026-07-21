#pragma once

#include "core/input/input_device.h"
#include "core/input/input_event.h"
#include "core/input/keycodes.h"

#include <glm/glm.hpp>

#include <functional>
#include <span>
#include <string>
#include <vector>

struct GLFWwindow;

namespace Input {

// Lifecycle — called from main.cpp.
void Init(GLFWwindow* window);
void BeginFrame();
void Shutdown();

// Capture gate — set by the overlay each frame so ImGui interaction
// suppresses engine input.
void SetCaptured(bool keyboardCaptured, bool mouseCaptured);
bool IsKeyboardCaptured();
bool IsMouseCaptured();

// Keyboard polling (respects capture gate).
bool IsKeyDown(Key k);
bool WasKeyPressed(Key k);
bool WasKeyReleased(Key k);

// Mouse polling (respects capture gate).
bool      IsMouseDown(MouseButton b);
bool      WasMousePressed(MouseButton b);
bool      WasMouseReleased(MouseButton b);
glm::dvec2 MousePosition();
glm::dvec2 MouseDelta();
glm::dvec2 ScrollDelta();

// Gamepad polling (no capture gate — physical controller is never ambiguous).
int            GamepadCount();
const Gamepad& GetGamepad(int index);

// Raw device access — bypasses capture gate. For ImGui internals or
// diagnostic panels that need unfiltered state.
const Keyboard& RawKeyboard();
const Mouse&    RawMouse();

// Per-frame event stream — consumed between BeginFrame and next BeginFrame.
std::span<const InputEvent> Events();
void EachEvent(const std::function<void(const InputEvent&)>& fn);

// OS file drops — returns and clears the list of paths dropped this frame.
std::vector<std::string> DroppedFiles();

} // namespace Input
