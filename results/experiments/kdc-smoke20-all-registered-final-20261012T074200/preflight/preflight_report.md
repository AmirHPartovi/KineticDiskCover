# Pre-flight Sanity Check Report

- **Summary:** 19 / 19 checks passed (0 failed)
- **Total check duration:** 0.0468294 s

| Check | Status | Duration (s) | Detail |
|---|---|---:|---|
|algorithm:branch-and-bound|PASS|9.5917e-05|cost=18173.818308|
|algorithm:brute-force|PASS|0.00425317|cost=18173.818308|
|algorithm:genetic|PASS|0.0145815|cost=18173.818308|
|algorithm:greedy|PASS|0.000135542|cost=22280.712426|
|algorithm:ip-kont|PASS|0|skipped: optional native KONT/COPT backend is unavailable|
|algorithm:local-search|PASS|4.025e-05|cost=18173.818308|
|algorithm:lp-rounding|PASS|7.2e-05|cost=18173.818308|
|algorithm:nn|PASS|7.333e-06|cost=26677.144374|
|algorithm:primal-dual|PASS|2.6167e-05|cost=33804.698710|
|algorithm:sa|PASS|0.003966|cost=24702.914351|
|algorithm:shifting|PASS|0.00328229|cost=26677.144374|
|all_algorithms_on_dummy|PASS|0.0187152|all algorithms produced feasible solutions|
|data_instances_directory|PASS|0.000270916|1 JSON instances found in data/instances|
|ilp_backend|PASS|0|using built-in-branch-and-bound-fallback|
|minmax_pipeline|PASS|9.2167e-05|min-max pipeline OK using branch-and-bound, peak=10072.263854|
|minsum_pipeline|PASS|0.000421292|min-sum pipeline OK using branch-and-bound, integral=10029.728856|
|registry_populated|PASS|0.000185667|registered: branch-and-bound, brute-force, genetic, greedy, ip-kont, local-search, lp-rounding, nn, primal-dual, sa, shifting|
|serializer_roundtrip|PASS|0.000509584|serializer roundtrip OK|
|verifier_accepts_all|PASS|0.000174417|verifier accepts the NN kinetic solution|
