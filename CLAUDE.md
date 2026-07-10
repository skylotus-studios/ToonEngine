# ToonEngine

From-scratch, cross-platform game engine focused on stylized / toon rendering,
built on **Diligent Engine** (Vulkan-only) + **GLFW** (windowing) + **Dear ImGui**
(debug/editor UI, via DiligentTools). D3D11/D3D12/OpenGL are disabled in
`CMakeLists.txt` to keep build times down — re-enable one only for a concrete reason
(e.g. D3D12 for RenderDoc/PIX).

The detailed story behind every decision, gotcha, and error message below lives in
**[MEMORY.md](MEMORY.md)** — pull it up when you hit a build error or want the "why"
behind a rule here.

> **History:** started as a from-scratch OpenGL 4.1 renderer (`main` branch), then
> pivoted to Diligent + Vulkan on the `diligent` branch. Mine `main` for reference only.

## Current state

The app opens a window, creates a Vulkan device + swap chain, and each frame draws a
small spinning scene — a smooth **sphere**, a faceted **cube**, a **torus**, on a
**ground plane** — each cel-shaded with a **banded diffuse fill + inverted-hull
silhouette outline** in its own color. The scene renders into an **HDR offscreen
target + world-space normal + motion-vector G-buffers** (MRT); a **DiligentFX post
chain** (via `PostFXContext`) applies **SSAO** (temporal-denoised contact shadows),
optional **TAA**, **depth of field**, and **screen-space reflections**, and **Bloom**,
then an **ACES tone-map pass** resolves to the back buffer, with a docked **Dear ImGui**
debug overlay driving the look live (light, band count, colors, outline width, camera,
and every post effect). HLSL shaders cross-compile to SPIR-V at runtime.

## Build

Toolchain: **CMake ≥ 3.20 · Ninja · clang-cl (LLVM) · Visual Studio 2022** (for the
Windows SDK + MSVC CRT/import libs clang-cl targets). C++17/C. Dependencies are **git
submodules** (no vcpkg) — clone recursively (DiligentTools has nested submodules):

```
git submodule update --init --recursive
```

**The IDE is CLion.** One-time toolchain + preset setup is in
**[docs/clion-setup-windows.md](docs/clion-setup-windows.md)** (Linux/macOS setup docs
also exist under `docs/` for when those platforms land — see Platform support below).
In short: a CLion **Visual Studio toolchain** supplies the VS Developer environment
automatically (the Windows SDK tools clang-cl needs — otherwise configure fails at
`CMAKE_MT-NOTFOUND`), and CLion reads `CMakePresets.json` for the `windows-debug` /
`windows-release` profiles.

**Command line / CI** needs that environment too — dot-source `scripts/vsenv.ps1`
(portable via vswhere + VsDevCmd.bat), then use the presets:

```
. .\scripts\vsenv.ps1
cmake --preset windows-debug
cmake --build --preset windows-debug
./build/windows-debug/ToonEngine.exe
```

Presets build into `build/<preset>/` with the engine DLLs copied next to the exe.

## Source layout

```
src/
  main.cpp                Entry point: GLFW window + game loop; builds the scene, drives Renderer (no Diligent headers)
  core/
    renderer.h            The seam: opaque handles + scene types (Vertex/Camera/ToonParams/Transform) + PIMPL Renderer
    renderer.cpp          Diligent Engine (Vulkan): toon PSOs/shaders/mesh buffers + ImGui-Diligent glue — ALL Diligent code here
    math.h                Minimal Diligent-free vector types for the seam's public API
    primitives.{h,cpp}    Procedural CPU mesh generators (sphere/cube/torus/plane) -> toon::MeshData
assets/shaders/           HLSL: toon_common.hlsli + toon_fill/toon_outline + tonemap.hlsl (HLSL->SPIR-V at runtime)
external/                 Git submodules (see .gitmodules): DiligentCore/Tools/FX, glfw
CMakeLists.txt            add_subdirectory the submodules; disables unused Diligent backends
CMakePresets.json         windows-debug / windows-release (Ninja + clang-cl)
scripts/vsenv.ps1         Imports the VS Developer env for command-line builds
docs/clion-setup-windows.md  CLion toolchain + preset + debug setup (Windows, active)
docs/clion-setup-linux.md    CLion setup notes for Linux (planned)
docs/clion-setup-macos.md    CLion setup notes for macOS (planned)
docs/style-guide.md          C++ house style (formatting + comments + seam rules)
.claude/skills/tidy-cpp/     Skill: clean src/** to the style guide (/tidy-cpp)
```

