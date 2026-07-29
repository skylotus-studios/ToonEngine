#!/usr/bin/env python3
"""Run every tests/scenes/golden_*.scene through ToonPlayer --headless-render and diff the
capture against tests/golden/<name>/frame.png.

CI entry point for the structural golden-image tests (see scripts/golden_diff.py's own docstring
for the metric, and tests/scenes/golden_*.scene's own banners for what each scene isolates and
why --post off is required). Pure stdlib, matching every other script in scripts/.

A GOLDEN GOING RED IS NOT SOMETHING TO FIX BY RE-BASELINING. See scripts/rebaseline.py and
CLAUDE.md's hard rule 6.
"""
import argparse
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

# name -> scene file (relative to REPO_ROOT). One capture frame per scene: these are all fully
# static (no rigidbody, no script -- see each scene file's own banner), so a few warmup frames
# plus one capture is exactly as reproducible as capturing frame 0 would be, with a small buffer
# against any first-frame-only initialization artifact.
SCENES = {
    "fill_outline": "tests/scenes/golden_fill_outline.scene",
    "shadow_cascades": "tests/scenes/golden_shadow_cascades.scene",
    "sprite_transparency": "tests/scenes/golden_sprite_transparency.scene",
}
FRAMES = 5
CAPTURE_FRAME = 4


def run_scene(player, name, scene_path, capture_dir):
    capture_dir.mkdir(parents=True, exist_ok=True)
    cmd = [
        str(player), "--headless-render", "--post", "off",
        "--scene", str(REPO_ROOT / scene_path),
        "--frames", str(FRAMES), "--capture", str(CAPTURE_FRAME),
        "--capture-dir", str(capture_dir),
        "--metrics-out", str(capture_dir / "metrics.json"),
    ]
    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        print(f"run_golden_tests: {name}: --headless-render exited {result.returncode}", file=sys.stderr)
        print(result.stdout, file=sys.stderr)
        print(result.stderr, file=sys.stderr)
        return None
    return capture_dir / f"frame_{CAPTURE_FRAME:04d}.png"


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--player", default=str(REPO_ROOT / "build/agent-debug/ToonPlayer.exe"),
                        help="path to ToonPlayer.exe (default: build/agent-debug/ToonPlayer.exe)")
    parser.add_argument("--threshold", type=float, default=None,
                        help="forwarded to golden_diff.py (default: golden_diff.py's own default)")
    parser.add_argument("--artifacts-dir", default=str(REPO_ROOT / "artifacts/golden"),
                        help="where captures are written (default: artifacts/golden)")
    args = parser.parse_args()

    player = Path(args.player)
    if not player.exists():
        print(f"run_golden_tests: player not found at '{player}' -- build it first "
              f"(cmake --build --preset agent-debug --target ToonPlayer)", file=sys.stderr)
        return 1

    golden_diff = Path(__file__).resolve().parent / "golden_diff.py"
    artifacts_dir = Path(args.artifacts_dir)

    failures = []
    for name, scene_path in SCENES.items():
        capture_dir = artifacts_dir / name
        current_png = run_scene(player, name, scene_path, capture_dir)
        if current_png is None:
            failures.append(name)
            continue

        baseline_png = REPO_ROOT / "tests/golden" / name / "frame.png"
        diff_cmd = [sys.executable, str(golden_diff), str(current_png), str(baseline_png)]
        if args.threshold is not None:
            diff_cmd += ["--threshold", str(args.threshold)]
        diff_result = subprocess.run(diff_cmd, capture_output=True, text=True)
        print(diff_result.stdout, end="")
        print(diff_result.stderr, end="", file=sys.stderr)
        if diff_result.returncode != 0:
            failures.append(name)

    print()
    if failures:
        print(f"run_golden_tests: FAIL -- {len(failures)}/{len(SCENES)} scene(s) breached: "
              f"{', '.join(failures)}", file=sys.stderr)
        return 1

    print(f"run_golden_tests: OK -- {len(SCENES)}/{len(SCENES)} scene(s) passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
