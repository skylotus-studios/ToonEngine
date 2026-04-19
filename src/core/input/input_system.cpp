#include "core/input/input_system.h"

#include <GLFW/glfw3.h>

#include <string>
#include <vector>

namespace {

GLFWwindow* gWindow = nullptr;

Keyboard gKeyboard;
Mouse    gMouse;
Gamepad  gGamepads[kMaxGamepads];

bool gKeyboardCaptured = false;
bool gMouseCaptured    = false;

std::vector<InputEvent> gEvents;
std::vector<InputEvent> gPending;

// GLFW callbacks ----------------------------------------------------------

void KeyCallback(GLFWwindow*, int key, int /*scancode*/, int action, int mods) {
    gKeyboard.OnKey(key, action);
    InputEvent e{};
    e.kind     = (action == GLFW_RELEASE) ? InputEvent::Kind::KeyUp
                                          : InputEvent::Kind::KeyDown;
    e.key.key  = KeyFromGlfw(key);
    e.key.mods = mods;
    gPending.push_back(e);
}

void CharCallback(GLFWwindow*, unsigned int codepoint) {
    InputEvent e{};
    e.kind = InputEvent::Kind::Char;
    e.character.codepoint = codepoint;
    gPending.push_back(e);
}

void MouseButtonCallback(GLFWwindow*, int button, int action, int mods) {
    gMouse.OnButton(button, action);
    InputEvent e{};
    e.kind = (action == GLFW_RELEASE) ? InputEvent::Kind::MouseButtonUp
                                      : InputEvent::Kind::MouseButtonDown;
    e.mouseButton.button = MouseButtonFromGlfw(button);
    e.mouseButton.mods   = mods;
    gPending.push_back(e);
}

void CursorPosCallback(GLFWwindow*, double x, double y) {
    double oldX = gMouse.position.x;
    double oldY = gMouse.position.y;
    gMouse.OnMove(x, y);
    InputEvent e{};
    e.kind = InputEvent::Kind::MouseMove;
    e.mouseMove = {x, y, x - oldX, y - oldY};
    gPending.push_back(e);
}

void ScrollCallback(GLFWwindow*, double xoff, double yoff) {
    gMouse.OnScroll(xoff, yoff);
    InputEvent e{};
    e.kind = InputEvent::Kind::Scroll;
    e.scroll = {xoff, yoff};
    gPending.push_back(e);
}

void CursorEnterCallback(GLFWwindow*, int entered) {
    gMouse.OnEnter(entered != 0);
}

void JoystickCallback(int jid, int event) {
    if (jid < 0 || jid >= kMaxGamepads) return;
    InputEvent e{};
    if (event == GLFW_CONNECTED && glfwJoystickIsGamepad(jid)) {
        gGamepads[jid].connected = true;
        gGamepads[jid].jid       = jid;
        e.kind       = InputEvent::Kind::GamepadConnected;
        e.gamepad.jid = jid;
        gPending.push_back(e);
    } else if (event == GLFW_DISCONNECTED) {
        gGamepads[jid].connected = false;
        e.kind       = InputEvent::Kind::GamepadDisconnected;
        e.gamepad.jid = jid;
        gPending.push_back(e);
    }
}

std::vector<std::string> gDroppedFiles;

void DropCallback(GLFWwindow*, int count, const char** paths) {
    for (int i = 0; i < count; ++i)
        gDroppedFiles.emplace_back(paths[i]);
}

} // namespace

namespace Input {

void Init(GLFWwindow* window) {
    gWindow = window;

    glfwSetKeyCallback(window, KeyCallback);
    glfwSetCharCallback(window, CharCallback);
    glfwSetMouseButtonCallback(window, MouseButtonCallback);
    glfwSetCursorPosCallback(window, CursorPosCallback);
    glfwSetScrollCallback(window, ScrollCallback);
    glfwSetCursorEnterCallback(window, CursorEnterCallback);
    glfwSetJoystickCallback(JoystickCallback);
    glfwSetDropCallback(window, DropCallback);

    // Detect gamepads already connected at startup.
    for (int jid = 0; jid < kMaxGamepads; ++jid) {
        if (glfwJoystickPresent(jid) && glfwJoystickIsGamepad(jid)) {
            gGamepads[jid].connected = true;
            gGamepads[jid].jid       = jid;
        }
    }

    // Seed mouse position so the first delta isn't garbage.
    double mx, my;
    glfwGetCursorPos(window, &mx, &my);
    gMouse.position = {mx, my};
    gMouse.previousPosition = gMouse.position;
}

void BeginFrame() {
    gKeyboard.BeginFrame();
    gMouse.BeginFrame();

    for (auto& gp : gGamepads) {
        gp.BeginFrame();
        gp.Poll();
    }

    // Swap pending events into the consumer-visible buffer.
    gEvents.swap(gPending);
    gPending.clear();
}

void Shutdown() {
    gWindow = nullptr;
}

void SetCaptured(bool keyboardCaptured, bool mouseCaptured) {
    gKeyboardCaptured = keyboardCaptured;
    gMouseCaptured    = mouseCaptured;
}

bool IsKeyboardCaptured() { return gKeyboardCaptured; }
bool IsMouseCaptured()    { return gMouseCaptured; }

// Keyboard ---------------------------------------------------------------

bool IsKeyDown(Key k)      { return !gKeyboardCaptured && gKeyboard.IsDown(k); }
bool WasKeyPressed(Key k)  { return !gKeyboardCaptured && gKeyboard.WasPressed(k); }
bool WasKeyReleased(Key k) { return !gKeyboardCaptured && gKeyboard.WasReleased(k); }

// Mouse ------------------------------------------------------------------

bool       IsMouseDown(MouseButton b)      { return !gMouseCaptured && gMouse.IsDown(b); }
bool       WasMousePressed(MouseButton b)  { return !gMouseCaptured && gMouse.WasPressed(b); }
bool       WasMouseReleased(MouseButton b) { return !gMouseCaptured && gMouse.WasReleased(b); }
glm::dvec2 MousePosition()                 { return gMouse.position; }
glm::dvec2 MouseDelta()                    { return gMouseCaptured ? glm::dvec2{0.0} : gMouse.delta; }
glm::dvec2 ScrollDelta()                   { return gMouseCaptured ? glm::dvec2{0.0} : gMouse.scrollDelta; }

// Gamepad ----------------------------------------------------------------

int            GamepadCount() {
    int n = 0;
    for (auto& gp : gGamepads) if (gp.connected) ++n;
    return n;
}
const Gamepad& GetGamepad(int index) {
    static const Gamepad sEmpty{};
    if (index < 0 || index >= kMaxGamepads) return sEmpty;
    return gGamepads[index];
}

// Raw access -------------------------------------------------------------

const Keyboard& RawKeyboard() { return gKeyboard; }
const Mouse&    RawMouse()    { return gMouse; }

// Events -----------------------------------------------------------------

std::span<const InputEvent> Events() {
    return {gEvents.data(), gEvents.size()};
}

void EachEvent(const std::function<void(const InputEvent&)>& fn) {
    for (auto& e : gEvents) fn(e);
}

std::vector<std::string> DroppedFiles() {
    std::vector<std::string> result;
    result.swap(gDroppedFiles);
    return result;
}

} // namespace Input
