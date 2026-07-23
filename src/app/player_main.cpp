//============================================================================
//  app/player_main.cpp: entry point for the standalone player (ToonPlayer.exe).
//
//  The shippable artifact: it links ToonRuntime and nothing from ui/panels/ or ImGuizmo, so a
//  built game contains no editor. All it does is open a window and hand off to the runtime loop
//  (app/runtime_init.h) -- the same loop the editor's `--play` dev flag drives. The scene to
//  play is argv[1] (defaulting to the bundled default scene).
//
//  Diligent-free like main.cpp: all GPU/backend work stays behind core/rendering/renderer.h.
//============================================================================
#include "app/runtime_init.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <ios>
#include <iostream>

int main(int argc, char **argv) {
    // Unbuffered stdout so a silent early-init failure still prints (see main.cpp).
    std::cout.setf(std::ios::unitbuf);

    const char *scenePath = (argc > 1) ? argv[1] : TOON_SCENES_DIR "/default.scene";

    if (!glfwInit()) {
        std::fprintf(stderr, "GLFW init failed\n");
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // Vulkan, not GL

    GLFWwindow *window = glfwCreateWindow(1600, 900, "ToonEngine", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }

    toon::RuntimeState rs;
    if (!toon::InitRuntime(rs, window, scenePath)) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    toon::RunRuntimeLoop(rs);
    toon::ShutdownRuntime(rs);

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
