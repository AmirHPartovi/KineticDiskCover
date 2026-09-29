import importlib.util
import json
from pathlib import Path
import sys

import pytest


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "prepare_real_instances", ROOT / "scripts" / "prepare_real_instances.py"
)
assert SPEC is not None and SPEC.loader is not None
prepare_real_instances = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = prepare_real_instances
SPEC.loader.exec_module(prepare_real_instances)


def test_converts_public_mdc_to_solver_instance(tmp_path):
    source = tmp_path / "sample.mdc"
    source.write_text(json.dumps({
        "name": "public-sample",
        "moving_points": [
            {"start": {"coord": [1, 2]}, "end": {"coord": [3, 4]}},
        ],
        "centers": [{"coord": [5, 6]}],
    }))

    converted = prepare_real_instances.convert_instance(source, 7)

    assert converted == {
        "id": 7,
        "name": "public-sample",
        "T_end": 1.0,
        "stations": [{"id": 0, "x": 5.0, "y": 6.0}],
        "trajectories": [{
            "t_breaks": [0.0, 1.0],
            "waypoints": [{"x": 1.0, "y": 2.0}, {"x": 3.0, "y": 4.0}],
        }],
    }


def test_converts_full_public_dataset(tmp_path):
    source = ROOT / "data" / "instances" / "public_instance_set"
    output = tmp_path / "canonical"

    count = prepare_real_instances.convert_dataset(source, output)

    assert count == 302
    converted = output / "us-night-0000070.json"
    record = json.loads(converted.read_text())
    source_record = json.loads(
        (source / "us-night-0000070.mdc").read_text()
    )
    assert record["name"] == source_record["name"]
    assert len(record["trajectories"]) == len(source_record["moving_points"])
    assert len(record["stations"]) == len(source_record["centers"])


def test_rejects_invalid_coordinate(tmp_path):
    source = tmp_path / "invalid.mdc"
    source.write_text(json.dumps({
        "name": "invalid",
        "moving_points": [
            {"start": {"coord": [1]}, "end": {"coord": [3, 4]}},
        ],
        "centers": [{"coord": [5, 6]}],
    }))

    with pytest.raises(ValueError, match="two-number coordinate"):
        prepare_real_instances.convert_instance(source, 0)
