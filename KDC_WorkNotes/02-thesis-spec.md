# KDC Stage 02 — Thesis Traceability Specification

Date: 2026-11-19
Scope: Read-only extraction from
`~/Desktop/Texts/MScThesisPersian/main.pdf`

## Extraction and page mapping

- PDF page count: 257.
- Chapter 5 starts at PDF page 127, printed page 111.
- Chapter 6 starts at PDF page 176, printed page 160.
- Chapter 7 starts at PDF page 188, printed page 172.
- Chapter 8 starts at PDF page 208, printed page 192.
- Observed offset: PDF index = printed page + 16 for the extracted chapter
  ranges.
- Ghostscript text extraction preserves Latin symbols, equation bodies, tables,
  and section numbers well, but Persian glyph direction/order is unreliable.
  Where prose or an exact equation is not recoverable, this report says
  "not specified in the extracted source" rather than using external knowledge.
- Labels:
  - **A**: mathematical requirement or definition.
  - **B**: engineering recommendation or option.
  - **C**: validity condition or claim limitation.
  - **D**: planned or not executed.

## Chapter 5 — MinSum main algorithm

| Label | Requirement | Reference | PDF / printed page | Mathematical form | Validity / arithmetic assurance | Necessary test |
|---|---|---|---|---|---|---|
| A | Model A is the primary kinetic MinSum model | Chapter 5 opening; Chapter 4.3.1 (Model A) | PDF 127-128 / printed 111-112 | Fixed centers \(c_i\), moving points \(p_j(t)\), single coverage, \(J=\int_0^T\pi\sum_i r_i(t)^2dt\) | Full [0,T] coverage; no extra handover, rate, radius, or station-capacity constraint may be silently introduced | Construct one moving instance and prove coverage and integral analytically, including assignment changes |
| A | Objective and exact optimum | Equation (1.5), Chapter 5 opening | PDF 127 / printed 111 | \(J(\sigma,r)=\int_0^T\pi\sum_i r_i(t)^2dt=\int_0^Tf(t)dt\), with \(f(t)=g(D(t))=\min_\sigma F_\sigma(t)\) | Integral and sum must be continuous/analytic; sampled-only evaluation is not optimality | Analytic small-instance test with exact integral and exact optimum |
| A | Input trajectory contract | Section 5.1.1, Table 5.1 | PDF 128-129 / printed 112-113 | \(n\ge1\), \(j=1,\ldots,n\), \(p_j:[0,T]\to R^2\), piecewise linear; \(T>0\); fixed \(m\) stations | Trajectories and breakpoints must be finite and valid; endpoints and direction at breakpoints must be defined | Invalid-dimension, zero-T, and breakpoint-direction tests |
| B | Solver inputs and controls | Section 5.1.1, Table 5.1 | PDF 128-130 / printed 112-114 | Three Oracles; \(\epsilon_{abs},\epsilon_{rel}\ge0\); \(\tau_{max}\), \(B_{max}\), \(Nora\), \(\Delta_{min}\), and counting limits | Values must be finite and nonnegative where specified; stop reasons must be separate | Configuration validation tests for negative/NaN/Inf limits |
| A | Each finite UB must come from a complete feasible program | Section 5.1.2, Table 5.1 | PDF 130-131 / printed 114-115 | \(UB=\) cost of complete verified assignment over all [0,T]; \(UB=\infty\) or incumbent=None when no complete incumbent exists | Local interval incumbents do not establish a global UB; continuous verification is required | Test partial solutions are not serialized as finite UB |
| A | LB and UB final record semantics | Section 5.1.2, Table 5.1 | PDF 130-131 / printed 114-115 | Final \(LB\le OPT_A\le UB\); \(UB=\) best exact feasible value; \(LB=\) best valid certified/interval lower bound | A heuristic primal cost is never an LB; unverified sampled/trapezoidal values are not LB | Test heuristic cost, primal objective, and LB remain separate fields |
| C | CERTIFIED status issue condition | Section 5.1.2, Table 5.2 | PDF 130-132 / printed 114-116 | CERTIFIED only for an independently valid certificate and an LB whose validity survived the current partition | Exact solver status or small gap alone is insufficient; exact-arithmetic/interval arithmetic is not implied by a backend `OPTIMAL` label | Negative test: backend OPTIMAL + no exact certificate must not issue CERTIFIED |
| C | NUMERICALLY_BOUNDED status | Section 5.1.2, Table 5.2 | PDF 131-132 / printed 115-116 | Valid nonzero finite LB/UB may exist but proof does not reach exact/interval assurance | Arithmetic rounding and interval inclusion must be documented and conservative | Test rounding monotonicity and certificate invalidation |
| C | HEURISTIC status | Section 5.1.2, Table 5.2 | PDF 131-132 / printed 115-116 | Complete incumbent exists and a feasible UB exists, but no certified LB gap | The incumbent must be continuously verified as feasible | Test verified incumbent with LB=0 yields HEURISTIC, not CERTIFIED |
| C | SAMPLED status | Section 5.1.2, Table 5.2 | PDF 131-132 / printed 115-116 | Evidence comes only from finite time samples | No statement for uncovered open intervals | Test uncovered interval remains SAMPLED/INCOMPLETE |
| C | INCOMPLETE and leaf invalidation | Section 5.1.2 and 5.1.2 table | PDF 131-133 / printed 115-117 | If one of the two leaf certificates is missing/invalid: \(LB=0\), \(UB=\infty\) for that current leaf contribution | Historical certificates remain independent history only after revalidation; never retain stale valid labels | Test oracle failure replaces current leaf bounds and does not keep stale values |
| C | Stopping reasons and provenance | Section 5.1.2, Table 5.1 | PDF 129-132 / printed 113-116 | `ORACLE_CAP`, `INTERVAL_CAP`, `TIMEOUT`, `GAP_REL`, `GAP_ABS`, `MIN_INTERVAL`, `VERIFY_FAIL`, `ORACLE_FAIL`, `INCONSISTENT` | Provenance must identify input, method, arithmetic state, source run/version, cache state, and solve/verify times | Test all stop reasons serialize independently and provenance is never omitted |
| A | Weighted static problem | Section 5.2.1, Eq. (2.5) | PDF 115/printed? 115? Wait: Chapter 5 extraction begins printed111; weighted section is PDF 147? / printed131? | \(W=[w_{ij}]\ge0\), \(g(W)=\pi\min_{\sigma}\sum_i\max_{j:\sigma(j)=i}w_{ij}\), \(\max\emptyset=0\) | W is independent of geometry and must not be converted into an artificial Instance/time object | Unit test D(t), D-(I), D+(I) remain distinct weighted inputs |
| A | D(t), D-(I), D+(I) | Section 5.2.1, Eq. (3.5) | PDF 147-148 / printed 131-132 | \(D(t)=[d_{ij}(t)]\), \(f(t)=g(D(t))\), \(g^-(I)=g(D^-(I))\), \(g^+(I)=g(D^+(I))\) | D-(I), D+(I) are endpoint-bound matrices, not distance matrices of a single geometric configuration | Matrix tests verify lower/upper componentwise constraints for all t in I |
| A | Componentwise interval bounds | Section 5.2.1 / interval discussion | PDF 147-148 / printed 131-132 | For every \(i,j,t\in I\): \(d^-_{ij}(I)\le d_{ij}(t)\le d^+_{ij}(I)\) | Bounds must use valid interval endpoint/derivative calculations; no midpoint sampling may substitute | Cross-check exact roots/endpoints against every open interval and support breakpoint |
| A | d^- and d^+ definitions | Section 5.2.1 / 5.4 | PDF 147-148 and 156-? / printed131-132 and 140-? | \(d^-_{ij}(I)\) and \(d^+_{ij}(I)\) are, respectively, lower and upper distance bounds on I; exact analytical forms not unambiguous in extracted text | Lower/upper bounds must remain conservative under floating-point rounding | Test near tangent, flat, monotone, and endpoint cases |
| B | Lipschitz bound | Chapter 5 lower/upper bound methods, table 5.3 | PDF 156-? / printed140-? | An interval Lipschitz constant \(\Lambda_I\) is used for an upper/lower bound; exact formula is not recoverable from the extracted source | If \(\Lambda_I\) is used, it must be independently verified and rounded outward | Test each bound with exact quadratic extrema |
| B | Cone function \(\Phi\) | Chapter 5 interval construction/bound section | PDF page exact not recoverable / printed exact not recoverable | The source names a cone function \(\Phi\); exact definition/equation is not specified in the extracted source | It must not be reconstructed from general knowledge | No implementation test until the source equation is recoverable |
| A | MILP assignment formulation | Section 5.2.1, Eq. (4.5) | PDF 148-149 / printed 132-133 | \(\min\pi\sum_iq_i\), \(s.t.\sum_ix_{ij}=1\), \(q_i\ge w_{ij}x_{ij}\), \(x_{ij}\in\{0,1\}\), \(q_i\ge0\) | For W=D(t), q_i may represent max assigned weight; candidate-disk representation is not the assignment formulation | Test assignment constraints and optimal objective against exhaustive enumeration |
| A | Candidate-disk weight problem | Section 5.2.1, Eq. (2.5) | PDF 148 / printed132 | \(g(W)=\pi\min_\sigma\sum_i\max_{j:\sigma(j)=i}w_{ij}\) | This is a weighted static problem; W need not be geometric D(t) | Test D-(I) and D+(I) solve the weighted problem directly |
| A | LP relaxation | Section 5.2.2, Eq. (5.5)-(6.5) | PDF 149-150 / printed133-134 | \(LP^c(W)=\min\sum_iq_i\), \(x_{ij}\in[0,1]\), \(LP(W)=\pi LP^c(W)\), \(LP(W)\le g(W)\) | LP objective is valid only as an LB and only with valid dual-feasibility/certificate where claimed | Test primal objective alone cannot be emitted as LB; dual check required |
| C | LP may be strict | Section 5.2.2 | PDF 150 / printed134 | \(LP(W)<g(W)\) may occur | LP relaxation must not be reported as exact optimum | Test a known strict-gap matrix |
| A | Oracle.Exact | Section 5.2.3 | PDF 151-152 / printed135-136 | Returns exact \(g(W)\) and assignment \(\sigma^*\); exact means equality to g(W), not merely solver `OPTIMAL` | Require exact proof or independently checked exact arithmetic; gap zero and both-bound proof | Test brute-force parity on small W |
| A | Oracle.Heuristic | Section 5.2.3 | PDF 151-152 / printed135-136 | Returns feasible \(\hat\sigma\) and \(\hat g=F_{\hat\sigma}(W)\ge g(W)\); never an LB | The result is only UB candidate | Test output is feasible and is never inserted as LB |
| A | Oracle.Relaxation | Section 5.2.3 | PDF 152 / printed136 | Returns \(\underline g\le g(W)\) together with a valid relaxation certificate | LP primal alone is not enough; independently checked dual feasibility/rounding is required | Test infeasible/invalid dual cannot produce LB |
| A | Upper-envelope integral | Section 5.3 and Eq. (1.5) | PDF 152-154 / printed136-138 | A complete program is formed from interval UB pieces; interval values are summed as integrals | Endpoints and all open subintervals must be covered; no overlap/gap | Partition union test and exact integral test |
| A | Initial partition P0 | Section 5.4.1, Eq. \(B=\{0=t_0<...<t_K=T\}\) | PDF 155 / printed139 | \(P_0=\{I_k=[t_{k-1},t_k]\}\) from trajectory breakpoints | Partition must contain [0,T] and be ordered with no overlap | Test boundary union and disjoint open intervals |
| A | Interval fields | Table 5.3 | PDF 156 / printed140 | start, end, midpoint, delta, depth, \(LB_I,UB_I\), lb_method, ub_method, \(\sigma_I\), pieces, \(\Lambda_I\), provenance, status | LB/UB, method, provenance, and arithmetic mode are mandatory per leaf | Schema test rejects missing provenance/method |
| A | P and live partition | Section 5.4.2 / partition data structure | PDF 156-157 / printed140-141 | P is the set of live leaves covering [0,T]; leaves are disjoint and cover the domain | Adjacent leaves may share endpoints but open intervals must be disjoint | Test P invariant after split/merge/rollback |
| B | Priority | Section 5.4.2 | PDF 156-157 / printed140-141 | Priority selects intervals for splitting/refinement; exact ordering parameter is not specified in source | Priority must carry reproducible provenance and tie-breaking | Test deterministic priority with seeded runs |
| B | Q queue | Section 5.4.2 | PDF 156-157 / printed140-141 | Q holds candidate leaves/events for processing; exact implementation is not specified | Stale entries must be validated against current P and freeze/rollback state | Test stale Q entries cannot change bounds |
| A | Splitting | Sections 5.4.1-5.4.2 | PDF 155-157 / printed139-141 | Split \(I=[a,b]\) into left/right children \(I_L=[a,c]\), \(I_R=[c,b]\) | Child leaves must replace I atomically and cover I exactly | Test sum of child bounds and exact endpoint equality |
| C | Rollback | Partition/history discussion, Chapter 5 | PDF 157-? / printed141-? | Rollback restores a frozen/invalid previous consistent partition state | Stale leaf UB/LB cannot remain valid after oracle/certificate invalidation | Test rollback clears invalid leaf contribution |
| C | Aggregation and history | Partition/history discussion, Chapter 5 | PDF 157-? / printed141-? | Aggregate \(LB=\sum_I LB_I\), \(UB=\sum_I UB_I\) only across live leaves; history is separate | Aggregate must not include historical invalid contributions; leaf validity must be revalidated | Test non-additive/historical/cross-partition leaf rejection |
| C | Status table | Table 5.2 | PDF 131-132 / printed115-116 | CERTIFIED, NUMERICALLY_BOUNDED, HEURISTIC, SAMPLED, INCOMPLETE | Status issuance preconditions in the table must be enforced | Parameterized status issuance tests |
| A | Stopping conditions | Section 5.1.1/5.8; Table 5.1 | PDF 128-130 and later / printed112-114 and later | ORACLE_CAP, INTERVAL_CAP, TIMEOUT, GAP_REL, GAP_ABS, MIN_INTERVAL, VERIFY_FAIL, ORACLE_FAIL, INCONSISTENT | Any termination with an incomplete program cannot emit finite UB | Test every stop reason against UB/ LB/status fields |
| C | Verifier | Section 5.8.5 / continuous verifier | PDF 167-168 / printed151-152 | Verify full partition coverage, radius/support assignments, piecewise quadratic coefficients, and analytic integral | Finite-sample checks are EMPIRICAL/SAMPLED, not continuous certification | Test one-sample crossing is caught continuously |
| C | Certified continuous verification | Section 5.8.5/Chapter 6.5 | PDF 168-169 / printed152-153 | CERTIFIED_CONTINUOUS means coverage/support/cost/integral checked over all roots/open pieces | It does not prove global objective optimality | Test status remains FEASIBLE/HEURISTIC unless independent global LB exists |

