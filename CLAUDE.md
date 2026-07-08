# ToonEngine

From-scratch, cross-platform game engine focused on stylized / toon rendering,
built on **Diligent Engine** (rendering) + **GLFW** (windowing). **Vulkan-first**;
D3D11/D3D12/OpenGL backends come free with Diligent and can be enabled later.

> **History:** ToonEngine began as a from-scratch OpenGL 4.1 renderer. It has
> since pivoted to Diligent Engine on the `diligent` branch. The old GL engine
> (custom renderer/mesh/scene/animation code, glTF+FBX loaders, CSM, toon
> shaders) lives in the `main` branch's history — mine it for reference, but the
> `diligent` branch is a fresh start at "first light."

## Status: first light ✅

The app opens a window and clears the screen every frame via a Vulkan device +
swap chain. **No pipelines, shaders, or draw calls yet.** It proves the
toolchain, window, and swap chain work end-to-end.

The rendering already sits behind the **renderer seam** (roadmap #2, done):
`src/main.cpp` drives a `toon::Renderer` and includes **no Diligent header at
all** — every Diligent/Vulkan call lives in `src/core/renderer.cpp`. See
*Conventions*.

**Verified building and running** (Windows / clang-cl / Vulkan): links to
`ToonEngine.exe`, `copy_required_dlls` drops the engine DLLs beside it, and it
reaches first light on-device (selects the physical GPU, creates the swap chain,
runs the clear loop, no errors). Two things beyond the raw skeleton were needed to
link, and are already in `CMakeLists.txt`:

- link **`Diligent-Common`** — its `Common/interface` is where `RefCntAutoPtr.hpp`
  lives, and the Vulkan engine target doesn't propagate that include dir; and
- define **`ENGINE_DLL=1`** — so the headers use the runtime DLL-load path
  (`LoadGraphicsEngineVk`) that matches linking the `-shared` engine.

If a Diligent symbol fails to resolve as you grow `main.cpp`, `Tutorial00_HelloTriangle`
and `GLFWDemo` are the authoritative references.

## Build

Toolchain: **CMake ≥ 3.20 · Ninja · clang-cl (LLVM) · Visual Studio 2022** (for the
Windows SDK + MSVC CRT/import libs that clang-cl targets). C++17.

Dependencies are **git submodules** built via `add_subdirectory` — **no vcpkg, no
package manager.** Clone with submodules (or init them after):

```
git submodule update --init --recursive
```

### The one gotcha: build inside a VS Developer environment

Ninja + clang-cl needs the Windows SDK tools on `PATH` (`mt.exe`, `rc.exe`) and
the MSVC libs. A plain terminal **does not** have these — configure fails at
`CMAKE_MT-NOTFOUND`. Two ways to get the environment:

- **VS Code (recommended):** `Ctrl+Shift+B` to build, `F5` to build+debug. The
  tasks in `.vscode/tasks.json` bootstrap the VS Dev environment automatically —
  they dot-source `scripts/vsenv.ps1`, which imports it via `vswhere` +
  `VsDevCmd.bat` — so no manual setup is needed. (Not `Launch-VsDevShell.ps1`:
  its `-DevCmdArguments` parameter doesn't exist on all VS builds.)
- **Command line:** open the bundled **"VS Dev PowerShell"** terminal profile
  (in `.vscode/settings.json`), run from a **"Developer PowerShell for VS 2022"**,
  or dot-source the importer yourself (`. .\scripts\vsenv.ps1`), then:

```
cmake --preset windows-debug        # configure (Ninja + clang-cl, Vulkan)
cmake --build --preset windows-debug
./build/windows-debug/ToonEngine.exe
```

Presets: `windows-debug` (Debug) and `windows-release` (RelWithDebInfo). Each
builds into `build/<preset>/` with the engine DLLs copied next to the exe by
Diligent's `copy_required_dlls`.

## Source layout

```
src/
  main.cpp                Entry point: GLFW window + game loop; drives Renderer (no Diligent headers)
  core/
    renderer.h            The seam: opaque handles + PIMPL Renderer (backend-agnostic public API)
    renderer.cpp          Diligent Engine (Vulkan) implementation — ALL Diligent code lives here
external/                 Git submodules (not committed as files; see .gitmodules)
  DiligentCore/           Rendering: RHI + D3D11/D3D12/GL/Vulkan backends, HLSL->SPIR-V
  glfw/                   Cross-platform window + input
CMakeLists.txt            add_subdirectory(DiligentCore, glfw); links Vk-shared + Diligent-Common
CMakePresets.json         windows-debug / windows-release (Ninja + clang-cl)
scripts/
  vsenv.ps1               Imports the VS Developer env (vswhere + VsDevCmd.bat); tasks dot-source it
.vscode/
  tasks.json              Build/Configure tasks (self-bootstrap the VS Dev env)
  launch.json             Debug/Release launch (cppvsdbg)
  settings.json           CMake-presets flow + "VS Dev PowerShell" terminal profile
  c_cpp_properties.json   IntelliSense include paths for Diligent's bare-name headers
```

## Rendering: the seam + how Diligent is wired

All of the below lives in `core/renderer.cpp` behind the seam; `main.cpp` only
calls `Renderer::Init / BeginFrame / EndFrame / Resize`.

1. **GLFW** creates the window with `GLFW_NO_API` (no GL context — Vulkan owns the surface).
2. `MakeNativeWindow()` fills Diligent's `NativeWindow` from the GLFW native handle,
   per platform: Win32 `hWnd` · Linux `WindowId` + `pDisplay` (X11) · macOS `pNSView`
   (needs the Cocoa `NSView`, a few lines of Objective-C++ from GLFWDemo).
3. `EngineFactoryVk` creates the `IRenderDevice` + `IDeviceContext`, then the `ISwapChain`.
4. Per frame: bind the back-buffer RTV/DSV, clear, (future) draw, `Present()`.

Shaders will be authored in **HLSL** and cross-compiled to SPIR-V by Diligent's
shader system — write once, run on every backend.

## Conventions

- **Diligent stays behind the renderer seam** — the load-bearing rule, now
  enforced. `core/renderer.h` exposes only opaque handles (`TextureHandle` /
  `BufferHandle` / `ShaderHandle` / `PipelineHandle`) and a PIMPL `Renderer`;
  **all** Diligent headers and `Diligent::` types are confined to
  `core/renderer.cpp`. No other translation unit may include a Diligent header.
  A future backend (or console port) is then a new `renderer_*.cpp`, not a
  rewrite — keep it that way as the engine grows.
- **HLSL** for all shaders (Diligent cross-compiles to SPIR-V).
- Diligent objects use COM-style refcounting — hold them in `RefCntAutoPtr<>`.
- Diligent types live in `namespace Diligent`.
- Target-based CMake only (`target_*`), consistent with the original engine.
- Add **DiligentTools** (Dear ImGui, texture/glTF loaders) and **DiligentFX**
  (PBR, post-processing) as submodules + `add_subdirectory` when needed — the
  hooks are already stubbed (commented) in `CMakeLists.txt`.

## Platform support

| Platform | Status  | Backend                              | Notes |
|----------|---------|--------------------------------------|-------|
| Windows  | active  | Vulkan (D3D12/D3D11/GL also linked)  | primary dev target |
| Linux    | planned | Vulkan                               | X11 handles wired; Wayland fields exist |
| macOS    | planned | Vulkan via **MoltenVK**              | needs `NSView` from GLFW Cocoa (`.mm` helper) |

## Roadmap / next steps

1. **First light — compile & run.** ✅ Done — cleared window on Vulkan (RTX 3080).
2. **Renderer interface seam.** ✅ Done — `core/renderer.{h,cpp}`: opaque handles
   + PIMPL `Renderer`, `main.cpp` is Diligent-free. Resource-creation APIs
   (buffers / textures / pipelines) fill in as the steps below need them.
3. **DiligentTools (next up).** Add Dear ImGui (editor / debug UI) and the
   texture / glTF loaders when you start pulling in assets — behind the seam too.
4. **First real shaders — the toon pipeline** (HLSL → SPIR-V):
   - **Fill pass:** banded / ramp lighting instead of smooth N·L.
   - **Outline pass:** inverted-hull (render back-faces pushed out along normals
     in a flat color) **or** a post-process depth + normal edge detect.
5. **DiligentFX (later).** Pull in if the toon look wants bloom, SSAO, or tone mapping.
6. **Cross-platform.** Bring up Linux (Vulkan), then macOS (MoltenVK — wire the
   `NSView` Cocoa helper).
7. **Other backends (optional).** D3D11/D3D12 are already linked on Windows; enable
   if useful for debugging (RenderDoc/PIX) or a Windows-native path.

## Constraints

- **C++17**, **clang everywhere** (clang-cl on Windows, Apple Clang on macOS).
- **Windows builds require a VS Developer environment** (Windows SDK on PATH).
- Dependencies are **submodules**, not vcpkg. Single-header libs go under a
  future `libs/` if needed.
- Keep Diligent contained behind the renderer seam (`core/renderer.cpp`) — no
  Diligent headers in any other translation unit.
