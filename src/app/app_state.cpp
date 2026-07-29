//============================================================================
//  app/app_state.cpp: see app_state.h.
//
//  The application state machine, used by the player (the editor drives the sim through its
//  own EditorMode instead and never calls these). SetAppState is the one funnel; TickAppState
//  is the per-frame per-state logic. Both switches are exhaustive with no default arm, so
//  -Wswitch flags every site the day a new AppState is added.
//============================================================================
#include "app/app_state.h"

#include "app/runtime_state.h"
#include "app/save_glue.h"          // QuickSave/QuickLoad + kQuickSaveSlot (roadmap #18)
#include "app/session.h"            // BeginSession/EndSession + BeginFadeIn (roadmap #19)
#include "core/input/input_system.h"
#include "core/platform/clock.h"    // Clock::Now (sim-clock reset on entering Playing)
#include "core/save/savegame.h"     // SaveExists (title "Continue" affordance)
#include "core/scene/serializer.h"  // LoadScene

namespace toon {
    namespace {

        // Loading drains at most this many assets per frame, rendering between chunks so the
        // window stays responsive. One today (a single scene load); roadmap #19 grows the queue.
        constexpr std::size_t kLoadItemsPerFrame = 1;

        void DrainLoadJob(RuntimeState &rs) {
            LoadJob &job = rs.loadJob;
            for (std::size_t n = 0; n < kLoadItemsPerFrame && job.done < job.queue.size(); ++n) {
                const PendingAsset &asset = job.queue[job.done];
                if (!asset.scenePath.empty()) {
                    // The core loader (not the editor's LoadSceneInto wrapper): fills the scene +
                    // a fallback camera. The gameplay camera (Part 7's GetActiveCamera) overrides
                    // the view per frame once Playing, if the scene has a primary CameraComponent.
                    if (!LoadScene(asset.scenePath.c_str(), rs.scene, rs.camera, rs.renderer)) {
                        job.failed = true; // Loading falls back to the title rather than to nothing
                    }
                }
                ++job.done;
            }
        }

    } // namespace

    void SetAppState(RuntimeState &rs, AppState next) {
        const AppState prev = rs.appState;
        if (prev == next) { return; }

        // Exit side effect: leaving Paused unfreezes audio (the enter side effect froze it).
        if (prev == AppState::Paused) { rs.audio.ResumeAll(); }

        rs.appState = next;

        // Enter side effects. No default arm: adding an AppState must be handled here explicitly.
        switch (next) {
            case AppState::Boot:
                break;
            case AppState::Title:
                break;
            case AppState::Loading:
                // Whatever session was live is over: release its scripts, sounds, and bodies in
                // dependency order before the scene under them is replaced (roadmap #19). A no-op
                // on the first boot, where there's nothing loaded yet.
                EndSession(rs);
                // Seed the work list with the scene to bring up. Today one item; the drain loop
                // and progress denominator already handle an N-item queue for roadmap #19.
                rs.loadJob = LoadJob{};
                rs.loadJob.queue.push_back(PendingAsset{rs.pendingScenePath});
                rs.loadJob.total = rs.loadJob.queue.size();
                rs.loadJob.done = 0;
                break;
            case AppState::Playing:
                // Reset the sim clock so a long load or a paused stretch (either can span many
                // wall-clock seconds) doesn't dump time debt into the first frame's accumulator
                // and fire a burst of catch-up ticks -- a visible physics lurch. The 0.25s
                // frameTime clamp in RuntimeBeginFrame guards a single stall; this guards the
                // transition itself, which the clamp alone wouldn't (0.25s is still 15 ticks).
                rs.lastTime = Clock::Now();
                rs.accumulator = 0.0;
                break;
            case AppState::Paused:
                rs.resumeTo = prev; // remember where we paused from (normally Playing)
                rs.audio.PauseAll();
                // Autosave (roadmap #18): pausing is the natural checkpoint, and it only ever
                // happens in the player (the editor pins Playing and never calls SetAppState), so
                // this never fires from the editor. A failed write just logs; it doesn't block the
                // pause.
                QuickSave(rs);
                break;
            case AppState::Quit:
                break;
        }
    }

    void BeginNewGame(RuntimeState &rs) {
        rs.playtimeSeconds = 0.0f; // fresh progress; keep the default scene
        SetAppState(rs, AppState::Loading);
    }

    void BeginContinue(RuntimeState &rs) {
        // Point the loader at the saved scene + restore playtime; fall back to a fresh start if the
        // save turned out unreadable (corrupt/newer) rather than stalling at the title.
        if (!QuickLoad(rs)) { rs.playtimeSeconds = 0.0f; }
        SetAppState(rs, AppState::Loading);
    }

    void TickAppState(RuntimeState &rs) {
        // No default arm: every AppState decides its own per-frame behavior explicitly.
        switch (rs.appState) {
            case AppState::Boot:
                // Nothing to show during init today (InitRuntime already finished before the
                // loop). Boot is just the initial value; advance straight to the title.
                SetAppState(rs, AppState::Title);
                break;
            case AppState::Title:
                // The title MENU (New Game / Continue / Quit) is drawn + driven by the in-game UI
                // (roadmap #17, app/runtime_ui.cpp's RenderHUD, called each frame right after this):
                // it routes New Game / Continue through BeginNewGame / BeginContinue and Quit through
                // SetAppState, navigable by mouse, keyboard arrows + Enter, and gamepad. Nothing to
                // poll here now.
                break;
            case AppState::Loading:
                DrainLoadJob(rs);
                if (rs.loadJob.done >= rs.loadJob.total) {
                    if (rs.loadJob.failed) {
                        // Nothing usable came up. Back to the title rather than into an empty
                        // world the player can neither play nor escape.
                        SetAppState(rs, AppState::Title);
                    } else {
                        // Build the world the player half never built before roadmap #19: fire
                        // OnCreate, seed the Jolt bodies, start the autoplay emitters. Runs
                        // BEFORE the Playing transition so its sim-clock reset covers this work
                        // too, not just the file load above.
                        BeginSession(rs);
                        BeginFadeIn(rs.transition); // appear rather than cut in hard
                        SetAppState(rs, AppState::Playing);
                    }
                }
                break;
            case AppState::Playing:
                // F5 quick-saves (roadmap #18): a manual checkpoint on top of the autosave that
                // fires on pause. Independent of the Escape->pause below (both can be pressed the
                // same frame without conflict).
                if (Input::WasKeyPressed(Input::Key::F5)) { QuickSave(rs); }
                // Escape pauses (which also autosaves; see SetAppState). No pause menu to draw
                // until roadmap #17, so Paused is just a frozen sim for now.
                if (Input::WasKeyPressed(Input::Key::Escape)) { SetAppState(rs, AppState::Paused); }
                break;
            case AppState::Paused:
                // Escape resumes (returns to whatever we paused from).
                if (Input::WasKeyPressed(Input::Key::Escape)) { SetAppState(rs, rs.resumeTo); }
                break;
            case AppState::Quit:
                break;
        }
    }

} // namespace toon
