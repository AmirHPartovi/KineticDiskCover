"""Delimited reporting exports and their auditable Markdown companions."""

from __future__ import annotations

import csv
import json
from pathlib import Path
import re
from typing import Any, Iterable, Mapping, Sequence


def _cell(value: Any) -> str:
    if value is None:
        return ""
    if isinstance(value, (dict, list, tuple)):
        return json.dumps(value, sort_keys=True, separators=(",", ":"))
    return str(value)


def _markdown_cell(value: Any) -> str:
    return _cell(value).replace("|", r"\|").replace("\n", " ")


def _status_counts(rows: Sequence[Mapping[str, Any]]) -> dict[str, int]:
    counts: dict[str, int] = {}
    for row in rows:
        if "terminal_status" not in row and "status" not in row:
            continue
        status = str(row.get("terminal_status", row.get("status", "")) or "UNKNOWN")
        counts[status] = counts.get(status, 0) + 1
    return dict(sorted(counts.items()))


def write_companion(
    path: str | Path,
    *,
    title: str,
    purpose: str,
    experiment_id: str,
    source: str,
    generation_command: str,
    file_scope: str,
    sort_order: str,
    filters: str,
    column_definitions: Mapping[str, str],
    rows: Sequence[Mapping[str, Any]] | None = None,
    preview_limit: int = 12,
    limitations: str,
    formulas: str = (
        "Values are copied from the source export; no metric is recomputed by "
        "companion generation."
    ),
) -> Path:
    """Write an informative sidecar with metadata, column meanings, and preview."""
    target = Path(path)
    if target.suffix.lower() not in {".csv", ".tsv"}:
        raise ValueError(f"expected CSV or TSV path, got {target}")
    delimiter = "\t" if target.suffix.lower() == ".tsv" else ","
    with target.open(encoding="utf-8", newline="") as source_file:
        reader = csv.DictReader(source_file, delimiter=delimiter)
        columns = list(reader.fieldnames or [])
        if rows is None:
            row_count = 0
            preview: list[Mapping[str, Any]] = []
            counts: dict[str, int] = {}
            for row in reader:
                row_count += 1
                if len(preview) < max(0, preview_limit):
                    preview.append(row)
                status = row.get("terminal_status", row.get("status"))
                if status is not None:
                    key = str(status or "UNKNOWN")
                    counts[key] = counts.get(key, 0) + 1
            materialized_rows: Sequence[Mapping[str, Any]] = preview
            status_counts = dict(sorted(counts.items()))
        else:
            source_rows = list(rows)
            row_count = len(source_rows)
            materialized_rows = source_rows[:max(0, preview_limit)]
            status_counts = _status_counts(source_rows)
    missing_definitions = [column for column in columns
                           if not column_definitions.get(column)]
    if missing_definitions:
        raise ValueError(
            f"missing column definitions for {target}: "
            + ", ".join(missing_definitions)
        )

    lines = [
        f"# {title}",
        "",
        f"- **Purpose:** {purpose}",
        f"- **Experiment:** `{experiment_id}`",
        f"- **Source manifest/result:** `{source}`",
        f"- **Generation:** `{generation_command}`",
        f"- **Rows:** {row_count}",
        f"- **File scope:** {file_scope}",
        f"- **Sort order:** {sort_order}",
        f"- **Filters:** {filters}",
        "- **Status counts:** "
        + (", ".join(f"{key}={value}" for key, value in status_counts.items())
           if status_counts else "not applicable or status column absent"),
        "- **Missing/null handling:** unavailable or inapplicable measurements "
        "remain blank/null; no unrelated metric is substituted.",
        f"- **Limitations:** {limitations}",
        "",
        "## Derived metric formulas",
        "",
        formulas,
        "",
        "## Column definitions",
        "",
    ]
    lines.extend(f"- `{column}` — {column_definitions[column]}"
                 for column in columns)
    lines.extend(["", "## Preview", ""])
    if not row_count:
        lines.append("No data rows; the header records the intended output shape.")
    else:
        shown = materialized_rows[:max(0, preview_limit)]
        lines.append("| " + " | ".join(_markdown_cell(c) for c in columns) + " |")
        lines.append("| " + " | ".join("---" for _ in columns) + " |")
        for row in shown:
            lines.append("| " + " | ".join(
                _markdown_cell(row.get(column, "")) for column in columns
            ) + " |")
        if row_count > len(shown):
            lines.extend([
                "",
                f"Preview shows {len(shown)} of {row_count} rows; "
                "the delimited file contains the complete population.",
            ])
    target.with_suffix(".md").write_text("\n".join(lines) + "\n",
                                         encoding="utf-8")
    return target.with_suffix(".md")


