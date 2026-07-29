#pragma once
//============================================================================
//  app/resize_soak.h: --resize-soak -- repeated Renderer::Resize under a real device.
//
//  Sibling to app/headless_render.h: same hidden-window/real-device bring-up
//  (InitRuntime(..., strictValidation=true)), but instead of a fixed frame budget with capture
//  points, it cycles the swap chain through a small fixed set of sizes -- alt-tab/monitor-change/
//  window-drag territory (roadmap #30 names exactly this as the highest-probability crash class
//  for a Vulkan title) -- rendering one real frame after each resize so the resized swap chain
//  actually gets used, not just reallocated. Vulkan validation stays strict throughout; a
//  nonzero validation-error count at the end is the failure signal, same convention
//  --headless-render already established.
//============================================================================
#include <string>

namespace toon {

    struct ResizeSoakOptions {
        std::string scenePath;
        int iterations = 40;
        std::string metricsOutPath = "artifacts/metrics.json";
    };

    // Returns a process exit code: 0 on success, 1 if the scene failed to load, 3 if any Vulkan
    // validation error fired during the soak (checked after metrics.json is written, same
    // ordering --headless-render uses).
    int RunResizeSoak(const ResizeSoakOptions &opts);

} // namespace toon
