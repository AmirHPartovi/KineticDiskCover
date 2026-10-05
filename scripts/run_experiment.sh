#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$SCRIPT_DIR/.." && pwd)"
PIPELINE="$ROOT/scripts/experiment_pipeline.py"
ORIGINAL_ARGS=("$@")

PIPELINE_PROFILE="full"
for ((index=0; index < ${#ORIGINAL_ARGS[@]}; index++)); do
  if [[ "${ORIGINAL_ARGS[$index]}" == "--pipeline" ]]; then
    ((index + 1 < ${#ORIGINAL_ARGS[@]})) || {
      echo "run_experiment.sh: missing value for --pipeline" >&2
      exit 2
    }
    PIPELINE_PROFILE="${ORIGINAL_ARGS[$((index + 1))]}"
    break
  fi
done
CONFIG_OUTPUT="$(python3 "$PIPELINE" config --pipeline "$PIPELINE_PROFILE" \
  --format shell)" || exit 2
while IFS=$'\t' read -r key value; do
  case "$key" in
    PIPELINE_PROFILE) PIPELINE_PROFILE="$value" ;;
    PIPELINE_LABEL) PIPELINE_LABEL="$value" ;;
    DATASET_PROFILE) DATASET_PROFILE="$value" ;;
    DATASET) DATASET="$value" ;;
    ALGORITHMS) ALGORITHMS="$value" ;;
    EXACT_REFERENCE) EXACT_REFERENCE="$value" ;;
    MODES) MODES="$value" ;;
    SEED) SEED="$value" ;;
    REPEATS) REPEATS="$value" ;;
    THREADS) THREADS="$value" ;;
    FAST_LIMIT) FAST_LIMIT="$value" ;;
    EXACT_LIMIT) EXACT_LIMIT="$value" ;;
    STATIC_LIMIT) STATIC_LIMIT="$value" ;;
    BENCHMARK_PROFILE) BENCHMARK_PROFILE="$value" ;;
    VERIFY_AFTER) VERIFY_AFTER="$value" ;;
    VERIFY_EACH_ITERATION) VERIFY_EACH_ITERATION="$value" ;;
    SAVE_TRACES) SAVE_TRACES="$value" ;;
    SAVE_SOLUTIONS) SAVE_SOLUTIONS="$value" ;;
    TABLES_ENABLED) TABLES_ENABLED="$value" ;;
    FIGURES_ENABLED) FIGURES_ENABLED="$value" ;;
    ANIMATION_ENABLED) ANIMATION_ENABLED="$value" ;;
    ANIMATION_POLICY) ANIMATION_POLICY="$value" ;;
    ANIMATION_TOP_N) ANIMATION_TOP_N="$value" ;;
    ANIMATION_FPS) ANIMATION_FPS="$value" ;;
    ANIMATION_FRAMES) ANIMATION_FRAMES="$value" ;;
    ANIMATION_DPI) ANIMATION_DPI="$value" ;;
    ARTIFACT_RETENTION_DAYS) ARTIFACT_RETENTION_DAYS="$value" ;;
    SMOKE_COUNT) SMOKE_COUNT="$value" ;;
    SMOKE_MAX_N) SMOKE_MAX_N="$value" ;;
    SMOKE_MAX_M) SMOKE_MAX_M="$value" ;;
    *) echo "run_experiment.sh: unexpected config key: $key" >&2; exit 2 ;;
  esac
done <<< "$CONFIG_OUTPUT"

DATASET_EXPLICIT=0
DATASET_PROFILE_EXPLICIT=0
OUTPUT=""
ALGORITHMS_EXPLICIT=0
ANIMATION_MODE="both"
ANIMATION_INSTANCES=""
SKIP_PREFLIGHT=0
SKIP_FIGURES=0
SKIP_TABLES=0
SKIP_ANIMATIONS=0
ANIMATIONS_EXPLICIT=0
ANIMATION_POLICY_EXPLICIT=0
RESUME=0
FORCE=0
THREADS_EXPLICIT=0
FAST_LIMIT_EXPLICIT=0
EXACT_LIMIT_EXPLICIT=0
STATIC_LIMIT_EXPLICIT=0
REPEATS_EXPLICIT=0
SEED_EXPLICIT=0
SMOKE_MAX_N_EXPLICIT=0
SMOKE_MAX_M_EXPLICIT=0

