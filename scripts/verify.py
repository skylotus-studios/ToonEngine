#!/usr/bin/env python3
"""Tiered verification harness: fast (<90s target) | full | deep.

Ties together every CI-shaped tool this repo already has (--sim-only, --hash-every,
metrics.json/metrics_diff.py, --headless-render, the golden tests, --scene-roundtrip,
--resize-soak, ToonUnitTests, the SaveGame round-trip, an ASan build) into three gates:

  fast  -- pre-commit: configure+build, unit tests, a --sim-only hash smoke check, a scene
           load/save idempotency check, and a stand-in "player purity" check (see below).
  full  = fast + the structural golden-image tests, a metrics.json diff against the checked-in
          baseline, and a Vulkan-validation-errors-must-be-zero check.
  deep  = full + a 20-run determinism soak, an AddressSanitizer run of --sim-only, an install
          smoke test, a resize soak, and the SaveGame compat check.

Pure stdlib, matching every other script in scripts/.

Player purity: the task that asked for this harness calls it "Part 8" and asked to defer the
real spec ("we are in part 4"). What's implemented here is a mechanical stand-in: it parses
build.ninja's ToonPlayer.exe link edge and asserts it contains exactly one project object file
(player_main.cpp.obj) and none of ui/panels, editor_*, ImGuizmo, picking, scene_ops, or
thumbnail_cache -- the invariant player_main.cpp's own banner and CMakeLists.txt's "the linker
strips the editor by reachability" comment already state, made checkable. Not Part 8's final
spec.
"""
import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
BUILD_DIR = REPO_ROOT / "build/agent-debug"
ASAN_BUILD_DIR = REPO_ROOT / "build/agent-debug-asan"
SMOKE_SCENE = REPO_ROOT / "assets/scenes/smoke.scene"
FAST_BUDGET_SECONDS = 90.0

# cmake --preset/--build's OWN cwd, distinct from REPO_ROOT: a from-scratch configure bakes
# absolute source paths into build.ninja, and this repo's deepest include chains (observed:
# DiligentCore/Graphics/GraphicsTools/src/RenderStateCacheImpl.cpp) blow past Windows'
# MAX_PATH (260 chars) under the long checkout path (C:\dev\ToonEngine\develop\...) with a
# literal "path too long" ninja failure -- reproduced during this script's own development.
# C:\ted is this repo's documented short-path junction for exactly this reason (see CLAUDE.md/
# MEMORY.md's "Agent-preset build environment" note); prefer it when present, since it's the
# only thing that changes about where configure/build run (every other step still uses
# REPO_ROOT -- running an already-built exe never hits this).
CMAKE_CWD = Path("C:/ted") if Path("C:/ted").exists() else REPO_ROOT

# --- color -----------------------------------------------------------------------------------

_USE_COLOR = sys.stdout.isatty() and os.environ.get("NO_COLOR") is None


def _c(code, text):
    if not _USE_COLOR:
        return text
    return f"\x1b[{code}m{text}\x1b[0m"


def green(t):
    return _c("32", t)


def red(t):
    return _c("31", t)


def yellow(t):
    return _c("33", t)


def dim(t):
    return _c("2", t)


# --- subprocess helpers ------------------------------------------------------------------------


def run(cmd, cwd=None, env=None, timeout=None):
    """Runs `cmd`, returns (returncode, stdout, stderr). Never raises on a nonzero exit -- the
    caller decides what that means for its own step."""
    full_env = dict(os.environ)
    if env:
        full_env.update(env)
    try:
        result = subprocess.run(cmd, cwd=cwd, env=full_env, capture_output=True, text=True, timeout=timeout)
        return result.returncode, result.stdout, result.stderr
    except subprocess.TimeoutExpired as e:
        return -1, e.stdout or "", f"TIMEOUT after {timeout}s"
    except FileNotFoundError as e:
        return -1, "", str(e)


def player_path():
    return str(BUILD_DIR / "ToonPlayer.exe")


# --- step results ------------------------------------------------------------------------------


class StepResult:
    def __init__(self, passed, detail=""):
        self.passed = passed
        self.detail = detail


def fail(detail):
    return StepResult(False, detail)


def ok(detail=""):
    return StepResult(True, detail)


# --- fast tier steps -----------------------------------------------------------------------------


