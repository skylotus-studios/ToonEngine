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
`docs/clion-setup-windows.md`). For a plain shell (no CLion), open a **Developer
PowerShell for VS 2022** (Start Menu shortcut, installed by Visual Studio itself) or
import it inline:

```powershell
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$devCmd = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"
cmd /c "`"$devCmd`" -arch=x64 -host_arch=x64 -no_logo && set" | ForEach-Object {
    if ($_ -match '^([^=]+)=(.*)$') { Set-Item "Env:$($matches[1])" $matches[2] }
}
```

There used to be a `scripts/vsenv.ps1` wrapping exactly this. It was **removed
deliberately**: vestigial from an earlier VS Code-based workflow this repo no longer uses,
and CLion needs none of it. Don't recreate it; inline the snippet above the one time a fresh
configure genuinely needs it from a bare shell. (A prior session did recreate it, from the
doc references below that still described it as if it existed. This section is what should
have been fixed instead. See "History" for the full correction.)

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

### `'lib.exe' is not recognized` — editing `CMakeLists.txt` needs the VS env too, not just a fresh configure

The `verify` skill's claim that "ninja caches tool paths from the last successful configure,
so the VS Developer environment doesn't need importing for an incremental build" is only
true when `CMakeLists.txt` itself doesn't change. Adding a new source file to
`add_executable(...)` (as scene serialization's `serializer.cpp` did) forces a CMake
reconfigure as the first step of the next `cmake --build`, and DiligentTools' "Combining
libraries" step invokes `lib.exe` (the MSVC librarian) by bare name, relying on `PATH` at
*build* time, not a path resolved once at configure time. From a bare shell with no VS env
imported, that step fails with `'lib.exe' is not recognized as an internal or external
command`. A plain compile-error grep won't find it; the real failure is
`ninja -t restat ... failed with: ninja: error: failed recompaction: Permission denied`-
adjacent noise if something else is *also* touching the build dir, or a clean
`FAILED: external/DiligentTools/Debug/DiligentTools.lib` if not. Either way, the fix is the
same VS-env-import snippet above — chained into the *same* shell invocation as the
`cmake --build`, since environment variables set in one tool call don't persist to the
next one here. A source-only change (no `CMakeLists.txt` edit) still doesn't need it, per
the skill.

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

### LSP (clangd) setup

`CMAKE_EXPORT_COMPILE_COMMANDS` is now set explicitly in `CMakePresets.json`'s
`windows-base` preset. Previously it was only emitted implicitly by CLion's own indexing,
so a pure command-line configure never produced one. A root `.clangd` points
`CompileFlags.CompilationDatabase` at `build/windows-debug`; clangd auto-detects the
`clang-cl` driver from the compile command (`--driver-mode=cl`) on its own, no
`--query-driver` needed. Verified with `clangd --check=<file>` on both `renderer.cpp` (the
deepest include graph: DiligentCore/Tools/FX + ImGui) and `main.cpp`: preamble + AST build
clean, zero real diagnostic errors (the only "errors" `--check` reports are its own
`ExtractFunction` tweak self-test failing on a `break`/`continue`, a clangd-internal
artifact, not a code problem).

### Implicit `cmake --build` reconfigure can silently under-apply a `CMakeLists.txt` edit

Editing `CMakeLists.txt` forces `cmake --build` to reconfigure as its first step (see
above) — but that *implicit* reconfigure isn't always fully reliable: it's been observed to
pick up a source-file-list change while silently dropping other same-sitting edits to the
same file (new `target_include_directories`/`target_compile_definitions`/
`target_link_libraries` calls), with no error at configure time — the build just fails later
on a missing header or symbol that *should* have been reachable. Diagnosis: grep the
generated `build.ninja` for content unique to the edit; if it's genuinely absent despite
`CMakeLists.txt` on disk having it (confirm with a fresh `Read`), an **explicit
`cmake --preset windows-debug`** (not `--build`) reconfigure resolves it — matches this
file's existing "explicit reconfigure > wipe" guidance below, now with a concrete incident
behind it. See "Input system" below for the full incident.

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

### Window icon: the taskbar needs an embedded resource, `glfwSetWindowIcon` alone isn't enough

`core/renderer.cpp`'s `SetWindowIcon` (decodes an image via DiligentTools' `Image.h`, calls
`glfwSetWindowIcon`) sets the icon via `WM_SETICON` on the live window; the title bar
updating immediately is real-time evidence it works. It does **not** touch the .exe's own icon
resources. GLFW's Win32 backend registers the window *class* icon by looking for a resource
named exactly `GLFW_ICON` in the executable (`external/glfw/src/win32_window.c`,
`createNativeWindow`), falling back to the generic system `IDI_APPLICATION` icon if it
can't find one. That fallback is what the taskbar, Alt-Tab, and shell kept showing even
after `SetWindowIcon` had already fixed the title bar — a title-bar-only fix reads as "half
done," not "wrong," so it's an easy thing to ship and only notice later.

Fix: embed a `GLFW_ICON` resource. `src/icon.rc.in` (one line: `GLFW_ICON ICON
"@TOON_ICON_ICO_PATH@"`) is `configure_file`'d by `CMakeLists.txt` into
`${CMAKE_CURRENT_BINARY_DIR}/icon.rc` and attached via `target_sources`. Windows-only,
guarded on `WIN32`; needs `enable_language(RC)` called once near the top of the file.
`assets/icon.ico` wraps the existing `assets/icon.png` in a minimal ICO container: a
22-byte ICONDIR + ICONDIRENTRY header prepended directly to the PNG's own bytes. A single
PNG-compressed frame is valid ICO content, supported since Windows Vista (no need for the
older multi-resolution uncompressed-BMP format); built by hand in PowerShell since neither
ImageMagick nor a working Python was available in this environment. The runtime
`SetWindowIcon` call stays. The two are complementary, not redundant: the resource fixes
the class/taskbar icon, the runtime call still explicitly covers the per-window instance.

Verified by screenshot, not just a clean build: the taskbar (`Shell_TrayWnd`, a real
top-level window distinct from ToonEngine's own) was captured with ordinary
`Graphics.CopyFromScreen` — unlike ToonEngine's own client area, the taskbar isn't a Vulkan
swap-chain surface, so plain GDI screen-copy works on it (the swap-chain-black issue only
applies to windows actually presenting through DXGI/Vulkan). The captured taskbar button
showed the real icon, not the generic fallback.

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

### Docking — own `external/imgui` submodule, overriding DiligentTools' vendored one

Docking (`ImGuiConfigFlags_DockingEnable`, `DockSpaceOverViewport`, DockBuilder)
lives on imgui's separate `docking` branch. DiligentTools vendors its own imgui
as a nested submodule (`ThirdParty/imgui`, pinned to the DiligentGraphics fork's
`diligent_v1.92.1` tag) — non-docking. Its own **`docking` branch is ancient**: a
pre-1.80 imgui (backends still under `examples/`, wrong API), incompatible with
DiligentTools' modern (1.92.1) `Diligent-Imgui` integration. Its `backends/` dir
doesn't even exist, so the build fails on `imgui_impl_glfw.cpp`. Don't use it.

**Durable fix: vendor our own docking-branch imgui and override the path,
rather than touch DiligentTools at all.** `DiligentTools/ThirdParty/CMakeLists.txt`
only defaults `DILIGENT_DEAR_IMGUI_PATH` `if (NOT DILIGENT_DEAR_IMGUI_PATH)` — a
supported override hook. So:

- ToonEngine has its own top-level `external/imgui` submodule, `branch = docking`,
  pinned to upstream **ocornut/imgui**'s docking tip `a23e9fb1b` (1.92.9-WIP) —
  same 1.92 minor as DiligentTools' pinned 1.92.1, modern `backends/` layout,
  `Diligent-Imgui` compiles against it clean.
- `CMakeLists.txt` sets `DILIGENT_DEAR_IMGUI_PATH` to `external/imgui` (`CACHE
  PATH ... FORCE`, grouped with the other Diligent cache vars) *before*
  `add_subdirectory(external/DiligentTools)`, so DiligentTools' own default never
  applies. DiligentTools is never forked or patched — it builds from its pristine
  upstream state; its own vendored `ThirdParty/imgui` submodule is initialized but
  unused.
- **Fully durable, verified end-to-end:** a plain `git submodule update --init
  --recursive` (the exact command that used to silently revert the old manual
  checkout back to non-docking) now leaves `external/imgui` pinned at `a23e9fb1b`
  and `external/DiligentTools` clean — no more `m external/DiligentTools`
  dirtiness, no manual steps on a fresh clone. Chosen over forking DiligentTools
  itself specifically to avoid rebasing a fork every time DiligentTools is bumped
  (it moves often — was at `API256018-28-ge637cfc` when this landed).
- Superseded the original 2026-07-09 approach (manually checking out
  DiligentTools' *nested* `ThirdParty/imgui` submodule to the same upstream
  commit, uncommitted, reverted by any `submodule update --recursive`) — see that
  changelog entry.
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
struct must match the `.hlsli` cbuffer field-for-field (4×float4x4 + 4×float4 =
320 B — grew as motion vectors and the normal matrix were added).

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

### Non-uniform scale (roadmap #1): inverse-transpose normal matrix + world-space outline

`Transform::scale` may now be non-uniform. Two things break under non-uniform scale,
both fixed by **one** added cbuffer matrix, `g_NormalMatrix`:

- **Shading / G-buffer normals.** `n * g_World` is correct only for rotation + *uniform*
  scale; non-uniform scale skews the normal — and the fill shading, the SSAO normal
  G-buffer, and SSR all read it, so all three go wrong at once. Fix: the standard
  **inverse-transpose** normal matrix `(World⁻¹)ᵀ`, built CPU-side in `DrawMesh` as
  `world.Inverse().Transpose()` and uploaded as `g_NormalMatrix`. Both toon VS shade with
  `mul(float4(N,0), g_NormalMatrix).xyz`. (Row-vector convention: the normal transform is
  `n * (M⁻¹)ᵀ` — same formula as column-vector. The 4×4's upper-left 3×3 is the true 3×3
  inverse-transpose even with translation present, so a full float4x4 is fine — no float3x3
  cbuffer-packing headaches.)
- **Outline width.** The inverted hull extruded a constant amount in *object* space, so
  non-uniform world scale stretched the shell (thick on the scaled axis). Fix: extrude a
  constant **world-space** width. To reuse the existing WVP path (and leave the
  motion-vector plumbing untouched), the offset is still applied in object space: take the
  true world normal `nWorld = normalize(smoothN * g_NormalMatrix)`, then map a world-space
  step back to object space through `world⁻¹` — which is exactly the **3×3 transpose of
  `g_NormalMatrix`** (since `g_NormalMatrix` is `World⁻ᵀ`). So
  `inflated = pos + mul(nWorld, transpose((float3x3)g_NormalMatrix)) * g_Outline.w`, fed to
  the same `g_WorldViewProj` / `g_PrevWorldViewProj`.

**One matrix, zero regression.** For scale = 1, `(World⁻¹)ᵀ`'s 3×3 is just the rotation
`R`, so the normal line reduces to `n*R` (= the old `n*g_World` for a pure rotation) and the
outline `inflated` reduces *algebraically* to `pos + smoothN * g_Outline.w` (the old
object-space extrude). Every existing object is scale = 1, so the scene stays
pixel-identical; only a non-uniformly-scaled object differs, and there it's correct. Hence
no new `viewProj`/`prevWorld` fields and no touch to the motion convention.

**cbuffer grew 256 → 320 B** (`g_NormalMatrix` inserted after `g_World`); the C++
`ShaderConstants` mirror must match field-for-field — a size mismatch trips a Diligent
validation error immediately, which is how the layout was confirmed clean.

**Demo:** the scene's sphere carries a fixed non-uniform `Transform::scale` (`{1.5, 0.8,
1.0}`) → a spinning **ellipsoid**, the textbook normal-matrix test (`main.cpp`'s `Object`
gained a per-object `scale`). Verified on the RTX 3080 via `PrintWindow`: the ellipsoid's
cel bands follow the true stretched surface (not skewed toward the wide axis) and its
outline stays a uniform rim; cube/torus visibly unchanged.

### Per-object outline tuning (roadmap #1)

The inverted-hull outline was always per-object *capable* — `Material::outlineColor` /
`outlineWidth` flow through `DrawMesh` into `g_Outline` — but `main.cpp` overwrote both
from one global `style` every frame, so every object shared one line. Made it genuinely
per-object, **app-side only** (no seam/shader change):

- Each `Object` owns its outline (sphere: thin dark-red rim; cube: bold near-black edge;
  torus: dark-bronze line) via its `Material{ baseColor, outlineColor, outlineWidth }`.
- The draw loop stopped stomping outline color/width; it now overlays only the
  genuinely-global bits (band count, ambient, SSR gloss) onto a **per-draw copy** of the
  object's material — so the object's stored outline stays the editable source of truth.
- A single global `outlineScale` (default 1.0) multiplies every object's width together
  (`m.outlineWidth = obj.material.outlineWidth * outlineScale`), for dialing the whole
  scene's line weight without losing per-object ratios.
- UI: an **"Objects"** section (per object: base color + outline width + outline color,
  `PushID(i)` so labels don't collide) plus a global **"Outline width ×"** slider; the old
  single global outline width/color controls are gone. `Object` gained a `name` for labels.
- The ground keeps `outlineWidth = 0` — the per-object *disable* case.

Verified via `PrintWindow`: three visibly distinct outlines (width + color), live-tunable,
clean run. Bands/ambient stay global by design (a scene-wide shading look).

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

## TAA (DiligentFX `TemporalAntiAliasing` via `PostFXContext`, roadmap #1)

Temporal anti-aliasing: jitter the camera sub-pixel each frame, accumulate over time
via the motion vectors. Reads only the scene color (context supplies motion+camera);
returns the resolved frame (`GetAccumulatedFrameSRV`). First in the color chain
(scene → **TAA** → DoF → Bloom) so the downstream effects see the clean image.

**Jitter coupling (the fiddly part):** the *scene* must render with the jittered
projection, and the jitter recorded so PostFX can undo it.
- `taa->GetJitterOffset()` returns the NDC sub-pixel offset (Halton, 0 until ready).
- `SetCamera` applies it with `TemporalAntiAliasing::GetJitteredProjMatrix(proj, j)`
  (only when `post.taa`), stores `frameJitter`, and everything downstream (viewProj,
  DrawMesh, depth) renders jittered.
- `FillCameraAttribs` writes `frameJitter` to `f2Jitter`; PostFX reprojection removes
  it (`ComputeReprojectedDepth.fx`: `+= NDC_TO_UVD.xy * f2Jitter`).
- **Frame order matters:** `main.cpp` now calls `SetPostParams` **before** `SetCamera`
  so the jitter decision sees `post.taa`.
- Motion vectors are computed from the jittered clips (include the sub-pixel jitter
  delta) — technically imprecise, but it's < 1px and TAA tolerates it; not worth
  threading an un-jittered WVP through the cbuffer.

**Off by default** — it softens the crisp cel edges + outlines that define the toon
look. Verified: clean run, graceful close, and (the real test) the spinning objects
**don't ghost** — edges anti-alias without smearing, confirming motion+jitter are
right (wrong motion/jitter would trail badly).

## SSR (DiligentFX `ScreenSpaceReflection` via `PostFXContext`, roadmap #1)

Screen-space reflections: ray-marches the depth buffer to reflect the scene in smooth
surfaces. Reads color + depth + **world-space normals** + a **roughness** input +
motion; returns reflection *radiance* (`GetSSRRadianceSRV`, `rgb` = radiance, `a` =
hit confidence).

- **Roughness rides in the normal buffer's `.w`** — no 4th MRT. The toon PS write
  `float4(N, roughness)`; `Material::roughness` → `g_Params.z` → `.w`. SSR reads the
  *same* normal texture as both `pNormalBufferSRV` (`.xyz`) and `pMaterialBufferSRV`,
  with `RoughnessChannel = 3` selecting `.w` (its extract pass dots the RGBA with a
  channel selector, so any channel works). `IsRoughnessPerceptual = 1` (we store
  artist roughness). `RoughnessThreshold` (0.2) gates rays: only smooth pixels reflect.
- **Simplified composite** — the "correct" SSR composite is full PBR specular IBL
  (BRDF LUT + env map + per-pixel F0), which a toon renderer has none of. So the
  tone-map just adds `ssr.rgb * ssr.a * strength` (like AO), via a `g_SSR` dynamic
  input (1x1 **black** default when off). No fresnel; good enough to mirror the scene.
- `RunPostFX` now outputs `(colorOut, aoOut, ssrOut)` — SSR, like SSAO, is a separate
  composited texture, not part of the color chain.

**Gotchas / honesty:**
- **It's subtle on this scene.** SSR only reflects what's *on screen*. A flat ground
  reflects mostly *up* → the sky/background, which has no geometry → rays miss →
  black. Verified working by temporarily visualizing the radiance buffer + raising
  `RoughnessThreshold` to 1.0 (then everything reflected). To make it *visible* by
  default when enabled, the scene objects are lightly glossy (`roughness 0.15`) so
  they catch reflections of the ground/each other; the ground is smooth (0.05) but
  its reflections mostly escape to the sky. A vertical mirror or floating objects over
  a mirror floor would show it far better.
- **Off by default** (opt-in) + UI (enable, strength).

## glTF model loading (Phase A: real assets)

Load + cel-shade real glTF/GLB models via **DiligentTools' `GLTF::Model`** (target
`Diligent-AssetLoader`), not a hand-rolled loader — it owns the GPU vertex/index buffers +
textures; we draw its primitives with our own toon cel-fill PSO (no DiligentFX / PBR
renderer). Seam: opaque `ModelHandle` + `LoadModel(path)` / `DrawModel(handle, xform,
prevXform, style)`, all in `renderer.cpp`. `main.cpp` loads `helmet.glb` (path baked via
`TOON_MODELS_DIR`) and draws it spinning; the model shares the toon `ShaderConstants` CB +
`CelShade`/motion helpers with the procedural fill via `model_fill.hlsl` +
`model_outline.hlsl` (vertex `pos/normal/uv`). The outline extrudes along the *shading*
normal (models carry no smooth normal) so smooth surfaces stay closed and hard creases may
gap slightly; `DrawModel` draws outline (cull FRONT, `FrontCounterClockwise = False`) then
fill per primitive, like the procedural `DrawMesh`.

**Two vertex formats coexist** (glTF has no "smoothNormal"): procedural
`pos/normal/smoothNormal` (inverted-hull outline), model `pos/normal/uv` (textured fill). The
model PSO's input layout is a hardcoded `LayoutElement[]` (pos@0 / normal@12 / uv@24, stride
32) matching the loader's buffer-0 packing from `ModelVertexAttribs` (POSITION/NORMAL/
TEXCOORD_0 all → buffer 0). CMake links `Diligent-AssetLoader` + `Diligent-TextureLoader`
(both built by the DiligentTools subdir; previously only reachable transitively via DiligentFX).

**Gotchas — each cost a debugging cycle:**
1. **`ModelCreateInfo::VertBufferBindFlags` defaults to `BIND_NONE`** (only `IndBufferBindFlags`
   defaults to `BIND_INDEX_BUFFER`). Without `ci.VertBufferBindFlags[0] = BIND_VERTEX_BUFFER`
   the vertex buffer isn't bindable and the draw faults.
2. **Loader textures are `Texture2DArray`, not `Texture2D`** (one layer each in the non-atlas
   path — for the PBR renderer's array binding). The shader must declare `Texture2DArray` and
   sample slice 0 (`g_Albedo.Sample(s, float3(uv, 0))`), and the untextured fallback must be a
   **2D-array** white (a plain-2D fallback trips `ValidateResourceViewDimension`: *"dimension
   of resource view ... is Texture 2D Array, but ... expected ... Texture 2D."*).
3. **`GetVertexBuffer` / `GetIndexBuffer` / `GetTexture` take `(idx, device, context)`** and
   materialize lazily — pass device + context or they can hand back non-materialized resources.
4. **A binding-validation failure (e.g. #2) manifests as a HANG, not a clean error** — the
   assertion blocks, and Diligent logs it to `std::cout` (buffered → **lost when you force-kill
   a hung app**). Diagnosis trick: `std::cout.setf(std::ios::unitbuf)` at `main()` start flushes
   each message so the real error survives the kill; and the Windows Application event log
   distinguishes hang from crash (a crash logs an *Application Error*, a hang doesn't).
5. **Winding is flipped vs our own primitives.** glTF winds front faces **CCW in a
   RIGHT-handed** space; our projection is **left-handed**, which flips screen-space winding,
   so the model's outward faces come out **CW**. The model PSO needs
   `FrontCounterClockwise = False` (the OPPOSITE of our procedurally-generated primitives,
   which are authored CCW-front for the LH setup). With `True` the outward faces cull and you
   see straight *through* the model to its textured inner surface (looks translucent, not a
   normals bug — it's culling).

**Draw:** iterate `Model.Scenes[DefaultSceneId].LinearNodes` → nodes with `pMesh` →
`Primitives`; world = `ComputeTransforms(...).NodeGlobalMatrices[node.Index] * objectWorld`;
per primitive bind `Materials[prim.MaterialId].Attribs.BaseColorFactor` + base-color texture
(`GetTextureId(DefaultBaseColorTextureAttribId)` → `GetTexture(...)->GetDefaultView(SRV)`),
`DrawIndexed(IndexCount, FirstIndexLocation = GetFirstIndexLocation()+prim.FirstIndex,
BaseVertex = GetBaseVertex()+prim.FirstVertex)`. Verified: helmet renders cel-shaded with its
albedo, SSAO/bloom apply, motion from the spin, clean exit. (Vendored API 256018/019.)

## Scene graph (Phase B)

`core/scene.{h,cpp}` — an entity tree replacing `main.cpp`'s hardcoded array. `Scene` is a
flat `std::vector<Entity>` with **parents always before children**, so one forward pass
composes world matrices. `Entity` holds a name, a `parent` index (-1 = root at index 0), an
optional local `Transform`, cached `worldMatrix` / `prevWorldMatrix` (plain `Mat4`), and a
renderable — a `MeshHandle` (primitive) OR a `ModelHandle` (glTF) — plus a `Material`
(primitive material / model tint + style). Ops: `EnsureSceneRoot`, `AddEntity`,
`UpdateWorldTransforms`, `DestroyScene`.

**Where the 4x4 math lives (the design call).** The hierarchy needs compose/inverse math;
`math.h` deliberately stops at vectors. Per build-on-Diligent:
- **`scene.cpp` is a Diligent-using engine TU** (the 2nd after `renderer.cpp`) — it uses
  Diligent's `float4x4` for composition (no hand-rolled 4x4 math).
- **`math.h` gained a plain, math-free `Mat4`** (16 floats, row-major = Diligent's layout)
  as the seam vocabulary for a composed world transform; `scene.cpp` / `renderer.cpp`
  convert to/from `float4x4` at the boundary (a straight element copy). `scene.h` +
  `main.cpp` stay Diligent-free.
- The seam grew **`Mat4` overloads** of `DrawMesh` / `DrawModel`; the existing `Transform`
  overloads now just build a world `Mat4` and delegate.

**Composition** (row-vector, v' = v·M): `worldMatrix = local * parentWorld` — local FIRST,
then parent (the reverse of glm's column-vector `parentW * local`). `LocalFromTransform` in
`scene.cpp` MUST match `renderer.cpp`'s `WorldFromTransform`
(`Scale·Rx·Ry·Rz·Translation`). `UpdateWorldTransforms` snapshots
`prevWorldMatrix = worldMatrix` first, so **motion vectors come from the scene's
double-buffered world matrices** — no per-object prev-angle bookkeeping in `main.cpp` anymore.

**Demo:** a small satellite entity is parented to the (spinning) cube and orbits it,
inheriting the cube's world transform. **Deferred to the editor step (item 5):**
`ReparentEntity` / `MoveEntityAsSibling` / `DuplicateEntity`, the topo-reorder helpers,
world-preserving reparent (needs `float4x4.Inverse()` + a TRS decompose), and
`Scene::selected` — all present in `ToonEngineOld/src/scene/scene.cpp` as the port reference.

## Editor camera + input (Phase B, item 4)

**Editor camera** — orbits a movable `pivot` at `distance`, yaw/pitch. Rather than port the
old glm camera (right-handed `lookAt` / `perspective` — would mirror the view in our LH
pipeline, and Diligent Core has no ready lookAt), the seam `Camera` gained a `pivot` and
`SetCamera` prepends `Translation(-pivot)` to the proven LH turntable view. Controls in
`core/camera.{h,cpp}` (a Diligent-using TU, like scene.cpp): orbit (yaw/pitch), zoom
(geometric on `distance`), pan/fly (move the `pivot` along the camera's world basis), focus
(set `pivot`). The **basis** is derived from the SAME Diligent `RotationX/Y` matrices the
view uses — `worldAxis = viewAxis · RotationX(-pitch) · RotationY(-yaw)` = row k of that
inverse — so it's correct-by-construction, with no hand-guessed LH signs.

**Input** — `core/input.{h,cpp}` (**superseded 2026-07-12** by `core/input/`, a full
action-map system — see "Input system" below): GLFW polling (mouse buttons + cursor delta +
keys) + a scroll callback (installed in `Input::Init` **before** `Renderer::InitUI`, so
ImGui's GLFW backend *chains* it instead of overwriting) + a **capture gate** (`SetCaptured`,
fed each frame from ImGui's `io.WantCaptureMouse/Keyboard` — a harmless 1-frame lag) so
dragging over the debug panel doesn't move the camera. `g_lastX/Y` advance every frame even
when captured, so releasing capture never yields a delta jump. Bindings (`main.cpp`):
right-drag orbit (+ WASD/QE fly), middle-drag pan, scroll zoom, F focus (origin for now).

**Deferred (still, after "Input system" below):** F-focus on the *selected* entity (needs
the editor selection — unrelated to the input layer); an in-editor rebind UI (bindings are
rebindable today via `assets/input.json`, just not from a panel); the event queue / char
callback / file-drops (`input_event.h`'s `std::span`-based stream is C++20 — this project
targets C++17 — and has no consumer yet; first one is the asset-browser roadmap item).
**Note:** the drag *directions* (orbit/pan signs) match common editor conventions but are
only verifiable interactively — any inverted axis is a one-line sign flip in `camera.cpp`.

## Editor UI (Phase B, item 5 — part 1)

**Panels (`main.cpp`, ImGui — exempt from the seam).** Three docked windows around the
pass-through scene: a **Scene Hierarchy** (left), an **Inspector** (top-right), and the
existing **Debug** panel (bottom-right), laid out once via `DockBuilder` (guarded by
`dockLayoutBuilt` + `#ifdef IMGUI_HAS_DOCK`; splits are left 0.20 → right 0.34 → right-up
0.55). A trimmed **dark theme** (`StyleColorsDark` + rounding + a muted-blue accent) is
applied once after `InitUI` — pure style-struct edit, no backend state.

