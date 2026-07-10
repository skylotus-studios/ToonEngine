# ToonEngine C++ Style Guide

House style for **our** code under `src/` (and any future engine modules). External
submodules under `external/` are off-limits — never reformat Diligent, GLFW, or
ImGui.

Two layers work together:

1. **`.clang-format`** (repo root) mechanically enforces layout — indentation,
   braces, column limit. CLion applies it on *Reformat Code* (`Ctrl+Alt+L`). Run it
   before committing; don't hand-fight it.
2. **This guide** covers what a formatter can't: file structure, comment intent,
   naming, and the architectural rules that keep the codebase approachable. When
   you clean a file, apply both.

The goal above all: **a newcomer should be able to open any file and understand what
it does and why, without reading the whole engine.** Optimize for the reader.

---

## 1. What `.clang-format` already handles

You don't need to think about these — the formatter does them. Listed so you know
what's intentional and won't "fix" it by hand:

| Rule | Value |
|------|-------|
| Base style | LLVM, C++ latest |
| Indent | 4 spaces, never tabs |
| Column limit | 120 |
| Braces | attached (K&R): `void f() {` |
| Braces required | always — the formatter inserts missing ones (`InsertBraces`) |
| Tiny guards | a short `if (!p) { return; }` may stay on one line |
| Namespaces | indented, closing brace labelled `} // namespace toon` |
| `public:`/`private:` | flush with `class` |
| Pointers | bind right: `int *p`, `const Vertex* v` |
| Includes | **never reordered** — order-sensitive headers are safe |
| Consecutive-assignment alignment | **off in the tool** (see §3 — we align by hand where it helps) |

Requires clang-format 15+ (for `InsertBraces`). CLion's bundled one is fine.

---

## 2. File structure

Every `.h`/`.cpp` opens with a **banner** naming the file and its one-paragraph
reason to exist — the elevator pitch a newcomer reads first:

```cpp
//============================================================================
//  core/primitives.cpp — procedural mesh generators.
//
//  Triangles are wound counter-clockwise as seen from OUTSIDE the surface ...
//============================================================================
```

Headers put `#pragma once` first, then the banner (see `core/math.h`).

Within a `.cpp`, group related functions under **section dividers** and keep the
groups in a sensible lifecycle order (setup → teardown → per-frame → …). Dividers
use the same form as the header's:

```cpp
// --- Per-frame lifecycle ----------------------------------------------------
```

`core/renderer.cpp` is the reference: *file-local helpers → construction →
targets/pipelines → teardown → per-frame → scene → UI*. When you add a function,
put it under the right divider rather than at the end.

Keep includes grouped and commented when non-obvious (own header first, then
platform/third-party, then std), and **never** sort them — a comment should explain
any order dependency.

---

## 3. Spacing & alignment (beyond the formatter)

- **One blank line** between functions and between logical paragraphs inside a
  function. No double blanks, no blank right after `{` or before `}`.
- **Manual column alignment is allowed and encouraged** for runs of related
  initializers or struct fields, because it makes divergence easy to scan:

  ```cpp
  cd.Name      = "HDR scene color";
  cd.Type      = RESOURCE_DIM_TEX_2D;
  cd.Format    = kHDRFormat;
  cd.BindFlags = BIND_RENDER_TARGET | BIND_SHADER_RESOURCE;
  ```

  The formatter is set to *leave alignment alone* (`AlignConsecutive* = None`), so it
  won't add or strip it — alignment is a deliberate authoring choice. Align only
  within a tight, related block; don't align across unrelated statements, and don't
  chase alignment so hard it hurts the diff.
- Let related one-liners share a shape (see the `Vec3` operators in `core/math.h`).

---

## 4. Comments — the onboarding layer

Comments are where we spend the newcomer's goodwill. Rules:

- **Say WHY, not WHAT the syntax already says.** `i++; // increment i` is noise.
  `// last column duplicates the first so a future UV seam wraps cleanly` earns its
  keep.
- **Every function gets a one-line lead comment** stating its job (and any
  precondition/ownership). Trivial getters can skip it.
- **Flag the non-obvious and the load-bearing**: winding/handedness, matrix
  conventions, init/teardown ordering, "this looks wrong but is deliberate," and any
  workaround for an external quirk. These are exactly what bites a newcomer. Prefer
  one clear sentence over a paragraph.
- **Point across the seam**: when a value has to match something elsewhere (a shader
  cbuffer, a winding order, a format), say so and name the other side.
- **No dead code, no commented-out code, no stale TODOs.** Delete it — git remembers.
  If a comment describes behavior that changed, fix the comment in the same edit.
- Use `//` for prose. Reserve a full banner/divider for files and sections only.

---

## 5. Naming

- **Types** `PascalCase` (`Renderer`, `MeshData`, `PostParams`).
- **Functions/methods** `PascalCase` (`CreateMesh`, `RunBloom`) — matches Diligent so
  the two don't clash visually at the seam.
- **Locals / parameters / struct data members** `camelCase` (`vertexCount`,
  `lightDir`). Plain data structs (`Vertex`, `Transform`) use bare `camelCase`
  fields.
- **Private class members** of the PIMPL etc. are accessed through `m_impl->…`; the
  owning pointer is `m_impl`. Prefer keeping mutable state inside `Impl`.
- **Constants** `kCamelCase` (`kHDRFormat`, `kPi`), `static constexpr`.
- **Namespaces** short and lowercase (`toon`). Anonymous namespaces for file-local
  helpers, or `static` for a single function.

---

## 6. Language & architecture rules

These are correctness/architecture, not taste — don't "clean" them away:

- **C++17**, clang everywhere. No compiler-specific extensions.
- **The renderer seam is load-bearing.** `core/renderer.h` exposes only opaque
  handles + plain types; **all** Diligent headers and `Diligent::` types stay in
  `core/renderer.cpp`. Dear ImGui is the one exemption (see CLAUDE.md). Never include
  a Diligent header outside the seam to "simplify" something.
- **Keep the public header Diligent-free**: forward-declare, use PIMPL, and put new
  backend state inside `Renderer::Impl`.
- Diligent objects are COM-refcounted — hold them in `RefCntAutoPtr<>`; release in
  reverse dependency order, resources before the device.
- Prefer `const`, references over pointers where null isn't meaningful, and
  `static_cast` over C casts.
- Target-based CMake only (`target_*`); no globals.

---

## 7. Cleanup checklist

When tidying a file (or running the `tidy-cpp` skill), walk this list:

1. Runs *Reformat Code* / clang-format cleanly, no manual layout fights left.
2. Banner present and accurate; sections under correct dividers in lifecycle order.
3. Every function has a clear lead comment; comments say *why*; none are stale.
4. No dead/commented-out code, no leftover debug prints, no unused includes or
   locals.
5. Manual alignment only within related blocks; blank lines separate paragraphs.
6. Naming matches §5; new state lives in the right place (PIMPL, not globals).
7. Seam intact — no Diligent leakage past `renderer.cpp`; header still compiles
   Diligent-free.
8. It still **builds and runs** (`cmake --build --preset windows-debug`, launch it) —
   a clean file that doesn't run is not clean.

Keep changes reviewable: tidy in focused passes, don't reorder a whole file and
change logic in the same commit.
