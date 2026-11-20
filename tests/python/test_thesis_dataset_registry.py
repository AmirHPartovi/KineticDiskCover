import copy
import hashlib
import math
from pathlib import Path
import sys

import pytest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))

from kdc_tools.schemas import (  # noqa: E402
    SchemaValidationError,
    dumps_json,
    validate_document,
)
from kdc_tools.thesis_dataset_registry import (  # noqa: E402
    FAMILY_IDS,
    GeneratorNotReadyError,
    derive_seed,
    family_blockers,
    load_spec,
    read_generated_instance,
    seal_generated_instance,
    validate_spec,
    verify_generated_instance,
    write_content_addressed_json,
    write_generated_instance,
)


def _record_metadata(**overrides):
    metadata = {
        "instance_id": "EC1-reference-r0",
        "family": "EC",
        "cell_id": "EC1",
        "replicate": None,
        "n": 1,
        "m": 1,
        "s": 0,
        "T": 1.0,
        "K": 1,
        "numeric_mode": "FLOAT64",
        "seed": None,
        "generator_version": "fixture-v1",
        "prng_version": None,
        "parent": None,
        "source": "synthetic-test-fixture",
        "distribution_transform": "deterministic fixture",
    }
    metadata.update(overrides)
    return metadata


def test_chapter7_registry_schema_and_count_arithmetic():
    spec = load_spec()
    validate_spec(spec)
    validate_document(spec, "thesis_dataset_generator_spec_v1")

    counts = spec["counts"]
    assert counts["unconditional"]["planned_memberships"] == 323
    assert counts["unconditional"]["planned_unique_files"] == 293
    assert counts["unconditional"]["family_unique_files"] == 70
    assert counts["unconditional"]["scale_shared_memberships_with_F1"] == 30
    assert counts["conditional_1d"]["total_memberships_if_1d_enabled"] == 338
    assert counts["conditional_1d"]["total_unique_files_if_1d_enabled"] == 308
    assert counts["source_evidence"]["generated_files_verified_in_thesis_package"] == 0


@pytest.mark.parametrize("family_id", FAMILY_IDS)
def test_seed_replay_is_blocked_until_each_family_is_fully_specified(family_id):
    spec = load_spec()
    family = spec["families"][family_id]
    assert family["generation_status"] in {"BLOCKED", "DISABLED"}
    assert family_blockers(family_id)
    with pytest.raises(GeneratorNotReadyError, match=family_id):
        from kdc_tools.thesis_dataset_registry import require_generation_ready

        require_generation_ready(family_id)


@pytest.mark.parametrize(
    "case_id",
    [f"EC{index}" for index in range(1, 14)],
)
def test_every_boundary_case_is_registered_but_numeric_parameters_are_unresolved(case_id):
    cases = load_spec()["families"]["EC"]["parameters"]["cases"]
    case = next(row for row in cases if row["id"] == case_id)
    assert case["construction"]
    assert case["test"]
    assert "Exact coordinates" in load_spec()["families"]["EC"]["unresolved_fields"][0]


def test_scale_grids_keep_memberships_separate_from_unique_files():
    families = load_spec()["families"]
    assert [
        families[name]["parameters"]["n_values"] for name in ("S1",)
    ] == [[5, 10, 20, 40, 80]]
    assert families["S2"]["parameters"]["m_values"] == [2, 3, 5, 10, 20]
    assert families["S3"]["parameters"]["s_values"] == [0, 1, 2, 4, 8]
    for family_id in ("S1", "S2", "S3"):
        assert families[family_id]["planned_memberships"] == 50
        assert families[family_id]["planned_unique_files"] == 40
        assert families[family_id]["shared_memberships"] == 10


def test_family_transforms_and_d1_gate_are_preserved_exactly():
    families = load_spec()["families"]
    assert families["F5"]["parameters"]["velocity_multiplier"] == 0.1
    assert families["F6"]["parameters"]["velocity_multiplier"] == 3
    assert families["F5"]["parameters"]["parent_family"] == "F1"
    assert families["F6"]["parameters"]["parent_family"] == "F1"
    assert families["F7"]["parameters"]["duplicate_center_pairs"] == "floor(m/2)"
    assert families["F3"]["parameters"]["point_grid"]["y"] == [
        0, "1/3", "2/3", 1
    ]
    assert families["1D"]["generation_status"] == "DISABLED"
    assert "license" in " ".join(families["1D"]["unresolved_fields"]).lower()


