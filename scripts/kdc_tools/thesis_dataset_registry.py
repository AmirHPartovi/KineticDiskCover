"""Registry and immutable-record support for thesis Chapter 7 datasets."""

from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import Path
from typing import Any, Mapping, Sequence

from .schemas import (
    dumps_json,
    load_schema,
    loads_json,
    validate_document,
)
from .storage import write_bytes_create_only


ROOT = Path(__file__).resolve().parents[2]
SPEC_PATH = (
    ROOT / "docs" / "thesis_implementation"
    / "chapter7_generator_spec_v1.json"
)
FAMILY_IDS = (
    "F1", "F2", "F3", "F4", "F5", "F6", "F7",
    "S1", "S2", "S3", "X", "EC", "1D",
)
MASK64 = (1 << 64) - 1


class GeneratorNotReadyError(ValueError):
    """A requested family is not specified sufficiently for reproducible use."""


def _canonical_json_bytes(document: Any) -> bytes:
    return (dumps_json(document, pretty=True) + "\n").encode("utf-8")


def _unsealed_record(record: Mapping[str, Any]) -> dict[str, Any]:
    result = deepcopy(dict(record))
    metadata = result.get("metadata")
    if not isinstance(metadata, dict):
        raise ValueError("generated instance metadata must be an object")
    metadata.pop("sha256", None)
    return result


def _shared_generation_blockers(
    document: Mapping[str, Any], family_id: str
) -> list[str]:
    blockers: list[str] = []
    if family_id != "EC":
        random_contract = document["common"]["random_stream_contract"]
        for key in (
            "PRNG_name_and_version",
            "uniform_integer_to_real_transform",
            "normal_distribution_algorithm_and_version",
            "family_numeric_codes",
            "generator_subversion",
            "master_seed_value_or_schedule",
            "replicate_index_base",
        ):
            if random_contract.get(key) == "UNRESOLVED":
                blockers.append(f"common.random_stream_contract.{key}")
    if (
        document["common"]["numeric_modes"]["negative_zero_normalization"]
        == "UNRESOLVED"
    ):
        blockers.append("common.numeric_modes.negative_zero_normalization")
    return blockers


def seal_generated_instance(
    metadata: Mapping[str, Any], instance: Mapping[str, Any]
) -> dict[str, Any]:
    """Create a schema-checked record whose digest excludes only metadata.sha256."""
    if "sha256" in metadata:
        raise ValueError("metadata.sha256 is computed by the registry")
    record = {
        "schema_version": 1,
        "metadata": deepcopy(dict(metadata)),
        "instance": deepcopy(dict(instance)),
    }
    digest = hashlib.sha256(_canonical_json_bytes(record)).hexdigest()
    record["metadata"]["sha256"] = digest
    validate_document(record, "thesis_generated_instance_v1")
    return record


def verify_generated_instance(record: Mapping[str, Any]) -> str:
    """Validate schema and digest, returning the independently recomputed SHA-256."""
    validate_document(record, "thesis_generated_instance_v1")
    expected = record["metadata"]["sha256"]
    actual = hashlib.sha256(_canonical_json_bytes(_unsealed_record(record))).hexdigest()
    if actual != expected:
        raise ValueError("generated instance SHA-256 mismatch")
    return actual


def write_generated_instance(
    output_root: str | Path, record: Mapping[str, Any]
) -> Path:
    """Write an immutable, content-addressed generated record."""
    digest = verify_generated_instance(record)
    family_id = record["metadata"]["family"]
    require_generation_ready(family_id)
    serialized = _canonical_json_bytes(record)
    relative = Path("sha256") / digest[:2] / f"{digest}.json"
    root = Path(output_root)
    root.mkdir(parents=True, exist_ok=True)
    destination = root / relative
    try:
        write_bytes_create_only(root, relative, serialized)
    except FileExistsError:
        if destination.read_bytes() != serialized:
            raise ValueError("content-addressed path contains different bytes")
    return destination


