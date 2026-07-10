# Building ToonEngine in CLion (macOS)

> **Status: planned, not yet built out.** ToonEngine's active development target is
> Windows (see [CLAUDE.md](../CLAUDE.md) → *Platform support*). macOS needs Vulkan via
> **MoltenVK** plus a GLFW Cocoa `.mm` helper to hand Diligent an `NSView` — that
> helper doesn't exist in the repo yet, so the engine won't run on macOS as-is.
> `CMakePresets.json` also has no `macos-debug` / `macos-release` presets. This doc
> covers the CLion/toolchain side so setup is ready once the Cocoa helper lands;
> treat it as a starting point, not a verified recipe.

## Prerequisites

- **Xcode Command Line Tools** (`xcode-select --install`) — provides Apple Clang and
  the macOS SDK.
- **MoltenVK** — either via the Vulkan SDK for macOS (LunarG) or `brew install
  molten-vk`. Diligent's Vulkan backend runs on top of it on macOS.
- **Ninja** and **CMake ≥ 3.20** (CLion bundles both; `brew install ninja cmake` if
  you need them outside CLion).
- **CLion 2023.1 or newer.**

## 1. Clone with submodules

```
git submodule update --init --recursive
```

## 2. Toolchain

**Settings → Build, Execution, Deployment → Toolchains.** CLion's bundled *System*
toolchain picks up Apple Clang from the Command Line Tools automatically — no extra
configuration needed here (unlike Windows, there's no separate VS-environment
bootstrap step).

## 3. CMake presets

There's no `macos-debug` / `macos-release` preset in `CMakePresets.json` yet. Until
one's added, configure a plain CMake profile in **Settings → Build, Execution,
Deployment → CMake**: Debug/RelWithDebInfo build type, Ninja generator, Apple Clang
toolchain from step 2. `CMakeLists.txt` already trims Diligent down to the Vulkan
backend, which on macOS resolves through MoltenVK — no extra cache variables needed
for that part.

The **outstanding blocker** is windowing: GLFW gives you an `NSWindow`, but Diligent's
Vulkan swap chain needs the `NSView`/`CAMetalLayer` underneath it, which requires a
small Objective-C++ (`.mm`) helper that isn't in `src/` yet. Until that's written and
wired into `CMakeLists.txt` (`add_executable` sources + `PLATFORM_MACOS` define via
`Diligent-BuildSettings`, which already propagates it), the app won't get past window
creation on macOS.

## 4. Run / Debug

Once the app builds and runs, this matches the other platforms: CLion auto-generates
a run configuration for `ToonEngine`, working directory at the project root so
`TOON_SHADERS_DIR` resolves, debug via CLion's bundled LLDB.

## Troubleshooting

- **MoltenVK not found at configure/link** — confirm `brew --prefix molten-vk` (or
  the Vulkan SDK's install path) is discoverable; you may need to point CMake at it
  via `CMAKE_PREFIX_PATH` until a preset codifies this.
- **Window opens but nothing renders / crashes on swap chain creation** — expected
  until the Cocoa `NSView` helper (see step 3) is written; this is the known
  cross-platform gap, not a local misconfiguration.
- **First configure is slow** — expected; it builds DiligentCore/Tools/FX from
  source, same as Windows/Linux. See [MEMORY.md](../MEMORY.md) → *Build gotchas*.
