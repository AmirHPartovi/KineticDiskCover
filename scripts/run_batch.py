#!/usr/bin/env python3
"""Run the batch workflow after a successful pre-flight check."""

from __future__ import annotations

import argparse
import csv
import os
import json
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


def _selection_metadata(path: str | None) -> tuple[dict, str | None]:
    if not path:
        return {}, None
    manifest_path = Path(path).resolve()
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if not isinstance(manifest, dict) or not isinstance(
        manifest.get("instances"), list
    ):
        raise ValueError("dataset manifest must contain an instances array")
    return manifest, str(manifest_path)


def annotate_dataset(output: Path, profile: str, manifest_path: str | None,
                     instance_count: int) -> None:
    selection, resolved_manifest = _selection_metadata(manifest_path)
    results_path = output / "master_results.json"
    records = json.loads(results_path.read_text(encoding="utf-8"))
    for record in records:
        record["dataset_profile"] = profile
        record["experiment_label"] = (
            "smoke / development validation" if profile == "smoke10"
            else "scientific benchmark dataset"
        )
    results_path.write_text(
        json.dumps(records, indent=2) + "\n", encoding="utf-8"
    )
    csv_path = output / "master_results.csv"
    if csv_path.is_file():
        with csv_path.open(newline="", encoding="utf-8") as source:
            reader = csv.DictReader(source)
            fields = list(reader.fieldnames or [])
            rows = list(reader)
        for field in ("dataset_profile", "experiment_label"):
            if field not in fields:
                fields.append(field)
        with csv_path.open("w", newline="", encoding="utf-8") as target:
            writer = csv.DictWriter(target, fieldnames=fields)
            writer.writeheader()
            for row in rows:
                row.update({
                    "dataset_profile": profile,
                    "experiment_label": (
                        "smoke / development validation"
                        if profile == "smoke10"
                        else "scientific benchmark dataset"
                    ),
                })
                writer.writerow(row)
    batch_manifest_path = output / "experiment_manifest.json"
    batch_manifest = json.loads(
        batch_manifest_path.read_text(encoding="utf-8")
    )
    selected_files = [
        {
            "family": row.get("family"),
            "source": row.get("source"),
            "n": row.get("n"),
            "m": row.get("m"),
            "selection_rank": row.get("selection_rank"),
        }
        for row in selection.get("instances", [])
    ]
    batch_manifest.update({
        "dataset_profile": profile,
        "dataset_label": (
            "smoke / development validation" if profile == "smoke10"
            else "scientific benchmark dataset"
        ),
        "selected_instance_count": instance_count,
        "selection_policy": selection.get("selection_policy"),
        "source_dataset_fingerprint": selection.get(
            "source_dataset_fingerprint"
        ),
        "selection_manifest": resolved_manifest,
        "selected_files": selected_files,
    })
    batch_manifest_path.write_text(
        json.dumps(batch_manifest, indent=2) + "\n", encoding="utf-8"
    )
    if profile == "smoke10":
        summary_path = output / "batch_summary.md"
        summary = summary_path.read_text(encoding="utf-8")
        summary_path.write_text(
            "# Smoke / development validation batch summary\n\n"
            "This is a smoke dataset for debugging and integration testing; "
            "it is not the full scientific benchmark.\n\n"
            + summary.split("\n", 1)[-1],
            encoding="utf-8",
        )


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--instances", default="data/instances",
                        help="instance directory")
    parser.add_argument("--dataset-profile", choices=("full", "smoke10"),
                        default="full")
    parser.add_argument("--dataset-manifest",
                        help="selection manifest describing a smoke dataset")
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
    parser.add_argument("--force", action="store_true",
                        help="run even if the preflight report is missing or failed")
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
    if args.dataset_profile == "smoke10" and not args.dataset_manifest:
        parser.error("smoke10 requires the selection manifest for provenance")
    if args.dataset_manifest:
        try:
            selection, _ = _selection_metadata(args.dataset_manifest)
        except (OSError, ValueError, json.JSONDecodeError) as error:
            parser.error(f"invalid dataset selection manifest: {error}")
        instance_count = len(list(Path(args.instances).rglob("*.json")))
        selected_count = int(selection.get(
            "selected_instance_count", len(selection["instances"])
        ))
        sources = [
            row.get("source") for row in selection["instances"]
            if isinstance(row, dict)
        ]
        if (len(selection["instances"]) != selected_count
                or len(sources) != selected_count
                or len(set(sources)) != selected_count
                or instance_count != selected_count):
            parser.error(
                "dataset selection manifest count/uniqueness does not match "
                f"the {instance_count} canonical instance files"
            )
    apply_execution_defaults(args)
    if (args.threads <= 0 or args.time_limit <= 0
            or args.fast_time_limit <= 0 or args.exact_time_limit <= 0
            or args.repeats <= 0 or args.seed < 0):
        parser.error("threads, time limits, and repeats must be positive; seed must be nonnegative")

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
    try:
        annotate_dataset(output, args.dataset_profile, args.dataset_manifest,
                         len(list(Path(args.instances).rglob("*.json"))))
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"error: failed to annotate dataset profile: {error}",
              file=sys.stderr)
        return 1
    print(f"Dataset profile: {args.dataset_profile}")
    print(f"Selected instances: {len(list(Path(args.instances).rglob('*.json')))}")
    print(f"Master results: {master_results}")
    print(f"Batch summary: {batch_summary}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
