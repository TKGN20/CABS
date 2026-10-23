# Verification and measured source revisions

Review date: 2026-10-21. Test platform: AMD Ryzen 7 8845H, Linux in WSL,
six exposed logical CPUs, GCC 11.4, C++17 Release. Search is sequential.

| Check | Result |
| --- | --- |
| Release: semantic, production-path and reporting CTest suites | PASS |
| ASan/UBSan: the same three suites | PASS |
| Synthetic exact scores and GT checked independently in Python | PASS |
| Public 0.1M/1M and query input size/header/SHA-256 checks | PASS |
| Two completed full-query result directories independently re-evaluated | PASS |
| Comment-only cleanup: compiled Release binary SHA-256 unchanged | PASS |
| New full 0.1M run: all 6,976 queries x three repeats | PASS |
| Old/new full 0.1M GT, ranked outputs, split and non-timing query fields | Identical |
| Scoring diagnostic: 960,000 actual 1M corpus/query vector pairs | Exact scores equal |
| Paired complete-query diagnostic: 200 queries x 4 repeats, one 0.1M index | IDs, scores, counters and complete candidate states equal |

## Full-query records

The 0.1M full run was repeated on the reviewed scoring version; the 1M record
retains the earlier published version. Each uses
ell=24, delta_h=0.2, k=50, k_max=100, seed=20261007, four calibration queries
and all 6,976 remaining queries over three timing repeats. Both inputs and raw
measurement records remain available under `results/measured/`.

| Dataset | Source | Recall@50 | QPS | IT (s) | IS (GB) | K | B |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| SPLADE-0.1M | `80b25cf` | 0.995407 | 158.12 | 164.71 | 0.392970 | 1 | 13976 |
| SPLADE-1M | `ac17836` | 0.995717 | 12.04 | 751.13 | 3.461449 | 8 | 51385 |

IS includes required original CSR plus retained index payload. Structure-only
bytes are recorded separately. IT includes exact calibration targets; evaluation
GT is excluded. These are local hardware measurements, with actual source and
binary hashes, not measurements on the manuscript's server.

## Scoring revision

The production workspace stores query values as doubles and accumulates products
without a per-coordinate nonzero branch. Input weights are finite nonnegative
float32 values. Float32-to-double conversion is exact; the document-coordinate
accumulation order is retained, and adding zero does not alter the finite score.
No approximate dot product, fast-math or new retrieval parameter is introduced.
The extra query workspace belongs to query scratch and is excluded from IS.

On the paired 0.1M index, the realized configuration was K=16 and B=7772.
Across 800 timed queries per implementation, previous throughput was 118.0254
QPS and revised throughput was 133.1980 QPS: a measured 1.1286x speedup.
The raw [paired record](../results/engineering/scoring_20261021/) and
[reproduction instructions](../benchmarks/README.md) are public. This diagnostic
does not overwrite earlier full-query metrics or establish the same speedup
on other indexes, datasets or hardware.

The reviewed version's full 0.1M run measured Recall@50=0.995407, QPS=158.1189,
IT=164.7133 s and IS=0.392970 GB. It independently selected K=1 and B=13976.
All GT and retrieved-ID/score CSV bytes match the earlier full 0.1M run; all
20,928 query records have identical non-timing fields. The
[equivalence audit](../results/engineering/scoring_20261021/full_0.1m_equivalence.json)
records this check. The paired experiment, rather than the difference between
separate full runs, is used to isolate the scoring-kernel benefit.

Source comments were removed separately before this revision; recompilation at
that step produced the same executable SHA-256. The MIT license is retained.

## 2026-10-22 review follow-up

Five CTest suites now pass locally in Release and ASan/UBSan: semantic,
end_to_end, reporting_validation, mechanism_contract and calibration_audit.
The latter two deterministically exercise multiple clusters, positive-floor
fallback, spillover, minimum B, fresh final hashes and reconstruction of formal
production outputs. The same matrix has an actual successful
[GitHub Actions run](https://github.com/TKGN20/CABS/actions/runs/39766748200)
at diagnostic/test source 0fb6b48; CI is enabled rather than just templated.

The completed b327ec35 1M production source is independently matched by content
hashes, with all 20,928 timed records and full GT available under
results/engineering/review_1m_20261022. Git identity capture in that WSL run was
null because its worktree metadata contained a Windows absolute path. The raw
metadata is preserved and source_attestation.json supplies a complete independent
snapshot-content correspondence. Later changes add tests, diagnostics and docs;
the production include/src files remain unchanged.

The selected-final-index 1M diagnostic matches 16 ranked query results and 32
repeat/counter records against the complete task. The small all-K audit checks
ten candidates: proxy, full calibration and sampled full evaluation costs all
have their observed minimum at K=1. This sample therefore does not demonstrate
a wrong K selection, and its recalls are not matched. Theory/representation
boundaries are documented in PAPER_IMPLEMENTATION.md and COMPLEXITY.md.
