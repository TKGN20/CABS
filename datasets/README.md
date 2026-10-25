# Public SPLADE inputs

Run `bash scripts/download_data.sh` from the repository root. Original inputs
are stored locally in `datasets/SPLADE/` and excluded from Git.

The fixed manifest [`configs/datasets.json`](../configs/datasets.json) records
the original-host URL, uncompressed SHA-256, exact size, row/dimension and entry
counts of each file. A valid size alone is insufficient; checksums must match.
The download helper never rescales, reorders or subsamples vectors.

Little-endian CSR64 binary layout:

1. `uint64 n, d, nnz`
2. `uint64 indptr[n+1]`
3. `int32 indices[nnz]`
4. `float32 values[nnz]`

Data pointers must be monotone; row coordinates must be unique and sorted;
weights must be finite and nonnegative. The C++ loader checks all entries before
building an index. The public 0.1M file contains 12,729,954 entries, while the 1M
file contains 126,320,094 entries. These identify specific published files, not
arbitrary samples of the same size.

The corresponding mean document nnz values are 127.29954 and 126.320094.
The query file contains 342,696 entries over 6,980 rows, or 49.0968481 entries
per query. These statistics belong to the fixed hashed files; a manuscript
table describing another slice must identify that slice separately.

Exact top-100 ground truth is freshly computed on the selected corpus; Recall@50
uses the first 50 entries. The fixed shuffled split is saved as query IDs in each
run. Four calibration queries are excluded from all 6,976 evaluation queries.
Both scales reuse the query file, not a full-corpus ground-truth file.

The data remains subject to its original provider's terms. This repository does
not redistribute it or grant a separate dataset license.
