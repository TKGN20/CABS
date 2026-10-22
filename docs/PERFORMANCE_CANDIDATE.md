# Performance review candidate

This branch is a review candidate based on stable main d091458. It has not
replaced that release. No new full 1M or server performance is claimed.

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
