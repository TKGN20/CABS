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
| Scoring diagnostic: 960,000 actual 1M corpus/query vector pairs | Exact scores equal |
| Paired complete-query diagnostic: 200 queries x 4 repeats, one 0.1M index | IDs, scores, counters and complete candidate states equal |

## Full-query records

The original full runs were published at source version `ac17836`. Each uses
ell=24, delta_h=0.2, k=50, k_max=100, seed=20261007, four calibration queries
and all 6,976 remaining queries over three timing repeats. Both inputs and raw
measurement records remain available under `results/measured/`.

| Dataset | Recall@50 | QPS | IT (s) | IS (GB) | K | B |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| SPLADE-0.1M | 0.995407 | 124.73 | 187.04 | 0.392970 | 1 | 13976 |
| SPLADE-1M | 0.995717 | 12.04 | 751.13 | 3.461449 | 8 | 51385 |

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

Source comments were removed separately before this revision; recompilation at
that step produced the same executable SHA-256. The MIT license is retained.
