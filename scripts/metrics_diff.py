#!/usr/bin/env python3
"""Compare a --sim-only metrics.json against a checked-in baseline.

Companion to app/metrics.h/.cpp (see that file's banner for what --sim-only can and can't
populate today) -- pure stdlib, no new dependency, matching the project's "no new dependencies
without asking" constraint.

Two kinds of field:
  - TOLERANCE fields (perf numbers: sim.tick_ms.*, render.frame_ms.*): allowed to drift by a
    relative fraction before it counts as a breach. Timing is inherently noisy.
  - EXACT fields (everything else that isn't a perf number: body/contact counts, the hash
    streams, scripts.ran, audio counters): a deterministic scene's structural facts. ANY drift
    here is a real regression, not noise -- these are exactly what --hash-every / HashWorldState
    (app/world_hash.h) exist to make bit-exact across runs.

assets.fallback_used is handled OUTSIDE the baseline diff entirely -- see --require-no-fallback
below. Comparing it against a baseline would be actively wrong: an ordinary dev build's baseline
will always say true (nothing stages assets/ next to a build/<preset>/ exe), so an equality check
would never catch the one case this field exists to catch -- an INSTALLED build silently reading
the source tree.
"""
import argparse
import json
import sys

# Relative tolerance for perf fields: current is allowed up to (1 + TOLERANCE) * baseline before
# it's reported as a breach. 20% is a starting point, not a measured budget -- see
# docs/performance.md's own "no frame-time target has been recorded yet" note; tighten once real
# runs establish a stable range.
TOLERANCE_FIELDS = {
    "sim.tick_ms.p50": 0.20,
    "sim.tick_ms.p99": 0.20,
    "render.frame_ms.p50": 0.20,
    "render.frame_ms.p99": 0.20,
    # Process peak working-set size (core/platform/memstats.h), NOT a deterministic count like
    # the fields below it: OS page allocation/rounding varies run to run for reasons that have
    # nothing to do with an engine regression (observed in practice: two otherwise-identical
    # --sim-only runs of the same scene differed by ~110KB / <0.5%). An exact match here was
    # scripts/verify.py's own fast tier's first real false positive.
    "alloc.peak_bytes": 0.20,
}

# Exact-match fields: any two values that differ are a breach, no tolerance. sim.state_hash and
# scripts.order_hash are compared as a whole (see compare_state_hash below), not listed here.
EXACT_FIELDS = [
    "jolt.bodies",
    "jolt.active_bodies",
    "jolt.contacts",
    "scripts.ran",
    "scripts.order_hash",
    "audio.voices_active",
    "audio.handles_leaked",
    "render.draw_calls",
    "render.pso_switches",
    "vulkan.validation_errors",
    "vulkan.validation_warnings",
    "gpu.mem_bytes",
    "alloc.count_after_init",
    "ui.boxes_live",
    "ui.boxes_pruned",
]


def get_path(data, dotted):
    """Walk a dotted field path ("sim.tick_ms.p50") through nested dicts. Returns
    _MISSING (a sentinel, not None -- None is a legitimate value for a not-yet-implemented
    field) if any segment along the path doesn't exist."""
    node = data
    for part in dotted.split("."):
        if not isinstance(node, dict) or part not in node:
            return _MISSING
        node = node[part]
    return node


_MISSING = object()


def compare_tolerance(field, baseline_val, current_val, tolerance, breaches):
    if baseline_val is None or current_val is None:
        # Both null (field not yet implemented on either side): not a regression. One null, one
        # not: a schema change worth knowing about but not a threshold breach -- see the
        # docstring on why null-vs-populated isn't treated as failure.
        return
    if baseline_val == 0:
        # Avoid a division-by-zero false breach on a baseline that happened to record exactly 0;
        # fall back to an absolute check instead.
        if current_val != 0:
            breaches.append((field, baseline_val, current_val, f"baseline=0, current={current_val}"))
        return
    limit = baseline_val * (1.0 + tolerance)
    if current_val > limit:
        breaches.append((field, baseline_val, current_val, f"exceeds +{tolerance*100:.0f}% (limit {limit:.4f})"))


