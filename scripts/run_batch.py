#!/usr/bin/env python3
"""Run the batch workflow after a successful pre-flight check."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import re
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]
PREFLIGHT_REPORT = ROOT / "results" / "preflight" / "preflight_report.md"
SOLVER = ROOT / "build" / "kdc-solver"


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
    parser.add_argument("--output", default="results/batch",
                        help="batch output directory")
    parser.add_argument("--algorithms", default="",
                        help="solver names or all-fast/all-comparison")
    parser.add_argument("--profile", choices=("fast", "exact-reference", "debug"),
                        default="fast")
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--repeats", type=int, default=1)
    parser.add_argument("--modes", choices=("minmax", "minsum", "both"),
                        default="both")
    parser.add_argument("--exact-reference", choices=("ip-kont", "branch-and-bound", "auto"),
                        default="auto",
                        help="exact backend used in the reference slot (default: auto)")
    parser.add_argument("--parallel", action="store_true",
                        help="run work items concurrently")
    parser.add_argument(
        "--threads", type=int, default=max(1, os.cpu_count() or 1),
        help="parallel worker count (default: available CPU cores)"
    )
    parser.add_argument("--time-limit", type=float, default=60.0,
                        help="maximum seconds per static/IP solve (default: 60)")
    parser.add_argument("--fast-time-limit", type=float, default=30.0,
                        help="global seconds per fast algorithm run (default: 30)")
    parser.add_argument("--exact-time-limit", type=float, default=600.0,
                        help="global seconds per exact algorithm run (default: 600)")
    parser.add_argument(
        "--minsum-refinement-policy", choices=("adaptive", "sampled"),
        default="adaptive",
        help="MinSum refinement policy (default: adaptive)",
    )
    parser.add_argument("--force", action="store_true",
                        help="run even if the preflight report is missing or failed")
    args = parser.parse_args()
    if (args.time_limit <= 0 or args.fast_time_limit <= 0 or
            args.exact_time_limit <= 0 or args.repeats <= 0 or args.seed < 0):
        parser.error("time limits and repeats must be positive; seed must be nonnegative")

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

    command = [
        str(SOLVER), "batch",
        "--instances", args.instances,
        "--output", args.output,
        "--modes", args.modes,
        "--time-limit", str(args.time_limit),
        "--fast-time-limit", str(args.fast_time_limit),
        "--exact-time-limit", str(args.exact_time_limit),
        "--minsum-refinement-policy", args.minsum_refinement_policy,
        "--exact-reference", args.exact_reference,
        "--profile", args.profile,
        "--seed", str(args.seed),
        "--repeats", str(args.repeats),
    ]
    requested_algorithms = [
        name.strip() for name in args.algorithms.split(",") if name.strip()
    ]
    if requested_algorithms and not any(
        name.lower() == "all" for name in requested_algorithms
    ):
        command.extend(["--algorithms", ",".join(requested_algorithms)])
    if args.parallel:
        command.extend(["--parallel", "--threads", str(args.threads)])

    print("+", " ".join(command), flush=True)
    completed = subprocess.run(command, cwd=ROOT, check=False)
    if completed.returncode != 0:
        return completed.returncode

    output = Path(args.output)
    if not output.is_absolute():
        output = ROOT / output
    master_results = output / "master_results.json"
    batch_summary = output / "batch_summary.md"
    if not master_results.is_file() or not batch_summary.is_file():
        print("error: batch command did not produce master results and summary",
              file=sys.stderr)
        return 1
    print(f"Master results: {master_results}")
    print(f"Batch summary: {batch_summary}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
