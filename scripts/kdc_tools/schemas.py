"""Strict JSON handling and validation for versioned experiment documents."""

from __future__ import annotations

from functools import lru_cache
import json
from pathlib import Path
from typing import Any

from jsonschema import Draft202012Validator, FormatChecker
from jsonschema.exceptions import ValidationError


SCHEMA_DIR = Path(__file__).resolve().parents[2] / "docs" / "schemas"
SCHEMA_FILES = {
    "artifact_manifest": "artifact_manifest.schema.json",
    "dataset_instance": "dataset_instance.schema.json",
    "experiment": "experiment.schema.json",
    "input_ref": "input_ref.schema.json",
    "request": "request.schema.json",
    "result": "result.schema.json",
    "run_plan_entry": "run_plan_entry.schema.json",
    "run_record": "run_record.schema.json",
    "run_status": "run_status.schema.json",
    "trace_event": "trace_event.schema.json",
    "verification": "verification.schema.json",
}


class SchemaValidationError(ValueError):
    """A document failed its named JSON Schema contract."""


def _reject_nonfinite(value: str) -> None:
    raise ValueError(f"non-finite JSON number is not allowed: {value}")


def loads_json(payload: str | bytes) -> Any:
    """Parse strict JSON, rejecting JavaScript's NaN and Infinity extensions."""
    return json.loads(payload, parse_constant=_reject_nonfinite)


def dumps_json(payload: Any, *, pretty: bool = False) -> str:
    """Serialize deterministic strict JSON suitable for checksums or storage."""
    options: dict[str, Any] = {
        "allow_nan": False,
        "ensure_ascii": False,
        "sort_keys": True,
    }
    if pretty:
        options["indent"] = 2
    else:
        options["separators"] = (",", ":")
    return json.dumps(payload, **options)


@lru_cache(maxsize=None)
def load_schema(name: str) -> dict[str, Any]:
    """Load and validate one repository-owned schema by its registered name."""
    try:
        filename = SCHEMA_FILES[name]
    except KeyError as exc:
        raise ValueError(f"unknown document schema: {name!r}") from exc

    path = SCHEMA_DIR / filename
    schema = loads_json(path.read_text(encoding="utf-8"))
    Draft202012Validator.check_schema(schema)
    return schema


def validate_document(document: Any, schema_name: str) -> None:
    """Raise SchemaValidationError unless document satisfies the named schema."""
    validator = Draft202012Validator(
        load_schema(schema_name), format_checker=FormatChecker()
    )
    error = next(iter(validator.iter_errors(document)), None)
    if error is None:
        return
    location = ".".join(str(part) for part in error.absolute_path)
    where = f" at {location}" if location else ""
    raise SchemaValidationError(
        f"{schema_name} schema validation failed{where}: {error.message}"
    )