usage() {
  cat <<'EOF'
Usage: bash scripts/run_experiment.sh [OPTIONS]

Options:
  --pipeline smoke|reference|full
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
  --animation-policy best|all-algorithms
  --all-algorithms                 Animate every algorithm for selected instances
  --smoke-max-n N                  Smoke instance trajectory limit
  --smoke-max-m N                  Smoke station limit
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
    --pipeline|--dataset|--dataset-profile|--output|--algorithms|--modes|--seed|--repeats|--threads|\
    --fast-time-limit|--exact-time-limit|--time-limit|--exact-reference|\
    --animation-top-n|--animation-mode|--animation-instances|--animation-policy|\
    --smoke-max-n|--smoke-max-m)
      (($# >= 2)) || fail_usage "missing value for $1"
      option="$1"
      value="$2"
      shift 2
      case "$option" in
        --pipeline) PIPELINE_PROFILE="$value" ;;
        --dataset) DATASET="$value"; DATASET_EXPLICIT=1 ;;
        --dataset-profile) DATASET_PROFILE="$value"; DATASET_PROFILE_EXPLICIT=1 ;;
        --output) OUTPUT="$value" ;;
        --algorithms) ALGORITHMS="$value"; ALGORITHMS_EXPLICIT=1 ;;
        --modes) MODES="$value" ;;
        --seed) SEED="$value"; SEED_EXPLICIT=1 ;;
        --repeats) REPEATS="$value"; REPEATS_EXPLICIT=1 ;;
        --threads) THREADS="$value"; THREADS_EXPLICIT=1 ;;
        --fast-time-limit) FAST_LIMIT="$value"; FAST_LIMIT_EXPLICIT=1 ;;
        --exact-time-limit) EXACT_LIMIT="$value"; EXACT_LIMIT_EXPLICIT=1 ;;
        --time-limit) STATIC_LIMIT="$value"; STATIC_LIMIT_EXPLICIT=1 ;;
        --exact-reference) EXACT_REFERENCE="$value" ;;
        --animation-top-n) ANIMATION_TOP_N="$value" ;;
        --animation-mode) ANIMATION_MODE="$value" ;;
        --animation-instances) ANIMATION_INSTANCES="$value" ;;
        --animation-policy) ANIMATION_POLICY="$value"; ANIMATION_POLICY_EXPLICIT=1 ;;
        --smoke-max-n) SMOKE_MAX_N="$value"; SMOKE_MAX_N_EXPLICIT=1 ;;
        --smoke-max-m) SMOKE_MAX_M="$value"; SMOKE_MAX_M_EXPLICIT=1 ;;
      esac
      ;;
    --skip-preflight) SKIP_PREFLIGHT=1; shift ;;
    --skip-figures) SKIP_FIGURES=1; shift ;;
    --skip-tables) SKIP_TABLES=1; shift ;;
    --skip-animations) SKIP_ANIMATIONS=1; shift ;;
    --with-animations) SKIP_ANIMATIONS=0; ANIMATIONS_EXPLICIT=1; shift ;;
    --all-algorithms) ANIMATION_POLICY="all-algorithms"; ANIMATION_POLICY_EXPLICIT=1; shift ;;
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
case "$PIPELINE_PROFILE" in smoke|reference|full) ;; *) fail_usage "invalid --pipeline value" ;; esac
case "$PIPELINE_PROFILE:$DATASET_PROFILE" in
  smoke:smoke10|reference:full|full:full) ;;
  smoke:*) fail_usage "smoke pipeline requires dataset profile smoke10" ;;
  *) fail_usage "$PIPELINE_PROFILE pipeline requires dataset profile full" ;;
