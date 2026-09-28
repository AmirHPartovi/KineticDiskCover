#!/usr/bin/env python3
"""Build master and detailed Markdown/CSV tables from batch results."""

from __future__ import annotations

import argparse
import json
from pathlib import Path
import re
import sys
from typing import Iterable

import numpy as np
import pandas as pd


REQUIRED_COLUMNS = [
    "instance_name",
    "algorithm_name",
    "objective",
    "n",
    "m",
    "wall_time_sec",
    "cpu_time_sec",
    "peak_memory_mb",
    "objective_value",
    "lower_bound",
    "gap",
    "num_iterations",
    "num_ip_solves",
    "verified",
    "feasible",
]
NUMERIC_COLUMNS = [
    "n",
    "m",
    "wall_time_sec",
    "cpu_time_sec",
    "peak_memory_mb",
    "objective_value",
    "lower_bound",
    "gap",
    "num_iterations",
    "num_ip_solves",
]
MASTER_COLUMNS = [
    "instance_name",
    "n",
    "m",
    "algorithm_name",
    "objective",
    "objective_value",
    "lower_bound",
    "gap_pct",
    "wall_time_sec",
    "cpu_time_sec",
    "peak_memory_mb",
    "num_iterations",
    "num_ip_solves",
    "verified",
]
PIVOT_METRICS = [
    "objective_value",
    "wall_time_sec",
    "gap_pct",
    "peak_memory_mb",
]


def _to_bool(value: object) -> bool:
    if isinstance(value, (bool, np.bool_)):
        return bool(value)
    if value is None or (isinstance(value, float) and np.isnan(value)):
        return False
    if isinstance(value, (int, np.integer)):
        return bool(value)
    normalized = str(value).strip().lower()
    if normalized in {"true", "1", "yes"}:
        return True
    if normalized in {"false", "0", "no", ""}:
        return False
    raise ValueError(f"invalid boolean value: {value!r}")


def instance_family(name: str) -> str:
    match = re.match(r"^(random_n|fix_|pub_)", str(name), re.IGNORECASE)
    if match:
        family = match.group(1).lower()
        return family.rstrip("_")
    prefix = re.split(r"[_\-\s]", str(name), maxsplit=1)[0]
    return prefix or "unknown"


def load_results(path: str | Path) -> tuple[pd.DataFrame, pd.DataFrame, pd.DataFrame]:
    source = Path(path)
    with source.open(encoding="utf-8") as input_file:
        raw = json.load(input_file)
    if not isinstance(raw, list):
        raise ValueError("batch results JSON root must be an array")
    df = pd.DataFrame(raw)
    missing = [column for column in REQUIRED_COLUMNS if column not in df.columns]
    if missing:
        raise ValueError("batch results missing required columns: " +
                         ", ".join(missing))
    for column in NUMERIC_COLUMNS:
        df[column] = pd.to_numeric(df[column], errors="coerce").astype("float64")
    for column in ("verified", "feasible"):
        df[column] = df[column].map(_to_bool).astype(bool)
    df["instance_family"] = df["instance_name"].map(instance_family)
    df["size_bucket"] = pd.cut(
        df["n"],
        bins=[-np.inf, 50, 200, np.inf],
        labels=["small: n<=50", "medium: 50<n<=200", "large: n>200"],
        right=True,
    )
    df["size_bucket"] = df["size_bucket"].astype("string")
    df["gap_pct"] = df["gap"] * 100.0
    failed = df.loc[~(df["feasible"] & df["verified"])].copy()
    successful = df.loc[df["feasible"] & df["verified"]].copy()
    return df, successful, failed


def build_master(df: pd.DataFrame) -> pd.DataFrame:
    """Return the sorted successful master table with the specified columns."""
    source = df.copy()
    if "gap_pct" not in source.columns and "gap" in source.columns:
        source["gap_pct"] = pd.to_numeric(source["gap"], errors="coerce") * 100.0
    missing = [column for column in MASTER_COLUMNS if column not in source.columns]
    if missing:
        raise ValueError("master table missing columns: " + ", ".join(missing))
    master = source[MASTER_COLUMNS].copy()
    return master.sort_values(
        ["instance_name", "algorithm_name", "objective"], kind="stable"
    ).reset_index(drop=True)


