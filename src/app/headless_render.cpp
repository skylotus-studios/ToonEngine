//============================================================================
//  app/headless_render.cpp: see headless_render.h.
//============================================================================
#include "app/headless_render.h"

#include "app/app_state.h"
#include "app/metrics.h"       // WriteMetrics (artifacts/metrics.json)
#include "app/runtime_init.h"  // InitRuntime / ShutdownRuntime
#include "app/runtime_render.h" // RenderScene
#include "app/runtime_state.h"
#include "app/runtime_tick.h" // TickRuntime + kFixedDt (the shared sim step)
#include "app/runtime_ui.h"   // RenderHUD + ScreenForAppState
#include "app/session.h"      // TickSceneTransition / EndSession + the sim gates
#include "core/platform/clock.h" // Clock::Now (frame timing)
#include "core/platform/paths.h" // Assets::Scene (bare-filename scene resolution)
#include "core/scene/scripts/builtin_scripts.h" // RegisterBuiltinScripts

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstdio>
#include <filesystem> // scene-path resolution (path-as-given vs. asset-dir filename)

namespace toon {
    namespace {

        // Same path resolution as app/sim_runtime.cpp's ResolveScenePath -- kept as its own
        // small copy rather than a shared helper: two three-line functions didn't seem worth a
        // new file, and each mode's error message differs slightly in ownership (--sim-only vs
        // --headless-render).
        std::string ResolveScenePath(const std::string &given) {
            std::error_code ec;
            if (!given.empty() && std::filesystem::exists(given, ec)) { return given; }
            const std::string byName = Assets::Scene(std::filesystem::path(given).filename().string());
            if (std::filesystem::exists(byName, ec)) { return byName; }
            return {};
        }

