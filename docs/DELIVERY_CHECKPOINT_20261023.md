# Issue #6 local engineering checkpoint — 2026-10-23

Decision: **NOT FINAL PASS; not submission-ready**. The owner confirmed that the
target server is not ready. No target-server run, server speedup or paper table
has been produced in this checkpoint. The acceptance matrix remains authoritative
in [DELIVERY_STATUS.md](../DELIVERY_STATUS.md).

## Verified work

| Item | Executed evidence | Boundary |
| --- | --- | --- |
| Production/default freeze | 19-file baseline check and historical source/artifact audit | Does not establish paper performance |
| Local inputs | Two database scales plus original 6,980-query CSR, header/size/SHA-256 checked | No server hardware equivalence |
| Independent oracle | Nine adversarial tests; original IDs, exact binary64 scores, ties, missing/duplicate rows and timings | Small synthetic fixture scope |
| SOSIA | Pinned official source; Release and ASan/UBSan builds and fixture checks | Seed/scoring adaptation disclosed; IS and common protocol pending |
| SEISMIC | Pinned v0.5.4 native CLIs and candidate adapter built; original-ID/score/native-candidate fixture passed | Not asserted to be original paper release or native quantized-default parity |
| SINDI | Pinned native build and synthetic build/serialize/reload/search smoke completed | Native CLI prints low synthetic recall; candidate/ID/score audit required, not accepted as common baseline |
| Dependency reproducibility | Three baseline archive SHA-256 values; pinned ThreadPool; dated local Rust toolchain; registered Cargo.lock | SINDI transitive dependency inventory not fully accepted |
| Raw local evidence | SOSIA and SEISMIC synthetic CSR, ranked outputs and per-query times retained; both re-audited from saved files | Receipt recomputation is not an independent full-run replication |

SEISMIC's build initially required an isolated newer Rust toolchain; global
toolchains and shell profiles were not changed. Its native search remains
available. The candidate entry exposes the visited offset set and retains native
float pruning/scoring. A timed binary64 tail reranks all exposed candidates using
original float32 values. Three construction RNG sites accept an explicit index
seed. These are reviewable experimental adaptations, not undisclosed replacement
algorithms. The per-build diff, driver and binary hashes are recorded.

The SINDI build helper repairs a floating ThreadPool archive/checksum failure by
using a concrete official commit. Native compilation and a synthetic native CLI
smoke run completed. The host exposes AVX-512F, but the upstream hard-coded ISA
flag remains a portability audit item. The native CLI reports Recall 0.343333 on
this synthetic smoke, despite alpha=1, beta=1 and gamma=160 for 160 documents.
That aggregate CLI output is not an independent returned-ID audit and is not a
paper result. Preserve it and investigate candidates, duplicates, underfill and
native score ordering before accepting an adapter. The zero-returning memory
API also remains an audit item; neither compilation nor serialized file size is
accepted as proof of fair runtime or IS accounting.

## Still required before submission readiness

1. Finish SINDI adapter and independent parity/error fixtures. Complete retained
   IS inventories, timing boundaries, build/thread/ISA and precision policies for
   all methods; disclose any native-default deviations.
2. Register sourced parameter grids and integrate every adapter with disjoint
   calibration/selection/evaluation IDs, independent seeds and immutable
   validation-only locks. No final query may be used to select parameters.
3. With actual authorized server resources, run the pilot and the two-scale,
   four-method matrix with at least three independent builds and three timing
   repetitions. Preserve unsuccessful and unmatched results.
4. Run the crossed-seed and routing/cost diagnostics, then the manuscript's full
   workload and ablation coverage. Reconcile each claimed number and figure with
   raw evidence. Historical engineering values are not relabeled measurements.
5. Identify the authoritative manuscript project, complete its evidence map and
   journal-format audit, and obtain independent clean-checkout verification.

PR #4 remains frozen, public and unmerged. Experimental work continues on draft
PR #5; neither this checkpoint nor green CI grants merge approval. The historical
local SPLADE-1M result (17.47 QPS) remains a local measurement and is not replaced
by synthetic fixture throughput or a hardware scaling estimate.

## Recompute this checkpoint

```bash
python3 experiments/delivery/verify_receipts.py
python3 experiments/baselines/test_oracle.py
```

Source/build commands and their limits are in
[baseline integration notes](../experiments/baselines/README.md).
Machine-readable receipts are under `experiments/delivery/receipts/`; synthetic
raw data and records are under `experiments/delivery/fixtures/`. Rebuilding an
adapter is a separate operation from replaying its saved record audit.

At `59c3e3f0b72e74eca56bbbc4a7eccef5195799bd`, the four separate GitHub workflows
passed: [production/record audit](https://github.com/TKGN20/CABS/actions/runs/39940077958),
[fixed-query protocol](https://github.com/TKGN20/CABS/actions/runs/39940078136),
[SOSIA Release and sanitizer fixtures](https://github.com/TKGN20/CABS/actions/runs/39940078028),
and [SEISMIC clean-build fixture](https://github.com/TKGN20/CABS/actions/runs/39940077973).
The later SINDI native-smoke records are an additional bounded checkpoint and do
not imply SINDI candidate acceptance. Their original artifacts are preserved in
`experiments/delivery/native_smoke/sindi/`; the receipt checker verifies their
hashes, not the correctness of native ranked outputs that the CLI did not export.
