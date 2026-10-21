# CABS: Cluster-Adaptive Binary Sketching for Sparse AMIPS

CABS combines IDF-aware SOS encoding, spherical partitioning with boundary
spillover, and cluster-adaptive MinHash candidate generation. Returned candidates
are scored and ranked by their original sparse inner products.

This version provides the complete static CABS implementation and two default
reproduction tasks: **SPLADE-0.1M** and **SPLADE-1M**, using `ell=24`,
`delta_h=0.2`, and top-50 search. It replaces the former fixed-`K`/`n_probe`/
global-`m` implementation. Internal `K`, per-cluster `m_i`, and global candidate
pool capacity `B` are calibrated from the data; they are not manually fixed.

## Compilation

Use Linux or Linux in WSL, a C++17 compiler (GCC 9+), OpenMP, CMake 3.16+,
Python 3.8+, and GNU `time`. Python scripts use only the standard library.
The loader uses POSIX memory mapping; native MSVC is currently unsupported.
No AVX-512 instruction set is required.

```bash
# Ubuntu/Debian dependencies
sudo apt-get install g++ cmake make python3 time

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure
```

`make` and `make test` are aliases for these CMake operations. `make sanitize`
builds and tests separately with AddressSanitizer and UndefinedBehaviorSanitizer.
The executable is `build/cabs`.

## Dataset

