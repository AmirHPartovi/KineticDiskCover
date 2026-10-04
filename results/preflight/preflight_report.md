# Pre-flight Sanity Check Report

- **Summary:** 19 / 19 checks passed (0 failed)
- **Total check duration:** 0.0426779 s

| Check | Status | Duration (s) | Detail |
|---|---|---:|---|
|algorithm:branch-and-bound|PASS|0.000126208|cost=18173.818308|
|algorithm:brute-force|PASS|0.00308504|cost=18173.818308|
|algorithm:genetic|PASS|0.0145155|cost=18173.818308|
|algorithm:greedy|PASS|1.3958e-05|cost=22280.712426|
|algorithm:ip-kont|PASS|3.8292e-05|cost=26677.144374|
|algorithm:local-search|PASS|4.95e-05|cost=18173.818308|
|algorithm:lp-rounding|PASS|5.0417e-05|cost=26677.144374|
|algorithm:nn|PASS|4.166e-06|cost=26677.144374|
|algorithm:primal-dual|PASS|2.0667e-05|cost=33804.698710|
|algorithm:sa|PASS|0.00390567|cost=24702.914351|
|algorithm:shifting|PASS|0.00136908|cost=26677.144374|
|all_algorithms_on_dummy|PASS|0.01773|all algorithms produced feasible solutions|
|data_instances_directory|PASS|0.000134917|1 JSON instances found in /Users/amirpartovi/myKDC/data/instances|
|ilp_backend|PASS|0|KONT not provided; using MockILPSolver|
|minmax_pipeline|PASS|0.000234417|min-max pipeline OK, peak=11956.604428|
|minsum_pipeline|PASS|0.000223792|min-sum pipeline OK, integral=11881.728759|
|registry_populated|PASS|2.7375e-05|registered: branch-and-bound, brute-force, genetic, greedy, ip-kont, local-search, lp-rounding, nn, primal-dual, sa, shifting|
|serializer_roundtrip|PASS|0.0006125|serializer roundtrip OK|
|verifier_accepts_all|PASS|0.000536333|verifier accepts the NN kinetic solution|
