"""Versioned, immutable experiment-run manifest snapshots."""

from __future__ import annotations

from copy import deepcopy
import datetime as dt
import hashlib
from functools import lru_cache
import json
from pathlib import Path, PurePosixPath
import subprocess
import uuid
from typing import Any, Mapping

from jsonschema import Draft202012Validator, FormatChecker

from .schemas import dumps_json, loads_json


ROOT = Path(__file__).resolve().parents[2]
SCHEMA_PATH = ROOT / "docs" / "schemas" / "experiment_run_manifest_v2.schema.json"
UNRESOLVED = "UNRESOLVED"


def _utc_now() -> str:
    return dt.datetime.now(dt.timezone.utc).isoformat().replace("+00:00", "Z")


def _canonical_bytes(document: Any) -> bytes:
    return (dumps_json(document, pretty=True) + "\n").encode("utf-8")


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


@lru_cache(maxsize=1)
def _validator() -> Draft202012Validator:
    schema = loads_json(SCHEMA_PATH.read_bytes())
    Draft202012Validator.check_schema(schema)
    return Draft202012Validator(schema, format_checker=FormatChecker())


def _validate_schema(document: Any) -> None:
    error = next(iter(_validator().iter_errors(document)), None)
    if error is None:
        return
    location = ".".join(str(item) for item in error.absolute_path)
    where = f" at {location}" if location else ""
    raise ValueError(f"experiment-run manifest schema failed{where}: {error.message}")


def capture_local_git_state(repository_root: str | Path) -> dict[str, Any]:
    """Capture only local commit and porcelain status; never contact a remote."""
    root = Path(repository_root).resolve()
    commands = [
        ["git", "rev-parse", "--show-toplevel"],
        ["git", "rev-parse", "HEAD"],
        ["git", "status", "--porcelain=v1", "--untracked-files=all"],
    ]
    outputs = []
    for command in commands:
        completed = subprocess.run(
            command,
            cwd=root,
            check=False,
            capture_output=True,
            text=True,
        )
        if completed.returncode != 0:
            message = completed.stderr.strip() or "git command failed"
            raise RuntimeError(f"local Git provenance unavailable: {message}")
        outputs.append(completed.stdout.strip())

    top_level = Path(outputs[0]).resolve()
    try:
        root.relative_to(top_level)
    except ValueError as error:
        raise RuntimeError("requested path is outside the detected Git repository") from error
    commit = outputs[1]
    status_text = outputs[2]
    return {
        "commit": commit,
        "dirty": bool(status_text),
        "status_sha256": hashlib.sha256(status_text.encode("utf-8")).hexdigest(),
        "captured_at": _utc_now(),
    }


def _read_cmake_cache(repository_root: Path) -> dict[str, str]:
    cache = repository_root / "build" / "CMakeCache.txt"
    values: dict[str, str] = {}
    if not cache.is_file():
        return values
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith((
            "CMAKE_CXX_COMPILER:FILEPATH=",
            "CMAKE_BUILD_TYPE:STRING=",
        )):
            key, value = line.split("=", 1)
            values[key.split(":", 1)[0]] = value
    return values


def _compiler_version(compiler: str) -> str:
    if compiler == UNRESOLVED or not compiler:
        return UNRESOLVED
    try:
        completed = subprocess.run(
            [compiler, "--version"],
            check=False,
            capture_output=True,
            text=True,
            timeout=5,
        )
    except (OSError, subprocess.TimeoutExpired):
        return UNRESOLVED
    if completed.returncode != 0:
        return UNRESOLVED
    first_line = (completed.stdout or completed.stderr).splitlines()
    return first_line[0].strip() if first_line else UNRESOLVED


