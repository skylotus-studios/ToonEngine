#pragma once
//============================================================================
//  app/editor_init.h: one-time editor setup.
//============================================================================
struct GLFWwindow;

namespace toon {

    struct EditorState;

    // Renderer/PhysicsWorld/input init, font + starting theme, the framebuffer-resize
    // callback, and the demo scene (procedural primitives + a loaded glTF model + falling
    // physics bodies) main.cpp used to build inline. `window` must already exist (GLFW init +
    // glfwCreateWindow are main.cpp's job, since EditorState doesn't exist until this returns).
    // Returns false on an unrecoverable failure (Renderer::Init/InitUI) -- PhysicsWorld::Init
    // failing is logged but non-fatal, matching the original main().
    bool InitEditor(EditorState &state, GLFWwindow *window);

} // namespace toon
