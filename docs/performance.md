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

## Known Open Questions

- No frame-time target has been set for any platform.
- The two-pass toon draw doubles draw-call count; no instancing or batching exists yet.
- Shadow cascade count and resolution have not been tuned against a measured cost.
- The GPU leak noted when scene transitions landed (roadmap #19) was deferred to #20 and is
  still open. See [roadmap.md](roadmap.md).

## Related

[architecture.md](architecture.md) describes the pipeline these costs come from.
[MEMORY.md](../MEMORY.md), read on demand, holds the debugging history behind specific
pipeline decisions.