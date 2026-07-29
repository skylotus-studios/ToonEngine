#pragma once
//============================================================================
//  app/sim_runtime.h: the headless simulation driver (ToonPlayer --sim-only).
//
//  Runs the fixed-timestep simulation -- scripts, physics, contact events, animation, level
//  transitions -- with NO graphics device, NO window, and NO audio device, as fast as the CPU
//  will go. Two uses: a smoke/regression harness that exercises a scene for thousands of ticks
//  in a fraction of a second, and the seam a dedicated server would need, since a server has
//  exactly the same "simulate without presenting" requirement.
//
//  It is a sibling of app/runtime_init.h's RunRuntimeLoop, not a variant of it: that loop is
//  window-bound at every step (glfwWindowShouldClose, glfwPollEvents, RenderScene, Present), so
//  sharing it would mean threading a flag through all of them. The genuinely shared part -- the
//  sim itself -- is already factored out as TickRuntime (app/runtime_tick.h) and IS reused here
//  unchanged, which is the whole reason this file is short.
//
//  What it does NOT do is merely avoid touching the GPU: it verifies, every tick, that no
//  device and no window exist (see RunSimOnly's contract), so the mode fails loudly if a future
//  change quietly reintroduces one.
//============================================================================
#include <string>

namespace toon {

    // Everything --sim-only takes off the command line (see app/player_main.cpp).
    struct SimOptions {
        // The scene to simulate. Used as given if it names an existing file; otherwise resolved
        // against the scenes/ asset dir, so both a path and a bare filename work.
        std::string scenePath;
        // Recorded, printed, and used to seed std::srand. NOTHING in the engine consumes
        // randomness today (there is no rand/mt19937/random_device call anywhere under src/), so
        // this is a forward-compatibility hook and a run label -- NOT a reproducibility
        // guarantee. Jolt also runs its solver across hardware_concurrency()-1 worker threads
        // (core/physics/physics.cpp), which its own docs say must be pinned before run-to-run
        // determinism can be claimed at all.
        unsigned long long seed = 0;
        // How many fixed sim ticks to run before exiting. At kFixedDt (60 Hz) 3600 ticks is one
        // minute of simulated time.
        int ticks = 3600;
        // 0 (default) = never hash: no output, no cost -- purely additive. N > 0 prints one
        // `HASH tick=<n> value=<hex>` line (see app/world_hash.h) every N ticks while the scene
        // is actually Playing, so two runs of the same scene+seed can be diffed line-for-line to
        // prove they simulated identically (or to catch the first tick where they didn't).
        int hashEvery = 0;
        // Where app/metrics.h's WriteMetrics writes its structured per-run summary. Always
        // written (unlike hashEvery, this isn't opt-in) -- see app/metrics.h's own banner.
        std::string metricsOutPath = "artifacts/metrics.json";
    };

    // Stand up the engine's simulation half, run `opts.ticks` fixed steps, report the rate, and
    // tear down. Returns a process exit code: 0 on success, 1 if the scene failed to load, 2 if
    // the no-device/no-window invariant was violated. Owns the whole lifecycle -- the caller has
    // only to have run Assets::Init() first.
    int RunSimOnly(const SimOptions &opts);

} // namespace toon
