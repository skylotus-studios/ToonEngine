---
name: verify
description: Run ToonEngine's tiered CI harness (scripts/verify.py fast|full|deep) and report a straight pass/fail. Triggers on "verify", "run verify", "is it green". Not for a live interactive/UI smoke check -- see verify-build-launch-screenshot-archived for that.
---

# verify: ToonEngine's Tiered Verification Harness

Runs `scripts/verify.py`, which already wires together every CI-shaped tool in this repo
(`--sim-only`, `--hash-every`, `metrics.json`/`metrics_diff.py`, `--headless-render`, the
golden tests, `--scene-roundtrip`, `--resize-soak`, ToonUnitTests, the SaveGame round-trip, an
ASan build) into three gates. This skill does not reimplement any of that logic -- it just
invokes the script honestly and reports what it says.

## Step 1: Pick a Tier

If the user's request already names a tier (fast/full/deep), use it. Otherwise ask:

- **fast** (default) -- configure+build, unit tests, a `--sim-only` hash smoke check, a scene
  load/save idempotency check, player-purity check. Pre-commit weight, <90s budget.
- **full** -- fast + structural golden-image tests, a `metrics.json` diff against the checked-in
  baseline, a Vulkan-validation-errors-must-be-zero check.
- **deep** -- full + a 20-run determinism soak, an ASan `--sim-only` run, an install smoke test,
  a resize soak, the SaveGame compat check.

## Step 2: Run It

```
python scripts/verify.py --tier <fast|full|deep>
```

Run from `REPO_ROOT`; the script itself handles the `C:\ted` short-path junction for
configure/build when present (see repo MEMORY.md -> "Agent-preset build environment"). Do not
pre-import the VS Developer environment yourself unless the run reports a `lib.exe`/`CMAKE_MT`
failure -- the script's own `configure`/`build` steps are what need it, and importing it wastes
a step if the build dir is already configured and warm.

## Step 3: Report

Report the script's own summary table verbatim (it already prints `TIER / CHECK / STATUS /
TIME`), plus the **actual process exit code** (`0` or `1` -- read it, don't infer it from the
table). A row can be `PASS`, `SKIP`, `known-failing: item N -- ...` (an `xfail.json` entry,
shown in yellow, does not fail the run), or `FAIL -- ...` (red, sets exit 1).

**Never report a nonzero exit as success, and never re-baseline to make a run green** -- both
per the Six Hard Rules (rule 6) and per this skill's own purpose. If a `full`/`deep` run's
`metrics_diff` or `golden_tests` step fails, that is real signal, not noise to paper over; a
re-baseline needs a stated intended visual change and Nate looking at the diff image, which is
outside this skill's scope to decide.

## Step 4: On Failure, Offer Exactly Two Next Actions

Don't sprawl into a longer menu. Offer:

1. **"investigate [failing check]"** -- read that step's own captured output/log (the summary
   row's `FAIL -- ...` detail is already a truncated tail of it; the step functions in
   `scripts/verify.py` show where the full output or backing artifact lives).
2. **"show me the artifact"** -- surface the concrete evidence for that check:
   - `metrics_diff` failures: the metrics JSON at `artifacts/verify/metrics_diff_metrics.json`
     (or `sim_only_smoke_metrics.json` for the fast-tier smoke check) diffed against
     `tests/baselines/metrics.json` via `scripts/metrics_diff.py`.
   - `golden_tests` / `vulkan_validation` failures: `artifacts/golden/<test>/metrics.json` and
     whatever diff image `scripts/run_golden_tests.py` wrote alongside it.
   - `determinism_soak` failures: the first divergent tick, from comparing
     `artifacts/verify/soak_<i>.json` across runs.
   - `scene_roundtrip` failures: the two scene files under `artifacts/verify/roundtrip/`
     (`pass1.scene` vs `pass2.scene`) that should have matched.

Wait for the user to pick one rather than dumping both proactively.
