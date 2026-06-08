````markdown
# CABS: Cluster-Adaptive Binary Sketching for Sparse AMIPS

This repository provides the C++ implementation of **CABS**, a cluster-adaptive binary sketching framework for approximate maximum inner product search (AMIPS) over high-dimensional sparse vectors.

The code is intended to support reproducibility checks for sparse AMIPS experiments. It includes the implementation of CABS, baseline-related experimental entry points, dataset loading utilities, configuration files, and scripts for running representative experiments.

## 1. What is CABS?

CABS targets neural sparse retrieval workloads. In these workloads, documents and queries are represented as high-dimensional non-negative sparse vectors, and relevance is measured by sparse inner product.

CABS follows a candidate generation plus exact refinement pipeline:

1. It constructs IDF-aware dimension-adaptive binary sketches from sparse vectors.
2. It partitions data vectors into directional local regions using spherical clustering.
3. It expands boundary clusters through a spillover mechanism.
4. It builds cluster-level MinHash indexes for local candidate generation.
5. It verifies retained candidates by exact sparse inner products over the original sparse vectors.

The sketch and MinHash structures are used only for candidate generation. Final ranking is still based on the original sparse inner product.

## 2. Repository Structure

```text
CABS/
├── src/                         # Core C++ source files
│   ├── main.cpp                  # Main entry and experiment modes
│   ├── Preprocess.cpp/.h         # Sparse data loading and ground-truth handling
│   ├── spherical_kmeans.*        # Spherical clustering
│   ├── cluster_index.*           # Cluster-level index construction
│   ├── cluster_query.*           # Cluster-level query processing
│   ├── sosia.*                   # SOSIA-style sketch baseline components
│   └── ...
├── scripts/                     # Auxiliary scripts
├── results/                     # Experiment outputs
├── datasets/                    # Local datasets, not tracked by Git
├── Makefile                     # Build script
├── run.sh                       # Legacy run script
├── download.sh                  # Dataset download helper
├── multidataset_round1_config.json
├── .gitignore
└── README.md
````

Large datasets, binary ground-truth files, raw experiment outputs, and cache files should not be committed to this repository.

## 3. Environment

The implementation has been tested under the following environment:

```text
Operating system: Ubuntu Linux
Compiler: g++ 11.4.0 or compatible C++17 compiler
Parallelism: OpenMP
CPU: x86 CPU
```

The current Makefile uses aggressive optimization flags, including `-Ofast`, `-march=native`, OpenMP, and AVX512-related options. If the target machine does not support AVX512, remove `-mavx512f` from `Makefile` before compiling.

## 4. Build

Compile the project from the repository root:

```bash
make
```

or explicitly build the main target:

```bash
make sos
```

The executable is generated as:

```text
./sos
```

To remove the executable and temporary text files:

```bash
make clean
```

## 5. Dataset Preparation

### 5.1 Download Links

The full datasets are not included in this repository because of their size. The SPLADE sparse vectors can be downloaded from the public ANN benchmark sparse-vector storage:

```text
https://storage.googleapis.com/ann-challenge-sparse-vectors/csr/
```

For the SPLADE-1M setting, the required files are:

```text
base_1M.csr.gz
queries.dev.csr.gz
```

The repository provides `download.sh` as a helper script. It downloads `queries.dev.csr.gz` and the selected base file from the above storage location, then decompresses them locally.

A manual download example is:

```bash
mkdir -p "datasets/MS MARCO v1/SPLADE/1M"

wget -O "datasets/MS MARCO v1/SPLADE/1M/base_1M.csr.gz" \
  "https://storage.googleapis.com/ann-challenge-sparse-vectors/csr/base_1M.csr.gz"

wget -O "datasets/MS MARCO v1/SPLADE/1M/queries.dev.csr.gz" \
  "https://storage.googleapis.com/ann-challenge-sparse-vectors/csr/queries.dev.csr.gz"

gunzip "datasets/MS MARCO v1/SPLADE/1M/base_1M.csr.gz"
gunzip "datasets/MS MARCO v1/SPLADE/1M/queries.dev.csr.gz"
```

### 5.2 Expected Directory Layout

For SPLADE-1M, the expected local layout is:

```text
datasets/
└── MS MARCO v1/
    └── SPLADE/
        └── 1M/
            ├── base_1M.csr
            ├── queries.dev.csr
            └── base_1M_all.ben
