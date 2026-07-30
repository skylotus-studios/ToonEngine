# ToonEngine Invariants

The six hard rules from [CLAUDE.md](../CLAUDE.md), with the reasoning behind each. CLAUDE.md
states them; this file explains them and names what checks each one. Break one only with a
stated reason, and update this file when you do.

For the story behind how a rule came to exist, including the build errors and dead ends that
produced it, read [MEMORY.md](../MEMORY.md) on demand.

## What Checks What

| Rule | Enforced by |
|---|---|
| 1. Build on Diligent | Review only. No script can judge whether a wrapper earns its place. |
| 2. The seams stay backend-free | `scripts/check_invariants.py --check seams` |
| 3. HLSL only | `scripts/check_invariants.py --check shaders` |
| 4. Disable unused Diligent modules | `scripts/check_invariants.py --check cmake` |
| 5. Toolchain and language | `--check cmake` covers the CMake half; the compiler choice is not asserted |
| 6. Golden-image re-baselining | `scripts/rebaseline.py` refuses to run without `--reason`, then a person looks at the diff |

`scripts/check_invariants.py` is the fast tier's first step in `scripts/verify.py` and needs no
build, so a violation costs a second instead of a full compile. It reads source text only. The
parts of these rules that need judgment stay with the reviewer, and this file says which parts
those are rather than implying a green run means every rule held.

## Terms Used Below

A *seam* is a header that names a whole subsystem in ToonEngine's own vocabulary, with the
library that implements it hidden behind. There are three: `core/rendering/renderer.h` over
Diligent, `core/physics/physics.h` over Jolt, `core/audio/audio.h` over miniaudio.

An *opaque handle* is a plain 32-bit id standing in for a resource, like `MeshHandle` or
`BodyHandle`. Calling code holds the number and can do nothing with it except hand it back;
only the implementation knows which real object it points at. Zero always means invalid.

*PIMPL* (pointer to implementation) is how the seams hide their libraries. The class in the
header declares one member, `Impl *m_impl`, and `struct Impl` is defined in the `.cpp` file.
Every Diligent or Jolt type lives inside that struct, so nothing that includes the header ever
sees one. A *translation unit* (TU) is one `.cpp` file plus everything it includes: the unit
the compiler actually processes, and the unit a library can be quarantined inside.

## 1. Build on Diligent, Don't Reinvent It

Diligent Engine (Core, Tools, and FX) is the framework ToonEngine is built on. Where Diligent
already implements something, use its implementation instead of hand-rolling an equivalent.
That covers the glTF and texture loaders (`Diligent-AssetLoader`, `Diligent-TextureLoader`),
the ImGui rendering backend (`Diligent-Imgui`), the post chain (`DiligentFX`: bloom, SSAO, SSR,
depth of field, temporal AA), shader cross-compilation, and the matrix and quaternion math
behind the seam.

What ToonEngine adds is a thin layer that tames Diligent's boilerplate, meaning the setup dance
a given task requires, and keeps the app-facing API agnostic of graphics backend and platform.
That is the layer's only justification. Never wrap a Diligent call one-to-one just to hide it.
A type belongs in the abstraction layer only if it removes real boilerplate or serves as the
portability boundary.

### Where This Rule Yields to Rule 2

Rule 1 and rule 2 pull in opposite directions at the seam, and rule 2 wins there. Two places in
the code make that concrete, and neither is a violation:

`src/core/math.h` defines `Vec2`, `Vec3`, `Vec4`, `Quat`, and `Mat4` as plain structs, plus a
small set of quaternion operations, rather than using Diligent's `BasicMath.hpp`. It has to:
these are the types the seam's function signatures are written in, and a Diligent type in a
seam signature would defeat the seam. The heavier math still happens on the Diligent side.
`Mat4` carries no math at all, only sixteen floats in Diligent's own row-major layout so the
conversion is a straight copy, and `math.h`'s own comments record which Diligent function each
hand-rolled quaternion operation mirrors field for field.

`src/core/camera/camera.h` is ToonEngine's own orbit, pan, zoom, fly, and focus controls as
free functions over the seam's `Camera` struct. Diligent's linked modules ship no camera
utility to reuse. `camera.cpp` uses Diligent's `float4x4` to derive the camera basis, so it
matches the renderer's view convention exactly.

Read the rule as: reuse Diligent's implementations, and where a seam signature forbids that,
mirror Diligent's semantics in plain data and say so in a comment.

## 2. The App Layer Stays Diligent-Free

Diligent stays out of the app and game layer, not out of the engine.

