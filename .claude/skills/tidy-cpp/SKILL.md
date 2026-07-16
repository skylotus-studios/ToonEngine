---
name: tidy-cpp
description: Clean up ToonEngine's own C++ source (src/**) to the house style in docs/cpp-style-guide.md — formatting, section structure, comment clarity/onboarding, and removing cruft. Use when the user asks to tidy, clean up, neaten, reorganize, or improve the readability of ToonEngine source files. Quality/readability only; it does not change behavior.
---

# tidy-cpp — clean ToonEngine source to house style

Tidy **our** C++ under `src/**` to `docs/cpp-style-guide.md`. Read that guide first —
it is the source of truth; this skill is the procedure for applying it.

## Scope

- **In scope:** `src/**` only (`main.cpp`, `core/*.h`, `core/*.cpp`, future modules).
- **Never touch** `external/**` (Diligent/GLFW/ImGui submodules) or generated
  `build/**`. Never reformat a file we don't own.
- If the user named specific files, limit to those. Otherwise ask which files, or
  default to the ones changed on the current branch (`git diff --name-only main`)
  rather than churning the whole tree.
- **Readability only.** Do not change behavior, rename public API, or alter the
  renderer's abstraction layer. If you spot a real bug while tidying, surface it separately — don't
  silently "fix" it inside a cleanup pass.

## Procedure

For each in-scope file, walk the style guide's **§7 cleanup checklist**:

1. **Format** — apply clang-format (repo `.clang-format`). If `clang-format` is on
   PATH: `clang-format -i --style=file <files>`. Otherwise hand-align to the rules in
   §1. Don't fight the formatter afterward.
2. **Structure** — confirm the file banner is present and accurate; group functions
   under `// --- Section ---` dividers in lifecycle order (see `core/renderer.cpp`).
   Move a stray function under the right divider; don't reorder wholesale.
3. **Comments** — every function has a clear one-line lead comment; comments explain
   *why* (winding, matrix/format conventions, ordering, deliberate-looking-wrong,
   external quirks), not what the syntax says. Fix stale comments in place. Point
   across the abstraction layer when a value must match elsewhere.
4. **Cruft** — delete dead/commented-out code, leftover debug prints, unused includes
   and locals, stale TODOs. git remembers.
5. **Spacing** — single blank lines between paragraphs/functions; manual column
   alignment only within tight related blocks.
6. **Naming & placement** — matches §5; new mutable state lives in the data-encapsulated
   `Impl`, not globals.
7. **Abstraction layer** — no Diligent header or `Diligent::` type leaked past `core/renderer.cpp`;
   `core/renderer.h` still compiles Diligent-free.

## Finish

- **Build and run** — a clean file that doesn't run isn't clean:
  ```
  cmake --build --preset windows-debug
  ./build/windows-debug/ToonEngine.exe
  ```
  (Windows CLI needs the VS Dev environment — see CLAUDE.md / docs/clion-setup-windows.md.)
- Keep it reviewable: tidy in focused passes; don't mix a reformat with a logic
  change in one commit. Summarize what you changed per file.