```

Only the following two files need to be downloaded manually:

```text
base_1M.csr
queries.dev.csr
```

The file `base_1M_all.ben` is the exact top-k ground-truth file used by the evaluation code. It is generated locally by the program when the corresponding benchmark file is absent. For large datasets, this step can be time-consuming because it requires exhaustive sparse inner-product computation. If a precomputed `.ben` file is available in your environment, it can be placed at the path specified in the configuration file.

### 5.3 CSR Binary Format

The sparse vectors are stored in a binary CSR format:

```text
size_t nrow
size_t ncol
size_t nnz
size_t indptr[nrow + 1]
int    indices[nnz]
float  values[nnz]
```

Both base vectors and query vectors use this format.

## 6. Configuration File

A multi-dataset configuration example is provided in:

```text
multidataset_round1_config.json
```

A typical dataset entry is:

```json
{
  "name": "SPLADE-1M",
  "base_path": "datasets/MS MARCO v1/SPLADE/1M/base_1M.csr",
  "query_path": "datasets/MS MARCO v1/SPLADE/1M/queries.dev.csr",
  "groundtruth_path": "datasets/MS MARCO v1/SPLADE/1M/base_1M_all.ben"
}
```

The method switch controls which methods are enabled:

```json
"methods": {
  "LinearScan": true,
  "SOSIA": true,
  "WAND": false,
  "BinSketch": false,
  "CABS": true
}
```

The output directory is specified by:

```json
"output_dir": "results/multidataset_round1_tmp"
```

When adding a new dataset, update `base_path`, `query_path`, and `groundtruth_path` consistently.

## 7. Running Experiments

### 7.1 Multi-Dataset Evaluation

After compiling the code and preparing the data, run:

```bash
./sos 11 multidataset_round1_config.json
```

This mode reads dataset and method settings from the JSON configuration file.

### 7.2 Legacy Script

A simple legacy script is also provided:

```bash
bash run.sh
```

This script compiles the code, checks whether a selected dataset exists, downloads missing files through `download.sh`, and runs several predefined modes. The script is mainly kept for compatibility with earlier experiments. For controlled experiments, using the JSON configuration mode is recommended.

### 7.3 General Executable Format

The general command format is:

```bash
./sos <mode> <dataset_or_config> [query_name_or_path]
```

The main multi-dataset mode is:

```bash
./sos 11 <config_path>
```

Some older modes are retained for parameter tuning and compatibility with previous SOSIA-style experiments. If you modify the mode interface, check `src/main.cpp`.

## 8. Experimental Protocol

The main evaluation uses an equal-recall protocol.

For each dataset and each target Recall@50, every method scans its parameter configurations and selects the configuration with the highest QPS among those whose measured Recall@50 is no lower than the target.

The reported metrics include:

```text
Recall@50
    Fraction of exact top-50 results recovered by the approximate method.

QPS
    Online query throughput. Index loading and disk I/O are excluded.

Avg. Verifications
    Average number of candidates entering exact sparse inner-product refinement.

Indexing Time
    Offline index construction time.

Index Size
    Size of the constructed index and auxiliary structures.
```

For CABS, the default working point used in mechanism validation and parameter analysis is:

```text
K = 15
n_probe = 5
eta = 0.03
tau = 0.8
IDF weight range = [0.5, 3.0]
m = 170
```

For the overall equal-recall comparison, the base expansion length is scanned in:

```text
ell in [3, 40]
```

The fastest qualified configuration at each target Recall@50 is selected. The same selection rule is used across datasets.

## 9. How to Change Metrics or Output Statistics

The code reports retrieval quality and efficiency statistics through the experiment runners and result output structures.

If you want to add a new metric, follow this process:

1. Locate the query evaluation logic in the corresponding method runner.
2. Add the metric field to the result structure used by that runner.
3. Compute the metric after query processing or after exact refinement.
4. Update the CSV or text output section to write the new field.
5. Re-run the same configuration to ensure the new metric is computed for all compared methods.

Typical metrics that can be added include:

```text
P95 query latency
P99 query latency
candidate set size before deduplication
candidate set size after deduplication
number of visited buckets
number of active clusters
index construction memory peak
```

When adding a metric, keep the equal-recall protocol unchanged. A new metric should describe the behavior of a configuration, while the configuration selection rule should still be based on Recall@50 and QPS unless explicitly stated otherwise.

## 10. How to Add a New Dataset

To add a new dataset:

1. Convert the base vectors and query vectors into the binary CSR format described above.
2. Place the files under `datasets/`.
3. Add a dataset entry to the JSON configuration file.
4. Set `groundtruth_path` to the desired `.ben` location.
5. Run the selected mode. If the `.ben` file is absent, the program can generate it locally, although this may be expensive for large datasets.

Example:

```json
{
  "name": "NewDataset-1M",
  "base_path": "datasets/NewDataset/1M/base.csr",
  "query_path": "datasets/NewDataset/1M/queries.dev.csr",
  "groundtruth_path": "datasets/NewDataset/1M/base_all.ben"
}
```

## 11. Large File Policy

Do not commit the following files to GitHub:

```text
datasets/
data/
*.csr
*.ben
*.bin
*.npy
*.npz
*.pt
*.pth
*.pkl
results/**/raw/
results/**/cache/
.vs/
*.ipch
```

The repository should store only source code, scripts, configuration files, documentation, small figures, and small result summaries.

If large datasets, generated indexes, or binary caches need to be shared, use external storage or Git LFS. For paper review, it is recommended to provide data download instructions, preprocessing steps, and configuration files rather than storing large binaries directly in the repository.

## 12. Notes for Reviewers

To inspect the project quickly:

1. Check `src/main.cpp` for available execution modes.
2. Check `Preprocess.cpp` for the CSR loading format and benchmark handling.
3. Check `cluster_index.*` and `cluster_query.*` for CABS index construction and query processing.
4. Check `multidataset_round1_config.json` for dataset paths and enabled methods.
5. Run `make` to build the executable.
6. Prepare `base_1M.csr` and `queries.dev.csr` under the expected SPLADE-1M path.
7. Run `./sos 11 multidataset_round1_config.json`.

The repository is intended to support code inspection and experiment reproduction. Full large-scale reproduction requires downloading the sparse vector files and generating or providing the corresponding `.ben` ground-truth files locally.

```
```