def _configuration_slices(
    configuration: Mapping[str, Any], manifest_context: Mapping[str, Any]
) -> tuple[dict[str, Any], dict[str, Any]]:
    explicit_budgets = manifest_context.get("budgets", configuration.get("budgets"))
    explicit_tolerances = manifest_context.get(
        "tolerances", configuration.get("tolerances")
    )
    budget_markers = ("budget", "time_limit", "timeout", "max_", "assignments")
    tolerance_markers = ("tolerance", "epsilon", "gap", "delta", "threshold")
    budgets = (
        deepcopy(dict(explicit_budgets))
        if isinstance(explicit_budgets, Mapping)
        else {
            key: value for key, value in configuration.items()
            if any(marker in key.lower() for marker in budget_markers)
        }
    )
    tolerances = (
        deepcopy(dict(explicit_tolerances))
        if isinstance(explicit_tolerances, Mapping)
        else {
            key: value for key, value in configuration.items()
            if any(marker in key.lower() for marker in tolerance_markers)
        }
    )
    if not budgets:
        budgets = {"status": UNRESOLVED}
    if not tolerances:
        tolerances = {"status": UNRESOLVED}
    return budgets, tolerances


def _backend_role(
    roles: Mapping[str, Any], role: str
) -> dict[str, str]:
    raw = roles.get(role)
    if not isinstance(raw, Mapping):
        return {"status": "UNRESOLVED", "name": UNRESOLVED, "version": UNRESOLVED}
    status = raw.get("status", "UNRESOLVED")
    name = raw.get("name", UNRESOLVED)
    version = raw.get("version", UNRESOLVED)
    return {
        "status": str(status),
        "name": str(name),
        "version": str(version),
    }


def _safe_artifact_path(run_root: Path, relative_path: str) -> Path:
    relative = PurePosixPath(relative_path)
    if (
        relative.is_absolute()
        or not relative.parts
        or any(part in {"", ".", ".."} for part in relative.parts)
        or "\x00" in relative_path
    ):
        raise ValueError(f"unsafe artifact path: {relative_path!r}")
    candidate = (run_root / Path(*relative.parts)).resolve()
    try:
        candidate.relative_to(run_root.resolve())
    except ValueError as error:
        raise ValueError(f"artifact escapes run directory: {relative_path}") from error
    return candidate


def _artifact_reference(run_root: Path, relative_path: str) -> dict[str, Any]:
    path = _safe_artifact_path(run_root, relative_path)
    if not path.is_file():
        raise FileNotFoundError(f"run artifact is missing: {relative_path}")
    digest = _sha256_file(path)
    return {
        "path": PurePosixPath(relative_path).as_posix(),
        "sha256": digest,
        "size_bytes": path.stat().st_size,
        "media_type": {
            ".json": "application/json",
            ".jsonl": "application/jsonl",
            ".csv": "text/csv",
            ".tsv": "text/tab-separated-values",
            ".txt": "text/plain",
        }.get(path.suffix.lower(), "application/octet-stream"),
    }


def _artifact_roles(
    run_root: Path,
    result_document: Mapping[str, Any],
    known_artifacts: list[Mapping[str, Any]],
    context: Mapping[str, Any],
) -> dict[str, list[dict[str, Any]]]:
    result_path = "result/result.json"
    verifier_path = "verification/verification.json"
    solver_result = result_document.get("solver_result")
    solver_result = solver_result if isinstance(solver_result, Mapping) else {}

    def declared_paths(context_key: str, *result_keys: str) -> set[str]:
        values = context.get(context_key, [])
        paths = {
            value for value in values
            if isinstance(value, str) and value
        } if isinstance(values, (list, tuple)) else set()
        for key in result_keys:
            value = solver_result.get(key)
            if isinstance(value, str) and value:
                paths.add(value)
        return paths

    solution_paths = declared_paths(
        "solution_paths", "solution_json_path", "solution_path"
    )
    trace_paths = declared_paths(
        "trace_paths", "trace_csv_path", "trace_jsonl_path"
    )
    for artifact in known_artifacts:
        path = artifact.get("path")
        if not isinstance(path, str):
            continue
        lowered = path.lower()
        if "solution" in lowered:
            solution_paths.add(path)
        if "trace" in lowered:
            trace_paths.add(path)

    return {
        "result": [_artifact_reference(run_root, result_path)],
        "solution": [
            _artifact_reference(run_root, path) for path in sorted(solution_paths)
        ],
        "trace": [_artifact_reference(run_root, path) for path in sorted(trace_paths)],
        "verifier": [_artifact_reference(run_root, verifier_path)],
    }