**Hierarchy = a flat list, not a real `TreeNode`.** It iterates `scene.entities` in vector
order and indents each row by its parent-chain depth (`ImGui::Indent(depth*16)`). This reads
as a tree **only because the vector is kept in pre-order** (parents immediately followed by
their subtree). The editor mutations all topo-reorder to preserve that; the one thing that
can break it is the **scripted scene build** (`AddEntity` is a plain append), so `main.cpp`
creates the satellite **right after** its parent cube (not last) to keep the initial scene
pre-order. Selection is a single `int Scene::selected` (click toggles it off; the cube is
selected on launch so the Inspector isn't empty).

**Deferred-mutation pattern (load-bearing).** Every structural edit reorders `entities` and
invalidates indices, so the hierarchy loop must **never** mutate mid-iteration — it only
*records* one pending op (add-child / duplicate / delete) + one pending drag-drop, then
applies them **after** the loop and fixes up `selected`. Drag-drop payload is the int index
(`"TOON_ENTITY_IDX"`); the drop **zone** is picked by cursor-Y within the row — top/bottom
quarter = sibling before/after, middle = make-child (the root only accepts children).

**Scene mutations (`core/scene.{h,cpp}`, plain index/vector work — no Diligent).**
`IsAncestorOrSelf`, `AddChildEntity`, `DeleteEntity` (whole subtree via a kill-set fixpoint),
`DuplicateEntity` (clones the subtree as a sibling — copies mesh/model **handles** + material,
so models stay shared, no re-load), `ReparentEntity` / `MoveEntityAsSibling`, plus the topo
helpers (`BuildChildrenList` / `TopoOrderFromChildren` / `ApplyReorder` — a pre-order DFS that
also patches parent indices + `selected`). Adapted from `ToonEngineOld/src/scene/scene.cpp`.
**Reparent is "simple" for now** — it sets the parent and keeps the *local* transform, so the
object jumps into the new parent's frame; **world-preserving** reparent needs the decompose
(part 2). Cycle-guarded (`IsAncestorOrSelf`), root never reparented/deleted/duplicated.

**Inspector.** Name (`InputText` over a copy-to-buffer-then-read-back — fine since the string
only changes via this widget), Transform (`DragFloat3` position / **rotation shown in
DEGREES**, converted to/from the stored radians / scale — only for a non-root entity with a
transform), Material (base + outline color, outline width, roughness — only for renderables).
The rotation display fights the spin animation for spinning entities (expected — it sticks
when Spin is off).

**Fonts + themes (part 2, done).** The UI font is **Bai Jamjuree**
(`assets/fonts/BaiJamjuree-Medium.ttf`) and there are **3 selectable themes** ported verbatim
from `ToonEngineOld/src/ui/themes.cpp` — **Amber Yellow** (default), **Gruvbox Hard**, **Gray
Stone** — chosen from a combo in the Debug panel.
- **No seam change for the font.** The font hook I expected turned out unnecessary: the
  Diligent ImGui renderer sets `ImGuiBackendFlags_RendererHasTextures` (imgui 1.92's dynamic
  atlas — `ImGuiDiligentRenderer.cpp` `UpdateTexture`/`DestroyTexture` over `io.Textures`), so
  a font added **after** `InitUI` (context exists) and **before** the first frame uploads its
  glyph texture automatically. So `main.cpp` calls `io.Fonts->AddFontFromFileTTF(...)` directly
  (ImGui is seam-exempt). Baked path via `TOON_FONTS_DIR` (CMake), like shaders/models.
- **DPI.** Font is rasterized at `18 * dpiScale` (crisp — not `FontGlobalScale`, which blurs),
  where `dpiScale = glfwGetWindowContentScale` (1.5 on the 150% dev monitor). `ApplyTheme`
  ends with `ImGui::GetStyle().ScaleAllSizes(dpiScale)` so widget metrics match — the old
  themes' padding/rounding were authored at 1×.
- **`ApplyTheme(theme, dpiScale)`** resets `GetStyle() = ImGuiStyle()` (default metrics +
  dark colors, so unset entries fall back sanely), applies the theme's colors/metrics, sets
  `WindowMenuButtonPosition = ImGuiDir_None`, then `ScaleAllSizes`. Re-runs on every combo
  switch (no compounding — the reset zeroes it first). Gray Stone authors colors as
  `0xAARRGGBB` (a `FromARGB` helper) with two `LerpColor`-derived tab tints, and uses newer
  `ImGuiCol_` enums (`InputTextCursor`, `TabSelectedOverline`, `TreeLines`, …) — all present in
  imgui 1.92.9.

**Gizmos (part 2, done).** ImGuizmo is vendored as a submodule at `external/ImGuizmo`
(`src/ImGuizmo.cpp` added to the target; its `imgui.h`/`imgui_internal.h` resolve transitively
from the `Diligent-Imgui` link). `main.cpp` draws a translate/rotate/scale gizmo on the
selected entity (op/space toolbar in the Inspector), and `Renderer::GetViewProj` exposes the
camera matrices as `Mat4`.

- **No transpose, no Y-flip.** ImGuizmo's `matrix_t` is **row-major, row-vector** (`right, up,
  dir, position` = rows 0–3) — the *same* convention as Diligent. So `view`/`proj`/`world` feed
  in as their raw 16 floats and `mViewProjection = view*proj` matches our `viewProj` exactly.
  I initially "fixed" a suspected Vulkan Y-flip by negating the projection's Y column — that
  was **wrong** and mirrored the gizmo. ImGuizmo's `worldToPos` already does the screen
  `y = 1 - y` flip, and it expects `[0,1]` depth (its ray uses `zNear=0`), so Diligent's LH
  `[0,1]` projection drops straight in. Verified with a one-shot dump: the world origin →
  `ndc=(0,0)` (exact screen centre). The gizmo only *looked* offset at the world origin because
  that spot is small and cluttered by the helmet/ground — moving the cube to a clear position
  showed the gizmo dead-on. **Lesson: don't calibrate a gizmo by eye against a busy origin;
  dump the projected NDC and/or move the target somewhere unambiguous.**
- `AllowAxisFlip(false)` so axes show their true directions (no auto-facing the camera). The
  gizmo draws on `ImGui::GetForegroundDrawList()` with `SetRect(0,0,DisplaySize)`; `IsUsing()`
  feeds the input capture gate (so a drag doesn't also orbit the camera). Blue **+Z pointing
  down** at the default view is correct — the camera sits below the origin looking up, so world
  +Z foreshortens downward.
- **Decompose (`scene.cpp DecomposeToTransform`)** is the real work: the exact inverse of
  `LocalFromTransform` (`Scale·Rx·Ry·Rz`, row-vector), derived from Diligent's `RotationX/Y/Z`.
  Translation = row 3; scale = the upper-3×3 row lengths (with a determinant-sign fold into X
  for mirrors); then Euler from the normalised rows (`ry = asin(-R[0][2])`, `rx =
  atan2(R[1][2],R[2][2])`, `rz = atan2(R[0][1],R[0][0])`, with a gimbal fallback). Self-consistent
  by construction, so gizmo edits round-trip through the stored Euler `Transform` without drift.
  `SetEntityWorldMatrix` folds the parent world back out (`local = world · parent⁻¹`) before
  decomposing, and **`ReparentEntity`/`MoveEntityAsSibling` now preserve world** the same way
  (the object no longer jumps on reparent).

**Still deferred:** sprite / animation entity components (the light entity shipped — see
"Light entity component" below).

**Gizmo snap + hotkeys.** MEMORY.md previously deferred hotkeys here because "WASD is taken by
the camera fly" — but that fly only runs *while right-mouse is held* (`main.cpp`'s camera
block, gated on `Input::IsMouseDown(Mouse::Right)`), so **W/E/R** are free the rest of the
time. Added Unity-style bindings, all in `main.cpp` (no seam/renderer/shader/input-layer
change — `gizmoOp`/`gizmoMode` are already plain locals there, and ImGui/ImGuizmo are
seam-exempt):

- **W/E/R** switch move/rotate/scale, **X** toggles local/world — `ImGui::IsKeyPressed(...,
  false)` (edge-triggered, no-repeat; a hold must not re-toggle X every frame), gated on
  `!io.WantCaptureKeyboard && !ImGui::IsMouseDown(ImGuiMouseButton_Right)` so typing in the
  Name field or flying the camera doesn't also drive the gizmo. Placed right after
  `ImGuizmo::BeginFrame()` (post-`NewFrame`, so `io.*` is the current frame's) — the changed op
  is picked up the same frame by both the Inspector radios and `Manipulate`.
- **Snap** — `ImGuizmo::Manipulate`'s optional `snap` param (`external/ImGuizmo/src/ImGuizmo.h`)
  reads `snap[0]` for rotate/scale and `snap[0..2]` for translate; one `float step` per op
  (rotate in degrees, translate/scale in world units / factor) is broadcast into a
  `float[3]` at the call site and passed only `snapping ? snapVec : nullptr`. **Snapping
  engages on a checkbox OR while Ctrl is held** (`gizmoSnap || io.KeyCtrl`) — Ctrl gives the
  familiar momentary-snap gesture, the checkbox an always-on mode. Per-op step fields live in
  the Inspector's "Gizmo" section (only the active op's step shows, to stay compact).

**Bugs found dogfooding the above** (pre-existing — from the original gizmo commit, none
caused by the snap/hotkey change; all surfaced because hotkeys made gizmo-dragging easy
enough that this was the first time someone actually drove it hard):

- **Gizmo rotate silently did nothing on a spinning entity, on any axis, whether Spin was
  ticked or not — but worked fine on the (non-spinning) Ground; and re-enabling Spin after a
  manual edit snapped back to the old trajectory instead of continuing from the new
  orientation.** Root cause: the spin animation was an **absolute** function of one shared
  clock — `for (spinners) e.transform->rotationEuler = axis * spinAngle;` — run
  unconditionally every frame regardless of the `spin` checkbox (only advancing `spinAngle`
  itself was gated). So the frame after any gizmo edit, this stomped `rotationEuler` right
  back to `axis * spinAngle` for every entity in `spinners` (Sphere/Cube/Torus/Helmet — not
  Ground, hence it alone worked); the whole `Vec3` gets replaced, not added to, so *every*
  axis got wiped. And even gated on `if (spin)`, resuming would still snap to wherever the
  shared clock said it "should" be — unrelated to the gizmo-set orientation. **Real fix:
  made the animation incremental instead of absolute** — `rotationEuler = rotationEuler +
  axis * (dt * kSpinRate)` each frame while `spin` is on, so it's always continuing from
  whatever `rotationEuler` currently *is* (a natural continuation, or a gizmo-set baseline)
  rather than recomputing from a shared clock. This let the shared `spinAngle` float be
  deleted entirely — each entity is now self-contained. Mathematically equivalent to the
  original formula for the untouched default scene (sum of per-frame increments == the old
  closed form), so no visual change there; only a paused-then-edited-then-resumed spinner
  differs, and only in the intended way.
- **A faint trail ("screen burn-in") followed objects while gizmo-dragging them** (both
  move and rotate). `Impl::RunPostFX` feeds `PostFXContext` the current depth buffer as
  *both* curr and prev (`pPrevDepthBufferSRV = depthSRV; // no history — reuse current`, a
  deliberate simplification from the original SSAO work, since nothing needed real depth
  history at the time). That defeats depth-based disocclusion entirely, so both SSAO's
  temporal AO reprojection *and* TAA's color-history accumulation lean solely on motion
  vectors — fine for smooth camera/spin motion, not for a large discontinuous mouse-driven
  jump (not what such reprojection heuristics are tuned for). Fix: a new **app-computed**
  `PostParams::gizmoManipulating` (not a Debug-panel toggle — set from `ImGuizmo::IsUsing()`,
  same 1-frame-lag pattern already used for the camera capture-gate, read at the top of the
  frame before that frame's `Manipulate()` call happens) forces
  `ScreenSpaceAmbientOcclusionAttribs::ResetAccumulation = 1` **and**
  `TemporalAntiAliasingAttribs::ResetAccumulation = true` for the duration of a drag — SSAO
  reuses the exact flag its `ssaoTemporal` off-toggle already sets for the same "no
  ghosting" reason; TAA's was previously never set at all (always `FALSE`, i.e. always
  accumulating — though TAA is off by default, so it likely wasn't the primary contributor
  unless the Debug panel had it toggled on). AO/TAA are very slightly noisier for the
  duration of a drag, then resume smooth accumulation the instant it ends. **Not fully
  confirmed by the user as of the first fix attempt (SSAO-only)** — the TAA half was added
  as a natural extension of the same confirmed-correct root cause, not yet independently
  re-tested. A real fix (an actual double-buffered depth history) is bigger; deferred unless
  this residual trail is still visible after the TAA extension too.
