# CABS: Cluster-Adaptive Binary Sketching for Sparse AMIPS

CABS is a C++17 implementation of sparse approximate maximum inner product
search. It combines IDF-aware SOS encoding, spherical partitioning with
spillover, and cluster-adaptive MinHash candidate generation. Candidates are
ranked using their original sparse inner products with double accumulation.

## Compilation

Requirements: Linux or WSL, GCC 9 or later with OpenMP, CMake 3.16 or later,
Python 3.8 or later, and GNU time. Python scripts use the standard library.
The CSR loader uses POSIX memory mapping. Native Windows/MSVC is unsupported.

```bash
sudo apt-get install g++ cmake make python3 time
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

The executable is `build/cabs`. `make`, `make test`, and `make sanitize` provide
build, test, and ASan/UBSan checks. Release compilation uses `-O3`; neither
fast-math nor AVX-512 is required; floating-point contraction is disabled to
preserve the score accumulation convention. The benchmark script explicitly disables
sanitizers so an existing debug build cannot silently affect timing.

## Dataset

The public SPLADE vectors come from the
[BigANN sparse-vector benchmark](https://github.com/harsha-simhadri/big-ann-benchmarks/blob/main/benchmark/datasets.py).
The same CSR source is documented by
[SOSIA](https://github.com/Jacyhust/SOSIA#dataset) and
[SINDI](https://github.com/Roxanne0321/vsag/tree/sparse#-offline-evaluation-datasets).

| Task | Base file | Documents | Dimensions | Query file |
| --- | --- | ---: | ---: | --- |
| SPLADE-0.1M | `base_small.csr` | 100,000 | 30,109 | `queries.dev.csr` |
| SPLADE-1M | `base_1M.csr` | 1,000,000 | 30,109 | `queries.dev.csr` |

```bash
bash scripts/download_data.sh
python3 scripts/prepare_data.py --scale 0.1M
python3 scripts/prepare_data.py --scale 1M
```

The first command prepares both scales; the other commands prepare one scale.
Files are stored in `datasets/SPLADE/`. URLs, headers, sizes and SHA-256 values
are fixed in [configs/datasets.json](configs/datasets.json). Downloads preserve
coordinates, float32 values and row order. Dataset terms are separate from the
source-code license; see the [MS MARCO terms](https://microsoft.github.io/msmarco/).

The little-endian binary layout is `uint64(n,d,nnz)`, `uint64 indptr[n+1]`,
`int32 indices[nnz]`, and `float32 values[nnz]`. Coordinates must be sorted and
unique per row; weights must be finite and nonnegative. Base vectors must be
nonzero. See [datasets/README.md](datasets/README.md) for validation details.

## Command usage

Each command performs data verification, exact ground-truth generation,
automatic calibration, fresh index construction, single-threaded search and
independent result evaluation:

```bash
bash scripts/reproduce.sh splade-0.1m
bash scripts/reproduce.sh splade-1m
```

Existing verified data can be used without downloading:

```bash
bash scripts/reproduce.sh splade-1m --offline --data-directory /data/SPLADE
```

For a shorter check, explicitly select fewer evaluation queries:

```bash
bash scripts/reproduce.sh splade-1m --eval-queries 100
```

Such runs are recorded with `full_query_run=false`. `--out PATH` selects a new
output directory; existing results are never overwritten. Use `--help` for all
script options. The complete static algorithm is used for both full and subset
runs. No prebuilt index or checked-in result is used to answer queries.

### Parameters

| Setting | Value |
| --- | --- |
| Result count | `k=50` |
| SOS expansion capacity | `ell=24` |
| MinHash collision-failure setting | `delta_h=0.2` |
| Calibration target count | `k_max=100` per calibration query |
| Calibration / evaluation queries | 4 / all remaining 6,976 |
| Seed | 20261007 |
| Lloyd iterations | 10 |
| Build and GT threads / search threads | 4 / 1 |
| Timing repeats / warm-up queries | 3 / 5 |

These settings are recorded in [configs/](configs/). Internal `K`, local `m_i`,
and global candidate-pool capacity `B` are automatically calibrated. The memory
guard stops an infeasible build without reducing those parameters or skipping a
candidate configuration. `--max-index-gib` changes only this resource guard.

### Direct executable

```bash
build/cabs inspect --data datasets/SPLADE/base_1M.csr --queries datasets/SPLADE/queries.dev.csr
build/cabs selftest
```

The `run` command exposes the core settings using `--ell`, `--delta-h`, `--k`,
`--kmax`, `--seed`, `--threads`, `--calibration-queries`, `--eval-queries`,
`--iterations`, and `--repeats`. Prefer the reproduction script for benchmark
runs: it records the configuration, environment and file hashes automatically.

## Results

All numerical results in [results/measured/](results/measured/) are actual
measurements with raw query times, ranked IDs, exact GT and configuration hashes.
Each full-run record identifies its measured source version. Both were collected
on an AMD Ryzen 7 8845H under WSL, GCC 11.4, using the protocol above. Search was
single-threaded. Later source revisions and their validation are recorded in
[docs/VERIFICATION.md](docs/VERIFICATION.md).

| Dataset | Source | Recall@50 | QPS | IT (s) | IS (GB) |
| --- | --- | ---: | ---: | ---: | ---: |
| SPLADE-0.1M | `80b25cf` | 0.995407 | 158.12 | 164.71 | 0.392970 |
| SPLADE-1M | `ac17836` | 0.995717 | 12.04 | 751.13 | 3.461449 |

Hardware and software are part of each measurement. These local observations
are not server measurements. The [server execution procedure](docs/EXPERIMENTS.md)
uses the same algorithm, input checksums and metric definitions.

New runs are saved in `results/DATASET/TIMESTAMP/`:

| File | Contents |
| --- | --- |
| `metrics.json` | Measured Recall, QPS, IT, IS and per-repeat QPS |
| `resolved_config.json` | External parameters, automatic K/m/B and stage timings |
| `queries.csv`, `retrieved.csv` | Per-query times/counters and ranked IDs/scores |
| `ground_truth.csv`, `query_split.csv` | Corpus-specific exact GT and disjoint query IDs |
| `index_space.json` | Index component and original-vector byte counts |
| `manifest.json`, `config.json` | Input/source/binary hashes, environment, command and status |
| `run.log`, `build.log`, `resource_usage.log` | Execution/build logs and peak RSS |
| `audit.json`, `complete.json` | Independent validation and completed-file hashes |

```bash
python3 scripts/evaluate.py results/measured/splade-1m
```

The evaluator reads either CSV or compressed CSV files, verifies published-file
checksums, checks result ordering and completeness, and recomputes Recall/QPS.
Running the evaluator does not execute a new search or produce new timing data.

### Metric definitions

- **Recall@50:** overlap with exact top-50 on the same corpus, averaged across
  evaluation queries. Exact scores use float32 inputs and double accumulation;
  ties are resolved by ascending document ID.
- **QPS:** all timed queries across three repeats divided by their total query
  time. Includes query encoding, routing, probing, candidate handling, exact
  scoring and result formation. Excludes build, GT, warm-up, validation and IO.
  Timing repeats reuse one index and are not independent builds.
- **IT:** input loading/validation, document frequencies, exact calibration GT,
  all candidate partitions and temporary indexes, automatic calibration and final
  index construction. Evaluation GT is excluded. Nested stage times are not
  independently additive.
- **IS:** retained search payload including original CSR vectors required for
  exact scoring, in decimal GB. The component inventory counts object headers
  and owned vector capacities. Allocator/node overhead, query scratch and freed
  temporary structures are excluded. IS is distinct from process peak RSS;
  structure-only and bucket-only byte counts are retained as separate fields.

## Implementation and scope

The source includes static construction/search and the insertion, deletion,
update and refresh APIs. Tests exercise these operations. The two public task
scripts evaluate static CABS; full baseline comparisons, parameter sweeps,
ablation, other encoders and dynamic benchmark tables are outside their scope.
Indexes are built in memory; disk save/load and service deployment are not
implemented. See [the repository comparison](docs/REPOSITORY_COMPARISON.md) and
[implementation details](docs/METHOD.md).

## License

[MIT License](LICENSE). Data-provider terms continue to apply to the datasets.

## Reference

This repository accompanies *Adaptive Binary Sketching for Approximate Maximum
Inner Product Search over Sparse Vectors*. Publication metadata will be added
when available.
