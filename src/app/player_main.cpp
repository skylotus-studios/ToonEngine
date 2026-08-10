//============================================================================
//  app/player_main.cpp: entry point for the standalone player (ToonPlayer.exe).
//
//  The shippable artifact: it links ToonRuntime and nothing from ui/panels/ or ImGuizmo, so a
//  built game contains no editor. All it does is open a window and hand off to the runtime loop
//  (app/runtime_init.h) -- the same loop the editor's `--play` dev flag drives. The scene to
//  play is argv[1] (defaulting to the bundled default scene).
//
//  Diligent-free like main.cpp: all GPU/backend work stays behind core/rendering/renderer.h.
//
//  Also the entry point for --sim-only (app/sim_runtime.h) and --headless-render
//  (app/headless_render.h). --sim-only branches BEFORE glfwInit, so that mode touches no
//  windowing, no graphics device, and no audio device at all. --headless-render DOES need GLFW +
//  a window (just a hidden one) -- see that mode's own file banner for why. Both are runtime
//  flags, not build variants: there is exactly one ToonPlayer.exe.
//============================================================================
#include "app/headless_render.h"   // --headless-render: real device, hidden window, PNG capture
#include "app/resize_soak.h"       // --resize-soak: repeated Renderer::Resize under a real device
#include "app/runtime_init.h"
#include "app/scene_roundtrip.h"   // --scene-roundtrip: load/save idempotency check
#include "app/sim_runtime.h"       // --sim-only: the headless simulation harness
#include "core/platform/paths.h"   // Assets::Init + Assets::Scenes (exe-relative asset paths)

#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib> // std::strtoull / std::strtol -- command-line number parsing
#include <cstring> // std::strcmp -- flag matching
#include <ios>
#include <iostream>
#include <sstream> // --capture's comma-separated frame-index list
#include <string>
#include <vector>

namespace {

    // Read the value that follows `argv[i]`, advancing `i` past it. Returns nullptr (and logs) if
    // the flag was last on the line, so a typo'd invocation reports itself instead of reading off
    // the end of argv.
    const char *FlagValue(int argc, char **argv, int &i) {
        if (i + 1 >= argc) {
            std::fprintf(stderr, "Missing value for %s\n", argv[i]);
            return nullptr;
        }
        return argv[++i];
    }

    // "60,150,299" -> {60, 150, 299}. A malformed entry (non-numeric, trailing comma) is skipped
    // silently rather than failing the whole run -- a capture list is a diagnostic aid, not a
    // contract worth aborting over.
    std::vector<int> ParseCaptureFrames(const std::string &csv) {
        std::vector<int> frames;
        std::istringstream ss(csv);
        std::string token;
        while (std::getline(ss, token, ',')) {
            if (!token.empty()) { frames.push_back(std::atoi(token.c_str())); }
        }
        return frames;
    }

} // namespace

