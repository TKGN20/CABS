# CABS: Cluster-Adaptive Binary Sketching for Sparse AMIPS

## Introduction

CABS is a C++17 implementation of approximate maximum inner product search over
sparse vectors, accompanying *Adaptive Binary Sketching for Approximate Maximum
Inner Product Search over Sparse Vectors*.
It combines IDF-aware SOS encoding, spherical partitioning with spillover, and
cluster-adaptive MinHash candidate generation. Final candidates are ranked by
their original sparse inner products. Index construction, automatic K/m/B
calibration, static search and dynamic maintenance are included.

## Compilation

Requirements: Linux or WSL, GCC 9+ with OpenMP, CMake 3.16+, Python 3.8+, Git and
GNU time. Python scripts use the standard library. Native Windows/MSVC builds
are not supported.

On Ubuntu, install the dependencies and build:

```bash
sudo apt-get install git g++ cmake make python3 time
git clone https://github.com/TKGN20/CABS.git
cd CABS
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 2
ctest --test-dir build --output-on-failure
```

The executable is `build/cabs`. `make`, `make test` and `make sanitize` are also
available for building, testing and ASan/UBSan checks.

## Command Usage

A small example can be run without downloading any dataset:

```bash
python3 examples/tiny.py --out results/tiny
```

To also exercise insertion, deletion, update and refresh:

```bash
python3 examples/tiny.py --maintenance --out results/tiny-maintenance
```

Both examples generate CSR inputs, call the CABS executable and independently
check exact ground truth and returned scores. Use a new output directory for
each run. These are functional examples; their timings are not dataset results.

## Dataset

The public SPLADE inputs are from the
[BigANN sparse-vector benchmark](https://github.com/harsha-simhadri/big-ann-benchmarks/blob/main/benchmark/datasets.py).
The two supplied tasks share `queries.dev.csr`:

| Dataset | Base file | Documents | Dimensions |
| --- | --- | ---: | ---: |
| SPLADE-0.1M | `base_small.csr` | 100,000 | 30,109 |
| SPLADE-1M | `base_1M.csr` | 1,000,000 | 30,109 |

Download and verify both scales:

```bash
bash scripts/download_data.sh
```

Files are stored in `datasets/SPLADE/`. To prepare only one scale, use
`python3 scripts/prepare_data.py --scale 0.1M` or `--scale 1M`.
The [input manifest](configs/datasets.json) fixes download URLs, sizes and
SHA-256 checksums. Dataset use remains subject to the
[MS MARCO terms](https://microsoft.github.io/msmarco/).

Inputs use little-endian binary CSR:

```text
uint64 n, d, nnz
uint64 indptr[n + 1]
int32 indices[nnz]
float32 values[nnz]
```

Coordinates must be sorted and unique within each row; weights must be finite
and nonnegative, and base vectors must be nonzero.
See [datasets/README.md](datasets/README.md) for the full format and checks.

## Reproduction in Linux

Run CABS on either public task:

```bash
bash scripts/reproduce.sh splade-0.1m
bash scripts/reproduce.sh splade-1m
```

Each command verifies inputs, computes exact ground truth, calibrates K/m/B,
builds a fresh index, runs single-threaded queries and evaluates the outputs.
The principal defaults are:

| Parameter | Value |
| --- | --- |
| Result count `k` | 50 |
| SOS expansion capacity `ell` | 24 |
| MinHash collision-failure setting `delta_h` | 0.2 |
| Calibration targets `k_max` | 100 per calibration query |
| Calibration / evaluation queries | 4 / remaining 6,976 |
| Seed | 20261007 |
| Build and GT threads / search threads | 4 / 1 |
| Timing repeats | 3 |

Complete settings are in [configs/](configs/); K, local hash counts `m_i` and
candidate-pool capacity B are selected automatically.
For existing verified public inputs:

```bash
bash scripts/reproduce.sh splade-1m --offline --data-directory /data/SPLADE
```

Append `--eval-queries 100` to evaluate a query subset, or `--out PATH` to select
a new output directory. Existing runs are never overwritten. Run
`bash scripts/reproduce.sh --help` for all script options.

## Results

New runs are written to `results/DATASET/TIMESTAMP/`. They include
`metrics.json` (Recall, QPS, IT and IS), `resolved_config.json`, exact GT,
ranked IDs/scores, per-query times, input/source hashes and execution logs.

Existing measurements and their source versions are listed in
[the reproducibility notes](docs/REPRODUCIBILITY.md#recorded-results-and-output-files).
To check the saved records without downloading vectors:

```bash
python3 scripts/verify_artifacts.py
```

This recomputes metrics from saved measurement records; it does not rerun
retrieval or collect new timings.
Metric definitions, output fields and troubleshooting are in
[docs/REPRODUCIBILITY.md](docs/REPRODUCIBILITY.md).

## Documentation

- [Reproduction details, measurements and limitations](docs/REPRODUCIBILITY.md)
- [Algorithm mechanisms and source mapping](docs/PAPER_IMPLEMENTATION.md)
- [Implementation contract and maintenance](docs/METHOD.md)
- [Implementation complexity](docs/COMPLEXITY.md)
- [Final acceptance report and CI evidence](docs/OPEN_SOURCE_ACCEPTANCE.md)

## Reference

Publication metadata will be added when available. For code reuse, cite this
repository and record the commit used in your experiments:

```bibtex
@misc{cabs_code,
  author = {{CABS contributors}},
  title = {{CABS}: Sparse Approximate Maximum Inner Product Search},
  year = {2026},
  url = {https://github.com/TKGN20/CABS},
  note = {Source code}
}
```

## License

[MIT License](LICENSE). Dataset-provider terms apply separately to the vectors.
