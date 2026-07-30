<!-- tidy-md:locked — hand-authored house style; revise deliberately, not via routine tidying -->

# ToonEngine C++ Style Guide

House style for **our** code under `src/` (and any future engine modules). External
submodules under `external/` are off-limits — never reformat Diligent, GLFW, or
ImGui.

Two layers work together:

1. **`.clang-format`** (repo root) mechanically enforces layout — indentation, braces,
   column limit. CLion applies it on *Reformat Code* (`Ctrl+Alt+L`). Read §1 before you
   run it over an existing file: the tree has drifted from it and a blind reformat will
   bury your change.
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
| Consecutive-assignment alignment | **off in the tool** (see §3) |

Requires clang-format 15+ (for `InsertBraces`). CLion's bundled one is fine.

`.clang-format` is gitignored on `develop` and `main` and symlinked in from the `backup`
branch (where it is named `clang-format`, no leading dot), like `CLAUDE.md` and this
file. `bootstrap.cmd` re-links it, along with `.clangd`. If it is missing, you are
looking at a checkout that has not been bootstrapped.

### Don't reformat a file you're only editing

`src/` and this config have drifted: running clang-format over every file under `src/`
rewrites 2,455 of 16,554 lines across 40 of 110 files (clang-format 22.1.3). Most of it
is trailing-comment alignment and rewrapping, not real style breaches — 31 lines exceed
the 120-column limit.

So reformat the lines you touched, not the file, and never the tree. Closing that gap is
worth doing as a commit that does nothing else, reviewed as a diff of its own; folded
into a feature change it just buries the change. Note also that CLion's "no
`.clang-format` found" prompt was dismissed for this project at some point
(`.idea/workspace.xml`), so check *Settings > Editor > Code Style > Enable ClangFormat*
is actually on before trusting `Ctrl+Alt+L` to apply this file rather than the IDE's own
scheme.

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

  Guard any such block with `// clang-format off` / `// clang-format on`. `§1`'s
  `AlignConsecutiveAssignments: None` does *not* mean "leave alignment alone" — it means
  one space around `=`, so running the formatter over the block above flattens it back
  to `cd.Name = ...`. Verified, not assumed. Without the guard, the alignment survives
  only until someone reformats.

  Align only within a tight, related block; don't align across unrelated statements,
  and don't chase alignment so hard it hurts the diff.
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
- **Point across the abstraction layer**: when a value has to match something elsewhere (a shader
  cbuffer, a winding order, a format), say so and name the other side.
- **No dead code, no commented-out code, no stale TODOs.** Delete it — git remembers.
  If a comment describes behavior that changed, fix the comment in the same edit.
- Use `//` for prose. Reserve a full banner/divider for files and sections only.

---

## 5. Naming

- **Types** `PascalCase` (`Renderer`, `MeshData`, `PostParams`).
- **Functions/methods** `PascalCase` (`CreateMesh`, `RunBloom`) — matches Diligent so
  the two don't clash visually across the abstraction layer.
- **Locals / parameters / struct data members** `camelCase` (`vertexCount`,
  `lightDir`). Plain data structs (`Vertex`, `Transform`) use bare `camelCase`
  fields.
- **Private class members** under data encapsulation are accessed through `m_impl->…`; the
  owning pointer is `m_impl`. Prefer keeping mutable state inside `Impl`.
- **Constants** `kCamelCase` (`kHDRFormat`, `kPi`), `static constexpr`.
- **Namespaces** short and lowercase (`toon`). Anonymous namespaces for file-local
  helpers, or `static` for a single function.

---

## 6. Language & architecture rules

These are correctness/architecture, not taste — don't "clean" them away:

- **C++17**, clang everywhere. No compiler-specific extensions.
- **The renderer's abstraction layer is load-bearing.** `core/renderer.h` exposes only opaque
  handles + plain types; **all** Diligent headers and `Diligent::` types stay in
  `core/renderer.cpp`. Dear ImGui is the one exemption (see CLAUDE.md). Never include
  a Diligent header outside the abstraction layer to "simplify" something.
- **Keep the public header Diligent-free**: forward-declare, use data encapsulation, and put new
  backend state inside `Renderer::Impl`.
- Diligent objects are COM-refcounted — hold them in `RefCntAutoPtr<>`; release in
  reverse dependency order, resources before the device.
