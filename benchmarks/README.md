# Paired scoring benchmark

This optional benchmark builds one full CABS index, checks 200 non-calibration
queries against the previous scoring workspace, and alternates the two complete
query implementations over four timing repeats. Both use the same realized
index, routing, candidate pool and query IDs. Returned IDs, double scores,
semantic counters and complete proxy/verified candidate states are checked.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCABS_SANITIZE=OFF
cmake --build build --target cabs_scoring_pair -j4
mkdir -p results/scoring-check
build/cabs_scoring_pair datasets/SPLADE/base_small.csr datasets/SPLADE/queries.dev.csr results/scoring-check/paired.csv
```

The final argument is the per-query output CSV. The summary is printed as JSON.
`reference_query_workspace.hpp` preserves the previous float-lookup scoring
workspace for this comparison; production search uses the main include tree.
It is not a separate retrieval algorithm or the entry used by default tasks.

The measured [engineering record](../results/engineering/scoring_20261021/)
is a 200-query paired diagnostic, not a full-query default-result table or an
independent-build variance experiment. Automatic K depends on measured
calibration cost; record its actual value and B for each execution.