## Chapter 6 — Static MILP and implementation methods

| Label | Requirement | Reference | PDF / printed page | Mathematical form | Validity / arithmetic assurance | Necessary test |
|---|---|---|---|---|---|---|
| A | Assignment MILP | Section 6.1.1 / Eq. (4.5) same static problem | PDF 176-177 / printed160-161 | Same \(m\times n\) assignment formulation as Chapter 5 | Binary assignment variables and continuous q variables must be preserved separately | Reproduce Chapter 5 matrix and compare exhaustive optimum |
| A | Candidate-disk representation | Sections 6.1.2 and 6.2 | PDF 177-179 / printed161-163 | Each candidate disk is represented by station i and support point j with weight/radius | It is another representation of the weighted set-cover problem, not a second geometric model | Test candidate disk generation equals assignment coverage exactly |
| C | Assignment versus candidate-disk distinction | Section 6.1.2 | PDF 177-179 / printed161-163 | Assignment formulation has x/q variables; candidate-disk representation has coverage rows/disks | Do not convert D-(I)/D+(I) into D(t) or artificial Instance/time objects | Interface test uses W directly and never constructs Instance/time |
| A | IP-KONT role | Section 6.2.1 / implementation method table | PDF 176-177 / printed160-161 | Native exact MIP solver produces incumbent and global best bound | Exact only if solver status, feasibility, and independent bound certificate satisfy preconditions | Known small instance; infeasible/time-limited/error cases |
| C | IP-KONT validity limitation | Chapter 6 status table and section 6.5 | PDF 176-168? / printed160-152? | `OPTIMAL` requires a valid proof from the native backend; timeout is TIME_LIMIT; licensing/runtime failure is NUMERIC/ERROR | LP/MIP primal objective alone is not a certificate | Mock backend test for OPTIMAL/FEASIBLE/TIME_LIMIT/ERROR |
| A | Branch-and-Bound role | Sections 6.2.1 and 6.3.1 | PDF 176-179 / printed160-163 | Built-in exact static search enumerates assignments/disks with frontier bounds | Exact only after exhaustive frontier; timeout or incumbent is FEASIBLE/TIME_LIMIT | Small brute-force parity and timeout incumbent test |
| C | Branch-and-Bound validity limitation | Section 6.2.1, Eq. and status table | PDF 176-179 / printed160-163 | Frontier bound must remain valid after relaxation/preprocessing | Stale/global lower bounds cannot survive a changed matrix W | Mutate W and verify cache/leaf invalidation |
| A | Brute Force role | Section 6.3.1 | PDF 179-180 / printed163-164 | Enumerates every feasible assignment for small m,n | Exact reference only if enumeration completes | Random exhaustive m,n small test |
| C | Brute Force limitation | Section 6.3.1 | PDF 179-180 / printed163-164 | Enumeration limit/cap/time may make result only FEASIBLE; no completion means no exact result | Output exact only on full enumeration | Test `Nora`/assignment-count cap prevents OPTIMAL |
| B | Nearest-neighbor and greedy/static heuristics | Section 6.2.1 | PDF 176-177 / printed160-161 | NN assigns by nearest station; greedy adds improving coverage/disks | Heuristic incumbents are UB candidates; no LB inference | Feasibility and UB tests; explicitly reject heuristic LB |
| B | LP/relaxation method | Section 6.1.2 and 6.5 | PDF 177-179 and 167-168 / printed161-163 and151-152 | LP(W) as relaxation; valid only if independently checked lower-bound certificate exists | LP primal objective cannot itself be called LB | Dual-feasibility test and strict-gap matrix test |
| B | Preprocessing/reduction rules | Sections 6.3.1-6.3.2 | PDF 179-164? / printed163-164? | Station row deletion, dominated column/radius merging, fixed assignment, and objective-bound reductions are described | Every rule must be proved to preserve g(W) or to preserve a revalidated incumbent/bound | Ablation tests: enable/disable one rule and compare exact result |
| B | Warm start | Section 6.4.1 | PDF 165-166 / printed149-150? | Reuse previous assignment vector and q values for changed W | Warm start may only seed a candidate; it cannot certify bounds | Test changed W invalidates model-specific cache/bounds |
| B | Cache | Section 6.4.1 | PDF 165-166 / printed149-150? | Cache canonical matrix bytes, parameters, model version, solution, status, provenance, and costs | Cache hit requires exact canonical matrix/model/config match | Test byte-order, NaN/Inf rejection, model-version mismatch |
| C | Method validity limitation table | Table 6.2 / Table 6.3 | PDF 176-164 / printed160-164 | Static statuses and reduction rules have separate valid-claim preconditions | A status name alone is insufficient; exact, heuristic, numeric, and incomplete must remain separate | Parameterized status/precondition tests |
| C | Numerical precision policy | Section 6.5 | PDF 167-168 / printed151-152 | Separate \(\epsilon_{int},\epsilon_{feas},\tau_{root},\epsilon_{solve},\epsilon_{gap},\epsilon_{abs},\epsilon_{rel}\) roles | No single sample or approximate comparison may establish certification | Test tolerances at zero, negative, tangent, and strict-gap boundaries |