def step_configure(ctx):
    # Skip a no-op `cmake --preset` reconfigure: regenerating build.ninja bumps its mtime even
    # when nothing in it actually changes, and ninja treats that as "rules changed" -- which
    # cascades into a broad rebuild on the NEXT step (`build`) that has nothing to do with any
    # real staleness. Only reconfigure when CMakeLists.txt/CMakePresets.json are actually newer
    # than the last-generated build.ninja -- the same staleness check `cmake --build` itself
    # already performs internally before every build; doing it here too avoids paying for it
    # twice.
    ninja_path = BUILD_DIR / "build.ninja"
    if ninja_path.exists():
        ninja_mtime = ninja_path.stat().st_mtime
        sources_mtime = max((REPO_ROOT / f).stat().st_mtime for f in ("CMakeLists.txt", "CMakePresets.json"))
        if sources_mtime <= ninja_mtime:
            return ok("skipped -- build.ninja already up to date")

    env = {"SCCACHE_MAX_FRAME_LENGTH": "1073741824"}
    rc, out, err = run(["cmake", "--preset", "agent-debug"], cwd=CMAKE_CWD, env=env, timeout=180)
    if rc != 0:
        return fail(f"configure failed (exit {rc}): {err[-500:] or out[-500:]}")
    return ok()


def step_build(ctx):
    env = {"SCCACHE_MAX_FRAME_LENGTH": "1073741824"}
    rc, out, err = run(
        ["cmake", "--build", "--preset", "agent-debug", "--target", "ToonEngine", "ToonPlayer", "ToonUnitTests"],
        cwd=CMAKE_CWD, env=env, timeout=1200)
    if rc != 0:
        return fail(f"build failed (exit {rc}): {err[-1000:] or out[-1000:]}")
    return ok()


def step_unit_tests(ctx):
    rc, out, err = run(["ctest", "--test-dir", str(BUILD_DIR), "-R", "Math", "--output-on-failure"],
                       cwd=REPO_ROOT, timeout=60)
    if rc != 0:
        return fail(f"ctest -R Math failed (exit {rc}): {(out + err)[-800:]}")
    return ok()


def step_sim_only_smoke(ctx):
    tmp_metrics = REPO_ROOT / "artifacts/verify/sim_only_smoke_metrics.json"
    tmp_metrics.parent.mkdir(parents=True, exist_ok=True)
    rc, out, err = run([
        player_path(), "--sim-only", "--scene", str(SMOKE_SCENE), "--seed", "1234", "--ticks", "600",
        "--hash-every", "100", "--metrics-out", str(tmp_metrics),
    ], cwd=REPO_ROOT, timeout=60)
    if rc != 0:
        return fail(f"--sim-only exited {rc}: {(out + err)[-500:]}")

    baseline = REPO_ROOT / "tests/baselines/sim_only_smoke.json"
    rc, out, err = run([sys.executable, str(REPO_ROOT / "scripts/metrics_diff.py"), str(tmp_metrics),
                        "--baseline", str(baseline)], cwd=REPO_ROOT, timeout=30)
    if rc != 0:
        return fail(f"hash stream diverged from tests/baselines/sim_only_smoke.json: {(out + err)[-500:]}")
    return ok()


def step_scene_roundtrip(ctx):
    out_dir = REPO_ROOT / "artifacts/verify/roundtrip"
    rc, out, err = run([
        player_path(), "--scene-roundtrip", "--scene", str(SMOKE_SCENE), "--out-dir", str(out_dir),
    ], cwd=REPO_ROOT, timeout=30)
    if rc != 0:
        return fail(f"--scene-roundtrip exited {rc}: {(out + err)[-500:]}")

    pass1 = (out_dir / "pass1.scene").read_text()
    pass2 = (out_dir / "pass2.scene").read_text()
    if pass1 != pass2:
        return fail("pass1.scene and pass2.scene differ -- load->save is not idempotent "
                     "(something was dropped, reordered, or reformatted)")
    return ok()


# Substrings that must never appear anywhere in ToonPlayer.exe's build.ninja link edge --
# player_main.cpp's own banner: "links ToonRuntime and nothing from ui/panels/ or ImGuizmo".
_PURITY_FORBIDDEN = ["ui\\panels", "ui/panels", "editor_", "ImGuizmo", "picking.cpp.obj",
                     "scene_ops.cpp.obj", "thumbnail_cache.cpp.obj"]