        // frame_<NNNN>.png, zero-padded to 4 digits -- generous for the frame counts this mode
        // is meant for (a --frames value past 9999 would just widen the number, not break
        // anything).
        std::string CapturePath(const std::string &dir, int frame) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "frame_%04d.png", frame);
            return dir + "/" + buf;
        }

    } // namespace

    int RunHeadlessRender(const HeadlessRenderOptions &opts) {
        RegisterBuiltinScripts();

        const std::string scenePath = ResolveScenePath(opts.scenePath);
        if (scenePath.empty()) {
            std::fprintf(stderr, "--headless-render: no scene file found for '%s'\n", opts.scenePath.c_str());
            return 1;
        }

        // Requested capture frames past the frame budget can't ever fire -- said once, up front,
        // rather than discovered by a missing file after the run.
        for (int frame : opts.captureFrames) {
            if (frame < 0 || frame >= opts.frames) {
                std::fprintf(stderr, "--headless-render: capture frame %d is outside [0, %d), skipping\n", frame,
                             opts.frames);
            }
        }

        if (!glfwInit()) {
            std::fprintf(stderr, "--headless-render: GLFW init failed\n");
            return 1;
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // Vulkan, not GL -- same as the normal player
        // The one visible difference from player_main.cpp's window creation: hidden, not shown.
        // Everything else about this window -- size, backend hint -- matches the normal player's,
        // per the task's own instruction to use a real (if invisible) window rather than
        // windowless Vulkan.
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

        GLFWwindow *window = glfwCreateWindow(1600, 900, "ToonEngine (headless-render)", nullptr, nullptr);
        if (!window) {
            std::fprintf(stderr, "--headless-render: failed to create the hidden window\n");
            glfwTerminate();
            return 1;
        }

        // RuntimeState (and therefore Renderer, PhysicsWorld, AudioEngine) is scoped to END
        // before glfwDestroyWindow/glfwTerminate below, not after: RuntimeState's destructor
        // runs ~Renderer(), which calls Shutdown() a SECOND time (ShutdownRuntime already calls
        // it once, explicitly, inside this block) -- Diligent's own teardown needs the GLFW/
        // Vulkan loader state glfwTerminate() tears down to still be intact for that second,
        // implicit call to be safe. Letting `rs` merely go out of scope at the bottom of the
        // function (i.e. AFTER glfwTerminate(), the same shape player_main.cpp's normal path
        // already uses) reproduced a reliable access-violation crash during that second Shutdown
        // call in this session's own testing -- isolated by bisection to be unrelated to any of
        // this mode's own logic (validation, draw/PSO counting, capture, RenderHUD all ruled
        // out; reproduces even with zero real draw calls). See this task's impl-notes for the
        // full isolation trail; player_main.cpp's own ordering carries the same latent risk and
        // was left unchanged here as an out-of-scope, separately-flagged finding.
        int exitCode = 0;
        {
            RuntimeState rs;
            // strictValidation=true: forces VALIDATION_LEVEL_2 and installs the counting debug-
            // message callback (see Renderer::Init's own comment, core/rendering/renderer.h).
            // Both rs.window and Renderer::HasDevice are real here -- see runtime_state.h's
            // RuntimeMode comment for why this mode's RuntimeMode is {true, true}, not
            // {false, true}.
            if (!InitRuntime(rs, window, scenePath.c_str(), /*strictValidation=*/true)) {
                glfwDestroyWindow(window);
                glfwTerminate();
                return 1;
            }

            rs.pendingScenePath = scenePath;
            BeginNewGame(rs); // same Title-bypass --sim-only uses; see app/sim_runtime.cpp's own comment

            // --post off (scripts/run_golden_tests.py's golden-image mode): zero every
            // stochastic/temporal PostFX effect so two runs of the same scene produce
            // bit-for-bit identical captures. Applied AFTER BeginNewGame (which may load a
            // scene, but never touches rs.post -- PostParams isn't scene data) and BEFORE the
            // frame loop, so every rendered frame -- including warmup frames before the capture
            // index -- sees the same params.
            if (opts.postOff) {
                rs.post.bloom = false;
                rs.post.ssao = false;
                rs.post.dof = false;
                rs.post.ssr = false;
                rs.post.taa = false;
            }

            std::printf("--headless-render: scene=%s frames=%d captures=%zu\n", scenePath.c_str(), opts.frames,
                        opts.captureFrames.size());
            std::fflush(stdout);

            MetricsInputs metrics;
            metrics.hasRenderData = true;
            metrics.frameMs.reserve(static_cast<size_t>(opts.frames));

            int framesRun = 0;

            for (int frame = 0; frame < opts.frames; ++frame) {
                // RuntimeBeginFrame's SIDE EFFECTS matter here (Input::BeginFrame, glfwPollEvents
                // -- OS message-pump hygiene for the hidden window), but its RETURNED wall-clock
                // delta is deliberately discarded: driving the sim off real elapsed time would
                // make a captured frame's content depend on how fast THIS machine happened to
                // render the frames before it, which defeats the entire point of capturing
                // frame_0060.png etc. for reproducible image-diff regression testing. kFixedDt
                // below is the same "every tick is exactly one 60 Hz step" determinism
                // --sim-only already relies on, just with real rendering layered on top instead
                // of nothing.
                RuntimeBeginFrame(rs);

                const bool wasPlaying = (rs.appState == AppState::Playing);
                TickAppState(rs);

                // Same fail-fast as --sim-only: a real player shows the title menu and waits;
                // this mode has nothing to render there meaningfully and nothing would ever
                // change it.
                if (rs.appState == AppState::Title) {
                    std::fprintf(stderr, "--headless-render: scene failed to load, nothing to render: %s\n",
                                 scenePath.c_str());
                    exitCode = 1;
                    break;
                }

                TickSceneTransition(rs, kFixedDt);

                SimTickParams sim;
                sim.advanceSim = wasPlaying && !SceneTransitionBlocksSim(rs.transition);
                sim.stepOnce = false;
                sim.cameraFromScene = (rs.appState == AppState::Playing);
                sim.suppressTemporalHistory = SceneTransitionSuppressesHistory(rs.transition);
                TickRuntime(rs, kFixedDt, sim);

                const double renderStart = Clock::Now();
                RenderScene(rs);
                RenderHUD(rs, ScreenForAppState(rs.appState));

                // ui.boxes_live/pruned (app/metrics.h): overwritten every frame, so after the
                // loop these hold the LAST frame's numbers -- same "last frame's snapshot, not a
                // percentile series" convention DrawCallCount/PSOSwitchCount below use.
                metrics.uiBoxesLive = rs.ui.boxesLive;
                metrics.uiBoxesPruned = rs.ui.boxesPruned;

                // Capture happens AFTER RenderHUD/DrawUI and BEFORE EndFrame -- see
                // Renderer::CaptureFrameToPNG's own comment on why Present() would make this too
                // late.
                if (std::find(opts.captureFrames.begin(), opts.captureFrames.end(), frame) !=
                    opts.captureFrames.end()) {
                    const std::string path = CapturePath(opts.captureDir, frame);
                    if (!rs.renderer.CaptureFrameToPNG(path.c_str())) {
                        std::fprintf(stderr, "--headless-render: failed to capture frame %d to '%s'\n", frame,
                                     path.c_str());
                    }
                }

                metrics.drawCalls = rs.renderer.DrawCallCount();
                metrics.psoSwitches = rs.renderer.PSOSwitchCount();

                rs.renderer.EndFrame();
                metrics.frameMs.push_back((Clock::Now() - renderStart) * 1000.0);
                ++framesRun;

                if (rs.appState == AppState::Quit) { break; }
            }

            std::printf("--headless-render: %d frames rendered\n", framesRun);

            if (exitCode == 0) {
                // Cumulative over the whole run -- not per-frame, unlike drawCalls/psoSwitches above.
                metrics.validationErrors = rs.renderer.ValidationErrorCount();
                metrics.validationWarnings = rs.renderer.ValidationWarningCount();

                MetricsOptions mopts;
                mopts.outPath = opts.metricsOutPath;
                WriteMetrics(rs, metrics, mopts);

                // Checked AFTER metrics.json is written, so the artifact still exists to explain
                // the failure -- the nonzero-exit requirement this whole mode exists to serve.
                if (metrics.validationErrors > 0) {
                    std::fprintf(stderr,
                                 "--headless-render: %u Vulkan validation error(s) fired during this run "
                                 "(%u warning(s)) -- failing.\n",
                                 metrics.validationErrors, metrics.validationWarnings);
                    exitCode = 3;
                }
            }

            // Unlike --sim-only (which never calls InitRuntime, so it tears down physics/session
            // itself), this mode DID call InitRuntime -- ShutdownRuntime already calls EndSession
            // internally (see app/runtime_init.cpp), so calling it again here would fire every
            // script's OnDestroy twice. Same teardown call the normal player makes.
            ShutdownRuntime(rs);
        } // `rs` (and therefore Renderer/PhysicsWorld/AudioEngine) destructs HERE -- see this
          // block's opening comment for why that has to happen before glfwTerminate() below.

        glfwDestroyWindow(window);
        glfwTerminate();
        return exitCode;
    }

} // namespace toon
