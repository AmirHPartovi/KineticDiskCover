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
SOLVER = ROOT / "build" / "kdc-solver"
sys.path.insert(0, str(ROOT / "scripts"))
from select_test_instances import (
    _validate_selection_identity,
    apply_execution_defaults,
    resolve_instance_directory,
)


def preflight_passed(report: Path | None) -> bool:
    if report is None or not report.is_file():
        return False
    contents = report.read_text(encoding="utf-8")
    match = re.search(
        r"\*\*Summary:\*\* (\d+) / (\d+) checks passed \(0 failed\)", contents
    )
    return match is not None and int(match.group(1)) == int(match.group(2))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--instances", default=str(ROOT / "data" / "instances"),
                        help="instance directory")
    parser.add_argument("--dataset-profile", choices=("full", "smoke20"),
                        default="full")
    parser.add_argument("--dataset-manifest",
                        help="selection manifest describing a smoke dataset")
    parser.add_argument(
        "--output", default=None,
        help="batch compatibility-export directory (defaults to a unique experiment)",
    )
    parser.add_argument(
        "--preflight-report", type=Path,
        help="experiment-scoped preflight report to validate",
    )
    parser.add_argument("--algorithms", default="",
                        help="solver names or all-fast/all-comparison/all-registered")
    parser.add_argument("--plan-only", action="store_true",
                        help="write the run plan without executing solver slots")
    parser.add_argument("--profile", choices=("fast", "exact-reference", "debug"),
                        default="fast")
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--repeats", type=int, default=1)
    parser.add_argument("--modes",
                        choices=("minmax", "minsum", "minmaxsum", "both", "all"),
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
    parser.add_argument("--brute-force-max-assignments", type=int, default=100_000)
    parser.add_argument("--brute-force-time-limit-sec", type=float, default=0.5)
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
            args.dataset_profile = "smoke20"
    if args.dataset_manifest:
        try:
            selection = json.loads(Path(args.dataset_manifest).read_text(
                encoding="utf-8"
            ))
        except (OSError, json.JSONDecodeError) as error:
            parser.error(f"invalid selection manifest: {error}")
        if not isinstance(selection, dict) or not isinstance(
                selection.get("instances"), list):
            parser.error("selection manifest must contain an instances array")
        selection_name = selection.get("name", selection.get("profile"))
        if selection_name == "smoke10":
            parser.error(
                "legacy smoke10 manifests cannot be relabeled; regenerate "
                "selection evidence for smoke20"
            )
        if args.dataset_profile == "smoke20":
            try:
                _validate_selection_identity(selection, 20, 50, 25)
            except ValueError as error:
                parser.error(f"invalid smoke20 selection manifest: {error}")
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
            or args.minsum_budget_fraction <= 0
            or args.brute_force_max_assignments <= 0
            or not math.isfinite(args.brute_force_time_limit_sec)
            or args.brute_force_time_limit_sec <= 0):
        parser.error("threads, time limits, and repeats must be positive; seed must be nonnegative")
    if (args.modes in {"minmaxsum", "all"}
            and abs(args.minmax_budget_fraction
                    + args.minsum_budget_fraction - 1.0) > 1e-9):
        parser.error("MinMaxSum budget fractions must sum to one")

    preflight_report = args.preflight_report
    if preflight_report is not None and not preflight_report.is_absolute():
        preflight_report = ROOT / preflight_report
    if not preflight_passed(preflight_report):
        print(f"warning: preflight is missing or did not pass: "
              f"{preflight_report or '<no --preflight-report provided>'}",
              file=sys.stderr)
        if not args.force:
            print("Provide a passing experiment-scoped --preflight-report, or pass --force.",
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
        "--brute-force-max-assignments",
        str(args.brute_force_max_assignments),
        "--brute-force-time-limit-sec", str(args.brute_force_time_limit_sec),
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
    if args.plan_only:
        batch_args.append("--plan-only")
    sys.path.insert(0, str(ROOT / "scripts"))
    from run_batch_limited import main as run_immutable_batch

    return run_immutable_batch(batch_args)


if __name__ == "__main__":
    sys.exit(main())