- **The Helmet's outline has visible gaps at hard edges — NOT a regression, already
  documented.** `model_outline.hlsl`'s own header comment (and this file's "glTF model
  loading" section) already states the exact limitation: loaded models carry no smooth
  normal (unlike procedural primitives' `Vertex::smoothNormal`), so the inverted-hull
  outline extrudes along the plain shading normal and gaps at split-vertex hard creases.
  The Helmet's dense mechanical panel lines make this far more visible than on smoother
  models. A real fix needs computing an averaged normal per unique position across the
  loaded glTF vertex buffer (a real geometry-processing task, not a quick patch) — worth a
  future roadmap item, not folded into this dogfooding pass.

**Not independently verified interactively by Claude** — this dev environment has no live
input desktop, so synthetic keyboard/mouse (`SendInput`) reaches no window at all, proven and
written up in `.claude/skills/verify/SKILL.md`. Both fixes above were made from a precise
code trace (confirmed correct on read, both root-caused to an exact line), then confirmed
working by the user after a manual test.

**Light entity component (roadmap A.1).** Promoted the single global light (a
`toon::Vec3 lightDir` plus a Debug-panel `SliderFloat3("Direction")`) to a first-class scene
entity, the same move already made for per-object outlines (see "Per-object outline tuning"
above). A new `Sun` entity carries a `LightComponent{color, intensity}`, aimed by its
**rotation**: local +Z in world space is the direction light rays travel (Unity/Godot-style),
so the existing gizmo (Rotate) re-aims it with no new input plumbing. `scene.cpp` gained
`MakeLightTransform(position, dirToLight)`, which builds an orthonormal basis from
`dirToLight` (a cross-product pair, guarded for a near-vertical direction) and reuses the
existing `DecomposeToTransform` to extract Euler angles, so the light shares the exact
rotation convention every other entity uses. `GetActiveLight(scene, ...)` is the inverse: it
finds the first light entity and reads its direction back out as row 2 of its world matrix.
`main.cpp`'s scripted scene seeds `Sun` with `MakeLightTransform({0,4,0}, {0.5,0.8,-0.3})`,
the exact old default direction, so the default render is unchanged.

Color and intensity needed a real shader change; direction alone didn't. `SetLight` gained
`color`/`intensity` params, premultiplied into one `float3`. The shared cbuffer
(`toon_common.hlsli` / `ShaderConstants`) grew 384 → 400 bytes with a new `g_LightColor`
field (five `float4x4` rows plus five `float4` rows now, was four).
`toon_fill.hlsl`/`model_fill.hlsl` multiply their `CelShade` result by `g_LightColor.rgb`.
This is deliberately simple: it tints the ambient/shadow floor along with the lit side, with
no separate ambient color, consistent with the renderer's stylized shading elsewhere.

**Scope, deliberately: single light only.** The cel shader does one `N·L`; `GetActiveLight`
returns the first light entity found and ignores the rest (silent no-ops). True multi-light
needs a different shading model, a light array or loop, left for later. There is no "Add
Light" hierarchy affordance yet either, since nothing distinguishes it from `Add Child` until
multi-light makes "which light" a meaningful question. Sprite and animation entity
components are still not started; both are deferred to roadmap phase C, where their
rendering paths (2D/sprites, skeletal animation) actually land. Building the entity-side
scaffolding now, with nothing to render, would be exactly the speculative, half-finished work
CLAUDE.md's guiding principles warn against.

Verified: a clean build (a cbuffer size or field mismatch fails loudly and immediately, so a
clean PSO-creation log is real evidence the C++/HLSL layouts agree), then launch +
`PrintWindow` screenshots (see "Verifying a Vulkan build" below; no live input desktop here).
The default scene rendered identically to before (regression check) with `Sun` listed in the
Scene Hierarchy. A temporary spot-check, reverted after, forced `Sun` selected with a strong
blue color at intensity 2: the entire rendered scene visibly tinted blue, proving
`g_LightColor` flows through the cbuffer and shader multiply rather than being a UI-only
change. The Inspector showed exactly Name, Transform, and Light (no Material, correctly
gated on mesh/model presence), with the Color swatch and Intensity matching the injected
values and a genuinely non-trivial decomposed rotation. Console log clean, only the
pre-existing benign warnings already noted throughout this file.

## Scene serialization (roadmap A.2)

`core/serializer.{h,cpp}` adds `SaveScene`/`LoadScene`, ported from
`ToonEngineOld/src/scene/serializer.*` but redesigned around this engine's actual Entity
shape. The old engine had no procedural primitives — every renderable carried a
`modelPath` and reloaded from disk. This engine's scripted scene is almost entirely
procedural (sphere/cube/torus/plane), so a serializer that only handled `modelPath` would
round-trip just the Helmet and Sun; everything else would come back as an empty transform
node. Reusing the old format directly wasn't an option.

**A procedural mesh needs provenance, not just a handle.** `Entity` gained two fields:
`PrimitiveDesc primitive` (`core/primitives.h` — a `PrimitiveKind` enum plus the generator
params: radius/rings/segments for a sphere, half-extent for a cube, major/minor
radius/segments for a torus) and `std::string modelPath`. `MakePrimitiveMesh(desc)`
dispatches back to `MakeUVSphere`/`MakeCube`/`MakeTorus`/`MakePlane`, so a saved primitive
regenerates its exact mesh on load without a source file; a saved model re-feeds its path
to `Renderer::LoadModel`. `main.cpp`'s scripted scene now builds every primitive through a
`SetPrimitive(renderer, entity, desc)` helper instead of calling `Upload(MakeXxx(...))`
directly, so the provenance is always recorded alongside the mesh handle rather than
reconstructed after the fact. `PrimitiveDesc` gets one static factory per kind
(`PrimitiveDesc::Sphere(radius, rings, segments)` etc., the same pattern as
`Mat4::Identity()` in math.h) instead of a positional-argument aggregate literal, since
five same-typed floats/ints in a row at the call site is an easy transposition bug.

**File format.** A simple line-based text file, human-readable and diffable, matching the
old engine's approach:

```
camera.pivot 0 0 0
camera.distance 10
entity "Cube"
  parent 0
  position 0 0 0
  rotation 0 0 0
  scale 1 1 1
  primitive cube 0.9
  material.baseColor 0.3 0.45 0.85
  ...
```

Entities are written and re-read in vector order, which is exactly parent-before-child
(the scene's own invariant, see scene.h), so a `parent <index>` line always names an
already-parsed entity, and `LoadScene`'s append-as-you-go rebuild reproduces the identical
vector — no second pass or index remapping needed.

**Scope: camera + entities, not editor/session state.** A saved scene is exactly what the
Scene Hierarchy and Inspector panels expose (transform, primitive-or-model, material,
light, hierarchy) plus the camera. `PostParams`, the shared `style` (bands/ambient/
outline scale), the UI theme, and Spin are deliberately excluded: they're renderer/editor
tuning, not scene content, and folding them in would blur that boundary for no clear
benefit yet (nothing currently lets you attach "spin" to an arbitrary entity — it's
main.cpp's scripted-demo behavior, not a scene-graph concept).

**`LoadScene` takes a `Renderer&`** and calls `CreateMesh`/`LoadModel` itself, so the
Debug panel's Load button is one call, matching the old engine's "reload from disk on
load" pattern. It parses into a side-buffer `Scene`/`Camera` and only replaces the
caller's on a fully successful parse, so a malformed file leaves the live scene untouched
rather than half-applying. `SaveScene` creates any missing parent directories via
`<filesystem>` first, so the first save into a fresh `assets/scenes/` (not committed,
created on demand) doesn't silently fail.

**Known gap, not a new one.** Neither `Renderer` nor `DestroyScene` frees GPU mesh/model
resources today (`DestroyScene`'s own comment in scene.h says so), so Load leaks the
replaced scene's GPU resources exactly like `DeleteEntity` already does. A real fix needs
`Renderer::DestroyMesh`/`DestroyModel` plus either refcounting or a path cache for
repeated `LoadModel` calls on the same file; out of scope here.

**`main.cpp` wiring.** A new "Scene" section in the Debug panel: a path field (defaulting
to a new `TOON_SCENES_DIR` compile define, matching the `TOON_SHADERS_DIR`/
`TOON_MODELS_DIR`/`TOON_FONTS_DIR` pattern) and Save/Load buttons, with a status line
since `SaveScene`/`LoadScene` also print to the console and this dev environment has no
reliably visible one. The scripted demo's `spinners` vector (which entities animate, and
by which axis) holds raw indices into `scene.entities` and isn't part of the serialized
model — Load clears it on success so a freshly-loaded scene doesn't drive spin off stale
or wrong indices.

**Verified for real, not just by compiling.** This dev environment has no live input (see
`.claude/skills/verify/SKILL.md`), so the Save/Load buttons can't be click-tested here. A
temporary, reverted self-test in `main.cpp` round-tripped the scripted default scene
through `SaveScene` + `LoadScene` into a throwaway `Scene`/`Camera` at startup (never
touching the live one) and printed a comparison to stderr, redirected to a file at launch.
Confirmed: 8/8 entities round-tripped with correct parent indices (Satellite correctly
came back with `parent=3`, Cube's index), correct positions, a valid regenerated mesh
handle for every primitive, a valid reloaded model handle for Helmet, and Sun reconstructed
with neither mesh nor model set, light only. Also read the written `.scene` file directly:
clean, matches the format above, human-readable. Screenshot-confirmed (before and after
reverting the temp code) that the Debug panel's new Scene section renders correctly and
nothing else regressed.

## Input system (roadmap A.1)

Ported `ToonEngineOld/src/core/input/` into `core/input/` — action maps, an input-context
stack, gamepad, and JSON-bound rebinding — replacing the minimal polling-only
`core/input.{h,cpp}` documented in "Editor camera + input" above. Checked first against the
guiding principle (build *on* Diligent): confirmed nothing to build on — see "Diligent
overlap check" above, which already established DiligentCore/Tools have no input/camera
abstraction and DiligentSamples' `InputController` isn't vendored here.

**Six files, all under `namespace toon::Input`** (the reference left the action-query free
functions and enums at global scope; unified here): `keycodes.h` (Key/MouseButton/
GamepadButton/GamepadAxis/MouseAxis — Key mirrors GLFW's own codes), `input_device.h`
(Keyboard/Mouse/Gamepad — current/previous arrays for edge detection), `input_system.{h,cpp}`
(GLFW callback wiring, the capture gate, the polling API), `action_map.{h,cpp}`
(FNV-1a-hashed actions/axes, the context stack, `RegisterDefaultEditorBindings`),
`binding_io.{h,cpp}` (JSON save/load). `input_device.cpp` was dropped — the reference's own
version was a stub whose only content was a comment admitting all its methods are inline in
the header; CMake doesn't need a `.cpp` per header, so there was nothing to port.

**Adaptations from the reference (ToonEngineOld had glm + vcpkg; this engine has neither):**
- **`glm::dvec2` → `toon::Vec2`** in the device layer, with component-wise arithmetic written
  out by hand rather than adding operators to `math.h`'s otherwise-operator-free `Vec2` (a
  one-off, not worth growing the seam's math vocabulary for).
- **No event queue.** `input_event.h`'s `Events()`/`EachEvent()` stream (plus the char and
  drop callbacks that feed it) wasn't ported — it has no consumer yet (its first is the
  asset-browser roadmap item, for drag-drop + text input), and **`std::span` — the only
  C++20 feature anywhere in the reference — lives in exactly that API**, so dropping it is
  what keeps this a clean C++17 port rather than a standard bump. Confirmed via
  `action_map.cpp`: it only reads `RawKeyboard()/RawMouse()/GetGamepad()`, never the event
  stream, so nothing else depends on it.
- **JSON via the already-vendored `Diligent-JSON`, not a new dependency** — but it must be
  **linked explicitly**: `Diligent-AssetLoader` links `Diligent-JSON` as `PRIVATE`, so its
  include dir does *not* propagate transitively to `ToonEngine` even though
  `Diligent-AssetLoader` is already linked. `CMakeLists.txt` needs its own
  `target_link_libraries(... Diligent-JSON)` plus a `target_include_directories` for
  `ThirdParty/json/single_include` (the `Diligent-JSON` INTERFACE target's own include dir
  only exposes the `nlohmann` leaf, i.e. bare `#include <json.hpp>`; the extra dir keeps the
  conventional `#include <nlohmann/json.hpp>` spelling working).
- **Scroll semantics simplified, not just ported.** The reference double-buffers
  `scrollAccum`/`scrollDelta` (`BeginFrame` latches `scrollDelta = scrollAccum` from the
  *previous* frame's accumulation) — a real one-frame lag given the reference's own
  `BeginFrame()`-before-`glfwPollEvents()` loop order (the same order this engine now uses,
  see below). This engine's device layer collapses that to a single live `scrollDelta`
  accumulator, mirroring how `Mouse::delta` already worked: `BeginFrame` clears it,
  `OnScroll` (fired during the poll, which runs *after* `BeginFrame`) accumulates into it
  directly, and it's read live by that same frame's queries. Simpler than the reference and
  removes a latency bug rather than reproducing it.
- **Capture-gate parameter order kept as `(mouseCaptured, keyboardCaptured)`** — this
  engine's existing convention, the reverse of the reference's `(keyboardCaptured,
  mouseCaptured)`. `main.cpp`'s existing `SetCaptured(io.WantCaptureMouse || gizmoActive,
  io.WantCaptureKeyboard)` call needed no change.

**`main.cpp` integration is a hybrid, matching the reference's own pattern — not a blind
route-everything-through-actions rewrite.** The reference's own `main.cpp` (not just its
`action_map.cpp`) keeps mouse-drag orbit/pan/zoom on **raw** `Input::IsMouseDown`/
`WasMousePressed`/`MouseDelta`/`ScrollDelta` queries — never through the action map — while
routing only the fly axes (which need to merge keyboard *and* gamepad into one named value)
and discrete actions (focus, gizmo ops, quit) through it. Ported that same split rather than
inventing a different one: right-drag orbit/middle-drag pan/scroll zoom stay raw; fly
(`camera.fly.forward/right/up`) and focus (`camera.focus`) go through `GetAxis`/
`WasActionPressed`.

- **Default bindings keep E/Q for fly up/down**, not the reference's Space/LeftShift — this
  engine's existing scheme (`main.cpp` already used E/Q before this port), preserved so the
  port doesn't change today's feel.
- **Dropped `gizmo.*` and `app.quit` from the defaults.** The reference bound these too, but
  gizmo hotkeys stay on ImGui's own key routing here (`main.cpp`'s `ImGui::IsKeyPressed`,
  unchanged — see "Gizmo snap + hotkeys" above) and nothing calls
  `WasActionPressed("app.quit")`, so shipping them would be dead config baked into every
  generated `assets/input.json`.
- **New capability: gamepad orbit** (right stick, `camera.orbit.x/y`) — ungated (a physical
  stick is never ambiguous with ImGui text focus) and scaled by `dt` (frame-rate
  independent, unlike the mouse path's per-frame pixel deltas), rather than the reference's
  flat per-frame multiplier, since this engine's loop is variable-rate with no fixed-
  timestep accumulator (see the roadmap's unscheduled fixed-timestep item). The tuning
  constant (150 px-equivalent/sec at full deflection) is an untested starting point — no
  controller here to feel-tune it against.
- **Capture-gate bypass, made explicit.** The action-map query functions read `RawKeyboard/
  RawMouse/GetGamepad`, which — like the reference — bypass `SetCaptured` entirely. Routing
  fly/focus through them without a guard would let W both type in an ImGui field *and* fly
  the camera during a right-drag. `main.cpp` wraps both call sites in
  `!io.WantCaptureKeyboard`; gamepad orbit stays deliberately ungated.

**Frame-order fix (the one real behavior change to `main.cpp`'s existing loop).**
`Input::BeginFrame()` moved from its old spot (right before the camera block, *after*
`glfwPollEvents()`) to immediately *before* `glfwPollEvents()`. Callback-driven edge
detection needs the previous-state snapshot to happen before the callbacks that mutate
current state fire; the reference's own loop was already ordered this way.
`SetCaptured`/the camera queries stay where they were, after the poll.

**Startup wiring.** `RegisterDefaultEditorBindings()` builds and pushes the "editor"
context; `action_map.h` gained a `GetContext(name)` accessor (not in the reference, which
never needed to hand the just-pushed context to anything else) so `main.cpp` can fetch it
and feed it to `BindingIO::Load(TOON_INPUT_JSON, ...)` — the same "load or write the
defaults on first run" shape as scene save/load. `BindingIO::Load`'s failure contract was
tightened versus the reference (which cleared and repopulated the caller's `InputContext`
in place, so a mid-populate exception could leave it partially overwritten): this port
parses into a side buffer under one `try`/`catch` and only assigns on full success, the same
"side-copy, swap on success" pattern `serializer.cpp`'s `LoadScene` already uses.

**A real, non-obvious build gotcha found here — see "Build gotchas" above for the general
lesson now folded in there.** After adding four `target_*` calls to `CMakeLists.txt` (new
sources, the JSON include dir, the `Diligent-JSON` link, two compile defs) in one sitting,
`cmake --build --preset windows-debug` forced a reconfigure and got most of the way through
a full DiligentCore/Tools/FX rebuild before failing on `binding_io.cpp(6,10): fatal error:
'nlohmann/json.hpp' file not found`. The file exists exactly where the new include dir
points (confirmed on disk); the actual cause, found by extracting the real compiler
invocation from `compile_commands.json`, was that **the include dir (and the two new
compile defs) were simply absent from the generated command** — despite `CMakeLists.txt` on
disk having all four edits, confirmed via a fresh `Read` immediately before the build.
Grepping the generated `build.ninja` directly for the new content confirmed zero matches:
the implicit reconfigure genuinely hadn't processed those lines, even though it *had*
picked up the new source-file list (the three new `.cpp`s did compile). Root cause not
fully isolated — `build.ninja`/`compile_commands.json`'s timestamps were only 2 seconds
after `CMakeLists.txt`'s own last-write time, so this reads as the implicit
regenerate-if-stale check running, but CMake's own configure pass not fully applying every
`target_*` call from the edited file. **Fix: an *explicit* `cmake --preset windows-debug`
reconfigure** (not `--build`) picked up all twelve new references immediately (confirmed via
the same `build.ninja` grep), and the subsequent build succeeded.

**Verified:**
- **Clean build** (`cmake --preset windows-debug` then `cmake --build`, exit 0, 663/663
  steps) after the reconfigure fix above.
- **Persistence round-trip — the strongest evidence available without live input.** First
  launch printed `Bindings saved: .../assets/input.json` (the file didn't exist before);
  reading it back confirmed the exact expected schema — `camera.fly.up` bound to E/Q,
  `camera.orbit.x/y` present as gamepad-only axes, no `gizmo.*`/`app.quit` keys anywhere.
  This exercises `RegisterDefaultEditorBindings` → `GetContext` → `BindingIO::Load` (miss)
  → `BindingIO::Save`, the action-map's binding→JSON serialization, and the
  `Diligent-JSON` link, all in one observable artifact — not just "it compiled."
- **Launch + `PrintWindow` screenshot** (cold-start wait, DPI-aware capture — see
  "Screenshotting the window" below): the full scene rendered normally at 144 FPS (helmet,
  cube+satellite, sphere, torus, ground, gizmo, all panels), confirming the moved
  `BeginFrame` and the new startup load/save path didn't crash or hang. The Debug panel's
  new Camera-section lines rendered correctly, including the conditional gamepad-count
  text — which read as *connected* on this machine. Cross-checked via `Get-PnpDevice`: the
  only matching HID entries are "HID-compliant system controller" collections under Razer/
  keyboard vendor IDs, which look like a peripheral's extra HID interface rather than a
  dedicated controller — reported as an unconfirmed, likely-benign detection, not a
  verified real gamepad.
- **Graceful close** — `CloseMainWindow()` + `WaitForExit` returned within 5s, no hang, no
  abort dialog (the ImGui shutdown-order fix from "Dear ImGui integration" above is
  untouched by this change).
- **Blocked, reported as such rather than glossed over:** live interactive behavior (does a
  held key actually fly the camera, does editing `assets/input.json` change the feel) can't
  be driven synthetically here (`SendInput` reaches no window in this environment — see the
  `verify` skill), and there's no confirmed physical gamepad to test the new stick bindings
  against. Both need a manual check on the user's own machine.

**Still deferred** (see the "Editor camera + input" update above): F-focus on the selected
entity, an interactive in-editor rebind panel (rebinding today is edit-the-JSON-and-
relaunch), and the event queue / file-drops.

## Asset browser (roadmap A.1)

Ported `ToonEngineOld/src/ui/file_browser.*` + `thumbnail_cache.*` onto the current engine —
the last editor-layer item from the carry-over survey. The old files were written against a
**free-function** seam (`LoadTexture`/`DestroyTexture`/`GetTextureNativeID`/...); the current
seam is a PIMPL `Renderer` class with no texture API at all, so the real work here was adding
one, not just moving UI code.

**Seam addition** (`core/renderer.h`/`.cpp`): `LoadTexture`/`DestroyTexture`/
`GetTextureImGuiID`/`GetTextureSize`, mirroring the existing `meshes`/`models` handle-vector
convention (`Impl::textures`, 1-based handles, 0 = Invalid). `LoadTexture` uses
`CreateTextureFromFile` (already-linked `Diligent-TextureLoader`) with a **default**
`TextureLoadInfo` — its defaults (`IMMUTABLE`, `BIND_SHADER_RESOURCE`, `IsSRGB=false`,
`GenerateMips=true`) are exactly right, so nothing is overridden except `Name`.
`GetTextureImGuiID` returns `reinterpret_cast<uint64_t>(tex->GetDefaultView(...SHADER_RESOURCE))`
as a plain integer — not an ImGui type — so `renderer.h` stays ImGui-free; the UI casts it to
`ImTextureID` at the `ImGui::Image` call site. Confirmed against
`external/DiligentTools/Imgui/src/ImGuiDiligentRenderer.cpp:1135`
(`reinterpret_cast<ITextureView*>(pCmd->GetTexID())`, then `Set` + `CommitShaderResources(...,
RESOURCE_STATE_TRANSITION_MODE_TRANSITION)`) that this is exactly the mechanism the Diligent
ImGui backend expects — the same path the model albedo texture already goes through, so no
new state-management burden. `Renderer::Shutdown` gained `m_impl->textures.clear()` alongside
`meshes`/`models`, so thumbnails free on the idle'd device even if a caller forgets to.

**Two real bugs the GL reference would have carried over silently:**
- **`IsSRGB` must be `false`, not the seemingly-obvious `true` for a color image.** ImGui
  doesn't tone-map; its pixel shader computes `vertexColor * texture.Sample(...)` and applies
  sRGB handling to that *product* uniformly, so a bound texture's samples and the UI's own
  (gamma-space-authored theme) vertex colors have to live in the same color space. Loading
  sRGB would linearize on sample while the UI around it doesn't, making every thumbnail read
  too dark — caught before shipping by comparing a captured thumbnail directly against the
  source PNG (see Verification below), not by inspection alone.
- **Drop the reference's UV flip.** The old code passed `ImVec2(0,1),(1,0)` to `ImGui::Image`
  for GL's bottom-origin textures; Diligent/Vulkan's `CreateTextureFromFile` decodes
  top-origin (same convention the model albedo texture already uses), so the port uses
  `ImGui::Image`'s default `(0,0)-(1,1)` UVs. Carrying the flip over would have rendered every
  thumbnail upside down.

**C++17, not the reference's C++20:** `FormatTime` used `std::chrono::clock_cast`
(C++20) — rewritten as the portable pre-`clock_cast` idiom (measure `ft`'s offset from the
file clock's `now()`, apply that same offset to `system_clock::now()`). The old `FileFilter`
(`.gitignore`-pattern matching, for browsing the *whole* repo) was dropped entirely rather than
ported: it also used C++20's `std::string::starts_with`, and scoping the browser to `assets/`
only (see Scope below) removes the reason it existed in the first place — a plain dotfile
check is the whole filter now.

**Scope decisions** (confirmed with the user before building): thumbnails are **images only**
(PNG/JPG/BMP/TGA decoded to textures); models/other files get a colored text tag
(`[M]`/`[D]`), no rendered model previews. Root is **`assets/` only**, not the whole repo.
Mostly **passive** — navigate/sort/preview — except double-clicking a `.scene` file, which
loads it. That last path is shared with the Debug panel's existing "Load Scene" button via a
`loadScene` lambda in `main.cpp`, not duplicated: `LoadScene` resets `scene.selected` and
invalidates every `spinners[]` index (see "Scene serialization" above), so both call sites
need the same `spinners.clear()` cleanup, and a lambda was the only way to guarantee that
without two copies of it drifting apart. `FileBrowser::Render` reports an activated file's
path back to the caller rather than knowing what a `.scene` file means itself — `main.cpp`
decides that, keeping the browser decoupled from scene/serializer semantics. Dock layout:
split off the bottom ~28% of the remaining pass-through center (after the existing
Hierarchy/Inspector/Debug splits), so the 3D viewport shrinks but nothing else moves.

**Verified:** clean build (after the CMakeLists.txt reconfigure gotcha below). Screenshot
comparison, not just a compile check: cropped the captured `icon.png` row's thumbnail out of
a full-window `PrintWindow` capture and compared it directly against the source file — same
upright orientation, same brightness, confirming both bug fixes above actually took. Graceful
shutdown was also genuinely exercised despite the no-synthetic-input limitation (see the
`verify` skill): `PostMessage(hwnd, WM_CLOSE, ...)` is a direct Win32 message post, not
`SendInput`-based injection, so it isn't subject to that limitation — GLFW's win32 backend
handles `WM_CLOSE` in its window procedure regardless of focus state. The process exited
cleanly in ~2s with nothing in the Application event log — stronger evidence than one captured
frame rendering fine, since it confirms `FileBrowser::Shutdown` and the new
`Impl::textures.clear()` are ordered correctly across teardown. **Still blocked:** clicking a
row to check the preview pane, double-clicking a folder to navigate, and double-clicking a
`.scene` file to confirm the load all need synthetic input this environment doesn't have. The
last one is also untestable for an unrelated reason: no `.scene` file exists yet in a fresh
`assets/scenes/` (nobody has clicked "Save Scene" in this build), so even a manual check needs
that done first.

**CMakeLists.txt gotcha, hit again:** adding a source file (`src/ui/file_browser.cpp`) and a
new define (`TOON_ASSETS_DIR`) forced the exact reconfigure-needs-the-VS-env failure mode in
"Build gotchas" above (`'lib.exe' is not recognized`) — the fix was the same chained
VS-env-import + build in one shell call. Worth reinforcing since it was hit from a plain
(non-CLion) shell across two separate tool invocations: the first rebuild attempt, run
without re-importing the VS env in that specific call, failed the same way even though an
explicit `cmake --preset` reconfigure had just succeeded moments earlier in the environment —
the import genuinely doesn't outlive the shell process it ran in.

## Verifying a Vulkan build

### Link fails: `permission denied` writing `ToonEngine.exe`

If a previous instance is still running, `lld-link` can't overwrite the exe. Kill
it first. If it's a **stuck / elevated** instance that won't die (`Stop-Process`,
`taskkill`, and CIM `Terminate` all return Access Denied), **rename the running
exe aside** — Windows allows renaming a running executable (it's a metadata op) —
then re-link creates a fresh one. The renamed file can't be deleted until that
process finally exits.

**Same failure, a DLL instead of the exe.** A running instance also locks the engine DLLs it
loaded — `lld-link: error: failed to write output 'GraphicsEngineVk_64d.dll': permission
denied` fails identically and for the same reason. `Get-Process | Where-Object { $_.Path
-like '*ToonEngine*' }` finds the culprit. **Check whether it's actually yours before
killing it** — a window you didn't just launch this session may be the user's own live
session (see the `verify` skill's "don't assume a process you didn't just launch is yours");
ask first unless durably authorized to close it.

### Screenshotting the window (GDI `CopyFromScreen` returns black)

A Vulkan swap-chain doesn't show up in GDI screen-copy — `Graphics.CopyFromScreen`
captures the client area as pure black (the DWM-drawn title bar still shows).
**`PrintWindow(hwnd, hdc, PW_RENDERFULLCONTENT=2)` does capture the rendered
content.** That's how the toon sphere was verified.

**High-DPI crop (150% display).** `PrintWindow` captures at the window's *physical*
framebuffer resolution, and ImGui's viewport / dock layout is sized in those physical
pixels. On a 150%-scaled monitor the framebuffer is 1.5× the logical client (e.g. **3840×2054
vs the 2560×1369 `GetClientRect` reports**) — so **right-docked panels sit beyond the logical
width and get cropped out of the shot** unless the *capturing* process is DPI-aware. Fix in
the capture script: `SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2 = -4)` at the top, so
`GetClientRect` returns the true 3840-wide size and the bitmap grabs the whole framebuffer
(then downscale for viewing). Relatedly, the **app starts maximized** (`GLFW_MAXIMIZED` hint +
a modest restored size) rather than hardcoding a 3840×2160 window — an oversize window on a
smaller screen pushed the dock layout's right column off-screen even in the real app.
`PrintWindow` also **intermittently returns an all-white frame** (a race with the swap-chain
present) — the render is fine; just re-run the capture.

## ToonEngineOld — carry-over reference (roadmap: renderer → engine)

`ToonEngineOld/` (untracked, **gitignored**, temporary — slated for removal once ported;
`src` + `assets` only) is the old from-scratch **OpenGL 4.1** engine, kept as a porting reference. It's the inverse of the
current tree: a real little **editor** (scene graph, inspector + gizmos, model loader,
input, camera, serialization, shadows, grid, sprites) on the weaker renderer. The new
engine has the strong Vulkan renderer but was a hardcoded demo — so the roadmap's next arc
is porting that engine/editor layer *above the seam* onto Diligent.

**Seam note:** the old seam is the same "opaque handles behind a backend-agnostic header"
idea, but **low-level** (`BindShader`/`SetUniform`/immediate binds/framebuffers) — it does
NOT map onto Diligent's PSO/SRB model, so the old `renderer.cpp` is reference-only. The
value is everything above the seam; our seam grows instead (textured materials, a UV/bone
vertex, a framebuffer path for shadows).

**Carry-over map** (per system):
- **assets** — fonts (BaiJamjuree, OpenSans), 4 test models, icon: **copied** into
  `assets/` (models are Git LFS). The GLSL shaders stay in `ToonEngineOld` as HLSL-port
  references — esp. `toon.frag` (spec + rim + shadow ramp, richer than our current fill),
  `grid.frag`, `shadow.*`.
- **scene/scene.{h,cpp}** — entity **tree** (flat vector + parent index, root at 0, cached
  world matrices, add/delete/reparent/duplicate, world-preserving reparent). High value;
  port glm→Diligent math and old handles→our `MeshHandle`/`Material`.
- **scene/model_loader** — cgltf (glTF) + ufbx (FBX) → meshes/materials/skeleton/anim.
  **Decision: use DiligentTools' glTF loader instead** (native integration) → glTF/GLB
  only, no FBX (`dragon.fbx` won't load via that path; `dragon.gltf` does). Keep the old
  loader as the reference if FBX / skeleton parsing is ever wanted.
- **ui/overlay** — inspector + `RenderSettings` (bands, spec, rim, shadow ramp, outline
  incl. a screen-space-width flag, CSM, grid, sky, gizmo) + **ImGuizmo** transform gizmos.
  ImGui logic ports (our seam already exposes ImGui); ImGuizmo must be vendored.
- **scene/camera** — orbit/pan/zoom/fly/focus editor camera (replaces our turntable).
- **core/input/** — keyboard/mouse/gamepad + action maps + rebinding + an ImGui capture
  gate; GLFW-based, largely direct.
- **core/animator + animation** — skeleton + keyframe clips; after skinned loading.
- **ui/file_browser + themes + thumbnail_cache** — asset browser + 3 themes + texture/model
  preview thumbnails; ImGui, mostly portable. Thumbnails should decode/upload through the
  already-linked `Diligent-TextureLoader` (`CreateTextureFromFile`), not a new image lib.
- **core/renderer (GL) + main.cpp** — reference only.

**Materials will need textures:** the old `Material` is `baseColor + texture + normalMap`,
and loaded models (helmet.glb) carry albedo/normal maps — so Phase A adds texture handles
to the seam + a textured cel fill, and the toon `Vertex` gains UVs (bone weights later).

### Diligent overlap check (roadmap fold-in, 2026-07-11)

Before adding the input-system / asset-browser / fixed-timestep / shader-hot-reload items
to CLAUDE.md's roadmap, checked each against the guiding principle (build *on* Diligent,
don't reinvent it) — against the actual vendored source (DiligentCore/Tools/FX only;
DiligentSamples is **not** a submodule here) plus Diligent's own docs/blog.

- **Input / camera controllers — genuinely nothing to build on.** DiligentCore and
  DiligentTools have no windowing or input abstraction at all: the only `*Camera*` hit in
  either (grepped both trees) is `NativeApp/Android/ndk_helper/tapCamera.h`, Android-only.
  `FirstPersonCamera` / `InputController` exist only in **DiligentSamples** — a separate
  repo ToonEngine doesn't vendor — and Diligent's own docs confirm the engine "does not
  define any platform-specific window abstraction"; DiligentSamples' own maze demo just
  uses GLFW for windowing + input, same as us. So `core/input.{h,cpp}` and
  `core/camera.{h,cpp}` staying hand-rolled isn't a guiding-principle violation — there's
  no in-scope Diligent equivalent to defer to, so the ToonEngineOld port is genuinely new
  engine-layer code, same as the seam philosophy already treats scene.cpp/camera.cpp.
- **Shader hot-reload — Diligent already has this; don't hand-roll a file-watcher.** The
  interface is `Diligent::IRenderStateCache`, declared in
  `DiligentCore/Graphics/GraphicsTools/interface/RenderStateCache.h`. Create it with
  `EnableHotReload = true` (the default `RENDER_STATE_CACHE_FILE_HASH_MODE_BY_CONTENT`
  hash mode is required for this), route shader/PSO creation through its
  `CreateShader()` / `CreateGraphicsPipelineState()` instead of the raw device calls,
  then call `cache->Reload()` to recompile whatever source files changed. Confirmed via
  Diligent's 2.5.3 release notes + `Tutorial26_StateCache`: `Reload()` is a manual trigger
  (a UI button/hotkey), not something polled every frame. The implementation
  (`RenderStateCacheImpl.cpp`) builds unconditionally into `Diligent-GraphicsTools`, which
  `CMakeLists.txt` already links (for `MapHelper.hpp`); the only other requirement is
  `Diligent-ArchiverInterface` for the `IArchiverFactory` the cache needs, and the Archiver
  DLL already ships (see *Compile time* above). Zero new deps.
- **Fixed timestep — not a Diligent concern either way.**
  `DiligentCore/Common/interface/Timer.hpp` is a bare `std::chrono` stopwatch (`Restart` /
  `GetElapsedTime[f]`), not a fixed-timestep/accumulator solution — swapping `main.cpp`'s
  `glfwGetTime()` for it would change nothing functionally. The accumulator + decoupled
  sim-rate pattern is pure game-loop architecture, orthogonal to the graphics API either way.
- **Asset thumbnails — reuse the texture path already on the seam.** No new image
  library needed: `Diligent-TextureLoader`'s `CreateTextureFromFile` (already linked, used
  today for model textures) is the right entry point for generating/caching browser
  thumbnails, per `ToonEngineOld/src/ui/thumbnail_cache.{h,cpp}` — a real implemented file
  the original carry-over survey above had missed (now added to the file_browser bullet).

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
  is ancient/incompatible. That checkout was manual and uncommitted (reverted by
  any `git submodule update --recursive`) — superseded 2026-07-12 by a dedicated
  `external/imgui` submodule; see "Docking" above for the current mechanism.
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
  **`docs/style-guide.md`** (renamed `docs/cpp-style-guide.md` on 2026-07-11) and a
  **`.claude/skills/tidy-cpp`** skill for future cleanups. See the Dear ImGui + Bloom
  "Gotchas" above.
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
- **2026-07-10** — **TAA** (roadmap #1): DiligentFX `TemporalAntiAliasing`, first in the
  color chain. `SetCamera` jitters the projection (`GetJitteredProjMatrix`) when TAA is
  on and records `f2Jitter`; `main.cpp` now sets post params before `SetCamera`. Off by
  default (softens toon edges). Verified: clean run, graceful close, spinning objects
  anti-alias without ghosting (motion+jitter correct). See "TAA" above.
- **2026-07-10** — **SSR** (roadmap #1, the last DiligentFX effect): DiligentFX
  `ScreenSpaceReflection`. Roughness packed into the normal buffer's `.w`
  (`Material::roughness`, `RoughnessChannel = 3`); reflection radiance composited in
  the tone-map (`g_SSR`, simplified — no PBR BRDF/env-map). `RunPostFX` now returns
  `(colorOut, aoOut, ssrOut)`. Off by default; objects made lightly glossy (0.15) so
  it's visible when enabled (a flat ground reflects the sky → misses). Verified via
  the radiance buffer; clean run, graceful close. See "SSR" above. **All six DiligentFX
  post effects (Bloom, SSAO, DoF, motion vectors, TAA, SSR) are now in.**
- **2026-07-10** — **Non-uniform scale** (roadmap #1, toon pipeline extensions): added an
  inverse-transpose **normal matrix** (`g_NormalMatrix`) so the fill shading and the
  normal/roughness G-buffer stay correct under non-uniform `Transform::scale`, and reworked
  the inverted-hull outline to extrude a uniform **world-space** width (reusing the WVP path
  via the 3×3 transpose of the normal matrix = world⁻¹). One added cbuffer matrix
  (256→320 B); both changes reduce algebraically to the old behavior at scale = 1, so the
  existing scene is unchanged. Demo: the sphere is now a non-uniformly-scaled spinning
  **ellipsoid** (`Object` gained a per-object `scale`). Built clean (clang-cl), ran with
  zero validation errors, graceful close; verified the ellipsoid shading + uniform outline
  via `PrintWindow`. See "Non-uniform scale" above.
- **2026-07-10** — **Per-object outline tuning** (roadmap #1): stopped `main.cpp` stomping
  each object's outline with a shared `style` — every `Object` now carries its own outline
  color + width (sphere thin dark-red, cube bold near-black, torus dark-bronze), the draw
  loop overlays only global band/ambient/gloss onto a per-draw material copy, and a global
  `outlineScale` scales all widths together. UI reworked into a per-object "Objects" section
  + a global "Outline width ×" multiplier (`Object` gained a `name`). App-only — the
  Material/shader already carried per-object outlines. Built clean, verified three distinct
  outlines via `PrintWindow`. See "Per-object outline tuning" above.
- **2026-07-10** — **Roadmap redesign + ToonEngineOld carry-over.** With the renderer core
  done, pivoted the roadmap from "more rendering" to the **engine/editor layer** (phases:
  real assets → scene graph → editor UI → environment → animation/2D; instancing deferred),
  porting `ToonEngineOld`'s systems onto the Vulkan seam. Surveyed the old engine (untracked
  reference folder) and copied its portable assets (fonts, 4 test models, icon) into
  `assets/` — models via **Git LFS** (`.gitattributes` tracks `assets/models/**`). Model
  loading will use **DiligentTools' glTF loader** (glTF/GLB only; the old cgltf/ufbx loader
  is the FBX reference). Next up: Phase A — textured materials + load/cel-shade a real
  model. See "ToonEngineOld — carry-over reference" above. Also **codified the
  build-on-Diligent principle** in CLAUDE.md (use Diligent's own implementations — loaders,
  FX, ImGui; the seam only tames boilerplate + keeps the app/public API backend-agnostic,
  never 1:1 abstraction) and generalized the seam rule: Diligent lives in the engine's
  implementation TUs, not just `renderer.cpp` — only the app layer + public headers stay
  Diligent-free.
- **2026-07-10** — **glTF model loading** (Phase A / "real assets"): load + cel-shade real
  models via DiligentTools' `GLTF::Model` (Diligent-first — no hand-rolled loader, no PBR
  renderer). New seam `ModelHandle` / `LoadModel` / `DrawModel`; `model_fill.hlsl` reuses the
  toon CB + `CelShade` helper; `helmet.glb` renders textured + cel-shaded in the HDR/post
  pipeline. Linked `Diligent-AssetLoader` + `Diligent-TextureLoader`; baked `TOON_MODELS_DIR`.
  Four loader gotchas cost cycles (VertBufferBindFlags = BIND_NONE default; textures are
  Texture2DArray; the buffer/texture getters need device+context; a dimension-mismatch
  assertion HANGS and logs to buffered cout). See "glTF model loading" above. Verified on the
  RTX 3080 via `PrintWindow` (clean run, graceful exit).
- **2026-07-10** — **Model outline**: models get the inverted-hull silhouette too, via
  `model_outline.hlsl` (extrude along the shading normal, no smooth normal) + a cull-FRONT
  outline PSO; `DrawModel` draws outline→fill per primitive. The helmet now matches the toon
  look. See "glTF model loading".
- **2026-07-10** — **Scene graph** (Phase B): `core/scene.{h,cpp}` — an entity tree with
  hierarchy-composed world matrices; the render loop walks the scene instead of a hardcoded
  array. Design call: `scene.cpp` is a Diligent-using TU (composition via `float4x4`, no
  reinvented 4x4 math) and `math.h` gained a plain `Mat4` (seam vocabulary) with new `Mat4`
  `DrawMesh`/`DrawModel` overloads; the `Transform` overloads delegate. Motion vectors now
  come from the scene's double-buffered world matrices (no prev-angle bookkeeping). Demo: a
  satellite parented to the cube orbits it. Editor-triggered mutations (reparent / duplicate
  / decompose / selection) deferred to the editor step. Built clean, verified via
  `PrintWindow`. See "Scene graph" above.
- **2026-07-10** — **Editor camera + input** (Phase B, item 4): an orbit-around-pivot camera
  (extends the LH turntable — `SetCamera` prepends `Translation(-pivot)`; did NOT port glm's
  RH lookAt) + `core/camera.{h,cpp}` controls (orbit/pan/zoom/fly/focus; basis from the same
  Diligent rotations as the view) + `core/input.{h,cpp}` (GLFW polling, scroll callback
  chained by ImGui, capture gate from `io.WantCapture`). Right-drag orbit / mid-drag pan /
  scroll zoom / WASD fly / F focus, suppressed over the UI. Action-map/rebinding deferred.
  Built clean; static render verified via `PrintWindow` (drag feel is interactive-only). See
  "Editor camera + input" above.
- **2026-07-11** — **Gizmo snap + hotkeys** (roadmap A.1 follow-up, closing out the item that
  shipped gizmos + world-preserving reparent): **W/E/R** switch move/rotate/scale, **X** toggles
  local/world (edge-triggered `ImGui::IsKeyPressed`, gated on not-typing / not-flying), and
  **snap** (checkbox or held Ctrl) feeds ImGuizmo's per-op `snap` param with editable
  translate/rotate/scale step sizes. Resolved the "WASD is taken by the camera fly" deferral
  by noticing the fly only runs while right-mouse is held. `main.cpp`-only (no seam/renderer/
  shader/input-layer change). See "Gizmo snap + hotkeys" above.
- **2026-07-11** — **Dogfooding bugfixes** found immediately after shipping the above (all
  pre-existing, from the original gizmo commit, not the snap/hotkey change itself). Also
  discovered and documented — the hard way — that this dev environment has **no live input
  desktop**: `SendInput` reports success and focus APIs agree, but nothing actually receives
  synthetic keyboard/mouse, proven decisively with an isolated WinForms textbox test.
  Interactive UI/gizmo verification is therefore not possible from Claude here; every fix
  below was code-traced to an exact root cause and reported back by the user manually.
  Written up for reuse in `.claude/skills/verify/SKILL.md`.
  - **Round 1** (user confirmed hotkeys/snap work; found two more issues while testing):
    gizmo rotate on a spinning entity silently did nothing (`if (spin)`-gated the per-frame
    spin write, which had been stomping `rotationEuler` unconditionally); a faint trail
    followed move-dragged objects (new `PostParams::gizmoManipulating` forces SSAO
    `ResetAccumulation` during a drag).
  - **Round 2** (user reported round 1 incomplete): the trail persisted for *rotate* drags
    too, and re-enabling Spin after a manual edit snapped back to the old trajectory instead
    of continuing from the new orientation. Real fixes: made spin **incremental**
    (`rotationEuler += axis*rate*dt`, deleting the shared `spinAngle` clock entirely) so a
    paused-then-gizmo-edited orientation is the new baseline it resumes from; extended the
    same `gizmoManipulating` reset to **TAA's** `ResetAccumulation` too (previously never
    set — always accumulating), on the reasoning that TAA hits the exact same no-real-
    depth-history gap SSAO does, just for full color instead of AO alone. Not yet
    re-confirmed by the user. Also flagged (not fixed, not a regression): the Helmet's
    outline has visible gaps at hard edges — an already-documented limitation
    (`model_outline.hlsl`'s own header comment) of extruding along the plain shading normal
    for glTF models, which carry no smooth normal. See "Gizmo snap + hotkeys" above for the
    full root-cause writeups.
  - **Round 3** (user: round 2 still very present, "pretty much any interaction" — including
    dragging the **Outline-width slider**, no gizmo involved at all — shows a fading ghost of
    the old width; rotate still keeps the silhouette visibly trailing). The outline-width
    slider report was the key clue: it **proves** the trigger can't be gizmo-specific, since
    `ImGuizmo::IsUsing()` is false for a plain Inspector drag — `gizmoManipulating` could
    never have caught that case regardless of whether SSAO/TAA's reset was wired correctly.
    Confirmed the exact mechanism by reading the outline shaders directly: both
    `toon_outline.hlsl` and `model_outline.hlsl` build `CurrClip`/`PrevClip` from the *same*
    (current-frame) extruded position, varying only the WorldViewProj — so if only outline
    width changes (camera + object otherwise static), `g_WorldViewProj == g_PrevWorldViewProj`
    and the reported motion is exactly zero even though the rendered shell visibly grew or
    shrank. Renamed `PostParams::gizmoManipulating` → **`activeInteraction`**, now
    `ImGuizmo::IsUsing() || ImGui::IsAnyItemActive()` — the latter is a real, general ImGui
    query ("is any item active") that's true for ANY widget being dragged/typed/edited
    anywhere in the UI, not just the gizmo. Key reasoning for *why* this alone should suffice
    without also patching the outline shaders' motion-vector math: `ResetAccumulation` means
    "ignore history and motion vectors entirely this frame" — so a wrong per-frame motion
    vector is irrelevant precisely during the frames it's wrong (the interactive ones), since
    reprojection isn't happening on those frames at all; the *shader* fix was scoped out as
    unnecessary rather than skipped for expediency. Also found and fixed a real gap: **SSR has
    no `ResetAccumulation` field at all** (unlike SSAO/TAA) and its own
    `TemporalRadianceStabilityFactor` defaults to `1.0` — the most ghosting-prone end of its
    documented range, per SSR's own doc comment ("higher values ... more likely to exhibit
    ghosting artefacts"). Since SSR can't be reset for an interaction's duration, tuned it down
    to `0.7f` defensively (SSR is off by default; unknown whether the user had toggled it on,
    but cheap and safe to harden regardless). Not yet re-confirmed. If the trail *still*
    persists after this, the next diagnostic is decisive: disable SSAO + TAA + SSR entirely and
    check whether rotating still trails — if it does, the cause isn't a temporal post-effect at
    all (candidates: the already-known outline-gap issue reading as a "trail" on the geometrically
    complex Helmet, or something in the base render neither of us has considered yet), and the
    real double-buffered depth history (deferred twice now) should be built rather than patched
    around again.
  - **Round 4 — the actual persistent root cause** (user: ghost is present from **startup**,
    before any interaction at all, and doesn't clear on its own; toggling **"AO temporal
    (motion-vector denoise)"** off makes it vanish, back on brings it right back; same for the
    SSAO master toggle, "since denoise is a sub-feature of that"). This single report reframed
    everything — it **can't** be interaction-driven (nothing is interacting at startup), so
    every fix through Round 3 (`activeInteraction`-gated resets) was necessarily addressing a
    different, smaller problem, not this one. The one thing always running from frame 1 by
    default is **Spin**. Re-examined the Round-3-identified outline approximation with that in
    mind: `toon_outline.hlsl`/`model_outline.hlsl` computed `PrevClip` by extruding along
    *this frame's* rotation-derived normal (`g_NormalMatrix`) and only varying the
    WorldViewProj between curr/prev — exact for pure translation, but the extrude direction
    is itself rotation-dependent, so under continuous rotation this *always* slightly
    under-reports motion, every single frame, forever (not a one-off transient — a permanent
    steady-state error for as long as anything is spinning, which by default is always). That
    fully explains every symptom: present at startup (spin starts immediately), never
    self-clears (spin never stops), toggling `ssaoTemporal` off/on toggles it directly
    (reset=1 makes the wrong motion vector irrelevant; reset=0 makes temporal blending —
    and thus the error — resume immediately, not just once).
    **Real fix, not another reset/mask**: added `g_PrevNormalMatrix` to the shared cbuffer
    (grew 320→384 B) — the inverse-transpose of the *previous* frame's world matrix,
    computed in `DrawMesh`/`DrawModel` from the `prevWorld`/`objPrevWorld` matrices those
    functions already receive (no seam signature change needed — the data was already
    threaded through, just not used for this). Both outline vertex shaders now redo the
    extrude for `PrevClip` using `g_PrevNormalMatrix` instead of reusing `inflated`, so a
    rotating shell's motion vector is now computed the same principled way as its position —
    no more approximation. Verified: clean C++ build, and (the best check available without
    live input here) launched the exe and confirmed a clean console log with no Diligent
    validation errors — a cbuffer field mismatch fails loudly and immediately, so its absence
    here means the C++/HLSL layouts genuinely agree — plus a normal-looking render (all five
    objects, outlines, UI, steady 144 FPS). Not yet re-confirmed visually by the user.
  - **Round 5 — the real fix (finally built, not patched around)**: user confirmed the ghost
    *still* appeared at startup even after Round 4's mathematically-correct outline fix. Two
    targeted fixes (interaction resets, outline rotation math) both individually did what they
    were supposed to, and neither fully solved it — the pattern pointed at the architectural
    gap flagged (and deferred) since Round 1: `PostFXContext` had **never** had a real
    previous-frame depth buffer, only the current frame reused as a stand-in
    (`pPrevDepthBufferSRV = depthSRV`). That defeats depth-based disocclusion entirely for
    every effect (SSAO, TAA, SSR alike) — the exact mechanism that's supposed to catch a
    moving silhouette edge and distrust stale history there, which no amount of motion-vector
    accuracy can substitute for (a perfectly accurate motion vector still doesn't help if
    nothing can independently confirm "does the depth here actually still match what I
    remember"). Built the real thing: a new `Impl::prevSceneDepth` texture (D32_FLOAT, **same
    full BindFlags as `sceneDepth`** — `BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE`, even
    though it's never bound as a DSV; dropping DEPTH_STENCIL trips Vulkan validation errors
    `VUID-VkImageViewCreateInfo-image-01762` / `-subresourceRange-09594` on the SRV's
    depth→R32_FLOAT reinterpretation — the two textures need matching creation flags for
    Diligent's Vulkan backend to set that up the same way for both), recreated alongside the
    other offscreen targets (`CreateOffscreenTargets`, so resize is handled for free).
    `EndScene` now `CopyTexture`s `sceneDepth` → `prevSceneDepth` at the very end, once
    `RunPostFX` no longer needs the *old* `prevSceneDepth` (that call already used it
    correctly as "previous" for this frame) — the copy makes it ready to be genuinely
    "previous" for *next* frame. `RunPostFX` then feeds `prevSceneDepth`'s SRV as
    `pfx.pPrevDepthBufferSRV` instead of reusing `depthSRV`. Undefined content for exactly one
    frame on startup/resize (same class of harmless blip as `prevViewProj` starting as
    identity). Verified: clean build; first attempt (BindFlags = SHADER_RESOURCE only) hit the
    two VUIDs above on launch — real validation errors, not benign — fixed by matching
    `sceneDepth`'s BindFlags exactly; second attempt ran clean (no errors/warnings beyond
    DiligentFX's own pre-existing benign ones), steady 144 FPS, normal-looking render, clean
    exit. Not yet re-confirmed by the user — if this *still* doesn't fully resolve it, the
    remaining candidates are the already-documented Helmet outline-gap issue reading as a
    "trail" (unrelated to any of this — a genuinely different bug), or something in
    DiligentFX's own SSAO/TAA implementation neither of us has considered yet.
  - **Round 6 — actually reading DiligentFX's algorithm (the real diagnosis)**: user
    confirmed Round 5 didn't fix it either — the ghost specifically follows Spin, never
    self-clears. Five rounds of increasingly-informed guessing from the *outside*
    (attribute struct field names, doc comments) had each fixed something real but never
    the actual cause, so this round stopped guessing and read the actual shader source:
    `external/DiligentFX/Shaders/PostProcess/ScreenSpaceAmbientOcclusion/private/
    SSAO_ComputeTemporalAccumulation.fx` + `.../public/
    ScreenSpaceAmbientOcclusionStructures.fxh`. Two findings:
    1. `ScreenSpaceAmbientOcclusionAttribs::TemporalStabilityFactor` — the one exposed
       "tune the temporal aggressiveness" field, matching the README's documented API —
       is declared in the struct but **never read by any SSAO shader**. Dead parameter;
       not a lever we can use (confirmed by `grep -rl TemporalStabilityFactor` across
       every `.fx` file — only the struct declaration matches).
    2. The real algorithm (`ComputeTemporalAccumulationPS`) has a *correct*, real
       depth-based disocclusion check (`IsCameraZSimilar`, which Round 5's real
       `prevSceneDepth` now feeds properly) plus a **separate motion-magnitude-based
       variance safety net**: it computes `MotionFactor` from the current pixel's motion
       vector length, and scales down history trust when motion is large. All the actual
       tuning constants (`SSAO_TEMPORAL_MIN/MAX_VARIANCE_GAMMA` = 0.5/2.5,
       `SSAO_TEMPORAL_MOTION_VECTOR_DIFF_FACTOR` = 128, `SSAO_MAX_HISTORY_LENGTH` = 16)
       are `#define`s compiled into the shader in `external/DiligentFX` — not exposed via
       `ScreenSpaceAmbientOcclusionAttribs` at all, so unreachable from our side without
       patching a vendored submodule (off-limits per the style guide).
    **The actual root cause**: a rotating silhouette is a *view-dependent contour* — which
    physical surface points satisfy "this is the silhouette" changes every frame as the
    object turns — so no per-vertex motion vector, however correctly computed (Round 4's
    fix included), can fully represent its true screen-space motion; there's always a
    small residual error. At Spin's default rate (0.6 rad/sec, ~144 fps → a fraction of a
    pixel of motion per frame) that residual error is small enough to slip under the
    128-scaled motion threshold, so `MotionFactor` stays near 1.0 and the variance safety
    net barely engages — meaning the system heavily trusts and accumulates the (slightly,
    systematically wrong) reprojected silhouette AO across up to 16 frames of history,
    compounding a small per-frame error into a visible, persistent ghost that never
    resolves because the same error recurs every single frame for as long as anything is
    continuously rotating (which, with Spin on by default, is always).
    **Fix**: since the shader-internal thresholds are unreachable, and `ResetAccumulation`
    is the one sanctioned lever DiligentFX exposes for "don't trust history this frame,"
    renamed `PostParams::activeInteraction` → **`suppressTemporalHistory`** and folded in
    a third trigger: `gizmoActive || ImGui::IsAnyItemActive() || spin`. While Spin is on,
    SSAO/TAA never accumulate at all — every frame is a fresh, non-temporal computation
    (slightly noisier AO, no temporal denoise), completely sidestepping the question of
    whether the motion-based safety net engages correctly for slow rotation. The instant
    Spin (and any interaction) stops, normal smooth accumulation resumes on an already-
    static scene, converging cleanly within a few frames — matching the already-verified
    "SSAO doesn't ghost on a static/orbiting-camera scene" behavior. Verified: clean
    build, clean console log (no errors), steady ~144 FPS, AO contact shadows still
    visible and correctly composited under all objects, clean exit. Not yet re-confirmed
    by the user.
- **2026-07-11** — **Tooling correction: `scripts/vsenv.ps1` should not exist.** A session
  building a `tidy-md` doc-maintenance skill + LSP setup found CLAUDE.md, this file, and the
  `verify` skill all describing `scripts/vsenv.ps1` as if it were present, confirmed it
  wasn't (twice), and wrongly concluded the file was the bug, then recreated it. It wasn't:
  the user had deliberately deleted it as vestigial from the pre-CLion VS Code
  workflow (see the 2026-07-10 CLion-migration entry above) and explicitly did not want it
  recreated. Corrected by deleting the file again and fixing every doc that referenced it
  (CLAUDE.md, this file's "Build gotchas", the `verify` and `tidy-md` skills, `.clangd`,
  `docs/clion-setup-windows.md`, `README.md`) to describe the VS-environment import as an
  inline snippet or the stock "Developer PowerShell for VS 2022" shortcut instead of a repo
  script. General lesson (folded into the `tidy-md` skill): a doc referencing a missing file
  is stale in one of *two* directions — the file may need restoring, or the doc may need to
  stop claiming it exists — check which before acting, don't assume the first.
- **2026-07-11** — **Light entity component** (the light piece of roadmap A.1's
  "light/sprite/animation entity components"): promoted the global `lightDir` + Debug-panel
  slider to a `Sun` scene entity, aimed by rotation via a new `MakeLightTransform`/
  `GetActiveLight` pair in `scene.cpp`, with editable color/intensity (`LightComponent`)
  premultiplied into a new `g_LightColor` cbuffer field (384→400 B) that the two fill
  shaders multiply in. Reproduces the old default direction and look exactly; single-light
  scope (first entity found) by design. Sprite/animation entity components remain, deferred
  to roadmap phase C. Verified via build + screenshot: a regression check, a blue-tinted
  spot-check proving the shader path actually runs, and a clean console log. See "Light
  entity component" above.
- **2026-07-11** — **Roadmap audit: ToonEngineOld's own CLAUDE.md TODO lists.** Diffed
  `ToonEngineOld/CLAUDE.md`'s "Engine Roadmap TODO" / "ImGui TODO" lists (the old engine's
  own unshipped wishlist — distinct from the proven systems it actually built, which the
  carry-over survey above already covers) against the current roadmap. Folded in four
  concrete, verified gaps as new CLAUDE.md roadmap items: an explicit **input system**
  bullet (already noted as deferred here, but never promoted to CLAUDE.md's forward
  roadmap), an **asset browser panel** bullet (`ui/file_browser` + the previously-unlisted
  `ui/thumbnail_cache`), a **fixed-timestep** game-loop bullet (`main.cpp` currently runs a
  plain variable `dt`), and a **shader hot-reload** bullet wired explicitly to Diligent's
  own `IRenderStateCache` rather than a hand-rolled file-watcher. Skipped the speculative
  half of the old lists (audio, physics, particles, undo/redo, material editor, drag-drop
  material/model workflows, render-stats/profiling panel, status bar, shortcuts overlay,
  animation blending, morph targets) — no code or design work backs any of them yet,
  unlike the four folded in. See "Diligent overlap check" above for the per-item
  guiding-principle verification (checked against the actual vendored source, since
  DiligentSamples — where Diligent's own camera/input helpers actually live — isn't a
  submodule here).
- **2026-07-12** — **Scene serialization** (roadmap A.2, now shipped — see "Scene
  serialization" above for the full writeup). `core/serializer.{h,cpp}`: `SaveScene`/
  `LoadScene` to a line-based text `.scene` file covering the camera and every entity's
  hierarchy, transform, material, and light. Required extending `Entity` with
  `PrimitiveDesc primitive` + `std::string modelPath` so procedural meshes (which have no
  source file, unlike a loaded model) can regenerate on load instead of just carrying a
  live GPU handle that a fresh process can't reconstruct. Deliberately scoped to camera +
  entities, not `PostParams`/style/theme/Spin, which are editor tuning, not scene content.
  Verified with a temporary, reverted self-test (no live input here to click the actual
  buttons — see the `verify` skill) that round-tripped the scripted default scene end to
  end: 8/8 entities, correct hierarchy, valid regenerated mesh/model handles.
- **2026-07-12** — **Window icon (taskbar fix)** — see "Window icon: the taskbar needs an
  embedded resource" above for the full writeup. The user added `SetWindowIcon`
  (`glfwSetWindowIcon` via `WM_SETICON`) separately; it fixed the title bar but not the
  taskbar/Alt-Tab/shell, which GLFW's Win32 backend drives from a `GLFW_ICON`-named
  resource embedded in the .exe, not runtime state. Added `src/icon.rc.in` +
  `assets/icon.ico` (hand-built: a 22-byte ICO header prepended to the existing PNG's
  bytes) + `CMakeLists.txt` wiring (`enable_language(RC)`, `configure_file`,
  `target_sources`, all `WIN32`-guarded). Verified by screenshot: captured the taskbar
  itself (`Shell_TrayWnd`, via ordinary `CopyFromScreen` since it isn't a Vulkan swap-chain
  surface) and confirmed the real icon renders there now.
- **2026-07-12** — **Durable docking fix** (closes the item CLAUDE.md's roadmap listed as
  "fork DiligentTools, pin imgui to a `docking` commit" — see "Docking" above for the
  mechanism actually shipped, which is lighter than that). Added ToonEngine's own
  `external/imgui` submodule (`branch = docking`, pinned to the same upstream ocornut/imgui
  commit — `a23e9fb1b`, 1.92.9-WIP — the manual checkout used) and pointed
  `DILIGENT_DEAR_IMGUI_PATH` at it in `CMakeLists.txt`, before
  `add_subdirectory(external/DiligentTools)`. DiligentTools itself is untouched, not
  forked: its `ThirdParty/CMakeLists.txt` only defaults that path `if (NOT
  DILIGENT_DEAR_IMGUI_PATH)`, so the override wins and DiligentTools builds from its
  pristine upstream state (its own vendored `ThirdParty/imgui` is initialized but unused).
  Chosen over forking DiligentTools to avoid rebasing a fork every time DiligentTools is
  bumped. Verified end-to-end: a fresh `cmake --preset windows-debug` +
  `cmake --build --preset windows-debug` built clean (680 steps — the first CLI build under
  this preset dir), a launch + `PrintWindow` screenshot showed the real DockBuilder split
  layout (Scene Hierarchy left, Inspector + Debug right, scene visible through the
  pass-through center — not floating windows), and re-running `git submodule update --init
  --recursive` afterward left `external/imgui` pinned and `external/DiligentTools` clean —
  the exact command that used to silently revert docking now leaves it intact.
- **2026-07-12** — **Input system** (roadmap A.1, now shipped — see "Input system" above
  for the full writeup). Ported `ToonEngineOld/src/core/input/` into `core/input/`: action
  maps (FNV-1a hashed, keyboard/mouse/gamepad bindings, an axis type merging keyboard +
  gamepad into one named value), an input-context stack, and JSON-bound rebinding
  (`assets/input.json`, via the already-vendored `Diligent-JSON` — had to be linked
  explicitly, since `Diligent-AssetLoader` links it `PRIVATE`). Replaces the minimal
  polling-only `core/input.{h,cpp}`. Checked against the guiding principle first (nothing
  in DiligentCore/Tools to build on; DiligentSamples' `InputController` isn't vendored —
  already established in "Diligent overlap check"). `main.cpp`'s camera controls now split
  cleanly between raw mouse-drag polling (orbit/pan/zoom, unchanged) and the new action map
  (fly axes + focus, gated on `WantCaptureKeyboard` since the action queries bypass the
  capture gate by design); gamepad right-stick orbit is a genuinely new capability. Found
  and fixed a real build-system gotcha along the way: `cmake --build`'s implicit reconfigure
  can silently under-apply a multi-call `CMakeLists.txt` edit — an explicit
  `cmake --preset` resolved it (see "Build gotchas" above). Verified: clean build (663/663
  steps), a generated `assets/input.json` matching the exact expected binding schema, a
  clean launch/render/graceful-close via screenshot, and honestly-reported limits (no live
  input desktop here to drive interactively, no confirmed physical gamepad to test the new
  stick bindings against).
- **2026-07-12** — **Round 7 — the actual camera-motion root cause (SSAO/TAA/SSR ghosting on
  zoom/orbit/pan, not just Spin).** A fresh session, asked to understand the renderer in depth
  and fix SSAO "the Diligent way," found a bug none of Rounds 1-6 had touched: `RunPostFX` fed
  `PostFXContext::RenderAttributes` the *same* `postCamera` instance as both `pCurrCamera` and
  `pPrevCamera` — `Impl` had no `prevPostCamera` at all. Traced the exact mechanism by reading
  DiligentFX's actual shaders (Round 6's own lesson: read the algorithm, don't guess from
  struct field names): `ComputeReprojectedDepth.fx` unprojects the current depth through
  `g_CurrCamera.mViewProjInv` then reprojects through `g_PrevCamera.mViewProj` — with curr==prev
  this round-trips through the identical matrix and is a no-op *regardless of whether the
  camera actually moved*. SSAO's `SSAO_ComputeTemporalAccumulation.fx` uses exactly that value
  as `CurrCamZ` and compares it to the real previous frame's depth (correctly motion-vector-
  compensated) via `IsCameraZSimilar`, a relative-depth-ratio disocclusion test. During genuine
  camera motion (zoom/orbit/pan/fly) a static surface's camera-space depth legitimately changes
  frame-to-frame *because the camera moved* — the reprojection step exists specifically to
  cancel that out before comparing. With it disabled, the test compares depths that differ for
  a benign reason, and for slow/moderate camera motion the ratio often still passes the
  disocclusion threshold — so stale AO blends in across a frame where the framing genuinely
  changed, exactly the "screen burn" reported on zoom. TAA and SSR pull the same camera CB via
  `pPostFXContext->GetCameraAttribsCB()`, so this silently degraded all three temporal effects,
  not just SSAO — camera motion was simply never in any of the six prior rounds' hypothesis
  space (all of them looked at object/Spin motion vectors and `prevSceneDepth`, never at the
  camera-attribs plumbing itself). Also re-traced `DuplicateEntity` (scene.cpp) specifically to
  rule out a bad `prevWorldMatrix` init on a freshly duplicated entity — it's fine, the struct
  copy carries a correct, static `prevWorldMatrix`/`worldMatrix` pair; "duplicate + move"
  showing the same ghosting is almost certainly this same camera bug (nobody repositions a
  duplicate without also orbiting/zooming to see it), not a second one.
  **Fix**: added `Impl::prevPostCamera` (seeded to `postCamera` on frame 1, snapshotted right
  after `postFX->Execute()` each frame) — the same "copy current into history after this
  frame's read of it" idiom the code already used for `prevSceneDepth` (`CopyTexture` at the
  end of `EndScene`, Round 5) and `prevViewProj` (snapshotted in `SetCamera`), just never
  extended to the camera-attribs struct. Matches Diligent's own reference pattern too
  (`DiligentSamples/Tutorial27_PostProcessing` keeps a real double-buffered `CameraAttribs[2]`,
  never aliases curr/prev). Left the `spin`-forced `suppressTemporalHistory` reset untouched —
  that mitigates the separate, genuinely inherent Round 6 finding (rotating silhouettes are
  view-dependent contours; DiligentFX's motion-safety-net constants are compiled-in `#define`s,
  unreachable from the app), which this fix doesn't change.
  **Verified**: clean `cmake --build --preset windows-debug` (0 warnings/errors touching
  `renderer.cpp`; all warnings in the log are pre-existing, from vendored DiligentFX/
  DiligentCore source). Hit one build-environment snag along the way worth folding into "Build
  gotchas": a plain PowerShell tool session has no VS Developer environment loaded (unlike
  CLion's VS toolchain, which does this automatically) — a build attempted before importing it
  failed on `'lib.exe' is not recognized...` (not a code error). Fixed by locating the VS 2022
  install via `vswhere.exe` and dot-sourcing `Common7\Tools\Launch-VsDevShell.ps1 -Arch amd64
  -HostArch amd64` **in the same shell invocation** as the build command (shell state/env vars
  don't persist between separate tool calls here, so the import and the build must be chained
  in one command). After that, a clean build: 153/153 link steps, `ToonEngine.exe` produced.
  **Not yet visually re-confirmed by the user** — same honestly-reported limit as every prior
  round (no live input desktop here — see the `verify` skill).
- **2026-07-13** — **Asset browser panel shipped (roadmap A.1 — the last engine/editor-layer
  carry-over item).** Full writeup under "Asset browser" above. Headline points: added a
  texture API to the seam (`LoadTexture`/`DestroyTexture`/`GetTextureImGuiID`/
  `GetTextureSize`) since the current PIMPL `Renderer` had none, unlike the free-function seam
  the old `ui/file_browser`/`thumbnail_cache` reference was written against; caught two bugs
  the GL reference would have carried over silently (`IsSRGB` must be `false` or thumbnails
  render dark, and the reference's GL-bottom-origin UV flip must be dropped for Vulkan's
  top-origin decode) by comparing a captured thumbnail against its source file, not by
  inspection; and confirmed graceful shutdown genuinely exercises the new cleanup path via a
  direct `WM_CLOSE` post (sidesteps the no-synthetic-input limitation, since it's a Win32
  message post rather than `SendInput`). Hit the CMakeLists.txt reconfigure gotcha again
  (new source + new define forced the VS-env-import-must-be-chained-with-the-build failure).
  **Blocked:** click-to-preview, double-click-navigate, and double-click-to-load-scene all
  need a manual check — no live input desktop, and no `.scene` file exists yet to test the
  last one against.
