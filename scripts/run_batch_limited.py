#!/usr/bin/env python3
"""Run each instance/algorithm/objective in a process with a wall-time limit."""

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
    "instance_name", "algorithm_name", "objective", "n", "m",
    "wall_time_sec", "cpu_time_sec", "peak_memory_mb",
    "time_limit_per_ip_sec", "run_timeout_sec", "objective_value",
    "lower_bound", "gap", "num_iterations", "num_ip_solves", "verified",
    "feasible", "error_message", "solution_json_path", "trace_csv_path",
    "result_json_path",
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
            objective: str, run_timeout: float,
            per_ip_timeout: float) -> dict:
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
            "--exact-reference", "auto",
        ]
        try:
            completed = subprocess.run(
                command, cwd=ROOT, stdout=subprocess.DEVNULL,
                stderr=subprocess.PIPE, text=True, timeout=run_timeout,
                check=False,
            )
            master = job_output / "master_results.json"
            if master.is_file():
                rows = json.loads(master.read_text(encoding="utf-8"))
                if rows:
                    record = rows[0]
            if completed.returncode != 0 or record is None:
                error = (completed.stderr or "").strip()[-2000:]
                if not error:
                    error = f"solver exited with status {completed.returncode}"
        except subprocess.TimeoutExpired:
            error = f"run timed out after {run_timeout:g} seconds"

        elapsed = min(time.monotonic() - started, run_timeout)
        if record is not None:
            job_run = job_output / "runs" / safe_name(instance) / algorithm / objective
            final_run = output / "runs" / safe_name(instance) / algorithm / objective
            if job_run.is_dir():
                final_run.parent.mkdir(parents=True, exist_ok=True)
                shutil.copytree(job_run, final_run, dirs_exist_ok=True)
            record["wall_time_sec"] = elapsed
            record["time_limit_per_ip_sec"] = per_ip_timeout
            record["run_timeout_sec"] = run_timeout
            if error:
                record["feasible"] = False
                record["verified"] = False
                record["error_message"] = error
            _write_record_paths(record, output)
            return record

    record = {
        "instance_name": instance,
        "algorithm_name": algorithm,
        "objective": objective,
        "n": n,
        "m": m,
        "wall_time_sec": elapsed,
        "cpu_time_sec": 0.0,
        "peak_memory_mb": 0.0,
        "time_limit_per_ip_sec": per_ip_timeout,
        "run_timeout_sec": run_timeout,
        "objective_value": 0.0,
        "lower_bound": 0.0,
        "gap": 0.0,
        "num_iterations": 0,
        "num_ip_solves": 0,
        "verified": False,
        "feasible": False,
        "error_message": error or "solver produced no result record",
    }
    _write_record_paths(record, output)
    return record


def write_outputs(records: list[dict], output: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    records.sort(key=lambda row: (
        row["instance_name"], row["algorithm_name"], row["objective"]
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
        "# Batch Run Summary", "",
        f"- **Total runs:** {len(records)}",
        f"- **Successful runs:** {successful}",
        f"- **Failed runs:** {len(records) - successful}", "",
        "Every instance/algorithm/objective process was capped at "
        f"{records[0]['run_timeout_sec'] if records else 0:g} seconds.", "",
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
    parser.add_argument("--output", default="results/batch")
    parser.add_argument("--algorithms", default="all")
    parser.add_argument("--modes", choices=("minmax", "minsum", "both"),
                        default="both")
    parser.add_argument("--exact-reference", choices=("ip-kont", "branch-and-bound", "auto"),
                        default="auto",
                        help="exact backend used in the reference slot")
    parser.add_argument(
        "--threads", type=int, default=max(1, os.cpu_count() or 1),
        help="parallel worker count (default: available CPU cores)"
    )
    parser.add_argument("--run-timeout", type=float, default=60.0,
                        help="hard wall-clock limit per combination (default: 60)")
    parser.add_argument("--time-limit", type=float, default=60.0,
                        help="limit per static/IP subsolve (default: 60)")
    args = parser.parse_args(argv)
    if args.threads <= 0 or args.run_timeout <= 0 or args.time_limit <= 0:
        parser.error("--threads and timeout values must be positive")
    if not SOLVER.is_file():
        parser.error(f"solver executable not found: {SOLVER}")
    instances_root = Path(args.instances).resolve()
    if not instances_root.is_dir():
        parser.error(f"instance directory not found: {instances_root}")
    instance_paths = sorted(instances_root.rglob("*.json"))
    if not instance_paths:
        parser.error("no canonical JSON instances found")
    requested = [value.strip() for value in args.algorithms.split(",")
                 if value.strip()]
    selected = (set(ALGORITHMS) if not requested or
                any(value.lower() == "all" for value in requested)
                else set(requested))
    exact_reference = args.exact_reference
    if exact_reference == "auto":
        exact_reference = "ip-kont" if (ROOT / "build" / "kdc-solver").is_file() else "branch-and-bound"
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
    output = Path(args.output)
    if not output.is_absolute():
        output = ROOT / output
    clear_previous_output(output)
    output.mkdir(parents=True, exist_ok=True)

    records = []
    total = len(instance_paths) * len(algorithms) * len(objectives)
    print(f"{len(instance_paths)} instances × {len(algorithms)} algorithms "
          f"× {len(objectives)} modes = {total} combinations; "
          f"{args.run_timeout:g}s hard timeout each.", flush=True)
    complete = 0
    with ThreadPoolExecutor(max_workers=args.threads) as executor:
        for phase in ALGORITHM_PHASES:
            phase_algorithms = [algorithm for algorithm in algorithms
                                if algorithm in phase]
            futures = [
                executor.submit(
                    run_one, path, output, algorithm, objective,
                    args.run_timeout, args.time_limit,
                )
                for path in instance_paths
                for algorithm in phase_algorithms
                for objective in objectives
            ]
            for future in as_completed(futures):
                record = future.result()
                records.append(record)
                complete += 1
                if complete % 50 == 0 or complete == total:
                    print(f"Completed {complete}/{total} "
                          f"({sum(bool(row['feasible']) for row in records)} "
                          "feasible)", flush=True)
    write_outputs(records, output)
    print(f"Wrote results to {output}", flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
