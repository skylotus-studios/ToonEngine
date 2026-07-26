# ToonEngine

From-scratch, cross-platform game engine focused on stylized toon rendering, built on Diligent
Engine (Vulkan-only), GLFW for windowing, and Dear ImGui for the debug and editor UI via
DiligentTools. The app today is a working editor: cel-shaded scene, cascaded shadows, a
DiligentFX post chain, Jolt physics, miniaudio, and a docked ImGui UI.

This file is a router. It carries the commands, the hard rules, and pointers. Everything else
lives in the files below.

## Build and Verify

Windows builds require the Visual Studio Developer environment. In CLion a Visual Studio
toolchain supplies it automatically; from a terminal, open a Developer PowerShell for VS 2022.
Without it, configure fails at `CMAKE_MT-NOTFOUND`.

First clone only:

```
git submodule update --init --recursive
git lfs install && git lfs pull
```

Build and run:

```
cmake --preset windows-debug
cmake --build --preset windows-debug
./build/windows-debug/ToonEngine.exe
```

Presets are `windows-debug` and `windows-release`, building into `build/<preset>/` with the
engine DLLs copied next to the executable. The `/verify` skill builds, launches, and captures a
screenshot; this machine has no live input desktop.

## The Five Hard Rules

Full reasoning for each is in [docs/invariants.md](docs/invariants.md). Break one only with a
stated reason, and update that file when you do.

1. **Build on Diligent, don't reinvent it.** Use Diligent's loaders, post-processing, ImGui
   integration, shader cross-compilation, and math utilities. The thin layer ToonEngine adds
   exists to remove real boilerplate and hold the portability boundary, nothing else. Never
   wrap a Diligent call one-to-one just to hide it.
2. **The app layer stays Diligent-free.** `main.cpp`, `src/app/`, `ui/panels/`, and every
   public header use opaque handles and stay backend-agnostic; `Diligent::` types live in
   implementation TUs. `core/physics/` quarantines Jolt the same way. Dear ImGui is exempt:
   call `ImGui::` directly anywhere.
3. **HLSL only**, cross-compiled to SPIR-V by Diligent at runtime.
4. **Disable unused Diligent backends and modules.** Set `DILIGENT_NO_*` as
   `CACHE BOOL ... FORCE` before `add_subdirectory(DiligentCore)`. `DILIGENT_NO_RADIENT` is
   required: it does not compile under clang-cl.
5. **C++17, clang everywhere, target-based CMake only.** Dependencies are git submodules under
   `external/`; no vcpkg. Hold Diligent objects in `RefCntAutoPtr<>`.

## Where Things Live

| Topic | File                                                       |
|---|------------------------------------------------------------|
| Architecture, current state, source layout, platform support | [docs/architecture.md](docs/architecture.md)               |
| The five rules, in full, with reasoning | [docs/invariants.md](docs/invariants.md)                   |
| Build-time and frame-time cost centres | [docs/performance.md](docs/performance.md)                 |
| Architectural decision records | [docs/decisions/](docs/decisions/)                         |
| Per-feature designs, written before implementation | [docs/specs/](docs/specs/)                                 |
| What to work on next, shipped to not-yet-started | [docs/roadmap.md](docs/roadmap.md)                         |
| C++ house style, including the plain-data audit | `docs/cpp-style-guide.md`                                  |
| Prose style for every doc here | `docs/md-style-guide.md`                                   |
| CLion toolchain and preset setup | [docs/clion-setup-windows.md](docs/clion-setup-windows.md) |

## Past Decisions and History

For past decisions and history, read [MEMORY.md](MEMORY.md) or [ARCHIVE.md](ARCHIVE.md) on
demand. Neither is auto-loaded, and neither should be: together they are roughly 20 times the
size of this file.

## Local-Only Files

This file, `MEMORY.md`, `ARCHIVE.md`, both style guides, and `.claude/skills` and
`.claude/agents` are gitignored on `develop` and `main`, and live on the orphan `backup` branch
(`claude.md`, `memory.md`, `archive.md`, `cpp-style-guide.md`, `md-style-guide.md`,
`project/skills`, `project/agents`), checked out as the sibling worktree `../backup` and
symlinked into place. None of this is pushed from `develop` or `main`; editing any of it changes
the `backup` branch. `bootstrap.sh` on `backup` rebuilds this whole layout (both worktrees,
submodules, and every symlink) from a single clone of that branch. Your global Claude Code
config (`~/.claude`) is backed up separately in the `ClaudeUserBackup` repo, unrelated to this
branch.

## Skills

`/tidy-cpp` cleans `src/**` to the C++ style guide. `/tidy-md` keeps these docs accurate and
right-sized. `/verify` builds, launches, and screenshots. `/plan-roadmap` designs the next
roadmap item; `/update-roadmap` reprioritises the list.