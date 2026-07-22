# ToonEngine: Memory / Archive

Detailed gotchas and decision rationale that don't need to be in `CLAUDE.md`'s
always-loaded context, but are worth keeping on hand. Pull this up when you
hit one of the errors below, or want the "why" behind a rule in CLAUDE.md.
Organized by system, not by date: each shipped feature has one section
covering its design and gotchas. **[ARCHIVE.md](ARCHIVE.md)** holds the
material that no longer earns a place in this day-to-day lookup path: full
round-by-round debugging narratives (a condensed version stays here),
superseded approaches, oversized verification logs, and survey documents
whose subject matter has since shipped. Kept in sync by the `tidy-md` skill's
MEMORY.md/ARCHIVE.md migration step; nothing there needs reading for routine
engineering.

## Build Gotchas

### `CMAKE_MT-NOTFOUND`: Windows SDK Tools Not on PATH

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

**Do not use `Launch-VsDevShell.ps1 -DevCmdArguments ...`**: that parameter
doesn't exist on all VS builds (confirmed broken on this machine's VS 2022
Community: "A parameter cannot be found that matches parameter name
'DevCmdArguments'"). `VsDevCmd.bat`'s arguments are stable back to VS 2017.

A `'vswhere.exe' is not recognized` line printed by `VsDevCmd.bat` itself is
benign/cosmetic: the environment still imports correctly (verify by checking
`mt.exe` resolves on PATH).

After fixing the environment, a stale CMake cache from a failed configure will
keep `CMAKE_MT=NOTFOUND` cached: wipe `build/<preset>/` (or at least
`CMakeCache.txt` + `CMakeFiles/`) before reconfiguring.

### `'lib.exe' is not recognized`: Editing `CMakeLists.txt` Needs the VS Env Too, Not Just a Fresh Configure

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
same VS-env-import snippet above, chained into the *same* shell invocation as the
`cmake --build`, since environment variables set in one tool call don't persist to the
next one here. A source-only change (no `CMakeLists.txt` edit) still doesn't need it, per
the skill.

### `RefCntAutoPtr.hpp` Not Found / Link Errors Consuming the `-shared` Vulkan Engine

Linking `Diligent-GraphicsEngineVk-shared` alone doesn't propagate
`Common/interface` (where `RefCntAutoPtr.hpp` lives). Link `Diligent-Common`
too. Also define `ENGINE_DLL=1` so Diligent's headers use the
`LoadGraphicsEngineVk()` runtime-load path that matches linking the engine as
`-shared` (rather than assuming a static/import-lib link).

### `CMAKE_C_COMPILE_OBJECT` Missing Internal Variable

Happens when DiligentTools is added: it pulls in zlib/libpng (plain-C
libraries), which need a configured C toolchain, but the top-level
`project(ToonEngine CXX)` only enabled C++. Fix: `project(ToonEngine CXX C)`.
The error message gives no hint that "add C" is the fix. It just says CMake
"may not be built correctly."

### Compile Time: DiligentCore Builds Every Backend by Default

On Windows, DiligentCore builds D3D11 + D3D12 + OpenGL + Vulkan by default,
regardless of what ToonEngine actually links. That was the majority of a
from-scratch build (~800 Ninja steps). Fixed by setting
`DILIGENT_NO_DIRECT3D11` / `_DIRECT3D12` / `_OPENGL` to `ON` (as
`CACHE BOOL ... FORCE`) **before** `add_subdirectory(external/DiligentCore)`:
cut it to ~650 steps and from 6 shipped engine DLLs down to 3
(`Archiver`, `GraphicsEngineVk`, `SuperResolution`). These are CMake cache
variables, so introducing/changing them needs a build-dir wipe to take effect.

### Full Rebuilds Are Expensive: Avoid Wiping `build/` Unless Necessary

Wiping `build/<preset>/` throws away every compiled object; a from-scratch
DiligentCore+Tools build takes real time (many minutes). Only wipe when a
cache is genuinely stale (toolchain fix, new top-level `option()`/language). A
normal `cmake --build` is incremental (seconds). When a wipe IS needed,
deleting just `CMakeCache.txt` + `CMakeFiles/` (not the whole directory) may
preserve already-compiled objects whose command line didn't change.

### DiligentTools Has Nested Submodules

`git submodule update --init` alone won't populate DiligentTools' own
submodules (imgui, zlib, libpng, stb, json, args). Use `--recursive`.

### LSP (clangd) Setup

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

### Implicit `cmake --build` Reconfigure Can Silently Under-Apply a `CMakeLists.txt` Edit

Editing `CMakeLists.txt` forces `cmake --build` to reconfigure as its first step (see
above), but that *implicit* reconfigure isn't always fully reliable: it's been observed to
pick up a source-file-list change while silently dropping other same-sitting edits to the
same file (new `target_include_directories`/`target_compile_definitions`/
`target_link_libraries` calls), with no error at configure time. The build just fails later
on a missing header or symbol that *should* have been reachable. Diagnosis: grep the
generated `build.ninja` for content unique to the edit; if it's genuinely absent despite
`CMakeLists.txt` on disk having it (confirm with a fresh `Read`), an **explicit
`cmake --preset windows-debug`** (not `--build`) reconfigure resolves it. That matches this
file's existing "explicit reconfigure > wipe" guidance below, now with a concrete incident
behind it. See "Input system" below for the full incident.

### Jolt Physics: Runtime Library Mismatch (`MDd_DynamicDebug` vs `MTd_StaticDebug`)

Adding Jolt as a submodule (`add_subdirectory(external/JoltPhysics/Build)`, link target
`Jolt`) fails at link time with `lld-link: error: mismatch detected for 'RuntimeLibrary'`,
because Jolt's own CMake defaults to the static MSVC CRT (`/MTd` in Debug) while
DiligentFX and the rest of ToonEngine build against the dynamic CRT (`/MDd`). Fix:
`set(USE_STATIC_MSVC_RUNTIME_LIBRARY OFF CACHE BOOL "" FORCE)` before
`add_subdirectory(external/JoltPhysics/Build)`, the same cache-variable-before-
`add_subdirectory` pattern the `DILIGENT_NO_*` block already uses. Also set
`INTERPROCEDURAL_OPTIMIZATION OFF` (ToonEngine isn't LTO; a mismatched LTO setting fails to
link the same way) and confirm `CPP_RTTI_ENABLED`/`CPP_EXCEPTIONS_ENABLED` actually match
the flags clang-cl already builds ToonEngine with before assuming a link failure is this
specific mismatch.

### Jolt Physics: `BroadPhaseLayerInterfaceImpl`: Abstract Class Error

One of the three filter classes Jolt's `PhysicsSystem::Init` boilerplate requires
(alongside `ObjectVsBroadPhaseLayerFilter` and `ObjectLayerPairFilter`),
`BroadPhaseLayerInterfaceImpl : public JPH::BroadPhaseLayerInterface`, fails to compile as
`field type 'BroadPhaseLayerInterfaceImpl' is an abstract class` unless
`GetBroadPhaseLayerName` is also overridden, but only when the build defines
`JPH_EXTERNAL_PROFILE` or `JPH_PROFILE_ENABLED` (this one does, transitively), since Jolt
declares that method `#if`-gated on those macros. Guard the override the same way:
`#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)`.

### Jolt Physics: `RVec3Arg` Aliases `Vec3Arg` (Single-Precision Builds)

A `ToVec3` overload for both `JPH::Vec3Arg` and `JPH::RVec3Arg` fails with `redefinition of
'ToVec3'`: Jolt's "real" (possibly double-precision) coordinate type `RVec3`/`RVec3Arg` is a
plain alias for `Vec3`/`Vec3Arg` unless the build defines `JPH_DOUBLE_PRECISION` (this one
doesn't), so the two overloads are the same function twice. One overload covers both call
sites.

## Window + Device Bring-Up (GLFW + Vulkan)

`main.cpp` creates the GLFW window with `GLFW_NO_API` (Vulkan owns the surface,
not GL), then drives `Renderer`. Inside the abstraction layer (`renderer.cpp`):

- **`MakeNativeWindow()`** fills Diligent's `NativeWindow` per platform: Win32
  `hWnd`; Linux `WindowId` + `pDisplay` (X11 wired, Wayland fields exist); macOS
  `pNSView` (needs a Cocoa `.mm` helper from GLFWDemo, not yet written, so macOS
  is unbuilt).
- **`EngineFactoryVk`** creates the device + immediate context, then the swap
  chain. Desktop `PreTransform` is identity (it only matters on rotated mobile
  displays).
- **Dear ImGui** is brought up in `InitUI`: construct the Diligent renderer
  backend *before* the GLFW platform backend (see below for why).

Per-frame order in `main.cpp`: `BeginFrame` (bind the HDR offscreen target) →
`DrawMesh…` → `EndScene` (run bloom, then tone-map resolve to the back buffer) →
`BeginUI` / UI / `EndUI` → `EndFrame` (`Present`). `EndScene` internally runs the
DiligentFX bloom chain first (it binds its own targets), then resolves whichever HDR
source (raw scene or scene+bloom) to the back buffer.

### Window Icon: the Taskbar Needs an Embedded Resource, `glfwSetWindowIcon` Alone Isn't Enough

`core/renderer.cpp`'s `SetWindowIcon` (decodes an image via DiligentTools' `Image.h`, calls
`glfwSetWindowIcon`) sets the icon via `WM_SETICON` on the live window; the title bar
updating immediately is real-time evidence it works. It does **not** touch the .exe's own icon
resources. GLFW's Win32 backend registers the window *class* icon by looking for a resource
named exactly `GLFW_ICON` in the executable (`external/glfw/src/win32_window.c`,
`createNativeWindow`), falling back to the generic system `IDI_APPLICATION` icon if it
can't find one. That fallback is what the taskbar, Alt-Tab, and shell kept showing even
after `SetWindowIcon` had already fixed the title bar. A title-bar-only fix reads as "half
done," not "wrong," so it's an easy thing to ship and only notice later.

Fix: embed a `GLFW_ICON` resource. `src/icon.rc.in` (one line: `GLFW_ICON ICON
"@TOON_ICON_ICO_PATH@"`) is `configure_file`'d by `CMakeLists.txt` into
`${CMAKE_CURRENT_BINARY_DIR}/icon.rc` and attached via `target_sources`. Windows-only,
guarded on `WIN32`; needs `enable_language(RC)` called once near the top of the file.
`assets/icons/icon.ico` wraps the existing `assets/sprites/icon.png` in a minimal ICO container: a
22-byte ICONDIR + ICONDIRENTRY header prepended directly to the PNG's own bytes. A single
PNG-compressed frame is valid ICO content, supported since Windows Vista (no need for the
older multi-resolution uncompressed-BMP format); built by hand in PowerShell since neither
ImageMagick nor a working Python was available in this environment. The runtime
`SetWindowIcon` call stays. The two are complementary, not redundant: the resource fixes
the class/taskbar icon, the runtime call still explicitly covers the per-window instance.

