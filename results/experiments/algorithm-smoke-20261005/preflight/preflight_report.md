# Pre-flight Sanity Check Report

- **Summary:** 19 / 19 checks passed (0 failed)
- **Total check duration:** 0.0471677 s

| Check | Status | Duration (s) | Detail |
|---|---|---:|---|
|algorithm:branch-and-bound|PASS|9.0208e-05|cost=18173.818308|
|algorithm:brute-force|PASS|0.00320163|cost=18173.818308|
|algorithm:genetic|PASS|0.0144473|cost=18173.818308|
|algorithm:greedy|PASS|3.1833e-05|cost=22280.712426|
|algorithm:ip-kont|PASS|0|skipped: optional native KONT/COPT backend is unavailable|
|algorithm:local-search|PASS|4.1167e-05|cost=18173.818308|
|algorithm:lp-rounding|PASS|8.5334e-05|cost=18173.818308|
|algorithm:nn|PASS|7.958e-06|cost=26677.144374|
|algorithm:primal-dual|PASS|2.85e-05|cost=33804.698710|
|algorithm:sa|PASS|0.00399683|cost=24702.914351|
|algorithm:shifting|PASS|0.00391504|cost=26677.144374|
|all_algorithms_on_dummy|PASS|0.0204106|all algorithms produced feasible solutions|
|data_instances_directory|PASS|0.000148|1 JSON instances found in data/instances|
|ilp_backend|PASS|0|using built-in-branch-and-bound-fallback|
|minmax_pipeline|PASS|7.9625e-05|min-max pipeline OK using branch-and-bound, peak=10072.263854|
|minsum_pipeline|PASS|0.000142042|min-sum pipeline OK using branch-and-bound, integral=10029.728856|
|registry_populated|PASS|2.5333e-05|registered: branch-and-bound, brute-force, genetic, greedy, ip-kont, local-search, lp-rounding, nn, primal-dual, sa, shifting|
|serializer_roundtrip|PASS|0.000424875|serializer roundtrip OK|
|verifier_accepts_all|PASS|9.15e-05|verifier accepts the NN kinetic solution|
