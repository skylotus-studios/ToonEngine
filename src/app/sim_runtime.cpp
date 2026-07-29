//============================================================================
//  app/sim_runtime.cpp: see sim_runtime.h.
//============================================================================
#include "app/sim_runtime.h"

#include "app/app_state.h"
#include "app/metrics.h"                         // WriteMetrics (artifacts/metrics.json)
#include "app/runtime_state.h"
#include "app/runtime_tick.h"                    // TickRuntime + kFixedDt (the shared sim step)
#include "app/session.h"                         // TickSceneTransition / EndSession + the sim gates
#include "app/world_hash.h"                      // HashWorldState (--hash-every)
#include "core/platform/clock.h"                 // Clock::Now (the ticks/second measurement)
#include "core/platform/paths.h"                 // Assets::Scene (bare-filename scene resolution)
#include "core/scene/scripts/builtin_scripts.h"  // RegisterBuiltinScripts

#include <cstdio>
#include <cstdlib>    // std::srand -- see SimOptions::seed for what that is and isn't worth
#include <filesystem> // scene-path resolution (path-as-given vs. asset-dir filename)

namespace toon {
    namespace {

        // Resolve `given` to a readable scene file: used as-is when it already names one (so a
        // path relative to the working directory, or an absolute one, both work), otherwise
        // re-resolved by FILENAME against the scenes/ asset dir -- which is what makes
        // `--scene assets/scenes/smoke.scene` work from a build directory too, since only the
        // leaf name survives that fallback. Empty if neither exists.
        std::string ResolveScenePath(const std::string &given) {
            std::error_code ec; // exists(): report by return value, never throw
            if (!given.empty() && std::filesystem::exists(given, ec)) { return given; }

            const std::string byName = Assets::Scene(std::filesystem::path(given).filename().string());
            if (std::filesystem::exists(byName, ec)) { return byName; }
            return {};
        }

        // The invariant this whole mode exists to hold, checked against REALITY rather than
        // against the intent recorded in rs.mode: no window handle, and no graphics device. It
        // runs every tick because "we simply never called Init" is not a guarantee -- any future
        // code reached from the sim path could create one, and silently rendering in a headless
        // smoke test would make the mode worthless without ever failing. Two pointer comparisons.
        bool VerifyHeadless(const RuntimeState &rs, int tick) {
            if (rs.window == nullptr && !rs.renderer.HasDevice()) { return true; }
            std::fprintf(stderr,
                         "--sim-only: headless invariant violated at tick %d: window=%s device=%s. "
                         "Something in the sim path created a presentation resource.\n",
                         tick, rs.window ? "created" : "null", rs.renderer.HasDevice() ? "created" : "none");
            return false;
        }

        // Warn once if this scene animates. Without a device no model is loaded, so every clip's
        // duration reads 0 (see Renderer::GetModelAnimationDuration) and app/runtime_tick.cpp
        // treats 0 as "don't wrap or clamp": the animation tick still runs and still advances
        // each clip's time, but a clip never loops and never stops at its end. Said out loud
        // rather than left for someone to discover from a wrong number.
        void WarnIfAnimatedWithoutDevice(const RuntimeState &rs) {
            for (const Entity &e : rs.scene.entities) {
                if (!e.animation) { continue; }
                std::fprintf(stderr,
                             "--sim-only: this scene has animated entities, but clip durations live in the "
                             "loaded model and no model loads without a device -- clip time will advance "
                             "without ever looping or clamping.\n");
                return;
            }
        }

        // How many live (non-null-instance) scripts this scene currently carries. A PROXY input
        // to metrics.scripts.ran (app/metrics.h), not an instrumented call counter: the caller
        // multiplies this by "was this a tick where UpdateScripts actually ran," mirroring
        // TickRuntime's own runScripts/advanceSim gate (runtime_tick.cpp) instead of adding a
        // counter inside core/scene/script.cpp's UpdateScripts itself -- that file is shared by
        // the editor and the player, and a metrics-only need didn't seem worth touching it for.
        // Trade-off: if that gate's logic ever changes, this proxy has to be updated to match, or
        // it silently drifts from what actually ran.
        uint64_t LiveScriptCount(const RuntimeState &rs) {
            uint64_t count = 0;
            for (const Entity &e : rs.scene.entities) {
                for (const ScriptComponent &sc : e.scripts) {
                    if (sc.instance) { ++count; }
                }
            }
            return count;
        }

    } // namespace