def write_delimited(
    path: str | Path,
    rows: Iterable[Mapping[str, Any]],
    *,
    columns: Sequence[str],
    companion: Mapping[str, Any],
) -> tuple[Path, Path]:
    """Write CSV/TSV and a populated Markdown companion."""
    target = Path(path)
    if target.suffix.lower() not in {".csv", ".tsv"}:
        raise ValueError(f"expected CSV or TSV path, got {target}")
    materialized_rows = list(rows)
    target.parent.mkdir(parents=True, exist_ok=True)
    delimiter = "\t" if target.suffix.lower() == ".tsv" else ","
    with target.open("w", encoding="utf-8", newline="") as output:
        writer = csv.DictWriter(
            output, fieldnames=list(columns), delimiter=delimiter,
            extrasaction="ignore",
        )
        writer.writeheader()
        writer.writerows({
            key: _cell(value) for key, value in row.items()
        } for row in materialized_rows)
    details = dict(companion)
    details.pop("rows", None)
    write_companion(
        target, rows=materialized_rows,
        column_definitions=details.pop("column_definitions"),
        **details,
    )
    return target, target.with_suffix(".md")


def _fallback_definition(column: str, relative: str) -> str:
    known = {
        "wall_time_sec": "recorded wall-clock duration in seconds; not substituted for solve time.",
        "solve_time_sec": "solver-reported solve duration in seconds.",
        "verification_time_sec": "verification duration in seconds.",
        "serialization_time_sec": "serialization duration in seconds.",
        "total_time_sec": "reported total duration in seconds.",
        "objective_value": "scalar objective value only for MinMax or MinSum; blank for MinMaxSum.",
        "peak_cost": "MinMax peak-cost component.",
        "integral_cost": "MinSum integral-cost component.",
        "lower_bound": "reported lower bound; its certification is represented separately.",
        "upper_bound": "reported upper bound; not inferred from another field.",
        "verification_kind": "verification type recorded by the run, such as EMPIRICAL or CERTIFIED_CONTINUOUS.",
        "status": "terminal status from the canonical run record.",
        "run_status": "terminal status from the canonical run record.",
        "run_id": "immutable physical execution identifier.",
        "run_key": "logical plan-slot key.",
        "instance_id": "experiment-scoped canonical instance identifier.",
        "algorithm": "registered solver identifier.",
        "objective": "objective identifier.",
        "seed": "solver random seed.",
        "repeat": "zero-based repeat index.",
        "feasible": "recorded feasibility flag.",
        "verified": "recorded verification pass flag.",
    }
    if column in known:
        return known[column]
    return (
        f"Serialized source field `{column}` from `{relative}`; consult the "
        "canonical run record/schema for its originating semantics. It is "
        "not recomputed or substituted by this report."
    )


