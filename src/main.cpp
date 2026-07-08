//============================================================================
//  ToonEngine — entry point.
//
//  Owns the window + game loop and drives the renderer. All GPU/backend work
//  lives behind core/renderer.h (see that header for the seam rationale); this
//  file includes no Diligent header at all — that's the point of the seam.
//============================================================================
#include "core/renderer.h"

#include <cstdint>
#include <cstdio>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

// Dear ImGui is a plain UI library, not a Diligent type, so engine/game code
// is free to include it directly and call ImGui:: between Renderer::BeginUI()
// and EndUI(). Diligent's ImGui *renderer* glue stays behind the seam.
#include "imgui.h"

int main() {
    if (!glfwInit()) {
        std::fprintf(stderr, "GLFW init failed\n");
        return 1;
    }

    // We render with Vulkan, so tell GLFW not to create an OpenGL context.
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    GLFWwindow* window = glfwCreateWindow(3840, 2160, "ToonEngine", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }

    toon::Renderer renderer;
    if (!renderer.Init(window)) {
        std::fprintf(stderr, "Renderer init failed\n");
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    if (!renderer.InitUI(window)) {
        std::fprintf(stderr, "Renderer UI init failed\n");
        renderer.Shutdown();
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // Route framebuffer resizes to the renderer's swap chain.
    glfwSetWindowUserPointer(window, &renderer);
    glfwSetFramebufferSizeCallback(window, [](GLFWwindow* w, int width, int height) {
        if (auto* r = static_cast<toon::Renderer*>(glfwGetWindowUserPointer(w)))
            r->Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
        });

    const toon::Color clearColor{ 0.0f, 0.0f, 0.0f, 1.0f };
    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();
        renderer.BeginFrame(clearColor);

        renderer.BeginUI();
        // TODO: grow this into the real editor/debug UI (roadmap: render stats,
        // status bar, etc.); for now it proves the ImGui path end to end.
        if (ImGui::Begin("ToonEngine Debug")) {
            ImGui::Text("Renderer seam + Dear ImGui OK");
            ImGui::Text("%.1f FPS (%.3f ms/frame)", ImGui::GetIO().Framerate,
                        1000.0f / ImGui::GetIO().Framerate);
        }
        ImGui::End();
        renderer.EndUI();

        // TODO: scene draw calls go here.
        renderer.EndFrame();
    }

    renderer.Shutdown();
    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