    int RunSimOnly(const SimOptions &opts) {
        // Seeded for whatever later consumes randomness; nothing does today (SimOptions::seed).
        std::srand(static_cast<unsigned int>(opts.seed));

        // The scene loader reconstructs scripts through the name registry, and nothing else
        // populates it -- same first-thing-in-bring-up call InitRuntime makes.
        RegisterBuiltinScripts();

        const std::string scenePath = ResolveScenePath(opts.scenePath);
        if (scenePath.empty()) {
            std::fprintf(stderr, "--sim-only: no scene file found for '%s'\n", opts.scenePath.c_str());
            return 1;
        }

        RuntimeState rs;
        // Declare both presentation axes off, then never call Renderer::Init, AudioEngine::Init,
        // Input::Init, or glfwInit at all. Audio needs no stub of its own: with Init never run,
        // every AudioEngine method early-outs on its `initialized` flag or on an empty handle
        // map, so BuildAudioWorld's Play calls return SoundHandle::Invalid and the listener/
        // emitter updates in TickRuntime are no-ops.
        rs.mode.window = false;
        rs.mode.renderDevice = false;

        if (!rs.physicsWorld.Init()) {
            std::fprintf(stderr, "--sim-only: PhysicsWorld init failed\n");
            return 1;
        }

        // Straight to Loading, bypassing Title: the title menu's New Game button is what calls
        // BeginNewGame in a windowed run (app/runtime_ui.cpp), and that button only exists inside
        // the UI build -- which needs both a window and a device. Calling the same intent
        // directly is the seam a dedicated server would use for exactly the same reason.
        rs.pendingScenePath = scenePath;
        BeginNewGame(rs);

        std::printf("--sim-only: scene=%s seed=%llu ticks=%d dt=%.6f\n", scenePath.c_str(), opts.seed, opts.ticks,
                    kFixedDt);
        // stdout is block-buffered when redirected to a file/pipe (a CI log), stderr never is, so
        // without this the warnings below would surface ABOVE this header in a captured run.
        std::fflush(stdout);

        int ticksRun = 0;
        bool animationChecked = false;
        const double startTime = Clock::Now();

        MetricsInputs metrics;
        metrics.tickMs.reserve(static_cast<size_t>(opts.ticks));

        for (int tick = 0; tick < opts.ticks; ++tick) {
            if (!VerifyHeadless(rs, tick)) { return 2; }

            // Same frame shape as RunRuntimeLoop, minus everything presentational. advanceSim is
            // decided on the state at tick START for the same reason the player does it: the tick
            // that transitions Loading -> Playing must not also advance the sim. (The clock-debt
            // hazard behind that rule can't arise here -- every tick is exactly kFixedDt -- but a
            // regression harness should tick the way the thing it guards ticks.)
            const bool wasPlaying = (rs.appState == AppState::Playing);
            TickAppState(rs);

            // Loading falls back to Title when the scene fails to load (app/app_state.cpp). In a
            // windowed run that shows the title menu; here there is no menu and nothing further
            // would ever happen, so treat it as the failure it is instead of spinning.
            if (rs.appState == AppState::Title) {
                std::fprintf(stderr, "--sim-only: scene failed to load, nothing to simulate: %s\n",
                             scenePath.c_str());
                return 1;
            }

            TickSceneTransition(rs, kFixedDt);

            SimTickParams sim;
            sim.advanceSim = wasPlaying && !SceneTransitionBlocksSim(rs.transition);
            sim.stepOnce = false;
            sim.cameraFromScene = (rs.appState == AppState::Playing);
            sim.suppressTemporalHistory = SceneTransitionSuppressesHistory(rs.transition);

            const double tickStart = Clock::Now();
            TickRuntime(rs, kFixedDt, sim);
            metrics.tickMs.push_back((Clock::Now() - tickStart) * 1000.0);
            ++ticksRun;

            // scripts.ran (app/metrics.h): mirrors UpdateScripts' own gate inside TickRuntime
            // (runtime_tick.cpp) -- exactly one fixed step runs per tick here (frameTime is
            // always exactly kFixedDt), so "scripts ran this tick" reduces to this same
            // advanceSim/runScripts pair. See LiveScriptCount's own comment for the trade-off.
            if (sim.advanceSim && rs.runScripts) { metrics.scriptsRan += LiveScriptCount(rs); }

            // Once the first scene is actually up (not before -- Loading has no entities yet).
            if (!animationChecked && rs.appState == AppState::Playing) {
                animationChecked = true;
                WarnIfAnimatedWithoutDevice(rs);
            }

            // --hash-every: same Playing gate as the animation warning above -- hashing during
            // Loading would just fingerprint an empty scene every one of those ticks. Streamed
            // as its own grep-able line so two runs' streams diff cleanly:
            // diff <(run1 | grep '^HASH ') <(run2 | grep '^HASH '). Also recorded into
            // metrics.sim.state_hash so a single artifacts/metrics.json carries the same samples.
            if (opts.hashEvery > 0 && rs.appState == AppState::Playing && tick % opts.hashEvery == 0) {
                const uint64_t hash = HashWorldState(rs, opts.seed);
                std::printf("HASH tick=%d value=%016llx\n", tick, static_cast<unsigned long long>(hash));
                metrics.hashSamples.emplace_back(tick, hash);
            }

            if (rs.appState == AppState::Quit) { break; }
        }

        const double elapsed = Clock::Now() - startTime;
        const double ticksPerSecond = elapsed > 0.0 ? ticksRun / elapsed : 0.0;
        std::printf("--sim-only: %d ticks in %.4f s -- %.0f ticks/second (%.1f s simulated)\n", ticksRun, elapsed,
                    ticksPerSecond, ticksRun * kFixedDt);

        // Written while rs's physics/audio/asset state still reflects the run that just
        // happened, BEFORE EndSession/Shutdown tear any of it down (see app/metrics.h's own
        // contract on WriteMetrics).
        MetricsOptions mopts;
        mopts.outPath = opts.metricsOutPath;
        WriteMetrics(rs, metrics, mopts);

        // Mirrors ShutdownRuntime's order: end the session first so every script's OnDestroy runs
        // while the bodies it might touch are still alive. The Renderer needs no Shutdown call --
        // it was never initialized, and its destructor covers it either way.
        EndSession(rs);
        rs.physicsWorld.Shutdown();
        return 0;
    }

} // namespace toon
