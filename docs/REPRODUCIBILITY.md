# Reproducibility and implementation notes

For installation and runnable examples, start with [README](../README.md).
This guide documents the CABS-only evaluation protocol, versioned measurements,
output files, metric definitions and practical limits. Historical measurements
remain associated with their original source revisions.

## Default configuration

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
| Static document-sketch cache retention | disabled for static dataset tasks |

These settings are recorded in [configs/](../configs/). Internal `K`, local `m_i`,
and global candidate-pool capacity `B` are automatically calibrated. The memory
guard stops an infeasible build without reducing those parameters or skipping a
candidate configuration. `--max-index-gib` changes only this resource guard.

## Direct executable

```bash
build/cabs inspect --data datasets/SPLADE/base_1M.csr --queries datasets/SPLADE/queries.dev.csr
build/cabs selftest
```

The `run` command exposes the core settings using `--ell`, `--delta-h`, `--k`,
`--kmax`, `--seed`, `--threads`, `--calibration-queries`, `--eval-queries`,
`--iterations`, and `--repeats`. Prefer the reproduction script for benchmark
runs: it records the configuration, environment and file hashes automatically.

## Recorded results and output files

All numerical results in [results/measured/](../results/measured/) are actual
measurements with raw query times, ranked IDs, exact GT and configuration hashes.
Each full-run record identifies its measured source version. Both were collected
on an AMD Ryzen 7 8845H under WSL, GCC 11.4, using the protocol above. Search was
single-threaded. Later source revisions and their validation are recorded in
[docs/VERIFICATION.md](../docs/VERIFICATION.md).

| Dataset | Source | Recall@50 | QPS | IT (s) | IS (GB) |
| --- | --- | ---: | ---: | ---: | ---: |
| SPLADE-0.1M | `80b25cf` | 0.995407 | 158.12 | 164.71 | 0.392970 |
| SPLADE-1M | `ac17836` | 0.995717 | 12.04 | 751.13 | 3.461449 |

Hardware and software are part of each measurement. These are local
observations; no server measurements or assumed hardware speedups are claimed.

Additional versioned full-query measurements are retained:

| Dataset | Source | Recall@50 | QPS | IT (s) | IS (GB) |
| --- | --- | ---: | ---: | ---: | ---: |
| SPLADE-0.1M | `fd470db` | 0.995407 | 185.54 | 178.50 | 0.209247 |
| SPLADE-1M | `b327ec35`, content-attested | 0.995717 | 17.47 | 747.44 | 1.746099 |

Raw records are under [the cache candidate](../results/engineering/cache_20261022/)
and [the completed 1M review run](../results/engineering/review_1m_20261022/).
The latter's complete GT, ranked outputs, split and all 20,928 non-timing query
records match the prior K=8/B=51385 realization. Its source/pipeline hashes match
the detached b327ec35 snapshot; WSL could not resolve the Windows-created
worktree's Git metadata path, so the original null Git field and the independent
content attestation are both preserved. Later documentation/test revisions do
not change the production source files. These separate full runs do not isolate
the speedup of individual optimizations.

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

## Implementation and maintenance

CABS provides static index construction/search and the maintenance operations
`cabs_index::Index::insert`, `erase`, `update` and `refresh`. The public
`examples/tiny.py --maintenance` example exercises the `dynamic` CLI command.
Tests cover tombstones, fresh internal identifiers, refresh and sketch-cache
lifecycle. Indexes are built in memory; concurrent maintenance, crash recovery
and disk save/load are not implemented.

For static dataset tasks, per-document construction sketches are released after
final hash-table construction. Cardinalities and final tables remain available
for search. Release cost is included in IT; the same-index byte inventory is
saved as `sketch_cache_release.json`. The core option `--keep-sketch-cache 1`
retains the cache; public dataset configurations use `retain_sketch_cache=false`.
IS payload reductions must not be equated with peak-RSS reductions.

See the [mechanism-to-code map](../docs/PAPER_IMPLEMENTATION.md),
[implementation contract](../docs/METHOD.md) and [implementation complexity](../docs/COMPLEXITY.md)
for automatic K/m/B, routing/stopping, dense workspaces/centroids, bucket lookup
and the original-vector scorer. The optional
[calibration audit](../docs/CALIBRATION_AUDIT.md) is separate from default QPS runs.
Its instrumented subset diagnostics do not replace full-query measurements.

## Verify published records

No vector download is needed to check the saved measurements:

```bash
python3 scripts/verify_artifacts.py
```

For historical source verification, use a full Git clone:

```bash
python3 scripts/verify_artifacts.py --source-check
```

The first command also works from a source archive. The second additionally
checks the measured source revisions against Git blobs. Both check the
[19-file production/default freeze](../docs/approval_baseline.json). Neither
command builds a new full-corpus index or collects new query timings.

## Common errors and limitations

- A missing offline input requires downloading it first or correcting
  `--data-directory`. A checksum mismatch means the file is not the manifest's
  public input; do not bypass verification or replace the manifest hash.
- Choose a new `--out` directory for every run. Existing directories are refused
  to preserve earlier results. Interrupted downloads can leave ignored `.part`
  files; rerunning the preparation command retries the download.
- An index-memory guard failure is a resource error, not a completed result.
  `--max-index-gib` changes the guard, not automatic K/m/B. Check available RAM
  before increasing it. See [implementation complexity](../docs/COMPLEXITY.md).
- Indexes are in memory. Concurrent maintenance, crash recovery, persistence and
  native Windows builds are not provided. Retrieval is approximate; calibration
  is not a universal Recall@50 guarantee. Hardware-dependent QPS is not a release
  acceptance threshold.

## Documentation and archived reviews

The [final acceptance report](../docs/OPEN_SOURCE_ACCEPTANCE.md) and
[release checklist](../CABS_RELEASE_CHECKLIST.md) identify the accepted code,
clean-clone records and CI. The [review index](../docs/REVIEW_INDEX.md),
[review response](../docs/REVIEW_RESPONSE.md), [approval review](../docs/APPROVAL_REVIEW_ZH.md)
and [verification history](../docs/VERIFICATION.md) preserve earlier evidence.
Server proposals and third-party comparison notes are historical research
material, outside the completed CABS-only open-source delivery.
