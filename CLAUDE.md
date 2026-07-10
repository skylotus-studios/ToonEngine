# ToonEngine

From-scratch, cross-platform game engine focused on stylized / toon rendering,
built on **Diligent Engine** (rendering) + **GLFW** (windowing) + **Dear ImGui**
(debug/editor UI, via DiligentTools). **Vulkan-only** — D3D11/D3D12/OpenGL are
disabled in `CMakeLists.txt` to keep build times down; re-enable one only if
you have a concrete reason (e.g. D3D12 for RenderDoc/PIX).

> **History:** ToonEngine began as a from-scratch OpenGL 4.1 renderer (see the
> `main` branch). It pivoted to Diligent Engine on the `diligent` branch, which
> is a fresh start at "first light" — mine `main`'s history for reference only.

For the detailed story behind any decision below — why a gotcha exists, what
was tried, exact error text — see **[MEMORY.md](MEMORY.md)**.

## Status: toon pipeline first light ✅

The app opens a window, creates a Vulkan device + swap chain, and every frame
draws a small spinning scene — a smooth **sphere**, a faceted **cube**, and a
**torus** — each cel-shaded with a **banded (ramp) diffuse fill + an
inverted-hull silhouette outline**, in its own color. A Dear ImGui debug window
drives the look live (light direction, band count, per-object colors, outline
width, camera). HLSL shaders are cross-compiled to SPIR-V by Diligent at runtime.

Rendering sits behind the **renderer seam**: `src/main.cpp` builds the scene and
drives a `toon::Renderer` (`CreateMesh` / `SetCamera` / `SetLight` /
`DrawMesh(mesh, transform, material)`) and includes no Diligent header — all
Diligent/Vulkan code lives in `src/core/renderer.cpp` (see *Conventions*).
`main.cpp` does include `<imgui.h>` directly — Dear ImGui isn't a Diligent type,
so it's exempt.

## Build

Toolchain: **CMake ≥ 3.20 · Ninja · clang-cl (LLVM) · Visual Studio 2022** (for
the Windows SDK + MSVC CRT/import libs clang-cl targets). C++17/C.

Dependencies are **git submodules** via `add_subdirectory` — no vcpkg. Clone
with `--recursive` (DiligentTools has its own nested submodules):

```
git submodule update --init --recursive
```

**Windows builds need a VS Developer environment** (Windows SDK tools on PATH)
or configure fails at `CMAKE_MT-NOTFOUND`:

- **VS Code (recommended):** `Ctrl+Shift+B` to build, `F5` to build+debug —
  the tasks bootstrap the environment automatically via `scripts/vsenv.ps1`.
- **Command line:** open the **"VS Dev PowerShell"** terminal profile, or
  dot-source it yourself (`. .\scripts\vsenv.ps1`), then:

```
cmake --preset windows-debug
cmake --build --preset windows-debug
./build/windows-debug/ToonEngine.exe
```

Presets: `windows-debug` / `windows-release`. Each builds into `build/<preset>/`
with engine DLLs copied next to the exe (`copy_required_dlls`).

## Source layout

```
src/
  main.cpp                Entry point: GLFW window + game loop; builds the scene + drives Renderer (no Diligent headers)
  core/
    renderer.h            The seam: opaque handles, scene types (Vertex/Camera/ToonParams/Transform) + PIMPL Renderer
    renderer.cpp          Diligent Engine (Vulkan): toon PSOs/shaders/mesh buffers + ImGui-Diligent glue — ALL Diligent code here
    math.h                Minimal Diligent-free vector types (Vec2/3/4) for the seam's public API
    primitives.{h,cpp}    Procedural CPU mesh generators (sphere/cube/torus) -> toon::MeshData
assets/
  shaders/                HLSL: toon_common.hlsli + toon_fill/toon_outline + tonemap.hlsl (HLSL->SPIR-V at runtime)
external/                 Git submodules (not committed as files; see .gitmodules)
  DiligentCore/           Rendering: RHI + Vulkan backend (D3D11/D3D12/GL disabled), HLSL->SPIR-V
  DiligentTools/          Dear ImGui renderer backend (Diligent-Imgui), texture/glTF loaders
  DiligentFX/             Post-processing effects (bloom, SSAO, tone-mapping shaders)
  glfw/                   Cross-platform window + input
CMakeLists.txt            add_subdirectory(DiligentCore, DiligentTools, glfw); disables unused backends
CMakePresets.json         windows-debug / windows-release (Ninja + clang-cl)
scripts/vsenv.ps1         Imports the VS Developer env (vswhere + VsDevCmd.bat); tasks dot-source it
.vscode/                  tasks/launch/settings/c_cpp_properties — self-bootstrapping build+debug
```

## Rendering: the seam + how Diligent is wired

All Diligent/ImGui-backend code lives in `core/renderer.cpp`; `main.cpp` only
calls `Renderer::Init / BeginFrame / EndFrame / Resize / InitUI / BeginUI / EndUI`.

1. **GLFW** creates the window with `GLFW_NO_API` (Vulkan owns the surface).
2. `MakeNativeWindow()` fills Diligent's `NativeWindow` per platform: Win32
   `hWnd` · Linux `WindowId`+`pDisplay` · macOS `pNSView` (needs a Cocoa `.mm`
   helper from GLFWDemo — not yet written).
