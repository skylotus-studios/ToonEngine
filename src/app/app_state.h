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
        // Set by the drain if any item failed to load. Before roadmap #19 the loader's return
        // value was discarded outright, so a missing or malformed scene was indistinguishable
        // from a good one and the game dropped into an empty world; now Loading falls back to
        // the title screen instead.
        bool failed = false;
    };

    // The one transition funnel: every AppState change goes through here, so a state can never
    // be entered without its enter/exit side effects (e.g. Loading seeds the LoadJob; entering
    // Playing from Loading resets the sim clock). Nothing assigns RuntimeState::appState
    // directly. See app/app_state.cpp.
    void SetAppState(RuntimeState &rs, AppState next);

    // Per-frame AppState logic, called by the runtime loop after TickRuntime: Loading drains its
    // queue, Playing/Paused watch for the pause/resume keypress. The Title/Pause menus themselves
    // are drawn + driven by the in-game UI (roadmap #17, app/runtime_ui.cpp). See app/app_state.cpp.
    void TickAppState(RuntimeState &rs);

    // New Game / Continue intents, shared so a menu button (app/runtime_ui.cpp) and any keyboard
    // shortcut funnel through the same transition. BeginNewGame resets progress and enters Loading
    // with the default scene; BeginContinue loads the quicksave (falling back to a fresh start if it
    // can't be read) and enters Loading.
    void BeginNewGame(RuntimeState &rs);
    void BeginContinue(RuntimeState &rs);

} // namespace toon
