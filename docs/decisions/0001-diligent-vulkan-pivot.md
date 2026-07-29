# 0001. Build on Diligent Engine With Vulkan

**Status:** Accepted
**Date:** 2026-07-06

## Context

ToonEngine began as a from-scratch OpenGL 4.1 renderer on the `main` branch, with dependencies
managed by vcpkg. Carrying a hand-written renderer meant re-implementing asset loading, shader
cross-compilation, post-processing, and an ImGui integration that mature libraries already
provide.

## Decision

Pivot to Diligent Engine (Core, Tools, and FX) with Vulkan as the only enabled backend, on the
`diligent` branch. Replace vcpkg with git submodules under `external/`. Keep `main` for
reference only.

Diligent's own implementations are used wherever they exist. ToonEngine adds a thin layer that
tames Diligent's boilerplate and holds the portability boundary, described in
[invariants.md](../invariants.md) rules 1 and 2.

## Consequences

Vulkan-only keeps build times down; D3D11, D3D12, and OpenGL are disabled in `CMakeLists.txt`
and re-enabled only for a concrete reason such as a D3D12 build for RenderDoc or PIX.

Diligent builds every supported backend by default, so the `DILIGENT_NO_*` cache options
became load-bearing rather than an optimisation. `DILIGENT_NO_RADIENT` is required because
DiligentFX's GI module does not compile under clang-cl.

Git submodules require a recursive clone, and DiligentTools has nested submodules of its own.

Linux and macOS remain reachable through the same Vulkan path, macOS via MoltenVK.

The full record of the errors and dead ends behind this pivot is in
[MEMORY.md](../../MEMORY.md) under the build and stack sections; read it on demand.