int main(int argc, char **argv) {
    // Unbuffered stdout so a silent early-init failure still prints (see main.cpp).
    std::cout.setf(std::ios::unitbuf);

    // Resolve the asset root (assets/ next to the exe for a shipped build, else the baked
    // source tree) before the default scene path below is built from it; see core/platform/paths.h.
    toon::Assets::Init();

    // Parse the flag form first. The historical positional argv[1] scene path still works
    // unchanged (that's the `!simOnly && sceneArg.empty()` fallback below), so nothing about
    // launching the normal player has changed.
    bool simOnly = false;
    bool headlessRender = false;
    bool sceneRoundtrip = false;
    bool resizeSoak = false;
    std::string sceneArg;
    std::string metricsOutPath = "artifacts/metrics.json";
    toon::SimOptions sim;
    toon::HeadlessRenderOptions headless;
    toon::SceneRoundtripOptions roundtrip;
    toon::ResizeSoakOptions resize;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--sim-only") == 0) {
            simOnly = true;
        } else if (std::strcmp(argv[i], "--headless-render") == 0) {
            headlessRender = true;
        } else if (std::strcmp(argv[i], "--scene-roundtrip") == 0) {
            sceneRoundtrip = true;
        } else if (std::strcmp(argv[i], "--out-dir") == 0) {
            const char *v = FlagValue(argc, argv, i);
            if (!v) { return 1; }
            roundtrip.outDir = v;
        } else if (std::strcmp(argv[i], "--resize-soak") == 0) {
            resizeSoak = true;
            const char *v = FlagValue(argc, argv, i);
            if (!v) { return 1; }
            resize.iterations = std::atoi(v);
            if (resize.iterations <= 0) {
                std::fprintf(stderr, "--resize-soak must be positive (got '%s')\n", v);
                return 1;
            }
        } else if (std::strcmp(argv[i], "--width") == 0) {
            const char *v = FlagValue(argc, argv, i);
            if (!v) { return 1; }
            headless.width = std::atoi(v);
            if (headless.width <= 0) {
                std::fprintf(stderr, "--width must be positive (got '%s')\n", v);
                return 1;
            }
        } else if (std::strcmp(argv[i], "--height") == 0) {
            const char *v = FlagValue(argc, argv, i);
            if (!v) { return 1; }
            headless.height = std::atoi(v);
            if (headless.height <= 0) {
                std::fprintf(stderr, "--height must be positive (got '%s')\n", v);
                return 1;
            }
        } else if (std::strcmp(argv[i], "--frames") == 0) {
            const char *v = FlagValue(argc, argv, i);
            if (!v) { return 1; }
            headless.frames = std::atoi(v);
            if (headless.frames <= 0) {
                std::fprintf(stderr, "--frames must be positive (got '%s')\n", v);
                return 1;
            }
        } else if (std::strcmp(argv[i], "--capture") == 0) {
            const char *v = FlagValue(argc, argv, i);
            if (!v) { return 1; }
            headless.captureFrames = ParseCaptureFrames(v);
        } else if (std::strcmp(argv[i], "--capture-dir") == 0) {
            const char *v = FlagValue(argc, argv, i);
            if (!v) { return 1; }
            headless.captureDir = v;
        } else if (std::strcmp(argv[i], "--post") == 0) {
            // The only recognized value today is "off" (scripts/run_golden_tests.py's golden-
            // image mode -- see app/headless_render.h's own comment on HeadlessRenderOptions::
            // postOff). Anything else is a usage error rather than silently ignored, so a typo
            // ("--post ofF") fails loudly instead of quietly rendering with post-processing on.
            const char *v = FlagValue(argc, argv, i);
            if (!v) { return 1; }
            if (std::strcmp(v, "off") == 0) {
                headless.postOff = true;
            } else {
                std::fprintf(stderr, "--post: unrecognized value '%s' (only 'off' is supported)\n", v);
                return 1;
            }
        } else if (std::strcmp(argv[i], "--scene") == 0) {
            const char *v = FlagValue(argc, argv, i);
            if (!v) { return 1; }
            sceneArg = v;
        } else if (std::strcmp(argv[i], "--seed") == 0) {
            const char *v = FlagValue(argc, argv, i);
            if (!v) { return 1; }
            sim.seed = std::strtoull(v, nullptr, 10);
        } else if (std::strcmp(argv[i], "--ticks") == 0) {
            const char *v = FlagValue(argc, argv, i);
            if (!v) { return 1; }
            sim.ticks = static_cast<int>(std::strtol(v, nullptr, 10));
            if (sim.ticks <= 0) {
                std::fprintf(stderr, "--ticks must be positive (got '%s')\n", v);
                return 1;
            }
        } else if (std::strcmp(argv[i], "--hash-every") == 0) {
            const char *v = FlagValue(argc, argv, i);
            if (!v) { return 1; }
            sim.hashEvery = static_cast<int>(std::strtol(v, nullptr, 10));
            if (sim.hashEvery <= 0) {
                std::fprintf(stderr, "--hash-every must be positive (got '%s')\n", v);
                return 1;
            }
        } else if (std::strcmp(argv[i], "--metrics-out") == 0) {
            const char *v = FlagValue(argc, argv, i);
            if (!v) { return 1; }
            metricsOutPath = v;
        } else if (argv[i][0] != '-' && sceneArg.empty()) {
            sceneArg = argv[i]; // the original positional form
        } else {
            std::fprintf(stderr, "Unknown argument: %s\n", argv[i]);
            return 1;
        }
    }

    const std::string defaultScenePath = toon::Assets::Scenes() + "/default.scene";

    // Headless: no window, no graphics device, no audio device. Branches ahead of glfwInit so
    // this mode never initializes the windowing library at all.
    if (simOnly) {
        sim.scenePath = sceneArg.empty() ? defaultScenePath : sceneArg;
        sim.metricsOutPath = metricsOutPath;
        return toon::RunSimOnly(sim);
    }

    // Also headless: LoadScene/SaveScene only reach a Renderer to upload GPU resources, which
    // early-out with no device (see app/scene_roundtrip.h's own banner).
    if (sceneRoundtrip) {
        roundtrip.scenePath = sceneArg.empty() ? defaultScenePath : sceneArg;
        return toon::RunSceneRoundtrip(roundtrip);
    }

    // A real device + a real (hidden) window -- see app/headless_render.h's own banner for why
    // this isn't windowless Vulkan.
    if (headlessRender) {
        headless.scenePath = sceneArg.empty() ? defaultScenePath : sceneArg;
        headless.metricsOutPath = metricsOutPath;
        return toon::RunHeadlessRender(headless);
    }

    if (resizeSoak) {
        resize.scenePath = sceneArg.empty() ? defaultScenePath : sceneArg;
        resize.metricsOutPath = metricsOutPath;
        return toon::RunResizeSoak(resize);
    }

    const std::string scenePath = sceneArg.empty() ? defaultScenePath : sceneArg;

    if (!glfwInit()) {
        std::fprintf(stderr, "GLFW init failed\n");
        return 1;
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // Vulkan, not GL

    GLFWwindow *window = glfwCreateWindow(1600, 900, "ToonEngine", nullptr, nullptr);
    if (!window) {
        std::fprintf(stderr, "Failed to create window\n");
        glfwTerminate();
        return 1;
    }

    toon::RuntimeState rs;
    if (!toon::InitRuntime(rs, window, scenePath.c_str())) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }
    toon::RunRuntimeLoop(rs);
    toon::ShutdownRuntime(rs);

    glfwDestroyWindow(window);
    glfwTerminate();
    return 0;
}
