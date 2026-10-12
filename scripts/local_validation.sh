#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"

usage() {
  cat <<'EOF'
Usage: bash scripts/local_validation.sh COMMAND [ARGS]

Commands:
  configure-build              Configure CMake and build tests/benchmarks
  ctest                        Run the registered C++ tests
  python-tests                 Run tests/python with pytest
  certification [N [R [START]]]
                               Run differential certification (defaults: 200 1 0)
  experiment PROFILE [OPTIONS]
                               Run smoke, reference, or full via run_experiment.sh
  --help

Set KDC_BUILD_JOBS to override the build parallelism (default: 2).
Set KONT_ROOT, COPT_HOME, or KONT_HOME to discover an optional native backend.
EOF
}

if (($# == 0)); then
  usage >&2
  exit 2
fi

command="$1"
shift

case "$command" in
  configure-build)
    (($# == 0)) || { echo "configure-build takes no arguments" >&2; exit 2; }
    jobs="${KDC_BUILD_JOBS:-2}"
    [[ "$jobs" =~ ^[1-9][0-9]*$ ]] || {
      echo "KDC_BUILD_JOBS must be a positive integer" >&2
      exit 2
    }
    cmake -S "$ROOT" -B "$ROOT/build" \
      -DBUILD_TESTS=ON -DBUILD_BENCHMARK=ON
    cmake --build "$ROOT/build" --parallel "$jobs"
    ;;
  ctest)
    (($# == 0)) || { echo "ctest takes no arguments" >&2; exit 2; }
    [[ -f "$ROOT/build/CTestTestfile.cmake" ]] || {
      echo "CTest build not found; run configure-build first: $ROOT/build" >&2
      exit 1
    }
    ctest --test-dir "$ROOT/build" --output-on-failure
    ;;
  python-tests)
    (($# == 0)) || { echo "python-tests takes no arguments" >&2; exit 2; }
    cd "$ROOT"
    exec python3 -m pytest -q tests/python
    ;;
  certification)
    (($# <= 3)) || {
      echo "certification accepts at most N, R, and START" >&2
      exit 2
    }
    seed_count="${1:-200}"
    repetitions="${2:-1}"
    start_index="${3:-0}"
    [[ "$seed_count" =~ ^[1-9][0-9]*$ ]] || {
      echo "seed count must be a positive integer" >&2
      exit 2
    }
    [[ "$repetitions" =~ ^[1-9][0-9]*$ ]] || {
      echo "repetitions must be a positive integer" >&2
      exit 2
    }
    [[ "$start_index" =~ ^[0-9]+$ ]] || {
      echo "start index must be a nonnegative integer" >&2
      exit 2
    }
    certification="$ROOT/build/kdc-kinetic-certification"
    [[ -x "$certification" ]] || {
      echo "certification executable not found; run configure-build first: $certification" >&2
      exit 1
    }
    experiments="$ROOT/results/experiments"
    mkdir -p "$experiments"
    while :; do
      experiment_id="$(python3 "$ROOT/scripts/experiment_pipeline.py" \
        new-id --profile certification)"
      experiment_dir="$experiments/$experiment_id"
      if mkdir "$experiment_dir" 2>/dev/null; then
        break
      fi
      [[ -e "$experiment_dir" ]] || {
        echo "unable to create certification experiment: $experiment_dir" >&2
        exit 1
      }
    done
    mkdir -p "$experiment_dir/certification" "$experiment_dir/logs"
    output="$experiment_dir/certification/kinetic-certification.csv"
    log="$experiment_dir/logs/certification.log"
    command_text="$(printf '%q ' "$certification" "$seed_count" \
      "$repetitions" "$start_index")"
    python3 - "$ROOT" "$experiment_dir" "$command_text" \
      "$seed_count" "$repetitions" "$start_index" <<'PY'
import datetime, json, platform, subprocess, sys
from pathlib import Path
root, directory, command = Path(sys.argv[1]), Path(sys.argv[2]), sys.argv[3]
seed_count, repetitions, start_index = map(int, sys.argv[4:7])
commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=root,
                        text=True, capture_output=True, check=False)
dirty = subprocess.run(["git", "status", "--porcelain"], cwd=root,
                       text=True, capture_output=True, check=False)
cache = root / "build" / "CMakeCache.txt"
cache_values = {}
if cache.is_file():
    for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith(("CMAKE_CXX_COMPILER:FILEPATH=",
                            "CMAKE_BUILD_TYPE:STRING=")):
            key, value = line.split("=", 1)
            cache_values[key.split(":", 1)[0]] = value
manifest = {
    "experiment_id": directory.name,
    "timestamp": datetime.datetime.now(datetime.timezone.utc).isoformat(),
    "experiment_label": "kinetic differential certification",
    "repository": "AmirHPartovi/KineticDiskCover",
    "git_commit": commit.stdout.strip() if commit.returncode == 0 else None,
    "git_dirty": bool(dirty.stdout.strip()) if dirty.returncode == 0 else None,
    "host": {"platform": platform.platform(), "machine": platform.machine()},
    "python_version": platform.python_version(),
    "compiler": cache_values.get("CMAKE_CXX_COMPILER"),
    "build_type": cache_values.get("CMAKE_BUILD_TYPE"),
    "certification": {
        "seed_count": seed_count,
        "repetitions": repetitions,
        "start_index": start_index,
    },
    "reproduction_command": command,
    "status": "RUNNING",
    "stages": {"certification": {"status": "RUNNING", "command": command}},
    "required_stages": ["certification"],
}
path = directory / "experiment_manifest.json"
temporary = path.with_suffix(".json.tmp")
temporary.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
temporary.replace(path)
PY
    set +e
    "$certification" "$seed_count" "$repetitions" "$start_index" \
      > "$output" 2> "$log"
    result=$?
    set -e
    if ((result == 0)) && [[ ! -s "$output" ]]; then
      result=1
    fi
    python3 - "$ROOT" "$experiment_dir" "$result" "$command_text" <<'PY'
import datetime, json, sys
from pathlib import Path
root, directory = Path(sys.argv[1]), Path(sys.argv[2])
code, command = int(sys.argv[3]), sys.argv[4]
manifest_path = directory / "experiment_manifest.json"
manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
status = "COMPLETED" if code == 0 else "FAILED"
now = datetime.datetime.now(datetime.timezone.utc).isoformat()
manifest["status"] = "COMPLETE" if code == 0 else "PARTIAL"
manifest["stages"]["certification"].update({
    "status": status, "command": command, "exit_code": code,
    "completed_at": now,
})
temporary = manifest_path.with_suffix(".json.tmp")
temporary.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
temporary.replace(manifest_path)
sys.path.insert(0, str(root / "scripts"))
from kdc_tools.storage import index_experiment_artifacts
index_experiment_artifacts(directory)
PY
    printf 'Certification experiment: %s\n' "$experiment_dir"
    printf 'Certification output: %s\n' "$output"
    if ((result != 0)); then
      printf 'Certification failed with exit status %d; partial output retained.\n' \
        "$result" >&2
      exit "$result"
    fi
    printf 'Certification passed (%s seeds, %s repetitions).\n' \
      "$seed_count" "$repetitions"
    ;;
  experiment)
    (($# >= 1)) || {
      echo "experiment requires a profile: smoke, reference, or full" >&2
      exit 2
    }
    profile="$1"
    shift
    case "$profile" in smoke|reference|full) ;; *)
      echo "unknown profile '$profile'; expected smoke, reference, or full" >&2
      exit 2
    esac
    exec bash "$ROOT/scripts/run_experiment.sh" --pipeline "$profile" "$@"
    ;;
  --help|-h|help)
    usage
    ;;
  *)
    echo "unknown command: $command" >&2
    usage >&2
    exit 2
    ;;
esac