esac
if [[ "$PIPELINE_PROFILE" == reference ]]; then
  case "$ALGORITHMS" in
    nn|greedy|primal-dual|local-search|sa|genetic|lp-rounding|shifting) ;;
    *) fail_usage "reference pipeline requires exactly one heuristic algorithm" ;;
  esac
fi
case "$MODES" in minmax|minsum|both) ;; *) fail_usage "invalid --modes value" ;; esac
case "$ANIMATION_MODE" in minmax|minsum|both) ;; *) fail_usage "invalid --animation-mode value" ;; esac
case "$ANIMATION_POLICY" in best|all-algorithms) ;; *) fail_usage "invalid --animation-policy value" ;; esac
case "$EXACT_REFERENCE" in auto|ip-kont|branch-and-bound) ;; *) fail_usage "invalid --exact-reference value" ;; esac
[[ "$SEED" =~ ^[0-9]+$ ]] || fail_usage "--seed must be a nonnegative integer"
[[ "$REPEATS" =~ ^[1-9][0-9]*$ ]] || fail_usage "--repeats must be positive"
[[ "$THREADS" =~ ^[1-9][0-9]*$ ]] || fail_usage "--threads must be positive"
[[ "$ANIMATION_TOP_N" =~ ^[0-9]+$ ]] || fail_usage "--animation-top-n must be nonnegative"
[[ "$SMOKE_MAX_N" =~ ^[1-9][0-9]*$ ]] || fail_usage "--smoke-max-n must be positive"
[[ "$SMOKE_MAX_M" =~ ^[1-9][0-9]*$ ]] || fail_usage "--smoke-max-m must be positive"
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
TEMP_SELECTION_DIR=""
if [[ "$DATASET_PROFILE" == "smoke10" ]]; then
  if [[ -d "$DATASET_PATH" && -s "$DATASET_PATH/manifest.json" && \
        -d "$DATASET_PATH/instances" ]]; then
    SELECTION_MANIFEST="$DATASET_PATH/manifest.json"
  elif [[ -f "$DATASET_PATH" ]]; then
    SELECTION_MANIFEST="$DATASET_PATH"
  elif [[ -d "$DATASET_PATH" ]]; then
    SOURCE_DATASET_PATH="$DATASET_PATH"
    EXP_ROOT="$ROOT/results/experiments"
    mkdir -p "$EXP_ROOT"
    TEMP_SELECTION_DIR="$(mktemp -d "$EXP_ROOT/.selection-${PIPELINE_PROFILE}.XXXXXX")"
    python3 "$ROOT/scripts/select_test_instances.py" \
      --source "$DATASET_PATH" --output "$TEMP_SELECTION_DIR/materialized" \
      --count "$SMOKE_COUNT" --max-n "$SMOKE_MAX_N" --max-m "$SMOKE_MAX_M" \
      --manifest-output "$TEMP_SELECTION_DIR/selection.json" \
      || fail_usage "unable to select smoke instances from $DATASET_PATH"
    SELECTION_MANIFEST="$TEMP_SELECTION_DIR/selection.json"
    DATASET_PATH="$SELECTION_MANIFEST"
  else
    fail_usage "smoke10 requires a dataset directory, selection manifest, or materialized smoke dataset"
  fi
  if [[ -z "$TEMP_SELECTION_DIR" ]]; then
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
    EXPERIMENT_ID="kdc-${PIPELINE_PROFILE}-$(date '+%Y-%m-%d_%H%M%S')_$$"
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
mkdir -p "$EXPERIMENT_DIR"/{dataset,preflight,calibration,batch,tables,figures,animations,logs,reports}
PIPELINE_LOG="$EXPERIMENT_DIR/logs/pipeline.log"
touch "$PIPELINE_LOG"
REPRO_COMMAND="bash scripts/run_experiment.sh"
for argument in "${ORIGINAL_ARGS[@]}"; do
  printf -v quoted_argument '%q' "$argument"
  REPRO_COMMAND+=" $quoted_argument"
