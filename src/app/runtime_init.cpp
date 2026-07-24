//============================================================================
//  app/runtime_init.cpp: see runtime_init.h.
//============================================================================
#include "app/runtime_init.h"

#include "app/app_state.h"
#include "app/runtime_render.h"
#include "app/runtime_tick.h"
#include "core/input/input_system.h"
#include "core/platform/paths.h" // Assets::Icon (exe-relative asset paths)

#include <GLFW/glfw3.h>

#include <cstdint>
#include <cstdio>

namespace toon {

    bool InitRuntime(RuntimeState &rs, GLFWwindow *window, const char *scenePath) {
        rs.window = window;
        SetWindowIcon(window, Assets::Icon().c_str());

        if (!rs.renderer.Init(window)) {
            std::fprintf(stderr, "Renderer init failed\n");
            return false;
        }

        // Jolt's and miniaudio's one-time setup, same as the editor's InitEditor. Neither world
        // has any bodies/sounds until the scene loads and Play (here, AppState::Playing) begins.
        if (!rs.physicsWorld.Init()) { std::fprintf(stderr, "PhysicsWorld init failed\n"); }
        if (!rs.audio.Init()) { std::fprintf(stderr, "AudioEngine init failed\n"); }

        // Input, but no ImGui: the runtime's key handling (AppState transitions) reads raw device
        // state, so no action-map context is needed. Callbacks still install so key/mouse edges
        // register (the editor installs these BEFORE InitUI so ImGui chains them; there's no UI
        // here to chain).
        Input::Init(window);

        // Route framebuffer resizes to the swap chain (same hook the editor installs).
        glfwSetWindowUserPointer(window, &rs.renderer);
        glfwSetFramebufferSizeCallback(window, [](GLFWwindow *w, int width, int height) {
            if (auto *r = static_cast<Renderer *>(glfwGetWindowUserPointer(w))) {
                r->Resize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
            }
        });

        // The scene to bring up isn't loaded here: the Loading state loads it (app/app_state.cpp's
        // DrainLoadJob), so the load happens inside the frame loop with the window responsive.
        rs.pendingScenePath = scenePath ? scenePath : "";

        rs.lastTime = glfwGetTime();
        rs.appState = AppState::Boot;
        return true;
    }

    void RunRuntimeLoop(RuntimeState &rs) {
        while (rs.appState != AppState::Quit && !glfwWindowShouldClose(rs.window)) {
            const double frameTime = RuntimeBeginFrame(rs);

            // Sim advance is decided on the state at frame START: if TickAppState transitions
            // Loading -> Playing this frame (resetting the sim clock), we must NOT also advance
            // the sim with this frame's (post-load, clamped-but-large) delta -- that's the
            // multi-tick lurch SetAppState's clock reset exists to prevent. The gameplay camera,
            // by contrast, is picked up immediately (the scene is already loaded by then).
            const bool wasPlaying = (rs.appState == AppState::Playing);
            TickAppState(rs);

            SimTickParams sim;
            sim.advanceSim = wasPlaying;
            sim.stepOnce = false;
            sim.cameraFromScene = (rs.appState == AppState::Playing);
            sim.suppressTemporalHistory = false;
            TickRuntime(rs, frameTime, sim);

            RenderScene(rs);
            rs.renderer.EndFrame();
        }
    }

    void ShutdownRuntime(RuntimeState &rs) {
        Input::Shutdown();
        rs.physicsWorld.Shutdown();
        rs.renderer.Shutdown(); // ShutdownUI inside is a no-op: InitUI was never called
    }

} // namespace toon
