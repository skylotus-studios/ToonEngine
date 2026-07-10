# ToonEngine — Memory / Archive

Detailed history, gotchas, and decision rationale that don't need to be in
`CLAUDE.md`'s always-loaded context, but are worth keeping on hand. Pull this
up when you hit one of the errors below, or want the "why" behind a rule in
CLAUDE.md.

## Build gotchas

### `CMAKE_MT-NOTFOUND` — Windows SDK tools not on PATH

Ninja + clang-cl need `mt.exe`/`rc.exe` and the MSVC libs on `PATH`. A plain
terminal doesn't have these; configure fails with the cryptic
`CMAKE_MT-NOTFOUND`. In CLion this is handled by a **Visual Studio toolchain**,
which sources the VS Developer environment automatically (see
`docs/clion-setup-windows.md`). For command-line builds, `scripts/vsenv.ps1` does the
same: it locates the VS install via `vswhere` and imports the environment by
sourcing `VsDevCmd.bat -arch=x64 -host_arch=x64` and copying the resulting env
vars into the session.

**Do not use `Launch-VsDevShell.ps1 -DevCmdArguments ...`** — that parameter
doesn't exist on all VS builds (confirmed broken on this machine's VS 2022
Community: "A parameter cannot be found that matches parameter name
'DevCmdArguments'"). `VsDevCmd.bat`'s arguments are stable back to VS 2017.

A `'vswhere.exe' is not recognized` line printed by `VsDevCmd.bat` itself is
benign/cosmetic — the environment still imports correctly (verify by checking
`mt.exe` resolves on PATH).

After fixing the environment, a stale CMake cache from a failed configure will
keep `CMAKE_MT=NOTFOUND` cached — wipe `build/<preset>/` (or at least
`CMakeCache.txt` + `CMakeFiles/`) before reconfiguring.

### `RefCntAutoPtr.hpp` not found / link errors consuming the `-shared` Vulkan engine

Linking `Diligent-GraphicsEngineVk-shared` alone doesn't propagate
`Common/interface` (where `RefCntAutoPtr.hpp` lives) — link `Diligent-Common`
too. Also define `ENGINE_DLL=1` so Diligent's headers use the
`LoadGraphicsEngineVk()` runtime-load path that matches linking the engine as
`-shared` (rather than assuming a static/import-lib link).

### `CMAKE_C_COMPILE_OBJECT` missing internal variable

Happens when DiligentTools is added: it pulls in zlib/libpng (plain-C
libraries), which need a configured C toolchain, but the top-level
`project(ToonEngine CXX)` only enabled C++. Fix: `project(ToonEngine CXX C)`.
The error message gives no hint that "add C" is the fix — it just says CMake
"may not be built correctly."

### Compile time: DiligentCore builds every backend by default

On Windows, DiligentCore builds D3D11 + D3D12 + OpenGL + Vulkan by default,
regardless of what ToonEngine actually links — that was the majority of a
from-scratch build (~800 Ninja steps). Fixed by setting
`DILIGENT_NO_DIRECT3D11` / `_DIRECT3D12` / `_OPENGL` to `ON` (as
`CACHE BOOL ... FORCE`) **before** `add_subdirectory(external/DiligentCore)` —
cut it to ~650 steps and from 6 shipped engine DLLs down to 3
(`Archiver`, `GraphicsEngineVk`, `SuperResolution`). These are CMake cache
variables, so introducing/changing them needs a build-dir wipe to take effect.

### Full rebuilds are expensive — avoid wiping `build/` unless necessary

Wiping `build/<preset>/` throws away every compiled object; a from-scratch
DiligentCore+Tools build takes real time (many minutes). Only wipe when a
cache is genuinely stale (toolchain fix, new top-level `option()`/language) —
a normal `cmake --build` is incremental (seconds). When a wipe IS needed,
deleting just `CMakeCache.txt` + `CMakeFiles/` (not the whole directory) may
preserve already-compiled objects whose command line didn't change.

### DiligentTools has nested submodules

`git submodule update --init` alone won't populate DiligentTools' own
submodules (imgui, zlib, libpng, stb, json, args) — use `--recursive`.

## Window + device bring-up (GLFW + Vulkan)

`main.cpp` creates the GLFW window with `GLFW_NO_API` (Vulkan owns the surface,
not GL), then drives `Renderer`. Inside the seam (`renderer.cpp`):

- **`MakeNativeWindow()`** fills Diligent's `NativeWindow` per platform — Win32
  `hWnd`; Linux `WindowId` + `pDisplay` (X11 wired, Wayland fields exist); macOS
  `pNSView` (needs a Cocoa `.mm` helper from GLFWDemo — not yet written, so macOS
  is unbuilt).