def write_content_addressed_json(output_root: str | Path, payload: Any) -> Path:
    """Store canonical JSON create-only under its content digest."""
    serialized = _canonical_json_bytes(payload)
    digest = hashlib.sha256(serialized).hexdigest()
    relative = Path("sha256") / digest[:2] / f"{digest}.json"
    root = Path(output_root)
    root.mkdir(parents=True, exist_ok=True)
    destination = root / relative
    try:
        write_bytes_create_only(root, relative, serialized)
    except FileExistsError:
        if destination.read_bytes() != serialized:
            raise ValueError("content-addressed path contains different bytes")
    return destination


def read_generated_instance(path: str | Path) -> dict[str, Any]:
    """Read, schema-check, and digest-check one immutable generated record."""
    source = Path(path)
    try:
        record = loads_json(source.read_bytes())
    except (OSError, json.JSONDecodeError, UnicodeDecodeError) as error:
        raise ValueError(f"cannot read generated instance: {error}") from error
    if not isinstance(record, dict):
        raise ValueError("generated instance root must be an object")
    digest = verify_generated_instance(record)
    if source.name != f"{digest}.json":
        raise ValueError("generated instance filename does not match its digest")
    if source.parent.name != digest[:2] or source.parent.parent.name != "sha256":
        raise ValueError("generated instance path does not match its digest")
    return record


def validate_spec(document: Mapping[str, Any]) -> None:
    """Validate the versioned registry and reconcile planned-count arithmetic."""
    validate_document(document, "thesis_dataset_generator_spec_v1")
    families = document["families"]
    if set(families) != set(FAMILY_IDS):
        raise ValueError("generator registry family set is incomplete")

    structural = document["counts"]["unconditional"]["family_memberships"]
    structural_unique = document["counts"]["unconditional"]["family_unique_files"]
    family_allocation = document["counts"]["family_allocation"]
    if structural != 70 or structural_unique != 70:
        raise ValueError("Chapter 7 F1-F7 aggregate count must be 70/70")
    for family_id in (f"F{index}" for index in range(1, 8)):
        allocation = family_allocation[family_id]
        family = families[family_id]
        for field in ("memberships", "unique_files"):
            planned_field = (
                "planned_memberships" if field == "memberships"
                else "planned_unique_files"
            )
            value = allocation[field]
            if value == "UNRESOLVED":
                if family[planned_field] is not None:
                    raise ValueError(
                        f"{family_id} individual {field} must remain unresolved"
                    )
            elif family[planned_field] != value:
                raise ValueError(f"{family_id} allocation disagrees with its record")
    if (
        family_allocation["total_F1_to_F7_memberships"] != structural
        or family_allocation["total_F1_to_F7_unique_files"] != structural_unique
        or family_allocation["F1"]["memberships"] != 10
        or family_allocation["F1"]["unique_files"] != 10
    ):
        raise ValueError("F1-F7 per-family allocation and aggregate counts disagree")
    scale_memberships = sum(
        families[name]["planned_memberships"] for name in ("S1", "S2", "S3")
    )
    scale_new = sum(
        families[name]["planned_unique_files"] for name in ("S1", "S2", "S3")
    )
    scale_shared = sum(
        families[name]["shared_memberships"] for name in ("S1", "S2", "S3")
    )
    small = families["X"]["planned_memberships"]
    boundary = families["EC"]["planned_memberships"]
    unconditional = document["counts"]["unconditional"]
    expected_memberships = structural + scale_memberships + small + boundary
    expected_unique = structural + scale_new + small + boundary
    if (
        expected_memberships != unconditional["planned_memberships"]
        or expected_unique != unconditional["planned_unique_files"]
        or structural != unconditional["family_memberships"]
        or structural_unique != unconditional["family_unique_files"]
        or scale_memberships != unconditional["scale_memberships"]
        or scale_new != unconditional["scale_unique_new_files"]
        or scale_shared != unconditional["scale_shared_memberships_with_F1"]
    ):
        raise ValueError("planned family/scale counts do not reconcile")
    if structural != 70 or scale_memberships != 150 or scale_new != 120:
        raise ValueError("Chapter 7 family or scale counts changed unexpectedly")
    if scale_shared != 30 or expected_memberships != 323 or expected_unique != 293:
        raise ValueError("Chapter 7 unique-file and shared-membership counts disagree")

    conditional = document["counts"]["conditional_1d"]
    if (
        families["1D"]["generation_status"] != "DISABLED"
        or conditional["planned_memberships"] != 15
        or conditional["planned_unique_files"] != 15
        or conditional["total_memberships_if_1d_enabled"] != 338
        or conditional["total_unique_files_if_1d_enabled"] != 308
    ):
        raise ValueError("conditional 1D counts or disabled status are inconsistent")

    for family_id, family in families.items():
        blockers = list(family["unresolved_fields"])
        blockers.extend(_shared_generation_blockers(document, family_id))
        if family["generation_status"] == "READY" and blockers:
            raise ValueError(f"{family_id} cannot be READY with unresolved fields")
    eligible = [family for name, family in families.items() if name != "1D"]
    ready_count = sum(family["generation_status"] == "READY" for family in eligible)
    if ready_count == len(eligible):
        expected_status = "READY"
    elif ready_count:
        expected_status = "PARTIALLY_READY"
    else:
        expected_status = "BLOCKED_NO_REPRODUCIBLE_FAMILY"
    if document["generation_status"] != expected_status:
        raise ValueError("registry status does not match family readiness")