## The renderer seam (load-bearing rule)

**Diligent stays behind the seam.** `core/renderer.h` exposes only opaque handles
(`TextureHandle`/`BufferHandle`/`ShaderHandle`/`PipelineHandle`) and a PIMPL
`Renderer`; **all** Diligent headers and `Diligent::` types live in
`core/renderer.cpp` — the only translation unit allowed to name one. `main.cpp` just
calls `Renderer::Init / BeginFrame / DrawMesh / EndScene / EndFrame / Resize / InitUI
/ BeginUI / EndUI`. A backend swap or console port then becomes a new
`renderer_*.cpp`, not a rewrite.

**Dear ImGui is exempt** — it's a plain UI library; engine/game code may `#include
"imgui.h"` and call `ImGui::` directly (as `main.cpp` does). Only its Diligent render
backend stays in `core/renderer.cpp`.

**Toon draw (`DrawMesh`)** runs two passes over one mesh sharing a dynamic constant
buffer: the **outline** pass (inverted hull — extrude along the normal, cull front)
then the **fill** pass (banded diffuse, cull back); the fill's nearer depth overwrites
the enlarged shell everywhere but the silhouette rim. See MEMORY.md for the
matrix-convention, winding, and outline-ordering details.

## Conventions

- **HLSL** for all shaders (cross-compiled to SPIR-V by Diligent at runtime).
- Diligent objects are COM-refcounted — hold them in `RefCntAutoPtr<>`, namespace
  `Diligent`.
- **Disable Diligent backends/modules you're not using** (`DILIGENT_NO_*`, set as
  `CACHE BOOL ... FORCE` before `add_subdirectory(DiligentCore)`) — it builds every
  supported backend by default, which dominates compile time. We also set
  `DILIGENT_NO_RADIENT` (DiligentFX's GI module — unused, and it fails to compile
  under clang-cl; a full `cmake --build` / CI hits it even though CLion doesn't).
- Target-based CMake only (`target_*`).
- C++17, clang everywhere (clang-cl on Windows, Apple Clang on macOS).
- Windows builds require the VS Developer environment (see Build).

## Platform support

| Platform | Status  | Backend                 | Notes |
|----------|---------|-------------------------|-------|
| Windows  | active  | Vulkan                  | primary dev target; D3D11/D3D12/OpenGL disabled |
| Linux    | planned | Vulkan                  | X11 handles wired; Wayland fields exist |
| macOS    | planned | Vulkan via **MoltenVK** | needs `NSView` from a GLFW Cocoa `.mm` helper |

## Roadmap

1. **DiligentFX post effects — done.** Bloom, SSAO, DoF, motion vectors, TAA, and SSR
   are all in via `PostFXContext` (see MEMORY.md), sharing the normal + motion
   G-buffers and real `CameraAttribs`. Further effects would reuse the same plumbing,
   but the roster is comprehensive; tuning + toon-appropriate use is the open work
   (SSR is subtle on flat matte geometry; TAA softens cel edges — both opt-in).
2. **Toon pipeline extensions** — inverse-transpose normals for non-uniform scale;
   instancing; per-object outline tuning; an optional post-process depth+normal
   edge-detect outline variant.
3. **Asset loading** — wire in DiligentTools' `AssetLoader` / `TextureLoader` / glTF
   loaders when pulling in real assets.
4. **Cross-platform** — Linux (Vulkan) first, then macOS (MoltenVK; needs the GLFW
   Cocoa `NSView` `.mm` helper).
5. **Durable docking fix** — fork DiligentTools and pin its imgui to a `docking`
   commit so `git submodule update --recursive` stops reverting the local checkout
   (see MEMORY.md → *Docking*).
6. **Other backends** — re-enable D3D11/D3D12 individually when there's a concrete
   reason (older Windows devices; RenderDoc/PIX).

## Constraints

- **C++17**, **clang everywhere** (clang-cl on Windows, Apple Clang on macOS).
- **Windows builds require a VS Developer environment.**
- Dependencies are **git submodules**, not vcpkg.
- Keep Diligent behind the renderer seam — no Diligent headers outside
  `core/renderer.cpp`.