3. `EngineFactoryVk` creates the device/context, then the swap chain.
4. Per frame: render the scene (toon passes) into an **HDR offscreen target**,
   resolve it to the back buffer with a tone-map pass (`EndScene`), draw the
   ImGui overlay on top, `Present()`.
5. **Dear ImGui**: `InitUI` constructs `ImGuiImplDiligent` then the GLFW
   platform backend (order matters — see MEMORY.md). Per frame: `BeginUI()` →
   engine/game code calls `ImGui::` directly → `EndUI()`.

**Toon draw (`DrawMesh`)** runs two passes over the same mesh, sharing one
dynamic constant buffer: the **outline** pass first (inverted hull — extrude
along the normal, cull front faces) then the **fill** pass (banded diffuse, cull
back). The fill's nearer depth overwrites the enlarged shell everywhere but the
silhouette rim. Shaders are authored in **HLSL** (`assets/shaders/`) and
cross-compiled to SPIR-V by Diligent at runtime, loaded via a shader-source
stream factory rooted at `TOON_SHADERS_DIR` (baked in by CMake). See MEMORY.md
for the matrix-convention, winding, and outline-ordering details.

## Conventions

- **Diligent stays behind the renderer seam** — the load-bearing rule.
  `core/renderer.h` exposes only opaque handles (`TextureHandle`/`BufferHandle`/
  `ShaderHandle`/`PipelineHandle`) and a PIMPL `Renderer`; **all** Diligent
  headers/types are confined to `core/renderer.cpp`. A future backend swap (or
  console port) is then a new `renderer_*.cpp`, not a rewrite.
- **Dear ImGui is exempt from the seam** — it's a plain UI library. Engine/game
  code may `#include "imgui.h"` and call `ImGui::` directly; only its Diligent
  render backend stays in `core/renderer.cpp`.
- **HLSL** for all shaders. Diligent objects use COM-style refcounting —
  hold them in `RefCntAutoPtr<>`, namespace `Diligent`.
- Target-based CMake only (`target_*`).
- **Disable Diligent backends you're not using** (`DILIGENT_NO_*`, set before
  `add_subdirectory(DiligentCore)`) — it builds every supported backend by
  default, which dominates compile time.
- Add **DiligentFX** (PBR, post-processing) as a submodule when needed — the
  hook is already stubbed (commented) in `CMakeLists.txt`.

## Platform support

| Platform | Status  | Backend    | Notes |
|----------|---------|------------|-------|
| Windows  | active  | Vulkan     | primary dev target; D3D11/D3D12/OpenGL disabled |
| Linux    | planned | Vulkan     | X11 handles wired; Wayland fields exist |
| macOS    | planned | Vulkan via **MoltenVK** | needs `NSView` from GLFW Cocoa (`.mm` helper) |

## Roadmap / next steps

1. **First light — compile & run.** ✅ Done.
2. **Renderer interface seam.** ✅ Done — `core/renderer.{h,cpp}`.
3. **DiligentTools.** ✅ Done — Dear ImGui wired in. Texture/glTF loaders
   (`AssetLoader`, `TextureLoader`) already built; wire in when pulling assets.
4. **First real shaders — the toon pipeline.** ✅ Done (HLSL → SPIR-V):
   - **Fill pass:** ✅ banded/ramp diffuse instead of smooth N·L (`toon_fill.hlsl`).
   - **Outline pass:** ✅ inverted-hull (`toon_outline.hlsl`). A post-process
     depth+normal edge-detect variant is still open if the look wants it.
   - Wired through the seam (`CreateMesh`/`SetCamera`/`SetLight`/`DrawMesh`)
     with sphere/cube/torus primitives and live ImGui controls.
   - **Refinements:** ✅ cube + torus primitives (left-handed winding); ✅ a
     multi-object scene with per-object `Material`; ✅ smooth-normal outline hull
     so hard edges (a cube's corners) stay closed. Still open: non-uniform-scale
     normals (inverse-transpose), instancing, per-object outline tuning.
5. **imgui docking.** ✅ Done — see-through dock space, debug panel docked left
   by default; enabled from `main.cpp`, guarded on `IMGUI_HAS_DOCK`. ⚠️ Needs
   imgui's `docking` branch: `ThirdParty/imgui` is locally checked out to
   **upstream ocornut/imgui** docking (the DiligentGraphics fork's docking branch
   is too old to build). `git submodule update` reverts it (docking then silently
   disables). See MEMORY.md → "Docking".
6. **DiligentFX.** ⏳ In progress. Added as a submodule (API256018) + wired into
   the build. HDR pipeline established: the scene renders to an offscreen RGBA16F
   target, resolved to the back buffer by a full-screen **tone-map** pass (ACES +
   exposure — `assets/shaders/tonemap.hlsl`, `Renderer::EndScene`). This is the
   foundation DiligentFX's **Bloom / SSAO** components plug into — those are next.
7. **Cross-platform.** Linux (Vulkan), then macOS (MoltenVK — Cocoa helper).
8. **Other backends.** D3D11 disabled by default; re-enable to support older Windows devices.

## Constraints

- **C++17**, **clang everywhere** (clang-cl on Windows, Apple Clang on macOS).
- **Windows builds require a VS Developer environment.**
- Dependencies are **submodules**, not vcpkg.
- Keep Diligent contained behind the renderer seam — no Diligent headers
  outside `core/renderer.cpp`.
