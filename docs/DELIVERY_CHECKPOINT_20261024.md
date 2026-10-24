# Issue #6 delivery checkpoint — 2026-10-24

Decision: **NOT FINAL PASS; not submission-ready**. No target-server experiment
has run. PR #4 and its 19 production/default files remain frozen; this work is
isolated in draft PR #5. The [acceptance matrix](../DELIVERY_STATUS.md) remains
authoritative.

## Executed work

| Item | Evidence | Limit |
| --- | --- | --- |
| SINDI GT correction | Same native binaries give aggregate 0.971667 after fixing our diagnostic query type; all 600 GT items agree with independent C++ scoring | Earlier 0.343333 was invalid GT, not native quality evidence |
| Original SINDI candidate failure | Public raw trace shows 140/82/76/33 unique candidates at gamma=160; last query cannot provide top-50 | Preserved failed diagnostic; no fallback |
| Disclosed repaired SINDI | Fresh native build; candidate adapter passes original IDs, exact scores, 160-candidate counts, ties, allocator controls, non-reranking bounds and error cases | Synthetic fixture only; not unmodified official baseline or performance acceptance |
| Independent GT producer | Unpruned standard-C++ reference agrees with independent Python scoring on random, rounding, tie/zero/empty cases | No full-corpus run |
| Role-isolated GT generation | Input/source/binary hashes, explicit original IDs, independent calibration seed, locked evaluation and no-overwrite tests | Synthetic lock tests; common four-method selection still incomplete |
| Saved evidence | 19 receipt hashes; five fixture bundles recomputed; correct/invalid native GT classifications and original failed trace checked | Saved-record audit is not an independent full-run replication |

The [SINDI audit](SINDI_INTEGRATION_AUDIT_20261023.md) details the integration
erratum, separate native failure and optional registered source patch. All old
records remain available. Patches and source/binary receipts disclose precisely
which native version produced the repaired fixture.

## Reproduction and review

```bash
python3 experiments/delivery/verify_receipts.py
cmake -S experiments -B build-experiment -DCMAKE_BUILD_TYPE=Release
cmake --build build-experiment --parallel 2
ctest --test-dir build-experiment --output-on-failure
python3 scripts/verify_artifacts.py --source-check
```

The experimental suite contains five tests. Release and Debug ASan/UBSan runs
pass locally; native baseline instrumentation is separate. Full adapter rebuild
commands are in the [integration notes](../experiments/baselines/README.md).
The portable SINDI build removes forced global AVX-512 flags while retaining
native per-ISA dispatch; clean-build verification is pending at this checkpoint.

The historical source/record audit passed under WSL Python 3.10: 185 artifact
members, 164 canonical source files and all 19 frozen production/default files.
Windows Python 3.14 failed the historical pilot's strict dictionary equality:
recomputed mean Recall differed by only 1.1e-16 to 8.9e-16 because its floating
reduction differs. No saved metrics were changed. Use the recorded Linux/Python
environment for that exact historical audit; cross-version reduction portability
remains a tooling item, separate from algorithm or measured-performance claims.

## Remaining submission gates

1. Audit retained index space, timing/thread/precision policies and all third-party
   dependency/build provenance. Extend native/repaired controls on real inputs.
2. Register sourced parameter grids, integrate all methods with validation-only
   selection and immutable final locks, and verify matched/unmatched accounting.
3. When the owner provides server resources, run the registered pilot, then the
   two-scale four-method matrix with at least three builds and three timings.
4. Complete crossed seeds, routing/cost analysis, the full manuscript workload and
   ablation matrix, and independent clean-checkout verification.
5. Identify the authoritative manuscript and reconcile every claim/table/figure
   with actual evidence before the journal-format/final submission audit.

The historical local SPLADE-1M measurement remains 17.47 QPS. Synthetic fixture
throughput is not substituted for this measurement or the paper's draft targets.
No merge approval or independent GitHub review is implied by this checkpoint.