def _format_value(column: str, value: object) -> str:
    if pd.isna(value):
        return ""
    if column.endswith("_time") or column.endswith("_time_sec"):
        return f"{float(value):.3f}s"
    if column in {"minmax_time", "minsum_time", "median_time_sec",
                  "min_time_sec", "max_time_sec", "p95_time_sec"}:
        return f"{float(value):.3f}s"
    if (column in {"minmax_mem", "minsum_mem", "median_mem_mb"} or
            column.endswith("_median_mem_mb")):
        return f"{float(value):.1f} MB"
    if column in {"peak_memory_mb", "minmax_mem", "minsum_mem"}:
        return f"{float(value):.1f} MB"
    if column == "gap_pct" or column.endswith("_gap_pct") or column == "median_gap_pct":
        return f"{float(value):.2f}%"
    if column in {"verified", "feasible"} or column.endswith("_verified"):
        return "true" if bool(value) else "false"
    if isinstance(value, (float, np.floating)):
        return f"{float(value):.6g}"
    if isinstance(value, (int, np.integer)):
        return str(int(value))
    return str(value)


def _markdown_table(
    df: pd.DataFrame,
    *,
    bold_min_columns: Iterable[str] = (),
    bold_min_by: tuple[str, ...] = (),
) -> str:
    columns = [str(column) for column in df.columns]
    formatted: list[list[str]] = []
    minima: dict[tuple[object, ...] | tuple[str, object], float] = {}
    bold_columns = set(bold_min_columns)
    if bold_columns:
        for column in bold_columns:
            if column not in df.columns:
                continue
            if bold_min_by:
                for keys, group in df.groupby(list(bold_min_by), dropna=False,
                                              sort=False):
                    key_tuple = keys if isinstance(keys, tuple) else (keys,)
                    valid = pd.to_numeric(group[column], errors="coerce").dropna()
                    if not valid.empty:
                        minima[(column, *key_tuple)] = float(valid.min())
            else:
                valid = pd.to_numeric(df[column], errors="coerce").dropna()
                if not valid.empty:
                    minima[(column,)] = float(valid.min())

    for _, row in df.iterrows():
        output_row = []
        for column in columns:
            value = row[column]
            cell = _format_value(column, value)
            if column in bold_columns and not pd.isna(value):
                if bold_min_by:
                    key = (column,) + tuple(row[name] for name in bold_min_by)
                else:
                    key = (column,)
                if key in minima and np.isclose(
                    float(value), minima[key], rtol=1e-12, atol=1e-12
                ):
                    cell = f"**{cell}**"
            output_row.append(cell.replace("|", r"\|").replace("\n", " "))
        formatted.append(output_row)

    widths = [len(column) for column in columns]
    for row in formatted:
        for index, cell in enumerate(row):
            widths[index] = max(widths[index], len(cell))
    header = "| " + " | ".join(
        column.ljust(widths[index]) for index, column in enumerate(columns)
    ) + " |"
    separator = "| " + " | ".join("-" * width for width in widths) + " |"
    rows = [
        "| " + " | ".join(cell.ljust(widths[index])
                           for index, cell in enumerate(row)) + " |"
        for row in formatted
    ]
    return "\n".join([header, separator, *rows])


def render_master(df: pd.DataFrame) -> str:
    master = build_master(df)
    table = _markdown_table(
        master,
        bold_min_columns=("objective_value",),
        bold_min_by=("instance_name", "objective"),
    )
    return "# Master Results\n\n" + table + "\n"