def load_spec() -> dict[str, Any]:
    """Load and validate the checked-in Chapter 7 registry."""
    load_schema("thesis_dataset_generator_spec_v1")
    try:
        document = loads_json(SPEC_PATH.read_bytes())
    except (OSError, json.JSONDecodeError, UnicodeDecodeError) as error:
        raise ValueError(f"cannot load Chapter 7 generator specification: {error}") \
            from error
    if not isinstance(document, dict):
        raise ValueError("Chapter 7 generator specification root must be an object")
    validate_spec(document)
    return document


def family_blockers(family_id: str) -> tuple[str, ...]:
    """Return all source omissions preventing reproducible family generation."""
    spec = load_spec()
    try:
        family = spec["families"][family_id]
    except KeyError as error:
        raise ValueError(f"unknown Chapter 7 dataset family: {family_id!r}") from error

    blockers = list(family["unresolved_fields"])
    if family["generation_status"] == "DISABLED":
        blockers.append("family is explicitly disabled pending external confirmation")
    blockers.extend(_shared_generation_blockers(spec, family_id))
    return tuple(dict.fromkeys(blockers))


def require_generation_ready(family_id: str) -> Mapping[str, Any]:
    """Return a family specification only if it is ready for deterministic replay."""
    spec = load_spec()
    try:
        family = spec["families"][family_id]
    except KeyError as error:
        raise ValueError(f"unknown Chapter 7 dataset family: {family_id!r}") from error
    blockers = family_blockers(family_id)
    if family["generation_status"] != "READY" or blockers:
        details = "; ".join(blockers) if blockers else "family is not marked READY"
        raise GeneratorNotReadyError(f"{family_id} generation is blocked: {details}")
    return family


def derive_seed(master_seed: int, components: Sequence[int]) -> int:
    """Apply the thesis's 64-bit seed mixer to explicitly supplied components."""
    if len(components) not in (6, 7):
        raise ValueError("Chapter 7 seed flow requires six components, or seven for D1")
    if isinstance(master_seed, bool) or not isinstance(master_seed, int):
        raise TypeError("master_seed must be an integer")
    if any(isinstance(value, bool) or not isinstance(value, int) for value in components):
        raise TypeError("seed components must be integers")

    def mix(value: int) -> int:
        x1 = ((value ^ (value >> 30)) * 0xBF58476D1CE4E5B9) & MASK64
        x2 = ((x1 ^ (x1 >> 27)) * 0x94D049BB133111EB) & MASK64
        return (x2 ^ (x2 >> 31)) & MASK64

    state = mix(master_seed & MASK64)
    for component in components:
        state = mix((state + component) & MASK64)
    return state
