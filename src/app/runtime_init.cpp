//============================================================================
//  app/runtime_init.cpp: see runtime_init.h.
//============================================================================
#include "app/runtime_init.h"

#include "app/app_state.h"
#include "app/runtime_render.h"
#include "app/runtime_tick.h"
#include "app/session.h" // TickSceneTransition + EndSession (roadmap #19)
#include "core/scene/scripts/builtin_scripts.h" // RegisterBuiltinScripts
#include "app/runtime_ui.h" // RenderHUD (roadmap #17: the in-game HUD/menu)
#include "core/input/input_system.h"
#include "core/platform/clock.h" // Clock::Now (seeds the sim clock; see clock.h)
#include "core/platform/paths.h" // Assets::Icon (exe-relative asset paths)

#include <GLFW/glfw3.h>

#include <cstdint>
#include <cstdio>

namespace toon {

    bool InitRuntime(RuntimeState &rs, GLFWwindow *window, const char *scenePath, bool strictValidation) {
        // Before anything can load a scene: the name -> factory registry has to know the script
        // types this build ships with, and nothing else populates it (see builtin_scripts.h --
        // the self-registering statics this replaced were being dropped by the linker, which is
        // why a shipped player silently ran no scripts at all).
        RegisterBuiltinScripts();

        // Window-backed bring-up: both presentation axes are on. The sim-only path
        // (app/sim_runtime.h) is the one that clears them, and never comes through here.
        rs.mode = RuntimeMode{};

        rs.window = window;
        SetWindowIcon(window, Assets::Icon().c_str());

        if (!rs.renderer.Init(window, strictValidation)) {
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

        rs.lastTime = Clock::Now();
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

            // Level transitions (roadmap #19): the one point in the frame where the scene vector
            // may be replaced -- outside the fixed-step loop and outside every script, physics,
            // and audio callback. Runs before TickRuntime so this frame's post params carry the
            // fade level it just advanced.
            TickSceneTransition(rs, frameTime);

            SimTickParams sim;
            sim.advanceSim = wasPlaying && !SceneTransitionBlocksSim(rs.transition);
            sim.stepOnce = false;
            sim.cameraFromScene = (rs.appState == AppState::Playing);
            sim.suppressTemporalHistory = SceneTransitionSuppressesHistory(rs.transition);
            TickRuntime(rs, frameTime, sim);

            RenderScene(rs);
            RenderHUD(rs, ScreenForAppState(rs.appState)); // in-game HUD/menus (roadmap #17)
            rs.renderer.EndFrame();
        }
    }

    void ShutdownRuntime(RuntimeState &rs) {
        // Close out the live session first, so a script's OnDestroy still runs on a normal quit
        // (the hook's contract is "fires once when the session ends", and exiting the game ends
        // it) and so the bodies/sounds are released in dependency order rather than by whatever
        // order the destructors below happen to run in.
        EndSession(rs);
        Input::Shutdown();
        // AudioEngine::Shutdown's own header comment says "Shutdown once at exit" -- this call
        // was simply missing. EndSession's StopAll() above only stops/releases individual
        // handled sounds; it leaves miniaudio's ma_engine (and the realtime audio callback
        // thread that owns, per audio.cpp's own banner) running. Without this, that background
        // thread keeps touching AudioEngine::Impl's memory while ~RuntimeState's member
        // destructors free it out from under it moments later -- a use-after-free race that
        // reproduced as a reliable access-violation crash on every clean exit (editor, player,
        // and --headless-render alike), isolated during this session's own testing by bisecting
        // down to exactly this gap. The same fix is needed in main.cpp's separate editor
        // teardown block, which has the identical gap.
        rs.audio.Shutdown();
        rs.physicsWorld.Shutdown();
        rs.renderer.Shutdown(); // ShutdownUI inside is a no-op: InitUI was never called
    }

} // namespace toon
