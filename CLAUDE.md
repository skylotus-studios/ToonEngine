# ToonEngine

From-scratch, cross-platform game engine focused on stylized / toon rendering,
built on **Diligent Engine** (Vulkan-only) + **GLFW** (windowing) + **Dear ImGui**
(debug/editor UI, via DiligentTools). D3D11/D3D12/OpenGL are disabled in
`CMakeLists.txt` to keep build times down. Re-enable one only for a concrete reason
(e.g. D3D12 for RenderDoc/PIX).

The detailed story behind every decision, gotcha, and error message below lives in
**[MEMORY.md](MEMORY.md)**: pull it up when you hit a build error or want the "why"
behind a rule here. **[ARCHIVE.md](ARCHIVE.md)** holds material demoted out of MEMORY.md's
day-to-day lookup path (superseded approaches, full debugging narratives); MEMORY.md links
to it where relevant.

> **History:** started as a from-scratch OpenGL 4.1 renderer (`main` branch), then
> pivoted to Diligent + Vulkan on the `diligent` branch. Mine `main` for reference only.

## Guiding Principle: Build *on* Diligent, Don't Reinvent It

**Diligent Engine (Core + Tools + FX) is the framework ToonEngine is built on.** Wherever
Diligent already implements something (glTF / asset / texture loaders, the ImGui
integration, post-processing via DiligentFX, shader cross-compilation, camera & math
utilities), **use Diligent's implementation; don't hand-roll an equivalent.**

What ToonEngine adds is a **thin layer that tames Diligent's boilerplate** (the setup
dance a task needs) and keeps the app/game-facing API **backend- and platform-agnostic**
(any Diligent backend / OS). That is the layer's *only* justification. It is **not**
abstraction for its own sake. Never wrap a Diligent call 1:1 just to hide it; a type belongs
in the abstraction layer only if it removes real boilerplate or serves as the portability
boundary.
The goal is to *write on Diligent's framework*, not to live in a renderer that
re-implements it.

## Current State

