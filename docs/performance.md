# ToonEngine Performance

Where the engine's time goes, at build time and at frame time, and which levers exist.

This file starts as a map of the known cost centres, not a set of measured budgets. No
profiling capture or frame-time target has been recorded yet. Anything below stated as a cost
is structural, derived from how the pipeline is built, rather than measured. Add numbers here
as you take them, and mark what was measured on what hardware.

## Build Time

Diligent builds every supported backend by default, and that dominates a clean build. The
`DILIGENT_NO_*` cache options set before `add_subdirectory(DiligentCore)` are the largest
single lever; see [invariants.md](invariants.md) rule 4. D3D11, D3D12, and OpenGL are off.

`DILIGENT_NO_RADIENT` also has to stay set. Beyond being unused, DiligentFX's GI module fails
to compile under clang-cl, so a full `cmake --build` or a CI run breaks without it even when
CLion's incremental build appears fine.

Builds go to `build/<preset>/` with engine DLLs copied next to the executable.

### Measured, agent-debug

Taken on the development machine (Windows 11, clang-cl 22, Ninja, `sccache` as compiler
launcher), rebuilding only ToonEngine's own 48 translation units. Diligent, GLFW, and Jolt are
already built in every figure below.

| Scenario | Time |
|---|---:|
| Rebuild all 48 of our TUs, warm cache (48/48 hits) | 5.8 s |
| Same, with caching forced off | 17.3 s |
| Edit `core/math.h`, rebuild the 41 TUs that see it | 14.5 s |
| Edit one leaf `.cpp` | 2.3 s |
| Null build, nothing changed | 0.11 s |

Median single-TU compile is about 1.2 s. `core/rendering/renderer.cpp` is the outlier at 4.3 s;
it is the only TU that includes the Diligent engine headers directly, and it preprocesses to
255,809 lines.

The dominant per-TU cost is the standard library, not our dependencies. Under the real compile
flags `<array>` preprocesses to 54,346 lines, `<string>` to 64,468, and `<vector>` to 62,333,
while all of `core/math.h` is 5,282 and `<GLFW/glfw3.h>` is 611.

### Precompiled headers: measured and rejected

The obvious lever for that standard-library cost is `target_precompile_headers`. It was
implemented, measured, and reverted. CMake drives clang-cl's PCH with MSVC-style `/Yc` and
`/Yu`, and `sccache` classifies every `/Yu` compile as a **non-cacheable call**: it runs the
compiler and stores nothing. Enabling a PCH on the agent presets therefore turns all 48 of our
TUs from cache hits into full compiles, replacing the 5.8 s warm rebuild with something at or
above the 17.3 s uncached figure, permanently.

The two are mutually exclusive, and the cache is worth more. Anyone revisiting this should
either drop `sccache` from the agent presets first, or wait for `sccache` to gain MSVC PCH
support. Note that a probe using the *Clang*-style `-Xclang -include-pch` spelling caches fine
and will mislead you; CMake does not emit that form for clang-cl.

### Two sccache traps in this build

`sccache` drops the connection ("An existing connection was forcibly closed", os error 10054)
partway through `Diligent-GraphicsEngineVk-static` unless its protocol frame limit is raised.
The server's own log gives the real reason, `frame size too big`: Diligent's largest Vulkan
sources preprocess past the default limit and kill the server mid-request. Starting the server
with `SCCACHE_MAX_FRAME_LENGTH` set high (1 GiB works) builds those targets cleanly. Retrying
without it does not converge — the failure count wanders rather than falling.

Configure through the `C:\ted` symlink, not the full source path. From the long path the same
Diligent sources fail with "path too long" instead, and CMake must also be run from a Developer
PowerShell or it picks up standalone LLVM rather than the VS-bundled clang-cl.

## Frame Structure

The simulation runs on a fixed 60 Hz tick, decoupled from the render rate and interpolated
into it. Rendering can therefore run faster or slower than the sim without changing gameplay
behaviour. Physics and scripts advance only while the mode is Playing.

Per rendered frame, in order:

1. Shadow pre-pass. Cascaded shadow maps via Diligent's `ShadowMapManager`, rendered before
   either toon pipeline.
