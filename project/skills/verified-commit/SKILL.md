---
name: verified-commit
description: Commit + push ToonEngine changes gated on scripts/verify.py fast passing, with a greppable audit trail (Verified:/Determinism:/Review:/Invariant: trailers) in the message. Refuses to commit without a fresh passing verify run. Distinct from the user-global "commit" skill, which does no gating. Triggers on "verified commit", "commit with verify", "audit commit".
---

# verified-commit: Gated Commit With an Audit-Trail Message

This is the repo-level counterpart to the user-global `commit` skill. That one is
"the user already decided it's ready, just push it" -- fast, no gating. This one is the
opposite: it exists specifically so `git log --grep="Verified:"` and
`git log --grep="Determinism:"` are a permanent, greppable, file-free audit trail of what
was actually checked before each commit landed. Never skip the gate to go faster; that
defeats the only reason this skill exists over the plain `commit` one.

**No AI/authorship attribution, ever.** Never add `Co-Authored-By`, "Generated with
Claude", or any other AI-mention line to the commit message -- trailers below are audit
metadata, not authorship claims, and stay factual/tool-output only.

## Step 1: Gate on a Fresh `verify.py fast` Pass

Always run it fresh, right now, on the current tree -- staged and unstaged changes together,
exactly as they'll land in the commit. "Re-run if anything changed since" is satisfied by
this being unconditional, not by trying to detect staleness:

```
python scripts/verify.py --tier fast
```

(Or `--tier full`/`--tier deep` if the user explicitly asked for a stronger gate on this
commit -- the `Verified:` trailer records whichever tier actually ran.)

- **Exit 0 -> proceed to Step 2.**
- **Exit != 0 -> refuse to commit.** Report the failing check(s) exactly as the `verify`
  skill does (summary table + real exit code), and stop. Do not commit "anyway," do not
  offer to skip the gate, do not re-baseline to force it green -- that's rule 6 of the Six
  Hard Rules and the entire point of this skill. If nothing was staged/changed at all,
  there's nothing to commit; say so instead of running the gate pointlessly.

## Step 2: Build the Message

**Subject**: conventional-commit format (`type(scope): summary`, imperative, under ~70
chars) inferred from the diff, matching this repo's existing log style (`git log --oneline`
for tone/verb choice -- e.g. "Add X", "Fix Y").

**Body**: only if the diff does something non-obvious; same bar as the plain `commit` skill.

**Trailers** (blank line before the trailer block, one trailer per line, only the ones that
apply -- don't pad with empty/N-A trailers):

- `Verified: <tier> (exit 0)` -- **always present**, from Step 1's run. This is the one
  load-bearing trailer; everything else is conditional.
- `Spec: docs/specs/<file>` -- when the commit implements or follows a spec under
  `docs/specs/` (check whether the diff touches, or was scoped by, a file there).
- `Determinism: soak-20 pass, hash <first-hash> @ tick <N>` -- only when a determinism soak
  actually ran as part of this commit's verification (i.e. Step 1 used `--tier deep`, or the
  user separately ran the soak this session). Pull the hash/tick from that step's own output
  (`scripts/verify.py`'s `determinism_soak` step, or `artifacts/verify/soak_*.json`). Never
  fabricate a hash to fill this trailer in -- omit it if no soak ran.
- `Review: <model> cold-review -- blockers N, should-fix M` -- only when a review (e.g.
  `/code-review`) already ran earlier in this session against this diff. Pull the real
  blocker/should-fix counts from that review's findings; omit if none ran.
- `Invariant: <one line>` -- only when this commit adds or changes an entry in
  `docs/invariants.md` (the Six Hard Rules doc). State which invariant, one line.

## Step 3: Refuse If `Verified:` Would Be Missing

This should be unreachable given Step 1 always runs first, but it's the hard backstop:
never construct or emit a commit message that lacks a `Verified:` trailer. If somehow
reached with no passing verify result in hand, stop and re-run Step 1 rather than commit
without it.

## Step 4: Commit, Then Push

```
git commit -m "<subject>" -m "<body if any>" -m "<trailer block>"
git push
```

Commit on the current branch (do not switch branches to force `develop` specifically --
match wherever the user is already working); push to that branch's configured remote/
upstream. Report the commit hash, branch, and remote push result in one line.
