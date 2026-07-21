# ToonEngine

![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue.svg)
![Vulkan](https://img.shields.io/badge/graphics-Vulkan-red.svg)
![Platform](https://img.shields.io/badge/platform-Windows-lightgrey.svg)

A from-scratch, cross-platform stylized rendering engine built on Vulkan via [Diligent
Engine](https://github.com/DiligentGraphics/DiligentEngine), with a full in-editor scene
authoring workflow: an entity hierarchy, an inspector, transform gizmos, and a live-tunable
HDR post-processing stack, all built on the abstraction layer described below.

*A [Skylotus Studios](LICENSE.md) project.*

![ToonEngine editor: a cel-shaded scene (sphere, cube, torus, glTF helmet) with SSAO contact
shadows and bloom, alongside the docked scene hierarchy, inspector, debug, and asset browser
panels](docs/screenshots/editor-overview.png)

## Highlights

- Custom toon/cel-shading pipeline: banded diffuse lighting plus inverted-hull silhouette
  outlines, with per-object base and outline color and width, applied the same way to
  procedural primitives and textured glTF models.
- Cascaded shadow maps (4 cascades, PCF-filtered) via Diligent's own `ShadowMapManager`,
  feeding directly into the cel-shading ramp, so a shadowed pixel lands on a darker rung of
  the same toon band ladder.
- Full HDR post-processing stack via DiligentFX's `PostFXContext`: temporal-denoised SSAO,
  TAA, depth of field, screen-space reflections, bloom, and an ACES filmic tone map, every
  parameter live-tunable from the editor.
- Real scene graph: an entity tree with hierarchy-composed world transforms, parent/child
  relationships, and world-preserving reparenting, so dragging an object under a new parent
  doesn't move it in world space.
- Play / Pause / Step simulation control on a fixed 60 Hz sim tick, decoupled from and
  interpolated into the render rate: an explicit editing-vs-playing mode with a
  snapshot-and-restore sandbox, so testing gameplay never permanently alters a hand-placed
  scene.
- Native gameplay scripting: per-entity `Update` hooks through a script-component slot
  (Unity/Hazel-style), reconstructed from a name registry so scripts round-trip through
  scene save/load and the Play/Pause/Step sandbox alike.
- Physics and collision via Jolt Physics: independent Collider and Rigid Body entity
  components (Box/Sphere/Capsule; Static/Dynamic/Kinematic), built fresh from the scene on
  Play and stepped on the same fixed tick, with a collider debug wireframe overlay.
- Scene serialization: save/load the full hierarchy, transforms, materials, and lighting to
  a human-readable text file. Procedural geometry round-trips from its generator parameters
  (radius, segment counts), so a saved scene has no binary blobs.
- Editor UI built from scratch on Dear ImGui: a docked layout with 3 selectable themes, a
  scene hierarchy with drag-drop reparenting, an inspector with live material, transform,
  and light editing, ImGuizmo move/rotate/scale gizmos with Unity-style hotkeys (W/E/R/X)
  and snapping, and an asset browser with sortable file listings and image thumbnails.
- glTF model loading via Diligent's own asset loader, textured and cel-shaded with the same
  inverted-hull outline technique as the procedural geometry.
- Editor camera: orbit, pan, zoom, and WASD/QE fly, with input capture suppressed correctly
  while interacting with the UI or dragging a gizmo.
- Built for portability: an abstraction layer keeps every Diligent/Vulkan type out of the
  application code (see Architecture below), so a backend swap or a new platform port is
  scoped to a single new implementation file.

The SSAO/TAA pipeline had a temporal-reprojection ghosting bug that took six rounds of
investigation to root-cause: the post-processing context had no real previous-frame depth
history, and a rotating silhouette's true motion can't be fully captured by a per-vertex
motion vector. Fixed with a real double-buffered depth history.

## Tech Stack

| | |
|---|---|
| Language | C++17 |
| Graphics API | Vulkan, via [Diligent Engine](https://github.com/DiligentGraphics/DiligentEngine) (Core + Tools + FX) |
| Shaders | HLSL, cross-compiled to SPIR-V at runtime |
| Windowing | GLFW |
| Physics | [Jolt Physics](https://github.com/jrouwe/JoltPhysics) |
| Editor UI | Dear ImGui (docking branch) + ImGuizmo |
| Build | CMake + Ninja + clang-cl (LLVM) |
| Assets | glTF / GLB, fetched via Git LFS |

## Architecture

The engine is built directly on Diligent: asset loading, the ImGui render backend,
post-processing, and shader cross-compilation are all Diligent's own.
What ToonEngine adds is a thin abstraction layer: `core/rendering/renderer.h` exposes opaque
resource handles and a data-encapsulated `Renderer`, keeping every Diligent header and type
behind `core/rendering/renderer.cpp`. The application layer (`main.cpp`, `src/app/`,
`ui/panels/`) never includes a Diligent header; it calls `Init` / `BeginFrame` / `DrawMesh` /
`EndScene` / `EndFrame`. A backend swap or a console port is scoped to writing another
`renderer_*.cpp`.

See [docs/architecture.md](docs/architecture.md) for the full architecture writeup.

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
one-time toolchain and preset setup.

## Roadmap

Windows on Vulkan is currently supported. Linux (Vulkan) and macOS (Vulkan via MoltenVK) are
planned. See [docs/roadmap.md](docs/roadmap.md) for what's shipped and for the full ranked
list of what's next.

```mermaid
flowchart LR
  subgraph V01["v0.1: Foundation"]
    direction TB
    S1["Vulkan renderer &amp; toon pipeline"]
    S2["Dear ImGui editor"]
    S3["Editor camera &amp; input"]
    S4["Scene graph &amp; serialization"]
    S1 --> S2 --> S3 --> S4
  end

  subgraph V02["v0.2: Simulation"]
    direction TB
    S5["Fixed-timestep simulation"]
    S6["Physics: Jolt"]
    S7["Audio: miniaudio"]
    S5 --> S6 --> S7
  end

  subgraph V03["v0.3: Interaction"]
    direction TB
    S8["Mouse-pick via raycast"]
    S9["Contact events to scripts"]
    S10["Shader hot-reload"]
    S8 --> S9 --> S10
  end

  subgraph V04["v0.4: Characters &amp; World"]
    direction TB
    N11["Skeletal animation"]
    N12["Grid &amp; sky gradient"]
    N13["2D &amp; sprites"]
    N11 --> N12 --> N13
  end

  subgraph V05["v0.5: Scale &amp; Tools"]
    direction TB
    N14["Instancing"]
    N15["Frustum culling"]
    N16["Prefabs"]
    N17["Particles &amp; VFX"]
    N14 --> N15 --> N16 --> N17
  end

  subgraph V10["v1.0: Ship"]
    direction TB
    N18["Steamworks SDK bootstrap"]
    N19["Settings menu"]
    N20["Controller UI &amp; Steam Deck keyboard"]
    N21["Crash reporting"]
    N22["Asset packaging"]
    N18 --> N19 --> N20 --> N21 --> N22
  end

  subgraph V11["v1.1: Platform Expansion"]
    direction TB
    N23["Linux support"]
    N24["macOS support"]
    N25["Re-enable D3D11"]
    N23 --> N24 --> N25
  end

  V01 --> V02 --> V03 --> V04 --> V05 --> V10 --> V11

  classDef v01 fill:#5C8A7D,stroke:#3f6357,color:#EAF6F1;
  classDef v02 fill:#6B9C8C,stroke:#4a7062,color:#EAF6F1;
  classDef v03 fill:#5C8A7D,stroke:#3f6357,color:#EAF6F1;
  classDef v04 fill:#FF9F40,stroke:#c97927,color:#2B1400;
  classDef v05 fill:#F4C542,stroke:#c99e28,color:#2B2100;
  classDef v10 fill:#4FA8D8,stroke:#2f7fac,color:#00202E;
  classDef v11 fill:#8A7CFF,stroke:#6357cc,color:#160B3E;
  classDef shipped fill:#5C8A7D,stroke:#3f6357,color:#EAF6F1;

  class S1,S2,S3,S4 v01
  class S5,S6,S7 v02
  class S8,S9,S10 v03
  class N11,N12,N13 v04
  class N14,N15,N16,N17 v05
  class N18,N19,N20,N21,N22 v10
  class N23,N24,N25 v11
```

## License

[MIT](LICENSE.md)