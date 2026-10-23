# Measured results

`measured/splade-0.1m/` and `measured/splade-1m/` contain completed full runs of
the static algorithm. Metrics, compressed raw CSV records, resolved parameters
and hashes identify the exact implementation and execution environment.

New reproduction commands create `results/DATASET/TIMESTAMP/`. They do not read
checked-in results as search inputs and never overwrite existing runs. A failed
run retains its log and a manifest with `status=failed`.

```bash
python3 scripts/evaluate.py results/measured/splade-0.1m
python3 scripts/evaluate.py results/measured/splade-1m
```

The evaluator verifies artifact hashes and recomputes metrics from raw records.
GT and retrieved IDs have not been interpolated; query times are actual timed
search calls. `metrics.json` and `audit.json` are derived files; the published
artifact manifest covers the immutable measurement/provenance records.

The review branch also retains a complete candidate 1M task under
`engineering/review_1m_20261022/`. Its preserved Git-capture limitation and
independent snapshot-content proof are in `source_attestation.json`.
`engineering/selected_profile_1m_20261022/` contains a final-index reconstruction
checked against that run. `engineering/calibration_audit_small_20261022/` contains
all-K temporary-index costs. Diagnostics use small subsets and instrumented
profiles; they do not replace complete default-task metrics.