def audit_companions(
    experiment_dir: str | Path,
    *,
    create_missing: bool = False,
    experiment_id: str | None = None,
) -> list[str]:
    """Audit every CSV/TSV recursively; optionally document canonical exports."""
    experiment = Path(experiment_dir).resolve()
    identifier = experiment_id or experiment.name
    errors: list[str] = []
    delimited = sorted(
        path for path in experiment.rglob("*")
        if path.is_file() and path.suffix.lower() in {".csv", ".tsv"}
    )
    for path in delimited:
        companion = path.with_suffix(".md")
        relative = path.relative_to(experiment).as_posix()
        regenerate = not companion.is_file()
        if companion.is_file() and create_missing:
            try:
                companion_text = companion.read_text(encoding="utf-8")
                with path.open(encoding="utf-8", newline="") as source_file:
                    reader = csv.DictReader(
                        source_file,
                        delimiter="\t" if path.suffix.lower() == ".tsv" else ",",
                    )
                    source_row_count = sum(1 for _ in reader)
                row_match = re.search(
                    r"- \*\*Rows:\*\* ([0-9]+)\b", companion_text
                )
                regenerate = (
                    "## Derived metric formulas" not in companion_text
                    or row_match is None
                    or int(row_match.group(1)) != source_row_count
                )
            except (OSError, csv.Error, ValueError):
                regenerate = True
        if regenerate:
            if not create_missing:
                errors.append(f"missing or stale companion: {relative}.md")
                continue
            try:
                with path.open(encoding="utf-8", newline="") as source_file:
                    reader = csv.DictReader(
                        source_file,
                        delimiter="\t" if path.suffix.lower() == ".tsv" else ",",
                    )
                    columns = list(reader.fieldnames or [])
                definitions = {
                    column: _fallback_definition(column, relative)
                    for column in columns
                }
                write_companion(
                    path,
                    title=f"Data dictionary and preview: {path.name}",
                    purpose=(
                        f"Documents the existing experiment-scoped export "
                        f"`{relative}` without altering its source data."
                    ),
                    experiment_id=identifier,
                    source="validated canonical aggregate/run-record export",
                    generation_command="scripts/report_experiment.py --stage tables",
                    file_scope=relative,
                    sort_order="preserved from the source export",
                    filters="none applied by companion generation",
                    column_definitions=definitions,
                    rows=None,
                    limitations=(
                        "The sidecar documents the existing export; values and "
                        "status semantics remain those of its canonical source."
                    ),
                    formulas=(
                        "No metrics are recomputed here; all row values are "
                        "direct serialized source fields."
                    ),
                )
            except (OSError, ValueError, csv.Error) as error:
                errors.append(f"cannot document {relative}: {error}")
                continue
        try:
            text = companion.read_text(encoding="utf-8")
            with path.open(encoding="utf-8", newline="") as source_file:
                reader = csv.DictReader(
                    source_file,
                    delimiter="\t" if path.suffix.lower() == ".tsv" else ",",
                )
                columns = list(reader.fieldnames or [])
                actual_rows = sum(1 for _ in reader)
            required = (
                "Purpose:", "Experiment:", "Source manifest/result:",
                "Generation:", "Rows:", "File scope:", "Sort order:",
                "Filters:", "Status counts:", "Missing/null handling:",
                "Limitations:", "## Derived metric formulas",
                "## Column definitions", "## Preview",
            )
            missing = [marker for marker in required if marker not in text]
            if not text.startswith("# ") or companion.stat().st_size == 0:
                missing.append("nonempty titled Markdown document")
            match = re.search(r"- \*\*Rows:\*\* ([0-9]+)\b", text)
            if match is None or int(match.group(1)) != actual_rows:
                missing.append(
                    f"row count must equal delimited file count ({actual_rows})"
                )
            for marker in (
                "Purpose:", "Experiment:", "Source manifest/result:",
                "Generation:", "File scope:", "Sort order:", "Filters:",
                "Limitations:",
            ):
                line = next((item for item in text.splitlines()
                             if marker in item), "")
                if not line.split(marker, 1)[-1].strip().strip("`"):
                    missing.append(f"nonempty {marker[:-1].lower()}")
            formulas_section = text.split("## Derived metric formulas", 1)
            if len(formulas_section) < 2 or not formulas_section[1].split(
                "## Column definitions", 1
            )[0].strip():
                missing.append("nonempty derived-metric formula explanation")
            missing.extend(
                f"definition for `{column}`"
                for column in columns if f"`{column}` — " not in text
            )
            if not columns:
                missing.append("a delimited-file header")
            if missing:
                errors.append(
                    f"non-informative companion {relative}.md: "
                    + ", ".join(missing)
                )
        except (OSError, csv.Error) as error:
            errors.append(f"cannot audit {relative}: {error}")
    return errors
