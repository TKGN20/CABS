# Verification of the static reproduction package

Verification date: 2026-10-21. These are **actual local Linux-in-WSL measurements**,
not the manuscript's estimated performance targets or measurements on its server.
Hardware: AMD Ryzen 7 8845H, WSL exposing six logical CPUs; GCC 11.4.0, C++17
Release build. Construction/GT use four threads; search is sequential.

| Check | Result |
| --- | --- |
| Release build and all three CTest suites | PASS |
| ASan/UBSan build and the same suites | PASS |
| Fresh source-only archive extraction, build and CTest | PASS |
| Fresh public 0.1M/query download and checksums | PASS |
| Existing public 1M corpus, exact size/header/SHA-256 | PASS |
| Full default 0.1M and 1M production pipelines | PASS |
| Disjoint query sets, complete records, independent metric arithmetic | PASS |

Both real-data runs use ell=24, delta_h=0.2, k=50, k_max=100, seed=20261007,
four calibration queries and **all 6,976 remaining queries x three repeats**.
Each constructs a fresh automatically calibrated index. Independent synthetic
production-path tests check C++ exact GT and returned scores against Python
sparse inner products; they are not performance measurements.

| Dataset | Recall@50 | QPS | IT (s) | IS incl. vectors (GB) | Structure only (GB) | K | B |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| SPLADE-0.1M | 0.995407 | 124.73 | 187.04 | 0.392970 | 0.290331 | 1 | 13976 |
| SPLADE-1M | 0.995717 | 12.04 | 751.13 | 3.461449 | 2.442889 | 8 | 51385 |

IS follows manuscript VI-A by including original CSR required for exact scoring.
The separate structure-only column follows the narrower excluding-CSR scope in
the preparation proposal. Both are capacity-based payload inventories, not
allocator-level peak RSS. IT includes calibration-target exact search and all
automatic configuration construction; evaluation GT is excluded.

Full precision, resolved hash counts, input/source/binary hashes, raw-record
hashes and arithmetic audits are in
[`verification/measured_defaults.json`](verification/measured_defaults.json).
Ranked IDs, GT and query timing records are also published losslessly compressed
under `results/measured/`; the evaluator reads these directly and checks the
artifact checksums. Original vectors and private execution paths/logs are not
bundled. Fresh reproduction never reads the published results as search inputs.
The 0.1M derived metrics were independently re-evaluated to
apply the final VI-A IS convention; original timing/search records were unchanged.

These measurements describe the stated local protocol; numerical reproduction
of manuscript claims is not established. Public base_small.csr has average
document nnz=127.29954. Exact input slices, query/calibration IDs, hardware and
metric boundaries must match for a paper comparison. No target metric is
hardcoded or substituted.

The author selected the MIT License and approved updating the original GitHub
repository with the new implementation and actual results.
Manuscript numerical agreement remains unverified and is not implied by code
publication or successful execution of these two commands.