- **`EngineFactoryVk`** creates the device + immediate context, then the swap
  chain. Desktop `PreTransform` is identity (it only matters on rotated mobile
  displays).
- **Dear ImGui** is brought up in `InitUI` — construct the Diligent renderer
  backend *before* the GLFW platform backend (see below for why).

Per-frame order in `main.cpp`: `BeginFrame` (bind the HDR offscreen target) →
`DrawMesh…` → `EndScene` (run bloom, then tone-map resolve to the back buffer) →
`BeginUI` / UI / `EndUI` → `EndFrame` (`Present`). `EndScene` internally runs the
DiligentFX bloom chain first (it binds its own targets), then resolves whichever HDR
source — raw scene or scene+bloom — to the back buffer.

## Dear ImGui integration

- **No GLFW backend shipped by DiligentTools.** `Diligent-Imgui` only
  auto-compiles Win32/Linux-native/SDL/macOS platform backends. Fixed by
  compiling `imgui_impl_glfw.cpp` (from the vendored
  `DiligentTools/ThirdParty/imgui/backends/`) as an extra ToonEngine source in
  `CMakeLists.txt`, linked against `Diligent-Imgui` to inherit its include
  paths/defines (`IMGUI_USER_CONFIG`, etc).
- **Init ordering bug (real, easy to hit again):** `ImGuiImplDiligent`'s
  constructor calls `ImGui::CreateContext()`. It must run **before**
  `ImGui_ImplGlfw_InitForVulkan()`, which calls `ImGui::GetIO()` and hits
  `assert(GImGui != 0)` if no context exists yet. Construct the Diligent
  renderer backend first, then the GLFW platform backend.