def step_player_purity(ctx):
    ninja_path = BUILD_DIR / "build.ninja"
    if not ninja_path.exists():
        return fail(f"{ninja_path} not found -- run the build step first")

    edge_line = None
    for line in ninja_path.read_text(errors="replace").splitlines():
        if line.startswith("build ToonPlayer.exe:"):
            edge_line = line
            break
    if edge_line is None:
        return fail("no 'build ToonPlayer.exe:' edge found in build.ninja")

    for forbidden in _PURITY_FORBIDDEN:
        if forbidden in edge_line:
            return fail(f"ToonPlayer.exe's link edge contains '{forbidden}' -- editor code is "
                        f"reaching the player build")

    # Direct (non order-only) inputs are everything before " || "; count project objects among
    # them (paths under src\ or src/, ending .obj) -- expect exactly player_main.cpp.obj.
    direct = edge_line.split(" || ", 1)[0]
    project_objs = [tok for tok in direct.split() if tok.endswith(".obj") and ("src\\" in tok or "src/" in tok)]
    project_obj_names = [Path(p).name for p in project_objs]
    if project_obj_names != ["player_main.cpp.obj"]:
        return fail(f"expected exactly one project object (player_main.cpp.obj), found: {project_obj_names}")

    return ok(f"note: stand-in for the real Part 8 player-purity check, not the final spec")


# --- full tier steps -----------------------------------------------------------------------------


def step_golden_tests(ctx):
    rc, out, err = run([sys.executable, str(REPO_ROOT / "scripts/run_golden_tests.py"),
                        "--player", player_path()], cwd=REPO_ROOT, timeout=120)
    if rc != 0:
        return fail(f"run_golden_tests.py exited {rc}: {(out + err)[-800:]}")
    return ok()


def step_metrics_diff(ctx):
    tmp_metrics = REPO_ROOT / "artifacts/verify/metrics_diff_metrics.json"
    tmp_metrics.parent.mkdir(parents=True, exist_ok=True)
    rc, out, err = run([
        player_path(), "--sim-only", "--scene", str(SMOKE_SCENE), "--seed", "0", "--ticks", "3600",
        "--hash-every", "60", "--metrics-out", str(tmp_metrics),
    ], cwd=REPO_ROOT, timeout=60)
    if rc != 0:
        return fail(f"--sim-only exited {rc}: {(out + err)[-500:]}")

    baseline = REPO_ROOT / "tests/baselines/metrics.json"
    rc, out, err = run([sys.executable, str(REPO_ROOT / "scripts/metrics_diff.py"), str(tmp_metrics),
                        "--baseline", str(baseline)], cwd=REPO_ROOT, timeout=30)
    if rc != 0:
        return fail(f"metrics_diff.py breach vs tests/baselines/metrics.json: {(out + err)[-500:]}")
    return ok()


def step_vulkan_validation(ctx):
    total_errors = 0
    total_warnings = 0
    golden_dir = REPO_ROOT / "artifacts/golden"
    found_any = False
    for name in ("fill_outline", "shadow_cascades", "sprite_transparency"):
        metrics_path = golden_dir / name / "metrics.json"
        if not metrics_path.exists():
            continue
        found_any = True
        d = json.loads(metrics_path.read_text())
        total_errors += d.get("vulkan", {}).get("validation_errors") or 0
        total_warnings += d.get("vulkan", {}).get("validation_warnings") or 0

    if not found_any:
        return fail("no artifacts/golden/*/metrics.json found -- run the golden_tests step first")
    if total_errors > 0:
        return fail(f"{total_errors} Vulkan validation error(s) across the golden runs "
                    f"({total_warnings} warning(s))")
    return ok(f"0 errors, {total_warnings} warning(s) across 3 golden runs")


# --- deep tier steps -----------------------------------------------------------------------------


def _parse_hash_stream(stdout):
    stream = {}
    for line in stdout.splitlines():
        if line.startswith("HASH tick="):
            parts = line.split()
            tick = int(parts[1].split("=", 1)[1])
            value = parts[2].split("=", 1)[1]
            stream[tick] = value
    return stream


def step_determinism_soak(ctx):
    runs = []
    for i in range(20):
        rc, out, err = run([
            player_path(), "--sim-only", "--scene", str(SMOKE_SCENE), "--seed", "1234", "--ticks", "600",
            "--hash-every", "100", "--metrics-out", str(REPO_ROOT / f"artifacts/verify/soak_{i}.json"),
        ], cwd=REPO_ROOT, timeout=30)
        if rc != 0:
            return fail(f"run {i} exited {rc}: {(out + err)[-400:]}")
        runs.append(_parse_hash_stream(out))

    baseline = runs[0]
    for run_idx in range(1, len(runs)):
        for tick in sorted(baseline):
            if runs[run_idx].get(tick) != baseline.get(tick):
                return fail(f"determinism bug: run {run_idx} diverges from run 0 at tick {tick} "
                            f"(run0={baseline.get(tick)}, run{run_idx}={runs[run_idx].get(tick)})")
    return ok(f"20/20 runs produced identical hash streams")