## Chapter 7 — Dataset and experimental protocol

| Label | Requirement | Reference | PDF / printed page | Mathematical form | Validity / arithmetic assurance | Necessary test |
|---|---|---|---|---|---|---|
| B | Dataset families F1-F7 | Section 7.2.1 and Table 7.5 | PDF 188-? / printed172-? | Industrial/artificial families F1-F7; S1-S3 scale networks; X small exact set; EC manual edge cases | Dataset membership and family labels must be immutable and manifest-checked | Manifest family membership test |
| B | 1D family | Sections 7.2.1 and 7.2.4 | PDF 188-190 / printed172-174 | 1D is conditional on final validation and not part of ordinary 1D approval; not specified in source | Avoid assumptions about model extension to 1D | Conditional test is run only if 1D instance is approved |
| A | Manifest | Section 7.2.4 | PDF 190-191 / printed174-175 | Every instance has stable name, dimensions n,m,s,T,K, seed, source/version, model version, canonical JSON SHA-256 | UTF-8 JSON, sorted keys, LF line ending, no NaN/Inf, round-trip serialization | Manifest checksum and round-trip tests |
| A | Seed/PRNG | Section 7.2.4 | PDF 190-191 / printed174-175 | Name `<family>-<cell>-r<replicate>`; SplitMix64 PRNG and explicit 64-bit seed equations | Deterministic independent streams; no mapping PRNG output to nominal variables is specified | Same seed/config produces same canonical dataset bytes |
| B | Baselines | Section 7.3 | PDF 191-? / printed175-? | B-nn, B-grid, B-static-hold, B-minmax, B-ours, and exact MILP/Brute Force references | Baseline must use the same instance/objective and be labeled separately | Baseline parity and objective-field tests |
| B | E0 dependent experiment | Section 7.1/Table 7.1 | PDF 188-189 / printed172-173 | E0 is the prerequisite dataset/construction gate | E0 must pass before E1-E9 dependent execution | Gate test rejects downstream run with missing E0 |
| B | E1-E9 experiments | Section 7.1, Table 7.1 | PDF 188-189 / printed172-173 | H1-H10 map to E1-E9 and depend on M? exact; statuses must be recorded | Exact experiment IDs and hypotheses need to match manifest | Manifest/experiment ID consistency test |
| B | Budgets | Section 7.1/Table 7.1 | PDF 188-189 / printed172-173 | \(T_\epsilon\), \(\tau_{max}\), \(T_{cap}=\min(T_\epsilon,\tau_{max})\), \(Nora\), \(B_{max}\), and \(B_i\) | Budget units, fairness, and timeout behavior must be identical for comparable runs | Side-by-side time-limit and oracle-cap test |
| B | Repeats and fair comparison | Section 7.1/Table 7.1 | PDF 188-189 / printed172-173 | Fixed 70 samples in F1-F7; repeats and cells as listed in Table 7.1/7.5 | The same instance, budget, model, and PRNG policy must be used; different methods are not mixed silently | Paired-run manifest equality test |
| C | Measurement criteria | Section 7.1 and Table 7.2 | PDF 189-190 / printed173-174 | γ, \(\gamma_{final}-\gamma_{15s}\), node count, oracle count, cache/hits, \(t_{oracle}\), \(t_{loop}\), \(t_{verify}\), \(t_{solve}\) | Report success/failure/timeout separately; no average can hide zero or invalid values | Aggregate test verifies denominators and null values |
| C | Holm correction/statistical claim | Table 7.2 | PDF 189-190 / printed173-174 | Holm correction applies to selected hypothesis tests H2-H6/H3/H5/H6 where specified | No claim without preregistered hypotheses and independent replicates | Statistical test of multiplicity and sample pairing |
| C | Fair-comparison requirements | Sections 7.3.6 and 7.6 | PDF 192-193 / printed176-177 | Baseline, implementation, and exact solver must use the same objective, instance, and time/iteration policy | LB/UB and status must be comparable; exact run is not a heuristic baseline | Add matrix comparing base fields and rejection cases |