`src/core/rendering/renderer.h` speaks only in opaque handles (`TextureHandle`, `BufferHandle`,
`ShaderHandle`, `PipelineHandle`, `MeshHandle`, `ModelHandle`) and plain structs from
`core/math.h`. It includes no Diligent header; `Renderer` holds a single `Impl *`, and every
Diligent type lives in that struct inside `renderer.cpp`. The seam is Diligent-free, not
dependency-free: it forward-declares `GLFWwindow` for the four calls that need a window
(`Init`, `InitUI`, `SetWindowIcon`, `SetTitleBarTheme`).

Three TUs carry Diligent code today: `src/core/rendering/renderer.cpp`,
`src/core/scene/scene.cpp` (world-transform composition), and `src/core/camera/camera.cpp`
(the camera basis). `scripts/check_invariants.py` holds that list, and adding to it is a
deliberate edit rather than something a build failure pressures you into. `src/core/physics/`
and `src/core/audio/` are the same shape for their own libraries: `BodyHandle` and
`SoundHandle`, `PhysicsWorld` and `AudioEngine`, with every `JPH::` type quarantined in
`physics.cpp` and every `ma_*` type in `audio.cpp` and `miniaudio_impl.cpp`.

The invariant is that `src/main.cpp`, `src/app/`, `src/ui/`, and every header under `src/` stay
free of all three libraries. It is not that a single file owns all Diligent. A backend swap or
a console port then rewrites those quarantined TUs instead of the whole codebase.

Do not enumerate the seam's API here: it is around forty entry points now, covering the shadow
cascade pass, glTF model loading and animation, textures, sprites, screen-space UI vertices,
post parameters, and PNG capture. `renderer.h` is the list, and a copy of it in prose would be
wrong within a month.

The checker works on comment-stripped source, because it has to. The word "Diligent" appears
seventeen times across `src/main.cpp`, `src/app/`, and `src/ui/`, every one of them in a
comment explaining the boundary. A checker that cannot tell a comment from code is one nobody
leaves switched on. It allow-lists includes rather than deny-listing: Diligent's headers are bare
quoted names with no shared prefix (`BasicMath.hpp`, `RenderDevice.h`,
`Shaders/Common/public/BasicStructures.fxh`), so a deny-list would only ever catch the ones
someone remembered to list, while an allow-list of project roots (`core/`, `app/`, `ui/`) plus
the named ImGui exemptions fails closed on a header nobody has seen before.

### Dear ImGui Is Exempt

Dear ImGui is a plain UI library, so engine and game code may include `imgui.h` and call
`ImGui::` directly, as `src/ui/panels/*.cpp` does. The exemption covers `imgui.h`,
`imgui_internal.h`, `ImGuizmo.h`, `IconsFontAwesome6.h`, and the GLFW platform backend. It does
not cover `ImGuiImplDiligent.hpp`, Diligent's ImGui renderer backend, which stays in
`renderer.cpp` like everything else.

### The Toon Draw

`DrawMesh` (`src/core/rendering/renderer.cpp:3310`) runs two passes over one mesh sharing a
dynamic constant buffer: the outline pass, then the fill pass. The outline pass draws the same
mesh enlarged, by pushing every vertex outward along its normal, and keeps only the back faces.
That leaves a slightly oversized shell of the object. The fill pass then draws the real
surface, which is nearer the camera and so overwrites the shell everywhere except the rim
peeking out around the silhouette. That rim is the outline. The matrix-convention, winding, and
outline-ordering details that make it work are in [MEMORY.md](../MEMORY.md).

## 3. HLSL Only

All shaders are HLSL, cross-compiled to SPIR-V by Diligent at runtime. Sources live in
`assets/shaders/`: sixteen files today, `.hlsl` for entry points and `.hlsli` for shared
includes. The checker holds both that directory to those two suffixes and the rest of the tree
to no GLSL, SPIR-V, Metal, or WGSL sources at all.

## 4. Disable Unused Diligent Backends and Modules

Diligent builds every supported backend by default, which dominates compile time. Set the
`DILIGENT_NO_*` options as `CACHE BOOL "" FORCE` before
`add_subdirectory(external/DiligentCore)`. Both halves of that matter. A submodule declares its
own options with CMake's `option()`, which does nothing if the variable is already in the
cache, so a plain `set()` loses and `FORCE` wins; and a cache variable read during
`add_subdirectory` has to be set before that line, not after.
`CMakeLists.txt:79-88` sets them, `CMakeLists.txt:101` adds the subdirectory, and the checker
asserts both the form and the ordering.

