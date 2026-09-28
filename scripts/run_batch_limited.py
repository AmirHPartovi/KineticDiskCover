#!/usr/bin/env python3
"""Run each instance/algorithm/objective in a process with a wall-time limit."""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import csv
import json
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time


ROOT = Path(__file__).resolve().parents[1]
SOLVER = ROOT / "build" / "kdc-solver"
ALGORITHMS = (
    "branch-and-bound", "brute-force", "genetic", "greedy", "ip-kont",
    "local-search", "lp-rounding", "nn", "primal-dual", "sa", "shifting",
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


def _read_existing(output: Path, instance: str, algorithm: str,
                   objective: str, run_timeout: float) -> dict | None:
    result = output / "runs" / instance / algorithm / objective / "result.json"
    if not result.is_file():
        return None
    try:
        record = json.loads(result.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError):
        return None
    if (record.get("time_limit_per_ip_sec") != run_timeout
            or record.get("wall_time_sec", float("inf")) > run_timeout
            or not record.get("result_json_path")):
        return None
    for field in ("result_json_path", "trace_csv_path"):
        path = record.get(field)
        if path and not Path(path).is_file():
            return None
    if record.get("feasible"):
        solution = record.get("solution_json_path")
        if not solution or not Path(solution).is_file():
            return None
    record["run_timeout_sec"] = run_timeout
    return record


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
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--run-timeout", type=float, default=10.0,
                        help="hard wall-clock limit for each combination")
    parser.add_argument("--time-limit", type=float, default=10.0,
                        help="limit for each static/IP subsolve")
    parser.add_argument("--no-resume", action="store_true",
                        help="rerun even combinations already within limits")
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
    algorithms = (list(ALGORITHMS) if not requested or
                  any(value.lower() == "all" for value in requested)
                  else requested)
    unknown = set(algorithms) - set(ALGORITHMS)
    if unknown:
        parser.error("unknown algorithms: " + ", ".join(sorted(unknown)))
    objectives = OBJECTIVES if args.modes == "both" else (args.modes,)
    output = Path(args.output)
    if not output.is_absolute():
        output = ROOT / output
    output.mkdir(parents=True, exist_ok=True)

    records = []
    pending = []
    for path in instance_paths:
        for algorithm in algorithms:
            for objective in objectives:
                existing = None if args.no_resume else _read_existing(
                    output, str(json.loads(path.read_text()).get("name")
                                or path.stem),
                    algorithm, objective, args.time_limit,
                )
                if existing is not None:
                    records.append(existing)
                else:
                    pending.append((path, algorithm, objective))
    total = len(records) + len(pending)
    print(f"{len(instance_paths)} instances × {len(algorithms)} algorithms "
          f"× {len(objectives)} modes = {total} combinations; "
          f"{len(records)} reusable, {len(pending)} to run; "
          f"{args.run_timeout:g}s hard timeout each.", flush=True)
    complete = len(records)
    with ThreadPoolExecutor(max_workers=args.threads) as executor:
        futures = {
            executor.submit(
                run_one, path, output, algorithm, objective,
                args.run_timeout, args.time_limit,
            ): (path, algorithm, objective)
            for path, algorithm, objective in pending
        }
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