def create_run_manifest_snapshot(
    *,
    run_root: str | Path,
    experiment_id: str,
    run_id: str,
    run_status: str,
    request: Mapping[str, Any],
    input_reference: Mapping[str, Any],
    result_document: Mapping[str, Any],
    backend: Mapping[str, Any],
    provenance: Mapping[str, Any],
    git_state: Mapping[str, Any],
    known_artifacts: list[Mapping[str, Any]],
    context: Mapping[str, Any] | None = None,
) -> dict[str, Any]:
    """Build and seal a complete v2 snapshot from finalized local run data."""
    root = Path(run_root).resolve()
    context = context or {}
    configuration = request.get("resolved_configuration")
    configuration = configuration if isinstance(configuration, Mapping) else {}
    solver_result = result_document.get("solver_result")
    solver_result = solver_result if isinstance(solver_result, Mapping) else {}
    input_path = input_reference.get("canonical_path")
    input_sha = input_reference.get("canonical_sha256")
    if not isinstance(input_path, str) or not isinstance(input_sha, str):
        raise ValueError("run input reference lacks its canonical path or SHA-256")
    input_file = _safe_artifact_path(root, input_path)
    if not input_file.is_file():
        raise FileNotFoundError(f"canonical run input is missing: {input_path}")
    actual_input_sha = _sha256_file(input_file)
    if actual_input_sha != input_sha:
        raise ValueError("canonical run input checksum mismatch")

    lifecycle = context.get("input_lifecycle", {})
    if not isinstance(lifecycle, Mapping):
        raise ValueError("input_lifecycle must be an object")
    used_in_run = lifecycle.get(
        "used_in_run", context.get("used_in_run", run_status != "SKIPPED")
    )
    lifecycle = {
        key: lifecycle.get(key)
        for key in ("designed", "generated", "validated")
    }
    lifecycle["used_in_run"] = used_in_run
    roles = context.get("backends", {})
    roles = dict(roles) if isinstance(roles, Mapping) else {}
    category = str(
        configuration.get("algorithm_category")
        or solver_result.get("algorithm_category")
        or ""
    ).lower()
    inferred_role = (
        "exact" if category in {"exact_reference", "exact_candidate", "diagnostic"}
        else "heuristic" if category == "heuristic"
        else "relaxation" if "relaxation" in category
        else None
    )
    if run_status == "SKIPPED" and inferred_role:
        for role in ("exact", "heuristic", "relaxation"):
            roles.setdefault(role, {
                "status": "NOT_USED",
                "name": "NOT_USED",
                "version": "NOT_APPLICABLE",
            })
    elif inferred_role:
        for role in ("exact", "heuristic", "relaxation"):
            roles.setdefault(role, {
                "status": "NOT_USED",
                "name": "NOT_USED",
                "version": "NOT_APPLICABLE",
            })
        existing_role = roles.get(inferred_role)
        if (
            not isinstance(existing_role, Mapping)
            or existing_role.get("status") == "NOT_USED"
        ):
            roles[inferred_role] = {
                "status": "USED",
                "name": str(
                    backend.get("actual") or backend.get("selected") or UNRESOLVED
                ),
                "version": str(backend.get("version", UNRESOLVED)),
            }
    reported_backend = {
        "requested": str(request.get("requested_backend", UNRESOLVED)),
        "selected": backend.get("selected"),
        "actual": backend.get("actual"),
        "version": str(backend.get("version", UNRESOLVED)),
    }
    build_context = context.get("build", {})
    build_context = build_context if isinstance(build_context, Mapping) else {}
    cache_values = _read_cmake_cache(ROOT)
    def known_value(*values: Any) -> Any:
        return next(
            (value for value in values if value not in (None, "", UNRESOLVED)),
            UNRESOLVED,
        )

    compiler = str(known_value(
        build_context.get("compiler"),
        provenance.get("compiler"),
        cache_values.get("CMAKE_CXX_COMPILER"),
    ))
    build_type = str(known_value(
        build_context.get("build_type"),
        provenance.get("build_type"),
        cache_values.get("CMAKE_BUILD_TYPE"),
    ))
    libraries = build_context.get(
        "libraries",
        provenance.get("library_versions", context.get("library_versions")),
    )
    if not isinstance(libraries, Mapping) or not libraries:
        libraries = {
            "solver_backend": str(backend.get("version", UNRESOLVED))
        }
    budgets, tolerances = _configuration_slices(configuration, context)
    compiler_version = known_value(
        build_context.get("compiler_version"),
        provenance.get("compiler_version"),
    )
    if compiler_version == UNRESOLVED:
        compiler_version = _compiler_version(compiler)

    input_lifecycle = {
        "designed": lifecycle["designed"],
        "generated": lifecycle["generated"],
        "validated": lifecycle["validated"],
        "used_in_run": lifecycle["used_in_run"],
    }
    manifest = {
        "schema_version": 2,
        "manifest_id": str(uuid.uuid4()),
        "created_at": _utc_now(),
        "experiment_id": experiment_id,
        "run_id": run_id,
        "run_status": run_status,
        "input": {
            "path": input_path,
            "canonical_sha256": input_sha,
            "generator_version": str(
                context.get("generator_version")
                or input_reference.get("generator_version")
                or UNRESOLVED
            ),
            "registry_version": str(
                context.get("registry_version")
                or input_reference.get("registry_version")
                or UNRESOLVED
            ),
            "numeric_mode": str(
                context.get("numeric_mode")
                or input_reference.get("numeric_mode")
                or solver_result.get("numeric_mode")
                or solver_result.get("arithmetic_mode")
                or configuration.get("numeric_mode")
                or UNRESOLVED
            ),
            "seed": request.get("seed"),
            "prng": str(
                context.get("prng")
                or input_reference.get("prng")
                or UNRESOLVED
            ),
            "replicate": request.get("repeat"),
            "lifecycle": input_lifecycle,
        },
        "algorithm": {
            "name": str(request.get("algorithm", UNRESOLVED)),
            "version": str(
                context.get("algorithm_version")
                or provenance.get("algorithm_version")
                or provenance.get("solver_version")
                or UNRESOLVED
            ),
            "objective": str(request.get("objective", UNRESOLVED)),
            "model_regime": str(
                context.get("model_regime")
                or configuration.get("model_regime")
                or UNRESOLVED
            ),
        },
        "backends": {
            "exact": _backend_role(roles, "exact"),
            "heuristic": _backend_role(roles, "heuristic"),
            "relaxation": _backend_role(roles, "relaxation"),
            "reported": reported_backend,
        },
        "numeric_mode": str(
            context.get("numeric_mode")
            or input_reference.get("numeric_mode")
            or solver_result.get("numeric_mode")
            or solver_result.get("arithmetic_mode")
            or configuration.get("numeric_mode")
            or UNRESOLVED
        ),
        "budgets": budgets,
        "tolerances": tolerances,
        "build": {
            "compiler": compiler,
            "compiler_version": str(compiler_version),
            "build_type": build_type,
            "libraries": deepcopy(dict(libraries)),
        },
        "git": {
            "commit": git_state.get("commit"),
            "dirty": git_state.get("dirty"),
            "status_sha256": git_state.get("status_sha256"),
            "captured_at": git_state.get("captured_at", _utc_now()),
        },
        "resolved_configuration": deepcopy(dict(configuration)),
        "artifacts": _artifact_roles(
            root, result_document, known_artifacts, context
        ),
    }
    manifest["manifest_sha256"] = hashlib.sha256(
        _canonical_bytes(manifest)
    ).hexdigest()
    validate_run_manifest(manifest, run_root=root)
    return manifest