- **Shutdown ordering bug (the mirror image — aborts on window close):**
  `ShutdownUI` must call **`ImGui_ImplGlfw_Shutdown()` before** destroying the
  Diligent backend (`imgui.reset()` → `~ImGuiImplDiligent` → `ImGui::DestroyContext`).
  `DestroyContext` asserts `IO.BackendPlatformUserData == 0` ("Forgot to shutdown
  Platform backend?") if the GLFW backend is still registered — and that assert
  `abort()`s the process (exit 3 / abort-retry-ignore dialog) when you click the
  window's X. Tear down in the exact reverse of `InitUI`.
- **ImGui PSO depth format = `TEX_FORMAT_UNKNOWN`.** The UI is drawn to the back
  buffer with **no** depth attachment (EndScene binds a null DSV). Build the backend
  with `ImGuiDiligentCreateInfo{device, ColorBufferFormat, TEX_FORMAT_UNKNOWN}`, not
  the `(device, SwapChainDesc)` overload — the latter picks up the swap chain's depth
  format and Diligent then warns *every frame* that the bound DSV (none) doesn't match
  the ImGUI PSO.
- **`ImGui::ShowDemoWindow` fails to link.** `Diligent-Imgui`'s CMake
  deliberately excludes `imgui_demo.cpp` (demo/test code, not meant to ship).
  Don't reach for the demo window to smoke-test ImGui — write a tiny custom
  window instead (that's what `main.cpp` does).

### Docking (roadmap #5) — needs the imgui `docking` branch (not the fork's)

Docking (`ImGuiConfigFlags_DockingEnable`, `DockSpaceOverViewport`, DockBuilder)
lives on imgui's separate `docking` branch. Two traps enabling it here:

- **The vendored DiligentGraphics imgui fork's `docking` branch is ancient** — a
  pre-1.80 imgui (backends still under `examples/`, wrong API), incompatible with
  DiligentTools' modern (1.92.1) `Diligent-Imgui` integration. Its `backends/`
  dir doesn't even exist, so the build fails on `imgui_impl_glfw.cpp`. Don't use it.
- **Fix: point `ThirdParty/imgui` at UPSTREAM ocornut/imgui's `docking` tip**
  (1.92.9-WIP when this was done). Same 1.92 minor as DiligentTools' pinned
  1.92.1, modern `backends/` layout — `Diligent-Imgui` compiles against it clean:
  ```
  cd external/DiligentTools/ThirdParty/imgui
  git remote add upstream https://github.com/ocornut/imgui    # once
  git fetch --depth 1 upstream docking
  git checkout FETCH_HEAD          # was a23e9fb1 (1.92.9-WIP)
  ```
- **It's a LOCAL nested-submodule checkout, committed nowhere.** ToonEngine shows
  `m external/DiligentTools` (dirty submodule); DiligentTools shows
  `M ThirdParty/imgui`. **`git submodule update --recursive` reverts it** → imgui
  goes back to the fork's master (no docking). Re-run the checkout to restore it.
  A durable fix would fork DiligentTools and pin imgui to a docking commit — deferred.
- **Build stays green either way:** the docking code in `main.cpp` is guarded on
  `#ifdef IMGUI_HAS_DOCK` (imgui defines it only on the docking branch). With a
  non-docking imgui the debug window simply floats instead of docking.
- **API notes:** 1.92.x signature is `DockSpaceOverViewport(ImGuiID id = 0,
  const ImGuiViewport* = NULL, ImGuiDockNodeFlags = 0, ...)` — id is the FIRST
  arg (older took the viewport first). Enable only `DockingEnable`, NOT
  `ViewportsEnable` (multi-OS-window viewports need platform/renderer backend
  support the GLFW+Diligent combo here doesn't provide). Central node uses
  `ImGuiDockNodeFlags_PassthruCentralNode` so the 3D scene shows through.

## Toon pipeline (fill + outline)

The first real shaders. `Renderer::DrawMesh` runs an **outline** pass then a
**fill** pass over the same mesh, sharing one dynamic constant buffer.

### Matrix convention — declare `row_major` in HLSL, don't transpose

Diligent's `float4x4` is **row-major / row-vector** (`v' = v * M`, and
`WVP = World * View * Proj`). HLSL's default matrix packing is column-major, so
uploading a Diligent matrix as-is would transpose it. Two fixes exist (transpose
on upload, or declare `row_major`); we use **`row_major float4x4` in the cbuffer**
(`toon_common.hlsli`) and upload verbatim — no `.Transpose()`. Shaders then use
`mul(float4(pos,1), g_WorldViewProj)` (row-vector). The C++ `ShaderConstants`
struct must match the `.hlsli` cbuffer field-for-field (2×float4x4 + 4×float4 =
192 B).

Projection: `float4x4::Projection(fovY, aspect, near, far, /*NegativeOneToOneZ=*/false)`
→ `[0,1]` depth for Vulkan/D3D. **No manual Y-flip needed** — Diligent handles
the Vulkan framebuffer Y-flip internally; verified the sphere renders right-side
up. Desktop swap-chain `PreTransform` is identity, so it's skipped (would matter
on rotated mobile displays).

### Winding + culling (verified on Vulkan)

Primitives are wound **CCW as seen from outside** the surface; both PSOs set
`RasterizerDesc.FrontCounterClockwise = True`. **Fill** culls back faces,
**outline** culls front faces. Confirmed correct empirically (sphere renders
solid — not culled/inside-out — and the outline is a thin rim, not a filled
blob). If a future mesh comes out inside-out or the outline covers everything,
the first thing to try is flipping `FrontCounterClockwise` (it's the one
convention that depends on the backend's NDC Y direction and was resolved by
testing, not derivation).

**Left-handed gotcha (bit the cube):** Diligent's `float4x4::Projection` is
*left-handed*, so "outward = front" winding is the **reverse** of the natural
right-handed `u × v = n` face order. The cube generator therefore emits its
corners `(-u-v, -u+v, +u+v, +u-v)` — the reverse of what you'd write from
`u × v = n`. The sphere/torus index pattern `(a, a+stride, a+stride+1, a+1)`
already comes out outward-front (verify at a viewer-facing vertex: its screen
triangle should be CCW). Get this backwards and the shape is culled inside-out.

### Outline = inverted hull, drawn first

`toon_outline.hlsl` extrudes each vertex along its normal in object space by
`g_Outline.w`. Draw order per mesh is **outline first** (cull front → the
enlarged back-facing shell) **then fill** (cull back) on top: the fill's nearer
depth overwrites the shell everywhere except the silhouette rim.

**Hard edges need a second, smoothed normal.** A faceted mesh (per-face normals,
e.g. a cube) would gap at edges — the 3 verts sharing a corner point along
different face normals and extrude apart. Fixed with a dual-normal vertex:
`Vertex::smoothNormal` (`ATTRIB2`) is an *averaged* normal the **outline** VS
extrudes along (corner verts share it → the hull stays closed), while the
**fill** VS still shades with the per-face `normal` (crisp flat faces). For
smooth meshes the two normals are equal. The cube sets `smoothNormal =
normalize(cornerPosition)` (center-outward), which is identical for all verts at
a given corner.

### Wiring details

- **Shared dynamic CB** bound as a `SHADER_RESOURCE_VARIABLE_TYPE_STATIC` var on
  both PSOs (set once via `GetStaticVariableByName(...)->Set()`), updated per
  draw with `MapHelper<ShaderConstants>(ctx, cb, MAP_WRITE, MAP_FLAG_DISCARD)`.
- **`MapHelper.hpp` lives in `Diligent-GraphicsTools`**, not GraphicsEngine —
  must link that target (added to `target_link_libraries`).
- **Shaders load at runtime** via
  `CreateDefaultShaderSourceStreamFactory(TOON_SHADERS_DIR)`; `TOON_SHADERS_DIR`
  is an **absolute path baked in by CMake** (`target_compile_definitions`) so it
  works regardless of CWD. The `.hlsli` `#include` resolves through the same
  factory. Shipping later would copy `assets/shaders` next to the exe and use a
  relative path.

## DiligentFX / HDR post-processing (roadmap #6)

Added `external/DiligentFX` as a submodule pinned to **API256018** — match the
DiligentCore/Tools API version (they're released together; a mismatched FX would
fail to compile against Core/Tools). Enabled via the stubbed
`add_subdirectory(external/DiligentFX)` hook (must come AFTER Core/Tools) + link
the `DiligentFX` target. Builds clean in the Vulkan-only, backends-trimmed setup.

**HDR pipeline (the foundation FX effects need).** The scene no longer renders
straight to the back buffer:
- `BeginFrame` binds an offscreen **RGBA16F** color + D32 depth
  (`CreateOffscreenTargets`, recreated on resize). The toon PSOs' RTV/DSV formats
  are `kHDRFormat` / `kSceneDepthFormat`, not the swap-chain formats.
- `EndScene` resolves HDR → back buffer with a full-screen triangle
  (`tonemap.hlsl`, drawn as `Draw(NumVertices=3)`, no vertex buffer): exposure +
  ACES filmic curve, then leaves the back buffer bound so the UI overlays it.
- Frame order in `main.cpp`: BeginFrame → DrawMesh… → **EndScene** →
  BeginUI/UI/EndUI → EndFrame.

**Gotchas:**
- **sRGB:** the Vulkan backend picks the back-buffer format at runtime. If it's
  NOT an sRGB format, the tone-map shader must encode sRGB itself (else mid-tones
  crush). `Init` sets `outputSRGB` by testing the format; the shader branches on
  it. An sRGB back buffer gets hardware encoding, so output linear there.
- **Resolve resources:** the tone-map PSO binds the HDR color as a MUTABLE
  texture var + an immutable linear-clamp sampler (combined-sampler name
  `g_HDRColor`). On resize the target is recreated, so `BindPostInput` re-points
  the SRB at the new SRV.
- Tone mapping is a **self-contained ACES** shader, not DiligentFX's
  `ToneMapping.fxh` — the DiligentFX shader includes (`SRGBUtilities.fxh`, the
  dual C++/HLSL `*Structures.fxh` with their macro setup) add include-path
  plumbing not worth it for that pass.

## Bloom (DiligentFX `Bloom` via `PostFXContext`, roadmap #1)

The bright toon bands bleed a soft glow. Implemented with DiligentFX's real `Bloom`
effect (compute-ish full-screen-triangle passes: prefilter → downsample → upsample),
all in `core/renderer.cpp` behind the seam. Per-object controls live in
`PostParams` (enable, intensity, threshold, soft-knee, radius); the debug UI drives
them live.

**The `PostFXContext` tax.** `Bloom::Execute` requires a `PostFXContext`, and Bloom
only pulls frame-size / supported-features / a copy helper / `IsPSOsReady()` from it —
it reads **no** depth/motion/camera. But `IsPSOsReady()` only flips true *inside*
`PostFXContext::Execute`, which hard-requires a current **and** previous depth SRV, a
**motion-vector** SRV, and **camera attribs**. Those feed the shared temporal
machinery (reprojected depth, closest motion, blue noise) that TAA/SSR/SSAO use and
Bloom ignores. So `Impl::RunBloom` feeds it scaffolding purely to reach the ready
gate:
- **Depth** — `sceneDepth` now also carries `BIND_SHADER_RESOURCE` (D32 → R32_FLOAT
  SRV on Vulkan). Passed as *both* current and previous (we keep no history).
- **Motion** — a frame-sized `RG16_FLOAT` target cleared to zero once in
  `CreateOffscreenTargets` (never written again; the closest-motion pass `Load`s it
  at full-res pixel coords, so it must be frame-sized, not 1×1).
- **Camera** — a zeroed `HLSL::CameraAttribs` passed as curr+prev; PostFXContext
  makes its own CB. Values are unused by Bloom, so zeros are fine.
This runs blue-noise/reproj/motion compute every frame for nothing — the accepted
cost of the "via PostFXContext" route (chosen deliberately over a self-contained
bloom).

**Compositing is a drop-in.** `Bloom`'s final upsample returns
`SourceColor + Intensity*glow` (see `Bloom_ComputeUpsampledTexture.fx`), so
`GetBloomTextureSRV()` is the **full scene+bloom in HDR**, not just the glow. So
`EndScene` just points the tone-map's `g_HDRColor` at the bloom output instead of the
raw `hdrColor` — **`tonemap.hlsl` is unchanged**. Bloom off → point back at `hdrColor`.

**Gotchas:**
- **`g_HDRColor` must be DYNAMIC, not MUTABLE.** EndScene re-points it every frame
  (scene ↔ bloom output, and the target also changes on resize). Overwriting a
  *mutable* variable's binding trips `VerifyResourceBinding` ("already bound ...
  Overwriting ... is disallowed") — a real in-flight hazard, not pedantry. A dynamic
  variable is the type meant for a per-frame-changing binding (Diligent gives it a
  fresh descriptor each commit), so just `Set` it each frame before commit. This let
  the per-frame "only re-Set when changed" cache and the `BindPostInput` helper go
  away entirely.
- **Threshold < 1.0 by default.** The prefilter blooms on `max(r,g,b)` of the *raw*
  HDR scene (before exposure/tone map). The toon fill maxes near the base color
  (< 1.0, no over-bright), so the library default `Threshold = 1.0` blooms nothing.
  Default is `0.6`. Raise toward/above 1.0 once the scene carries emissive HDR.
- **Ready gate / fallback.** Sync PSO creation (`EnableAsyncCreation = false`), so
  Bloom is live from frame 1 after a one-time compile hitch. `RunBloom` still returns
  `nullptr` (→ resolve the raw scene) while `!IsPSOsReady()`, so no black first frame.
- **Frame index** is incremented and handed to `FrameDesc.Index`; the full-screen VS
  uses `VertexId % 3`, and Bloom's own composite draw uses a literal
  `StartVertexLocation`, so an unbounded index is fine.
- **C++-side FX structs.** `renderer.cpp` includes `BasicStructures.fxh`
  (→ `CameraAttribs`) + `BloomStructures.fxh` (→ `BloomAttribs`) inside
  `namespace Diligent::HLSL` (float4x4/uint resolve from `BasicMath.hpp`). The
  DiligentFX target exposes its root for the full-path includes, but
  `BasicStructures.fxh` does a *bare* `#include "ShaderDefinitions.fxh"`, so
  `CMakeLists.txt` also puts `DiligentFX/Shaders/Common/public` on the include path.
- **Radient disabled.** `set(DILIGENT_NO_RADIENT ON …)` — DiligentFX's GI module is
  unused and fails a clang-cl `noexcept` static_assert. CLion (ToonEngine target only)
  never built it; a full `cmake --build`/CI (the `all` target) does. Nothing links it,
  so disabling is free.
- Bloom's shaders are **embedded** in the DiligentFX lib (a `MemoryShaderSourceFactory`
  via `DiligentFXShaderSourceStreamFactory`), so no shader-file plumbing was needed —
  only the two C++ struct headers above.

**SSAO/DoF next** would reuse this `PostFXContext`, but they *do* read depth + camera +
motion, so those inputs need to become real (actual view/proj camera attribs and, for
motion-dependent effects, real motion vectors) rather than the zero scaffolding Bloom
tolerates.

## SSAO (DiligentFX `ScreenSpaceAmbientOcclusion` via `PostFXContext`, roadmap #1)

Darkens contact/creased areas. Unlike Bloom, SSAO reads the shared `PostFXContext`
inputs for real, so this is where they got filled in. `Impl::RunPostFX` now runs the
context once and then whichever effects are on (SSAO then Bloom), returning an AO SRV +
a bloom SRV to `EndScene`.

**What SSAO needs (and where it came from):**
- **World-space normal G-buffer** (required — `pNormalBufferSRV`; SSAO does not
  reconstruct normals from depth). Added a second scene render target
  (`normalBuffer`, RGBA16F — holds signed normals in [-1,1] directly, no encode).
  The scene pass is now **MRT**: both toon PSOs set `NumRenderTargets = 2`, and
  `toon_fill`/`toon_outline` return a `PSOutput { Color:SV_Target0; Normal:SV_Target1 }`
  writing the normalized world normal. `BeginFrame` binds + clears both targets.
- **Real `CameraAttribs`** (SSAO rebuilds view-space position/normal from depth).
  `SetCamera` now keeps `view`/`proj`/near/far split (not just `viewProj`);
  `FillCameraAttribs` fills the struct — matrices + inverses, `SetClipPlanes`,
  `f4Position` from the view-inverse translation, and crucially
  `fHandness = view.Determinant() > 0 ? 1 : -1` (copied from DiligentFX's own
  `RadientGeometryPass` — get this wrong and AO inverts or vanishes).
- **Depth** — the same `sceneDepth` SRV Bloom already used.
- **Motion** — now a **real** NDC velocity buffer (see *Motion vectors* below), so SSAO
  **temporal accumulation is on by default** (`ResetAccumulation = 0`) and denoises the
  AO without ghosting the spinning objects. UI keeps a temporal toggle.

**Compositing:** SSAO output (`GetAmbientOcclusionSRV`, R8) is *visibility* (1 = open),
so the tone-map multiplies: `hdr *= lerp(1, ao*ao, strength)`. Squaring is a stylized
punch — GTAO is physically restrained (subtle on convex shapes), so raw `ao` barely
reads; `ao*ao` deepens contact shadows while leaving open areas (1.0) untouched.
`g_AO` is a **DYNAMIC** var like `g_HDRColor`; when SSAO is off/not-ready a **1x1 white
texture** (`aoWhite`) is bound so the multiply is a no-op with no shader branch.

**Gotchas / notes:**
- **It's subtle without contact geometry.** The demo added a **ground plane**
  (`MakePlane`) under the trio so there are contact shadows to see; on the original
  floating convex objects SSAO computes almost nothing. Verified correct by
  temporarily returning `float4(ao,ao,ao,1)` from the tone-map — the raw visibility
  buffer showed the torus hole dark, background white (right orientation).
- Default look: `ssaoRadius` (EffectRadius) 1.5 world units, `ssaoStrength` 1.0.
- `EndScene` gates `ssaoStrength` to 0 unless a real AO texture was produced this
  frame (`aoSRV != null`), so the white default never darkens anything.

## Motion vectors (for SSAO temporal / DoF)

The scene now writes a **third MRT target** (`motionVectors`, RG16F) — per-pixel
screen velocity — replacing the zero texture Bloom/SSAO were fed. This is what
temporal effects reproject the previous frame with.

**Convention (get it exactly right or temporal smears):** store
`motion = currNDC.xy − prevNDC.xy` (**NDC** space, current − previous). DiligentFX
stores motion in NDC and applies the NDC→UV `F3NDC_XYZ_TO_UVD_SCALE = (0.5, −0.5)`
itself (SSAO temporal: `prevPixel = currPixel − motion·viewportSize`), so **do not**
pre-scale or flip Y — hand it the raw NDC delta. Derived + checked against
`SSAO_ComputeTemporalAccumulation.fx`; `ComputeReprojectedDepth.fx` reprojects depth
from the camera matrices separately, so the motion texture specifically needs the
**object+camera** motion.

**Plumbing:**
- `ShaderConstants`/`toon_common.hlsli` gained `prevWorldViewProj`. The toon VS
  outputs both clip positions (`CurrClip`, `PrevClip`); the PS writes
  `ComputeMotion() = currClip.xy/w − prevClip.xy/w` to `SV_Target2`. Both toon PSOs
  are now 3-RT.
- Camera motion: `SetCamera` snapshots the old `viewProj` as `prevViewProj` before
  overwriting. Object motion: **`DrawMesh` gained a `prevTransform`** — the app owns
  object history (the seam philosophy; the renderer has no stable object identity).
  `main.cpp` tracks `prevSpinAngle`; the static ground passes its transform twice.
- `DrawMesh` combines them: `prevWVP = WorldFromTransform(prevT) · prevViewProj`.

**Verified** by temporarily routing the motion buffer through the tone-map
(`abs(motion)·120`): static ground + background **black** (zero), spinning objects
show **red/green rotational gradients** (opposite sides move opposite screen
directions). First frame's `prevViewProj` is identity → one frame of bad motion,
harmless (temporal rejects large deltas).

## Depth of field (DiligentFX `DepthOfField` via `PostFXContext`, roadmap #1)

Bokeh depth-of-field: blurs by depth-based circle of confusion, sharp at the focus
plane. Reads scene color + depth and (like Bloom) returns a **full replacement
color**, so it slots into the color chain. Uses the motion vectors for temporal CoC
smoothing (`FEATURE_FLAG_ENABLE_TEMPORAL_SMOOTHING`).

- **Focus/aperture live in `CameraAttribs`**, not the DoF attribs: `FillCameraAttribs`
  sets `fFocusDistance` + `fFStop` from `PostParams` (focal length / sensor keep the
  struct's 50mm / 36mm defaults). The DoF attribs only carry kernel/quality
  (`MaxCircleOfConfusion` = blur size, bokeh ring count/density, temporal factor).
  CoC math is in `DOF_ComputeCircleOfConfusion.fx`.
- **Color chain in `RunPostFX`:** scene → **DoF** → **Bloom**. Each enabled stage reads
  the previous stage's output; `colorOut` ends on the last one that ran (or null →
  raw scene). SSAO stays separate (its AO multiplies in the resolve). So `RunPostFX`
  now returns `(colorOut, aoOut)` instead of `(bloomOut, aoOut)`.
- **Off by default** — it's a strong look; the user opts in. Default focus 10.5 (≈ the
  objects at camera distance 10), f/6, MaxCoC 0.015 — objects sharp, near/far ground
  blurs. Verified visually (cube sharp, bokeh on out-of-focus) + clean run / graceful
  close. f/2 was way too shallow (everything blurred); a higher f-stop widens focus.

## Verifying a Vulkan build

### Link fails: `permission denied` writing `ToonEngine.exe`

If a previous instance is still running, `lld-link` can't overwrite the exe. Kill
it first. If it's a **stuck / elevated** instance that won't die (`Stop-Process`,
`taskkill`, and CIM `Terminate` all return Access Denied), **rename the running
exe aside** — Windows allows renaming a running executable (it's a metadata op) —
then re-link creates a fresh one. The renamed file can't be deleted until that
process finally exits.

### Screenshotting the window (GDI `CopyFromScreen` returns black)

A Vulkan swap-chain doesn't show up in GDI screen-copy — `Graphics.CopyFromScreen`
captures the client area as pure black (the DWM-drawn title bar still shows).
**`PrintWindow(hwnd, hdc, PW_RENDERFULLCONTENT=2)` does capture the rendered
content.** That's how the toon sphere was verified. (Windows also reports the
window smaller than requested under display scaling — cosmetic.)

## Architecture decisions

### Renderer seam: PIMPL, not a virtual `IRenderer`

`core/renderer.h` exposes opaque id-based handles (`enum class : uint32_t`,
`0 = Invalid`) plus a PIMPL `Renderer` class — not an abstract interface with
virtual methods. Reasoning: Diligent already provides runtime backend
selection (Vk/D3D12/GL/Metal) *beneath* the seam, so a second layer of runtime
polymorphism in ToonEngine buys nothing. A backend swap or console port is a
build-time concern — swap in a different `renderer_*.cpp` — with zero virtual
overhead. `src/core/renderer.cpp` is the *only* translation unit allowed to
include a Diligent header or name a `Diligent::` type.

### Vulkan-only, not "Vulkan-first with others linked"

Originally D3D11/D3D12/OpenGL were linked alongside Vulkan "for debugging with
RenderDoc/PIX." Given the compile-time cost (see above) and that nothing
currently uses them, they're disabled by default. Re-enable a specific one
only when there's a concrete reason (e.g. actually reaching for RenderDoc).

## History

- **2026-07-06** — Pivoted from a from-scratch OpenGL 4.1 engine (see `main`
  branch history) to Diligent Engine + Vulkan on the `diligent` branch.
  Verified first light: window + Vulkan device + swap chain + clear loop,
  running on an NVIDIA RTX 3080.
- **2026-07-08** — Added the renderer seam (`core/renderer.h/.cpp`); `main.cpp`
  became Diligent-free. Added DiligentTools + Dear ImGui behind the seam;
  fixed the C-language, ShowDemoWindow-link, and ImGui-ordering issues above;
  disabled D3D11/D3D12/OpenGL to cut build time.
- **2026-07-08** — Toon pipeline first light: banded (cel) fill +
  inverted-hull outline on a spinning UV sphere, with live ImGui controls.
  Added `core/math.h` (Diligent-free vectors), `core/primitives.{h,cpp}`
  (UV-sphere generator), and `assets/shaders/` (HLSL). Extended the seam with
  `CreateMesh` / `SetCamera` / `SetToonParams` / `DrawMesh`. Verified the render
  via `PrintWindow` capture on the RTX 3080 (see *Toon pipeline* above for the
  matrix/winding/outline conventions nailed down here).
- **2026-07-09** — Toon pipeline refinements: multi-object scene (sphere + cube
  + torus) with per-object `Material` (replaced global `SetToonParams`;
  `DrawMesh` now takes a material, light is global via `SetLight`). Added
  `MakeCube`/`MakeTorus` and the dual-normal outline (`Vertex::smoothNormal`)
  so the cube's hard edges outline cleanly. Nailed the left-handed winding
  gotcha (cube corners) above. Verified all three shapes on the RTX 3080.
- **2026-07-09** — imgui docking (roadmap #5): see-through dock space, debug
  panel docked left by default, driven from `main.cpp` and guarded on
  `IMGUI_HAS_DOCK`. Required checking out the nested imgui submodule to upstream
  ocornut/imgui's `docking` branch — the DiligentGraphics fork's docking branch
  is ancient/incompatible. See "Docking" above for the checkout + the
  `git submodule update` reversion caveat.
- **2026-07-09** — DiligentFX (roadmap #6, in progress): added the submodule
  (API256018) + build wiring, and stood up the HDR pipeline — offscreen RGBA16F
  scene target resolved to the back buffer by an exposure + ACES tone-map pass
  (`Renderer::EndScene`, `tonemap.hlsl`). Foundation for DiligentFX bloom/SSAO
  next. See "DiligentFX / HDR post-processing" above.
- **2026-07-10** — Tooling: migrated the IDE from VS Code to **CLion**. Removed
  `.vscode/` (tasks/launch/settings/c_cpp_properties) and added
  **`docs/clion-setup.md`** (Visual Studio toolchain + CMake presets + debug). The
  CLion VS toolchain sources the VS Developer environment automatically, so
  `scripts/vsenv.ps1` is now only for command-line / CI builds. Trimmed `CLAUDE.md`
  to a lean, forward-only roadmap — completed items live here in the archive.
  (Later split into per-platform `docs/clion-setup-{windows,linux,macos}.md`.)
- **2026-07-10** — **Bloom** (roadmap #1): wired DiligentFX's `Bloom` via
  `PostFXContext` onto the HDR target. `Impl::RunBloom` in `EndScene` prepares +
  executes PostFXContext (fed scene depth as curr/prev, a zero motion-vector target,
  a zeroed camera — scaffolding it needs to reach `IsPSOsReady()` but Bloom never
  reads) then Bloom over `hdrColor`; the tone-map then resolves Bloom's output, which
  already holds scene+glow (so `tonemap.hlsl` is unchanged). `PostParams` + UI gained
  bloom controls; default threshold is 0.6 (the LDR toon fill never exceeds ~0.9).
  Also `DILIGENT_NO_RADIENT ON` (broke the full `cmake --build`) and a new include
  dir for DiligentFX's C++-side `*Structures.fxh`. Built clean (clang-cl) and ran
  with zero Diligent validation errors. See "Bloom" above.
- **2026-07-10** — Bloom bugfixes + cleanup. (1) `g_HDRColor` MUTABLE→**DYNAMIC** —
  the per-frame scene↔bloom re-`Set` was tripping `VerifyResourceBinding`; killed the
  cache + `BindPostInput`. (2) **ImGui shutdown order** — `ImGui_ImplGlfw_Shutdown()`
  before context destroy, else `abort()` on window close ("Forgot to shutdown Platform
  backend?"); also built the ImGui PSO with depth = `TEX_FORMAT_UNKNOWN` (kills a
  per-frame DSV-mismatch warning) and `WaitForIdle()` before teardown. Verified via a
  close test (exit 0). (3) Added section dividers to `renderer.cpp`, plus
  **`docs/style-guide.md`** and a **`.claude/skills/tidy-cpp`** skill for future
  cleanups. See the Dear ImGui + Bloom "Gotchas" above.
- **2026-07-10** — **SSAO** (roadmap #1): DiligentFX `ScreenSpaceAmbientOcclusion` via
  the shared `PostFXContext`, which now gets *real* inputs (unlike Bloom): a
  world-space **normal G-buffer** (scene pass is now MRT; toon shaders write
  `PSOutput` color+normal) and real **`CameraAttribs`** (`FillCameraAttribs`, handness
  from the view determinant). Motion stays zero, so SSAO temporal accumulation is off
  by default (would ghost the spinning scene). AO (visibility) composited in the
  tone-map as `hdr *= lerp(1, ao*ao, strength)`; `g_AO` dynamic, 1x1-white default when
  off. Added a **ground plane** (`MakePlane`) so contact shadows are visible; verified
  the raw AO buffer (torus hole dark, bg white → correct orientation), ran clean, close
  exits 0. `RunBloom` generalized to `RunPostFX`. See "SSAO" above.
- **2026-07-10** — **Motion vectors** (unblocks SSAO temporal + DoF): the scene now
  writes a real NDC velocity buffer (3rd MRT target) instead of a zero texture. Toon
  shaders difference `currClip`/`prevClip`; `DrawMesh` gained a `prevTransform` and
  `SetCamera` snapshots `prevViewProj`. SSAO temporal accumulation now **on by
  default** (denoises without ghosting). Convention: `currNDC - prevNDC`, raw (the lib
  applies the NDC->UV (0.5,-0.5) scale). Verified the motion buffer directly (static =
  black, spinning = rotational red/green). See "Motion vectors" above.
- **2026-07-10** — **Depth of field** (roadmap #1): DiligentFX `DepthOfField` via the
  shared context, using the new motion vectors for temporal CoC smoothing. `RunPostFX`
  became a color chain (scene → DoF → Bloom, returns `colorOut`); focus/aperture set
  in `CameraAttribs` from `PostParams`. Off by default (strong look); tuned defaults
  (focus 10.5, f/6). Verified: clean run, graceful close, visible depth blur (cube in
  focus, bokeh elsewhere). See "Depth of field" above.
