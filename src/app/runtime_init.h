#pragma once
//============================================================================
//  app/runtime_init.h: bring-up, teardown, and the main loop for the standalone runtime.
//
//  The player's counterpart to app/editor_init.h + main.cpp's editor loop, minus everything
//  editor: no ImGui (InitUI is never called), no fonts/themes/docking, no demo scene. Takes a
//  RuntimeState&, so ToonPlayer links this and nothing from ui/panels/. The editor's --play dev
//  flag (main.cpp) drives the exact same three calls.
//============================================================================
#include "app/runtime_state.h"

struct GLFWwindow;

namespace toon {

    // Stand up the engine on an already-created window: renderer + physics + audio + input, a
    // framebuffer-resize hook, and the scene to bring up (recorded as pendingScenePath; the
    // Loading state actually loads it). Does NOT touch ImGui. Returns false on a fatal failure
    // (renderer init), having logged it. appState is left at Boot for the loop to advance.
    bool InitRuntime(RuntimeState &rs, GLFWwindow *window, const char *scenePath);

    // Drive frames until the app requests Quit or the window is closed. One frame is:
    // RuntimeBeginFrame -> TickAppState -> TickRuntime -> RenderScene -> EndFrame. Advancing the
    // sim is gated on the state at frame start (so entering Playing after a load doesn't dump a
    // frame's worth of catch-up ticks), while the gameplay camera is picked up immediately.
    void RunRuntimeLoop(RuntimeState &rs);

    // Tear down in the reverse of InitRuntime. Renderer::Shutdown's ShutdownUI is a no-op here
    // (InitUI was never called), so this is clean even though the runtime never had a UI.
    void ShutdownRuntime(RuntimeState &rs);

} // namespace toon
