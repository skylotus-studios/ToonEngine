# Building ToonEngine in CLion (Windows)

ToonEngine is a **CLion project**. This is the one-time setup for the toolchain,
CMake presets, and run/debug. After it, `Ctrl+F9` builds and `Shift+F9` debugs.

The build uses **Ninja + clang-cl**, which target the MSVC ABI and therefore need
the **Visual Studio Developer environment** on `PATH`: the Windows SDK's `mt.exe` /
`rc.exe` and the MSVC CRT/import libs. CLion's **Visual Studio toolchain** sets that
environment up automatically for every configure/build/run, so (unlike a plain shell, which
needs it imported manually; see MEMORY.md → *Build gotchas*) you never have to think about
it here.

## Prerequisites

- **Visual Studio 2022** with the *Desktop development with C++* workload, providing
  the Windows SDK + MSVC CRT/import libraries clang-cl links against. You do **not**
  build with MSVC's `cl.exe`; only its environment + libraries are used.
- **LLVM / Clang** providing `clang-cl`: either the standalone LLVM (default
  `C:\Program Files\LLVM\bin`) or VS's *C++ Clang tools for Windows* component.
  Make sure `clang-cl` is on `PATH` (or note its full path for the toolchain).
- **CLion 2023.1 or newer** (native `CMakePresets.json` support). CLion bundles
  CMake and Ninja, so nothing extra to install.

## 1. Clone with Submodules

Dependencies are git submodules (DiligentTools has nested ones):

```
git submodule update --init --recursive
```

## 2. Create a Visual Studio Toolchain

**Settings → Build, Execution, Deployment → Toolchains → `+` → Visual Studio.**

| Field                     | Value                                                             |
|---------------------------|-------------------------------------------------------------------|
| Name                      | e.g. `VS2022 clang-cl`                                             |
| Visual Studio             | your VS 2022 install (CLion auto-detects it)                      |
| Architecture              | `amd64`                                                           |
| C Compiler / C++ Compiler | `clang-cl` (full path if not on `PATH`, e.g. `C:\Program Files\LLVM\bin\clang-cl.exe`) |
| Debugger                  | *Bundled LLDB* (default; it reads the PDBs clang-cl emits)         |

CLion runs `VsDevCmd` internally for this toolchain, so the SDK tools and MSVC libs are
always present without any manual environment setup. (The compiler is also driven by the
preset in the next step; setting it here too is belt-and-braces and lets CLion resolve the
toolchain correctly.)

## 3. Enable the CMake Presets

The repo ships `CMakePresets.json` with two configure presets: **`windows-debug`**
(Debug) and **`windows-release`** (RelWithDebInfo), both Ninja + clang-cl, building
into `build/<preset>/`.

**Settings → Build, Execution, Deployment → CMake.** CLion lists the presets; enable
`windows-debug` (and `windows-release` if you want it). For each enabled profile make
sure its **Toolchain** is the *Visual Studio* toolchain from step 2. CLion normally
auto-selects a compatible one, but if it picked another (MinGW/default), switch it or
configure fails with `CMAKE_MT-NOTFOUND`. Apply.

CLion reconfigures on Apply. The **first configure builds DiligentCore / Tools / FX
and can take several minutes**; later incremental builds are seconds. If a configure
was already attempted with the wrong toolchain, run **Tools → CMake → Reset Cache and
Reload Project** to clear a stale `CMAKE_MT-NOTFOUND`.

## 4. Run / Debug

CLion auto-generates a run configuration for the `ToonEngine` executable target.

- Set **Working directory** to the project root (`$ProjectFileDir$`). Shaders load
  via an absolute path baked in by CMake and the engine DLLs are copied next to the
  exe, so it runs from anywhere. Project root is just the sensible default.
- **Run** (`Shift+F10`) opens the window; **Debug** (`Shift+F9`) stops on breakpoints
  via the bundled LLDB reading clang-cl's PDBs.

## 5. Code Style

CLion auto-detects the repo-root `.clang-format`. If it doesn't prompt on open,
enable **Settings → Editor → Code Style → Enable ClangFormat**. *Reformat Code*
(`Ctrl+Alt+L`) then follows the house style.

## Troubleshooting

- **`CMAKE_MT-NOTFOUND` at configure**: the VS Developer environment isn't active.
  Confirm the CMake profile uses the *Visual Studio* toolchain (step 2). See
  [MEMORY.md](../MEMORY.md) → *Build gotchas*.
- **`clang-cl` not found**: add LLVM's `bin` to `PATH`, or set the full path in the
  toolchain's compiler fields.
- **Changed a toolchain/preset and configure is stuck**: *Tools → CMake → Reset
  Cache and Reload Project*. Avoid deleting all of `build/` unless necessary; a full
  wipe rebuilds Diligent from scratch (minutes). See MEMORY.md.

> `.idea/` (CLion's per-user project state) is git-ignored, and toolchains are a
> global CLion setting rather than project state, so each machine does step 2 once.