def step_asan_sim_only(ctx):
    env = {"SCCACHE_MAX_FRAME_LENGTH": "1073741824"}
    rc, out, err = run(["cmake", "--preset", "agent-debug-asan"], cwd=CMAKE_CWD, env=env, timeout=180)
    if rc != 0:
        return fail(f"ASan configure failed (exit {rc}): {err[-800:] or out[-800:]}")

    rc, out, err = run(["cmake", "--build", "--preset", "agent-debug-asan", "--target", "ToonPlayer"],
                       cwd=CMAKE_CWD, env=env, timeout=1200)
    if rc != 0:
        return fail(f"ASan build failed (exit {rc}): {err[-1500:] or out[-1500:]}")

    asan_player = str(ASAN_BUILD_DIR / "ToonPlayer.exe")
    asan_env = {"ASAN_OPTIONS": "halt_on_error=1:abort_on_error=1"}
    rc, out, err = run([
        asan_player, "--sim-only", "--scene", str(SMOKE_SCENE), "--seed", "1234", "--ticks", "600",
        "--metrics-out", str(REPO_ROOT / "artifacts/verify/asan_metrics.json"),
    ], cwd=REPO_ROOT, env=asan_env, timeout=60)
    if rc != 0:
        return fail(f"ASan run exited {rc} (a real ASan finding, or a toolchain issue -- see output): "
                    f"{(out + err)[-1500:]}")
    return ok("clang-cl /fsanitize=address only -- no UBSan/TSan on this toolchain")


def step_install_smoke(ctx):
    install_dir = REPO_ROOT / "artifacts/verify/install"
    rc, out, err = run(["cmake", "--install", str(BUILD_DIR), "--prefix", str(install_dir),
                        "--component", "toonengine"], cwd=REPO_ROOT, timeout=60)
    if rc != 0:
        return fail(f"cmake --install exited {rc}: {(out + err)[-500:]}")

    installed_player = install_dir / "ToonPlayer.exe"
    if not installed_player.exists():
        return fail(f"{installed_player} was not staged by cmake --install")

    metrics_path = install_dir / "metrics.json"
    rc, out, err = run([
        str(installed_player), "--sim-only", "--scene", "smoke.scene", "--ticks", "60",
        "--metrics-out", str(metrics_path),
    ], cwd=install_dir, timeout=30)
    if rc != 0:
        return fail(f"installed ToonPlayer exited {rc}: {(out + err)[-500:]}")

    # A deliberately nonexistent --baseline: this 60-tick run has nothing to do with
    # tests/baselines/metrics.json's own 3600-tick/seed-0 shape, so pointing at it would compare
    # apples to oranges (a guaranteed, meaningless "different tick samples" breach). Passing a
    # missing baseline path makes metrics_diff.py skip the diff entirely (its own documented
    # behavior) and run ONLY the --require-no-fallback check this step actually cares about.
    rc, out, err = run([sys.executable, str(REPO_ROOT / "scripts/metrics_diff.py"), str(metrics_path),
                        "--baseline", str(REPO_ROOT / "tests/baselines/__no_such_baseline__.json"),
                        "--require-no-fallback"], cwd=REPO_ROOT, timeout=30)
    if rc != 0:
        return fail(f"installed build read assets.fallback_used=true -- it's reading the dev "
                    f"tree instead of its own staged assets/: {(out + err)[-500:]}")
    return ok()


def step_resize_soak(ctx):
    metrics_path = REPO_ROOT / "artifacts/verify/resize_soak_metrics.json"
    rc, out, err = run([
        player_path(), "--resize-soak", "40", "--scene", str(SMOKE_SCENE),
        "--metrics-out", str(metrics_path),
    ], cwd=REPO_ROOT, timeout=60)
    if rc == 3:
        return fail(f"Vulkan validation error(s) during the resize soak: {(out + err)[-500:]}")
    if rc != 0:
        return fail(f"--resize-soak exited {rc}: {(out + err)[-500:]}")
    return ok()


def step_savegame_compat(ctx):
    rc, out, err = run(["ctest", "--test-dir", str(BUILD_DIR), "-R", "SaveGame", "--output-on-failure"],
                       cwd=REPO_ROOT, timeout=30)
    if rc != 0:
        return fail(f"ctest -R SaveGame failed (exit {rc}): {(out + err)[-800:]}")
    return ok()


# --- registry ------------------------------------------------------------------------------------

