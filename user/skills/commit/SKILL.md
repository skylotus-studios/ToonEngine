---
name: commit
description: Fast git commit + push for changes that are already ready. Minimal analysis, no back-and-forth. Use when the user asks to commit (and push) current changes, e.g. "/commit".
---

# commit: Fast Commit + Push

The user has already decided the changes are ready. Don't re-review the diff at length,
don't run tests/build/lint first, don't co-author, and don't ask for confirmation before 
pushing: invoking this skill *is* the confirmation.

Steps:

1. `git status` and `git diff --staged` (or `git diff` if nothing is staged yet): one
   glance to write the message, not a review.
2. If nothing is staged: `git add -u` (tracked files only). If there are untracked files
   that look like part of this change, ask once which to include; otherwise leave them.
3. Write a short, imperative commit message matching this repo's style: one line, body
   only if the diff does something non-obvious.
4. `git push`.
5. Report the commit hash and branch in one line. Nothing else unless push fails.