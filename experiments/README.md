# Fixed-query experiment driver (separate from PR #4)

This CABS-only driver calls the frozen production Index API. No `include/`,
`src/`, public configuration, or root build files change. It is the next experiment
engineering step after PR #4; it adds no server performance measurements or baseline
adapters. Existing public task commands retain their historical two-way protocol.

```bash
cmake -S experiments -B build-experiment -DCMAKE_BUILD_TYPE=Release
cmake --build build-experiment -j2
ctest --test-dir build-experiment --output-on-failure
python3 experiments/protocol.py plan --queries datasets/SPLADE/queries.dev.csr --out split.json
```

`plan` reserves 128 calibration IDs, 1,000 selection IDs and the remaining final
evaluation IDs. The explicit IDs, input checksum and split seed are saved before
measurement. `--nc` defaults to four; the calibration seed samples only from the
reservoir. Index seed controls only the production Index options. All outputs
use original CSR row IDs; queries are never silently renumbered.

Create `candidate.json` with these keys (no additional keys are accepted):

```json
{"ell":24,"delta_h":0.2,"kmax":100,"k":50,"threads":4,"iterations":10}
```

```bash
python3 experiments/protocol.py run --binary build-experiment/cabs_fixed_queries --data datasets/SPLADE/base_small.csr --queries datasets/SPLADE/queries.dev.csr --plan split.json --config candidate.json --role selection --index-seed 20261017 --calibration-seed 20261007 --out validation-default
python3 experiments/protocol.py lock --runs validation-default --target 0.95 --out selected.json
python3 experiments/protocol.py run --binary build-experiment/cabs_fixed_queries --data datasets/SPLADE/base_small.csr --queries datasets/SPLADE/queries.dev.csr --plan split.json --config candidate.json --role evaluation --lock selected.json --index-seed 20261018 --calibration-seed 20261007 --out evaluation-1
python3 experiments/protocol.py report --lock selected.json --runs evaluation-1
```

Run every predeclared candidate configuration on selection queries before `lock`.
The lock chooses the fastest eligible recorded selection point in `[r,r+0.002]`;
no eligible point creates an unmatched lock and evaluation is refused. It binds
configuration, inputs, plan, calibration seed, source and binary hashes. Final
evaluation requires the lock and rejects a changed binding. A different index
seed is permitted for independent builds; calibration IDs remain fixed. Locks and
run directories cannot be overwritten through these commands.

`report` requires distinct index seeds and reports each build plus mean Recall;
if final mean Recall falls outside the band it reports unmatched. It explicitly
states whether at least three independent builds are present. A one-build test is
not enough for the final server protocol. The full comparison still requires
baseline adapters, predeclared grids, server setup, multiple builds and independent
audit. The example does not promise that its single candidate matches 0.95.

Run records include all measured IDs, exact GT, ranked IDs/scores, per-query times,
K/B/local m, IT and IS. Query timing includes encoding through exact reranking,
and excludes GT/IO/warm-up. IT includes loading, document frequencies, calibration
GT, all automatic candidate builds and cache release. IS includes required CSR
and retained payload; it is not RSS. Final GT is computed only for the requested
role after configuration checks. Loading the original query CSR validates all
rows but does not use final retrieval results to select a configuration.

The tiny test's broad `[0,1]` band is only a protocol fixture; server runs use the
registered 0.002 tolerance. Tests check original-ID exact GT against independent
Python scoring, seed isolation, leakage rejection, final locks and tamper checks.
Use `-DCABS_SANITIZE=ON` in a Debug experiment build for ASan/UBSan. Hashes provide
traceability, not protection against a user deliberately rewriting code/manifests
or inspecting held-out results outside the runner.
