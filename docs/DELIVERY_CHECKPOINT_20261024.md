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
| Common result audit | Independent role-bound GT audits SOSIA/SEISMIC/repaired SINDI saved outputs and a fresh CABS run; wrong scores, missing timings and wrong method/lock rejected | Small synthetic records; not common parameter selection or fair performance acceptance |
| Saved evidence | 21 receipt hashes; eight synthetic fixture bundles recomputed; five real-smoke method/control records checked against saved independent GT; original failures preserved | Real-vector exact scoring requires original data; saved-record audit is not an independent full-run replication |

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

The experimental suite contains six tests. Release and Debug ASan/UBSan runs
pass locally; native baseline instrumentation is separate. Full adapter rebuild
commands are in the [integration notes](../experiments/baselines/README.md).
The portable SINDI build removes forced global AVX-512 flags while retaining
native per-ISA dispatch. Its [actual clean-build CI](https://github.com/TKGN20/CABS/actions/runs/40071609451)
passed at commit `24e03bfc06d519cdbae2177ab10f306ea79d7f05`. The downloaded artifact
archive matches GitHub's SHA-256; source diff, build logs, receipts, fixture inputs
and raw ranked outputs are preserved in the repository. Later common-audit CI at
`44db1055f2c97486be07b53012215f09425b3343` passes production, six experimental
tests, SOSIA and SEISMIC; its repeated SINDI build is separately tracked.

## Local real-data smoke

One predeclared configuration per method was run against SPLADE-0.1M, using four
selection query IDs `966, 5380, 1447, 6577`, a separately generated unpruned exact
GT, one search thread and three timing repeats. CABS uses four disjoint calibration
IDs, its original automatic K/m/B rules and default ell=24, delta_h=0.2. Each
method has a 600-second timeout and an 8-GiB address-space limit.

| Method | Recall@50 on four queries | Record audit |
| --- | ---: | --- |
| CABS | 0.965 | PASS |
| SOSIA, ell=4/m=16/budget=1000 | 0.265 | PASS |
| Disclosed repaired SINDI, alpha=beta=1/gamma=1000 | 1.000 | PASS |
| SEISMIC, postings=1000/centroid fraction=0.1/energy=1/query cut=20/heap factor=0.7 | 1.000 | PASS |
| Original SINDI control, same configuration as repaired version | 1.000 | PASS |

PASS means original IDs, original-vector binary64 scores, ranked top-k and timing
records passed the independent audit. It does not mean target recall or fair
performance was accepted. The SOSIA smoke configuration is deliberately small and
is not asserted to be its native default, a tuned configuration or its performance
frontier. Both SINDI versions expose 1000 unique candidates for each real query;
the synthetic boundary failure is not generalized to these real queries.

CABS selected K=1, B=13798 and m=167 in this realization. This does not establish
that spherical routing helps this particular run. Query/candidate diagnostics,
calibration stability and representative-query evaluation remain required.
Raw timings, build costs and resources are retained, but no speedup ratio is
computed from these unmatched four-query smoke points. No final evaluation query
was used for this diagnostic. Public records are under
`experiments/delivery/real_smoke/splade_0.1m_20261024/`.

The historical source/record audit passed under WSL Python 3.10: 185 artifact
members, 164 canonical source files and all 19 frozen production/default files.
Windows Python 3.14 failed the historical pilot's strict dictionary equality:
recomputed mean Recall differed by only 1.1e-16 to 8.9e-16 because its floating
reduction differs. No saved metrics were changed. Use the recorded Linux/Python
environment for that exact historical audit; cross-version reduction portability
remains a tooling item, separate from algorithm or measured-performance claims.

## Remaining submission gates

The later [SOSIA space checkpoint](SOSIA_SPACE_AUDIT_20261024.md) adds a checked
16-component inventory, unchanged ranked-output controls and one real-data
component sum. The later [three-baseline checkpoint](BASELINE_SPACE_AUDIT_20261024.md)
adds SINDI/SEISMIC component and serialize/reload checks. Saved verification now
covers 33 receipts, twenty synthetic bundles, five real method/control records
and three real component inventories. It also preserves the original SOSIA zero-query sanitizer failure.
Common IS and four-method selection remain incomplete.

1. Audit retained index space, timing/thread/precision policies and all third-party
   dependency/build provenance. Extend native/repaired controls on real inputs.
   The [cost source audit](BASELINE_COST_AUDIT_20261024.md) records the zero SINDI
   memory stub, native forward-store accounting and remaining inventory work.
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
