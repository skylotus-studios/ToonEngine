 # ToonEngine Invariants

The six hard rules from [CLAUDE.md](../CLAUDE.md), with the reasoning behind each. CLAUDE.md
states them; this file explains them. Break one only with a stated reason, and update this file
when you do.

For the story behind how a rule came to exist, including the build errors and dead ends that
produced it, read [MEMORY.md](../MEMORY.md) on demand.

## 1. Build on Diligent, Don't Reinvent It

Diligent Engine (Core, Tools, and FX) is the framework ToonEngine is built on. Where Diligent
already implements something, use its implementation instead of hand-rolling an equivalent.
That covers the glTF, asset, and texture loaders, the ImGui integration, post-processing
through DiligentFX, shader cross-compilation, and the camera and math utilities.

What ToonEngine adds is a thin layer that tames Diligent's boilerplate, meaning the setup dance
a given task requires, and keeps the app-facing API agnostic of backend and platform. That is
the layer's only justification. It is not abstraction for its own sake. Never wrap a Diligent
call one-to-one just to hide it. A type belongs in the abstraction layer only if it removes
real boilerplate or serves as the portability boundary. The goal is to write on Diligent's
framework, not to live in a renderer that re-implements it.

## 2. The App Layer Stays Diligent-Free

Diligent stays out of the app and game layer, not out of the engine.

`core/rendering/renderer.h` exposes only opaque handles (`TextureHandle`, `BufferHandle`,
`ShaderHandle`, `PipelineHandle`) and a data-encapsulated `Renderer`. Diligent headers and
`Diligent::` types live in the engine's implementation TUs: `core/rendering/renderer.cpp`
today, and Diligent-backed systems such as the asset loader as the engine grows, built
directly on Diligent's modules per rule 1.

The invariant is that the app and game layer (`main.cpp`, `src/app/`, `ui/panels/`) and the
public headers stay Diligent-free and backend-agnostic. It is not that a single file owns all
Diligent. The app layer calls `Renderer::Init`, `BeginFrame`, `DrawMesh`, `EndScene`,
`EndFrame`, `Resize`, `InitUI`, `BeginUI`, and `EndUI`. A backend swap or a console port then
swaps those implementation TUs rather than forcing a rewrite.

`core/physics/` follows the same shape: an opaque `BodyHandle` and a data-encapsulated
`PhysicsWorld`, with every Jolt type quarantined in `physics.cpp`.

### Dear ImGui Is Exempt

Dear ImGui is a plain UI library, so engine and game code may include `imgui.h` and call
`ImGui::` directly, as `ui/panels/*.cpp` does. Only its Diligent render backend stays in
`core/rendering/renderer.cpp`.

### The Toon Draw

`DrawMesh` runs two passes over one mesh sharing a dynamic constant buffer: the outline pass
(inverted hull, extruded along the normal, front faces culled), then the fill pass (banded
diffuse, back faces culled). The fill's nearer depth overwrites the enlarged shell everywhere
except the silhouette rim. The matrix-convention, winding, and outline-ordering details that
make this work are in [MEMORY.md](../MEMORY.md).

## 3. HLSL Only

All shaders are HLSL, cross-compiled to SPIR-V by Diligent at runtime. Shader sources live in
`assets/shaders/`.

## 4. Disable Unused Diligent Backends and Modules

Diligent builds every supported backend by default, which dominates compile time. Set the
`DILIGENT_NO_*` options as `CACHE BOOL ... FORCE` before `add_subdirectory(DiligentCore)`.

`DILIGENT_NO_RADIENT` is also set. That is DiligentFX's GI module: it is unused here, and it
fails to compile under clang-cl. A full `cmake --build` or a CI run hits it even though CLion's
incremental build does not.

D3D11, D3D12, and OpenGL are disabled in `CMakeLists.txt`. Re-enable one only for a concrete
reason, such as D3D12 for RenderDoc or PIX capture.

## 5. Toolchain and Language

C++17 and C, clang everywhere (clang-cl on Windows, Apple Clang on macOS). Target-based CMake
only, meaning the `target_*` commands rather than directory-scoped ones. Dependencies are git
submodules under `external/`; there is no vcpkg.

Windows builds require the Visual Studio Developer environment for the Windows SDK tools that
clang-cl needs. Without it, configure fails at `CMAKE_MT-NOTFOUND`. See
[clion-setup-windows.md](clion-setup-windows.md).

Diligent objects are COM-refcounted: hold them in `RefCntAutoPtr<>` in namespace `Diligent`.

## 6. Golden-Image Re-Baselining

`scripts/run_golden_tests.py` runs `ToonPlayer --headless-render --post off` against
`tests/scenes/golden_*.scene` and diffs each capture, perceptually, against the checked-in
`tests/golden/<name>/frame.png` (see `scripts/golden_diff.py`'s own docstring for the metric).
The value of that test depends entirely on a red result meaning something: the moment a red
golden test becomes "just re-run `rebaseline.py`" instead of "explain what changed", it stops
catching real regressions and starts rubber-stamping them — a shader constant flipped, a shadow
cascade silently disabled, a sprite-blend order broken all look identical to "this diff was
expected" from a script's point of view.

`scripts/rebaseline.py` is the mechanical enforcement: it refuses to run without `--reason "..."`
(`argparse`'s `required=True`, not a convention), and stages the new baseline PNGs plus that
reason for the commit rather than committing them itself. The reason belongs in the commit
message. Nate looks at the diff image before a re-baseline merges — that's a review step, not
something a script can substitute for.

## Code Style

`docs/cpp-style-guide.md` carries the house C++ style, including the section 7 audit that
prefers plain structs and free functions over classes with private members. Encapsulation is
for hiding a real external dependency or removing real boilerplate, not for tidiness.

`cpp-style-guide.md`, `md-style-guide.md`, `CLAUDE.md`, `MEMORY.md`, and `ARCHIVE.md` are
local-only by design: gitignored on every branch and never pushed. Links to them from this file
resolve on a working checkout and not in a fresh clone.