done
python3 "$PIPELINE" init --experiment "$EXPERIMENT_DIR" \
  --pipeline "$PIPELINE_PROFILE" \
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
  --animation-policy "$ANIMATION_POLICY" \
  --animation-instances "$ANIMATION_INSTANCES" \
  --exact-reference "$EXACT_REFERENCE" --benchmark-profile "$BENCHMARK_PROFILE" \
  --verify-after "$VERIFY_AFTER" --verify-each-iteration "$VERIFY_EACH_ITERATION" \
  --save-traces "$SAVE_TRACES" --save-solutions "$SAVE_SOLUTIONS" \
  --reproduction-command "$REPRO_COMMAND" \
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
      --name "$name" --status COMPLETED --exit-code 0 \
      --duration-sec "$elapsed"
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
    --name "$name" --status SKIPPED --exit-code 0 --duration-sec 0 \
    --message "$reason"
}

echo "Experiment directory: $EXPERIMENT_DIR" | tee -a "$PIPELINE_LOG"

CANONICAL_FINGERPRINT=""
if [[ -s "$EXPERIMENT_DIR/experiment_manifest.json" ]]; then
  CANONICAL_FINGERPRINT="$(python3 - "$EXPERIMENT_DIR/experiment_manifest.json" <<'PY'
import json, sys
from pathlib import Path
print(json.loads(Path(sys.argv[1]).read_text(encoding="utf-8")).get("dataset_fingerprint") or "")
PY
)"
fi
if ! stage_done dataset; then
  run_stage dataset dataset.log python3 "$PIPELINE" dataset \
    --experiment "$EXPERIMENT_DIR" --source "$DATASET_PATH"
else
  python3 "$PIPELINE" dataset --experiment "$EXPERIMENT_DIR" \
    --source "$DATASET_PATH" --resume \
    --canonical-fingerprint "$CANONICAL_FINGERPRINT"
fi
if [[ -s "$EXPERIMENT_DIR/dataset_selection_manifest.json" ]]; then
  SELECTION_MANIFEST="$EXPERIMENT_DIR/dataset_selection_manifest.json"
fi
if [[ -n "$TEMP_SELECTION_DIR" ]]; then
  rm -rf -- "$TEMP_SELECTION_DIR"
  TEMP_SELECTION_DIR=""
fi

if ((SKIP_PREFLIGHT)); then
  printf '# Preflight\n\nSkipped by `--skip-preflight`.\n' > "$EXPERIMENT_DIR/preflight/preflight_report.md"
  skip_stage build "requested by --skip-preflight"
  skip_stage tests "requested by --skip-preflight"
  skip_stage preflight "requested by --skip-preflight"
else
  if ! stage_done build; then
    run_stage build build.log bash -c \
      'cmake -S "$1" -B "$1/build" -DCMAKE_BUILD_TYPE=Release && cmake --build "$1/build" --parallel "$2"' \
      _ "$ROOT" "$THREADS"
  fi
  if ! stage_done tests; then
    run_stage tests tests.log ctest --test-dir "$ROOT/build" --output-on-failure
  fi
  if ! stage_done preflight; then
    if run_stage preflight preflight.log "$ROOT/build/kdc-solver" preflight \
      --output "$EXPERIMENT_DIR/preflight/preflight_report.md"; then
      :
    else
      preflight_rc=$?
      if ((!FORCE)); then
        exit "$preflight_rc"
      fi
      printf '[%s] PREFLIGHT FAILED but continuing due to --force; failure remains recorded.\n' \
        "$(date '+%Y-%m-%dT%H:%M:%S%z')" | tee -a "$PIPELINE_LOG"
    fi
  fi
fi

