# ToonEngine

![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![Vulkan](https://img.shields.io/badge/graphics-Vulkan-red.svg)
![Platform](https://img.shields.io/badge/platform-Windows-lightgrey.svg)

A from-scratch, cross-platform stylized rendering engine built on Vulkan via [Diligent
Engine](https://github.com/DiligentGraphics/DiligentEngine), with a full in-editor scene
authoring workflow: an entity hierarchy, an inspector, transform gizmos, and a live-tunable
HDR post-processing stack, all built on the seam described below.

*A [Skylotus Studios](LICENSE.md) project.*

![ToonEngine editor: a cel-shaded scene (sphere, cube, torus, glTF helmet) with SSAO contact
shadows and bloom, alongside the docked scene hierarchy, inspector, and debug
panels](docs/screenshots/editor-overview.png)

## Highlights

- Custom toon/cel-shading pipeline: banded diffuse lighting plus inverted-hull silhouette
  outlines, with per-object base and outline color and width, applied the same way to
  procedural primitives and textured glTF models.
- Full HDR post-processing stack via DiligentFX's `PostFXContext`: temporal-denoised SSAO,
  TAA, depth of field, screen-space reflections, bloom, and an ACES filmic tone map, every
  parameter live-tunable from the editor.
- Real scene graph: an entity tree with hierarchy-composed world transforms, parent/child
  relationships, and world-preserving reparenting, so dragging an object under a new parent
  doesn't move it in world space.
- Editor UI built from scratch on Dear ImGui: a docked layout with 3 selectable themes, a
  scene hierarchy with drag-drop reparenting, an inspector with live material and transform
  editing, and ImGuizmo move/rotate/scale gizmos with Unity-style hotkeys (W/E/R/X) and
  snapping.
- glTF model loading via Diligent's own asset loader, textured and cel-shaded with the same
  inverted-hull outline technique as the procedural geometry.
- Editor camera: orbit, pan, zoom, and WASD/QE fly, with input capture suppressed correctly
  while interacting with the UI or dragging a gizmo.
- Built for portability: a seam keeps every Diligent/Vulkan type out of the application
  layer (see Architecture below), so a backend swap or a new platform port means writing
  another implementation file instead of rewriting the engine.

The SSAO/TAA pipeline had a temporal-reprojection ghosting bug that took six rounds of
investigation to root-cause: the post-processing context had no real previous-frame depth
history, and a rotating silhouette's true motion can't be fully captured by a per-vertex
motion vector. Fixed with a real double-buffered depth history instead of a partial
workaround. Full write-up in [MEMORY.md](MEMORY.md).

## Tech Stack

| | |
|---|---|
| Language | C++17 |
| Graphics API | Vulkan, via [Diligent Engine](https://github.com/DiligentGraphics/DiligentEngine) (Core + Tools + FX) |
| Shaders | HLSL, cross-compiled to SPIR-V at runtime |
| Windowing | GLFW |
| Editor UI | Dear ImGui (docking branch) + ImGuizmo |
| Build | CMake + Ninja + clang-cl (LLVM) |
| Assets | glTF / GLB, fetched via Git LFS |

## Architecture

The engine is built on Diligent, not around a reimplementation of it: asset loading, the
ImGui render backend, post-processing, and shader cross-compilation are all Diligent's own.
What ToonEngine adds is a thin seam: `core/renderer.h` exposes opaque resource handles and a
PIMPL `Renderer`, keeping every Diligent header and type behind `core/renderer.cpp`. The
application layer (`main.cpp`) never includes a Diligent header; it calls `Init` /
`BeginFrame` / `DrawMesh` / `EndScene` / `EndFrame`. A backend swap or a console port means
writing another `renderer_*.cpp`, not a rewrite.

See [CLAUDE.md](CLAUDE.md) for the full architecture writeup and [MEMORY.md](MEMORY.md) for
the detailed history and reasoning behind the non-obvious decisions.

## Building

Requires CMake 3.20+, Ninja, clang-cl (LLVM), and Visual Studio 2022 (for the Windows SDK
and MSVC libs clang-cl targets). Dependencies are git submodules; clone recursively
(DiligentTools has nested submodules of its own):

```
git submodule update --init --recursive
git lfs pull        # fetch the LFS-tracked model assets
```

```
cmake --preset windows-debug
cmake --build --preset windows-debug
./build/windows-debug/ToonEngine.exe
```

The IDE is **CLion**. See [docs/clion-setup-windows.md](docs/clion-setup-windows.md) for
one-time toolchain and preset setup (Linux and macOS setup docs also exist, for when those
platforms land).

## Status

Windows on Vulkan is the active target; Linux (Vulkan) and macOS (Vulkan via MoltenVK) are
planned. See [CLAUDE.md](CLAUDE.md) → *Roadmap* for what's shipped and what's next.

## License

[MIT](LICENSE.md)
