# Local validation and experiment commands

All commands use `scripts/local_validation.sh`, which resolves the repository
root from its own location. Set `KDC_REPO` to the absolute path of your clone,
then invoke the script through that path; the commands work from any current
working directory:

```bash
KDC_REPO=/path/to/KineticDiskCover
bash "$KDC_REPO/scripts/local_validation.sh" configure-build
bash "$KDC_REPO/scripts/local_validation.sh" ctest
bash "$KDC_REPO/scripts/local_validation.sh" python-tests
bash "$KDC_REPO/scripts/local_validation.sh" certification
bash "$KDC_REPO/scripts/local_validation.sh" experiment smoke
bash "$KDC_REPO/scripts/local_validation.sh" experiment reference
bash "$KDC_REPO/scripts/local_validation.sh" experiment full
```

The `configure-build` command configures CMake with `BUILD_TESTS` and
`BUILD_BENCHMARK` enabled, then builds all targets. Override parallelism with
`KDC_BUILD_JOBS=N`. CTest and Python tests can also be run independently after
the build. Python report/test dependencies are listed in `requirements.txt`;
install them in the Python environment used for the commands, for example
with `python3 -m pip install -r "$KDC_REPO/requirements.txt"`.

The certification command defaults to 200 deterministic seeds, one
repetition, and start index zero. Optional arguments are
`certification [SEED_COUNT [REPETITIONS [START_INDEX]]]`. Each campaign gets a
unique `results/experiments/<id>/` directory; its CSV and log are written in
`certification/` and `logs/`, with command, stage status, and artifact
checksums recorded. A failed campaign retains its partial CSV and returns the
executable's nonzero status.
Run `bash "$KDC_REPO/scripts/local_validation.sh" --help` for command help.

Each experiment profile runs through `scripts/run_experiment.sh` and writes
to a unique `results/experiments/` directory. The pipeline runs CMake
configure/build, CTest, the Python test suite, preflight, exact-backend
resolution/calibration, benchmark execution, result validation, tables,
figures, animations, and final reports. Stage status, exit code, duration,
commands, and logs are retained in the experiment manifest and its `logs/`
directory. A failed required build or test stage stops dependent work, is
recorded as `FAILED`, and preserves the nonzero process status. `--force`
only permits continuing after a preflight failure; that stage remains failed
and the final experiment cannot be marked complete.

The experiment tree distinguishes the pipeline `experiment_manifest.json`
(provenance and stage states) from the schema-validated `experiment.json`
(storage lifecycle and immutable plan identity). Canonical attempt records
exist only at `runs/<run-id>/run.json`; `aggregates/` contains derived
aggregates and `batch/` contains compatibility exports. Dataset-selection
evidence is stored in `dataset/`, run input snapshots in `input/`, and the
root `artifacts.json` indexes generated experiment files with SHA-256 and
size. Calibration, preflight, tables, figures, animations, and reports are
created within the experiment only when those stages produce them. Explicit
output paths already in use are not overwritten; resumption requires the
matching configuration and immutable run plan.

Use `bash "$KDC_REPO/scripts/run_experiment.sh" --help` for pipeline options.
The smoke profile materializes a deterministic `smoke20` selection from the
public source dataset using the configured seed and strict `n <= 50`,
`m <= 25` limits. It fails rather than selecting oversized instances if the
configured count cannot be met. The legacy `data/test_sets/smoke10.json`
selection remains unchanged and cannot be relabeled as smoke20. Reference and
full profiles use the complete public dataset by default.
The smoke profile's `all-solutions` visualization stage renders every feasible
run with a valid serialized solution, without filtering out unverified or
time-limited incumbents. It writes a three-panel image at normalized times
`0`, `0.5`, and `1` (converted with the instance's `T_end`), individual
per-solution animations, and one HTML comparison sheet per selected
instance/objective. Missing, skipped, failed, invalid, and non-renderable
slots remain explicit rows/placeholders. Verification kind and optimality are
copied from run records and are never inferred from a successful render.
`reports/visualization_manifest.json` maps artifacts to runs and records
source-solution hashes, rendering parameters, and artifact checksums.
For example, profile overrides can be passed through the wrapper:

```bash
bash "$KDC_REPO/scripts/local_validation.sh" experiment smoke \
  --threads 2 --exact-reference branch-and-bound
```

KONT/COPT is optional. The CMake configure output and preflight/backend
reports distinguish SDK discovery, API compile checks, runtime/license
availability, and an unused or unavailable backend. With
`--exact-reference auto`, the pipeline selects the available native backend
only when its runtime probe and calibration support it; otherwise it uses
the built-in `branch-and-bound`. An explicit unavailable `ip-kont` request
fails with a diagnostic rather than silently substituting a backend.
Animation output uses MP4 when local FFmpeg is available and otherwise falls
back to GIF through Pillow. The visualization audit validates every generated
artifact and verifies comparison-sheet coverage against the immutable run
plan. A plan-only experiment has comparison sheets with planned/no-result
placeholders but cannot produce solution images or animations until solver
runs provide serialized outputs.

This is not a guaranteed offline build: on a fresh CMake build directory,
`FetchContent` may download Eigen, nlohmann-json, Catch2, and spdlog if those
dependencies are not already cached. The Python dependencies likewise need to
be installed locally. No GitHub Actions variables, dispatch events, hosted
runners, workflow artifacts, or remote workflow execution are required.
