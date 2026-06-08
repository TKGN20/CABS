````markdown
# CABS: Cluster-Adaptive Binary Sketching for Sparse AMIPS

CABS is a C++ implementation of **Cluster-Adaptive Binary Sketching** for approximate maximum inner product search (AMIPS) over high-dimensional sparse vectors.

The method is designed for neural sparse retrieval workloads, where documents and queries are represented as non-negative sparse vectors and relevance is measured by sparse inner product. CABS combines IDF-aware dimension-adaptive binary sketching, spherical cluster routing with boundary spillover, and cluster-adaptive MinHash indexing. Candidate generation is performed through local sketch collisions, while the final ranking is computed by exact sparse inner products over the retained candidates.

## Main Features

- **Sparse AMIPS retrieval** over high-dimensional non-negative sparse vectors.
- **IDF-aware dimension-adaptive sketching** to suppress weak collisions from frequent dimensions and enhance informative low-frequency dimensions.
- **Spherical cluster routing** to restrict candidate generation to relevant local regions.
- **Boundary spillover** to improve coverage for vectors near cluster boundaries.
- **Cluster-adaptive MinHash indexing** with local hash counts and local candidate control.
- **Exact sparse inner-product refinement** for final top-k ranking.
- **Equal-recall evaluation protocol** for comparing QPS and verification cost at matched Recall@50 targets.

## Repository Structure

```text
CABS/
├── src/                         # C++ source files
├── scripts/                     # Auxiliary scripts
├── results/                     # Experiment outputs, ignored for large raw files
├── datasets/                    # Local datasets, ignored by Git
├── Makefile                     # Build script
├── run.sh                       # Simple run script
├── download.sh                  # Dataset download helper
├── multidataset_round1_config.json
├── .gitignore
└── README.md
````

Large datasets and raw experiment caches are not stored in this repository. They should be downloaded or generated locally.

## Requirements

The implementation has been tested with the following environment:

```text
OS: Ubuntu Linux
Compiler: g++ 11.4.0 or compatible C++17 compiler
CPU: x86 CPU with AVX512 support recommended
Build: Makefile
Parallelism: OpenMP
```

The Makefile uses C++17, `-Ofast`, `-march=native`, `-mavx512f`, and OpenMP flags. If your machine does not support AVX512, please remove `-mavx512f` from the Makefile before compilation.

## Build

Compile the project from the repository root:

```bash
make
```

or explicitly build the main target:

```bash
make sos
```

The executable will be generated as:

```text
./sos
```

To clean the executable and temporary text files:

```bash
make clean
```

## Dataset Preparation

### Downloadable Sparse Vector Data

The current download script uses the sparse vector files from the public ANN benchmark storage:

```text
https://storage.googleapis.com/ann-challenge-sparse-vectors/csr/
```

For the SPLADE-1M setting, the main files are:

```text
base_1M.csr.gz
queries.dev.csr.gz
```

You can download and decompress them manually:

```bash
mkdir -p "datasets/MS MARCO v1/SPLADE/1M"

wget -O "datasets/MS MARCO v1/SPLADE/1M/base_1M.csr.gz" \
  "https://storage.googleapis.com/ann-challenge-sparse-vectors/csr/base_1M.csr.gz"

wget -O "datasets/MS MARCO v1/SPLADE/1M/queries.dev.csr.gz" \
  "https://storage.googleapis.com/ann-challenge-sparse-vectors/csr/queries.dev.csr.gz"

gunzip "datasets/MS MARCO v1/SPLADE/1M/base_1M.csr.gz"
gunzip "datasets/MS MARCO v1/SPLADE/1M/queries.dev.csr.gz"
```

The expected local layout for SPLADE-1M is:

```text
datasets/
└── MS MARCO v1/
    └── SPLADE/
        └── 1M/
            ├── base_1M.csr
            ├── queries.dev.csr
            └── base_1M_all.ben
```

Here, `base_1M.csr` is the base sparse vector file, `queries.dev.csr` is the query sparse vector file, and `base_1M_all.ben` is the exact top-k ground-truth file used for evaluation. If the ground-truth file is absent, the program may create or load it depending on the selected execution mode and dataset size. For large datasets, preparing the ground truth in advance is recommended.

### CSR File Format

The program reads binary CSR files with the following layout:

```text
size_t nrow
size_t ncol
size_t nnz
size_t indptr[nrow + 1]
int    indices[nnz]
float  values[nnz]
```

The base vectors and query vectors are both stored in this format.

## Quick Start

After compiling the project and preparing the SPLADE-1M data, run:

```bash
./sos 11 multidataset_round1_config.json
```

The configuration file specifies dataset paths, query paths, ground-truth paths, output directory, top-k value, random seed, and enabled methods.

A simple script is also provided:

```bash
bash run.sh
```

The default script compiles the code, checks whether the selected dataset exists, downloads missing files through `download.sh`, and then runs several predefined modes.

## Configuration File

An example configuration is provided in:

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

The method switch section controls which methods are enabled:

```json
"methods": {
  "LinearScan": true,
  "SOSIA": true,
  "WAND": false,
  "BinSketch": false,
  "CABS": true
}
```

The output directory is controlled by:

```json
"output_dir": "results/multidataset_round1_tmp"
```

## Reproducing Experiments

The main evaluation protocol follows an equal-recall rule. For each dataset and target Recall@50, each method selects the configuration with the highest QPS among all configurations whose measured Recall@50 is no lower than the target.

The main metrics are:

```text
Recall@50        retrieval quality against exact top-50 ground truth
QPS              online query throughput
Avg. Verifications
                 average number of candidates entering exact sparse inner-product refinement
Indexing Time    offline index construction time
Index Size       offline index size
```

The default CABS working point used for mechanism validation and parameter analysis is:

```text
K = 15
n_probe = 5
eta = 0.03
tau = 0.8
IDF weight range = [0.5, 3.0]
m = 170
```

For the overall equal-recall comparison, the base expansion length `ell` is scanned in:

```text
ell in [3, 40]
```

The fastest configuration satisfying each target Recall@50 is then selected. The same selection rule is used across all datasets.

## Running Modes

The executable follows the general form:

```bash
./sos <mode> <dataset_or_config> [query_name_or_path]
```

Common examples:

```bash
# Multi-dataset evaluation from a JSON config
./sos 11 multidataset_round1_config.json

# Legacy script-based run
bash run.sh
```

Some older modes are kept for compatibility with previous SOSIA-style experiments and parameter scans. Please check `src/main.cpp` before changing the mode interface.

## Notes on Large Files

Do not commit datasets, raw experiment outputs, Visual Studio cache files, or binary caches to GitHub. The repository should only keep source code, scripts, configuration files, small figures, and documentation.

The `.gitignore` file is configured to exclude common large files and caches, including:

```text
datasets/
data/
*.csr
*.bin
*.npy
*.npz
results/**/raw/
results/**/cache/
.vs/
*.ipch
```

If you need to publish full datasets or large generated indexes, use an external storage service or Git LFS. For reproducible papers, it is recommended to provide download links, preprocessing scripts, and checksums instead of storing large binary files in the Git repository.

## Citation

If you use this code, please cite the corresponding paper:

```bibtex
@inproceedings{cabs2027,
  title     = {Cluster-Adaptive Binary Sketching for Sparse Maximum Inner Product Search},
  author    = {Anonymous},
  booktitle = {Proceedings of the IEEE International Conference on Data Engineering},
  year      = {2027}
}
```

The citation entry will be updated after the paper information is finalized.

## License

This repository is intended for academic research. Please add a formal license file before public release if the code is to be redistributed or reused by external users.

```
```
