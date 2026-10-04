#!/usr/bin/env python3
"""Run each combination in its own process with cooperative solver deadlines."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import csv
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parent))
from select_test_instances import (
    apply_execution_defaults,
    resolve_instance_directory,
)


ROOT = Path(__file__).resolve().parents[1]
SOLVER = ROOT / "build" / "kdc-solver"
ALGORITHM_PHASES = (
    ("nn", "greedy"),
    ("primal-dual", "local-search", "sa", "genetic",
     "lp-rounding", "shifting"),
    ("ip-kont", "branch-and-bound"),
)
ALGORITHMS = tuple(
    algorithm for phase in ALGORITHM_PHASES for algorithm in phase
)
OBJECTIVES = ("minmax", "minsum")
CSV_COLUMNS = (
    "schema_version", "instance_name", "algorithm_name", "algorithm_category",
    "requested_backend", "actual_backend", "objective", "repeat", "n", "m",
    "wall_time_sec", "solve_time_sec", "verification_time_sec",
    "serialization_time_sec", "total_wall_time_sec", "cpu_time_sec",
    "peak_memory_mb", "time_limit_per_ip_sec", "global_time_limit_sec",
    "fast_time_limit_sec", "exact_time_limit_sec", "per_static_time_limit_sec",
    "objective_value", "peak_cost", "integral_cost",
    "empirical_ratio_to_exact", "ratio_to_incumbent",
    "lower_bound", "bound_status", "upper_bound", "certified_gap",
    "optimality_status", "refinement_policy", "minsum_refinement_policy",
    "exact_solver", "certified_lower_bound", "heuristic_lower_bound", "gap",
    "num_iterations", "num_static_solves", "num_ip_solves",
    "seed", "verify_each_iteration", "verify_after", "handovers_enabled",
    "candidate_count", "coverage_nnz", "verified", "feasible",
    "time_limited", "timeout", "failed", "error_message",
    "verification_kind",
    "git_commit", "compiler", "build_type", "thread_count", "experiment_id",
    "configuration", "external_timeout", "wrapper_wall_time_sec",
    "wrapper_error",
    "solution_json_path", "trace_csv_path",
    "result_json_path",
    "dataset_profile", "experiment_label",
)


def safe_name(value: str) -> str:
    return "".join(
        char if char.isalnum() or char in "-_." else "_"
        for char in value
    ).lstrip(".") or "unnamed"


def clear_previous_output(output: Path) -> None:
    runs = output / "runs"
    if runs.is_symlink() or runs.is_file():
        runs.unlink()
    elif runs.exists():
        shutil.rmtree(runs)
    for filename in ("master_results.json", "master_results.csv",
                     "batch_summary.md"):
        artifact = output / filename
        if artifact.is_symlink() or not artifact.is_dir():
            if artifact.exists() or artifact.is_symlink():
                artifact.unlink()
        elif artifact.exists():
            shutil.rmtree(artifact)


def order_algorithms(selected: set[str]) -> list[str]:
    return [algorithm for phase in ALGORITHM_PHASES
            for algorithm in phase if algorithm in selected]


def _write_record_paths(record: dict, output: Path) -> None:
    instance = safe_name(str(record["instance_name"]))
    algorithm = safe_name(str(record["algorithm_name"]))
    objective = safe_name(str(record["objective"]))
    run_dir = output / "runs" / instance / algorithm / objective
    repeat = int(record.get("repeat", 0))
    if repeat > 0:
        run_dir /= f"repeat-{repeat}"
    run_dir.mkdir(parents=True, exist_ok=True)
    record["result_json_path"] = str(run_dir / "result.json")
    record["trace_csv_path"] = str(run_dir / "trace.csv")
    record["solution_json_path"] = str(run_dir / "solution.json")
    (run_dir / "result.json").write_text(
        json.dumps(record, indent=2) + "\n", encoding="utf-8"
    )
    if not (run_dir / "trace.csv").exists():
        (run_dir / "trace.csv").write_text(
            "iter,t_max,objective,lower_bound,gap,wall_time,num_ip_solves\n",
            encoding="utf-8",
        )


def run_one(instance_path: Path, output: Path, algorithm: str,
            objective: str, per_ip_timeout: float,
            fast_time_limit: float, exact_time_limit: float,
            minsum_refinement_policy: str,
            exact_reference: str, profile: str, seed: int, repeat: int,
            safety_timeout: float | None) -> dict:
    source = json.loads(instance_path.read_text(encoding="utf-8"))
    instance = str(source.get("name") or instance_path.stem)
    n = len(source.get("trajectories", []))
    m = len(source.get("stations", []))
    started = time.monotonic()
    record = None
    error = ""
    with tempfile.TemporaryDirectory(prefix="kdc-one-run-") as tmp:
        tmp_path = Path(tmp)
        input_dir = tmp_path / "instances"
        input_dir.mkdir()
        (input_dir / "instance.json").write_text(
            json.dumps(source), encoding="utf-8"
        )
        job_output = tmp_path / "output"
        command = [
            str(SOLVER), "batch",
            "--instances", str(input_dir),
            "--output", str(job_output),
            "--algorithms", algorithm,
            "--modes", objective,
            "--time-limit", str(per_ip_timeout),
            "--fast-time-limit", str(fast_time_limit),
            "--exact-time-limit", str(exact_time_limit),
            "--minsum-refinement-policy", minsum_refinement_policy,
            "--exact-reference", exact_reference,
            "--profile", profile,
            "--seed", str(seed),
            "--repeats", "1",
        ]
        if algorithm != exact_reference:
            command.append("--allow-no-exact-reference")
        external_timeout = False
        try:
            completed = subprocess.run(
                command, cwd=ROOT, stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE, text=True, check=False,
                timeout=safety_timeout,
            )
        except subprocess.TimeoutExpired as timeout_error:
            external_timeout = True
            completed = SimpleNamespace(
                returncode=-1, stdout="", stderr=str(timeout_error)
            )
        master = job_output / "master_results.json"
        if master.is_file():
            rows = json.loads(master.read_text(encoding="utf-8"))
            record = next(
                (row for row in rows
                 if row.get("algorithm_name") == algorithm),
                None,
            )
        if completed.returncode != 0 or record is None:
            error = (completed.stderr or "").strip()[-2000:]
            if not error:
                error = f"solver exited with status {completed.returncode}"

        elapsed = time.monotonic() - started
        if record is not None:
            record["repeat"] = repeat
            record["external_timeout"] = external_timeout
            record["wrapper_wall_time_sec"] = elapsed
            record["wrapper_error"] = error
            job_run = (job_output / "runs" / safe_name(instance) /
                       algorithm / objective)
            final_run = (output / "runs" / safe_name(instance) /
                         algorithm / objective)
            if repeat > 0:
                job_run /= f"repeat-{repeat}"
                final_run /= f"repeat-{repeat}"
            if job_run.is_dir():
                final_run.parent.mkdir(parents=True, exist_ok=True)
                shutil.copytree(job_run, final_run, dirs_exist_ok=True)
            record["wrapper_wall_time_sec"] = elapsed
            record["fast_time_limit_sec"] = fast_time_limit
            record["exact_time_limit_sec"] = exact_time_limit
            if error:
                record["wrapper_error"] = error
            _write_record_paths(record, output)
            return record

    record = {
        "schema_version": 2,
        "instance_name": instance,
        "algorithm_name": algorithm,
        "algorithm_category": (
            "exact_reference"
            if algorithm in {"ip-kont", "branch-and-bound"}
            else "heuristic"
        ),
        "requested_backend": exact_reference,
        "actual_backend": None,
        "objective": objective,
        "repeat": repeat,
        "n": n,
        "m": m,
        "wall_time_sec": elapsed,
        "solve_time_sec": None,
        "verification_time_sec": None,
        "serialization_time_sec": None,
        "total_wall_time_sec": None,
        "cpu_time_sec": 0.0,
        "peak_memory_mb": 0.0,
        "time_limit_per_ip_sec": per_ip_timeout,
        "global_time_limit_sec": (
            exact_time_limit if algorithm == exact_reference
            else fast_time_limit
        ),
        "per_static_time_limit_sec": per_ip_timeout,
        "fast_time_limit_sec": fast_time_limit,
        "exact_time_limit_sec": exact_time_limit,
        "objective_value": None,
        "peak_cost": None,
        "integral_cost": None,
        "empirical_ratio_to_exact": None,
        "ratio_to_incumbent": None,
        "lower_bound": None,
        "bound_status": "NONE",
        "upper_bound": None,
        "certified_gap": None,
        "optimality_status": None,
        "refinement_policy": (
            "CERTIFIED_BOUND"
            if minsum_refinement_policy == "sampled"
            else "HEURISTIC_ADAPTIVE"
        ),
        "minsum_refinement_policy": (
            "CERTIFIED_BOUND"
            if minsum_refinement_policy == "sampled"
            else "HEURISTIC_ADAPTIVE"
        ),
        "exact_solver": False,
        "certified_lower_bound": None,
        "heuristic_lower_bound": None,
        "gap": None,
        "num_iterations": 0,
        "num_static_solves": 0,
        "num_ip_solves": 0,
        "verified": False,
        "feasible": False,
        "time_limited": False,
        "timeout": False,
        "failed": bool(error and not external_timeout),
        "error_message": "",
        "external_timeout": external_timeout,
        "wrapper_wall_time_sec": elapsed,
        "wrapper_error": error or "solver produced no result record",
        "seed": seed,
        "verify_each_iteration": profile == "debug",
        "verify_after": True,
        "handovers_enabled": True,
        "candidate_count": None,
        "coverage_nnz": None,
        "git_commit": None,
        "compiler": None,
        "build_type": None,
        "thread_count": None,
        "experiment_id": None,
        "configuration": {},
    }
    _write_record_paths(record, output)
    return record


def write_outputs(records: list[dict], output: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    exact_results = {}
    incumbents = {}
    for row in records:
        if not row.get("feasible") or row.get("objective_value") is None:
            continue
        key = (row["instance_name"], row["objective"])
        value = float(row["objective_value"])
        incumbents[key] = min(incumbents.get(key, value), value)
        if (row.get("algorithm_category") == "exact_reference" and
                row.get("optimality_status") == "OPTIMAL"):
            exact_results[key] = value
    for row in records:
        row["empirical_ratio_to_exact"] = None
        row["ratio_to_incumbent"] = None
        if not row.get("feasible") or row.get("objective_value") is None:
            continue
        key = (row["instance_name"], row["objective"])
        value = float(row["objective_value"])
        exact = exact_results.get(key)
        if exact is not None:
            row["empirical_ratio_to_exact"] = (
                value / exact if exact > 0.0 else (1.0 if value <= 1e-12 else None)
            )
        incumbent = incumbents.get(key)
        if incumbent is not None:
            row["ratio_to_incumbent"] = (
                value / incumbent
                if incumbent > 0.0 else (1.0 if value <= 1e-12 else None)
            )
    records.sort(key=lambda row: (
        row["instance_name"], row["algorithm_name"], row["objective"],
        row.get("repeat", 0)
    ))
    (output / "master_results.json").write_text(
        json.dumps(records, indent=2) + "\n", encoding="utf-8"
    )
    with (output / "master_results.csv").open(
        "w", newline="", encoding="utf-8"
    ) as file:
        writer = csv.DictWriter(file, fieldnames=CSV_COLUMNS)
        writer.writeheader()
        writer.writerows(records)
    successful = sum(bool(row["feasible"]) for row in records)
    lines = [
        ("# Smoke / development validation batch summary"
         if records and records[0].get("dataset_profile") == "smoke10"
         else "# Batch Run Summary"), "",
        f"- **Total runs:** {len(records)}",
        f"- **Successful runs:** {successful}",
        f"- **Failed runs:** {len(records) - successful}", "",
        "Solver work uses cooperative global deadlines. The optional external "
        "safety timeout is recorded separately and does not redefine solver "
        "scientific statuses.", "",
        "| Instance | Algorithm | Objective | Wall time (s) | Status |",
        "|---|---|---|---:|---|",
    ]
    for row in records:
        status = "ok" if row["feasible"] else row["error_message"]
        lines.append(
            f"| {row['instance_name']} | {row['algorithm_name']} | "
            f"{row['objective']} | {row['wall_time_sec']:.3f} | {status} |"
        )
    (output / "batch_summary.md").write_text(
        "\n".join(lines) + "\n", encoding="utf-8"
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--instances", default="/tmp/kdc-mdc-json")
    parser.add_argument("--dataset-profile", choices=("full", "smoke10"),
                        default="full")
    parser.add_argument("--dataset-manifest")
    parser.add_argument("--output", default="results/batch")
    parser.add_argument("--algorithms", default="all")
    parser.add_argument("--profile", choices=("fast", "exact-reference", "debug"),
                        default="fast")
    parser.add_argument("--seed", type=int, default=42)
    parser.add_argument("--repeats", type=int, default=1)
    parser.add_argument(
        "--safety-timeout", type=float, default=None,
        help="optional external subprocess safety timeout; not a solver limit",
    )
    parser.add_argument("--modes", choices=("minmax", "minsum", "both"),
                        default="both")
    parser.add_argument("--exact-reference", choices=("ip-kont", "branch-and-bound", "auto"),
                        default="auto",
                        help="exact backend used in the reference slot")
    parser.add_argument(
        "--threads", type=int, default=None,
        help="parallel worker count (smoke default: 2; full default: all cores)"
    )
    parser.add_argument("--time-limit", type=float, default=None,
                        help="limit per static/IP subsolve (default: 60)")
    parser.add_argument("--fast-time-limit", type=float, default=None,
                        help="global seconds per fast algorithm run (default: 30)")
    parser.add_argument("--exact-time-limit", type=float, default=None,
                        help="global seconds per exact algorithm run (default: 600)")
    parser.add_argument(
        "--minsum-refinement-policy", choices=("adaptive", "sampled"),
        default="adaptive",
        help="MinSum refinement policy (default: adaptive)",
    )
    args = parser.parse_args(argv)
    if not SOLVER.is_file():
        parser.error(f"solver executable not found: {SOLVER}")
    instances_root = Path(args.instances).resolve()
    if not instances_root.is_dir():
        parser.error(f"instance directory not found: {instances_root}")
    try:
        resolved_instance_root = resolve_instance_directory(instances_root)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        parser.error(f"invalid dataset manifest: {error}")
    if resolved_instance_root != instances_root:
        if args.dataset_manifest is None:
            args.dataset_manifest = str(instances_root / "manifest.json")
        instances_root = resolved_instance_root
        if args.dataset_profile == "full":
            args.dataset_profile = "smoke10"
    elif (args.dataset_manifest is None
          and instances_root.name == "instances"
          and (instances_root.parent / "manifest.json").is_file()):
        args.dataset_manifest = str(instances_root.parent / "manifest.json")
    if args.dataset_profile == "smoke10" and not args.dataset_manifest:
        parser.error("smoke10 requires the selection manifest for provenance")
    apply_execution_defaults(args)
    if (args.threads <= 0 or args.time_limit <= 0 or
            args.fast_time_limit <= 0 or args.exact_time_limit <= 0 or
            args.repeats <= 0 or args.seed < 0 or
            (args.safety_timeout is not None and args.safety_timeout <= 0)):
        parser.error("threads, repeats, and time limits must be positive; seed must be nonnegative")
    instance_paths = sorted(instances_root.rglob("*.json"))
    if not instance_paths:
        parser.error("no canonical JSON instances found")
    if args.dataset_manifest:
        try:
            selection = json.loads(
                Path(args.dataset_manifest).read_text(encoding="utf-8")
            )
        except (OSError, json.JSONDecodeError) as error:
            parser.error(f"invalid dataset selection manifest: {error}")
        if not isinstance(selection, dict) or not isinstance(
            selection.get("instances"), list
        ):
            parser.error("dataset manifest must contain an instances array")
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
                or len(instance_paths) != selected_count):
            parser.error(
                "dataset selection manifest count/uniqueness does not match "
                f"the {len(instance_paths)} canonical instance files"
            )
    requested = [value.strip() for value in args.algorithms.split(",")
                 if value.strip()]
    if "ip-kont" in requested and "branch-and-bound" in requested:
        parser.error(
            "strict benchmark mode cannot select both exact backends"
        )
    all_requested = not requested or any(
        value.lower() in {"all", "all-fast", "all-comparison"}
        for value in requested
    )
    selected = set(ALGORITHMS) if all_requested else set(requested)
    output = Path(args.output)
    if not output.is_absolute():
        output = ROOT / output
    clear_previous_output(output)
    output.mkdir(parents=True, exist_ok=True)

    calibration = subprocess.run(
        [
            str(SOLVER), "calibrate",
            "--dataset", str(instances_root),
            "--output", str(output),
            "--exact-reference", args.exact_reference,
        ],
        cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        text=True, check=False,
    )
    manifest_path = output / "experiment_manifest.json"
    if calibration.returncode != 0 or not manifest_path.is_file():
        parser.error(
            "exact-reference calibration failed: "
            + (calibration.stderr or calibration.stdout).strip()[-2000:]
        )
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    exact_reference = manifest.get("selected_backend")
    if exact_reference not in ("ip-kont", "branch-and-bound"):
        parser.error("calibration manifest has no valid selected backend")
    if args.profile == "exact-reference":
        selected = {exact_reference}
    elif all_requested:
        selected = set(ALGORITHMS)
    if exact_reference == "ip-kont":
        selected.discard("branch-and-bound")
    else:
        selected.discard("ip-kont")
    selected.discard("brute-force")
    selected.add(exact_reference)
    unknown = selected - set(ALGORITHMS)
    if unknown:
        parser.error("unknown algorithms: " + ", ".join(sorted(unknown)))
    algorithms = order_algorithms(selected)
    objectives = OBJECTIVES if args.modes == "both" else (args.modes,)
    records = []
    total = sum(
        1 if algorithm == exact_reference else args.repeats
        for _path in instance_paths
        for algorithm in algorithms
    ) * len(objectives)
    print(f"{len(instance_paths)} instances × {len(algorithms)} algorithms "
          f"× {len(objectives)} modes = {total} combinations; "
          f"{args.fast_time_limit:g}s fast / {args.exact_time_limit:g}s exact "
          "cooperative deadline.", flush=True)
    complete = 0
    with ThreadPoolExecutor(max_workers=args.threads) as executor:
        for phase in ALGORITHM_PHASES:
            phase_algorithms = [algorithm for algorithm in algorithms
                                if algorithm in phase]
            futures = [
                executor.submit(
                    run_one, path, output, algorithm, objective,
                    args.time_limit, args.fast_time_limit,
                    args.exact_time_limit,
                    args.minsum_refinement_policy,
                    exact_reference,
                    args.profile,
                    args.seed + repeat,
                    repeat,
                    args.safety_timeout,
                )
                for path in instance_paths
                for algorithm in phase_algorithms
                for objective in objectives
                for repeat in range(
                    1 if algorithm == exact_reference else args.repeats
                )
            ]
            for future in as_completed(futures):
                record = future.result()
                records.append(record)
                complete += 1
                if complete % 50 == 0 or complete == total:
                    print(f"Completed {complete}/{total} "
                          f"({sum(bool(row['feasible']) for row in records)} "
                          "feasible)", flush=True)
    experiment_id = (
        "kdc-limited-" + time.strftime("%Y%m%dT%H%M%SZ", time.gmtime())
        + f"-seed{args.seed}"
    )
    manifest.update({
        "experiment_id": experiment_id,
        "timestamp": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "git_commit": records[0].get("git_commit") if records else None,
        "compiler": records[0].get("compiler") if records else None,
        "build_type": records[0].get("build_type") if records else None,
        "hardware": {
            "architecture": __import__("platform").machine(),
            "logical_cpu_count": os.cpu_count(),
            "thread_count": args.threads,
        },
        "algorithm_set": algorithms,
        "benchmark_profile": args.profile.upper().replace("-", "_"),
        "dataset": {
            "path": str(instances_root),
            "identity": manifest.get("dataset_fingerprint"),
            "fingerprint_algorithm":
                manifest.get("dataset_fingerprint_algorithm"),
        },
        "dataset_profile": args.dataset_profile,
        "dataset_label": (
            "smoke / development validation"
            if args.dataset_profile == "smoke10"
            else "scientific benchmark dataset"
        ),
        "selected_instance_count": len(instance_paths),
        "selection_manifest": str(Path(args.dataset_manifest).resolve())
        if args.dataset_manifest else None,
        "time_limits": {
            "fast_global_sec": args.fast_time_limit,
            "exact_global_sec": args.exact_time_limit,
            "per_static_solve_sec": args.time_limit,
            "external_safety_timeout_sec": args.safety_timeout,
        },
        "seed_policy": {
            "base_seed": args.seed,
            "description":
                "fixed base seed with deterministic per-run variation; "
                "exact reference runs once per instance/objective",
            "repeats": args.repeats,
        },
        "refinement_policy": args.minsum_refinement_policy,
        "verification_policy": {
            "verify_after": True,
            "verify_each_iteration": args.profile == "debug",
            "verification_type": "certified_continuous",
        },
        "cache_policy": "auto",
        "handovers_enabled": True,
    })
    for record in records:
        record["experiment_id"] = experiment_id
        record["dataset_profile"] = args.dataset_profile
        record["experiment_label"] = manifest["dataset_label"]
    if args.dataset_manifest:
        selection = json.loads(
            Path(args.dataset_manifest).read_text(encoding="utf-8")
        )
        manifest["selection_policy"] = selection.get("selection_policy")
        manifest["source_dataset_fingerprint"] = selection.get(
            "source_dataset_fingerprint"
        )
        manifest["selected_files"] = [
            {
                "family": row.get("family"),
                "source": row.get("source"),
                "n": row.get("n"),
                "m": row.get("m"),
                "selection_rank": row.get("selection_rank"),
            }
            for row in selection.get("instances", [])
        ]
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n",
                             encoding="utf-8")
    write_outputs(records, output)
    print(f"Wrote results to {output}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
