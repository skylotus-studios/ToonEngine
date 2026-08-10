#pragma once
//============================================================================
//  app/metrics.h: emit artifacts/metrics.json, a structured per-run fingerprint.
//
//  --sim-only (app/sim_runtime.h) and --hash-every (app/world_hash.h) already prove two runs
//  simulate identically, but neither produces something a CI job -- or a human -- can diff
//  run-over-run without grepping stdout. This is that artifact: one JSON object, schema-stable
//  across today's --sim-only and the future --headless-render, every field either a real
//  measured number or an honest null.
//
//  Roughly half of today's fields are unconditionally null: --sim-only creates no window, no
//  Vulkan device, and no audio device (Renderer::HasDevice is always false; see
//  app/sim_runtime.cpp's VerifyHeadless), so anything that needs one -- render.*, vulkan.*,
//  gpu.*, ui.* -- has nothing to report. Each null field's own comment in metrics.cpp names what
//  unlocks it (the future --headless-render mode, which creates an OFFSCREEN device).
//============================================================================
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace toon {

    struct RuntimeState;

    struct MetricsOptions {
        std::string outPath = "artifacts/metrics.json";
    };

    // Everything WriteMetrics can't read straight off `rs` because the caller already discarded
    // it (per-tick samples) or computed it its own way (the scripts.ran proxy -- see its own
    // comment at its computation site in sim_runtime.cpp for why that's here and not on
    // RuntimeState/PhysicsWorld).
    struct MetricsInputs {
        std::vector<std::pair<int, uint64_t>> hashSamples; // (tick, HashWorldState result)
        std::vector<double> tickMs;                        // one entry per simulated tick
        uint64_t scriptsRan = 0;

        // --headless-render only (app/headless_render.h). `hasRenderData` is the gate: false
        // (the default, and --sim-only's only ever call shape) leaves render.*/vulkan.errors+
        // warnings/ui.* exactly as null as they've always been; true supplies real numbers for
        // all of them. vulkan.instance_extensions/device_extensions and gpu.mem_bytes stay null
        // either way -- see metrics.cpp's own comment on why those two are out of scope here.
        bool hasRenderData = false;
        std::vector<double> frameMs; // one entry per rendered frame
        uint32_t drawCalls = 0;      // last frame's count (see Renderer::DrawCallCount)
        uint32_t psoSwitches = 0;    // last frame's count (see Renderer::PSOSwitchCount)
        uint32_t validationErrors = 0;
        uint32_t validationWarnings = 0;
        uint32_t uiBoxesLive = 0;
        uint32_t uiBoxesPruned = 0;

        // Per-pass draw-call breakdown (Renderer::DrawCallsByPass), last frame's snapshot, same
        // convention as drawCalls above. postResolve is only our own ACES tonemap draw --
        // DiligentFX's SSAO/TAA/DoF/SSR/Bloom draws aren't visible to Renderer's counter; see
        // renderer.h's RenderPassCounts comment.
        uint32_t drawCallsShadow = 0;
        uint32_t drawCallsOpaqueToon = 0;
        uint32_t drawCallsSprite = 0;
        uint32_t drawCallsUI = 0;
        uint32_t drawCallsPostResolve = 0;
        uint32_t drawCallsOther = 0;
        uint32_t shadowCascades = 0; // Renderer::ShadowCascadeCount()
        uint64_t gpuMemBytes = 0;    // Renderer::GpuMemBytes() -- see its own "not total VRAM" caveat
        std::vector<double> uiSolveMs; // one entry per rendered frame (RenderHUD's uiSolveMsOut)
    };

    // Writes `mopts.outPath` (creating parent directories as needed, same as
    // core/scene/serializer.cpp's SaveScene). Reads `rs` for its steady-state figures (physics/
    // audio/asset counts) -- call this AFTER the tick/frame loop but BEFORE EndSession/Shutdown
    // tear those systems down, so the numbers reflect the run that just happened, not a
    // half-torn-down one.
    void WriteMetrics(const RuntimeState &rs, const MetricsInputs &inputs, const MetricsOptions &mopts);

} // namespace toon
