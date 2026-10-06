#!/usr/bin/env python3
"""Run the batch workflow after a successful pre-flight check."""

from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
PREFLIGHT_REPORT = ROOT / "results" / "preflight" / "preflight_report.md"
SOLVER = ROOT / "build" / "kdc-solver"
sys.path.insert(0, str(ROOT / "scripts"))
from select_test_instances import (
    apply_execution_defaults,
    resolve_instance_directory,
)


def preflight_passed() -> bool:
    if not PREFLIGHT_REPORT.is_file():
        return False
    contents = PREFLIGHT_REPORT.read_text(encoding="utf-8")
    match = re.search(
        r"\*\*Summary:\*\* (\d+) / (\d+) checks passed \(0 failed\)", contents
    )
    return match is not None and int(match.group(1)) == int(match.group(2))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--instances", default="data/instances",
                        help="instance directory")
    parser.add_argument("--dataset-profile", choices=("full", "smoke10"),
                        default="full")
    parser.add_argument("--dataset-manifest",
                        help="selection manifest describing a smoke dataset")
    parser.add_argument(
        "--output", default=None,
        help="batch compatibility-export directory (defaults to a unique experiment)",
    )
    parser.add_argument("--algorithms", default="",
                        help="solver names or all-fast/all-comparison")
    parser.add_argument("--profile", choices=("fast", "exact-reference", "debug"),
                        default="fast")
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--repeats", type=int, default=1)
    parser.add_argument("--modes",
                        choices=("minmax", "minsum", "minmaxsum", "both"),
                        default="both")
    parser.add_argument("--exact-reference", choices=("ip-kont", "branch-and-bound", "auto"),
                        default="auto",
                        help="exact backend used in the reference slot (default: auto)")
    parser.add_argument("--parallel", action="store_true",
                        help="run work items concurrently")
    parser.add_argument(
        "--threads", type=int, default=None,
        help="parallel worker count (smoke default: 2; full default: all cores)"
    )
    parser.add_argument("--time-limit", type=float, default=None,
                        help="maximum seconds per static/IP solve (default: 60)")
    parser.add_argument("--fast-time-limit", type=float, default=None,
                        help="global seconds per fast algorithm run (default: 30)")
    parser.add_argument("--exact-time-limit", type=float, default=None,
                        help="global seconds per exact algorithm run (default: 600)")
    parser.add_argument(
        "--minsum-refinement-policy", choices=("adaptive", "sampled"),
        default="adaptive",
        help="MinSum refinement policy (default: adaptive)",
    )
    parser.add_argument("--minmax-budget-fraction", type=float, default=0.5)
    parser.add_argument("--minsum-budget-fraction", type=float, default=0.5)
    parser.add_argument("--force", action="store_true",
                        help="run even if the preflight report is missing or failed")
    parser.add_argument("--save-solutions", action="store_true")
    parser.add_argument("--save-traces", action="store_true")
    parser.add_argument("--resume", action="store_true")
    args = parser.parse_args()
    instance_root = Path(args.instances)
    try:
        resolved_instance_root = resolve_instance_directory(instance_root)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(f"invalid dataset manifest: {error}")
    if resolved_instance_root != instance_root:
        if args.dataset_manifest is None:
            args.dataset_manifest = str(instance_root / "manifest.json")
        args.instances = str(resolved_instance_root)
        if args.dataset_profile == "full":
            args.dataset_profile = "smoke10"
    elif (args.dataset_manifest is None
          and instance_root.name == "instances"
          and (instance_root.parent / "manifest.json").is_file()):
        args.dataset_manifest = str(instance_root.parent / "manifest.json")
    apply_execution_defaults(args)
    if (args.threads <= 0 or args.time_limit <= 0
            or args.fast_time_limit <= 0 or args.exact_time_limit <= 0
            or args.repeats <= 0 or args.seed < 0
            or not math.isfinite(args.minmax_budget_fraction)
            or not math.isfinite(args.minsum_budget_fraction)
            or args.minmax_budget_fraction <= 0
            or args.minsum_budget_fraction <= 0):
        parser.error("threads, time limits, and repeats must be positive; seed must be nonnegative")
    if (args.modes == "minmaxsum"
            and abs(args.minmax_budget_fraction
                    + args.minsum_budget_fraction - 1.0) > 1e-9):
        parser.error("MinMaxSum budget fractions must sum to one")

    if not preflight_passed():
        print(f"warning: preflight is missing or did not pass: "
              f"{PREFLIGHT_REPORT}", file=sys.stderr)
        if not args.force:
            print("Run scripts/preflight_check.py first, or pass --force.",
                  file=sys.stderr)
            return 1

    if not SOLVER.is_file():
        print(f"error: solver executable not found: {SOLVER}", file=sys.stderr)
        return 1

    batch_args = [
        "--instances", str(resolved_instance_root),
        "--dataset-profile", args.dataset_profile,
        "--algorithms", args.algorithms or "all",
        "--profile", args.profile,
        "--seed", str(args.seed),
        "--repeats", str(args.repeats),
        "--modes", args.modes,
        "--exact-reference", args.exact_reference,
        "--threads", str(args.threads),
        "--time-limit", str(args.time_limit),
        "--fast-time-limit", str(args.fast_time_limit),
        "--exact-time-limit", str(args.exact_time_limit),
        "--minsum-refinement-policy", args.minsum_refinement_policy,
        "--minmax-budget-fraction", str(args.minmax_budget_fraction),
        "--minsum-budget-fraction", str(args.minsum_budget_fraction),
    ]
    if args.output:
        batch_args.extend(["--output", args.output])
    if args.dataset_manifest:
        batch_args.extend(["--dataset-manifest", args.dataset_manifest])
    if args.save_solutions:
        batch_args.append("--save-solutions")
    if args.save_traces:
        batch_args.append("--save-traces")
    if args.resume:
        batch_args.append("--resume")
    sys.path.insert(0, str(ROOT / "scripts"))
    from run_batch_limited import main as run_immutable_batch

    return run_immutable_batch(batch_args)


if __name__ == "__main__":
    sys.exit(main())
