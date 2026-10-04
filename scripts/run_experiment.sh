#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
PIPELINE="$ROOT/scripts/experiment_pipeline.py"
ORIGINAL_ARGS=("$@")

DATASET="data/instances/public_instance_set"
DATASET_PROFILE="full"
DATASET_EXPLICIT=0
OUTPUT=""
ALGORITHMS="all-fast"
ALGORITHMS_EXPLICIT=0
MODES="both"
SEED=42
REPEATS=1
THREADS=4
FAST_LIMIT=30
EXACT_LIMIT=600
STATIC_LIMIT=60
EXACT_REFERENCE="auto"
ANIMATION_TOP_N=1
ANIMATION_MODE="both"
ANIMATION_INSTANCES=""
SKIP_PREFLIGHT=0
SKIP_FIGURES=0
SKIP_TABLES=0
SKIP_ANIMATIONS=0
ANIMATIONS_EXPLICIT=0
RESUME=0
FORCE=0
THREADS_EXPLICIT=0
FAST_LIMIT_EXPLICIT=0
EXACT_LIMIT_EXPLICIT=0
STATIC_LIMIT_EXPLICIT=0

usage() {
  cat <<'EOF'
Usage: bash scripts/run_experiment.sh [OPTIONS]

Options:
  --dataset PATH                   Dataset directory or canonical JSON file
  --dataset-profile full|smoke10   Built-in dataset profile (default: full)
  --output PATH                    Experiment directory (default: unique results/experiments/<ID>)
  --algorithms all-fast|all-comparison|LIST
  --modes minmax|minsum|both
  --seed N
  --repeats N
  --threads N
  --fast-time-limit SEC
  --exact-time-limit SEC
  --time-limit SEC                 Per-static-solve limit
  --exact-reference auto|ip-kont|branch-and-bound
  --animation-top-n N
  --animation-mode minmax|minsum|both
  --animation-instances LIST       Comma-separated instance names or JSON basenames
  --skip-preflight
  --skip-figures
  --skip-tables
  --skip-animations
  --with-animations                Enable animations for smoke profiles
  --resume                         Resume the latest incomplete matching experiment
  --force                          Continue after preflight failure; never marks it passed
  --help
EOF
}

fail_usage() {
  echo "run_experiment.sh: $*" >&2
  usage >&2
  exit 2
}

while (($#)); do
  case "$1" in
    --dataset|--dataset-profile|--output|--algorithms|--modes|--seed|--repeats|--threads|\
    --fast-time-limit|--exact-time-limit|--time-limit|--exact-reference|\
    --animation-top-n|--animation-mode|--animation-instances)
      (($# >= 2)) || fail_usage "missing value for $1"
      option="$1"
      value="$2"
      shift 2
      case "$option" in
        --dataset) DATASET="$value"; DATASET_EXPLICIT=1 ;;
        --dataset-profile) DATASET_PROFILE="$value" ;;
        --output) OUTPUT="$value" ;;
        --algorithms) ALGORITHMS="$value"; ALGORITHMS_EXPLICIT=1 ;;
        --modes) MODES="$value" ;;
        --seed) SEED="$value" ;;
        --repeats) REPEATS="$value" ;;
        --threads) THREADS="$value"; THREADS_EXPLICIT=1 ;;
        --fast-time-limit) FAST_LIMIT="$value"; FAST_LIMIT_EXPLICIT=1 ;;
        --exact-time-limit) EXACT_LIMIT="$value"; EXACT_LIMIT_EXPLICIT=1 ;;
        --time-limit) STATIC_LIMIT="$value"; STATIC_LIMIT_EXPLICIT=1 ;;
        --exact-reference) EXACT_REFERENCE="$value" ;;
        --animation-top-n) ANIMATION_TOP_N="$value" ;;
        --animation-mode) ANIMATION_MODE="$value" ;;
        --animation-instances) ANIMATION_INSTANCES="$value" ;;
      esac
      ;;
    --skip-preflight) SKIP_PREFLIGHT=1; shift ;;
    --skip-figures) SKIP_FIGURES=1; shift ;;
    --skip-tables) SKIP_TABLES=1; shift ;;
    --skip-animations) SKIP_ANIMATIONS=1; shift ;;
    --with-animations) SKIP_ANIMATIONS=0; ANIMATIONS_EXPLICIT=1; shift ;;
    --resume) RESUME=1; shift ;;
    --force) FORCE=1; shift ;;
    --help|-h) usage; exit 0 ;;
    *) fail_usage "unknown option: $1" ;;
  esac