## Chapter 8 — Executed vs planned

| Label | Requirement | Reference | PDF / printed page | Mathematical form | Validity / arithmetic assurance | Necessary test |
|---|---|---|---|---|---|---|
| D | Chapter 7 protocol was designed, not fully executed | Chapter 8.1 and Table 8.1 | PDF 208-210 / printed192-194 | E1-E9 and 1D/2D protocol described; not all are executed | A protocol text or manifest does not imply execution | Check manifest contains actual run evidence and run IDs |
| D | General 20-smoke execution | Table 8.1 and Section 8.1 | PDF 208-210 / printed192-194 | 20 smoke × 11 × 3 × 1 = 660 planned slots | 487 completed, 51 failed, 39 timed out, 23 infeasible, 60 invalid as recorded in Chapter 8 Table 8.2; exact counts must be read from artifact, not general knowledge | Manifest/run audit for slot counts |
| D | Full 70-sample families | Table 8.1 | PDF 208-210 / printed192-194 | F1-F7 70 samples, S1-S3 150, X 90, EC 13; planned/partially reported in Chapter 8 | Only values present in chapter's artifact ledger are executed | Verify source run IDs and artifact paths |
| D | E1-E9 verification | Chapter 8.1 and final result | PDF 208-224 / printed192-208 | Table 8.1 marks some hypotheses not executed or only designed | Do not call E1-E9 successful based on smoke alone | Hypothesis status test against evidence ledger |
| D | 1D experiment | Table 8.1 / Chapter 8.10 | PDF 208-224 / printed192-208 | 1D family is listed in smoke protocol but source is not fully clear/approved | Not implemented/executed by a Chapter 8 conclusion | No test until source and generated instance policy are approved |
| C | Smoke is not a global proof | Section 8.1 and 8.3 | PDF 210-194 / printed194-? | 20-smoke statuses are empirical and may include FAIL/PARTIAL/TIME_LIMIT | `OPTIMAL` or CONTINUOUS must not be generalized to all instances | Test publication labels include smoke-only scope |
| D | MinSum full algorithm proof | Chapter 8 discussion/future work | PDF 224-225 / printed208-209 | The thesis reports implementation of a heuristic partition framework, but not a complete exact global MinSum result | Chapter 8 does not certify the thesis algorithm as fully implemented or globally proven | Searchable evidence test requires source run and independent LB/UB |
| D | KDS and handover framework | Chapter 8.3.2/future work | PDF 224-225 / printed208-209 | KDS/handover ideas are outside or partially beyond the currently executed core | Do not add extra handover/rate constraints to Model A | Model A test has no implicit handover cost/rate constraint |
| D | Analytical/scale/complexity experiments | Chapter 8.10 | PDF 224-225 / printed208-209 | Future exact small-instance and scale experiments are described | Not reported as executed | Future-work label required |
| C | Chapter 8 smoke evidence limitations | Chapter 8.1-8.3 | PDF 210-224 / printed194-208 | Some current run outputs mix statuses, missing LB fields, or heuristic gaps; they are not sufficient for exact/interval claims | Null LB, heuristic gap, and certified gap must remain separate | Test schema rejects heuristic gap as certified gap |
| C | Implemented capability boundary | Chapter 8.3.2 and opening | PDF 224-225 / printed208-209 | Executed artifacts cover static, kinetic, verifier, and some smoke experiments; not every Chapter 5/6/7/8 target | Capability must be declared from evidence, not from code presence alone | Evidence ledger test links capability to artifact/run/source |