We use the public MS MARCO SPLADE CSR files distributed for the
[BigANN sparse track](https://github.com/harsha-simhadri/big-ann-benchmarks/blob/main/benchmark/datasets.py),
also linked by [SOSIA](https://github.com/Jacyhust/SOSIA).

| Task | Base file | Documents | Dimensions | Query file |
| --- | --- | ---: | ---: | --- |
| SPLADE-0.1M | [base_small.csr.gz](https://storage.googleapis.com/ann-challenge-sparse-vectors/csr/base_small.csr.gz) | 100,000 | 30,109 | `queries.dev.csr` |
| SPLADE-1M | [base_1M.csr.gz](https://storage.googleapis.com/ann-challenge-sparse-vectors/csr/base_1M.csr.gz) | 1,000,000 | 30,109 | `queries.dev.csr` |

The shared [development query file](https://storage.googleapis.com/ann-challenge-sparse-vectors/csr/queries.dev.csr.gz)
contains 6,980 queries. Files are downloaded from the original host, decompressed
without coordinate/value changes, and checked against the sizes, CSR headers,
and SHA-256 hashes in [`configs/datasets.json`](configs/datasets.json). The C++
loader additionally validates all pointers, sorted unique coordinates, and finite
nonnegative values. Documents must be nonzero; zero queries have an exact fallback.

```bash
bash scripts/download_data.sh
# Or download one scale:
python3 scripts/prepare_data.py --scale 0.1M
python3 scripts/prepare_data.py --scale 1M
```

Default location: `datasets/SPLADE/`. Existing verified files can be used with
`--offline --data-directory PATH`. See [`datasets/README.md`](datasets/README.md)
for the binary format and checksums. Each corpus gets its own freshly computed
exact ground truth. Full-corpus GT is never reused to evaluate a smaller subset.
Dataset use remains subject to the original [MS MARCO terms](https://microsoft.github.io/msmarco/).

## Reproduction

```bash
bash scripts/reproduce.sh splade-0.1m
bash scripts/reproduce.sh splade-1m
```

Each command verifies/downloads data, builds the executable, computes exact GT,
calibrates and builds a fresh index, measures single-threaded search, independently
recomputes Recall/QPS from ranked IDs and query records, and saves a new result
directory. No pretrained or hidden index is required. On a larger-memory machine,
the explicit resource guard can be raised, e.g. `--max-index-gib 12`; insufficient
resources produce an error, never a capped hash count or skipped `K` candidate.
The guard is a conservative build estimate, not a promise about peak RAM.

Both tasks use all **6,976 non-calibration queries**, three timing repeats on one
index, and five untimed warm-up queries. Four calibration queries are disjoint
from evaluation queries. Seed `20261007`, `k_max=100`, ten spherical Lloyd
iterations, and four build/GT threads follow the prior audited CABS execution
records. These settings, including the lightweight calibration size, are explicit
public reproduction settings; they do not establish an exact match to every
manuscript experiment. Search uses one thread regardless of build thread count.
The complete settings are in [`configs/`](configs/).

For a shorter functional/performance check, select a subset explicitly:

```bash
bash scripts/reproduce.sh splade-0.1m --eval-queries 100
bash scripts/reproduce.sh splade-1m --eval-queries 100
```

Such output has `full_query_run=false` and must be described as a subset run.
`--out PATH` accepts a **new** directory only. Every invocation builds a fresh
index and retains failed logs instead of replacing previous measurements.

## Results

The checked-in [measured examples](results/measured/) contain the two completed
local WSL runs, including compressed GT, ranked IDs, timing records and hashes.
They describe this implementation, this protocol and this machine.

| Dataset | Recall@50 | Single-thread QPS | IT (s) | IS incl. vectors (GB) |
| --- | ---: | ---: | ---: | ---: |
| SPLADE-0.1M | 0.995407 | 124.73 | 187.04 | 0.392970 |
| SPLADE-1M | 0.995717 | 12.04 | 751.13 | 3.461449 |

These numbers do **not** establish numerical reproduction of manuscript claims.
See [paper-alignment status](docs/PAPER_ALIGNMENT.md) and the
[verification report](docs/VERIFICATION.md) before citing them.

Results are saved under `results/splade-0.1m/TIMESTAMP/` or
`results/splade-1m/TIMESTAMP/`.

| Output | Contents |
| --- | --- |
| `metrics.json` | Measured Recall@50, QPS, construction time (IT), index size (IS), and per-repeat QPS |
| `resolved_config.json` | External settings and automatically selected `K`, all `m_i`, `B`, cluster memberships/radii, and stage timings |
| `queries.csv`, `retrieved.csv` | Every timed query/repeat, counters, and ranked document IDs with original inner-product scores |
| `ground_truth.csv`, `query_split.csv` | Fresh corpus-specific exact GT and disjoint calibration/evaluation IDs |
| `index_space.json` | Retained index component inventory, original vector bytes, and combined bytes |
| `config.json`, `manifest.json` | Parameters, input/source/binary hashes, commit/worktree state, hardware, commands, and completion status |
| `run.log`, `build.log`, `resource_usage.log` | Program/build output and GNU time peak RSS |
| `audit.json`, `complete.json` | Independent arithmetic/record validation and completed-file hashes |

To recompute the metrics from an existing result directory:

```bash
python3 scripts/evaluate.py results/splade-1m/TIMESTAMP
# Audit the published example directly (compressed CSVs are read automatically):
python3 scripts/evaluate.py results/measured/splade-1m
```

**Recall@50** is the intersection with exact top-50 IDs on the same complete
corpus, divided by 50 and averaged across queries/repeats. Exact rankings break
score ties by ascending document ID. Returned scores are checked against original
float32 sparse vectors, using double accumulation.

**QPS** is total measured query count divided by total measured query time across
all repeats. Query timing includes encoding, routing, MinHash probing, candidate
handling, exact inner-product scoring, and result formation. Loading, construction,
GT, warm-up, output writing, and independent validation are excluded. Timing
repeats reuse the same index; they are not independent index builds.

**IT** includes CSR loading/validation, document-frequency statistics, exact
calibration-target search, all candidate partitions and temporary indexes,
automatic parameter calibration, and final index construction. Evaluation GT is
reported separately and excluded. Some detailed stage timers contain other
stages; do not sum every stage field to infer IT.

**IS** follows the manuscript Section VI-A definition: retained index payload
**plus the original CSR vectors required for exact search scoring**. The structure
inventory includes sketches, encoding/partition metadata, local indexes,
collection metadata, and retained calibration buffers. It counts object headers
and owned vector capacities but excludes allocator/node overhead, query scratch,
and freed temporary structures; it is neither peak RSS nor a serialized file
size. `IS_GB` uses decimal GB. `index_structure_bytes` separately reports the
scope excluding original CSR; both scopes are preserved in `index_space.json`.
The old bucket-only field remains separately available in `summary.json` and is
not relabeled as total index size.

No manuscript target Recall/QPS/IT/IS is used as program output or an acceptance
threshold. Hardware, calibration and the public query protocol affect measured
results. See [`docs/VERIFICATION.md`](docs/VERIFICATION.md) for actual checks and
[`docs/MIGRATION.md`](docs/MIGRATION.md) for version provenance.

## Source layout

```text
include/cabs/
  io/           # CSR loader and exact ground truth
  sketch/       # IDF allocation and SOS encoding
  clustering/   # Spherical partition and spillover
  minhash/      # Local MinHash buckets
  calibration/  # Calibration configuration
  search/       # Candidate pool, routing, scoring, maintenance API
  reporting.hpp # Explicit retained-memory inventory
src/main.cpp    # Production CLI and semantic checks
configs/        # Two public task configurations and input checksums
scripts/        # Data preparation, reproduction, independent evaluation
tests/          # Production-path and result-integrity tests
```

The public tasks cover static full CABS only. Baseline implementations,
sensitivity/ablation sweeps, multi-encoder workloads and dynamic maintenance
benchmarks are outside this package. The existing maintenance API remains in
the core; no separate simplified demo algorithm is used for the public tasks.

The practical MinHash kernel uses keyed pseudorandom slot ranks, not an exact
independent uniform permutation sampler. Safe stopping bounds unvisited clusters;
it does not repair earlier sketch/hash/pool misses. These implementation
boundaries are recorded in [`docs/METHOD.md`](docs/METHOD.md).

## Citation and license

The manuscript title, author list and publication identifier will be added when
the authors finalize the citation. No publication metadata is fabricated here.
The source code is distributed under the [MIT License](LICENSE).
Dataset terms are separate.
