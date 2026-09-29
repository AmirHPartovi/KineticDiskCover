import importlib.util
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location(
    "run_batch_limited", ROOT / "scripts" / "run_batch_limited.py"
)
assert SPEC is not None and SPEC.loader is not None
run_batch_limited = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = run_batch_limited
SPEC.loader.exec_module(run_batch_limited)


def test_algorithms_are_ordered_by_execution_phase():
    selected = {"branch-and-bound", "nn", "ip-kont", "greedy", "sa"}

    assert run_batch_limited.order_algorithms(selected) == [
        "nn", "greedy", "sa", "ip-kont", "branch-and-bound"
    ]


def test_clear_previous_output_removes_batch_artifacts_only(tmp_path):
    output = tmp_path / "output"
    stale_run = output / "runs" / "old" / "nn" / "minmax"
    stale_run.mkdir(parents=True)
    (stale_run / "result.json").write_text("{}")
    for filename in ("master_results.json", "master_results.csv",
                     "batch_summary.md"):
        (output / filename).write_text("stale")
    unrelated = output / "notes.txt"
    unrelated.write_text("keep")

    run_batch_limited.clear_previous_output(output)

    assert not (output / "runs").exists()
    assert not (output / "master_results.json").exists()
    assert not (output / "master_results.csv").exists()
    assert not (output / "batch_summary.md").exists()
    assert unrelated.read_text() == "keep"