## Audit findings and unresolved references

1. The exact extraction text is insufficient to recover every Persian section
   title and every equation number without ambiguity. Those items are marked
   with "exact ... not recoverable" or a bounded section/page reference.
2. No general knowledge was used to fill:
   - the exact definition of the cone function \(\Phi\);
   - the exact Lipschitz formula;
   - exact d^- and d^+ formulas;
   - exact priority/P and Q implementation details;
   - exact rollback and history formulas.
3. Chapter 5 explicitly distinguishes W from D(t), D-(I), and D+(I).
   D-(I)/D+(I) are weighted subproblem inputs and must never be mapped into
   artificial geometric Instance/time objects.
4. Oracle.Heuristic is UB-only. Oracle.Relaxation is LB-only only if an
   independently valid lower-bound certificate exists. Oracle.Exact is exact
   only with exact proof.
5. Chapter 5 status names are not enough: the preconditions in Table 5.1/Table
   5.2 and the Chapter 6/5.8 verifier requirements must be enforced.
6. Chapter 8 evidence is mixed and incomplete; smoke execution, continuous
   feasibility, heuristic result, and global exact certification must remain
   separate claims.

## Stage acceptance

- PASS: PDF-only source extraction performed.
- PASS: Chapter 5-8 traceability matrix created.
- PASS: A/B/C/D labels applied.
- PASS: PDF and printed-page references are separated.
- PASS: Unknown source parameters are explicitly marked as not specified.
- PASS: No repository code, test, config, or documentation file was modified.
