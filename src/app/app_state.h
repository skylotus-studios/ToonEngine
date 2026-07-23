#pragma once
//============================================================================
//  app/app_state.h: the application lifecycle state machine (roadmap #15).
//
//  A flat enum + a single transition funnel (SetAppState), not a state stack or virtual
//  State classes: the case set is small, fixed, and compile-time known, so a switch with no
//  default arm lets -Wswitch enumerate every obligation when the enum later grows (see
//  docs/cpp-style-guide.md §7). Deliberately a DIFFERENT axis from EditorState::EditorMode:
//  AppState is what the application is showing/doing; EditorMode is whether the editor's sim
//  is ticking. The player has only AppState; the editor has both, AppState pinned to Playing.
//============================================================================
#include <cstddef>
#include <string>
#include <vector>

namespace toon {

    struct RuntimeState; // app/runtime_state.h (holds the current AppState + the LoadJob)

    enum class AppState { Boot, Title, Loading, Playing, Paused, Quit };

    // One unit of loading work. Today a scene load is the whole queue's single entry; roadmap
    // #19 (level transitions) grows this into per-asset chunks that share this same structure.
    struct PendingAsset {
        std::string scenePath;
    };

    // A drainable loading work list (roadmap #15). AppState::Loading drains up to a few items
    // per frame and renders between chunks, so the window stays responsive and Quit stays
    // honorable (glfwWindowShouldClose is still polled every chunk) instead of blocking on a
    // synchronous load. Threading the producer is roadmap #19's concern; the queue survives.
    struct LoadJob {
        std::vector<PendingAsset> queue;
        std::size_t done = 0;
        std::size_t total = 0;
    };

    // The one transition funnel: every AppState change goes through here, so a state can never
    // be entered without its enter/exit side effects (e.g. Loading seeds the LoadJob; entering
    // Playing from Loading resets the sim clock). Nothing assigns RuntimeState::appState
    // directly. See app/app_state.cpp.
    void SetAppState(RuntimeState &rs, AppState next);

    // Per-frame AppState logic, called by the runtime loop after TickRuntime: Title waits for a
    // keypress, Loading drains its queue, Paused watches for resume. See app/app_state.cpp.
    void TickAppState(RuntimeState &rs);

} // namespace toon