done

if [[ "$DATASET_PROFILE" == "full" && "$DATASET_EXPLICIT" -eq 1 ]]; then
  candidate_dataset="$DATASET"
  [[ "$candidate_dataset" = /* ]] || candidate_dataset="$ROOT/$candidate_dataset"
  candidate_manifest=""
  if [[ -d "$candidate_dataset" && -s "$candidate_dataset/manifest.json" ]]; then
    candidate_manifest="$candidate_dataset/manifest.json"
  elif [[ -f "$candidate_dataset" ]]; then
    candidate_manifest="$candidate_dataset"
  fi
  if [[ -n "$candidate_manifest" ]] && python3 - "$candidate_manifest" <<'PY'
import json, sys
from pathlib import Path
payload = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
raise SystemExit(0 if isinstance(payload, dict)
                 and payload.get("name") == "smoke10" else 1)
PY
  then
    DATASET_PROFILE="smoke10"
  fi
fi
case "$DATASET_PROFILE" in full|smoke10) ;; *) fail_usage "invalid --dataset-profile value" ;; esac
if [[ "$DATASET_PROFILE" == "smoke10" ]]; then
  if ((!DATASET_EXPLICIT)); then
    DATASET="data/test_sets/smoke10.json"
  fi
  if ((!ALGORITHMS_EXPLICIT)); then
    ALGORITHMS="nn,greedy,primal-dual"
  fi
  ((THREADS_EXPLICIT)) || THREADS=2
  ((FAST_LIMIT_EXPLICIT)) || FAST_LIMIT=5
  ((EXACT_LIMIT_EXPLICIT)) || EXACT_LIMIT=10
  ((STATIC_LIMIT_EXPLICIT)) || STATIC_LIMIT=2
  ((ANIMATIONS_EXPLICIT)) || SKIP_ANIMATIONS=1
fi
case "$MODES" in minmax|minsum|both) ;; *) fail_usage "invalid --modes value" ;; esac
case "$ANIMATION_MODE" in minmax|minsum|both) ;; *) fail_usage "invalid --animation-mode value" ;; esac
case "$EXACT_REFERENCE" in auto|ip-kont|branch-and-bound) ;; *) fail_usage "invalid --exact-reference value" ;; esac
[[ "$SEED" =~ ^[0-9]+$ ]] || fail_usage "--seed must be a nonnegative integer"
[[ "$REPEATS" =~ ^[1-9][0-9]*$ ]] || fail_usage "--repeats must be positive"
[[ "$THREADS" =~ ^[1-9][0-9]*$ ]] || fail_usage "--threads must be positive"
[[ "$ANIMATION_TOP_N" =~ ^[0-9]+$ ]] || fail_usage "--animation-top-n must be nonnegative"
for limit in "$FAST_LIMIT" "$EXACT_LIMIT" "$STATIC_LIMIT"; do
  awk -v value="$limit" 'BEGIN { exit !(value + 0 > 0) }' \
    || fail_usage "time limits must be positive numbers"
done

resolve_from_root() {
  case "$1" in
    /*) printf '%s\n' "$1" ;;
    *) printf '%s\n' "$ROOT/$1" ;;
  esac
}

DATASET_PATH="$(resolve_from_root "$DATASET")"
[[ -e "$DATASET_PATH" ]] || fail_usage "dataset not found: $DATASET_PATH"
SELECTION_MANIFEST=""
SOURCE_DATASET_PATH="$DATASET_PATH"
if [[ "$DATASET_PROFILE" == "smoke10" ]]; then
  if [[ -d "$DATASET_PATH" && -s "$DATASET_PATH/manifest.json" ]]; then
    SELECTION_MANIFEST="$DATASET_PATH/manifest.json"
  elif [[ -f "$DATASET_PATH" ]]; then
    SELECTION_MANIFEST="$DATASET_PATH"
  else
    fail_usage "smoke10 requires a selection manifest or materialized smoke dataset directory"
  fi
  SOURCE_DATASET_PATH="$(python3 - "$SELECTION_MANIFEST" "$ROOT" <<'PY'
import json, sys
from pathlib import Path
manifest_path = Path(sys.argv[1])
root = Path(sys.argv[2])
manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
if not isinstance(manifest.get("instances"), list):
    raise SystemExit("selection manifest has no instances array")
source_root = Path(str(manifest.get("source_root", ".")))
if source_root == Path(".") and manifest_path.parent.name != "test_sets":
    source_root = manifest_path.parent
elif not source_root.is_absolute():
    source_root = root / source_root
print(source_root.resolve())
PY
)" || fail_usage "unable to read smoke10 selection manifest"
fi
EXP_ROOT="$ROOT/results/experiments"
mkdir -p "$EXP_ROOT"
SOURCE_FINGERPRINT="$(python3 "$PIPELINE" fingerprint --path "$SOURCE_DATASET_PATH")"
SELECTION_FINGERPRINT=""
if [[ -n "$SELECTION_MANIFEST" ]]; then
  SELECTION_FINGERPRINT="$(python3 "$PIPELINE" fingerprint --path "$SELECTION_MANIFEST")"
fi

if ((RESUME)); then
  if [[ -n "$OUTPUT" ]]; then
    EXPERIMENT_DIR="$(resolve_from_root "$OUTPUT")"
  else
    EXPERIMENT_DIR="$(python3 "$PIPELINE" resume --root "$EXP_ROOT" \
      --source "$DATASET_PATH" --source-fingerprint "$SOURCE_FINGERPRINT")"
  fi
  [[ -n "$EXPERIMENT_DIR" && -d "$EXPERIMENT_DIR" ]] \
    || fail_usage "no incomplete matching experiment to resume"
else
  if [[ -n "$OUTPUT" ]]; then
    EXPERIMENT_DIR="$(resolve_from_root "$OUTPUT")"
    [[ ! -e "$EXPERIMENT_DIR" ]] || fail_usage "output already exists; use --resume or choose another path"
  else
    EXPERIMENT_ID="$(date '+%Y-%m-%d_%H%M%S')_$$"
    EXPERIMENT_DIR="$EXP_ROOT/$EXPERIMENT_ID"
    collision=0
    while :; do
      if mkdir "$EXPERIMENT_DIR" 2>/dev/null; then
        break
      fi
      [[ -e "$EXPERIMENT_DIR" ]] || fail_usage "unable to create experiment output: $EXPERIMENT_DIR"
      collision=$((collision + 1))
      EXPERIMENT_DIR="$EXP_ROOT/${EXPERIMENT_ID}_$collision"
    done
  fi
fi

if ((!RESUME)) && [[ -n "$OUTPUT" ]]; then
  mkdir -p "$(dirname "$EXPERIMENT_DIR")"
  mkdir "$EXPERIMENT_DIR" || fail_usage "unable to create experiment output: $EXPERIMENT_DIR"
fi
mkdir -p "$EXPERIMENT_DIR"/{dataset,preflight,batch,tables,figures,exact,animations,logs,reports}
PIPELINE_LOG="$EXPERIMENT_DIR/logs/pipeline.log"
touch "$PIPELINE_LOG"
REPRO_COMMAND="bash scripts/run_experiment.sh"
for argument in "${ORIGINAL_ARGS[@]}"; do
  printf -v quoted_argument '%q' "$argument"
  REPRO_COMMAND+=" $quoted_argument"
done
python3 "$PIPELINE" init --experiment "$EXPERIMENT_DIR" \
  --dataset "$DATASET_PATH" --dataset-label "$DATASET" \
  --dataset-fingerprint "$SOURCE_FINGERPRINT" \
  --dataset-profile "$DATASET_PROFILE" \
  --source-dataset-fingerprint "$SOURCE_FINGERPRINT" \
  --selection-fingerprint "$SELECTION_FINGERPRINT" \
  --selection-manifest "$SELECTION_MANIFEST" \
  --algorithms "$ALGORITHMS" --modes "$MODES" --seed "$SEED" \
  --repeats "$REPEATS" --threads "$THREADS" --fast-limit "$FAST_LIMIT" \
  --exact-limit "$EXACT_LIMIT" --static-limit "$STATIC_LIMIT" \
  --animation-top-n "$ANIMATION_TOP_N" --animation-mode "$ANIMATION_MODE" \
  --animation-instances "$ANIMATION_INSTANCES" \
  --exact-reference "$EXACT_REFERENCE" --reproduction-command "$REPRO_COMMAND" \
  $([[ $RESUME -eq 1 ]] && printf '%s' '--resume')

on_exit() {
  local rc=$?
  if ((rc != 0)) && [[ -s "$EXPERIMENT_DIR/experiment_manifest.json" ]]; then
    printf '[%s] PIPELINE EXIT exit_status=%d; writing partial-run report\n' \
      "$(date '+%Y-%m-%dT%H:%M:%S%z')" "$rc" >> "$PIPELINE_LOG"
    if ! python3 "$PIPELINE" finalize --experiment "$EXPERIMENT_DIR" \
      >> "$PIPELINE_LOG" 2>&1; then
      printf '[%s] ERROR: failed to write partial-run report\n' \
        "$(date '+%Y-%m-%dT%H:%M:%S%z')" >> "$PIPELINE_LOG"
    fi
  fi
}
trap on_exit EXIT

log_command() {
  local command_string=""
  printf -v command_string '%q ' "$@"
  printf '[%s] COMMAND: %s\n' "$(date '+%Y-%m-%dT%H:%M:%S%z')" "$command_string" | tee -a "$PIPELINE_LOG"
  python3 "$PIPELINE" stage --experiment "$EXPERIMENT_DIR" \
    --name "$CURRENT_STAGE" --status RUNNING --command "$command_string"
}

stage_done() {
  local name="$1"
  python3 - "$EXPERIMENT_DIR/experiment_manifest.json" "$name" <<'PY'
import json, sys
from pathlib import Path
manifest = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
sys.exit(0 if manifest.get("stages", {}).get(sys.argv[2], {}).get("status") == "COMPLETED" else 1)
PY
}

run_stage() {
  local name="$1"
  local log_name="$2"
  shift 2
  CURRENT_STAGE="$name"
  local log_path="$EXPERIMENT_DIR/logs/$log_name"
  local started ended elapsed rc
  started="$(date +%s)"
  printf '\n========== STAGE %s START %s ==========\n' "$name" "$(date '+%Y-%m-%dT%H:%M:%S%z')" | tee -a "$PIPELINE_LOG"
  log_command "$@" | tee -a "$log_path"
  set +e
  "$@" > >(tee -a "$log_path") 2>&1
  rc=$?
  set -e
  ended="$(date +%s)"
  elapsed=$((ended - started))
  printf '[%s] STAGE %s ENDED exit_status=%d duration_sec=%d\n' \
    "$(date '+%Y-%m-%dT%H:%M:%S%z')" "$name" "$rc" "$elapsed" | tee -a "$PIPELINE_LOG"
  if ((rc == 0)); then
    python3 "$PIPELINE" stage --experiment "$EXPERIMENT_DIR" \
      --name "$name" --status COMPLETED --exit-code 0
    return 0
  fi
  python3 "$PIPELINE" stage --experiment "$EXPERIMENT_DIR" \
    --name "$name" --status FAILED --exit-code "$rc" \
    --message "See logs/$log_name"
  return "$rc"
}

skip_stage() {
  local name="$1"
  local reason="$2"
  printf '\n========== STAGE %s SKIPPED: %s ==========\n' "$name" "$reason" | tee -a "$PIPELINE_LOG"
  python3 "$PIPELINE" stage --experiment "$EXPERIMENT_DIR" \
    --name "$name" --status SKIPPED --message "$reason"
}

echo "Experiment directory: $EXPERIMENT_DIR" | tee -a "$PIPELINE_LOG"

if ((SKIP_PREFLIGHT)); then
  printf '# Preflight\n\nSkipped by `--skip-preflight`.\n' > "$EXPERIMENT_DIR/preflight/preflight_report.md"
  skip_stage preflight "requested by --skip-preflight"
else
  if ! stage_done preflight; then
    if run_stage preflight preflight.log python3 "$ROOT/scripts/preflight_check.py" \
      --build-jobs "$THREADS"; then
      :
    else
      preflight_rc=$?
      if [[ -f "$ROOT/results/preflight/preflight_report.md" ]]; then
        run_stage preflight_snapshot preflight.log python3 "$PIPELINE" \
          preflight-snapshot --experiment "$EXPERIMENT_DIR"
      else
        printf '# Preflight failure\n\nPreflight exited %s before producing its report. See `../logs/preflight.log`.\n' \
          "$preflight_rc" > "$EXPERIMENT_DIR/preflight/preflight_report.md"
      fi
      if ((FORCE)); then
        printf '[%s] PREFLIGHT FAILED but continuing due to --force; failure remains recorded.\n' "$(date '+%Y-%m-%dT%H:%M:%S%z')" | tee -a "$PIPELINE_LOG"
      else
        exit "$preflight_rc"
      fi
    fi
  fi
  if [[ -f "$ROOT/results/preflight/preflight_report.md" ]] && ! stage_done preflight_snapshot; then
    run_stage preflight_snapshot preflight.log python3 "$PIPELINE" \
      preflight-snapshot --experiment "$EXPERIMENT_DIR"
  fi
fi

CANONICAL_FINGERPRINT=""
if [[ -f "$EXPERIMENT_DIR/experiment_manifest.json" ]]; then
  CANONICAL_FINGERPRINT="$(python3 - "$EXPERIMENT_DIR/experiment_manifest.json" <<'PY'
import json, sys
from pathlib import Path
print(json.loads(Path(sys.argv[1]).read_text(encoding="utf-8")).get("dataset_fingerprint") or "")
PY
)"
fi
if ! stage_done dataset; then
  if [[ -d "$DATASET_PATH" ]] && find "$DATASET_PATH" -type f -name '*.mdc' -print -quit | grep -q .; then
    source_mdc_count="$(find "$DATASET_PATH" -type f -name '*.mdc' | wc -l | tr -d ' ')"
    canonical_json_count="$(find "$EXPERIMENT_DIR/dataset" -type f -name '*.json' | wc -l | tr -d ' ')"
    if [[ "$source_mdc_count" != "$canonical_json_count" ]]; then
      run_stage dataset_conversion dataset_conversion.log python3 "$ROOT/scripts/prepare_real_instances.py" \
        --input "$DATASET_PATH" --output "$EXPERIMENT_DIR/dataset"
    fi
  fi
  dataset_args=(python3 "$PIPELINE" dataset
    --experiment "$EXPERIMENT_DIR" --source "$DATASET_PATH")
  if ((RESUME)); then
    dataset_args+=(--resume --canonical-fingerprint "$CANONICAL_FINGERPRINT")
  fi
  run_stage dataset dataset_conversion.log "${dataset_args[@]}"
else
  python3 "$PIPELINE" dataset --experiment "$EXPERIMENT_DIR" \
    --source "$DATASET_PATH" --resume \
    --canonical-fingerprint "$CANONICAL_FINGERPRINT"
fi

batch_artifacts_exist=1
for required in master_results.json master_results.csv batch_summary.md experiment_manifest.json; do
  [[ -s "$EXPERIMENT_DIR/batch/$required" ]] || batch_artifacts_exist=0
done
if ! stage_done fast_benchmark || ((batch_artifacts_exist == 0)); then
  batch_args=(
    python3 "$ROOT/scripts/run_batch.py"
    --instances "$EXPERIMENT_DIR/dataset"
    --output "$EXPERIMENT_DIR/batch"
    --algorithms "$ALGORITHMS"
    --profile fast
    --modes "$MODES"
    --exact-reference "$EXACT_REFERENCE"
    --seed "$SEED" --repeats "$REPEATS"
    --parallel --threads "$THREADS"
    --time-limit "$STATIC_LIMIT"
    --fast-time-limit "$FAST_LIMIT"
    --exact-time-limit "$EXACT_LIMIT"
    --force
    --dataset-profile "$DATASET_PROFILE"
  )
  if [[ -n "$SELECTION_MANIFEST" ]]; then
    batch_args+=(--dataset-manifest "$SELECTION_MANIFEST")
  fi
  run_stage fast_benchmark fast_benchmark.log "${batch_args[@]}"
fi
for required in master_results.json master_results.csv batch_summary.md experiment_manifest.json; do
  [[ -s "$EXPERIMENT_DIR/batch/$required" ]] || {
    python3 "$PIPELINE" stage --experiment "$EXPERIMENT_DIR" \
      --name fast_benchmark --status FAILED --exit-code 1 \
      --message "missing required artifact: batch/$required"
    echo "Missing required benchmark artifact: $EXPERIMENT_DIR/batch/$required" >&2
    exit 1
  }
done
python3 - "$EXPERIMENT_DIR" <<'PY'
import json, sys
from pathlib import Path
experiment = Path(sys.argv[1])
manifest_path = experiment / "experiment_manifest.json"
batch_path = experiment / "batch" / "experiment_manifest.json"
manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
batch = json.loads(batch_path.read_text(encoding="utf-8"))
manifest["algorithm_set"] = batch.get("algorithm_set", [])
manifest["selected_exact_backend"] = (
    batch.get("selected_backend") or batch.get("selected_exact_reference")
)
manifest["actual_exact_backend"] = batch.get("actual_backend")
manifest_path.write_text(json.dumps(manifest, indent=2) + "\n",
                         encoding="utf-8")
PY

if ! stage_done result_validation || [[ ! -s "$EXPERIMENT_DIR/reports/result_integrity_report.md" ]]; then
  run_stage result_validation fast_benchmark.log python3 "$PIPELINE" validate \
    --experiment "$EXPERIMENT_DIR" --results "$EXPERIMENT_DIR/batch/master_results.json"
fi
if ((SKIP_TABLES)); then
  skip_stage tables "requested by --skip-tables"
else
  if ! stage_done tables || [[ ! -s "$EXPERIMENT_DIR/tables/00_index.md" ]] || \
    [[ ! -s "$EXPERIMENT_DIR/tables/master.md" ]] || \
    [[ ! -s "$EXPERIMENT_DIR/tables/master.csv" ]] || \
    ! python3 "$PIPELINE" check-tables --experiment "$EXPERIMENT_DIR"; then
    run_stage tables tables.log python3 "$ROOT/scripts/build_tables.py" \
      --input "$EXPERIMENT_DIR/batch/master_results.json" --output "$EXPERIMENT_DIR/tables"
    run_stage research_summary tables.log python3 "$PIPELINE" summary \
      --experiment "$EXPERIMENT_DIR"
  elif [[ ! -s "$EXPERIMENT_DIR/reports/benchmark_summary.md" ]]; then
    run_stage research_summary tables.log python3 "$PIPELINE" summary \
      --experiment "$EXPERIMENT_DIR"
  fi
fi
if ((SKIP_FIGURES)); then
  skip_stage figures "requested by --skip-figures"
else
  figure_png_count="$(find "$EXPERIMENT_DIR/figures" -type f -name '*.png' | wc -l | tr -d ' ')"
  figure_pdf_count="$(find "$EXPERIMENT_DIR/figures" -type f -name '*.pdf' | wc -l | tr -d ' ')"
  if ! stage_done figures || ((figure_png_count == 0 || figure_pdf_count == 0)) || \
    ! python3 "$PIPELINE" check-figures --experiment "$EXPERIMENT_DIR"; then
    run_stage figures figures.log python3 "$ROOT/scripts/plot_comparisons.py" \
      --input "$EXPERIMENT_DIR/batch/master_results.json" \
      --output "$EXPERIMENT_DIR/figures" --dpi 300
  fi
fi

if ((SKIP_ANIMATIONS)); then
  skip_stage exact_animation_selection "requested by --skip-animations"
  skip_stage exact_animation_solves "requested by --skip-animations"
  skip_stage exact_animations "requested by --skip-animations"
else
  if ! stage_done exact_animation_selection || [[ ! -s "$EXPERIMENT_DIR/exact/animation_instances.txt" ]]; then
    run_stage exact_animation_selection exact.log python3 "$PIPELINE" select \
      --experiment "$EXPERIMENT_DIR" --names "$ANIMATION_INSTANCES" \
      --top-n "$ANIMATION_TOP_N"
  fi
  if ! stage_done exact_animation_solves || \
    { [[ -s "$EXPERIMENT_DIR/exact/animation_instances.txt" ]] && \
      [[ ! -s "$EXPERIMENT_DIR/exact/solves/master_results.json" ]]; }; then
    SELECTED_BACKEND="$(python3 - "$EXPERIMENT_DIR/batch/experiment_manifest.json" <<'PY'
import json, sys
from pathlib import Path
m=json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
print(m.get("selected_backend") or m.get("selected_exact_reference") or "")
PY
)"
    [[ -n "$SELECTED_BACKEND" ]] || {
      echo "Experiment manifest does not identify a selected exact backend." >&2
      exit 1
    }
    if [[ -s "$EXPERIMENT_DIR/exact/animation_instances.txt" ]]; then
      exact_modes="$ANIMATION_MODE"
      run_stage exact_animation_solves exact.log python3 "$ROOT/scripts/run_batch.py" \
        --instances "$EXPERIMENT_DIR/exact/animation_dataset" \
        --output "$EXPERIMENT_DIR/exact/solves" \
        --algorithms "$SELECTED_BACKEND" --profile exact-reference \
        --modes "$exact_modes" --exact-reference "$SELECTED_BACKEND" \
        --seed "$SEED" --repeats 1 --parallel --threads "$THREADS" \
        --time-limit "$STATIC_LIMIT" --fast-time-limit "$FAST_LIMIT" \
        --exact-time-limit "$EXACT_LIMIT" --force
    else
      printf 'No animation candidates selected.\n' > "$EXPERIMENT_DIR/logs/exact.log"
      python3 "$PIPELINE" stage --experiment "$EXPERIMENT_DIR" \
        --name exact_animation_solves --status COMPLETED --exit-code 0 \
        --message "No candidates were selected"
    fi
  fi
  if ! stage_done exact_animations || \
    [[ ! -s "$EXPERIMENT_DIR/reports/exact_animation_report.md" ]] || \
    ! python3 "$PIPELINE" check-animations --experiment "$EXPERIMENT_DIR"; then
    case "$ANIMATION_MODE" in
      both) ANIMATION_MODES="minmax,minsum" ;;
      *) ANIMATION_MODES="$ANIMATION_MODE" ;;
    esac
    run_stage exact_animations animations.log python3 "$PIPELINE" animations \
      --experiment "$EXPERIMENT_DIR" --modes "$ANIMATION_MODES"
  fi
fi

run_stage final_report pipeline.log python3 "$PIPELINE" finalize \
  --experiment "$EXPERIMENT_DIR"

printf '\nExperiment status: ' | tee -a "$PIPELINE_LOG"
python3 - "$EXPERIMENT_DIR/experiment_manifest.json" <<'PY' | tee -a "$PIPELINE_LOG"
import json, sys
from pathlib import Path
print(json.loads(Path(sys.argv[1]).read_text(encoding="utf-8")).get("status", "UNKNOWN"))
PY
python3 - "$EXPERIMENT_DIR" <<'PY' | tee -a "$PIPELINE_LOG"
import json, sys
from pathlib import Path
experiment = Path(sys.argv[1])
manifest = json.loads((experiment / "experiment_manifest.json").read_text())
batch_path = experiment / "batch" / "experiment_manifest.json"
batch = json.loads(batch_path.read_text()) if batch_path.is_file() else {}
print(f"Dataset profile: {manifest.get('dataset_profile', 'full')}")
print(f"Selected instances: {manifest.get('instance_count')}")
print("Algorithms: " + ", ".join(batch.get("algorithm_set", [])))
print("Objectives: " + ", ".join(manifest.get("objectives", [])))
print(f"Threads: {manifest.get('thread_count')}")
print("Time limits (static/fast/exact): "
      f"{manifest.get('per_static_time_limit_sec')} / "
      f"{manifest.get('fast_time_limit_sec')} / "
      f"{manifest.get('exact_time_limit_sec')} seconds")
if manifest.get("dataset_profile") == "smoke10":
    print("Selected families: " + ", ".join(sorted({
        str(row.get("family")) for row in manifest.get("selected_files", [])
    })))
PY
printf 'Experiment artifacts: %s\n' "$EXPERIMENT_DIR"
