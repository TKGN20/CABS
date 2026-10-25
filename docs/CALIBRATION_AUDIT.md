# Diagnosing K and B without redefining the method

```bash
python3 scripts/audit_calibration.py splade-0.1m --offline --eval-queries 8 --out results/calibration-audit-small
```

The audit is separate from `scripts/reproduce.py`. Run it while other benchmarks
are idle. It uses the 2026-10-22 public defaults and checks the task configuration
before running. It evaluates all candidate K values with fixed realized sketches,
the same partition seeds and temporary hash domains as production calibration.
It does not cap K/m/B, replace K selection or reuse published query answers.

For each temporary index, it measures the actual proxy objective, calibrates B,
then runs complete searches on the four calibration queries and a disjoint seeded
evaluation subset. Each is warmed once and timed twice. Profile calls occur
after those measurements and are checked against uninstrumented ranked results
and counters. Profiling overhead affects its own timings; they are not QPS.

| File | Meaning |
| --- | --- |
| `candidates_full_cost.csv` | Per-K proxy cost, B, local m range and full-search means/counters/recall |
| `full_queries.csv` | Every uninstrumented query invocation and its split/repeat/counters |
| `pool_targets.csv` | Each calibration target's positive-overlap/touch status and best prefix rank; zero rank means never touched |
| `profile.csv` | Per-query encoding/routing/hash/lookup/scan/pool/verification times; dot-only is nested, not an additional stage |
| `observation.json` | Recomputed means, B-driver rows and which K minimizes each observed cost |
| `manifest.json`, `complete.json` | Protocol, source/input/binary hashes, environment and file hashes |

All searches here use temporary calibration hashes, not fresh final hashes.
Candidate recalls need not match. The lowest observed full-search time therefore
is not a fair matched-recall winner or a new default result. A discrepancy between
proxy and full-search minima diagnoses the model's scope; it does not establish
that production omitted a paper step. Revising the objective requires a separate
method/configuration proposal and new full measurements.

For profiling an already completed default measurement, use:

```bash
python3 scripts/audit_calibration.py splade-1m --offline --eval-queries 8 --selected-from results/COMPLETED_1M_RUN --out results/selected-1m-profile
```

This mode checks the original production source and input hashes and protocol,
uses the recorded automatically selected K, recalibrates B and requires it to
match, then builds the final seed domain and releases the cache. It verifies
every sampled ranked ID/score and semantic counter against the completed run.
It is a fixed-realization diagnostic, not a new K selection policy. It does not
measure complete IT or full-workload QPS, and cannot compare cost minima across K.

## Routing certificates and calibration pilot

Add `--routing-diagnostics` to a selected-index replay to save `routing_bounds.csv`.
Each row records centroid angle, covering radius, angular factor, norm cap,
cluster/suffix upper bounds, the current threshold and a stopping flag. For the
first four selected queries, every cluster's actual maximum original-vector dot
product is enumerated and checked against its bound. These scans and traces occur
outside the uninstrumented timings. Replay results and semantic counters must
match the completed record. A source-hash mismatch is rejected, including byte
differences caused by line endings; historical bytes must be restored explicitly.

```bash
python3 scripts/calibration_pilot.py --offline --common-queries 64 --out results/pilot-local
```

This experimental SPLADE-0.1M pilot builds all documents four times, with
`(calibration_queries, k_max)` equal to `(4,100), (8,50), (4,50), (8,100)` in that
order. Every case automatically recomputes K, local m and B. All share 64 query IDs
disjoint from the eight-query calibration pool. Extra evaluation queries needed
to align the split are saved but excluded from `common_metrics.json`; common
warm-up/equivalence prefixes are also aligned. There is one build per case and
three timing repeats. It neither changes public defaults nor matches recall.

Published raw evidence and the predeclared plan are in
[`calibration_pilot_20261022`](../results/engineering/calibration_pilot_20261022).
The [approval report](APPROVAL_REVIEW_ZH.md) links routing and frozen-budget evidence.
Recompute the analyses without vector datasets:

```bash
python3 scripts/approval_analysis.py routing results/engineering/routing_bounds_1m_20261022
python3 scripts/approval_analysis.py frozen-b results/engineering/selected_profile_1m_20261022 --complete-record results/engineering/review_1m_20261022
python3 scripts/verify_artifacts.py --source-check
```

The frozen-budget command reuses recorded prefix ranks and ground-truth ranks.
It does not rebuild hashes or infer QPS from B. Full-history source checks retain
measured byte hashes and canonical Git hashes separately, including explicitly
listed CRLF/LF variants. Existing manifests are not retroactively rewritten.