def validate_run_manifest(
    document: Mapping[str, Any], *, run_root: str | Path | None = None
) -> str:
    """Validate schema, self-digest, safe paths, and artifact bytes."""
    _validate_schema(document)
    unsealed = deepcopy(dict(document))
    expected = unsealed.pop("manifest_sha256")
    actual = hashlib.sha256(_canonical_bytes(unsealed)).hexdigest()
    if actual != expected:
        raise ValueError("experiment-run manifest SHA-256 mismatch")
    if run_root is None:
        return actual

    root = Path(run_root).resolve()
    input_info = document["input"]
    _verify_artifact(
        root,
        input_info["path"],
        input_info["canonical_sha256"],
        None,
    )
    for role, artifacts in document["artifacts"].items():
        for artifact in artifacts:
            _verify_artifact(
                root,
                artifact["path"],
                artifact["sha256"],
                artifact["size_bytes"],
            )
    return actual


def _verify_artifact(
    run_root: Path, relative_path: str, expected_sha: str, expected_size: int | None
) -> None:
    path = _safe_artifact_path(run_root, relative_path)
    if not path.is_file():
        raise FileNotFoundError(f"manifest artifact is missing: {relative_path}")
    actual_sha = _sha256_file(path)
    if actual_sha != expected_sha:
        raise ValueError(f"manifest artifact checksum mismatch: {relative_path}")
    if expected_size is not None and path.stat().st_size != expected_size:
        raise ValueError(f"manifest artifact size mismatch: {relative_path}")


