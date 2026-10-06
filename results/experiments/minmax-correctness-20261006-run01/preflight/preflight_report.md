# Pre-flight Sanity Check Report

- **Summary:** 19 / 19 checks passed (0 failed)
- **Total check duration:** 0.0457511 s

| Check | Status | Duration (s) | Detail |
|---|---|---:|---|
|algorithm:branch-and-bound|PASS|0.000107584|cost=18173.818308|
|algorithm:brute-force|PASS|0.00306662|cost=18173.818308|
|algorithm:genetic|PASS|0.0147869|cost=18173.818308|
|algorithm:greedy|PASS|3.7583e-05|cost=22280.712426|
|algorithm:ip-kont|PASS|0|skipped: optional native KONT/COPT backend is unavailable|
|algorithm:local-search|PASS|3.975e-05|cost=18173.818308|
|algorithm:lp-rounding|PASS|6.3291e-05|cost=18173.818308|
|algorithm:nn|PASS|8.5e-06|cost=26677.144374|
|algorithm:primal-dual|PASS|2.8083e-05|cost=33804.698710|
|algorithm:sa|PASS|0.00415967|cost=24702.914351|
|algorithm:shifting|PASS|0.00332075|cost=26677.144374|
|all_algorithms_on_dummy|PASS|0.0190605|all algorithms produced feasible solutions|
|data_instances_directory|PASS|0.000162459|1 JSON instances found in data/instances|
|ilp_backend|PASS|0|using built-in-branch-and-bound-fallback|
|minmax_pipeline|PASS|7.0958e-05|min-max pipeline OK using branch-and-bound, peak=10072.263854|
|minsum_pipeline|PASS|0.00013975|min-sum pipeline OK using branch-and-bound, integral=10029.728856|
|registry_populated|PASS|1.3667e-05|registered: branch-and-bound, brute-force, genetic, greedy, ip-kont, local-search, lp-rounding, nn, primal-dual, sa, shifting|
|serializer_roundtrip|PASS|0.000561291|serializer roundtrip OK|
|verifier_accepts_all|PASS|0.000123792|verifier accepts the NN kinetic solution|