def _pivot_table(df: pd.DataFrame, index: str) -> pd.DataFrame:
    mode_order = ["minmax", "minsum"]
    metrics = {
        "objective_value": "objective",
        "wall_time_sec": "time",
        "gap_pct": "gap_pct",
        "peak_memory_mb": "mem",
    }
    if index == "algorithm_name":
        columns = [
            f"{mode}_{suffix}"
            for suffix in ("objective", "time", "gap_pct", "mem")
            for mode in mode_order
        ]
    else:
        columns = [
            f"{mode}_{suffix}"
            for suffix in ("objective", "time", "gap_pct", "mem", "verified")
            for mode in mode_order
        ]
    result = pd.DataFrame(index=sorted(df[index].dropna().unique()), columns=columns)
    result.index.name = index
    for mode in mode_order:
        subset = df[df["objective"] == mode]
        for source, suffix in metrics.items():
            name = f"{mode}_{suffix}"
            if source in subset.columns:
                values = subset.drop_duplicates(index, keep="last").set_index(index)[source]
                result.loc[values.index, name] = values
        if index == "algorithm_name":
            continue
        verified_name = f"{mode}_verified"
        values = subset.drop_duplicates(index, keep="last").set_index(index)["verified"]
        result.loc[values.index, verified_name] = values
    result = result.reset_index()
    for column in columns:
        result[column] = pd.to_numeric(result[column], errors="coerce")
        if column.endswith("_verified"):
            result[column] = result[column].map(
                lambda value: bool(value) if pd.notna(value) else np.nan
            )
    if index == "instance_name":
        result = result.sort_values(
            "minmax_objective", na_position="last", kind="stable"
        )
    else:
        result = result.sort_values(index, kind="stable")
    return result.reset_index(drop=True)


def _family_table(df: pd.DataFrame) -> pd.DataFrame:
    metrics = [
        ("median_objective", "objective_value", "median"),
        ("median_gap_pct", "gap_pct", "median"),
        ("median_time_sec", "wall_time_sec", "median"),
        ("median_mem_mb", "peak_memory_mb", "median"),
        ("min_time_sec", "wall_time_sec", "min"),
        ("max_time_sec", "wall_time_sec", "max"),
        ("p95_time_sec", "wall_time_sec", "p95"),
    ]
    records: list[dict[str, object]] = []
    for algorithm, algorithm_group in df.groupby("algorithm_name", sort=True):
        record: dict[str, object] = {"algorithm_name": algorithm}
        for objective in ("minmax", "minsum"):
            group = algorithm_group[algorithm_group["objective"] == objective]
            for output_name, source_name, operation in metrics:
                values = group[source_name].dropna()
                if values.empty:
                    value = np.nan
                elif operation == "median":
                    value = values.median()
                elif operation == "min":
                    value = values.min()
                elif operation == "max":
                    value = values.max()
                else:
                    value = values.quantile(0.95)
                record[f"{objective}_{output_name}"] = value
            record[f"{objective}_num_instances"] = group["instance_name"].nunique()
        records.append(record)
    result = pd.DataFrame(records)
    if result.empty:
        return pd.DataFrame(columns=["algorithm_name"] + [
            f"{mode}_{metric}"
            for mode in ("minmax", "minsum")
            for metric, _, _ in metrics
        ] + ["minmax_num_instances", "minsum_num_instances"])
    result = result.sort_values(
        ["minmax_median_objective", "algorithm_name"],
        kind="stable",
        na_position="last",
    )
    return result.reset_index(drop=True)


def _safe_filename(value: object) -> str:
    filename = re.sub(r"[^A-Za-z0-9._-]+", "_", str(value)).strip("._")
    return filename or "unnamed"


def _write_formats(df: pd.DataFrame, stem: Path, formats: set[str],
                   markdown: str | None = None,
                   bold_min_columns: Iterable[str] = ()) -> list[Path]:
    written: list[Path] = []
    stem.parent.mkdir(parents=True, exist_ok=True)
    if "csv" in formats:
        df.to_csv(stem.with_suffix(".csv"), index=False)
        written.append(stem.with_suffix(".csv"))
    if "markdown" in formats:
        text = markdown if markdown is not None else _markdown_table(
            df, bold_min_columns=bold_min_columns
        )
        stem.with_suffix(".md").write_text(text + ("" if text.endswith("\n") else "\n"),
                                            encoding="utf-8")
        written.append(stem.with_suffix(".md"))
    return written


