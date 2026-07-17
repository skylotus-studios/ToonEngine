---
name: tidy-cpp
description: Clean up ToonEngine's own C++ source (src/**) to the house style in docs/cpp-style-guide.md — formatting, section structure, comment clarity/onboarding, cruft removal, and a data-oriented/abstraction-discipline audit (unjustified encapsulation, virtual dispatch vs. switch/table dispatch, reflexive fragmentation) grounded in Casey Muratori's clean-code critique. Use when the user asks to tidy, clean up, neaten, reorganize, or improve the readability of ToonEngine source files. Mechanical passes apply directly and preserve behavior; architecture-discipline findings are reported, not silently applied.
---

# tidy-cpp — clean ToonEngine source to house style

Tidy **our** C++ under `src/**` to `docs/cpp-style-guide.md`. Read that guide first — it
is the source of truth; this skill is the procedure for applying it, phase by phase.

## Scope

- **In scope:** `src/**` only (`main.cpp`, `core/**`, `ui/**`).
- **Never touch** `external/**` (Diligent/GLFW/ImGui/Jolt submodules) or generated
  `build/**`. Never reformat a file we don't own.
- If the user named specific files, limit to those. Otherwise ask which files, or default
  to the ones changed on the current branch (`git diff --name-only main`) rather than
  churning the whole tree.
- **Mechanical vs. judgment.** Phases 1-5 and 7 below are mechanical and
  behavior-preserving — apply them directly. Phase 6 (data-oriented discipline) is mostly
  judgment calls surfaced as findings, with one narrow mechanical exception noted inline —
  don't silently change a class's shape or remove a virtual function without flagging it
  first. Same rule as always for an actual bug spotted along the way: surface it
  separately, don't fix it inside a cleanup pass.
- **`Renderer` and `PhysicsWorld` are the reference examples, not audit targets.** Both
  already name their justification (they quarantine Diligent/Jolt behind data
  encapsulation) and must never be touched by this skill — no header moves, no
  "simplifying" the data encapsulation away.

## Procedure

Work file by file. Within a file, walk every phase below in order before moving to the
next file — don't do every file's Phase 1 before any file's Phase 2. That way each file's
summary (Phase 8) is complete before you've context-switched away from it.

### Phase 1 — Format

Apply clang-format (repo `.clang-format`): `clang-format -i --style=file <file>` if it's
on PATH; otherwise hand-align to style guide §1. Don't fight the formatter afterward — if
a hand alignment (§3) keeps getting stripped, it wasn't the formatter's to strip in the
first place (check `AlignConsecutive*` is off in `.clang-format` before assuming a bug).

### Phase 2 — File & section structure

- Banner present (file path + one-paragraph reason to exist) and still accurate to what
  the file actually does.
- Functions grouped under `// --- Section ---` dividers in lifecycle order (setup →
  teardown → per-frame → ...); `core/rendering/renderer.cpp` is the reference order.
- A stray function moves under its correct divider. Don't reorder a whole file in the same
  pass as a logic-adjacent change — a reorder is its own diff, reviewed on its own.

### Phase 3 — Comments

- Every function has a one-line lead comment stating its job (trivial getters can skip
  it). Comments say *why* — winding/handedness, matrix/format conventions, init/teardown
  ordering, "looks wrong but is deliberate," external quirks — never *what* the syntax
  already says.
- Fix a stale comment in place (delete it and rewrite, don't patch around it).
- When a value has to match something elsewhere (a shader cbuffer, a winding order, a
  save-file format), the comment names the other side.

### Phase 4 — Cruft sweep

Grep before trusting your eyes — this codebase is currently clean on every point below, so
a hit is worth double-checking rather than assuming it's real:

- Search for `TODO`, `FIXME`, `HACK`, `XXX` — stale markers. Resolve or delete; git
  remembers.
- A line that's a `//` immediately followed by what reads as a statement (a stray `if (`,
  a trailing `;`) is commented-out code, not prose — delete it.
- `fprintf(stderr, ...)` / `printf(...)` that reports a real failure or user-facing status
  (`"Renderer: failed to create swap chain"`, `"Bindings saved: %s"`) is house style, not
  cruft — keep it. A leftover author-only scratch print (no clear audience, a raw pointer
  dump, a "here"/"test" message) is cruft — delete it.
- **Don't guess at unused includes/locals — let the compiler decide.** Phase 8's rebuild
  surfaces clang-cl's `-Wunused-variable` / `-Wunused-but-set-variable`. Before pulling a
  suspected include, grep the file for the types/macros it provides to sanity-check, then
  let the rebuild confirm you didn't actually need it.