`DILIGENT_NO_RADIENT` is also set. That is DiligentFX's GI module: it is unused here, and it
fails to compile under clang-cl. A full `cmake --build` or a CI run hits it even though CLion's
incremental build of the `ToonEngine` target alone does not.

D3D11, D3D12, and OpenGL are disabled. Re-enable one only for a concrete reason, such as D3D12
for RenderDoc or PIX capture; the checker fails if one silently disappears, which is the point.
The same "disable what we do not use" pattern is applied to Jolt's GPU compute backends
(`CMakeLists.txt:130-133`), GLFW's docs, tests, and examples, and efsw's demo app, but only the
Diligent options are checked, because only those are what this rule names.

## 5. Toolchain and Language

C++17, with C enabled because DiligentTools pulls in zlib and libpng for its texture loaders.
Dependencies are git submodules under `external/`, ten of them; there is no vcpkg.

Every preset in `CMakePresets.json` sets `clang-cl` for both languages. That is where the
compiler choice lives, and nothing asserts it: a bare `cmake -S . -B out` with no preset picks
up MSVC and configures. Linux and macOS are planned and do not build yet
([architecture.md](architecture.md)), so "clang everywhere", meaning Apple Clang on macOS, is a
commitment about those ports rather than a description of a working build.

Prefer the target-scoped CMake commands. A directory-scoped command applies to every target
defined after it in that directory and below, which here means the entire vendored tree;
`target_include_directories` and `target_link_libraries` attach the same setting to one named
target instead. The checker rejects `include_directories`, `link_libraries`,
`link_directories`, and `add_definitions` outright.

`add_compile_options` is the one stated exception, and only inside `if(TOONENGINE_ASAN)`
(`CMakeLists.txt:56-71`). AddressSanitizer has to instrument the whole dependency tree
consistently, because a sanitizer runtime mismatch between TUs is the standard way this stops
catching anything while still appearing to work. Whole-tree reach is the requirement there, not
a shortcut. The checker allows `add_compile_options` inside that block and rejects it anywhere
else.

Windows builds need the Visual Studio Developer environment for the Windows SDK tools clang-cl
shells out to. Without it, configure fails at `CMAKE_MT-NOTFOUND`. That failure is loud and
immediate, so it needs no separate check. See
[clion-setup-windows.md](clion-setup-windows.md).

Diligent objects are COM-refcounted: each one counts how many references point at it and frees
itself when the count hits zero. `RefCntAutoPtr<>` is the smart pointer that does that counting
for you, so hold Diligent objects in one rather than a raw pointer. Nothing checks this; a
missed one shows up as a leak or a crash on shutdown.

## 6. Golden-Image Re-Baselining

`scripts/run_golden_tests.py` runs `ToonPlayer --headless-render --post off` against the three
scenes in `tests/scenes/golden_*.scene`, renders five frames and captures frame 4, then diffs
it against the checked-in `tests/golden/<name>/frame.png`. The metric is in
`scripts/golden_diff.py`: box-average both images down to a quarter resolution, convert to
luminance, and take RMSE against a 0.02 threshold. The downsampling absorbs sub-pixel
antialiasing jitter and encoder rounding while staying sensitive to a whole region changing.

The value of that test depends entirely on a red result meaning something. The moment a red
golden becomes "re-run `rebaseline.py`" instead of "explain what changed", it stops catching
regressions and starts ratifying them. A flipped shader constant, a silently disabled shadow
cascade, and a broken sprite blend order all look identical to "this diff was expected" from a
script's point of view.

`scripts/rebaseline.py` is the mechanical part: `--reason` is `required=True` in argparse
rather than a convention, a blank reason is rejected, and it stages the new baseline PNGs plus
that reason with `git add` without ever committing. The reason belongs in the commit message.
Nate looks at the diff image before a re-baseline merges, which is a review step no script
substitutes for.

## Code Style

`docs/cpp-style-guide.md` carries the house C++ style, including the section 7 audit that
prefers plain structs and free functions over classes with private members. A class earns its
encapsulation two ways only: it quarantines a genuine external dependency, as `Renderer`,
`PhysicsWorld`, and `AudioEngine` do, or it removes real repeated boilerplate. Tidiness is not
one of them.

`cpp-style-guide.md`, `md-style-guide.md`, `CLAUDE.md`, `MEMORY.md`, and `ARCHIVE.md` are
local-only by design: symlinks into the `backup` worktree, gitignored on every branch, never
pushed. Links to them from this file resolve on a working checkout and not in a fresh clone.