def compare_exact(field, baseline_val, current_val, breaches):
    if baseline_val is None or current_val is None:
        return  # not yet implemented on one or both sides -- see compare_tolerance's own note
    if baseline_val != current_val:
        breaches.append((field, baseline_val, current_val, "exact match required"))


def compare_state_hash(baseline, current, breaches):
    """sim.state_hash is an array of {tick, hash} samples, not a scalar -- compared entry-by-entry
    (by tick) rather than through the generic EXACT_FIELDS loop. A length mismatch (different
    --ticks/--hash-every between the two runs) is reported once rather than spamming a breach per
    missing sample."""
    b = get_path(baseline, "sim.state_hash")
    c = get_path(current, "sim.state_hash")
    if b is _MISSING or c is _MISSING or b is None or c is None:
        return
    b_by_tick = {entry["tick"]: entry["hash"] for entry in b}
    c_by_tick = {entry["tick"]: entry["hash"] for entry in c}
    if set(b_by_tick) != set(c_by_tick):
        breaches.append(("sim.state_hash", sorted(b_by_tick), sorted(c_by_tick),
                          "different tick samples -- runs used different --ticks/--hash-every"))
        return
    for tick, baseline_hash in b_by_tick.items():
        current_hash = c_by_tick[tick]
        if baseline_hash != current_hash:
            breaches.append((f"sim.state_hash[tick={tick}]", baseline_hash, current_hash,
                              "simulation diverged from the baseline at this tick"))


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("current", nargs="?", default="artifacts/metrics.json",
                        help="metrics.json to check (default: artifacts/metrics.json)")
    parser.add_argument("--baseline", default="tests/baselines/metrics.json",
                        help="baseline metrics.json to compare against (default: tests/baselines/metrics.json)")
    parser.add_argument("--require-no-fallback", action="store_true",
                        help="fail if assets.fallback_used is true in the CURRENT run -- the "
                             "'shipped build silently reading the source tree' alarm. Meant for "
                             "a CI job running an INSTALLED build; every ordinary dev build "
                             "legitimately has fallback_used=true and will trip this if passed.")
    args = parser.parse_args()

    with open(args.current, encoding="utf-8") as f:
        current = json.load(f)

    breaches = []
    fallback_failure = None

    if args.require_no_fallback:
        fallback_used = get_path(current, "assets.fallback_used")
        if fallback_used is True:
            fallback_failure = (
                "assets.fallback_used is TRUE: this build is reading assets from a fallback "
                "location instead of its own packaged assets/ directory. If this is an "
                "installed/packaged build, that is the alarm this flag exists to raise."
            )

    try:
        with open(args.baseline, encoding="utf-8") as f:
            baseline = json.load(f)
    except FileNotFoundError:
        baseline = None

    if baseline is not None:
        for field, tolerance in TOLERANCE_FIELDS.items():
            b_val = get_path(baseline, field)
            c_val = get_path(current, field)
            if b_val is _MISSING or c_val is _MISSING:
                continue
            compare_tolerance(field, b_val, c_val, tolerance, breaches)

        for field in EXACT_FIELDS:
            b_val = get_path(baseline, field)
            c_val = get_path(current, field)
            if b_val is _MISSING or c_val is _MISSING:
                continue
            compare_exact(field, b_val, c_val, breaches)

        compare_state_hash(baseline, current, breaches)
    elif not args.require_no_fallback:
        print(f"metrics_diff: no baseline at '{args.baseline}' -- skipping threshold comparison",
              file=sys.stderr)

    if breaches:
        print(f"metrics_diff: {len(breaches)} breach(es) against '{args.baseline}':", file=sys.stderr)
        for field, b_val, c_val, reason in breaches:
            print(f"  {field}: baseline={b_val!r} current={c_val!r} -- {reason}", file=sys.stderr)

    if fallback_failure:
        print(f"metrics_diff: {fallback_failure}", file=sys.stderr)

    if breaches or fallback_failure:
        return 1

    print(f"metrics_diff: OK ({args.current} vs {args.baseline})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