### Phase 5 — Spacing, alignment, naming

- One blank line between functions and between logical paragraphs inside one; no double
  blanks, none right after `{` or before `}`.
- Manual column alignment only within a tight, related block (style guide §3) — don't
  chase it across unrelated statements or fight the diff to preserve it.
- Naming matches §5 (`PascalCase` types/functions, `camelCase` locals/params/plain-struct
  fields, `kCamelCase` constants). New mutable state lives in the right place — a
  data-encapsulated `Impl` for a justified class, a plain field for a plain struct — never
  a free-floating global.

### Phase 6 — Data-oriented discipline audit

The judgment-heavy pass, grounded in style guide §7 (read it for the full reasoning and
in-repo examples before running this phase the first time). Everything here is a finding
to report, not a change to make silently, with one exception called out below. Work
through:

1. **Every class in the file** — a `class`, or a `struct` whose `private:` section holds
   more than plain data (i.e. it has real methods, not just a constructor). Does its lead
   comment name a justification: a genuine external dependency it quarantines, or real
   repeated boilerplate it removes? If the justification is missing, or was true once but
   no longer is (the dependency it hid is gone), flag it — for example, *"`X` encapsulates
   `Y` with no stated reason; plain-struct-and-free-functions candidate per style guide
   §7."* Don't rewrite it. Report it.
2. **Every `virtual` function and `: public Base` hierarchy** in scope. Is the set of
   derived types small, fixed, and known at compile time, AND is the call site
   per-frame/per-object/per-vertex? If both, flag it as a switch/table-dispatch candidate
   and name the call site. If it's a genuinely open-ended extension point (new cases added
   outside the engine — `Script` is the house example) or isn't hot-path, leave it, and say
   why in the summary instead of staying silent. A considered "no, this virtual stays" is a
   finding too, not a non-event.
3. **A switch or if/else chain keyed on a type/enum that appears more than once in
   scope.** Grep that enum's name across `src/` to find every site. For each pair, ask: do
   they perform the identical operation (real duplication — flag as a table-merge
   candidate), or different operations that merely share a key type (leave them separate)?
   `ColliderShape`'s five independent switches — Jolt shape construction, debug wireframe
   geometry, the inspector, serialization — are the house precedent for the second case:
   don't merge them just because they share an enum. Only the first case is a finding.
4. **A getter/setter wrapping a bare field, with no invariant and nothing external to
   hide, on what is otherwise a plain data struct.** This one is mechanical, not a judgment
   call — it's a direct §5 violation, not a §7 gray area. Grep every call site of the
   accessor first; if all of them are in the files you're tidying this pass, inline it back
   to a bare field directly, same as any other Phase 1-5 fix. If a call site lives outside
   this pass's file set, flag it instead of leaving the struct half-converted.
5. **A chain of single-purpose helpers, each called from exactly one place**, split out of
   one logical operation for no reuse reason (grep the helper's name — a definition plus
   exactly one call site is the signal). Flag as an inline candidate. Don't merge it
   automatically in the same pass as something else in that function — inlining touches
   every call site's surrounding code and deserves its own reviewable diff.

### Phase 7 — Abstraction-layer boundary check

Mechanical again: search for `Diligent::` across `src/**/*.h` and confirm every hit lives
inside `core/rendering/renderer.cpp` (do the same for `JPH::` against
`core/physics/physics.cpp`). Anything outside those two files is a hard failure — fix it
before moving on. This one isn't a Phase 6 finding to weigh; it's a broken invariant.

### Phase 8 — Build, run, report

- **Build and run** — a clean file that doesn't run isn't clean:
  ```
  cmake --build --preset windows-debug
  ./build/windows-debug/ToonEngine.exe
  ```
  (Windows CLI needs the VS Dev environment — see CLAUDE.md / docs/clion-setup-windows.md.)
  Read the rebuild's warnings, not just its exit code — that's Phase 4's unused-include/
  local check running for real.
- **Report in two buckets per file**: *Applied* (what Phases 1-5 and 7, plus Phase 6 item
  4 where it qualified, actually changed) and *Flagged* (Phase 6's other findings, each as
  `file:line — what, why, suggested direction`, left for the user to decide). Keep the two
  separate — a flagged finding is a proposal, not a change that already happened.
- Keep changes reviewable: tidy in focused passes, don't reorder a whole file and change
  logic in the same commit.