- Prefer `const`, references over pointers where null isn't meaningful, and
  `static_cast` over C casts.
- Target-based CMake only (`target_*`); no globals.

---

## 7. Data-oriented design discipline

Grounded in Casey Muratori's "clean code, horrible performance" critique: most bad code
comes from glue and infrastructure, not bad algorithms, and reflexive "clean code" rules
(polymorphism over switch, hide every internal behind a getter, one tiny function per
step) optimize for unmeasurable ideals instead of anything checkable. Default to
organizing code around the operation and the concrete data it touches, not around a type
hierarchy — that's what makes a shared pattern (a lookup table, a merged switch) visible
in the first place.

- **Plain data + free functions is the default.** A new state/logic grouping is a
  `struct` with public fields plus free functions taking a reference to it (`Scene` /
  `core/scene/scene.cpp` is the house shape) — not a class with private members and
  methods.
- **A class earns its encapsulation two ways only**: it quarantines a genuine external
  dependency behind an opaque handle or a data-encapsulated type (`Renderer` hides
  Diligent, `PhysicsWorld` hides Jolt — see the repo MEMORY.md's "Architecture
  decisions"), or it removes real, repeated boilerplate. Never "because a class reads
  cleaner." Every class under `src/` should be able to name one of those two reasons in
  its lead comment; if it can't, it's a plain-struct-and-free-functions candidate.
- **Prefer switch/table dispatch over virtual dispatch for a small, fixed,
  compile-time-known set of cases**, especially in per-frame/per-object/per-vertex code,
  where an indirect call and the lost inlining cost real time. `core/physics/physics.cpp`'s
  `CreateBody` and `ColliderWireframe` both switch on `ColliderShape` rather than giving
  each shape its own `Collider` subclass; that's the house pattern for a closed enum.
  Reach for virtual dispatch only for a genuinely open-ended extension point: `Script`
  (`core/scene/script.h`) is the one in this codebase, because new gameplay behaviors are
  added by whoever's scripting the game, not the engine, and `OnUpdate` runs once per
  entity per frame, not inside a hot inner loop.
- **The same enum switched on in more than one place isn't automatically duplication.**
  `ColliderShape` is switched on in five places (Jolt shape construction, debug wireframe
  geometry, the inspector, serialization) because each does a different job with the same
  key type. Forcing those into one lookup table would couple physics, rendering, UI, and
  serialization for no gain. Only merge switches performing the identical operation twice
  — that's the duplication worth compressing into a table.
- **A getter/setter wrapping a plain field, with no invariant to enforce and nothing
  external to hide, is a §5 violation, not a style nit** — a plain data struct exposes
  bare fields.
- **Don't fragment a function into single-purpose pieces on reflex.** Splitting an
  operation into many tiny named steps can hide that two of them do the same thing to the
  same shape of data, which is the exact pattern a lookup table or a merged switch would
  otherwise expose. Size a function around one coherent operation; extract a helper
  because it's reused or because leaving it inline would obscure that operation's shape,
  not on a line-count reflex.

This is a set of checkable signals for *this* codebase, not a rule against classes or
virtual functions on principle. `PhysicsWorld`, `Renderer`, and `Script` all stay exactly
as they are — each already names its justification.

---

## 8. Cleanup checklist

When tidying a file (or running the `tidy-cpp` skill), walk this list:

1. Layout matches §1 and the rest of the file. Don't reach for *Reformat Code* to get
   there — its scheme isn't §1's, and it will churn lines you didn't touch.
2. Banner present and accurate; sections under correct dividers in lifecycle order.
3. Every function has a clear lead comment; comments say *why*; none are stale.
4. No dead/commented-out code, no leftover debug prints, no unused includes or
   locals.
5. Manual alignment only within related blocks; blank lines separate paragraphs.
6. Naming matches §5; new state lives in the right place (data encapsulation, not globals).
7. Data-oriented discipline (§7): every class names its justification; a small, fixed,
   compile-time-known dispatch prefers switch/table over virtual; no getter/setter
   wrapping a bare field with nothing to hide.
8. Abstraction layer intact — no Diligent leakage past `renderer.cpp`; header still compiles
   Diligent-free.
9. It still **builds and runs** (`cmake --build --preset windows-debug`, launch it) —
   a clean file that doesn't run is not clean.

Keep changes reviewable: tidy in focused passes, don't reorder a whole file and
change logic in the same commit.