mkdir -p "$EXPERIMENT_DIR/batch"
if ! stage_done backend_selection; then
  run_stage backend_selection backend_selection.log bash -c \
    '"$1/build/kdc-solver" calibrate --dataset "$2/dataset" --output "$2/calibration" --exact-reference "$3" && cp "$2/calibration/experiment_manifest.json" "$2/batch/experiment_manifest.json"' \
    _ "$ROOT" "$EXPERIMENT_DIR" "$EXACT_REFERENCE"
fi

batch_artifacts_exist=1
for required in master_results.json master_results.csv batch_summary.md experiment_manifest.json; do
  [[ -s "$EXPERIMENT_DIR/batch/$required" ]] || batch_artifacts_exist=0
done
if [[ ! -s "$EXPERIMENT_DIR/aggregates/aggregate_manifest.json" ]] || \
   ! python3 - "$EXPERIMENT_DIR/aggregates/aggregate_manifest.json" <<'PY'
import json, sys
from pathlib import Path
manifest = json.loads(Path(sys.argv[1]).read_text(encoding="utf-8"))
sys.exit(0 if manifest.get("complete") is True else 1)
PY
then
  batch_artifacts_exist=0
fi
if ! stage_done benchmark || ((batch_artifacts_exist == 0)); then
  batch_args=(
    python3 "$ROOT/scripts/run_batch.py"
    --instances "$EXPERIMENT_DIR/dataset"
    --output "$EXPERIMENT_DIR/batch"
    --algorithms "$ALGORITHMS"
    --profile "$BENCHMARK_PROFILE"
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
  ((SAVE_SOLUTIONS)) && batch_args+=(--save-solutions)
  ((SAVE_TRACES)) && batch_args+=(--save-traces)
  ((RESUME)) && batch_args+=(--resume)
  if [[ -n "$SELECTION_MANIFEST" ]]; then
    batch_args+=(--dataset-manifest "$SELECTION_MANIFEST")
  fi
  run_stage benchmark benchmark.log "${batch_args[@]}"
fi
for required in master_results.json master_results.csv batch_summary.md experiment_manifest.json; do
  [[ -s "$EXPERIMENT_DIR/batch/$required" ]] || {
    python3 "$PIPELINE" stage --experiment "$EXPERIMENT_DIR" \
      --name benchmark --status FAILED --exit-code 1 \
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
  run_stage result_validation result_validation.log python3 "$PIPELINE" validate \
    --experiment "$EXPERIMENT_DIR" --results "$EXPERIMENT_DIR/batch/master_results.json"
fi
if ((SKIP_TABLES || !TABLES_ENABLED)); then
  skip_stage tables "disabled by pipeline configuration or command-line option"
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
if ((SKIP_FIGURES || !FIGURES_ENABLED)); then
  skip_stage figures "disabled by pipeline configuration or command-line option"
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

if ((SKIP_ANIMATIONS || !ANIMATION_ENABLED)); then
  skip_stage animations "disabled by pipeline configuration or command-line option"
else
  if ! stage_done animations || \
    [[ ! -s "$EXPERIMENT_DIR/reports/exact_animation_report.md" ]] || \
    ! python3 "$PIPELINE" check-animations --experiment "$EXPERIMENT_DIR"; then
    run_stage animations animations.log python3 "$PIPELINE" animations \
      --experiment "$EXPERIMENT_DIR" --modes "$ANIMATION_MODE" \
      --policy "$ANIMATION_POLICY" --instances "$ANIMATION_INSTANCES" \
      --top-n "$ANIMATION_TOP_N" --fps "$ANIMATION_FPS" \
      --frames "$ANIMATION_FRAMES" --dpi "$ANIMATION_DPI"
  fi
fi

run_stage final_report pipeline.log python3 "$PIPELINE" finalize \
  --experiment "$EXPERIMENT_DIR"
run_stage summary summary.log python3 "$PIPELINE" emit-summary \
  --experiment "$EXPERIMENT_DIR"
python3 "$PIPELINE" finalize --experiment "$EXPERIMENT_DIR"

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
