# Historical Performance Engineering Review (2026-10-22)

> **Historical engineering review (2026-10-22).** This document records
> experiments and review decisions made before PR #4 was merged into `main` on
> 2026-10-25. References to a "candidate branch" or "not yet merged" describe the
> status at that time, not the current repository state. The completed full-query
> runs cited here are recorded measurements. Current instructions and measured
> result provenance are in [README](../README.md) and the
> [Reproducibility guide](REPRODUCIBILITY.md). The historical record below is
> preserved unchanged.

This branch is a review candidate based on stable main d091458. It has not
replaced that release. A completed full 1M review run is now available below;
no new paper-server performance is claimed.

## Ordered exact verification

Sorting admissions by document ID preserves the per-cluster candidate and
verified sets, exact per-document score accumulation, routing and strict stopping.
On one shared 0.1M index (K=16, B=7772), 200 queries over four alternating repeats
measured 123.6625 to 126.8648 QPS, a 1.0259x ratio. All four repeats were faster.
Full per-cluster state, result and counter gates pass. This is a small-sample
engineering observation; the benchmark and raw records are under benchmarks/
and results/engineering/order_20261022/.

## Static sketch-cache release

The final tables and retained document cardinalities support query processing
without the document construction sketches. Static run releases that cache;
normal insert/delete/update/refresh are tested afterward, and low-level table or
calibration reconstruction requires a complete cache or a full rebuild.

The full 0.1M run of source fd470db evaluates all 6,976 queries over three repeats:
Recall@50 0.9954071101, QPS 185.5433191, IT 178.5021363 s, IS 0.209246838 GB.
On that same realized index, retained payload including vectors changes from
392,970,297 to 209,246,838 bytes (46.7525% reduction). Buckets, cardinality count,
K=1 and B=13976 remain unchanged. Release time 0.008126129 s is included in IT.
GT, ranked results, query split and all non-timing fields of 20,928 query records
match the prior K=1 full run. Raw records are in
results/engineering/cache_20261022/.

This full run contains both candidate changes and is a separate fresh build;
its QPS is not a controlled attribution to either change. Payload reduction is
not a claim about RSS or peak construction memory. The full-run measurement
commit is retained explicitly; subsequent cache-state guards and tests do not
retroactively change its source hash or timings.

## Rejected pool candidate

A separate in-place heap candidate passed state tests but measured only 1.0194x
on one index, below the predeclared 1.02x engineering gate. It is not included in
this branch. That result does not rule out benefits on other datasets.

## Completed 1M review run

The fixed b327ec35 production snapshot completed all 6,976 queries over three
repeats: Recall@50 0.9957167431, QPS 17.4717273, IT 747.4427805 s,
IS 1.746098882 GB including required CSR. K=8 and B=51385. Same-index retained
payload falls from 3,461,449,477 to 1,746,098,882 bytes; buckets remain 617,067,852
bytes. This is not an RSS or build-peak reduction claim.

All GT, ranked outputs, query split and non-timing fields of 20,928 records match
the earlier full 1M run. The raw bundle is
[review_1m_20261022](../results/engineering/review_1m_20261022/).
Its source identity is attested by every measured source/pipeline content hash.
The Windows-created worktree's absolute gitdir was unavailable to WSL Git;
the original null Git capture is explicitly preserved rather than rewritten.

A separate reconstruction of the selected final index matches all sampled
ranked results and counters. Its 16-query instrumented profile attributes about
68.6% to verification, 22.2% to pool updates and 8.3% to collision scans;
dot-only's 54.5% is nested inside verification, not additional time.
These subset observations are not full-workload QPS or significance estimates.
See [CALIBRATION_AUDIT.md](CALIBRATION_AUDIT.md) and the saved diagnostic records.