@pytest.mark.parametrize(
    ("family_id", "memberships", "unique_files"),
    [
        ("F1", 10, 10),
        *((f"F{index}", None, None) for index in range(2, 8)),
        ("S1", 50, 40),
        ("S2", 50, 40),
        ("S3", 50, 40),
        ("X", 90, 90),
        ("EC", 13, 13),
        ("1D", 15, 15),
    ],
)
def test_family_planned_memberships_and_unique_file_counts(
    family_id, memberships, unique_files
):
    family = load_spec()["families"][family_id]
    assert family["planned_memberships"] == memberships
    assert family["planned_unique_files"] == unique_files


def test_seed_mixer_matches_a_fixed_vector_and_requires_thesis_component_count():
    assert derive_seed(0, [0, 0, 0, 0, 0, 0]) == 0
    assert derive_seed(42, [1, 2, 3, 4, 5, 6]) == 0x80AE133C813B40FB
    with pytest.raises(ValueError, match="six components"):
        derive_seed(42, [1, 2, 3])
    with pytest.raises(TypeError, match="integers"):
        derive_seed(42, [1, 2, True, 4, 5, 6])


def test_generated_record_digest_and_round_trip_read(tmp_path):
    record = seal_generated_instance(
        _record_metadata(),
        {
            "stations": [{"id": 0, "x": 0.0, "y": 0.0}],
            "trajectories": [{"points": [{"x": 0.25, "y": 0.5}]}],
        },
    )
    digest = verify_generated_instance(record)
    assert digest == record["metadata"]["sha256"]
    assert digest == hashlib.sha256(
        (dumps_json({
            "schema_version": 1,
            "metadata": {
                key: value
                for key, value in record["metadata"].items()
                if key != "sha256"
            },
            "instance": record["instance"],
        }, pretty=True) + "\n").encode("utf-8")
    ).hexdigest()

    path = tmp_path / "sha256" / digest[:2] / f"{digest}.json"
    path.parent.mkdir(parents=True)
    path.write_bytes((dumps_json(record, pretty=True) + "\n").encode("utf-8"))
    first_bytes = path.read_bytes()
    assert first_bytes.endswith(b"\n")
    assert b"NaN" not in first_bytes and b"Infinity" not in first_bytes
    assert read_generated_instance(path) == record


def test_content_addressed_json_is_create_only_and_changes_get_new_hash(tmp_path):
    payload = {"value": 1.0, "name": "fixture"}
    path = write_content_addressed_json(tmp_path, payload)
    first_bytes = path.read_bytes()
    digest = hashlib.sha256(first_bytes).hexdigest()
    assert path == tmp_path / "sha256" / digest[:2] / f"{digest}.json"
    assert first_bytes.endswith(b"\n")
    assert write_content_addressed_json(tmp_path, payload) == path
    assert path.read_bytes() == first_bytes

    changed_path = write_content_addressed_json(
        tmp_path, {"value": 2.0, "name": "fixture"}
    )
    assert changed_path != path
    assert path.read_bytes() == first_bytes


def test_generated_instance_writer_refuses_unresolved_family(tmp_path):
    record = seal_generated_instance(_record_metadata(), {"value": 1})
    with pytest.raises(GeneratorNotReadyError):
        write_generated_instance(tmp_path, record)
    assert not list(tmp_path.rglob("*.json"))


def test_generated_record_digest_tampering_and_nonfinite_values_are_rejected():
    record = seal_generated_instance(
        _record_metadata(),
        {"value": 1.0},
    )
    tampered = copy.deepcopy(record)
    tampered["instance"]["value"] = 2.0
    with pytest.raises(ValueError, match="SHA-256 mismatch"):
        verify_generated_instance(tampered)

    with pytest.raises(ValueError):
        seal_generated_instance(_record_metadata(), {"value": math.inf})


def test_generated_record_schema_requires_generator_provenance():
    record = seal_generated_instance(_record_metadata(), {})
    del record["metadata"]["prng_version"]
    with pytest.raises(SchemaValidationError):
        verify_generated_instance(record)


def test_registry_fails_closed_when_family_set_or_counts_are_corrupted():
    spec = load_spec()
    incomplete = copy.deepcopy(spec)
    del incomplete["families"]["F7"]
    with pytest.raises(SchemaValidationError):
        validate_spec(incomplete)

    wrong_count = copy.deepcopy(spec)
    wrong_count["families"]["S1"]["planned_unique_files"] += 1
    with pytest.raises(ValueError, match="planned family/scale counts"):
        validate_spec(wrong_count)

    falsely_ready_ec = copy.deepcopy(spec)
    falsely_ready_ec["families"]["EC"]["unresolved_fields"] = []
    falsely_ready_ec["families"]["EC"]["generation_status"] = "READY"
    falsely_ready_ec["generation_status"] = "PARTIALLY_READY"
    with pytest.raises(ValueError, match="EC cannot be READY"):
        validate_spec(falsely_ready_ec)