def write_tables(df: pd.DataFrame, successful: pd.DataFrame,
                 failed: pd.DataFrame, output_dir: str | Path,
                 formats: set[str]) -> None:
    output = Path(output_dir)
    output.mkdir(parents=True, exist_ok=True)
    master = build_master(successful)
    written: list[Path] = []
    written.extend(_write_formats(
        master, output / "master", formats,
        markdown=render_master(successful),
    ))

    algorithm_files: list[Path] = []
    for algorithm in sorted(successful["algorithm_name"].unique()):
        table = _pivot_table(
            successful[successful["algorithm_name"] == algorithm],
            "instance_name",
        )
        stems = output / "per_algorithm" / _safe_filename(algorithm)
        algorithm_files.extend(_write_formats(table, stems, formats))

    instance_files: list[Path] = []
    for instance in sorted(successful["instance_name"].unique()):
        table = _pivot_table(
            successful[successful["instance_name"] == instance],
            "algorithm_name",
        )
        stems = output / "per_instance" / _safe_filename(instance)
        instance_files.extend(_write_formats(
            table, stems, formats,
            bold_min_columns=("minmax_objective", "minsum_objective"),
        ))

    family_files: list[Path] = []
    for family in sorted(successful["instance_family"].dropna().unique()):
        table = _family_table(
            successful[successful["instance_family"] == family]
        )
        stems = output / "per_family" / _safe_filename(family)
        family_files.extend(_write_formats(
            table, stems, formats,
            bold_min_columns=("median_objective",),
        ))

    failed_columns = [
        ("instance_name", "instance"),
        ("algorithm_name", "algorithm"),
        ("objective", "objective"),
        ("error_message", "error_message"),
    ]
    failed_output = pd.DataFrame({
        target: failed[source].fillna("").astype(str)
        if source in failed.columns
        else ""
        for source, target in failed_columns
    })
    if "markdown" in formats:
        failed_md = "# Failed Runs\n\n" + _markdown_table(failed_output) + "\n"
        (output / "failed_runs.md").write_text(failed_md, encoding="utf-8")
        written.append(output / "failed_runs.md")
    if "csv" in formats:
        failed_output.to_csv(output / "failed_runs.csv", index=False)
        written.append(output / "failed_runs.csv")

    total_runs = len(df)
    total_instances = df["instance_name"].nunique()
    total_algorithms = df["algorithm_name"].nunique()
    median_gap = successful["gap_pct"].median()
    median_time = successful["wall_time_sec"].median()
    summary = (
        f"Processed {total_runs} runs across {total_instances} instances and "
        f"{total_algorithms} algorithms. Among feasible, verified runs the "
        f"overall median gap is {_format_value('gap_pct', median_gap)} and "
        f"the median wall time is {_format_value('wall_time_sec', median_time)}."
    )
    links: list[tuple[str, Path]] = [
        ("Master table", Path("master.md") if "markdown" in formats
         else Path("master.csv")),
    ]
    if "markdown" in formats and "csv" in formats:
        links.append(("Master CSV", Path("master.csv")))
    for label, files in [
        ("Per-algorithm tables", algorithm_files),
        ("Per-instance tables", instance_files),
        ("Per-family tables", family_files),
    ]:
        for file in files:
            relative = file.relative_to(output)
            links.append((label, relative))
    index_lines = [
        "# Results Tables Index",
        "",
        summary,
        "",
    ]
    for label, relative in links:
        if relative.exists() or (output / relative).exists():
            index_lines.append(f"- [{label}: {relative.as_posix()}]({relative.as_posix()})")
    if "markdown" in formats:
        index_lines.append("- [Failed runs](failed_runs.md)")
    if "csv" in formats:
        index_lines.append("- [Failed runs CSV](failed_runs.csv)")
    (output / "00_index.md").write_text("\n".join(index_lines) + "\n",
                                        encoding="utf-8")


def parse_formats(value: str) -> set[str]:
    aliases = {"both": {"markdown", "csv"}}
    normalized = value.strip().lower()
    if normalized in aliases:
        return aliases[normalized]
    formats = {item.strip().lower() for item in normalized.split(",") if item.strip()}
    if not formats or not formats <= {"markdown", "csv"}:
        raise argparse.ArgumentTypeError(
            "format must be markdown, csv, or both"
        )
    return formats


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", default="results/batch/master_results.json")
    parser.add_argument("--output", default="results/tables")
    parser.add_argument("--format", type=parse_formats, default=parse_formats("both"))
    args = parser.parse_args(argv)
    try:
        df, successful, failed = load_results(args.input)
        write_tables(df, successful, failed, args.output, args.format)
    except (OSError, ValueError, json.JSONDecodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    print(
        f"Wrote tables for {len(df)} runs to {Path(args.output)} "
        f"({len(failed)} failed rows)"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
