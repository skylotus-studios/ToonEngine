#pragma once
//============================================================================
//  core/input.h — a small input layer (mouse + keyboard polling + capture gate).
//
//  GLFW-based, backend-agnostic (no Diligent). Enough to drive the editor camera; the
//  full action-map / rebinding system (ToonEngineOld/src/core/input) is deferred. All
//  queries are gated by SetCaptured(), which main.cpp feeds from ImGui's WantCapture so
//  UI interaction suppresses the camera.
//============================================================================
struct GLFWwindow;

namespace toon { namespace Input {

enum class Mouse { Left, Right, Middle };
enum class Key   { W, A, S, D, Q, E, F };

// Install the scroll callback. Call BEFORE Renderer::InitUI so ImGui's GLFW backend
// (install_callbacks = true) chains it instead of overwriting it.
void Init(GLFWwindow* window);

// Poll this frame's state: cursor position -> delta, and latch the scroll accumulator.
void BeginFrame();

// Feed ImGui's capture flags each frame (main.cpp reads io.WantCaptureMouse/Keyboard).
// When mouse (resp. keyboard) is captured, the mouse (resp. key) queries return neutral.
void SetCaptured(bool mouseCaptured, bool keyboardCaptured);

bool  IsMouseDown(Mouse b);
void  MouseDelta(float& dx, float& dy);   // pixels since last BeginFrame (0 if captured)
float ScrollDelta();                      // notches since last BeginFrame (0 if captured)
bool  IsKeyDown(Key k);

}} // namespace toon::Input
