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

The app opens a window, creates a Vulkan device + swap chain, and each frame draws a small
demo scene — procedural primitives (sphere/cube/torus/plane) and a loaded glTF model, all
nodes in an **entity-tree scene graph** with hierarchy-composed world transforms. Everything
is **cel-shaded** with a banded diffuse fill + inverted-hull outline (per-object color/width),
lit by **cascaded shadow maps** (Diligent's `ShadowMapManager`) from the scene light, rendered
into an **HDR + normal + motion-vector G-buffer**; a **DiligentFX post chain** (SSAO, optional
TAA/DoF/SSR, Bloom) resolves through ACES tone-mapping to the back buffer. A docked
**Dear ImGui editor** (3 themes) provides a Scene Hierarchy, an Inspector with an ImGuizmo
gizmo, a Debug panel (scene save/load, every post effect), and an Asset Browser with
thumbnails. An **editor camera** and a rebindable **action-map input system** (mouse/keyboard/
gamepad) drive it, with input suppressed while using the UI. HLSL shaders cross-compile to
SPIR-V at runtime. Gameplay state (today: the demo spin) advances on a fixed 60 Hz sim tick,
decoupled from and interpolated into the render rate, and only while an explicit Editing /
Playing / Paused mode (Play/Step/Stop controls in a **Playback** panel) is set to Playing.
Stop always reverts the scene to how it was before Play started.

See **[docs/architecture.md](docs/architecture.md)** for the full design: the renderer seam,
frame loop, rendering pipeline, scene model, and data flow.

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

**Command line / CI** needs that environment too — open a **Developer PowerShell for VS
2022** (Start Menu shortcut, installed by Visual Studio itself — no repo script for this,
see MEMORY.md → *Build gotchas*), then use the presets:

```
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
    renderer.h            The seam: opaque handles + scene types (Vertex/Camera/Material/Transform/PostParams) + PIMPL Renderer
    renderer.cpp          Diligent (Vulkan) backend behind the seam: toon PSOs/shaders/mesh buffers + DiligentFX post chain + ImGui-Diligent glue
    math.h                Minimal Diligent-free vector/matrix types for the seam's public API
    primitives.{h,cpp}    Procedural CPU mesh generators (sphere/cube/torus/plane) -> toon::MeshData
    scene.{h,cpp}         Entity-tree scene graph: hierarchy, world-transform composition, editor mutations
    camera.{h,cpp}        Editor camera controls: orbit/pan/zoom/fly/focus
    input/                 GLFW device/gamepad polling, action maps + rebinding (assets/input.json)
    serializer.{h,cpp}    Scene save/load — entity/camera state to a text .scene file
  ui/
    file_browser.{h,cpp}  "Asset Browser" panel: breadcrumb nav, sortable file table, preview pane
    thumbnail_cache.{h,cpp}  Path -> texture cache for the browser's inline icons/preview
assets/shaders/           HLSL: toon_common.hlsli + toon_fill/toon_outline + model_fill/model_outline + tonemap.hlsl
assets/models/            glTF/GLB/FBX test models (helmet/fox/dragon) — Git LFS
assets/fonts/             UI fonts (BaiJamjuree, OpenSans) for the editor overlay
assets/scenes/            Saved .scene text files (core/serializer.h); created on first Save
external/                 Git submodules (see .gitmodules): DiligentCore/Tools/FX, glfw, ImGuizmo, imgui
CMakeLists.txt            add_subdirectory the submodules; disables unused Diligent backends
CMakePresets.json         windows-debug / windows-release (Ninja + clang-cl)
.clangd                   Points clangd at build/windows-debug's compile_commands.json
docs/architecture.md          Full architecture writeup: seam, frame loop, pipeline, data flow
docs/clion-setup-windows.md    CLion toolchain + preset + debug setup (Windows, active)
docs/clion-setup-{linux,macos}.md  Setup notes for those platforms (planned)
docs/cpp-style-guide.md        C++ house style (formatting + comments + seam rules)
docs/md-style-guide.md       Prose/writing style (no puffery, no em-dash spam, no AI tells)
.claude/skills/tidy-cpp/     Skill: clean src/** to the style guide (/tidy-cpp)
.claude/skills/tidy-md/      Skill: keep CLAUDE.md/README.md/docs/** accurate + right-sized (/tidy-md)
.claude/skills/verify/       Skill: build/launch/screenshot-verify (no live input desktop here)
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

The renderer and editor are done (toon fill + outline, HDR + full DiligentFX post stack;
scene graph, inspector/gizmos, editor camera, action-map input, serialization, asset
browser). Today the app is an editor that draws a scene; the arc below turns it into a
runtime you can build a game on, sequenced by dependency. See docs/architecture.md for the
current design and MEMORY.md for history + the ToonEngineOld carry-over survey.

**M1 — Simulation foundation.** Turn the editor into a game runtime.
1. Entity behavior system — per-entity Update hooks (a component/behavior layer, ECS as a
   later scaling option). Entities are render-data only today; this is the core gap.

**M2 — Interaction and world.** Rules and feedback.
1. Physics + collision — rigid bodies, colliders, raycasts (new submodule, e.g. Jolt).
2. Audio — SFX + music, positional (new submodule, e.g. miniaudio).

**M3 — Characters and fidelity.** Populate and light the world (ports from `ToonEngineOld`).
1. Skeletal animation — play the fox/dragon clips; an animation entity component.
2. Grid + sky gradient — HLSL port of the old editor backdrop.
3. 2D / sprites, plus a sprite entity component.

**M4 — Scale and polish** (later): instancing (a per-instance draw path for many-object
scenes); particles / VFX; prefabs (reusable entity templates for runtime spawning).

**Infra / cross-cutting** (unscheduled): Linux (Vulkan) then macOS (MoltenVK, needs the
GLFW Cocoa `NSView` `.mm` helper); re-enable D3D11 for older Windows devices; shader
hot-reload via Diligent's `IRenderStateCache` (`EnableHotReload` + `Reload()`, already
reachable through the linked `Diligent-GraphicsTools`); asset packaging + relative
shader/asset paths for shipping a build.

## Constraints

- **C++17**, **clang everywhere** (clang-cl on Windows, Apple Clang on macOS).
- **Windows builds require a VS Developer environment.**
- Dependencies are **git submodules**, not vcpkg.
- Keep the **app/game layer + public headers Diligent-free and backend-agnostic**;
  Diligent lives in the engine's implementation TUs (see the seam rule + guiding principle).