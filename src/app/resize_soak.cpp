//============================================================================
//  app/resize_soak.cpp: see resize_soak.h.
//============================================================================
#include "app/resize_soak.h"

#include "app/app_state.h"
#include "app/metrics.h"
#include "app/runtime_init.h"
#include "app/runtime_render.h"
#include "app/runtime_state.h"
#include "app/runtime_tick.h"
#include "app/runtime_ui.h"
#include "app/session.h"
#include "core/platform/paths.h"
#include "core/scene/scripts/builtin_scripts.h"

#include <GLFW/glfw3.h>

#include <cstdio>
#include <filesystem>

namespace toon {
    namespace {

        std::string ResolveScenePath(const std::string &given) {
            std::error_code ec;
            if (!given.empty() && std::filesystem::exists(given, ec)) { return given; }
            const std::string byName = Assets::Scene(std::filesystem::path(given).filename().string());
            if (std::filesystem::exists(byName, ec)) { return byName; }
            return {};
        }

        // A small fixed cycle, not random sizes: deterministic across runs (the same failure
        // reproduces the same way twice), and deliberately excludes 0/degenerate sizes -- those
        // would exercise GLFW/Vulkan's own edge handling, not this engine's Resize path. Includes
        // both a shrink-below and grow-above the default 1600x900 to cross real swap-chain
        // recreation thresholds in both directions.
        constexpr uint32_t kSizes[][2] = {
            {1600, 900}, {640, 360}, {3840, 2160}, {800, 600}, {1920, 1080},
        };
        constexpr int kSizeCount = sizeof(kSizes) / sizeof(kSizes[0]);

    } // namespace

    int RunResizeSoak(const ResizeSoakOptions &opts) {
        RegisterBuiltinScripts();

        const std::string scenePath = ResolveScenePath(opts.scenePath);
        if (scenePath.empty()) {
            std::fprintf(stderr, "--resize-soak: no scene file found for '%s'\n", opts.scenePath.c_str());
            return 1;
        }

        if (!glfwInit()) {
            std::fprintf(stderr, "--resize-soak: GLFW init failed\n");
            return 1;
        }
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE); // same hidden-window shape as --headless-render

        GLFWwindow *window = glfwCreateWindow(1600, 900, "ToonEngine (resize-soak)", nullptr, nullptr);
        if (!window) {
            std::fprintf(stderr, "--resize-soak: failed to create the hidden window\n");
            glfwTerminate();
            return 1;
        }

        int exitCode = 0;
        {
            RuntimeState rs;
            if (!InitRuntime(rs, window, scenePath.c_str(), /*strictValidation=*/true)) {
                glfwDestroyWindow(window);
                glfwTerminate();
                return 1;
            }

            rs.pendingScenePath = scenePath;
            BeginNewGame(rs);

            std::printf("--resize-soak: scene=%s iterations=%d\n", scenePath.c_str(), opts.iterations);
            std::fflush(stdout);

            MetricsInputs metrics;
            metrics.hasRenderData = true;
            metrics.frameMs.reserve(static_cast<size_t>(opts.iterations));

            int iterationsRun = 0;
            for (int i = 0; i < opts.iterations; ++i) {
                const uint32_t w = kSizes[i % kSizeCount][0];
                const uint32_t h = kSizes[i % kSizeCount][1];
                rs.renderer.Resize(w, h);

                RuntimeBeginFrame(rs);
                const bool wasPlaying = (rs.appState == AppState::Playing);
                TickAppState(rs);
                if (rs.appState == AppState::Title) {
                    std::fprintf(stderr, "--resize-soak: scene failed to load, nothing to render: %s\n",
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

                RenderScene(rs);
                RenderHUD(rs, ScreenForAppState(rs.appState));

                metrics.uiBoxesLive = rs.ui.boxesLive;
                metrics.uiBoxesPruned = rs.ui.boxesPruned;
                metrics.drawCalls = rs.renderer.DrawCallCount();
                metrics.psoSwitches = rs.renderer.PSOSwitchCount();

                rs.renderer.EndFrame();
                ++iterationsRun;

                if (rs.appState == AppState::Quit) { break; }
            }

            std::printf("--resize-soak: %d resize(s) exercised\n", iterationsRun);

            if (exitCode == 0) {
                metrics.validationErrors = rs.renderer.ValidationErrorCount();
                metrics.validationWarnings = rs.renderer.ValidationWarningCount();

                MetricsOptions mopts;
                mopts.outPath = opts.metricsOutPath;
                WriteMetrics(rs, metrics, mopts);

                if (metrics.validationErrors > 0) {
                    std::fprintf(stderr,
                                 "--resize-soak: %u Vulkan validation error(s) fired across %d resize(s) "
                                 "(%u warning(s)) -- failing.\n",
                                 metrics.validationErrors, iterationsRun, metrics.validationWarnings);
                    exitCode = 3;
                }
            }

            ShutdownRuntime(rs);
        } // rs destructs before glfwTerminate() below -- see app/headless_render.cpp's own comment.

        glfwDestroyWindow(window);
        glfwTerminate();
        return exitCode;
    }

} // namespace toon