2. Toon draw, two passes per mesh sharing one dynamic constant buffer. The outline pass
   (inverted hull, front faces culled) and the fill pass (banded diffuse, back faces culled).
   Budget two draw calls per visible mesh, not one.
3. G-buffer resolve into HDR colour, normals, and motion vectors.
4. DiligentFX post chain: SSAO, then optional TAA, DoF, and SSR, then Bloom.
5. ACES tone-map resolve to the back buffer.
6. ImGui editor UI.

## Runtime Levers

Every post effect is individually toggleable from the Settings panel, which is the fastest way
to attribute a frame-time regression to a stage. TAA, DoF, and SSR are optional and off-path
when disabled; SSAO and Bloom are part of the standard chain.

The collider debug wireframe is a separate pass and should be off when measuring.

Mouse picking is a geometric ray-vs-bounds test rather than a physics raycast, so it costs
nothing in the physics world and runs in Editing mode.

## Measured Actuals

Taken on the development machine (Windows 11, NVIDIA GeForce RTX 3080, Vulkan driver
79.344.0, `agent-debug` build) against the demo scene
([`assets/scenes/default.scene`](../assets/scenes/default.scene), the one
[architecture.md](architecture.md) calls "the demo scene"). `--sim-only` gives the sim-tick
figures (3600 ticks, seed 0, no window/device at all); `--headless-render` gives everything
render-side (300 frames, a real Vulkan device, at both the tool's default window size and 4K):

```
ToonPlayer.exe --sim-only --scene assets/scenes/default.scene --seed 0 --ticks 3600 \
    --hash-every 60 --metrics-out artifacts/perf/sim_only.json

ToonPlayer.exe --headless-render --scene assets/scenes/default.scene --width 1600 --height 900 \
    --frames 300 --metrics-out artifacts/perf/headless_1600x900.json

ToonPlayer.exe --headless-render --scene assets/scenes/default.scene --width 3840 --height 2160 \
    --frames 300 --metrics-out artifacts/perf/headless_4k.json
```

| Metric | Value |
|---|---:|
| `sim.tick_ms.p50` / `.p99` | 0.017 ms / 0.089 ms |
| `render.frame_ms.p50` / `.p99`, 1600x900 | 6.81 ms / 13.97 ms |
| `render.frame_ms.p50` / `.p99`, 3840x2160 (4K) | 6.87 ms / 25.04 ms |
| `render.draw_calls` (total, both resolutions) | 27 |
| &nbsp;&nbsp;`draw_calls_shadow` | 16 (4 cascades x [mesh + model], see below) |
| &nbsp;&nbsp;`draw_calls_opaque_toon` | 8 (outline + fill, per mesh/model) |
| &nbsp;&nbsp;`draw_calls_sprite` | 0 (the demo scene has no sprites) |
| &nbsp;&nbsp;`draw_calls_ui` | 1 (one HUD batch) |
| &nbsp;&nbsp;`draw_calls_post_resolve` | 1 (the ACES tonemap resolve -- see caveat below) |
| &nbsp;&nbsp;`draw_calls_other` | 1 (editor sky; debug wireframe is off) |
| `render.shadow_cascades` | 4 |
| `render.pso_switches` | 12 |
| `alloc.peak_bytes`, `--sim-only` | 25.4 MB |
| `alloc.peak_bytes`, `--headless-render` | 292.2 MB (both resolutions -- CPU-side, not window-sized) |
| `gpu.mem_bytes`, 1600x900 | 104.0 MB |
| `gpu.mem_bytes`, 3840x2160 (4K) | 284.0 MB |
| `ui.solve_ms.p50` / `.p99`, 1600x900 | 0.023 ms / 0.115 ms |
| `ui.solve_ms.p50` / `.p99`, 3840x2160 | 0.015 ms / 0.062 ms |

Two caveats these numbers carry, not hidden:

- **`draw_calls_post_resolve` is not "the post chain."** DiligentFX's SSAO/TAA/DoF/SSR/Bloom
  `Execute()` calls issue their own draws directly on the Vulkan context (`Impl::RunPostFX`),
  bypassing the `CountedDraw`/`CountedDrawIndexed` wrappers every draw in `renderer.cpp` owns
  routes through. `draw_calls_post_resolve` is only the one draw this file issues itself, the
  ACES tonemap resolve. How many draws SSAO/Bloom/etc. cost internally isn't measured; patching
  DiligentFX itself to count them is out of scope (invariant 1 -- don't reinvent/modify
  Diligent's own modules).
- **`gpu.mem_bytes` is "engine-owned GPU memory we can see," not total VRAM use.** It sums the
  offscreen G-buffer targets, the shadow map, loaded editor/sprite textures, and per-mesh vertex/
  index buffers (`Renderer::GpuMemBytes`, `core/rendering/renderer.h`). Excluded: glTF model
  vertex/index/texture data (owned inside `Diligent::GLTF::Model`, not `Renderer`) and
  DiligentFX's own post-chain intermediates (bloom mips, SSAO/SSR/TAA history) -- the same
  resources `draw_calls_post_resolve` can't see the draws for. The G-buffer targets alone explain
  most of the 1600x900 -> 4K jump (5 window-sized RGBA16F/D32/RG16F targets scale with pixel
  count; everything else in the sum doesn't).

## Proposed Budgets

Budget = actual + headroom, using `scripts/metrics_diff.py`'s own existing convention: timing
fields get the same 20% relative tolerance already defined there (so the number below is exactly
what CI would flag once a tier exercises it -- see the "Enforced today" column). Draw-call/
cascade/memory counts are exact-match fields in `metrics_diff.py`: not a ceiling in the timing
sense, but a tripwire -- any change should be a reviewed, intentional rendering change, not silent
drift. Their "headroom" column below is a rough scene-growth planning number for a bigger future
scene, not something CI enforces on top of the exact match.

| Metric | Actual | Budget | Enforced today |
|---|---:|---:|:---|
| `sim.tick_ms.p99` | 0.089 ms | 0.107 ms (+20%) | **Yes** -- every fast/full tier run |
| `render.frame_ms.p99`, 1600x900 | 13.97 ms | 16.76 ms (+20%) | No -- no tier runs `--headless-render` |
| `render.frame_ms.p99`, 4K | 25.04 ms | 30.05 ms (+20%) | No |
| `render.draw_calls` (total) | 27 | exact match; +~15 headroom for scene growth | No |
| `render.draw_calls_shadow` | 16 | exact match | No |
| `render.draw_calls_opaque_toon` | 8 | exact match | No |
| `render.shadow_cascades` | 4 | exact match (a deliberate config change, not drift) | No |
| `alloc.peak_bytes`, `--headless-render` | 292.2 MB | 350.6 MB (+20%) | No |
| `gpu.mem_bytes`, 4K | 284.0 MB | exact match; +~100 MB headroom for scene growth | No |
| `ui.solve_ms.p99` | 0.115 ms | 0.138 ms (+20%) | No |

**None of the render/gpu/ui budgets are enforced by `verify.py` today.** Only `sim.tick_ms` is,
because only `step_sim_only_smoke`/`step_metrics_diff` in `scripts/verify.py` ever run
`--sim-only` -- no fast/full/deep tier step runs `--headless-render`, so every render/gpu/ui field
above stays null in the baselines those steps diff against, and `metrics_diff.py`'s "baseline
missing this key" behavior silently skips the comparison. `scripts/metrics_diff.py` already
carries tolerance/exact-match entries for all of them (`TOLERANCE_FIELDS`/`EXACT_FIELDS`), ready
for whichever tier eventually adds a `--headless-render` step -- see Known Open Questions below.

## Known Open Questions

- No CI tier runs `--headless-render`, so the render/gpu/ui budgets above are documented targets
  only, not gated -- wiring a `--headless-render` step (with its own checked-in baseline) into
  `scripts/verify.py` would close that gap.
- The two-pass toon draw doubles draw-call count; no instancing or batching exists yet.
- Shadow cascade count and resolution have not been tuned against a measured cost.
- DiligentFX's post-chain draw count (SSAO/TAA/DoF/SSR/Bloom) isn't measured -- see Measured
  Actuals' first caveat above.
- The GPU leak noted when scene transitions landed (roadmap #19) was deferred to #20 and is
  still open. See [roadmap.md](roadmap.md).

## Related

[architecture.md](architecture.md) describes the pipeline these costs come from.
[MEMORY.md](../MEMORY.md), read on demand, holds the debugging history behind specific
pipeline decisions.