FAST_STEPS = [
    ("configure", step_configure),
    ("build", step_build),
    ("unit_tests", step_unit_tests),
    ("sim_only_smoke", step_sim_only_smoke),
    ("scene_roundtrip", step_scene_roundtrip),
    ("player_purity", step_player_purity),
]
FULL_EXTRA = [
    ("golden_tests", step_golden_tests),
    ("metrics_diff", step_metrics_diff),
    ("vulkan_validation", step_vulkan_validation),
]
DEEP_EXTRA = [
    ("determinism_soak", step_determinism_soak),
    ("asan_sim_only", step_asan_sim_only),
    ("install_smoke", step_install_smoke),
    ("resize_soak", step_resize_soak),
    ("savegame_compat", step_savegame_compat),
]

TIERS = {
    "fast": FAST_STEPS,
    "full": FAST_STEPS + FULL_EXTRA,
    "deep": FAST_STEPS + FULL_EXTRA + DEEP_EXTRA,
}


def load_xfail():
    path = REPO_ROOT / "tests/xfail.json"
    if not path.exists():
        return {}
    entries = json.loads(path.read_text())
    return {e["check"]: e for e in entries}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--tier", choices=["fast", "full", "deep"], default="fast")
    parser.add_argument("--skip", action="append", default=[],
                        help="step name to skip; repeatable and/or comma-separated")
    parser.add_argument("--no-color", action="store_true")
    args = parser.parse_args()

    global _USE_COLOR
    if args.no_color:
        _USE_COLOR = False

    skip = set()
    for entry in args.skip:
        skip.update(s.strip() for s in entry.split(",") if s.strip())

    xfail = load_xfail()

    steps = TIERS[args.tier]
    results = []  # (name, StepResult|None "SKIP", duration)
    fast_step_names = {name for name, _ in FAST_STEPS}
    budget_stopped = False

    for name, fn in steps:
        if name in skip:
            results.append((name, "SKIP", 0.0))
            continue

        start = time.time()
        try:
            result = fn(None)
        except Exception as e:  # a step's own bug shouldn't take down the whole harness silently
            result = fail(f"step raised {type(e).__name__}: {e}")
        duration = time.time() - start
        results.append((name, result, duration))

        # After every fast-tier step's own name has been processed, check whether we've now run
        # the full fast subset (in order) and, if so, gate the budget once.
        ran_fast_so_far = [(n, r, d) for n, r, d in results if n in fast_step_names]
        if len(ran_fast_so_far) == len(FAST_STEPS) and not budget_stopped:
            fast_total = sum(d for _, _, d in ran_fast_so_far if isinstance(d, float))
            if fast_total > FAST_BUDGET_SECONDS:
                slowest = max(ran_fast_so_far, key=lambda t: t[2] if isinstance(t[2], float) else 0.0)
                print()
                print(red(f"BUDGET EXCEEDED: fast tier took {fast_total:.1f}s (budget "
                          f"{FAST_BUDGET_SECONDS:.0f}s)"))
                print(red(f"  slowest step: {slowest[0]} ({slowest[2]:.1f}s)"))
                print(red("  this is a Part 3 problem, not something to paper over -- stopping "
                          "before any further tier runs."))
                budget_stopped = True
                break

    # --- summary table --------------------------------------------------------------------------
    print()
    print(f"{'TIER':<6} {'CHECK':<20} {'STATUS':<28} {'TIME':>8}")
    print("-" * 66)

    exit_code = 0
    fast_total = 0.0
    for name, result, duration in results:
        tier_label = "fast" if name in fast_step_names else ("full" if any(name == n for n, _ in FULL_EXTRA) else "deep")
        if name in fast_step_names:
            fast_total += duration if isinstance(duration, float) else 0.0

        if result == "SKIP":
            plain_status = "SKIP"
            colorize = dim
            time_str = "-"
        elif result.passed:
            plain_status = "PASS" + (f" ({result.detail})" if result.detail else "")
            colorize = green
            time_str = f"{duration:.1f}s"
        else:
            xf = xfail.get(name)
            time_str = f"{duration:.1f}s"
            if xf:
                plain_status = f"known-failing: item {xf['roadmap_item']} -- {result.detail}"
                colorize = yellow
            else:
                plain_status = f"FAIL -- {result.detail}"
                colorize = red
                exit_code = 1

        # Pad the PLAIN text first, then colorize -- ANSI escape bytes would otherwise count
        # toward the field width and break column alignment.
        padded = f"{plain_status:<28}" if len(plain_status) <= 28 else plain_status
        print(f"{tier_label:<6} {name:<20} {colorize(padded)} {time_str:>8}")

    print("-" * 66)
    print(f"fast-tier total: {fast_total:.1f}s (budget {FAST_BUDGET_SECONDS:.0f}s)")

    if budget_stopped:
        return 3
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
