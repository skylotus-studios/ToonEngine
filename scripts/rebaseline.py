#!/usr/bin/env python3
"""Re-capture every golden scene and overwrite tests/golden/*/frame.png.

REFUSES TO RUN WITHOUT --reason "...". That refusal is the whole point of this script: a golden
test going red is a real signal (see scripts/run_golden_tests.py), and re-baselining without
saying why is exactly how a real regression quietly becomes "the new normal". See CLAUDE.md's
hard rule 6:

    Never re-baseline to make a build green. Re-baselining requires a stated intended visual
    change, and Nate looks at the diff image.

This script stages the new PNGs and the stated reason (git add) but never runs `git commit`
itself -- no script in this repo commits on the user's behalf. Put the reason in the commit
message; Nate reviews the diff image before this merges.
"""
import argparse
import subprocess
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(Path(__file__).resolve().parent))
from run_golden_tests import SCENES, run_scene  # reuse the exact same capture invocation


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--reason", required=True,
                        help="required: the stated, intended visual change this re-baseline reflects")
    parser.add_argument("--player", default=str(REPO_ROOT / "build/agent-debug/ToonPlayer.exe"),
                        help="path to ToonPlayer.exe (default: build/agent-debug/ToonPlayer.exe)")
    parser.add_argument("--artifacts-dir", default=str(REPO_ROOT / "artifacts/golden"),
                        help="where captures are written before being copied into tests/golden/")
    args = parser.parse_args()

    if not args.reason.strip():
        print("rebaseline: --reason cannot be blank -- say what visual change this reflects",
              file=sys.stderr)
        return 1

    player = Path(args.player)
    if not player.exists():
        print(f"rebaseline: player not found at '{player}' -- build it first "
              f"(cmake --build --preset agent-debug --target ToonPlayer)", file=sys.stderr)
        return 1

    artifacts_dir = Path(args.artifacts_dir)
    golden_dir = REPO_ROOT / "tests/golden"

    staged = []
    for name, scene_path in SCENES.items():
        capture_dir = artifacts_dir / name
        current_png = run_scene(player, name, scene_path, capture_dir)
        if current_png is None:
            print(f"rebaseline: capturing '{name}' failed -- aborting before touching tests/golden/",
                  file=sys.stderr)
            return 1

        dest_dir = golden_dir / name
        dest_dir.mkdir(parents=True, exist_ok=True)
        dest_png = dest_dir / "frame.png"
        dest_png.write_bytes(current_png.read_bytes())
        staged.append(str(dest_png))
        print(f"rebaseline: wrote {dest_png}")

    reason_file = golden_dir / "REBASELINE_REASON.txt"
    reason_file.write_text(args.reason.strip() + "\n", encoding="utf-8")
    staged.append(str(reason_file))

    subprocess.run(["git", "add", *staged], cwd=REPO_ROOT, check=False)

    print()
    print(f"rebaseline: staged {len(staged)} file(s). Reason: {args.reason.strip()}")
    print("rebaseline: this is staged, NOT committed. Put the reason in the commit message, and "
          "get Nate to look at the diff image before this merges (CLAUDE.md hard rule 6).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
