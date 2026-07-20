---
name: publish-main
description: Publish develop's current state to main, the clean public branch. Overlays develop's tree onto main and strips the AI-assisted-workflow files that never belong there (CLAUDE.md, MEMORY.md, ARCHIVE.md, .claude/, .clang-format, .clangd, the style guides), plus a couple of doc sections that only make sense with those files present. Use when the user asks to publish, sync, or push develop's work to main.
---

# publish-main: Sync develop -> main (Clean Publish)

`main` is the public-facing branch: a clone of it should look like a finished engine, not a
worked-on-with-Claude one. `develop` is where all day-to-day work actually happens. This
skill produces one new commit on `main` containing develop's current tree minus the
excluded paths below. It does **not** use `git merge` — develop edits CLAUDE.md/MEMORY.md
constantly, so a merge would hit a modify/delete conflict on those paths on every single
publish. Overlay + explicit removal never conflicts.

## Steps

1. **Confirm branch and working tree.** Must be run from (or able to reach) `develop`. If
   `develop`'s working tree has uncommitted changes, stash them first
   (`git stash push -u -m "..."`) — they'll be restored at the end. Don't publish uncommitted
   work; only what's actually committed to `develop` reaches `main`.
2. **Checkout `main`.**
3. **Overlay develop's tree**: `git checkout develop -- .`. This updates/adds every file
   develop has, but — this is the gotcha — it does **not** delete files `main` has that
   `develop` doesn't already lack. The excluded paths must be removed explicitly every time,
   even though they were removed last time too.
4. **Remove the excluded paths** (both from the index and the working tree):
   - `CLAUDE.md`, `MEMORY.md`, `ARCHIVE.md`
   - `.claude/` (all of it — skills and any local settings)
   - `.clang-format`, `.clangd`
   - `docs/cpp-style-guide.md`, `docs/md-style-guide.md`

   `git rm -rf --cached <paths>` followed by `rm -rf <paths>` handles both.
5. **Fix up the doc sections that only make sense with those files present** (main-only
   edits — do not carry these back to `develop`, they're wrong there):
   - `docs/roadmap.md`: drop the `## How This List Is Maintained` section (names
     `update-roadmap`/`plan-roadmap`/`tidy-md`, skills that don't exist on `main`).
   - `docs/clion-setup-windows.md`: drop the `## 5. Code Style` section (references the
     now-absent `.clang-format`).
   - Re-check with `grep -rn "CLAUDE\.md\|MEMORY\.md\|ARCHIVE\.md\|\.clang-format\|\.clangd" --include="*.md" .` before committing — if `develop` picked up a new cross-reference to
     one of the excluded files since the last publish, reword or drop it here too, the same
     way `git log` shows it was handled for the existing references.
6. **`main`'s `.gitignore` carries its own block** (separate from `develop`'s) listing the
   excluded paths as a safety net, so they can't be accidentally re-added to `main` by hand
   later. Leave that block as-is; only touch it if the excluded-paths list above changes.
7. **Commit** with a short message, e.g. `Publish snapshot from develop`, plus a one-line
   body naming anything unusual about this particular sync. Push `main`.
8. **Return to `develop`** and restore anything stashed in step 1
   (`git stash pop`). If the pop conflicts (it will, on any file this skill also edited
   fresh on `main` in step 5 — those edits don't touch `develop`, so this should be rare),
   resolve by keeping `develop`'s own content.

## Non-Goals

This does not rewrite `main`'s prior history — each publish is a new commit on top of the
last, not a squash or a force-push. It does not touch `develop`'s own files beyond the
stash/pop in steps 1 and 8. If the excluded-paths list itself needs to change (a new
develop-only doc appears, for instance), update this file, not just this one run.