def write_run_manifest_snapshot(
    run_root: str | Path, document: Mapping[str, Any]
) -> Path:
    """Publish a new immutable manifest ID/hash pair without replacing files."""
    root = Path(run_root).resolve()
    digest = validate_run_manifest(document, run_root=root)
    manifest_id = document["manifest_id"]
    target_dir = root / "manifest_snapshots"
    target_dir.mkdir(parents=True, exist_ok=True)
    if any(target_dir.glob(f"{manifest_id}-*.json")):
        raise FileExistsError(f"manifest ID already exists: {manifest_id}")
    filename = f"{manifest_id}-{digest[:16]}.json"
    serialized = _canonical_bytes(document)
    from .storage import write_bytes_create_only

    write_bytes_create_only(target_dir, filename, serialized)
    return target_dir / filename


def read_run_manifest(
    path: str | Path, *, run_root: str | Path | None = None
) -> dict[str, Any]:
    """Load one manifest snapshot and validate its digest and referenced files."""
    source = Path(path)
    try:
        document = loads_json(source.read_bytes())
    except (OSError, json.JSONDecodeError, UnicodeDecodeError) as error:
        raise ValueError(f"cannot read experiment-run manifest: {error}") from error
    if not isinstance(document, dict):
        raise ValueError("experiment-run manifest root must be an object")
    digest = validate_run_manifest(document, run_root=run_root)
    expected_name = f"{document['manifest_id']}-{digest[:16]}.json"
    if source.name != expected_name:
        raise ValueError("manifest filename does not match its ID and digest")
    return document


def read_run_manifest_snapshots(run_root: str | Path) -> list[dict[str, Any]]:
    """Read and validate all append-only v2 snapshots for one run directory."""
    root = Path(run_root).resolve()
    directory = root / "manifest_snapshots"
    if not directory.exists():
        return []
    if not directory.is_dir():
        raise ValueError("manifest_snapshots path is not a directory")
    return [
        read_run_manifest(path, run_root=root)
        for path in sorted(directory.glob("*.json"))
    ]