The app opens a window, creates a Vulkan device + swap chain, and each frame draws a small
demo scene: procedural primitives (sphere/cube/torus/plane) and a loaded glTF model, all
nodes in an **entity-tree scene graph** with hierarchy-composed world transforms. Everything
is **cel-shaded** with a banded diffuse fill + inverted-hull outline (per-object color/width),
lit by **cascaded shadow maps** (Diligent's `ShadowMapManager`) from the scene light, rendered
into an **HDR + normal + motion-vector G-buffer**; a **DiligentFX post chain** (SSAO, optional
TAA/DoF/SSR, Bloom) resolves through ACES tone-mapping to the back buffer. A docked
**Dear ImGui editor** (3 themes) provides an Objects hierarchy panel, a Properties inspector
with an ImGuizmo gizmo, a Settings panel (every post effect, a collider debug wireframe
toggle), and an Asset Browser with thumbnails. Clicking an entity in the viewport selects it
(a geometric ray-vs-bounds pick, not a physics raycast, so it works in Editing mode for every
visible entity, collider or not). An **editor camera** and a rebindable **action-map input
system** (mouse/keyboard/gamepad) drive it, with input suppressed while using the UI. HLSL
shaders cross-compile to SPIR-V at runtime. Gameplay state advances via
per-entity native scripts (a demo spin today) and **Jolt Physics** rigid bodies (independent
Collider/Rigid Body components; Box/Sphere/Capsule shapes) on a fixed 60 Hz sim tick,
decoupled from and interpolated into the render rate, and only while an explicit Editing /
Playing / Paused mode (Play/Step/Stop controls in a **Playback** panel) is set to Playing.
Stop always reverts the scene, and the physics world, to how it was before Play started.
**miniaudio** drives positional 3D SFX and music through an `AudioSource` entity component,
its listener tracking the interpolated editor camera each rendered frame.

See **[docs/architecture.md](docs/architecture.md)** for the full design: the renderer's
abstraction layer, frame loop, rendering pipeline, scene model, and data flow.

## Build

Toolchain: **CMake ≥ 3.20 · Ninja · clang-cl (LLVM) · Visual Studio 2022** (for the
Windows SDK + MSVC CRT/import libs clang-cl targets). C++17/C. Dependencies are **git
submodules** (no vcpkg). Clone recursively (DiligentTools has nested submodules):

```
git submodule update --init --recursive
```

Model assets under `assets/models/` use **Git LFS**: run `git lfs install` once, then
`git lfs pull` to fetch them (a plain clone leaves pointer files). Fonts + icon are normal.

**The IDE is CLion.** One-time toolchain + preset setup is in
**[docs/clion-setup-windows.md](docs/clion-setup-windows.md)** (Linux/macOS setup docs
also exist under `docs/` for when those platforms land; see Platform support below).
In short: a CLion **Visual Studio toolchain** supplies the VS Developer environment
automatically (the Windows SDK tools clang-cl needs; otherwise configure fails at
`CMAKE_MT-NOTFOUND`), and CLion reads `CMakePresets.json` for the `windows-debug` /
`windows-release` profiles.

**Command line / CI** needs that environment too: open a **Developer PowerShell for VS
2022** (Start Menu shortcut, installed by Visual Studio itself; no repo script for this,
see MEMORY.md → *Build gotchas*), then use the presets:

```
cmake --preset windows-debug
cmake --build --preset windows-debug
./build/windows-debug/ToonEngine.exe
```

Presets build into `build/<preset>/` with the engine DLLs copied next to the exe.

## Source Layout

```
src/
  main.cpp                Entry point: window + editor loop glue (calls into app/ + ui/panels/); no Diligent headers
  app/                    Editor state (plain EditorState struct) + init/tick/render/physics-glue/picking/scene-ops free functions
  core/
    math.h                 Minimal Diligent-free vector/matrix types for the abstraction layer's public API
    rendering/             The abstraction layer + its Diligent (Vulkan) backend, plus procedural mesh generators
    scene/                 Entity-tree scene graph, native scripts (+ scripts/), .scene serialization
    physics/               Physics abstraction layer: opaque BodyHandle + data-encapsulated PhysicsWorld (Jolt)
    camera/                Editor camera controls: orbit/pan/zoom/fly/focus
    input/                 GLFW device/gamepad polling, action maps + rebinding (assets/input.json)
  ui/
    thumbnail_cache.{h,cpp}  Path -> texture cache for the browser's inline icons/preview
    panels/                 Every ImGui editor panel (menu bar, dockspace, Playback, Objects, Properties,
                             gizmo overlay, Settings, themes, Asset Browser) as DrawXPanel(EditorState&)
assets/shaders/           HLSL: toon_common.hlsli + toon_fill/toon_outline + model_fill/model_outline + tonemap.hlsl + wireframe.hlsl
assets/models/            glTF/GLB/FBX test models (helmet/fox/dragon; Git LFS)
assets/fonts/             UI fonts (BaiJamjuree, OpenSans) for the editor overlay
assets/scenes/            Saved .scene text files (core/scene/serializer.h); created on first Save
external/                 Git submodules (see .gitmodules): DiligentCore/Tools/FX, glfw, ImGuizmo, imgui, JoltPhysics
CMakeLists.txt            add_subdirectory the submodules; disables unused Diligent backends
CMakePresets.json         windows-debug / windows-release (Ninja + clang-cl)
.clangd                   Points clangd at build/windows-debug's compile_commands.json
docs/architecture.md          Full architecture writeup: abstraction layer, frame loop, pipeline, data flow
docs/roadmap.md                One prioritized list, shipped to not-yet-started (see CLAUDE.md's Roadmap)
docs/clion-setup-windows.md    CLion toolchain + preset + debug setup (Windows, active)
docs/clion-setup-{linux,macos}.md  Setup notes for those platforms (planned)
docs/cpp-style-guide.md        C++ house style (formatting + comments + abstraction-layer rules)
docs/md-style-guide.md       Prose/writing style (no puffery, no em-dash spam, no AI tells)
.claude/skills/tidy-cpp/     Skill: clean src/** to the style guide (/tidy-cpp)
.claude/skills/tidy-md/      Skill: keep CLAUDE.md/README.md/docs/** accurate + right-sized (/tidy-md)
.claude/skills/verify/       Skill: build/launch/screenshot-verify (no live input desktop here)
```

## The Renderer Abstraction Layer (Load-Bearing Rule)

**Diligent stays out of the app/game layer, not out of the engine.** `core/rendering/renderer.h`
exposes only opaque handles (`TextureHandle`/`BufferHandle`/`ShaderHandle`/`PipelineHandle`)
and a data-encapsulated `Renderer`; Diligent headers and `Diligent::` types live in the engine's
**implementation** TUs (`core/rendering/renderer.cpp` today; Diligent-backed systems such as the
asset loader as the engine grows, per the guiding principle, built directly on Diligent's
modules). The invariant is that the **app/game layer (`main.cpp`, `src/app/`, `ui/panels/`)
and the public headers stay Diligent-free and backend-agnostic**; not that a single file
owns all Diligent. The app layer just calls `Renderer::Init / BeginFrame / DrawMesh /
EndScene / EndFrame / Resize / InitUI / BeginUI / EndUI`. A backend swap or console port
then swaps those implementation TUs, not a rewrite.

**Dear ImGui is exempt**: it's a plain UI library; engine/game code may `#include
"imgui.h"` and call `ImGui::` directly (as `ui/panels/*.cpp` does). Only its Diligent render
backend stays in `core/rendering/renderer.cpp`.

**Toon draw (`DrawMesh`)** runs two passes over one mesh sharing a dynamic constant
buffer: the **outline** pass (inverted hull, extrude along the normal, cull front)
then the **fill** pass (banded diffuse, cull back); the fill's nearer depth overwrites
the enlarged shell everywhere but the silhouette rim. See MEMORY.md for the
matrix-convention, winding, and outline-ordering details.

## Conventions

- **HLSL** for all shaders (cross-compiled to SPIR-V by Diligent at runtime).
- Diligent objects are COM-refcounted: hold them in `RefCntAutoPtr<>`, namespace
  `Diligent`.
- Disable Diligent backends/modules you're not using (`DILIGENT_NO_*`, set as
  `CACHE BOOL ... FORCE` before `add_subdirectory(DiligentCore)`); it builds every
  supported backend by default, which dominates compile time. We also set
  `DILIGENT_NO_RADIENT` (DiligentFX's GI module: unused, and it fails to compile
  under clang-cl; a full `cmake --build` / CI hits it even though CLion doesn't).
- Target-based CMake only (`target_*`).
- C++17, clang everywhere (clang-cl on Windows, Apple Clang on macOS).
- Windows builds require the VS Developer environment (see Build).

## Platform Support

| Platform | Status  | Backend                 | Notes |
|----------|---------|-------------------------|-------|
| Windows  | active  | Vulkan                  | primary dev target; D3D11/D3D12/OpenGL disabled |
| Linux    | planned | Vulkan                  | X11 handles wired; Wayland fields exist |
| macOS    | planned | Vulkan via **MoltenVK** | needs `NSView` from a GLFW Cocoa `.mm` helper |

## Roadmap

**[docs/roadmap.md](docs/roadmap.md)** is one list, start to finish: what's already shipped
(see also Current State above), then everything left, ranked from what to work on next down
to what's least urgent right now. No milestone codenames, no separate "someday" bucket for
infra: every remaining item, gameplay or otherwise, has an actual rank.