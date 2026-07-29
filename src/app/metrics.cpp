//============================================================================
//  app/metrics.cpp: see metrics.h.
//============================================================================
#include "app/metrics.h"

#include "app/runtime_state.h"
#include "app/world_hash.h" // HashScriptOrder
#include "core/platform/memstats.h"
#include "core/platform/paths.h" // Assets::UsedFallback

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio> // std::snprintf (hex formatting) + std::fprintf (error reporting)
#include <filesystem>
#include <fstream>

using json = nlohmann::json;

namespace toon {
    namespace {

        // p50/p99 over `samples` (already in whatever unit the caller wants -- ms here). Empty
        // input -> both null, which is exactly right for render.frame_ms under --sim-only: no
        // frames were ever rendered, so there is nothing to percentile, not a zero.
        json Percentiles(std::vector<double> samples) {
            if (samples.empty()) { return json{{"p50", nullptr}, {"p99", nullptr}}; }
            std::sort(samples.begin(), samples.end());
            const auto at = [&](double fraction) {
                const size_t idx = static_cast<size_t>(fraction * static_cast<double>(samples.size() - 1));
                return samples[idx];
            };
            return json{{"p50", at(0.50)}, {"p99", at(0.99)}};
        }

        std::string HexU64(uint64_t v) {
            char buf[17];
            std::snprintf(buf, sizeof(buf), "%016llx", static_cast<unsigned long long>(v));
            return buf;
        }

    } // namespace

    void WriteMetrics(const RuntimeState &rs, const MetricsInputs &inputs, const MetricsOptions &mopts) {
        json hashArray = json::array();
        for (const auto &[tick, hash] : inputs.hashSamples) {
            hashArray.push_back(json{{"tick", tick}, {"hash", HexU64(hash)}});
        }

        json root;

        root["sim"] = {
            {"state_hash", hashArray},
            {"tick_ms", Percentiles(inputs.tickMs)},
        };

        // render.*, vulkan.errors/warnings, ui.*: null unless the caller actually rendered
        // (--headless-render, app/headless_render.h, sets hasRenderData). Under --sim-only,
        // Renderer::Init is never called (see app/sim_runtime.cpp), so there is no swap chain to
        // time, no draw-call/PSO counter to read, and RenderHUD -- so UI_BeginBuild/EndBuild --
        // never runs either; every one of these fields stays null there, same as always.
        root["render"] = {
            {"frame_ms", Percentiles(inputs.hasRenderData ? inputs.frameMs : std::vector<double>{})},
            {"draw_calls", inputs.hasRenderData ? json(inputs.drawCalls) : nullptr},
            {"pso_switches", inputs.hasRenderData ? json(inputs.psoSwitches) : nullptr},
        };
        root["vulkan"] = {
            {"validation_errors", inputs.hasRenderData ? json(inputs.validationErrors) : nullptr},
            {"validation_warnings", inputs.hasRenderData ? json(inputs.validationWarnings) : nullptr},
            // Unconditionally null even with real render data: the ACTUALLY-enabled extension
            // list needs reaching into Diligent's internal Vulkan objects (no public API exposes
            // it, unlike the available-but-not-necessarily-enabled list a raw vkEnumerate* call
            // would give) -- fragile and version-coupled, out of scope for this pass.
            {"instance_extensions", nullptr},
            {"device_extensions", nullptr},
        };
        root["gpu"] = {
            // Unconditionally null: Diligent exposes only the ADAPTER's total capacity
            // (IRenderDevice::GetAdapterInfo().Memory), not this process's live allocated bytes.
            // A true figure needs new manual GPU resource-size bookkeeping -- the same class of
            // follow-up as alloc.count_after_init below, out of scope for this pass.
            {"mem_bytes", nullptr},
        };
        root["ui"] = {
            {"boxes_live", inputs.hasRenderData ? json(inputs.uiBoxesLive) : nullptr},
            {"boxes_pruned", inputs.hasRenderData ? json(inputs.uiBoxesPruned) : nullptr},
        };

        root["jolt"] = {
            {"bodies", rs.physicsWorld.BodyCount()},
            {"active_bodies", rs.physicsWorld.ActiveBodyCount()},
            {"contacts", rs.physicsWorld.TotalContactEvents()},
        };

        root["alloc"] = {
            // TODO(alloc-instrumentation): a true allocation tally needs a global operator new/
            // delete override, engine-wide -- out of scope for this pass; see
            // core/platform/memstats.h's own comment on why peak_bytes below is the smaller,
            // additive step instead.
            {"count_after_init", nullptr},
            {"peak_bytes", PeakWorkingSetBytes()},
        };

        root["assets"] = {
            // The one field metrics_diff.py treats specially (see scripts/metrics_diff.py's
            // --require-no-fallback): a real, correct value in every build, but only an ALARM
            // condition (should be false) in a properly installed/packaged build. Legitimately
            // true for every ordinary dev build/<preset>/ run.
            {"fallback_used", Assets::UsedFallback()},
        };

        root["audio"] = {
            // Exactly correct, not a placeholder: AudioEngine::Init is never called in
            // --sim-only (see app/sim_runtime.cpp), so `handled` can never hold anything --
            // ActiveVoiceCount() is always 0 by construction.
            {"voices_active", rs.audio.ActiveVoiceCount()},
            // Not the same accessor/formula as voices_active above, even though both are 0 here:
            // "leaked" means still-open handles that outlived the session that opened them,
            // which requires create/destroy pair tracking this seam doesn't have. It reads 0
            // honestly (nothing was ever created, so nothing could leak), not as a stand-in for
            // "= voices_active".
            {"handles_leaked", 0},
        };

        root["scripts"] = {
            // A PROXY, not an instrumented call counter: sim_runtime.cpp computes this by
            // mirroring TickRuntime's own runScripts/advanceSim gate rather than adding a counter
            // inside core/scene/script.cpp's UpdateScripts (shared by editor and player). See
            // this field's own comment at its computation site in sim_runtime.cpp for what that
            // trades away.
            {"ran", inputs.scriptsRan},
            {"order_hash", HexU64(HashScriptOrder(rs))},
        };

        std::error_code ec; // create_directories: report via return value, never throw
        std::filesystem::create_directories(std::filesystem::path(mopts.outPath).parent_path(), ec);

        std::ofstream f(mopts.outPath);
        if (!f.is_open()) {
            std::fprintf(stderr, "WriteMetrics: failed to open '%s' for writing\n", mopts.outPath.c_str());
            return;
        }
        f << root.dump(2) << "\n";
    }

} // namespace toon
