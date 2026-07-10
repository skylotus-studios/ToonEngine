# Building ToonEngine in CLion (Linux)

> **Status: planned, not yet built out.** ToonEngine's active development target is
> Windows (see [CLAUDE.md](../CLAUDE.md) → *Platform support*). GLFW's X11 backend is
> wired up in `renderer.cpp`, but Linux hasn't been built or run yet, and
> `CMakePresets.json` has no `linux-debug` / `linux-release` presets — only
> `windows-debug` / `windows-release`. This doc is the setup CLion will need once
> that lands; treat it as a starting point, not a verified recipe.

The build uses **Ninja + Clang**, targeting the Vulkan backend (same as Windows —
D3D11/D3D12/OpenGL are compiled out project-wide in `CMakeLists.txt`).

## Prerequisites

- **Clang** (via your distro's package manager, e.g. `apt install clang lld`).
- **Vulkan SDK** — loader + validation layers + `glslang`/`dxc` as needed by
  Diligent. LunarG's SDK or your distro's `vulkan-sdk` / `vulkan-tools` +
  `vulkan-validationlayers-dev` packages.
- **X11 development headers** — GLFW's Linux backend needs
  `libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev` (Debian/Ubuntu
  package names; adjust for your distro). Wayland is not wired up yet even though
  some fields exist in the windowing code.
- **Ninja** and **CMake ≥ 3.20** (CLion bundles both, but a system CMake ≥ 3.20 is
  needed if you ever configure outside CLion).
- **CLion 2023.1 or newer.**

## 1. Clone with submodules

```
git submodule update --init --recursive
```

## 2. Toolchain

**Settings → Build, Execution, Deployment → Toolchains.** On Linux CLion's bundled
*System* toolchain (GCC or Clang, whichever `cc`/`c++` resolve to) is normally
sufficient — no VS-style environment bootstrap is needed here, unlike Windows'
clang-cl setup. Set **C Compiler** / **C++ Compiler** to `clang` / `clang++`
explicitly if you want to match the Windows clang toolchain rather than system GCC.

## 3. CMake presets

There's no `linux-debug` / `linux-release` preset in `CMakePresets.json` yet. Until
one's added, configure a plain CMake profile in **Settings → Build, Execution,
Deployment → CMake**: Debug/RelWithDebInfo build type, Ninja generator (CLion
defaults to it), and the Clang toolchain from step 2 — no other flags needed, since
`CMakeLists.txt` already trims Diligent to the Vulkan backend for every platform.

When Linux presets are added, follow the same shape as `windows-debug` in
`CMakePresets.json`: a hidden `linux-base` inheriting the generator/output-dir
settings, minus the clang-cl-specific compiler cache variables.

## 4. Run / Debug

Same as Windows: CLion auto-generates a run configuration for the `ToonEngine`
target. Set **Working directory** to the project root so the baked-in shader path
(`TOON_SHADERS_DIR`) resolves. Debug via CLion's bundled LLDB or GDB.

## Troubleshooting

- **Vulkan validation layer / loader not found** — confirm `VK_LAYER_PATH` /
  `VULKAN_SDK` env vars are set if you installed the SDK outside your package
  manager; a distro-packaged SDK usually needs nothing extra.
- **X11 headers missing at configure/link** — install the `libx*-dev` packages
  listed above.
- **First configure is slow** — expected; it builds DiligentCore/Tools/FX from
  source, same as Windows. See [MEMORY.md](../MEMORY.md) → *Build gotchas*.
