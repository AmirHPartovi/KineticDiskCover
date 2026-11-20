# Pipeline solver selection

## Existing and temporal call paths

The existing kinetic benchmark selects a static assignment algorithm through
`BenchmarkConfig::algorithm_names`. `BenchmarkRunner::run_all` resolves that
list, `run_single` constructs the selected solver through
`StaticSolverRegistry`, and the objective-specific solver invokes the existing
`MinSumSolver`, `MinMaxSolver`, or `MinMaxSumSolver`. The existing MinSum
refinement policy remains controlled by the profile/configuration.

The batch path similarly resolves static algorithm names in
`BatchRunner::run`, then `BatchRunner::run_single` constructs a registered
static solver and invokes the existing kinetic objective solver. The kinetic
path in `AlgorithmComparator::compare_kinetic` also runs registered static
solvers. The registry is therefore not the temporal solver dispatch point.

`--solver-method temporal-interval-bnb` selects a separate Model A path in
`BenchmarkRunner::run_single` and `BatchRunner::run_single`. It directly invokes
`TemporalThesisSolver::solve_v2`; it does not create a static solver or call
`MinSumSolver`. It accepts only the `minsum` objective. In batch mode it does
not require or resolve an exact static backend. The old/default path remains
`legacy`; `--solver-method minsum-adaptive` explicitly selects the existing
`MinSumSolver` path with `HEURISTIC_ADAPTIVE`.

The comparator retains its existing static-algorithm contract and is not an
entry point for temporal interval B&B. This avoids presenting a temporal
certificate as one of the comparator's legacy scalar objective fields.

## Budgets and resolved configuration

The temporal global wall-clock limit uses `--fast-time-limit`. Per-Oracle work
uses `--oracle-time-limit`, independently capped by the remaining global
budget. `--delta-min`, `--max-leaves`, `--max-oracle-calls`, `--epsilon-abs`,
and `--epsilon-rel` configure the thesis loop and stopping rule. The selected
method and resolved budget values are stored in the experiment manifest and
each run's `configuration`.

Example:

```sh
kdc-solver benchmark --dataset data/instances --output results/temporal-smoke \
  --mode minsum --profile fast --solver-method temporal-interval-bnb \
  --fast-time-limit 10 --oracle-time-limit 2 --max-leaves 128
```

## Result schemas

Legacy benchmark records retain schema version 2 and legacy batch records
retain schema version 3. Temporal benchmark records use top-level schema
version 3 and temporal batch records use top-level schema version 4. Both
contain a separate `temporal_result_v2` object with schema version 2; its
`final` section retains bound validity, method, arithmetic, provenance,
certificate, gap, status, and stop-reason semantics. Its `execution` section
records loop status, budget outcome, verifier state, and call counts. A
complete incumbent snapshot is included when available.

Non-finite temporal values are encoded as JSON `null`; their validity and
status fields remain explicit. Temporal CSV and Markdown summaries use their
own field sets and do not assign temporal bounds to legacy `objective_value`,
`lower_bound`, or `upper_bound` columns.

An `INCOMPLETE` temporal result caused by a resolution or configured-cap stop
is not marked as an operational run failure. `failed` is reserved for invalid
inputs, execution errors, verification/reconciliation failures, and
inconsistency; time-limit outcomes are represented separately.
