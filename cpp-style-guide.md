<!-- tidy-md:locked — hand-authored house style; revise deliberately, not via routine tidying -->

# ToonEngine C++ Style Guide

House style for **our** code under `src/` (and any future engine modules). External
submodules under `external/` are off-limits — never reformat Diligent, GLFW, or
ImGui.

Two layers work together:

1. **Layout** — indentation, braces, column budget (§1). Held by hand, matching the
   file you're editing. There is deliberately no `.clang-format` in this repo; §1 says
   why.
2. **Everything a formatter couldn't decide anyway** — file structure, comment intent,
   naming, and the architectural rules that keep the codebase approachable (§2 onward).

When you clean a file, apply both.

The goal above all: **a newcomer should be able to open any file and understand what
it does and why, without reading the whole engine.** Optimize for the reader.

---

## 1. Layout

Nothing enforces these — match the file you're editing. Listed so you know what's
intentional and won't "fix" it by hand:

| Rule | Value |
|------|-------|
| Indent | 4 spaces, never tabs |
| Column budget | 120, as a target rather than a hard stop (31 lines under `src/` exceed it) |
| Braces | attached (K&R): `void f() {` |
| Braces required | always, including single-statement bodies |
| Tiny guards | a short `if (!p) { return; }` may stay on one line |
| Namespaces | indented, closing brace labelled `} // namespace toon` |
| `public:`/`private:` | flush with `class` |
| Pointers | bind right: `int *p` |
| Includes | **never reordered** — order-sensitive headers are safe |
| Consecutive-assignment alignment | by hand where it helps (see §3) |

### Why there's no `.clang-format`

`src/` was written to the table above by hand, and hand-written code and a formatter
config drift apart even when they agree on paper. A `.clang-format` reconstructed from
this table, run over `src/`, rewrites 4,375 of 16,554 lines across 63 of 110 files
(measured with clang-format 22.1.3). Checking one in means either landing that reformat
as its own commit first, or handing whoever next presses *Reformat Code* a 4,000-line
diff on top of their actual change.

Adopting one later is a reasonable call. It just has to be a deliberate commit that does
nothing else, not something discovered by accident. CLion's "no `.clang-format` found"
prompt has already been dismissed for this project (`.idea/workspace.xml`), so its
*Reformat Code* uses the IDE's own scheme, which is not this table either. Prefer hand
layout over reaching for the IDE shortcut.

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

## 3. Spacing & alignment

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

  Nothing will add or strip this for you (§1), so alignment is a deliberate authoring
  choice. Align only within a tight, related block; don't align across unrelated
  statements, and don't chase alignment so hard it hurts the diff.
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
