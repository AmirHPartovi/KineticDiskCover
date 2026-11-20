# KDC Stage 01 Baseline

Date: 2026-11-19
Repository: /Users/amirpartovi/Desktop/Codes/KineticDiskCover

## Git baseline

- Branch: `main`
- HEAD: `9d0bb0a415b8e3bb5a58bb1621b10db7768b9273`
- Initial status: clean (`## main...upstream/main`)
- Initial unstaged diff: none
- Initial staged diff: none
- Initial untracked files: none
- Pre-existing modified files to protect: none

## Repository structure inspected

- `CMakeLists.txt`
- `README.md`
- `include/kdc/`
- `include/kdc/algorithms/`
- `src/`
- `src/algorithms/`
- `tests/`
- `tests/python/`
- `benchmarks/`
- `configs/pipeline/` with `full.json`, `reference.json`, and `smoke.json`
- `docs/`
- `build/` existing build tree
- `scripts/`
- `data/`
- `requirements.txt`

Key source areas include:

- Kinetic core: `include/kdc/kinetic.hpp`, `src/kinetic.cpp`
- Solution and interval model: `include/kdc/solution.hpp`, `src/solution.cpp`
- MinSum: `include/kdc/minsum.hpp`, `src/minsum.cpp`
- MinMax: `include/kdc/minmax.hpp`, `src/minmax.cpp`
- MinMaxSum: `include/kdc/minmaxsum.hpp`, `src/minmaxsum.cpp`
- Verification: `include/kdc/verify.hpp`, `src/verify.cpp`
- Static ILP and geometry: `include/kdc/stationary.hpp`, `src/stationary.cpp`, `include/kdc/candidate.hpp`, `src/candidate.cpp`
- Static algorithms: `include/kdc/algorithms/`, `src/algorithms/`

## README build and test instructions

Valid repository commands documented by `README.md` and implemented by
`scripts/local_validation.sh` are:

```sh
KDC_REPO=/path/to/KineticDiskCover
bash "$KDC_REPO/scripts/local_validation.sh" configure-build
bash "$KDC_REPO/scripts/local_validation.sh" ctest
bash "$KDC_REPO/scripts/local_validation.sh" python-tests
bash "$KDC_REPO/scripts/local_validation.sh" certification
```

The existing script resolves the repository and build directory itself.
`configure-build` runs CMake with `BUILD_TESTS=ON` and `BUILD_BENCHMARK=ON`,
then builds with 2 parallel jobs by default.

The CMake test registration in the existing build is one test named `all`
and executes `build/kdc-tests --allow-running-no-tests`.

## Existing build environment

- CMake: 4.5.0
- Generator: Unix Makefiles
- Make: `/usr/bin/make`
- Compiler: Apple Clang 17.0.0.17000603
- Target architecture: arm64
- Build type: Release
- CMake source directory: repository root
- CMake build directory: `build/`
- C++ standard: C++17
- Existing executables:
  - `build/kdc-tests`
  - `build/kdc-solver`
  - `build/kdc-kinetic-certification`
- Existing bundled dependencies:
  - Catch2
  - Eigen
  - nlohmann/json
  - spdlog

The build cache reports CMake's `_deps` sources under `build/_deps`.
No clean build or build cleanup was performed.

## Solver backends

- Native KONT/COPT compile-time API: PASS
- Detected KONT: `/Applications/kont72/include`, `/Applications/kont72/include/kontcpp_inc`
- Detected KONT library: `/Applications/kont72/lib/libkont_cpp.dylib`
- Detected KONT version: 7.2.4
- COPT headers/library: not found
- Native runtime probe: FAIL
- License/runtime initialization: FAIL
- Failure reason: invalid MAC address for the local license
  (`76:74:D1:0F:5A:19`)
- Native backend: unavailable
- `--exact-reference auto` fallback: `branch-and-bound`
- Built-in `branch-and-bound` backend remains the usable local exact backend.

## Python environment

- Python: 3.9.6
- Available: pandas 2.3.3, numpy 2.0.2, pytest 8.4.2, matplotlib 3.7.2,
  seaborn 0.13.2, scipy 1.13.1, imageio 2.37.2, ffmpeg-python, jsonschema 4.25.1
- `requirements.txt` is present and lists pandas, numpy, pytest, matplotlib,
  seaborn, scipy, ffmpeg-python, imageio[ffmpeg], and jsonschema.

## Test baseline

Passed:

```sh
ctest --test-dir build --output-on-failure
```

Result: 1/1 test passed; total time 10.88 seconds.

The native backend probe was executed read-only except that it appended two
log lines to `results/logs/kdc.log`. Those lines were removed exactly after
inspection, restoring the initial clean worktree.

Not executed:

- `bash scripts/local_validation.sh configure-build`
  - Risk: can modify build configuration, fetched dependency cache, and build
    artifacts. A clean build was not requested.
- `bash scripts/local_validation.sh python-tests`
  - Risk: pytest may update `.pytest_cache`, bytecode caches, and temporary
    test outputs. The existing repository already contains these caches.
- `bash scripts/local_validation.sh certification`
  - Risk: creates a fresh experiment under `results/experiments/` and writes
    generated CSV/log/manifest/artifact files.
- `bash scripts/local_validation.sh experiment ...`
  - Risk: creates or overwrites experiment-specific outputs and generated
    reports/figures/animations.

## Limitations and blockers

- Native KONT/COPT cannot initialize because of an invalid local license MAC
  address, despite a successful compile/link API check.
- The build is usable through the built-in `branch-and-bound` fallback.
- No external solver license setup or dependency installation was performed.
- No clean build was performed.
- No configure/build output was regenerated.
- No Python or experiment suite was run because those commands can write caches,
  tracked/previous artifacts, or new experiment directories.

## Final worktree state

Final Git status after baseline inspection and cleanup of the probe log append:
clean, no unstaged diff, no staged diff, no untracked files.

## Acceptance

- PASS: Branch, HEAD, status, diff, staged changes, and untracked files recorded.
- PASS: Actual repository structure recorded.
- PASS: README/local-validation build and test commands identified.
- PASS: Existing build configuration and toolchain inspected.
- PASS: Compiler, CMake, generator, Python dependencies, and solver backend identified.
- PASS: Safe existing CTest suite passed.
- PASS: No source, test, config, documentation, dependency, or build cleanup changes remain.
