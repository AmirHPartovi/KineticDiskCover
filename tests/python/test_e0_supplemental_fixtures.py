import hashlib
import json
from pathlib import Path


FIXTURE_PATH = (
    Path(__file__).resolve().parents[1]
    / "fixtures"
    / "e0_supplemental_program_v2_v1.json"
)


def _canonical_case_hash(case):
    payload = {key: value for key, value in case.items() if key != "sha256"}
    encoded = json.dumps(
        payload,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
        allow_nan=False,
    ).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def test_supplemental_program_cases_have_stable_canonical_hashes():
    document = json.loads(FIXTURE_PATH.read_text(encoding="utf-8"))
    assert document["schema_version"] == 1
    assert document["dataset_class"] == (
        "supplemental_synthetic_not_thesis_generated"
    )
    cases = document["cases"]
    assert len(cases) == 5
    assert len({case["case_id"] for case in cases}) == len(cases)

    for case in cases:
        assert case["case_id"].startswith("SUP-PROG-")
        assert "supplemental synthetic case" in case["source_reference"]
        assert case["expected"]["continuous_coverage"] is True
        assert case["expected"]["serialized_program_cost_equals_ub"] is True
        assert case["sha256"] == _canonical_case_hash(case)
