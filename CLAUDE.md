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

## Guiding principle — build *on* Diligent, don't reinvent it

**Diligent Engine (Core + Tools + FX) is the framework ToonEngine is built on.** Wherever
Diligent already implements something — glTF / asset / texture loaders, the ImGui
integration, post-processing (DiligentFX), shader cross-compilation, camera & math
utilities — **use Diligent's implementation; don't hand-roll an equivalent.**

What ToonEngine adds is a **thin layer that tames Diligent's boilerplate** (the setup
dance a task needs) and keeps the app/game-facing API **backend- and platform-agnostic**
(any Diligent backend / OS). That is the layer's *only* justification — it is **not**
abstraction for its own sake. Never wrap a Diligent call 1:1 just to hide it; a seam type
earns its place only by removing real boilerplate or by being the portability boundary.
The goal is to *write on Diligent's framework*, not to live in a renderer that
re-implements it.

## Current state

The app opens a window, creates a Vulkan device + swap chain, and each frame draws a
small spinning scene — a smooth **sphere** (non-uniformly scaled into an **ellipsoid**,
exercising the inverse-transpose normal matrix), a faceted **cube**, a **torus**, and a
loaded **glTF model** (`helmet.glb`), on a **ground plane**. Everything is a node in an
**entity-tree scene graph** (`core/scene.h`) with hierarchy-composed world transforms — a
small satellite is parented to the cube and orbits it. The procedural primitives are
cel-shaded with a **banded diffuse fill + inverted-hull silhouette outline** in their own
colors (base + a **per-object outline**); the model is cel-shaded with its **albedo
texture** + an **inverted-hull outline** (via DiligentTools' glTF loader). The scene renders into an **HDR offscreen
target + world-space normal + motion-vector G-buffers** (MRT); a **DiligentFX post
chain** (via `PostFXContext`) applies **SSAO** (temporal-denoised contact shadows),
optional **TAA**, **depth of field**, and **screen-space reflections**, and **Bloom**,
then an **ACES tone-map pass** resolves to the back buffer, with a docked **Dear ImGui**
debug overlay driving the look live (light, band count, per-object colors + outlines, and
every post effect). An **editor camera** navigates the scene — right-drag orbit (+ WASD/QE
fly), middle-drag pan, scroll zoom, F focus — with input suppressed while using the UI.
HLSL shaders cross-compile to SPIR-V at runtime.

## Build

Toolchain: **CMake ≥ 3.20 · Ninja · clang-cl (LLVM) · Visual Studio 2022** (for the
Windows SDK + MSVC CRT/import libs clang-cl targets). C++17/C. Dependencies are **git
submodules** (no vcpkg) — clone recursively (DiligentTools has nested submodules):

```
git submodule update --init --recursive
```

Model assets under `assets/models/` use **Git LFS** — run `git lfs install` once, then
`git lfs pull` to fetch them (a plain clone leaves pointer files). Fonts + icon are normal.

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
    renderer.cpp          Diligent (Vulkan) backend behind the seam: toon PSOs/shaders/mesh buffers + DiligentFX post chain + ImGui-Diligent glue
    math.h                Minimal Diligent-free vector types for the seam's public API
    primitives.{h,cpp}    Procedural CPU mesh generators (sphere/cube/torus/plane) -> toon::MeshData
assets/shaders/           HLSL: toon_common.hlsli + toon_fill/toon_outline + tonemap.hlsl (HLSL->SPIR-V at runtime)
assets/models/            glTF/GLB/FBX test models (helmet/fox/dragon) — Git LFS
assets/fonts/             UI fonts (BaiJamjuree, OpenSans) for the editor overlay
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

**Diligent stays out of the app/game layer, not out of the engine.** `core/renderer.h`
exposes only opaque handles (`TextureHandle`/`BufferHandle`/`ShaderHandle`/`PipelineHandle`)
and a PIMPL `Renderer`; Diligent headers and `Diligent::` types live in the engine's
**implementation** TUs (`core/renderer.cpp` today; Diligent-backed systems such as the
asset loader as the engine grows — per the guiding principle, built directly on Diligent's
modules). The invariant is that the **app/game layer (`main.cpp`) and the public headers
stay Diligent-free and backend-agnostic** — not that a single file owns all Diligent.
`main.cpp` just calls `Renderer::Init / BeginFrame / DrawMesh / EndScene / EndFrame /
Resize / InitUI / BeginUI / EndUI`. A backend swap or console port then swaps those
implementation TUs, not a rewrite.

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

The renderer core is done (toon fill + outline, HDR, full DiligentFX post stack). The next
arc is the **engine/editor layer** — largely porting `ToonEngineOld`'s proven systems
(scene graph, model loading, inspector, input, camera) onto the Vulkan seam. See MEMORY.md
→ *ToonEngineOld carry-over* for the survey + per-system porting notes.

**A. Real assets** — glTF model loading **done**: `assets/models/helmet.glb` loads via
   DiligentTools' `GLTF::Model` (`LoadModel` / `DrawModel` behind the seam) and cel-shades
   with its **albedo texture** in the HDR/MRT/post pipeline (glTF/GLB only — the old
   cgltf/ufbx loader in `ToonEngineOld` is the FBX reference). See MEMORY.md → *glTF model
   loading* for the loader gotchas. The model also gets an **inverted-hull outline**
   (extruded along the shading normal — smooth surfaces stay closed). Follow-ups: normal /
   metallic-roughness maps, `fox.glb` / `dragon.gltf`; procedural-mesh texturing if wanted.

**B. Scene & editor**
3. **Scene graph** — **done**: `core/scene.{h,cpp}` — an entity tree with hierarchy-composed
   world matrices; the render loop walks the scene (via the new `Mat4` `DrawMesh`/`DrawModel`
   overloads), not a hardcoded array. `scene.cpp` is a Diligent-using TU (composition math).
   The editor-triggered mutations (reparent / duplicate / topo-reorder / decompose) are
   deferred to item 5, which exercises them.
4. **Editor camera + input** — **done**: an orbit-around-pivot `Camera` (extends the LH
   turntable) + `core/camera.{h,cpp}` controls (orbit/pan/zoom/fly/focus) + `core/input.{h,cpp}`
   (GLFW polling + ImGui capture gate). Right-drag orbit / mid-drag pan / scroll zoom / WASD
   fly / F focus. The full action-map/rebinding system is deferred (see MEMORY.md).
5. **Editor UI** (next) — inspector + hierarchy panel + themes/fonts + ImGuizmo transform gizmos.
6. **Scene serialization** — save/load scenes to disk.

**C. Environment & fidelity**
7. **Grid + sky gradient** — HLSL ports of the old editor backdrop.
8. **Cascaded shadow maps** — toon-friendly directional shadows (needs seam framebuffer /
   depth-array support).

**D. Later**
9. **Skeletal animation** (play the fox/dragon clips) · 10. **2D / sprites** ·
   11. **Instancing** (deferred — a per-instance draw path for many-object scenes).

**Infra / cross-cutting** (unscheduled): Linux (Vulkan) then macOS (MoltenVK, needs the
GLFW Cocoa `NSView` `.mm` helper); durable docking fix (fork DiligentTools, pin imgui to a
`docking` commit — see MEMORY.md → *Docking*); re-enable D3D11 for older Windows devices.

## Constraints

- **C++17**, **clang everywhere** (clang-cl on Windows, Apple Clang on macOS).
- **Windows builds require a VS Developer environment.**
- Dependencies are **git submodules**, not vcpkg.
- Keep the **app/game layer + public headers Diligent-free and backend-agnostic**;
  Diligent lives in the engine's implementation TUs (see the seam rule + guiding principle).