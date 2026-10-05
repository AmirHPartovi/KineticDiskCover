# Pre-flight Sanity Check Report

- **Summary:** 19 / 19 checks passed (0 failed)
- **Total check duration:** 0.0467688 s

| Check | Status | Duration (s) | Detail |
|---|---|---:|---|
|algorithm:branch-and-bound|PASS|9.075e-05|cost=18173.818308|
|algorithm:brute-force|PASS|0.00327783|cost=18173.818308|
|algorithm:genetic|PASS|0.0145812|cost=18173.818308|
|algorithm:greedy|PASS|3.8e-05|cost=22280.712426|
|algorithm:ip-kont|PASS|0|skipped: optional native KONT/COPT backend is unavailable|
|algorithm:local-search|PASS|5.1875e-05|cost=18173.818308|
|algorithm:lp-rounding|PASS|7.1416e-05|cost=18173.818308|
|algorithm:nn|PASS|9.625e-06|cost=26677.144374|
|algorithm:primal-dual|PASS|2.225e-05|cost=33804.698710|
|algorithm:sa|PASS|0.00399933|cost=24702.914351|
|algorithm:shifting|PASS|0.00381046|cost=26677.144374|
|all_algorithms_on_dummy|PASS|0.019921|all algorithms produced feasible solutions|
|data_instances_directory|PASS|0.000115959|1 JSON instances found in data/instances|
|ilp_backend|PASS|0|using built-in-branch-and-bound-fallback|
|minmax_pipeline|PASS|8.3833e-05|min-max pipeline OK using branch-and-bound, peak=10072.263854|
|minsum_pipeline|PASS|0.000149291|min-sum pipeline OK using branch-and-bound, integral=10029.728856|
|registry_populated|PASS|3.3542e-05|registered: branch-and-bound, brute-force, genetic, greedy, ip-kont, local-search, lp-rounding, nn, primal-dual, sa, shifting|
|serializer_roundtrip|PASS|0.000422125|serializer roundtrip OK|
|verifier_accepts_all|PASS|9.0333e-05|verifier accepts the NN kinetic solution|