Verified by screenshot, not just a clean build: the taskbar (`Shell_TrayWnd`, a real
top-level window distinct from ToonEngine's own) was captured with ordinary
`Graphics.CopyFromScreen`. Unlike ToonEngine's own client area, the taskbar isn't a Vulkan
swap-chain surface, so plain GDI screen-copy works on it (the swap-chain-black issue only
applies to windows actually presenting through DXGI/Vulkan). The captured taskbar button
showed the real icon, not the generic fallback.

## Dear ImGui Integration

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
- **Shutdown ordering bug (the mirror image, aborts on window close):**
  `ShutdownUI` must call **`ImGui_ImplGlfw_Shutdown()` before** destroying the
  Diligent backend (`imgui.reset()` → `~ImGuiImplDiligent` → `ImGui::DestroyContext`).
  `DestroyContext` asserts `IO.BackendPlatformUserData == 0` ("Forgot to shutdown
  Platform backend?") if the GLFW backend is still registered, and that assert
  `abort()`s the process (exit 3 / abort-retry-ignore dialog) when you click the
  window's X. Tear down in the exact reverse of `InitUI`.
- **ImGui PSO depth format = `TEX_FORMAT_UNKNOWN`.** The UI is drawn to the back
  buffer with **no** depth attachment (EndScene binds a null DSV). Build the backend
  with `ImGuiDiligentCreateInfo{device, ColorBufferFormat, TEX_FORMAT_UNKNOWN}`, not
  the `(device, SwapChainDesc)` overload: the latter picks up the swap chain's depth
  format and Diligent then warns *every frame* that the bound DSV (none) doesn't match
  the ImGUI PSO.
- **`ImGui::ShowDemoWindow` fails to link.** `Diligent-Imgui`'s CMake
  deliberately excludes `imgui_demo.cpp` (demo/test code, not meant to ship).
  Don't reach for the demo window to smoke-test ImGui: write a tiny custom
  window instead (that's what `main.cpp` does).

### Docking: Own `external/imgui` Submodule, Overriding DiligentTools' Vendored One

Docking (`ImGuiConfigFlags_DockingEnable`, `DockSpaceOverViewport`, DockBuilder)
lives on imgui's separate `docking` branch. DiligentTools vendors its own imgui
as a nested submodule (`ThirdParty/imgui`, pinned to the DiligentGraphics fork's
`diligent_v1.92.1` tag), non-docking. Its own **`docking` branch is ancient**: a
pre-1.80 imgui (backends still under `examples/`, wrong API), incompatible with
DiligentTools' modern (1.92.1) `Diligent-Imgui` integration. Its `backends/` dir
doesn't even exist, so the build fails on `imgui_impl_glfw.cpp`. Don't use it.

**Durable fix: vendor our own docking-branch imgui and override the path,
rather than touch DiligentTools at all.** `DiligentTools/ThirdParty/CMakeLists.txt`
only defaults `DILIGENT_DEAR_IMGUI_PATH` `if (NOT DILIGENT_DEAR_IMGUI_PATH)`: a
supported override hook. So:

- ToonEngine has its own top-level `external/imgui` submodule, `branch = docking`,
  pinned to upstream **ocornut/imgui**'s docking tip `a23e9fb1b` (1.92.9-WIP):
  same 1.92 minor as DiligentTools' pinned 1.92.1, modern `backends/` layout,
  `Diligent-Imgui` compiles against it clean.
- `CMakeLists.txt` sets `DILIGENT_DEAR_IMGUI_PATH` to `external/imgui` (`CACHE
  PATH ... FORCE`, grouped with the other Diligent cache vars) *before*
  `add_subdirectory(external/DiligentTools)`, so DiligentTools' own default never
  applies. DiligentTools is never forked or patched. It builds from its pristine
  upstream state; its own vendored `ThirdParty/imgui` submodule is initialized but
  unused.
- **Fully durable, verified end-to-end:** a plain `git submodule update --init
  --recursive` (the exact command that used to silently revert the old manual
  checkout back to non-docking) now leaves `external/imgui` pinned at `a23e9fb1b`
  and `external/DiligentTools` clean: no more `m external/DiligentTools`
  dirtiness, no manual steps on a fresh clone. Chosen over forking DiligentTools
  itself specifically to avoid rebasing a fork every time DiligentTools is bumped
  (it moves often, and was at `API256018-28-ge637cfc` when this landed).
- Superseded the original 2026-07-09 approach (manually checking out
  DiligentTools' *nested* `ThirdParty/imgui` submodule to the same upstream
  commit, uncommitted, reverted by any `submodule update --recursive`). See
  **[ARCHIVE.md](ARCHIVE.md)** for that approach's full write-up.
- **Build stays green either way:** the docking code in `main.cpp` is guarded on
  `#ifdef IMGUI_HAS_DOCK` (imgui defines it only on the docking branch). With a
  non-docking imgui the debug window simply floats instead of docking.
- **API notes:** 1.92.x signature is `DockSpaceOverViewport(ImGuiID id = 0,
  const ImGuiViewport* = NULL, ImGuiDockNodeFlags = 0, ...)`: id is the FIRST
  arg (older took the viewport first). Enable only `DockingEnable`, NOT
  `ViewportsEnable` (multi-OS-window viewports need platform/renderer backend
  support the GLFW+Diligent combo here doesn't provide). Central node uses
  `ImGuiDockNodeFlags_PassthruCentralNode` so the 3D scene shows through.

## Toon Pipeline (Fill + Outline)

The first real shaders. `Renderer::DrawMesh` runs an **outline** pass then a
**fill** pass over the same mesh, sharing one dynamic constant buffer.

### Matrix Convention: Declare `row_major` in HLSL, Don't Transpose

Diligent's `float4x4` is **row-major / row-vector** (`v' = v * M`, and
`WVP = World * View * Proj`). HLSL's default matrix packing is column-major, so
uploading a Diligent matrix as-is would transpose it. Two fixes exist (transpose
on upload, or declare `row_major`); we use **`row_major float4x4` in the cbuffer**
(`toon_common.hlsli`) and upload verbatim, no `.Transpose()`. Shaders then use
`mul(float4(pos,1), g_WorldViewProj)` (row-vector). The C++ `ShaderConstants`
struct must match the `.hlsli` cbuffer field-for-field (4×float4x4 + 4×float4 =
320 B, grew as motion vectors and the normal matrix were added).

Projection: `float4x4::Projection(fovY, aspect, near, far, /*NegativeOneToOneZ=*/false)`
→ `[0,1]` depth for Vulkan/D3D. **No manual Y-flip needed**: Diligent handles
the Vulkan framebuffer Y-flip internally; verified the sphere renders right-side
up. Desktop swap-chain `PreTransform` is identity, so it's skipped (would matter
on rotated mobile displays).

### Winding + Culling (Verified on Vulkan)

Primitives are wound **CCW as seen from outside** the surface; both PSOs set
`RasterizerDesc.FrontCounterClockwise = True`. **Fill** culls back faces,
**outline** culls front faces. Confirmed correct empirically (sphere renders
solid, not culled/inside-out, and the outline is a thin rim, not a filled
blob). If a future mesh comes out inside-out or the outline covers everything,
the first thing to try is flipping `FrontCounterClockwise` (it's the one
convention that depends on the backend's NDC Y direction and was resolved by
testing, not derivation).

**Left-handed gotcha (bit the cube):** Diligent's `float4x4::Projection` is
*left-handed*, so "outward = front" winding is the **reverse** of the natural
right-handed `u × v = n` face order. The cube generator therefore emits its
corners `(-u-v, -u+v, +u+v, +u-v)`: the reverse of what you'd write from
`u × v = n`. The sphere/torus index pattern `(a, a+stride, a+stride+1, a+1)`
already comes out outward-front (verify at a viewer-facing vertex: its screen
triangle should be CCW). Get this backwards and the shape is culled inside-out.

### Outline = Inverted Hull, Drawn First

`toon_outline.hlsl` extrudes each vertex along its normal in object space by
`g_Outline.w`. Draw order per mesh is **outline first** (cull front → the
enlarged back-facing shell) **then fill** (cull back) on top: the fill's nearer
depth overwrites the shell everywhere except the silhouette rim.

**Hard edges need a second, smoothed normal.** A faceted mesh (per-face normals,
e.g. a cube) would gap at edges: the 3 verts sharing a corner point along
different face normals and extrude apart. Fixed with a dual-normal vertex:
`Vertex::smoothNormal` (`ATTRIB2`) is an *averaged* normal the **outline** VS
extrudes along (corner verts share it → the hull stays closed), while the
**fill** VS still shades with the per-face `normal` (crisp flat faces). For
smooth meshes the two normals are equal. The cube sets `smoothNormal =
normalize(cornerPosition)` (center-outward), which is identical for all verts at
a given corner.

### Wiring Details

- **Shared dynamic CB** bound as a `SHADER_RESOURCE_VARIABLE_TYPE_STATIC` var on
  both PSOs (set once via `GetStaticVariableByName(...)->Set()`), updated per
  draw with `MapHelper<ShaderConstants>(ctx, cb, MAP_WRITE, MAP_FLAG_DISCARD)`.
- **`MapHelper.hpp` lives in `Diligent-GraphicsTools`**, not GraphicsEngine:
  must link that target (added to `target_link_libraries`).
- **Shaders load at runtime** via
  `CreateDefaultShaderSourceStreamFactory(TOON_SHADERS_DIR)`; `TOON_SHADERS_DIR`
  is an **absolute path baked in by CMake** (`target_compile_definitions`) so it
  works regardless of CWD. The `.hlsli` `#include` resolves through the same
  factory. Shipping later would copy `assets/shaders` next to the exe and use a
  relative path.

### Non-Uniform Scale (Roadmap #1): Inverse-Transpose Normal Matrix + World-Space Outline

`Transform::scale` may now be non-uniform. Two things break under non-uniform scale,
both fixed by **one** added cbuffer matrix, `g_NormalMatrix`:

- **Shading / G-buffer normals.** `n * g_World` is correct only for rotation + *uniform*
  scale; non-uniform scale skews the normal, and the fill shading, the SSAO normal
  G-buffer, and SSR all read it, so all three go wrong at once. Fix: the standard
  **inverse-transpose** normal matrix `(World⁻¹)ᵀ`, built CPU-side in `DrawMesh` as
  `world.Inverse().Transpose()` and uploaded as `g_NormalMatrix`. Both toon VS shade with
  `mul(float4(N,0), g_NormalMatrix).xyz`. (Row-vector convention: the normal transform is
  `n * (M⁻¹)ᵀ`, same formula as column-vector. The 4×4's upper-left 3×3 is the true 3×3
  inverse-transpose even with translation present, so a full float4x4 is fine, no float3x3
  cbuffer-packing headaches.)
- **Outline width.** The inverted hull extruded a constant amount in *object* space, so
  non-uniform world scale stretched the shell (thick on the scaled axis). Fix: extrude a
  constant **world-space** width. To reuse the existing WVP path (and leave the
  motion-vector plumbing untouched), the offset is still applied in object space: take the
  true world normal `nWorld = normalize(smoothN * g_NormalMatrix)`, then map a world-space
  step back to object space through `world⁻¹`, which is exactly the **3×3 transpose of
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
`ShaderConstants` mirror must match field-for-field: a size mismatch trips a Diligent
validation error immediately, which is how the layout was confirmed clean.

**Demo:** the scene's sphere carries a fixed non-uniform `Transform::scale` (`{1.5, 0.8,
1.0}`) → a spinning **ellipsoid**, the textbook normal-matrix test (`main.cpp`'s `Object`
gained a per-object `scale`). Verified on the RTX 3080 via `PrintWindow`: the ellipsoid's
cel bands follow the true stretched surface (not skewed toward the wide axis) and its
outline stays a uniform rim; cube/torus visibly unchanged.

### Per-Object Outline Tuning (Roadmap #1)

The inverted-hull outline was always per-object *capable*: `Material::outlineColor` /
`outlineWidth` flow through `DrawMesh` into `g_Outline`, but `main.cpp` overwrote both
from one global `style` every frame, so every object shared one line. Made it genuinely
per-object, **app-side only** (no abstraction-layer/shader change):

- Each `Object` owns its outline (sphere: thin dark-red rim; cube: bold near-black edge;
  torus: dark-bronze line) via its `Material{ baseColor, outlineColor, outlineWidth }`.
- The draw loop stopped stomping outline color/width; it now overlays only the
  genuinely-global bits (band count, ambient, SSR gloss) onto a **per-draw copy** of the
  object's material, so the object's stored outline stays the editable source of truth.
- A single global `outlineScale` (default 1.0) multiplies every object's width together
  (`m.outlineWidth = obj.material.outlineWidth * outlineScale`), for dialing the whole
  scene's line weight without losing per-object ratios.
- UI: an **"Objects"** section (per object: base color + outline width + outline color,
  `PushID(i)` so labels don't collide) plus a global **"Outline width ×"** slider; the old
  single global outline width/color controls are gone. `Object` gained a `name` for labels.
- The ground keeps `outlineWidth = 0`: the per-object *disable* case.

Verified via `PrintWindow`: three visibly distinct outlines (width + color), live-tunable,
clean run. Bands/ambient stay global by design (a scene-wide shading look).

## DiligentFX / HDR Post-Processing (Roadmap #6)

Added `external/DiligentFX` as a submodule pinned to **API256018**: match the
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
  `ToneMapping.fxh`: the DiligentFX shader includes (`SRGBUtilities.fxh`, the
  dual C++/HLSL `*Structures.fxh` with their macro setup) add include-path
  plumbing not worth it for that pass.

## Bloom (DiligentFX `Bloom` via `PostFXContext`, Roadmap #1)

The bright toon bands bleed a soft glow. Implemented with DiligentFX's real `Bloom`
effect (compute-ish full-screen-triangle passes: prefilter → downsample → upsample),
all in `core/renderer.cpp` behind the abstraction layer. Per-object controls live in
`PostParams` (enable, intensity, threshold, soft-knee, radius); the debug UI drives
them live.

**The `PostFXContext` tax.** `Bloom::Execute` requires a `PostFXContext`, and Bloom
only pulls frame-size / supported-features / a copy helper / `IsPSOsReady()` from it.
It reads **no** depth/motion/camera. But `IsPSOsReady()` only flips true *inside*
`PostFXContext::Execute`, which hard-requires a current **and** previous depth SRV, a
**motion-vector** SRV, and **camera attribs**. Those feed the shared temporal
machinery (reprojected depth, closest motion, blue noise) that TAA/SSR/SSAO use and
Bloom ignores. So `Impl::RunBloom` feeds it scaffolding purely to reach the ready
gate:
- **Depth**: `sceneDepth` now also carries `BIND_SHADER_RESOURCE` (D32 → R32_FLOAT
  SRV on Vulkan). Passed as *both* current and previous (we keep no history).
- **Motion**: a frame-sized `RG16_FLOAT` target cleared to zero once in
  `CreateOffscreenTargets` (never written again; the closest-motion pass `Load`s it
  at full-res pixel coords, so it must be frame-sized, not 1×1).
- **Camera**: a zeroed `HLSL::CameraAttribs` passed as curr+prev; PostFXContext
  makes its own CB. Values are unused by Bloom, so zeros are fine.
This runs blue-noise/reproj/motion compute every frame for nothing: the accepted
cost of the "via PostFXContext" route (chosen deliberately over a self-contained
bloom).

**Compositing is a drop-in.** `Bloom`'s final upsample returns
`SourceColor + Intensity*glow` (see `Bloom_ComputeUpsampledTexture.fx`), so
`GetBloomTextureSRV()` is the **full scene+bloom in HDR**, not just the glow. So
`EndScene` just points the tone-map's `g_HDRColor` at the bloom output instead of the
raw `hdrColor`. **`tonemap.hlsl` is unchanged**. Bloom off → point back at `hdrColor`.

**Gotchas:**
- **`g_HDRColor` must be DYNAMIC, not MUTABLE.** EndScene re-points it every frame
  (scene ↔ bloom output, and the target also changes on resize). Overwriting a
  *mutable* variable's binding trips `VerifyResourceBinding` ("already bound ...
  Overwriting ... is disallowed"), a real in-flight hazard, not pedantry. A dynamic
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
- **Radient disabled.** `set(DILIGENT_NO_RADIENT ON …)`: DiligentFX's GI module is
  unused and fails a clang-cl `noexcept` static_assert. CLion (ToonEngine target only)
  never built it; a full `cmake --build`/CI (the `all` target) does. Nothing links it,
  so disabling is free.
- Bloom's shaders are **embedded** in the DiligentFX lib (a `MemoryShaderSourceFactory`
  via `DiligentFXShaderSourceStreamFactory`), so no shader-file plumbing was needed:
  only the two C++ struct headers above.

**SSAO/DoF next** would reuse this `PostFXContext`, but they *do* read depth + camera +
motion, so those inputs need to become real (actual view/proj camera attribs and, for
motion-dependent effects, real motion vectors) rather than the zero scaffolding Bloom
tolerates.

## SSAO (DiligentFX `ScreenSpaceAmbientOcclusion` via `PostFXContext`, Roadmap #1)

Darkens contact/creased areas. Unlike Bloom, SSAO reads the shared `PostFXContext`
inputs for real, so this is where they got filled in. `Impl::RunPostFX` now runs the
context once and then whichever effects are on (SSAO then Bloom), returning an AO SRV +
a bloom SRV to `EndScene`.

**What SSAO needs (and where it came from):**
- **World-space normal G-buffer** (required, `pNormalBufferSRV`; SSAO does not
  reconstruct normals from depth). Added a second scene render target
  (`normalBuffer`, RGBA16F, holds signed normals in [-1,1] directly, no encode).
  The scene pass is now **MRT**: both toon PSOs set `NumRenderTargets = 2`, and
  `toon_fill`/`toon_outline` return a `PSOutput { Color:SV_Target0; Normal:SV_Target1 }`
  writing the normalized world normal. `BeginFrame` binds + clears both targets.
- **Real `CameraAttribs`** (SSAO rebuilds view-space position/normal from depth).
  `SetCamera` now keeps `view`/`proj`/near/far split (not just `viewProj`);
  `FillCameraAttribs` fills the struct: matrices + inverses, `SetClipPlanes`,
  `f4Position` from the view-inverse translation, and
  `fHandness = view.Determinant() > 0 ? 1 : -1` (copied from DiligentFX's own
  `RadientGeometryPass`. Get this wrong and AO inverts or vanishes.)
- **Depth**: the same `sceneDepth` SRV Bloom already used.
- **Motion**: now a **real** NDC velocity buffer (see *Motion vectors* below), so SSAO
  **temporal accumulation is on by default** (`ResetAccumulation = 0`) and denoises the
  AO without ghosting the spinning objects. UI keeps a temporal toggle.

**Compositing:** SSAO output (`GetAmbientOcclusionSRV`, R8) is *visibility* (1 = open),
so the tone-map multiplies: `hdr *= lerp(1, ao*ao, strength)`. Squaring is a stylized
punch: GTAO is physically restrained (subtle on convex shapes), so raw `ao` barely
reads; `ao*ao` deepens contact shadows while leaving open areas (1.0) untouched.
`g_AO` is a **DYNAMIC** var like `g_HDRColor`; when SSAO is off/not-ready a **1x1 white
texture** (`aoWhite`) is bound so the multiply is a no-op with no shader branch.

**Gotchas / notes:**
- **It's subtle without contact geometry.** The demo added a **ground plane**
  (`MakePlane`) under the trio so there are contact shadows to see; on the original
  floating convex objects SSAO computes almost nothing. Verified correct by
  temporarily returning `float4(ao,ao,ao,1)` from the tone-map: the raw visibility
  buffer showed the torus hole dark, background white (right orientation).
- Default look: `ssaoRadius` (EffectRadius) 1.5 world units, `ssaoStrength` 1.0.
- `EndScene` gates `ssaoStrength` to 0 unless a real AO texture was produced this
  frame (`aoSRV != null`), so the white default never darkens anything.

## Motion Vectors (for SSAO Temporal / DoF)

The scene now writes a **third MRT target** (`motionVectors`, RG16F), per-pixel
screen velocity, replacing the zero texture Bloom/SSAO were fed. This is what
temporal effects reproject the previous frame with.

**Convention (get it exactly right or temporal smears):** store
`motion = currNDC.xy − prevNDC.xy` (**NDC** space, current − previous). DiligentFX
stores motion in NDC and applies the NDC→UV `F3NDC_XYZ_TO_UVD_SCALE = (0.5, −0.5)`
itself (SSAO temporal: `prevPixel = currPixel − motion·viewportSize`), so **do not**
pre-scale or flip Y: hand it the raw NDC delta. Derived + checked against
`SSAO_ComputeTemporalAccumulation.fx`; `ComputeReprojectedDepth.fx` reprojects depth
from the camera matrices separately, so the motion texture specifically needs the
**object+camera** motion.

**Plumbing:**
- `ShaderConstants`/`toon_common.hlsli` gained `prevWorldViewProj`. The toon VS
  outputs both clip positions (`CurrClip`, `PrevClip`); the PS writes
  `ComputeMotion() = currClip.xy/w − prevClip.xy/w` to `SV_Target2`. Both toon PSOs
  are now 3-RT.
- Camera motion: `SetCamera` snapshots the old `viewProj` as `prevViewProj` before
  overwriting. Object motion: **`DrawMesh` gained a `prevTransform`**: the app owns
  object history (consistent with the abstraction layer's philosophy; the renderer has no
  stable object identity).
  `main.cpp` tracks `prevSpinAngle`; the static ground passes its transform twice.
- `DrawMesh` combines them: `prevWVP = WorldFromTransform(prevT) · prevViewProj`.

**Verified** by temporarily routing the motion buffer through the tone-map
(`abs(motion)·120`): static ground + background **black** (zero), spinning objects
show **red/green rotational gradients** (opposite sides move opposite screen
directions). First frame's `prevViewProj` is identity → one frame of bad motion,
harmless (temporal rejects large deltas).

## Depth of Field (DiligentFX `DepthOfField` via `PostFXContext`, Roadmap #1)

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
- **Off by default**: it's a strong look; the user opts in. Default focus 10.5 (≈ the
  objects at camera distance 10), f/6, MaxCoC 0.015: objects sharp, near/far ground
  blurs. Verified visually (cube sharp, bokeh on out-of-focus) + clean run / graceful
  close. f/2 was way too shallow (everything blurred); a higher f-stop widens focus.

## TAA (DiligentFX `TemporalAntiAliasing` via `PostFXContext`, Roadmap #1)

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
  delta), technically imprecise, but it's < 1px and TAA tolerates it; not worth
  threading an un-jittered WVP through the cbuffer.

**Off by default**: it softens the crisp cel edges + outlines that define the toon
look. Verified: clean run, graceful close, and (the real test) the spinning objects
**don't ghost**: edges anti-alias without smearing, confirming motion+jitter are
right (wrong motion/jitter would trail badly).

## SSR (DiligentFX `ScreenSpaceReflection` via `PostFXContext`, Roadmap #1)

Screen-space reflections: ray-marches the depth buffer to reflect the scene in smooth
surfaces. Reads color + depth + **world-space normals** + a **roughness** input +
motion; returns reflection *radiance* (`GetSSRRadianceSRV`, `rgb` = radiance, `a` =
hit confidence).

- **Roughness rides in the normal buffer's `.w`**: no 4th MRT. The toon PS write
  `float4(N, roughness)`; `Material::roughness` → `g_Params.z` → `.w`. SSR reads the
  *same* normal texture as both `pNormalBufferSRV` (`.xyz`) and `pMaterialBufferSRV`,
  with `RoughnessChannel = 3` selecting `.w` (its extract pass dots the RGBA with a
  channel selector, so any channel works). `IsRoughnessPerceptual = 1` (we store
  artist roughness). `RoughnessThreshold` (0.2) gates rays: only smooth pixels reflect.
- **Simplified composite**: the "correct" SSR composite is full PBR specular IBL
  (BRDF LUT + env map + per-pixel F0), which a toon renderer has none of. So the
  tone-map just adds `ssr.rgb * ssr.a * strength` (like AO), via a `g_SSR` dynamic
  input (1x1 **black** default when off). No fresnel; good enough to mirror the scene.
- `RunPostFX` now outputs `(colorOut, aoOut, ssrOut)`: SSR, like SSAO, is a separate
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

## glTF Model Loading (Phase A: Real Assets)

Load + cel-shade real glTF/GLB models via **DiligentTools' `GLTF::Model`** (target
`Diligent-AssetLoader`), not a hand-rolled loader: it owns the GPU vertex/index buffers +
textures; we draw its primitives with our own toon cel-fill PSO (no DiligentFX / PBR
renderer). Abstraction layer: opaque `ModelHandle` + `LoadModel(path)` / `DrawModel(handle, xform,
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

**Gotchas (each cost a debugging cycle):**
1. **`ModelCreateInfo::VertBufferBindFlags` defaults to `BIND_NONE`** (only `IndBufferBindFlags`
   defaults to `BIND_INDEX_BUFFER`). Without `ci.VertBufferBindFlags[0] = BIND_VERTEX_BUFFER`
   the vertex buffer isn't bindable and the draw faults.
2. **Loader textures are `Texture2DArray`, not `Texture2D`** (one layer each in the non-atlas
   path, for the PBR renderer's array binding). The shader must declare `Texture2DArray` and
   sample slice 0 (`g_Albedo.Sample(s, float3(uv, 0))`), and the untextured fallback must be a
   **2D-array** white (a plain-2D fallback trips `ValidateResourceViewDimension`: *"dimension
   of resource view ... is Texture 2D Array, but ... expected ... Texture 2D."*).
3. **`GetVertexBuffer` / `GetIndexBuffer` / `GetTexture` take `(idx, device, context)`** and
   materialize lazily: pass device + context or they can hand back non-materialized resources.
4. **A binding-validation failure (e.g. #2) manifests as a HANG, not a clean error**: the
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
   normals bug, it's culling).

**Draw:** iterate `Model.Scenes[DefaultSceneId].LinearNodes` → nodes with `pMesh` →
`Primitives`; world = `ComputeTransforms(...).NodeGlobalMatrices[node.Index] * objectWorld`;
per primitive bind `Materials[prim.MaterialId].Attribs.BaseColorFactor` + base-color texture
(`GetTextureId(DefaultBaseColorTextureAttribId)` → `GetTexture(...)->GetDefaultView(SRV)`),
`DrawIndexed(IndexCount, FirstIndexLocation = GetFirstIndexLocation()+prim.FirstIndex,
BaseVertex = GetBaseVertex()+prim.FirstVertex)`. Verified: helmet renders cel-shaded with its
albedo, SSAO/bloom apply, motion from the spin, clean exit. (Vendored API 256018/019.)

## Skeletal Animation (Roadmap #11)

Samples every animated model's bone pose through Diligent's own
`GLTF::Model::ComputeTransforms`, not a port of `ToonEngineOld`'s `animator.cpp`: that file
turned out to implement the identical keyframe/hierarchy/inverse-bind algorithm and nothing
more, so there was nothing left to port. A new `AnimationState` carries a clip index plus
current/previous time into `DrawModel`/`DrawModelShadow`, which sample it twice (this frame,
last frame) for nodes with a glTF skin and upload the resulting joint-matrix palette into a
growable `StructuredBuffer`, skinned in the vertex shader.

A skinned node draws through a dedicated fill/outline/shadow PSO trio
(`model_fill_skinned.hlsl`, `model_outline_skinned.hlsl`, `model_shadow_depth_skinned.hlsl`,
sharing a `SampleSkin` helper in `joints_common.hlsli`) rather than a runtime branch in the
existing model PSOs, matching this file's existing "different vertex layout gets its own PSO"
precedent from the procedural-vs-model shadow split. An unskinned model's draw path is
untouched byte-for-byte: a null `AnimationState*` is the pre-#11 default at every call site.

`AnimationComponent` (`core/scene`) is a new optional entity component holding a clip index
plus playing/looping flags. Its time advances in the fixed 60 Hz sim tick alongside
physics/scripts, gated on Playing/Step like everything else there, and reverts for free
through the existing Play/Stop scene-backup mechanism since it holds no runtime handle: the
palette is recomputed from the model and time at draw time, so there's nothing to rebuild on
Play or leave stale on Stop. The Properties panel exposes an Add/Remove Animation section with
a clip combo, offered only for models with an actual skin.

Demo: `assets/models/fox.glb` (the Khronos Fox test asset, previously unused) loads as a new
Fox entity playing its first clip.

**Two real bugs, root-caused during verification:**
1. `SHADER_COMPILE_FLAG_PACK_MATRIX_ROW_MAJOR` needed to be applied uniformly across all
   three skinned shader compile sites; missing it on one left `g_Joints` misinterpreted and
   the model rendered solid black.
2. `fox.glb` ships with no NORMAL accessor at all, which Diligent's loader leaves zero-filled
   rather than generating. Fixed with a screen-space-derivative flat-normal fallback in the
   fill pixel shader, plus a zero-length guard in the outline vertex/pixel shaders (skip the
   extrude, since `normalize()` of a zero vector is NaN, not a harmless zero).

Verified live: Play mode shows the fox animating with a shadow that tracks the animated pose
and an outline that follows the silhouette; Stop reverts to bind pose; the helmet's existing
static draw is unaffected.

## 2D Sprites (Roadmap #13)

A flat, textured, alpha-blended quad carried by an entity (`SpriteComponent`: texture path +
runtime `TextureHandle`, tint, an atlas UV rect, flip X/Y), transform-oriented like any other
entity, no billboarding. `Renderer::DrawSprite` draws it into the still-bound HDR G-buffer,
between the opaque `DrawMesh`/`DrawModel` loop and `EndScene()`, not after: a sprite needs to
be occluded by opaque geometry in front of it and to go through the same tone-map/bloom
resolve everything else gets. Its PSO shares the opaque pass's 3-target MRT shape (color +
normal + motion), since a PSO's declared render targets must match whatever's actually bound
when it draws, but sets `DepthWriteEnable = False` and enables standard
SrcAlpha/InvSrcAlpha blending on the color target only; the normal and motion targets are
write-masked to `COLOR_MASK_NONE` so an unlit, blended sprite can't corrupt the SSAO or
motion-vector G-buffer.

**Draw order is correctness, not a performance nicety.** With depth writes off,
`RenderFrame` (`app/editor_render.cpp`) gathers every sprite-bearing entity and sorts it
back-to-front by *view-space* depth (`Dot(worldPos - eye, camForward)`, from
`CameraWorldBasis`), not raw distance to the camera: the former matches the axis the depth
buffer itself measures along, so the sort agrees with what occlusion would decide. Flip X/Y
is applied by the app layer, negating the relevant axis of the UV rect passed to
`DrawSprite`, not a shader branch (`ToonEngineOld`'s convention).

**Texture loading reuses `Renderer::LoadTexture`'s existing `srgb` parameter**, but a sprite
is the first caller to pass `true`: an asset-browser thumbnail (`srgb = false`) composites in
ImGui's own gamma space, but a sprite composites into this engine's linear HDR scene like
every other draw and needs the linearize-on-sample an sRGB view gives it. `g_SpriteTex` binds
`SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC` on one shared SRB, re-`Set` per sprite, the same
per-draw-varying-texture shape `model_fill.hlsl`'s `g_Albedo` already established, not a
per-texture SRB cache; its immutable sampler is linear-**clamp**, not `g_Albedo`'s wrap,
since a sprite's UV rect is often an atlas sub-rect and wrapping would bleed the linear
filter into a neighboring cell at the rect's edge.

`SpriteComponent::texturePath` is a bare filename relative to a new `TOON_SPRITES_DIR`
(`assets/sprites/`), not a full baked-in path like `modelPath`/`AudioSource::clip`;
`SpriteTexturePath` (`core/scene/scene.h`) joins the two at every load site (the demo seed,
the Properties panel's Load button, serializer rehydration), so authoring a sprite never
needs a full path typed in by hand. `assets/icon.png`/`assets/icon.ico` moved to
`assets/sprites/icon.png`/`assets/icons/icon.ico` as part of this (the demo sprite reuses the
window icon rather than adding a new binary asset just to have one).

**Mouse-pick integration is real geometry, not the generic fallback box.** `picking.cpp`'s
`EntityWorldBounds` transforms a sprite's own local quad extent (`-0.5..0.5` XY, a thin
`kSpriteQuadHalfThickness` Z, `picking.h`) by its `worldMatrix`, the same treatment a
mesh/model gets, so it's picked via bounds that actually match what's on screen. Both the
generic pick-marker-box loop (`editor_render.cpp`) and the fallback-box path (`picking.cpp`)
exclude a sprite entity for the same reason: it already has real bounds and is already
visible, so a disconnected marker box would just be noise.

Serializes as one line ("sprite <texturePath|-> <tint xyzw> <uvRect xyzw> <flipX> <flipY>"),
the runtime `texture` handle deliberately not written, rebuilt from `texturePath` on load like
`AudioSource::handle`/`modelPath` already are.

Demo: a Sprite entity reusing the window icon texture, positioned above the cube/satellite
pair; orbiting the camera past its edge visibly thins it to a line, proof it isn't secretly
billboarding to face the camera.

## Scene Graph (Phase B)

`core/scene.{h,cpp}`: an entity tree replacing `main.cpp`'s hardcoded array. `Scene` is a
flat `std::vector<Entity>` with **parents always before children**, so one forward pass
composes world matrices. `Entity` holds a name, a `parent` index (-1 = root at index 0), an
optional local `Transform`, cached `worldMatrix` / `prevWorldMatrix` (plain `Mat4`), and a
renderable, a `MeshHandle` (primitive) OR a `ModelHandle` (glTF), plus a `Material`
(primitive material / model tint + style). Ops: `EnsureSceneRoot`, `AddEntity`,
`UpdateWorldTransforms`, `DestroyScene`.

**Where the 4x4 math lives (the design call).** The hierarchy needs compose/inverse math;
`math.h` deliberately stops at vectors. Per build-on-Diligent:
- **`scene.cpp` is a Diligent-using engine TU** (the 2nd after `renderer.cpp`): it uses
  Diligent's `float4x4` for composition (no hand-rolled 4x4 math).
- **`math.h` gained a plain, math-free `Mat4`** (16 floats, row-major = Diligent's layout)
  as the abstraction layer's vocabulary for a composed world transform; `scene.cpp` /
  `renderer.cpp` convert to/from `float4x4` at the boundary (a straight element copy).
  `scene.h` + `main.cpp` stay Diligent-free.
- The abstraction layer grew **`Mat4` overloads** of `DrawMesh` / `DrawModel`; the existing
  `Transform`
  overloads now just build a world `Mat4` and delegate.

**Composition** (row-vector, v' = v·M): `worldMatrix = local * parentWorld`: local FIRST,
then parent (the reverse of glm's column-vector `parentW * local`). `LocalFromTransform` in
`scene.cpp` MUST match `renderer.cpp`'s `WorldFromTransform`
(`Scale·Rx·Ry·Rz·Translation`). `UpdateWorldTransforms` snapshots
`prevWorldMatrix = worldMatrix` first, so **motion vectors come from the scene's
double-buffered world matrices**: no per-object prev-angle bookkeeping in `main.cpp` anymore.

**Demo:** a small satellite entity is parented to the (spinning) cube and orbits it,
inheriting the cube's world transform. **Deferred to the editor step (item 5):**
`ReparentEntity` / `MoveEntityAsSibling` / `DuplicateEntity`, the topo-reorder helpers,
world-preserving reparent (needs `float4x4.Inverse()` + a TRS decompose), and
`Scene::selected`: all present in `ToonEngineOld/src/scene/scene.cpp` as the port reference.

## Editor Camera + Input (Phase B, Item 4)

**Editor camera**: orbits a movable `pivot` at `distance`, yaw/pitch. Rather than port the
old glm camera (right-handed `lookAt` / `perspective`, would mirror the view in our LH
pipeline, and Diligent Core has no ready lookAt), the abstraction layer's `Camera` gained a `pivot` and
`SetCamera` prepends `Translation(-pivot)` to the proven LH turntable view. Controls in
`core/camera.{h,cpp}` (a Diligent-using TU, like scene.cpp): orbit (yaw/pitch), zoom
(geometric on `distance`), pan/fly (move the `pivot` along the camera's world basis), focus
(set `pivot`). The **basis** is derived from the SAME Diligent `RotationX/Y` matrices the
view uses: `worldAxis = viewAxis · RotationX(-pitch) · RotationY(-yaw)` = row k of that
inverse, so it's correct-by-construction, with no hand-guessed LH signs.

**Input**: `core/input.{h,cpp}` (**superseded 2026-07-12** by `core/input/`, a full
action-map system, see "Input system" below): GLFW polling (mouse buttons + cursor delta +
keys) + a scroll callback (installed in `Input::Init` **before** `Renderer::InitUI`, so
ImGui's GLFW backend *chains* it instead of overwriting) + a **capture gate** (`SetCaptured`,
fed each frame from ImGui's `io.WantCaptureMouse/Keyboard`, a harmless 1-frame lag) so
dragging over the debug panel doesn't move the camera. `g_lastX/Y` advance every frame even
when captured, so releasing capture never yields a delta jump. Bindings (`main.cpp`):
right-drag orbit (+ WASD/QE fly), middle-drag pan, scroll zoom, F focus (origin for now).

**Deferred (still, after "Input system" below):** F-focus on the *selected* entity (needs
the editor selection, unrelated to the input layer); an in-editor rebind UI (bindings are
rebindable today via `assets/input.json`, just not from a panel); the event queue / char
callback / file-drops (`input_event.h`'s `std::span`-based stream is C++20, this project
targets C++17, and has no consumer yet; first one is the asset-browser roadmap item).
**Note:** the drag *directions* (orbit/pan signs) match common editor conventions but are
only verifiable interactively: any inverted axis is a one-line sign flip in `camera.cpp`.

## Editor UI (Phase B, Item 5: Part 1)

**Panels (`main.cpp`, ImGui, exempt from the abstraction layer).** Three docked windows around the
pass-through scene: a **Scene Hierarchy** (left), an **Inspector** (top-right), and the
existing **Debug** panel (bottom-right), laid out once via `DockBuilder` (guarded by
`dockLayoutBuilt` + `#ifdef IMGUI_HAS_DOCK`; splits are left 0.20 → right 0.34 → right-up
0.55). A trimmed **dark theme** (`StyleColorsDark` + rounding + a muted-blue accent) is
applied once after `InitUI`: pure style-struct edit, no backend state.

**Hierarchy = a flat list, not a real `TreeNode`.** It iterates `scene.entities` in vector
order and indents each row by its parent-chain depth (`ImGui::Indent(depth*16)`). This reads
as a tree **only because the vector is kept in pre-order** (parents immediately followed by
their subtree). The editor mutations all topo-reorder to preserve that; the one thing that
can break it is the **scripted scene build** (`AddEntity` is a plain append), so `main.cpp`
creates the satellite **right after** its parent cube (not last) to keep the initial scene
pre-order. Selection is a single `int Scene::selected` (click toggles it off; the cube is
selected on launch so the Inspector isn't empty).

**Deferred-mutation pattern (load-bearing).** Every structural edit reorders `entities` and
invalidates indices, so the hierarchy loop must **never** mutate mid-iteration: it only
*records* one pending op (add-child / duplicate / delete) + one pending drag-drop, then
applies them **after** the loop and fixes up `selected`. Drag-drop payload is the int index
(`"TOON_ENTITY_IDX"`); the drop **zone** is picked by cursor-Y within the row: top/bottom
quarter = sibling before/after, middle = make-child (the root only accepts children).

**Scene mutations (`core/scene.{h,cpp}`, plain index/vector work, no Diligent).**
`IsAncestorOrSelf`, `AddChildEntity`, `DeleteEntity` (whole subtree via a kill-set fixpoint),
`DuplicateEntity` (clones the subtree as a sibling, copies mesh/model **handles** + material,
so models stay shared, no re-load), `ReparentEntity` / `MoveEntityAsSibling`, plus the topo
helpers (`BuildChildrenList` / `TopoOrderFromChildren` / `ApplyReorder`, a pre-order DFS that
also patches parent indices + `selected`). Adapted from `ToonEngineOld/src/scene/scene.cpp`.
**Reparent is "simple" for now**: it sets the parent and keeps the *local* transform, so the
object jumps into the new parent's frame; **world-preserving** reparent needs the decompose
(part 2). Cycle-guarded (`IsAncestorOrSelf`), root never reparented/deleted/duplicated.

**Inspector.** Name (`InputText` over a copy-to-buffer-then-read-back, fine since the string
only changes via this widget), Transform (`DragFloat3` position / **rotation shown in
DEGREES**, converted to/from the stored radians / scale, only for a non-root entity with a
transform), Material (base + outline color, outline width, roughness, only for renderables).
The rotation display fights the spin animation for spinning entities (expected, it sticks
when Spin is off).

**Fonts + themes (part 2, done).** The UI font is **Bai Jamjuree**
(`assets/fonts/BaiJamjuree-Medium.ttf`) and there are **3 selectable themes** ported verbatim
from `ToonEngineOld/src/ui/themes.cpp`: **Amber Yellow** (default), **Gruvbox Hard**, **Gray
Stone**, chosen from a combo in the Debug panel.
- **No abstraction-layer change for the font.** The font hook I expected turned out unnecessary: the
  Diligent ImGui renderer sets `ImGuiBackendFlags_RendererHasTextures` (imgui 1.92's dynamic
  atlas, `ImGuiDiligentRenderer.cpp` `UpdateTexture`/`DestroyTexture` over `io.Textures`), so
  a font added **after** `InitUI` (context exists) and **before** the first frame uploads its
  glyph texture automatically. So `main.cpp` calls `io.Fonts->AddFontFromFileTTF(...)` directly
  (ImGui is exempt from it). Baked path via `TOON_FONTS_DIR` (CMake), like shaders/models.
- **DPI.** Font is rasterized at `18 * dpiScale` (crisp, not `FontGlobalScale`, which blurs),
  where `dpiScale = glfwGetWindowContentScale` (1.5 on the 150% dev monitor). `ApplyTheme`
  ends with `ImGui::GetStyle().ScaleAllSizes(dpiScale)` so widget metrics match: the old
  themes' padding/rounding were authored at 1×.
- **`ApplyTheme(theme, dpiScale)`** resets `GetStyle() = ImGuiStyle()` (default metrics +
  dark colors, so unset entries fall back sanely), applies the theme's colors/metrics, sets
  `WindowMenuButtonPosition = ImGuiDir_None`, then `ScaleAllSizes`. Re-runs on every combo
  switch (no compounding, the reset zeroes it first). Gray Stone authors colors as
  `0xAARRGGBB` (a `FromARGB` helper) with two `LerpColor`-derived tab tints, and uses newer
  `ImGuiCol_` enums (`InputTextCursor`, `TabSelectedOverline`, `TreeLines`, …): all present in
  imgui 1.92.9.

**Gizmos (part 2, done).** ImGuizmo is vendored as a submodule at `external/ImGuizmo`
(`src/ImGuizmo.cpp` added to the target; its `imgui.h`/`imgui_internal.h` resolve transitively
from the `Diligent-Imgui` link). `main.cpp` draws a translate/rotate/scale gizmo on the
selected entity (op/space toolbar in the Inspector), and `Renderer::GetViewProj` exposes the
camera matrices as `Mat4`.

- **No transpose, no Y-flip.** ImGuizmo's `matrix_t` is **row-major, row-vector** (`right, up,
  dir, position` = rows 0–3): the *same* convention as Diligent. So `view`/`proj`/`world` feed
  in as their raw 16 floats and `mViewProjection = view*proj` matches our `viewProj` exactly.
  I initially "fixed" a suspected Vulkan Y-flip by negating the projection's Y column. That
  was **wrong** and mirrored the gizmo. ImGuizmo's `worldToPos` already does the screen
  `y = 1 - y` flip, and it expects `[0,1]` depth (its ray uses `zNear=0`), so Diligent's LH
  `[0,1]` projection drops straight in. Verified with a one-shot dump: the world origin →
  `ndc=(0,0)` (exact screen centre). The gizmo only *looked* offset at the world origin because
  that spot is small and cluttered by the helmet/ground, moving the cube to a clear position
  showed the gizmo dead-on. **Lesson: don't calibrate a gizmo by eye against a busy origin;
  dump the projected NDC and/or move the target somewhere unambiguous.**
- `AllowAxisFlip(false)` so axes show their true directions (no auto-facing the camera). The
  gizmo draws on `ImGui::GetForegroundDrawList()` with `SetRect(0,0,DisplaySize)`; `IsUsing()`
  feeds the input capture gate (so a drag doesn't also orbit the camera). Blue **+Z pointing
  down** at the default view is correct: the camera sits below the origin looking up, so world
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

**Still deferred:** sprite / animation entity components (the light entity shipped, see
"Light entity component" below).

**Gizmo snap + hotkeys.** MEMORY.md previously deferred hotkeys here because "WASD is taken by
the camera fly," but that fly only runs *while right-mouse is held* (`main.cpp`'s camera
block, gated on `Input::IsMouseDown(Mouse::Right)`), so **W/E/R** are free the rest of the
time. Added Unity-style bindings, all in `main.cpp` (no abstraction-layer/renderer/shader/
input-layer change: `gizmoOp`/`gizmoMode` are already plain locals there, and ImGui/ImGuizmo
are exempt from it):

- **W/E/R** switch move/rotate/scale, **X** toggles local/world: `ImGui::IsKeyPressed(...,
  false)` (edge-triggered, no-repeat; a hold must not re-toggle X every frame), gated on
  `!io.WantCaptureKeyboard && !ImGui::IsMouseDown(ImGuiMouseButton_Right)` so typing in the
  Name field or flying the camera doesn't also drive the gizmo. Placed right after
  `ImGuizmo::BeginFrame()` (post-`NewFrame`, so `io.*` is the current frame's): the changed op
  is picked up the same frame by both the Inspector radios and `Manipulate`.
- **Snap**: `ImGuizmo::Manipulate`'s optional `snap` param (`external/ImGuizmo/src/ImGuizmo.h`)
  reads `snap[0]` for rotate/scale and `snap[0..2]` for translate; one `float step` per op
  (rotate in degrees, translate/scale in world units / factor) is broadcast into a
  `float[3]` at the call site and passed only `snapping ? snapVec : nullptr`. **Snapping
  engages on a checkbox OR while Ctrl is held** (`gizmoSnap || io.KeyCtrl`): Ctrl gives the
  familiar momentary-snap gesture, the checkbox an always-on mode. Per-op step fields live in
  the Inspector's "Gizmo" section (only the active op's step shows, to stay compact).

**Two bugs found dogfooding gizmo snap/hotkeys**, pre-existing since the original gizmo
commit and surfaced only once the hotkeys made dragging easy enough to drive hard (full
write-up, including a third bug that turned out to be a pure restatement of the glTF
outline-gap limitation already covered above, in ARCHIVE.md):

- **Gizmo rotate did nothing on a spinning entity, and re-enabling Spin after a manual edit
  snapped back to the old trajectory.** The spin animation set `rotationEuler`
  **absolutely** from one shared clock every frame regardless of the Spin checkbox,
  stomping any gizmo edit the very next frame. Fixed by making it **incremental**
  (`rotationEuler += axis * dt * kSpinRate` while Spin is on), so it always continues from
  whatever `rotationEuler` currently is.
- **A ghost trail followed objects while gizmo-dragging them.** The SSAO-only partial fix
  applied here (`PostParams::gizmoManipulating` forcing `ResetAccumulation`) is superseded
  by "Temporal Ghosting Fixes" below, which covers this same root cause (`PostFXContext`'s
  missing previous-frame depth buffer) alongside three others found later.

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

## Scene Serialization (Roadmap A.2)

`core/serializer.{h,cpp}` adds `SaveScene`/`LoadScene`, ported from
`ToonEngineOld/src/scene/serializer.*` but redesigned around this engine's actual Entity
shape. The old engine had no procedural primitives: every renderable carried a
`modelPath` and reloaded from disk. This engine's scripted scene is almost entirely
procedural (sphere/cube/torus/plane), so a serializer that only handled `modelPath` would
round-trip just the Helmet and Sun; everything else would come back as an empty transform
node. Reusing the old format directly wasn't an option.

**A procedural mesh needs provenance, not just a handle.** `Entity` gained two fields:
`PrimitiveDesc primitive` (`core/primitives.h`, a `PrimitiveKind` enum plus the generator
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
vector: no second pass or index remapping needed.

**Scope: camera + entities, not editor/session state.** A saved scene is exactly what the
Scene Hierarchy and Inspector panels expose (transform, primitive-or-model, material,
light, hierarchy) plus the camera. `PostParams`, the shared `style` (bands/ambient/
outline scale), the UI theme, and Spin are deliberately excluded: they're renderer/editor
tuning, not scene content, and folding them in would blur that boundary for no clear
benefit yet (nothing currently lets you attach "spin" to an arbitrary entity, it's
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
model: Load clears it on success so a freshly-loaded scene doesn't drive spin off stale
or wrong indices.

**Verified via a temporary, reverted round-trip self-test** (no live input here to click
the actual Save/Load buttons, see the `verify` skill): the scripted default scene
round-tripped through `SaveScene`/`LoadScene` with all 8 entities, correct parent indices,
correct positions, and valid regenerated mesh/reloaded model handles; the written `.scene`
file matched the format above. Full verification log in ARCHIVE.md.

## Input System (Roadmap A.1)

Ported `ToonEngineOld/src/core/input/` into `core/input/`: action maps, an input-context
stack, gamepad, and JSON-bound rebinding, replacing the minimal polling-only
`core/input.{h,cpp}` documented in "Editor camera + input" above. Checked first against the
guiding principle (build *on* Diligent): confirmed nothing to build on, see "Diligent
overlap check" above, which already established DiligentCore/Tools have no input/camera
abstraction and DiligentSamples' `InputController` isn't vendored here.

**Six files, all under `namespace toon::Input`** (the reference left the action-query free
functions and enums at global scope; unified here): `keycodes.h` (Key/MouseButton/
GamepadButton/GamepadAxis/MouseAxis, Key mirrors GLFW's own codes), `input_device.h`
(Keyboard/Mouse/Gamepad, current/previous arrays for edge detection), `input_system.{h,cpp}`
(GLFW callback wiring, the capture gate, the polling API), `action_map.{h,cpp}`
(FNV-1a-hashed actions/axes, the context stack, `RegisterDefaultEditorBindings`),
`binding_io.{h,cpp}` (JSON save/load). `input_device.cpp` was dropped: the reference's own
version was a stub whose only content was a comment admitting all its methods are inline in
the header; CMake doesn't need a `.cpp` per header, so there was nothing to port.

**Adaptations from the reference (ToonEngineOld had glm + vcpkg; this engine has neither):**
- **`glm::dvec2` → `toon::Vec2`** in the device layer, with component-wise arithmetic written
  out by hand rather than adding operators to `math.h`'s otherwise-operator-free `Vec2` (a
  one-off, not worth growing the abstraction layer's math vocabulary for).
- **No event queue.** `input_event.h`'s `Events()`/`EachEvent()` stream (plus the char and
  drop callbacks that feed it) wasn't ported: it has no consumer yet (its first is the
  asset-browser roadmap item, for drag-drop + text input), and **`std::span`, the only
  C++20 feature anywhere in the reference, lives in exactly that API**, so dropping it is
  what keeps this a clean C++17 port rather than a standard bump. Confirmed via
  `action_map.cpp`: it only reads `RawKeyboard()/RawMouse()/GetGamepad()`, never the event
  stream, so nothing else depends on it.
- **JSON via the already-vendored `Diligent-JSON`, not a new dependency**: but it must be
  **linked explicitly**: `Diligent-AssetLoader` links `Diligent-JSON` as `PRIVATE`, so its
  include dir does *not* propagate transitively to `ToonEngine` even though
  `Diligent-AssetLoader` is already linked. `CMakeLists.txt` needs its own
  `target_link_libraries(... Diligent-JSON)` plus a `target_include_directories` for
  `ThirdParty/json/single_include` (the `Diligent-JSON` INTERFACE target's own include dir
  only exposes the `nlohmann` leaf, i.e. bare `#include <json.hpp>`; the extra dir keeps the
  conventional `#include <nlohmann/json.hpp>` spelling working).
- **Scroll semantics simplified, not just ported.** The reference double-buffers
  `scrollAccum`/`scrollDelta` (`BeginFrame` latches `scrollDelta = scrollAccum` from the
  *previous* frame's accumulation), a real one-frame lag given the reference's own
  `BeginFrame()`-before-`glfwPollEvents()` loop order (the same order this engine now uses,
  see below). This engine's device layer collapses that to a single live `scrollDelta`
  accumulator, mirroring how `Mouse::delta` already worked: `BeginFrame` clears it,
  `OnScroll` (fired during the poll, which runs *after* `BeginFrame`) accumulates into it
  directly, and it's read live by that same frame's queries. Simpler than the reference and
  removes a latency bug rather than reproducing it.
- **Capture-gate parameter order kept as `(mouseCaptured, keyboardCaptured)`**: this
  engine's existing convention, the reverse of the reference's `(keyboardCaptured,
  mouseCaptured)`. `main.cpp`'s existing `SetCaptured(io.WantCaptureMouse || gizmoActive,
  io.WantCaptureKeyboard)` call needed no change.

**`main.cpp` integration is a hybrid, matching the reference's own pattern, not a blind
route-everything-through-actions rewrite.** The reference's own `main.cpp` (not just its
`action_map.cpp`) keeps mouse-drag orbit/pan/zoom on **raw** `Input::IsMouseDown`/
`WasMousePressed`/`MouseDelta`/`ScrollDelta` queries, never through the action map, while
routing only the fly axes (which need to merge keyboard *and* gamepad into one named value)
and discrete actions (focus, gizmo ops, quit) through it. Ported that same split rather than
inventing a different one: right-drag orbit/middle-drag pan/scroll zoom stay raw; fly
(`camera.fly.forward/right/up`) and focus (`camera.focus`) go through `GetAxis`/
`WasActionPressed`.

- **Default bindings keep E/Q for fly up/down**, not the reference's Space/LeftShift: this
  engine's existing scheme (`main.cpp` already used E/Q before this port), preserved so the
  port doesn't change today's feel.
- **Dropped `gizmo.*` and `app.quit` from the defaults.** The reference bound these too, but
  gizmo hotkeys stay on ImGui's own key routing here (`main.cpp`'s `ImGui::IsKeyPressed`,
  unchanged, see "Gizmo snap + hotkeys" above) and nothing calls
  `WasActionPressed("app.quit")`, so shipping them would be dead config baked into every
  generated `assets/input.json`.
- **New capability: gamepad orbit** (right stick, `camera.orbit.x/y`): ungated (a physical
  stick is never ambiguous with ImGui text focus) and scaled by `dt` (frame-rate
  independent, unlike the mouse path's per-frame pixel deltas), rather than the reference's
  flat per-frame multiplier, since this engine's loop is variable-rate with no fixed-
  timestep accumulator (see the roadmap's unscheduled fixed-timestep item). The tuning
  constant (150 px-equivalent/sec at full deflection) is an untested starting point: no
  controller here to feel-tune it against.
- **Capture-gate bypass, made explicit.** The action-map query functions read `RawKeyboard/
  RawMouse/GetGamepad`, which, like the reference, bypass `SetCaptured` entirely. Routing
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
and feed it to `BindingIO::Load(TOON_INPUT_JSON, ...)`: the same "load or write the
defaults on first run" shape as scene save/load. `BindingIO::Load`'s failure contract was
tightened versus the reference (which cleared and repopulated the caller's `InputContext`
in place, so a mid-populate exception could leave it partially overwritten): this port
parses into a side buffer under one `try`/`catch` and only assigns on full success, the same
"side-copy, swap on success" pattern `serializer.cpp`'s `LoadScene` already uses.

**Hit the "implicit reconfigure under-applies a `CMakeLists.txt` edit" gotcha for real**:
four new `target_*` calls (new sources, the JSON include dir, the `Diligent-JSON` link, two
compile defs) landed on disk but only the source-file list took effect until an explicit
`cmake --preset windows-debug` reconfigure ran; see "Build gotchas" above for the general
lesson this incident produced. Full incident write-up in ARCHIVE.md.

**Verified:** clean build (663/663 steps); `assets/input.json` round-tripped through
save-then-load with the exact expected binding schema (E/Q fly, gamepad-only orbit axes, no
dead `gizmo.*`/`app.quit` keys); launch + screenshot showed no crash/hang; graceful close.
**Not verified:** live interactive feel (a held key actually flying the camera,
`assets/input.json` edits changing behavior) and the new gamepad stick bindings against a
real controller, both blocked by this environment's lack of live input (see the `verify`
skill) and needing a manual check. Full verification log, including the gamepad-detection
side investigation, in ARCHIVE.md.

**Still deferred** (see the "Editor camera + input" update above): F-focus on the selected
entity, an interactive in-editor rebind panel (rebinding today is edit-the-JSON-and-
relaunch), and the event queue / file-drops.

## Asset Browser (Roadmap A.1)

Ported `ToonEngineOld/src/ui/file_browser.*` + `thumbnail_cache.*` onto the current engine:
the last editor-layer item from the carry-over survey. The old files were written against a
**free-function** abstraction layer (`LoadTexture`/`DestroyTexture`/`GetTextureNativeID`/...);
the current abstraction layer is a data-encapsulated `Renderer` class with no texture API at
all, so the real work here was adding one, not just moving UI code.

**Abstraction-layer addition** (`core/renderer.h`/`.cpp`): `LoadTexture`/`DestroyTexture`/
`GetTextureImGuiID`/`GetTextureSize`, mirroring the existing `meshes`/`models` handle-vector
convention (`Impl::textures`, 1-based handles, 0 = Invalid). `LoadTexture` uses
`CreateTextureFromFile` (already-linked `Diligent-TextureLoader`) with a **default**
`TextureLoadInfo`: its defaults (`IMMUTABLE`, `BIND_SHADER_RESOURCE`, `IsSRGB=false`,
`GenerateMips=true`) are exactly right, so nothing is overridden except `Name`.
`GetTextureImGuiID` returns `reinterpret_cast<uint64_t>(tex->GetDefaultView(...SHADER_RESOURCE))`
as a plain integer, not an ImGui type, so `renderer.h` stays ImGui-free; the UI casts it to
`ImTextureID` at the `ImGui::Image` call site. Confirmed against
`external/DiligentTools/Imgui/src/ImGuiDiligentRenderer.cpp:1135`
(`reinterpret_cast<ITextureView*>(pCmd->GetTexID())`, then `Set` + `CommitShaderResources(...,
RESOURCE_STATE_TRANSITION_MODE_TRANSITION)`) that this is exactly the mechanism the Diligent
ImGui backend expects: the same path the model albedo texture already goes through, so no
new state-management burden. `Renderer::Shutdown` gained `m_impl->textures.clear()` alongside
`meshes`/`models`, so thumbnails free on the idle'd device even if a caller forgets to.

**Two real bugs the GL reference would have carried over silently:**
- **`IsSRGB` must be `false`, not the seemingly-obvious `true` for a color image.** ImGui
  doesn't tone-map; its pixel shader computes `vertexColor * texture.Sample(...)` and applies
  sRGB handling to that *product* uniformly, so a bound texture's samples and the UI's own
  (gamma-space-authored theme) vertex colors have to live in the same color space. Loading
  sRGB would linearize on sample while the UI around it doesn't, making every thumbnail read
  too dark: caught before shipping by comparing a captured thumbnail directly against the
  source PNG (see Verification below), not by inspection alone.
- **Drop the reference's UV flip.** The old code passed `ImVec2(0,1),(1,0)` to `ImGui::Image`
  for GL's bottom-origin textures; Diligent/Vulkan's `CreateTextureFromFile` decodes
  top-origin (same convention the model albedo texture already uses), so the port uses
  `ImGui::Image`'s default `(0,0)-(1,1)` UVs. Carrying the flip over would have rendered every
  thumbnail upside down.

**C++17, not the reference's C++20:** `FormatTime` used `std::chrono::clock_cast`
(C++20), rewritten as the portable pre-`clock_cast` idiom (measure `ft`'s offset from the
file clock's `now()`, apply that same offset to `system_clock::now()`). The old `FileFilter`
(`.gitignore`-pattern matching, for browsing the *whole* repo) was dropped entirely rather than
ported: it also used C++20's `std::string::starts_with`, and scoping the browser to `assets/`
only (see Scope below) removes the reason it existed in the first place: a plain dotfile
check is the whole filter now.

**Scope decisions** (confirmed with the user before building): thumbnails are **images only**
(PNG/JPG/BMP/TGA decoded to textures); models/other files get a colored text tag
(`[M]`/`[D]`), no rendered model previews. Root is **`assets/` only**, not the whole repo.
Mostly **passive**: navigate/sort/preview, except double-clicking a `.scene` file, which
loads it. That last path is shared with the Debug panel's existing "Load Scene" button via a
`loadScene` lambda in `main.cpp`, not duplicated: `LoadScene` resets `scene.selected` and
invalidates every `spinners[]` index (see "Scene serialization" above), so both call sites
need the same `spinners.clear()` cleanup, and a lambda was the only way to guarantee that
without two copies of it drifting apart. `FileBrowser::Render` reports an activated file's
path back to the caller rather than knowing what a `.scene` file means itself: `main.cpp`
decides that, keeping the browser decoupled from scene/serializer semantics. Dock layout:
split off the bottom ~28% of the remaining pass-through center (after the existing
Hierarchy/Inspector/Debug splits), so the 3D viewport shrinks but nothing else moves.

**Verified:** clean build; a cropped thumbnail compared directly against its source PNG
confirmed both bug fixes above. Graceful shutdown was also genuinely exercised despite the
no-synthetic-input limitation: `PostMessage(hwnd, WM_CLOSE, ...)` is a direct Win32 message
post, not `SendInput`-based injection, so GLFW's win32 backend still handles it regardless
of focus state (a reusable technique for this environment's testing limits, see the
`verify` skill). **Still blocked:** click-to-preview, double-click-navigate, and
double-click-to-load-scene all need live input this environment doesn't have. Full
verification log in ARCHIVE.md.

**CMakeLists.txt gotcha, hit again:** adding a source file (`src/ui/file_browser.cpp`) and a
new define (`TOON_ASSETS_DIR`) forced the exact reconfigure-needs-the-VS-env failure mode in
"Build gotchas" above (`'lib.exe' is not recognized`): the fix was the same chained
VS-env-import + build in one shell call. Worth reinforcing since it was hit from a plain
(non-CLion) shell across two separate tool invocations: the first rebuild attempt, run
without re-importing the VS env in that specific call, failed the same way even though an
explicit `cmake --preset` reconfigure had just succeeded moments earlier in the environment:
the import genuinely doesn't outlive the shell process it ran in.

## Entity Behavior System (Roadmap M1.3)

The last M1 item: per-entity `Update` hooks, replacing `main.cpp`'s hardcoded spin block
(the last hardcoded gameplay stand-in in the engine). Planned via the `plan-roadmap`
skill, then implemented in the same session; see that plan
(`.claude/plans/snug-squishing-rabbit.md` at the time, not repo-tracked) for the full
ELI5 trade-off writeup. This entry is the durable technical record.

The design is native scripts (Cherno/Hazel's `NativeScriptComponent` shape), with EnTT
deferred. Surveyed Casey Muratori (Handmade Hero: flat data + enum, no framework), Jonathan
Blow (ECS is premature before you feel the need), and The Cherno (Hazel: full `entt` ECS
plus a `NativeScriptComponent` script slot with `OnCreate`/`OnUpdate`/`OnDestroy`). Landed
on the `NativeScriptComponent` *shape*, backed by the existing entity vector rather than an
`entt` registry. CLAUDE.md's roadmap already called for "a component/behavior layer, ECS
as a later scaling option," matching Casey/Blow's position independently of this research.

`Entity` gained `std::vector<ScriptComponent> scripts`: a vector, not a single optional,
since entities can carry more than one independent concern (e.g. a future `Health` script
beside `PlayerMovement`). `ScriptComponent` = `{ name; unique_ptr<Script> }`. `Script`
(`core/script.h`) is a virtual base: `OnCreate(Entity&, Scene&)`, `OnUpdate(Entity&,
Scene&, float dt)`, `OnDestroy(Entity&, Scene&)` (declared, not wired; no mid-Play
spawn/destroy yet), `Save(ostream&) const` / `Load(istream&)`. A name -> factory registry
(`RegisterScript`/`CreateScript`, a function-local static map to dodge
static-init-order issues) lets a saved name or an in-memory clone reconstruct the right
subclass.

The class itself states the rule: a `Script` holds no private simulation state.
Anything persistent lives on the `Entity`, so a script is a pure function of `(entity
data, dt)`. Checked specifically against the user's confirmed long-term direction
(determinism, rollback netcode, Jolt, multi-game reuse) during planning: virtual dispatch
itself isn't a determinism hazard, since it's a deterministic indirect call like any other
vtable call, but a script with hidden state would force a future rollback snapshot to
know about every script type. The "no private state" rule keeps that future fast/binary
snapshot needing only the data, never the script objects; that snapshot, and a
cross-platform floating-point determinism audit, are real, separate, deliberately
un-built future work, not contradicted by this design.

The load-bearing consequence: `Entity`/`Scene` lose their implicit copy operations.
`std::unique_ptr` inside `ScriptComponent` deletes `Entity`'s implicit copy ctor/assignment
(and therefore `Scene`'s); this is real, not a footnote. Gave `Entity` an explicit
deep-cloning copy constructor/assignment (`core/scene.cpp`): every field copies normally
except `scripts`, which reconstructs each entry via `CreateScript(name)` then round-trips
that one script's `Save`/`Load` through an in-memory `ostringstream`/`istringstream`,
never touching the `Renderer`, so mesh/model handles are copied as plain IDs with no GPU
re-upload. Move stays `= default` (cheap: moves the vector's buffer, never touches an
individual `ScriptComponent`), required explicitly once a custom copy ctor is declared:
every `std::move(entity)` call site (`AddEntity`, `ApplyReorder`, `LoadScene`'s
side-buffer swap) would otherwise silently fall back to the expensive copy path instead
of moving.

This one change is what kept `main.cpp`'s Play/Stop and `DuplicateEntity` compiling
with *zero* call-site changes: `sceneBackup = scene` / `scene = sceneBackup` and
`Entity dup = scene.entities[oldIdx];` all just keep working. The copy is no longer free,
but it's still a copy from the caller's side. This is a deliberate deviation from the
original plan, which anticipated splitting the file serializer into stream-based
`WriteScene`/`ReadScene` functions specifically so Play/Stop could reuse them. That would
have been wrong in practice: routing Play/Stop through a full scene-file-style reload
means calling `renderer.CreateMesh`/`LoadModel` again on every Play press and every Stop
press, and this engine never frees an individual mesh (only at `Renderer::Shutdown`):
that's a real, cumulative GPU memory leak across a single editing session's worth of
Play/Stop cycles. The explicit `Entity` copy ctor avoids the renderer entirely, so the
stream/file split turned out unnecessary and was dropped; `SaveScene`/`LoadScene` gained
script support directly instead (see below), which is all the plan's persistence
requirement actually needed.

Persistence: one line per script, `script <Name> <field...>`, mirroring how
`primitive <kind> <field...>` already works: the name resolves through the registry on
load, and the fields are whatever that script's own `Save` writes to the *same* line (no
multi-line format needed). `SpinScript::Save` uses `std::fixed`/`setprecision(6)` to match
the rest of the file's `%.6f` convention, deliberately not bit-exact (that's the
rollback-grade concern named above, not this).

First concrete script and the cleanup it enabled: `SpinScript` (`core/scripts/spin_script.{h,cpp}`)
ports the old hardcoded spin verbatim (`axis`, `speed` fields; `OnUpdate` does the same
incremental `rotationEuler +=` math). This deleted the `spinners` side-list entirely: the
`Spinner` struct, the vector, every `push_back`, and its clear-on-load/clear-on-Stop
bookkeeping. That's the exact bug class (external index list going stale on
reparent/reload/Stop) that motivated storing behavior *inside* the entity in the first
place. The `spin` bool (Tools menu / Settings panel checkbox) was renamed `runScripts` and
now gates `UpdateScripts` generally, since it no longer only affects a literal spin.

Where it plugs in (matches `docs/architecture.md`'s pre-existing "Where new systems
plug in" prediction almost exactly): `UpdateScripts(scene, dt)` runs inside the fixed
`while (accumulator >= kFixedDt)` loop, right where the spin block used to sit, before
`UpdateWorldTransforms`, inheriting the `EditorMode` gate for free (scripts never run
outside Playing/Step). `CreateScripts(scene)` fires once, at both places a Play session
begins (the Play button from Editing, and Step from Editing), alongside the existing
`sceneBackup = scene`.

Verified non-interactively (no synthetic input reaches this environment; see the `verify`
skill): clean build; a temporary forced-Playing build confirmed the Cube's rotation
advancing at the correct rate along its authored axis; a second temporary test confirmed
the copy constructor deep-clones scripts (not aliased) and that save/load round-trips
every entity's script and field values correctly. Both temporary instrumentation blocks
were fully removed before the final build. Full verification log in ARCHIVE.md.

Deferred, named so they aren't forgotten: EnTT/ECS (revisit only when entity count or
a profiled hotspot demands it); Lua scripting (the script slot is shaped for a
`LuaScript : Script` drop-in, same lifecycle shape, not a redesign); rigidbody/collider
components (M2, via the same opaque-handle pattern `renderer.h` already uses, confirmed
consistent with Jolt's own `SaveState`/`RestoreState` rollback model); UI components;
inspector "Add Script" UI (scripts attach in code for now); `OnDestroy` actually firing;
the fast binary rollback snapshot path; the cross-platform FP determinism audit; a
non-real-time (`OnAction`) `Script` sibling for a future turn-based/card game.

## Physics + Collision (Roadmap M2.1)

Planned via the `plan-roadmap` skill (`.claude/plans/lovely-twirling-dewdrop.md` at the
time, not repo-tracked, see that plan for the full ELI5 trade-off writeup and the
Q&A that locked in the design below), then implemented across six phases in the same
overall session. This entry is the durable technical record.

Research (per `plan-roadmap`'s own checks) found ToonEngineOld had zero physics to port
from and Diligent has no physics of its own (expected, it's a renderer; its
`AdvancedMath.hpp` ray/AABB helpers remain useful later for picking, not used yet). **Jolt
Physics** (MIT, C++17, first-class clang-cl support) was the clear pick: it adds as a plain
CMake submodule and explicitly recommends a fixed 1/60 s step, exactly the `kFixedDt` loop
M1 already built.

### Phase A: `Transform.rotation` Becomes a `Quat`

`Transform.rotationEuler` (a `Vec3`) was replaced with `Transform.rotation` (a `Quat`,
`core/math.h`, Diligent-free) everywhere, ahead of any Jolt code, because physics write-back
and render interpolation both need it: a physics step naturally produces a quaternion each
tick, and decomposing that to Euler and back every frame is both lossy and unnecessary work,
while interpolating rotation between two fixed-sim ticks wants **slerp**, not a per-axis
lerp (which has a gimbal-adjacent "long way round" caveat the old code had to note and
slerp removes outright). `scene.cpp`'s `LocalFromTransform` now builds rotation via
`QuaternionF::ToMatrix()`; `DecomposeToTransform` extracts a quaternion straight from the
world matrix via `QuaternionF::FromRotationMatrix` (the old gimbal-lock special
case is gone, not papered over). The inspector's Rotation field still edits Euler degrees,
converting at the widget boundary only; a hidden "Euler hint" like Unity's (to stop 190°
from redisplaying as −170°) is deferred polish, not a correctness gap.

Composition order took a hand-derived check to get right: the existing convention (still
used by `spin_script.cpp` and everywhere else) applies rotation as "X, then Y, then Z", i.e.
`Rx * Ry * Rz` in the matrix form already in use. Verified against Diligent's actual
`Mul`/`ToMatrix` convention with concrete 90°-rotation test cases (by hand, not assumed) that
the matching quaternion composition is `Normalize(qz * qy * qx)`, **not** the more
intuitive-looking `qx * qy * qz`, since quaternion multiplication order and matrix
multiplication order invert relative to each other under Diligent's row-vector convention.
`QuatFromEuler`
encodes this order once, so every call site (scripts, the inspector, serialization) gets it
free and consistently.

Serialization writes `rotation x y z w` (4 floats). Load detects token count for back-compat:
3 tokens → parse as the old Euler triple and convert to a quaternion; 4 → parse as a
quaternion directly. Old `.scene` files still load, unchanged.

### Phase B: The Physics Abstraction Layer, Twin to the Renderer's

`core/physics.h`/`core/physics.cpp` mirror `core/renderer.h`/`core/renderer.cpp` almost
exactly, on purpose: an opaque `BodyHandle` (same `enum class : uint32_t { Invalid = 0 }`
shape as `MeshHandle`/`TextureHandle`), a data-encapsulated `PhysicsWorld`, and a hard rule
that every
`JPH::` type and Jolt header stays inside `physics.cpp`. `physics.h` speaks only `toon::`
types and plain enums, the same "Diligent-free" contract `renderer.h` keeps for Diligent.
`PhysicsWorld::Init` does Jolt's one-time boilerplate: `RegisterDefaultAllocator`, a
`Factory`, `RegisterTypes`, a `TempAllocatorImpl`, a `JobSystemThreadPool`, the three
required filter classes (`BroadPhaseLayerInterface`/`ObjectVsBroadPhaseLayerFilter`/
`ObjectLayerPairFilter`: see "Build gotchas" above for the abstract-class trap one of
these hit), and `PhysicsSystem::Init` with a default gravity.

Read-back (`GetBodyTransform`) uses Jolt's `GetPositionAndRotation`, deliberately **not**
`GetCenterOfMassPosition`: the latter returns the body's center of mass, which only
coincides with its origin for a shape whose mass is symmetric about that origin (true for
every M2.1 shape today, but the distinction matters the moment an off-center collider or a
compound shape shows up, so the correct call was used from the start rather than relying on
today's shapes hiding the bug).

### Phase C: `ColliderComponent` and `RigidBodyComponent` Are Independent, From the Start

Locked in during the plan's own Q&A, before any code: a `ColliderComponent` (shape +
extents) and a `RigidBodyComponent` (mass/friction/restitution/type) are two separate
`std::optional` fields on `Entity`, matching the grain `LightComponent`/`ScriptComponent`
already established, not one merged "Physics" component. Collider alone means an implicit
static collider (a wall/floor with no authored body); collider **and** body means a
dynamic/kinematic mover: the same split Unity's `Collider`/`Rigidbody` and Godot's
`CollisionShape`/`RigidBody` both use, for the same reason: a level's static geometry
vastly outnumbers its movers, and forcing every collider to carry unused mass/friction
fields would misrepresent that. `RigidBodyComponent::handle` is transient runtime state
(never serialized), rebuilt every time Play starts, see Phase D.

### Phase D: Play Builds the World, Stop Tears It Down, Physics Steps in the Fixed Tick

`BuildPhysicsWorld(physicsWorld, scene)` (`main.cpp`) is pure derived state: `Clear()`s the
world, then for each entity with a `ColliderComponent`, synthesizes an implicit static
`RigidBodyComponent` if none was authored, and calls `CreateBody` seeded from the entity's
current world pose. It runs once whenever Play (or Step-from-Editing) begins, right
alongside the existing `sceneBackup = scene` / `CreateScripts(scene)`, reusing the exact
Play/Stop disposable-sandbox convention M1.3's scripts already rely on. Stop's `scene =
sceneBackup` is preceded by `physicsWorld.Clear()`, so a physics session leaves no residue
behind, the same guarantee Stop already gave scripts.

Inside the fixed `while (accumulator >= kFixedDt)` loop, after `UpdateScripts`: every
static/kinematic body's entity transform is pushed into Jolt (`SetBodyTransform`), then
`physicsWorld.Step(kFixedDt)` runs, then every dynamic body's Jolt pose is read back
(`GetBodyTransform`) through `ComposeWorldMatrix` and `SetEntityWorldMatrix` into
`entity.transform`, landing exactly where a script's write would, so the existing render
interpolation (`alpha`, now slerping the quaternion) smooths physics motion for free, with
no physics-specific interpolation code needed.

This assumes every collider-bearing entity is root-parented: a real, documented
simplification, not an oversight: a nested collider is seeded once at Play-start but never
correctly re-synced against a moving parent afterward, since Jolt bodies simulate in world
space and `BuildPhysicsWorld` doesn't fold a parent chain in. Fine for M2.1's flat demo
scene; a real hierarchy fold is future work if a nested collider is ever needed.

Non-uniform scale has no exact representation for every shape: a `Box`'s three half-extents
bake a non-uniform scale in exactly, one axis at a time (`ScaledColliderExtents`,
`main.cpp`), but `Sphere`/`Capsule` only have 1-2 degrees of freedom, so a non-uniform scale
there is approximated by the largest relevant axis, with a one-time `stderr` warning naming
the entity, chosen over silently picking an axis, so a misconfigured entity is at least
discoverable. The demo scene (`Ground`, `PhysicsCube1/2`, `PhysicsSphere1`) drops a few
dynamic primitives above a static-collider ground plane so pressing Play makes them fall and
land/stack, the same "visible proof" role the spin demo played for M1.3's scripts.

### Phase E: The Inspector: A Correction on Component UI, Not Just Physics

First attempt merged Collider + RigidBody into one nested "Physics" inspector section with
enable/disable **checkboxes** (RigidBody's checkbox only appearing once Collider's was
checked; unchecking Collider cascaded to clear RigidBody too). The user corrected this
directly and specifically: the plan already called for **separate** components with real
**Add/Remove buttons**, not a nested enable/disable toggle. In their own words: "I should be
actually able to add or remove any component fully from the properties." They also asked why
the implementation had silently diverged from what the plan itself said. The plan's own text
did say "Add/Remove buttons for each component"; the substitution to checkboxes was an
unrequested simplification, not a plan ambiguity.

Rebuilt as four fully independent `SeparatorText` sections in the Properties panel (Light,
Collider, Rigid Body, Scripts), each showing either its fields plus a **Remove** button, or
just an **Add** button, with zero nesting or cascade between any of them (confirmed via
`AskUserQuestion` that Scripts should get the same treatment; Material was explicitly left
as a plain always-present block for now, in the user's own words "when UI comes into the
picture we might, but leave it as is for now", since every entity already has one and
there's no add/remove semantic for it yet). The underlying `scene.h` data model had been
correctly separate since Phase C; only the UI had merged them, which is exactly why this was
a presentation-layer fix, not a data-model change.

### Phase F: Collider Debug Wireframes, and Two Bugs the Verification Pass Caught

`ColliderWireframe(shape, extents)` (`core/physics.cpp`, pure math, no Jolt dependency)
returns a flat line-segment list: a box's 12 edges, a sphere's 3 orthogonal great circles,
a capsule's 2 rings + 4 struts + 4 hemisphere-cap arcs. `Renderer::DrawWireframe` draws it
through a small dedicated PSO: `PRIMITIVE_TOPOLOGY_LINE_LIST`, depth test **off** (an
always-on-top debug overlay, deliberately chosen over depth-tested lines specifically to
avoid the G-buffer MRT-compatibility risk a depth-tested variant would add), drawn directly
onto the already-bound back buffer between `EndScene()` and `BeginUI()`, the same "back
buffer only" PSO shape as the tonemap pass, reused rather than reinvented.

Two real bugs surfaced only once this was actually built and screenshotted, not during
implementation:

1. `DrawWireframe`'s first draft called `ToFloat4x4` (a file-scope `static` helper) before
   that helper's own definition later in `renderer.cpp`: `use of undeclared identifier`.
   Moved the function to sit after `ToFloat4x4`'s definition.
2. A clean build still errored **every frame** at runtime once colliders were actually
   shown: `No resource is bound to variable 'Constants' in shader 'wireframe PS'`.
   `wireframe.hlsl` declares one `Constants` cbuffer referenced by both `VSMain` and
   `PSMain`, but Diligent compiles each shader stage separately, so each stage gets its own
   copy of that variable needing its own bind; `CreateWireframePipeline` only bound the
   vertex-shader copy. The fix is the exact pattern already used, and already correct,
   everywhere else in `renderer.cpp` a `Constants` cbuffer spans both stages: the toon and
   model PSOs each bind `Constants` for `SHADER_TYPE_VERTEX` *and* `SHADER_TYPE_PIXEL`
   separately, and the wireframe PSO had only copied half of that pattern.

A third issue was caught by inspection before it ever ran: the first draft fed
`entity.worldMatrix` (the renderer's own, possibly-scaled, possibly-nested placement) and
raw unscaled `collider.extents` straight into `ColliderWireframe`, following the plan's own
shorthand wording literally. That would silently disagree with what Jolt actually simulates
for any non-uniformly-scaled Sphere/Capsule (see Phase D's `ScaledColliderExtents` above): a
physics-debug overlay that doesn't match physics defeats its own purpose. Fixed by having
the overlay call the exact same `ScaledColliderExtents` helper and build a scale-free
position/rotation matrix, mirroring `BuildPhysicsWorld` exactly rather than the renderer's
placement of the entity. The two happen to produce identical results for today's
root-parented, unit-scale demo scene, but only one of them is correct in general.
`ScaledColliderExtents` gained a `logWarnings` flag (default on), so the per-frame overlay
path, unlike the once-per-Play-start `BuildPhysicsWorld` call, doesn't spam `stderr` 60+
times a second on a misconfigured entity.

Verified non-interactively (no synthetic input reaches this environment; see the `verify`
skill): clean build, then screenshots with a temporarily-forced-on `showColliders` cross-
validated against the same flag forced off. On: the Settings panel's Physics section and its
"Show Colliders" checkbox render and toggle correctly; the Ground's box wireframe traces the
plane edge; `PhysicsCube1`'s box wireframe snugly bounds the cube (a second scale from
Ground's own box, both correct); `PhysicsSphere1`'s three-great-circle wireframe matches the
sphere exactly. Off: no wireframes anywhere, no regressions elsewhere in the scene.
`PhysicsCube2` fell outside the static camera framing available without live input, and no
demo entity uses `Capsule`, so those two shapes weren't re-confirmed visually post-fix (Box
and Sphere were, at two different scales each); `Capsule`'s geometry was checked
analytically during implementation instead.

Deferred, named so they aren't forgotten: mouse-pick via raycast and contact events → scripts
(both moved to CLAUDE.md's roadmap as immediate follow-ups, the `Raycast` abstraction-layer
method already shipped, just isn't wired to anything yet); mesh/convex-hull colliders (Box/Sphere/
Capsule only today); Jolt's `CharacterVirtual` character controller; triggers/sensors;
continuous collision detection; a physics-settings panel (gravity/substeps); the inspector
"Euler hint" polish (Phase A); non-uniform-scale collider approximation beyond baking; Jolt
`SaveState`/`RestoreState` for a fast binary rollback snapshot; the cross-platform FP
determinism audit.

## Audio (Roadmap M2.2)

**miniaudio** (single-header, MIT, zero build-system friction) via a new `external/miniaudio`
submodule. `core/audio/audio.h`/`audio.cpp` is a third seam, the same PIMPL shape as
`Renderer` and `PhysicsWorld`: an opaque `SoundHandle` (`enum class : uint32_t`, `Invalid = 0`,
identical to `BodyHandle`/`MeshHandle`), a `class AudioEngine` whose header speaks only
`toon::Vec3` and plain structs/enums, and every `ma_engine`/`ma_sound` type confined to
`audio.cpp` via an `Impl`. `miniaudio_impl.cpp` is the one TU that does
`#define MINIAUDIO_IMPLEMENTATION` before including the header (a single-header-library
convention: exactly one TU emits the implementation, every other includer just gets
declarations), keeping that macro out of `audio.cpp` itself.

**API shape** (`audio.h`): `PlayOneShot`/`PlayOneShotAt` are fire-and-forget (miniaudio frees
their resources itself, no handle to track); `Play(SoundDesc)` returns a `SoundHandle` for
anything that needs to be stopped, repositioned, or re-volumed later (scene emitters, music),
mirroring `PhysicsWorld`'s handle lifetime. `SoundDesc` carries `spatial` (3D-positioned vs.
plays-everywhere) and `stream` (decode-as-you-play for long music vs. load-fully-upfront for
short SFX) as independent flags, plus `maxDistance` for attenuation falloff. `SetListener` is
driven from the **editor camera every rendered frame, not the fixed sim tick**: audio is a
presentation concern like rendering (the interpolated camera pose), not a simulation concern
like physics, the same category `Transform` interpolation already put rendering in for M1.
`PauseAll`/`ResumeAll`/`StopAll` mirror `PhysicsWorld::Clear()` for the Playback panel's
Play/Pause/Stop session control.

**Engine-side plumbing**: a new `AudioSource` entity component (`scene.h` + `serializer.cpp`,
same shape as the existing `RigidBody`/`Collider` components: present only when
`entity.audioSource.has_value()`) and `app/audio_glue.{h,cpp}`'s `BuildAudioWorld`, a direct
structural twin of `BuildPhysicsWorld`: walks the scene once at Play-start, calls
`AudioEngine::Play` for every `AudioSource`, and stores the returned handles for teardown at
Stop. Properties panel gained Add/Remove Audio Source with a "Preview" toggle that auditions a
source's exact authored settings (loop/volume/pitch/spatial) via the same `Play`/`Stop` calls
a real Play session would use, works in any Editing/Playing/Paused mode since `AudioEngine`
itself tracks no such state. Settings panel gained master volume + mute. Clip paths resolve
against a new `TOON_AUDIO_DIR` when not already absolute, the same pattern
`assets/models`/`assets/fonts` use elsewhere.

**Bug caught during implementation**: `Entity`'s copy constructor had an explicit member-init
list that predated `audioSource` and was never extended when the field was added, so every
scene copy (`Play`'s pre-play snapshot, `DuplicateEntity`) silently dropped an entity's audio
component. Fixed in the same commit. General lesson, same shape as the `RigidBody`/`Collider`
addition before it: an explicit copy-constructor member list is a trap for every field added
after it exists; a defaulted copy constructor would not have had this failure mode, and is
worth preferring the next time `Entity` grows a component, unless a field genuinely needs
non-default copy behavior.

## Mouse-Pick (Roadmap M2.3)

Planned via the `plan-roadmap` skill, then implemented in the same session. Ships as
**geometric ray-vs-bounds picking**, not `PhysicsWorld::Raycast`: Jolt bodies only exist while
Playing (`BuildPhysicsWorld` runs at Play/Step, `Clear()`s on Stop), and only collider-bearing
entities would be hittable, so editor selection needs a path that works in Editing mode for
every visible entity, the same reason Unity's Scene view and Unreal's/Godot's editors decouple
their own editor picking from their runtime physics raycast. `PhysicsWorld::Raycast` stays
exactly what it was shipped for (M2.1): gameplay use, untouched.

**Half 1: screen click to world ray, behind the renderer seam.** `core/math.h` is
deliberately math-free (no matrix inverse), and view/projection matrices stay behind the seam
by design, so the unproject lives in `renderer.cpp`: `Renderer::ScreenPointToRay` inverts the
exact `view * proj` matrix `SetCamera` built that frame (`m_impl->viewProj`), converts the
mouse pixel to NDC (flipping Y; Diligent/Vulkan's depth range is `[0,1]`, not OpenGL's
`[-1,1]`, so near = z 0, far = z 1), and transforms both NDC points as **row vectors**
(`v * M`), matching the HLSL shaders' own `mul(v, M)` convention (`toon_common.hlsli`) --
Diligent's own free `operator*(Matrix, Vector)` is the opposite (column-vector), so a small
`TransformRowVector` helper does the actual multiply. The scene renders fullscreen behind the
dockspace's `PassthruCentralNode`, so the "viewport" is the whole window: `io.MousePos`/
`io.DisplaySize` feed straight in, no panel-offset math needed.

**Half 2: ray to nearest entity, app layer, plain math.** The renderer can't know about
`Scene` (that would leak `scene.h` into `renderer.h`), so it only exposes per-resource
**local** bounds: `Renderer::GetMeshBounds`/`GetModelBounds`, a min/max sweep done once at
`CreateMesh`/`LoadModel` time (model bounds via `GLTF::Model::ComputeBoundingBox` with an
identity `RootTransform`, giving the model's own object-space box). New `app/picking.{h,cpp}`
does the loop: `PickEntity` transforms each renderable entity's 8 local-bounds corners by its
`worldMatrix` and re-derives a world AABB (a rotated box isn't just its two corners
transformed); a light or empty anchor with no mesh/model gets a fixed `kPickBoxHalfExtent` box
centered on its world position instead, so it stays clickable with no geometry of its own. A
standard ray-vs-AABB slab test keeps the nearest hit and writes it to `scene.selected`.

**Wiring.** `DoMousePicking` (`main.cpp`, right before `DrawGizmoOverlay`) triggers on a
left-button release whose `ImGui::GetMouseDragDelta` stayed under a few pixels (a click, not
a drag -- camera orbit/pan use the right/middle buttons, so left is free), gated on
`!io.WantCaptureMouse && !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()` so a panel click or a
gizmo drag never triggers a pick (the guard reads last frame's `IsUsing()`/`IsOver()`, the
same one-frame lag the existing input capture-gate already accepts). A collider-less entity's
fallback pick box is drawn as a wireframe cube in `app/editor_render.cpp`'s existing
`DrawWireframe` overlay (reusing `ColliderWireframe`'s `Box` case, sized to exactly match what
`PickEntity` tests), so a light or empty anchor reads as clickable instead of a dead zone.

**Verified:** clean build; every ImGui/ImGuizmo call (`IsMouseReleased`, `GetMouseDragDelta`,
`IsOver`/`IsUsing`) checked against the vendored headers; a temporary instrumented run
(removed before commit) logging a synthetic center-screen ray confirmed `ScreenPointToRay`'s
direction matched the camera's actual orbit geometry by hand (`sin(pitch)`/`cos(pitch)`
matched the logged ray direction to three decimal places) and `PickEntity` resolved to the
geometrically correct nearest entity using live scene state; no synthetic input reaches this
environment's windows (see the `verify` skill), so a live click couldn't be driven directly.

## Contact Events to Scripts (Roadmap #9)

Shipped by a separate, concurrently running Claude Code session while this session worked on
mouse-pick and the roadmap-skill reorg above; written here from the actual shipped code, not
just the commit message, per this skill's own standard of verifying against real code.

Three new `Script` hooks (`core/scene/script.h`): `OnCollisionEnter`/`OnCollisionStay`/
`OnCollisionExit(Entity &self, Scene &scene, int other, const Vec3 &point, const Vec3
&normal)`. `other` is an entity INDEX, matching `Entity::parent`/`Scene::selected`/
`ReparentEntity`'s own convention, never a raw `Entity&` alias into the entities vector.
`normal` points away from `self` toward `other` (Unity's own convention); Enter fires once
when contact starts, Stay every tick it continues, Exit once it ends.

**The physics seam gained `ContactPhase`/`ContactEvent`/`ConsumeContactEvents`**
(`core/physics/physics.h`). The real complication: Jolt's contact callbacks
(`JPH::ContactListener::OnContactAdded/Persisted/Removed`) fire from Jolt's own job-system
worker threads, concurrently with the rest of this single-threaded engine, so
`PhysicsWorld::Impl` doubles as a `JPH::ContactListener` that only queues events behind a
`std::mutex` during `Step()`; `ConsumeContactEvents` drains the queue on the main thread right
after `Step()` returns, the only point it's safe to touch from the caller's thread. On Exit,
Jolt no longer has live contact geometry for the (possibly-destroyed) pair, so `point`/
`normal` are the last values seen on that pair's most recent Enter/Stay, not live geometry.

**`app/physics_glue.h` gained the resolution plumbing.** `BuildPhysicsWorld` now also fills an
`outBodyToEntity` map (`BodyHandle`'s raw id -> owning entity index), built once per Play/Step
session (no entity is created/destroyed mid-Play). `DispatchContactEvents` drains
`ConsumeContactEvents` and resolves each event's two `BodyHandle`s back to entities via that
map, firing both sides' scripts symmetrically (the reported normal flipped per side, so each
side always sees the normal pointing away from itself). Called once per fixed tick in
`editor_tick.cpp`, immediately after `PhysicsWorld::Step()`, gated on `runScripts` like
`UpdateScripts` (no point draining events nothing will react to).

**Verified** with a throwaway logging script plus forced Play-on-launch (no live input
reaches this environment): confirmed correct Enter/Stay/Exit sequencing and contact geometry
against the demo scene's falling physics bodies, then the throwaway script was fully removed
before the shipping commit.

## Fixed Timestep + Render Interpolation (Roadmap M1.1)

Replaced `main.cpp`'s single variable-`dt` frame loop with an accumulator-driven fixed 60 Hz
simulation tick, decoupled from the (still variable) render rate, with full "Fix Your
Timestep!"-style render interpolation (the user's explicit choice over the simpler
render-latest-tick alternative). Kept inline in `main.cpp` rather than extracted into its own
module: a scoped decision, see `docs/architecture.md`'s "Where new systems plug in".

**Mechanism:** `kFixedDt = 1/60` and a `double accumulator`. Each frame, `frameTime` is
clamped to <= 0.25s before feeding the accumulator (a spiral-of-death guard: a window
drag/resize genuinely stalls `glfwPollEvents` for its duration and would otherwise dump a
huge time debt in at once). A `while (accumulator >= kFixedDt)` runs zero-or-more fixed
steps, each calling `SnapshotSimState(scene)` (copies every transformed entity's `transform`
into `prevSimTransform`) before advancing gameplay by `kFixedDt`. After the loop, `alpha =
accumulator / kFixedDt` feeds `UpdateWorldTransforms(scene, alpha)` (default `alpha = 1.0`
for callers outside the loop), which composes each entity's world matrix from
`lerp(prevSimTransform.value_or(transform), transform, alpha)` via `LerpTransform`
(component-wise on position/rotation/scale; a documented Euler/quat-lerp approximation, fine
for small per-tick deltas). Camera navigation deliberately stays on the variable `dt`: it's
the user driving the editor, not the simulation.

**The motion-vector chain needed zero new bookkeeping.** `UpdateWorldTransforms` already
snapshotted `prevWorldMatrix = worldMatrix` before recomputing; since `worldMatrix` is now
always built from the interpolated pose, `prevWorldMatrix` automatically becomes "last
*rendered* frame's interpolated world," so TAA/SSAO/SSR keep measuring motion between
consecutive rendered frames exactly as before. `prevSimTransform` defaults to `nullopt` and
reads as `value_or(transform)`, so a fresh/loaded/anchor entity interpolates with itself: no
ghost on spawn or scene load.

Verified: clean build, then a launch + screenshot showing crisp (non-smeared) silhouettes on
the spinning cluster and a Cube rotation that advanced correctly over sustained runtime with
no NaN/corruption/crash. Interpolation smoothness itself and the gizmo-drag "no ghost-glide"
path aren't independently confirmed interactively (no live input desktop here); both follow
from the mechanism above by direct code inspection (`prevSimTransform` only updates at
fixed-tick boundaries, so an edit lands in `transform` immediately and is what the *next*
tick interpolates from).

## Play / Pause / Step Mode (Roadmap M1.2)

An explicit `EditorMode { Editing, Playing, Paused }` on top of the fixed-timestep loop
above: Editing (the default at launch) freezes the accumulator so nothing simulates, Playing
is the fixed-timestep loop's normal behavior, Paused freezes mid-play without discarding
progress. Step credits the accumulator with exactly one `kFixedDt` and lets the same while
loop drain it: no separate single-step code path. A "Playback" panel (Play/Pause toggle,
Step, Stop, a Mode label) docks as a thin strip at the top of the existing dockspace.

**Play-mode isolation, the user's explicit choice:** Stop always restores `scene` from
`sceneBackup`, a snapshot taken when Play starts, discarding whatever happened during Play
(Unity/Godot/Unreal convention). Cheap because `Scene` is a plain copyable struct (a
`vector<Entity>` plus an `int`; mesh/model/audio/body handles are owned elsewhere): `scene =
sceneBackup` is the entire mechanism, no new serialization code. This is the safety net that
makes M1.3's scripts and M2's physics/audio non-destructive to test.

**Two bugs caught by tracing the interpolation math before writing any code:** `alpha` only
means something while the accumulator is actively draining during Playing, so it's pinned to
`1.0` whenever `mode != Playing` (else a gizmo edit made while paused would blend against a
stale pre-edit tick). And a Stop-restore or a Step can teleport a pose in one rendered frame,
which TAA/SSAO/SSR would read as large spurious motion, the same problem category as the
temporal-ghosting work below: fixed by folding a one-frame `suppressNextFrameHistory` flag
(later folded into `suppressTemporalHistory`, see below) into both Stop and Step.

**Declined, on purpose:** a dedicated "play" input-context (the context stack already
supports shadowing bindings this way, but there's no gameplay-specific input to put there
until M1.3); locking scene-structural editing while Playing/Paused (snapshot/restore already
makes that safe to discard).

Verified: clean build, then a fresh-launch screenshot showing `Mode: EDITING` and the Cube at
exactly 0/0/0 degrees (versus the fixed-timestep screenshot's long-running Cube at ~1268/2536
degrees): direct confirmation nothing simulates until Play is pressed. Actually clicking
Play/Pause/Step/Stop and watching the transitions isn't independently confirmed interactively
(no live input desktop here); established by code reasoning instead.

## Cascaded Shadow Maps (Roadmap M3)

Directional shadows from the scene light onto every cel-shaded surface, via Diligent's own
`ShadowMapManager` (`external/DiligentFX/Components`): cascade distribution, the shadow-map
atlas, and cascade selection/PCF sampling (`Shaders/Common/public/Shadows.fxh`) are all
Diligent's, not hand-rolled, per the guiding principle. 4 cascades, 2048² D32_FLOAT, PCF (not
VSM/EVSM: cheap, no extra blur pass, matches the toon aesthetic, the user's explicit choice).
Shadow darkens the *existing* band ramp rather than painting a separate flat color:
`CelShade` gained a `shadow` factor multiplied into `NdotL` before quantization, so a
shadowed pixel just lands on a darker rung of the same ladder N·L already uses.

**Abstraction-layer additions** (`renderer.h`): `BeginShadowPass()` (returns the cascade
count to loop over, 0 when `PostParams::shadows` is off), `BeginShadowCascade(i)`,
`DrawMeshShadow`/`DrawModelShadow` (position-only, no material or motion history),
`EndShadowPass()` (a no-op today; the hook a future VSM/EVSM `ConvertToFilterable` would land
in). `main.cpp` runs these in a pre-pass, once per cascade, before `BeginFrame` (separate
depth-only targets, no interaction with the main G-buffer), which required moving
`SetPostParams`/`SetCamera`/`SetLight` earlier in the frame.

**`iNumCascades = 0` is the "shadows off" sentinel**, not a new shader branch: `Shadows.fxh`'s
own `FindCascade` treats it as "no cascade found," and `FilterShadowMap` short-circuits to
`fLightAmount = 1.0` without ever touching the shadow map texture.

**Two real, non-obvious bugs, worth remembering if any future `Components`-module Diligent
header (ShadowMapManager, BoundBoxRenderer, EnvMapRenderer, VectorFieldRenderer) gets added
here again:**
1. **A cross-module `BasicStructures.fxh` namespace collision, only caught at link time.**
   This file's own `namespace Diligent { namespace HLSL { #include ".../BasicStructures.fxh"
   ... } }` wrapper (needed because `PostFXContext.hpp`/`Bloom.hpp`/etc. forward-declare
   `Diligent::HLSL::CameraAttribs`) is not how `ShadowMapManager.hpp` includes the same file:
   it does a bare, unnested `namespace Diligent { #include ".../BasicStructures.fxh" ... }`.
   The header's normal include guard means whichever inclusion is textually first in this TU
   wins; the other becomes a silent no-op. And since `ShadowMapManager.cpp` is a *separate*
   translation unit (built into `DiligentFX.lib`) with its own independent copy of the same
   collision, it always resolves to the bare `Diligent::ShadowMapAttribs` type regardless of
   what this TU's own headers decide, so a `using`-directive bridge fixes lookup but not type
   identity: calling into `ShadowMapManager` with the `HLSL`-wrapped type is a genuinely
   different (if identically-named) C++ type, and fails at *link* time with an undefined
   symbol, not a compile error. **Fix:** `#undef` the include guard macro
   (`_BASIC_STRUCTURES_FXH_`) between the two inclusion points and force a second, genuinely
   independent expansion at bare `Diligent::` scope, so this TU ends up with both
   `Diligent::HLSL::CameraAttribs` (for the PostFXContext family) and
   `Diligent::ShadowMapAttribs` (for ShadowMapManager) as distinct, correctly-typed structs,
   and use the bare name whenever calling into `ShadowMapManager`. General lesson: a namespace
   mismatch between a header and its own separately-compiled `.cpp` is invisible until link
   time, and a `using`-directive only fixes lookup, never type identity.
2. **Combined-sampler mode binds a texture's sampler on the view, not as an SRB variable**,
   confirmed by a live Vulkan validation error, not by guessing from the header. The
   "set-if-present" `GetStaticVariableByName(..., "g_ShadowMap_sampler")` pattern (the same one
   used for every other combined-sampler texture here) compiles, links, and runs, silently
   binding nothing: the call is a graceful no-op when the name isn't a separately-reflected
   resource. The real signal only shows up in a redirected console log: `Failed to bind
   sampler to sampler variable 'g_ShadowMap_sampler' ...: no sampler is set in texture view`,
   followed by a Vulkan validation error every draw. **Fix:** `ITextureView::SetSampler()` on
   the shadow map's SRV itself, once, right after both the sampler and `ShadowMapManager`
   exist. General lesson: a PSO/SRB variable lookup returning null doesn't mean a resource
   doesn't need binding; it can mean you're binding it through the wrong mechanism for that
   resource's kind (combined-sampler textures bind through the view, not the SRB).

**Not done / deliberately deferred:** per-cascade frustum culling of the shadow-casting draw
loop (draws every entity into every cascade unconditionally); no cascade-boundary debug
visualization (`GetCascadeColor` exists in `Shadows.fxh` if ever needed); the shadow bias/
`fFilterWorldSize` is a single hand-picked `0.02`, untested against grazing-angle acne or
peter-panning beyond what front-face culling already mitigates structurally.

Verified: clean build with zero warnings/errors on the touched files; a redirected-stdout
relaunch caught the sampler bug directly (thousands of repeated per-draw validation errors
before the fix, a clean log after); two screenshots (before/after the sampler fix) both show a
correctly-shaped soft-edged shadow, shifted between captures consistent with `Spin` having
continued to animate the objects, confirming the shadow recomputes live each frame rather than
being cached.

## Grid and Sky Gradient Backdrop (Roadmap #12)

Two independent passes, both gated by new Settings > Environment toggles (`showGrid`/
`showSky`), added because the demo scene had no ground/horizon reference and every other
visual feature is easier to judge by eye with one.

**Sky** (`Renderer::DrawSky`) is a small custom fullscreen pass (`sky_gradient.hlsl`): a
two-color vertical gradient lerped by the *world-space* view ray's Y direction, not screen Y,
so the horizon stays level as the camera pitches. It draws into the offscreen HDR G-buffer
`BeginFrame` already bound, before any entity `DrawMesh`/`DrawModel` call, so opaque geometry
draws over it; depth writing is off (`DepthEnable = False`), since the sky always fully covers
the frame and leaving depth alone preserves `BeginFrame`'s far-plane clear for every pixel
opaque geometry doesn't later cover. `sky_gradient.hlsl`'s `SkyConstants` cbuffer
(`invViewProj` + top/bottom colors) mirrors a C++ `SkyConstants` struct, the same pattern
every other shader constant buffer in this file uses.

**Grid** (`Renderer::DrawGrid`) is DiligentFX's own `CoordinateGridRenderer`
(`external/DiligentFX/Components`, the same library `ShadowMapManager` lives in), not a
hand-port of `ToonEngineOld`'s `grid.frag`, per the build-on-Diligent guiding principle:
per-pixel ray/plane intersection against the XZ (Y=0) plane, antialiased multi-LOD lines,
colored X/Z axes. Unlike `DrawWireframe`, it occludes itself by *reading* the finished scene
depth buffer rather than writing its own, so it must run after `EndScene()` resolves that
depth, the same call-timing contract as `DrawWireframe`, not during the main pass where
`sceneDepth` is still bound as a write target.

**Gotchas:**
- `CoordinateGridRenderer::Render` binds only the color target it's given and unbinds every
  render target again before returning, unlike every other pass in this file, which leaves
  its bound targets in place for the next call. `DrawGrid` rebinds the back buffer itself
  afterward so `DrawWireframe` and the ImGui overlay right after it still have a target,
  preserving `EndScene`'s "leaves the back buffer bound" contract.
- `RunPostFX` (inside `EndScene`) only fills `postCamera` when at least one post effect is
  enabled, but the grid needs it regardless of post-effect state, so `DrawGrid` calls
  `FillCameraAttribs` itself rather than assuming `EndScene` already did.
- `CoordinateGridRenderer`'s own `.cpp` includes `BasicStructures.fxh` the same nested
  `namespace Diligent { namespace HLSL { ... } }` way the PostFX family does, unlike
  `ShadowMapManager`'s bare-namespace form above, so it doesn't need the `#undef` workaround
  the cascaded-shadow-maps namespace collision required.

## Shader Hot-Reload (Roadmap #10)

Diligent's `IRenderStateCache` (`DiligentCore/Graphics/GraphicsTools/interface/RenderStateCache.h`)
wraps `CreateShader`/`CreateGraphicsPipelineState`. Every shader/PSO in `renderer.cpp` (toon
fill/outline, model fill/outline, shadow, tonemap, wireframe) now routes through
`Renderer::Impl::stateCache` instead of the raw `IRenderDevice*`, in **every** build, so no call
site branches on whether hot-reload is actually enabled. `stateCache` is created via
`CreateRenderStateCache` with `FileHashMode = RENDER_STATE_CACHE_FILE_HASH_MODE_BY_CONTENT`,
needing `LoadAndGetArchiverFactory()` (`Diligent-ArchiverInterface`, already linked). Only
`EnableHotReload` and the file watcher below differ between Debug and Release.

**Debug-only pieces, gated on `TOON_SHADER_HOT_RELOAD` (set in `CMakeLists.txt`):**
- **efsw** (new submodule, pinned to tag **1.5.0**, not master: master needs CMake ≥3.27, this
  environment has 3.25.1), a small cross-platform file-system watcher, watches
  `TOON_SHADERS_DIR` non-recursively (every `.hlsl` lives flat in one directory).
  `ShaderReloadListener::handleFileAction` runs on efsw's own watch thread, so it does the
  minimum: flip `Impl::shadersDirty` (`std::atomic<bool>`, relaxed ordering, a flag rather than
  a value anything depends on seeing immediately). `BeginFrame` is the one reader, once per
  frame, calling `stateCache->Reload()` when set.
- **React to `Modified`, `Add`, *and* `Moved`, not `Modified` alone.** Many editors (confirmed
  directly against this repo's own tooling) save atomically: a temp file, then a rename over
  the original, which the OS reports as the old name disappearing and the new content arriving
  under the original name again, not a plain `Modified` event. `Modified`-only would have
  silently never fired on a real save. `Delete` is deliberately excluded (nothing to reload
  from a file that's mid-rename and briefly gone).
- **`efsw::FileWatchListener::handleFileAction`'s `oldFilename` parameter is by-value
  (`std::string`) in efsw 1.5.0**, not `const&` like current efsw master. Match the pinned
  version's signature exactly: a mismatched parameter type silently turns the override into a
  non-overriding hide of the pure-virtual base instead of a compile error (caught here by the
  `-Woverride` diagnostic, not by inspection).
- A **"Reload Now" button** in the Settings panel (also `TOON_SHADER_HOT_RELOAD`-gated) calls
  the same `Renderer::ReloadShaders()` the watcher calls internally: a manual fallback
  alongside the automatic per-frame path. `EditorState::shaderReloadStatus` echoes its last
  result count the same way `sceneStatus` already does for scene save/load.

**Release builds** link neither efsw nor define `TOON_SHADER_HOT_RELOAD`: `ReloadShaders()`
becomes a real no-op (`return 0`), matching Diligent's own guidance to keep hot-reload out of
production builds. `stateCache` itself still exists in Release (every shader still routes
through it), just without `EnableHotReload` or a watcher driving it.

Verified live: edited `toon_fill.hlsl` while the Debug build was running (no restart) and
confirmed both the stderr reload log and a visible on-screen shading change.

## Temporal Ghosting Fixes (Post-Ship Hardening)

A visible ghost/trail followed spinning and camera-moved objects for a stretch after the
gizmo/temporal-post-effects work first shipped. It took seven rounds of debugging across
multiple sessions to find every real cause (the full round-by-round misdiagnosis journey,
worth keeping for the methodology lesson, is in **[ARCHIVE.md](ARCHIVE.md)**); this section
is just the durable end state, current as of the last fix.

**Four independent causes, each needing its own fix** (fixing one alone never fully resolved
it, which is why this took multiple rounds):

1. **Rotating outlines under-reported their own motion.** The inverted-hull outline's
   `PrevClip` was built by extruding along *this frame's* normal and only varying
   `WorldViewProj` between curr/prev: exact for pure translation, but the extrude direction
   is itself rotation-dependent, so under continuous rotation this always slightly
   under-reports motion. Fixed by adding `g_PrevNormalMatrix` to the shared cbuffer (grew
   320→384 B): the inverse-transpose of the *previous* frame's world matrix, computed from
   the `prevWorld`/`objPrevWorld` matrices `DrawMesh`/`DrawModel` already receive. Both
   outline vertex shaders now redo the extrude for `PrevClip` using `g_PrevNormalMatrix`
   instead of reusing the current frame's `inflated` position.
2. **`PostFXContext` never had a real previous-frame depth buffer**, only the current frame
   reused as a stand-in (`pPrevDepthBufferSRV = depthSRV`). That defeats depth-based
   disocclusion entirely for every temporal effect (SSAO/TAA/SSR alike): the mechanism that's
   supposed to catch a moving silhouette edge and distrust stale history there. Fixed with a
   real `Impl::prevSceneDepth` texture, recreated alongside the other offscreen targets and
   `CopyTexture`'d from `sceneDepth` at the end of `EndScene`. **Gotcha:** it needs the exact
   same BindFlags as `sceneDepth` (`BIND_DEPTH_STENCIL | BIND_SHADER_RESOURCE`, even though
   it's never bound as a DSV) or Vulkan validation trips on the SRV's depth→R32_FLOAT
   reinterpretation (`VUID-VkImageViewCreateInfo-image-01762`/`-subresourceRange-09594`); the
   two textures need matching creation flags for Diligent's Vulkan backend to set that up the
   same way for both.
3. **Camera motion was never reprojected**, only object motion. `RunPostFX` fed
   `PostFXContext` the *same* camera-attribs instance as both curr and prev (no
   `prevPostCamera` existed at all), so `ComputeReprojectedDepth.fx`'s curr→prev round-trip
   was a no-op regardless of whether the camera actually moved. During genuine camera motion
   (zoom/orbit/pan/fly) a static surface's camera-space depth legitimately changes
   frame-to-frame, and the disocclusion test compared against the wrong baseline, blending in
   stale AO/color across a frame where the framing genuinely changed. Fixed by adding
   `Impl::prevPostCamera` (seeded to `postCamera` on frame 1, snapshotted right after
   `postFX->Execute()` each frame), the same double-buffering pattern already used for
   `prevSceneDepth`/`prevViewProj`, just never extended to the camera-attribs struct. Matches
   `DiligentSamples/Tutorial27_PostProcessing`'s own reference pattern (a real double-buffered
   `CameraAttribs[2]`, never aliased).
4. **A rotating silhouette is a genuinely view-dependent contour**, which no per-vertex motion
   vector, however correctly computed, can fully represent: there's always a small residual
   error. DiligentFX's SSAO shader has a motion-magnitude-based safety net for exactly this
   (`MotionFactor`, scaling down history trust when motion is large), but its tuning constants
   are compiled-in `#define`s in the vendored shader source, unreachable from the app without
   patching a submodule (off-limits per the style guide), and at Spin's default rate the
   residual error is small enough to slip under that threshold, so history is trusted and a
   small per-frame error compounds into a visible, persistent ghost. Since the shader-internal
   thresholds are unreachable, `PostParams::suppressTemporalHistory` (renamed from
   `activeInteraction`/`gizmoManipulating` through the debugging rounds) is the sanctioned
   lever: `gizmoActive || ImGui::IsAnyItemActive() || spin`. While Spin is on (the default),
   SSAO/TAA never accumulate at all: every frame is a fresh, non-temporal computation
   (slightly noisier AO, no denoise), sidestepping the question entirely. The instant Spin and
   any interaction stop, normal accumulation resumes on an already-static scene and converges
   within a few frames. SSR has no `ResetAccumulation` field at all (unlike SSAO/TAA); its own
   `TemporalRadianceStabilityFactor` was tuned down from the library default `1.0` to `0.7`
   defensively (higher values are the most ghosting-prone end of its documented range).

`ImGui::IsAnyItemActive()` (not just `ImGuizmo::IsUsing()`) was the key correction partway
through: a plain Inspector-slider drag (no gizmo involved at all) reproduced the same trail,
which proved the trigger couldn't be gizmo-specific.

## Verifying a Vulkan Build

### Link Fails: `permission denied` Writing `ToonEngine.exe`

If a previous instance is still running, `lld-link` can't overwrite the exe. Kill
it first. If it's a **stuck / elevated** instance that won't die (`Stop-Process`,
`taskkill`, and CIM `Terminate` all return Access Denied), **rename the running
exe aside**: Windows allows renaming a running executable (it's a metadata op),
then re-link creates a fresh one. The renamed file can't be deleted until that
process finally exits.

**Same failure, a DLL instead of the exe.** A running instance also locks the engine DLLs it
loaded: `lld-link: error: failed to write output 'GraphicsEngineVk_64d.dll': permission
denied` fails identically and for the same reason. `Get-Process | Where-Object { $_.Path
-like '*ToonEngine*' }` finds the culprit. **Check whether it's actually yours before
killing it**: a window you didn't just launch this session may be the user's own live
session (see the `verify` skill's "don't assume a process you didn't just launch is yours");
ask first unless durably authorized to close it.

### Screenshotting the Window (GDI `CopyFromScreen` Returns Black)

A Vulkan swap-chain doesn't show up in GDI screen-copy: `Graphics.CopyFromScreen`
captures the client area as pure black (the DWM-drawn title bar still shows).
**`PrintWindow(hwnd, hdc, PW_RENDERFULLCONTENT=2)` does capture the rendered
content.** That's how the toon sphere was verified.

**High-DPI crop (150% display).** `PrintWindow` captures at the window's *physical*
framebuffer resolution, and ImGui's viewport / dock layout is sized in those physical
pixels. On a 150%-scaled monitor the framebuffer is 1.5× the logical client (e.g. **3840×2054
vs the 2560×1369 `GetClientRect` reports**), so **right-docked panels sit beyond the logical
width and get cropped out of the shot** unless the *capturing* process is DPI-aware. Fix in
the capture script: `SetProcessDpiAwarenessContext(PER_MONITOR_AWARE_V2 = -4)` at the top, so
`GetClientRect` returns the true 3840-wide size and the bitmap grabs the whole framebuffer
(then downscale for viewing). Relatedly, the **app starts maximized** (`GLFW_MAXIMIZED` hint +
a modest restored size) rather than hardcoding a 3840×2160 window: an oversize window on a
smaller screen pushed the dock layout's right column off-screen even in the real app.
`PrintWindow` also **intermittently returns an all-white frame** (a race with the swap-chain
present), the render is fine; just re-run the capture.

## ToonEngineOld: Carry-Over Reference (Deleted 2026-07-21)

`ToonEngineOld/` (was untracked and **gitignored**, `src` + `assets` only) was the old
from-scratch **OpenGL 4.1** engine, kept as a porting reference for three specific systems:
grid/sky, skeletal animation, and sprites. Its abstraction layer was low-level
(`BindShader`/`SetUniform`/immediate binds/framebuffers) and never mapped onto Diligent's
PSO/SRB model, so only "above the abstraction layer" material ever had lasting value; that
value is fully captured in this file's own sections (see "Grid and Sky Gradient Backdrop,"
"Skeletal Animation," "2D Sprites," and every other roadmap item's write-up above) and in
**[ARCHIVE.md](ARCHIVE.md)**'s full original carry-over survey (per-system porting notes,
plus the 2026-07-11 audit of which items Diligent already has vs. genuinely needs
hand-rolling). All three tracked ports shipped (grid/sky #12, skeletal animation #11,
sprites #13, all 2026-07-21), meeting this section's own deletion trigger, so the folder
(and its `ToonEngine-backup/ToonEngineOld` backing copy) was deleted the same day. Nothing
here needs it anymore; treat this section as historical record only.

## Architecture Decisions

### Renderer Abstraction Layer: Data Encapsulation, Not a Virtual `IRenderer`

`core/renderer.h` exposes opaque id-based handles (`enum class : uint32_t`,
`0 = Invalid`) plus a data-encapsulated `Renderer` class, not an abstract interface with
virtual methods. Reasoning: Diligent already provides runtime backend
selection (Vk/D3D12/GL/Metal) *beneath* the abstraction layer, so a second layer of runtime
polymorphism in ToonEngine buys nothing. A backend swap or console port is a
build-time concern, swap in a different `renderer_*.cpp`, with zero virtual
overhead. `src/core/renderer.cpp` is the *only* translation unit allowed to
include a Diligent header or name a `Diligent::` type.

### Vulkan-Only, Not "Vulkan-First With Others Linked"

Originally D3D11/D3D12/OpenGL were linked alongside Vulkan "for debugging with
RenderDoc/PIX." Given the compile-time cost (see above) and that nothing
currently uses them, they're disabled by default. Re-enable a specific one
only when there's a concrete reason (e.g. actually reaching for RenderDoc).

### Data-Oriented Discipline: Plain Structs + Free Functions, Switch Over Virtual Dispatch

Extends the reasoning above from the renderer/physics seams specifically to a general
default, citing Casey Muratori's "clean code, horrible performance" critique: new
state/logic defaults to a plain struct + free functions, not a class with private
members, unless the class quarantines a genuine external dependency (as above) or cuts
real repeated boilerplate. Same logic extends to dispatch: prefer switch/table over
virtual for a small, fixed, compile-time-known case set, especially
per-frame/per-object/per-vertex: reserve virtual for a genuine open-ended extension
point (`Script`, since gameplay behaviors are added outside the engine). Written up in
full, with in-repo examples (`ColliderShape`'s switches, `Script`'s justified virtuals),
in docs/cpp-style-guide.md §7; enforced by the `tidy-cpp` skill's architecture-audit
pass.


## History

Chronological ship log, kept short: every entry below has a full technical write-up in its
own section above. The complete, unedited version of this entire log (every entry, full
length) is preserved in **[ARCHIVE.md](ARCHIVE.md)**'s "Full chronological history" section,
along with the full round-by-round temporal-ghosting debugging saga (2026-07-11 through
2026-07-12, distilled here into "Temporal ghosting fixes" above) and the original
ToonEngineOld carry-over survey (see "ToonEngineOld: carry-over reference" above for what's
still active from it). None of ARCHIVE.md needs to be read for day-to-day work; it's there
purely for when someone explicitly asks for the full history behind something.

- **2026-07-06**: pivoted from a from-scratch OpenGL 4.1 engine to Diligent Engine + Vulkan;
  first light (window, Vulkan device, swap chain, clear loop).
- **2026-07-08**: renderer abstraction layer (`main.cpp` Diligent-free); DiligentTools +
  Dear ImGui behind it; D3D11/D3D12/OpenGL disabled. Toon pipeline first light (banded cel
  fill + inverted-hull outline, spinning UV sphere). See "Toon pipeline".
- **2026-07-09**: multi-object scene + per-object `Material`; dual-normal outline for hard
  edges; ImGui docking (the original manual submodule checkout, later superseded, see
  "Docking"); DiligentFX added + the HDR/ACES tone-map pipeline stood up. See "Toon
  pipeline", "DiligentFX / HDR post-processing".
- **2026-07-10**: all six DiligentFX post effects shipped in one day (Bloom, SSAO, motion
  vectors, depth of field, TAA, SSR); non-uniform scale (inverse-transpose normal matrix +
  world-space outline width) and per-object outline tuning; CLion migration
  (`scripts/vsenv.ps1` removed, VS env import inlined instead); roadmap redesign around the
  ToonEngineOld carry-over; glTF model loading + model outline; scene graph; editor camera +
  input. See each feature's own section above.
- **2026-07-11**: gizmo snap + hotkeys; the temporal-ghosting debugging saga begins (rounds
  1-6 this day; see "Temporal ghosting fixes" for the resolved state, ARCHIVE.md for the
  full journey); `scripts/vsenv.ps1` mistakenly recreated, then corrected for good (see
  "Build gotchas"); light entity component; ToonEngineOld TODO-list audit folded 4 items
  (input system, asset browser, fixed timestep, shader hot-reload) into the roadmap.
- **2026-07-12**: scene serialization; window icon taskbar fix; the durable `external/imgui`
  submodule fix for docking (see "Docking"); input system (action maps + JSON rebinding);
  ghosting round 7 (camera-motion reprojection, the last of the four causes, see "Temporal
  ghosting fixes").
- **2026-07-13**: asset browser panel (closes the last engine/editor carry-over item);
  ToonEngineOld triage + roadmap reframed around milestones (M1-M4) + `docs/architecture.md`
  added; cascaded shadow maps; fixed-timestep sim loop (M1.1); Play/Pause/Step mode (M1.2);
  entity behavior system (M1.3, closing out M1).
- **2026-07-16**: physics + collision (M2.1); `src/` reorganized into subsystem folders
  (`core/rendering/`, `core/scene/`, `core/physics/`, `core/camera/`), `main.cpp`'s ~1600
  lines extracted into `app/` + `ui/panels/` (down to ~90 lines of init/loop glue).
- **2026-07-20**: audio (M2.2); mouse-pick via raycast (M2.3); contact events to scripts (#9);
  shader hot-reload (#10); roadmap-skill reorg (shipped-item promotion moved from `tidy-md` to
  `update-roadmap`) plus two Steam-release-gap roadmap items (Steamworks bootstrap,
  controller-navigable UI).
- **2026-07-21**: skeletal animation (#11, see "Skeletal animation"); grid + sky gradient
  backdrop (#12, see "Grid and sky gradient backdrop"); 2D sprites (#13, see "2D sprites");
  roadmap rebalanced into 0.1-increment milestones through a 1.0 "Official Release" boundary,
  Lua scripting + 2D editor mode added, the "Researched, Not Yet Ranked" holding bucket
  removed in favor of ranking everything directly; `ToonEngineOld/` deleted, all three
  tracked ports having shipped (see "ToonEngineOld: Carry-Over Reference").