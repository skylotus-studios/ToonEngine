#pragma once
//============================================================================
//  app/headless_render.h: the offscreen rendering driver (ToonPlayer --headless-render).
//
//  Runs the SAME real Vulkan device and swap chain the editor and the normal player use, driven
//  by a GLFW window that exists but is never shown (GLFW_VISIBLE = false) -- not windowless
//  Vulkan, which would need a much larger, riskier departure from the shipped rendering path for
//  little extra value. Fixed frame budget, specific frames captured to PNG, Vulkan validation
//  layers forced to strict, and a nonzero exit if any validation error fires. Twin to
//  app/sim_runtime.h's --sim-only: that mode proves the SIMULATION runs with no device at all;
//  this one proves the RENDERER produces clean, capturable frames.
//
//  A sibling of app/runtime_init.h's RunRuntimeLoop, not a variant of it, for the same reason
//  sim_runtime.h's RunSimOnly is: that loop runs until the window closes, with no frame budget
//  and no capture points, so bolting both on would mean threading flags through every step. The
//  genuinely shared parts -- InitRuntime/ShutdownRuntime and the frame's six-call shape -- ARE
//  reused here unchanged; InitRuntime doesn't care whether the GLFWwindow* it's handed is
//  visible, so almost nothing about bring-up differs from the normal player's.
//============================================================================
#include <string>
#include <vector>

namespace toon {

    struct HeadlessRenderOptions {
        // The scene to render. Same path-or-bare-filename resolution as --sim-only's SimOptions.
        std::string scenePath;
        // How many frames to render before exiting.
        int frames = 300;
        // Zero-based frame indices to capture to PNG (e.g. {60, 150, 299}). A requested index
        // >= frames is skipped with a warning, not an error.
        std::vector<int> captureFrames;
        // Directory captured PNGs are written into, as frame_<NNNN>.png.
        std::string captureDir = "artifacts";
        std::string metricsOutPath = "artifacts/metrics.json";
        // scripts/run_golden_tests.py's --post off: forces every stochastic/temporal PostFX
        // effect (bloom, SSAO, DoF, SSR, TAA) off so a capture is bit-for-bit reproducible run
        // over run -- see tests/scenes/golden_*.scene's own banners for why the golden tests
        // need this. `shadows` and `toneMap` are deliberately NOT touched: shadows are exactly
        // what golden_shadow_cascades.scene tests, and ACES tonemap is a deterministic per-pixel
        // curve, not a source of frame-to-frame noise. False (default) is byte-identical to
        // this option not existing.
        bool postOff = false;
    };

    // Owns its whole lifecycle: creates a hidden GLFW window + a real (strictly-validated)
    // Vulkan device, brings up the engine via InitRuntime, runs `opts.frames` real frames,
    // writes metrics.json, and tears down. Returns a process exit code: 0 on success, 1 if the
    // scene failed to load, 3 if any Vulkan validation error fired during the run (checked AFTER
    // metrics.json is written, so the artifact still exists to explain the failure).
    int RunHeadlessRender(const HeadlessRenderOptions &opts);

} // namespace toon
