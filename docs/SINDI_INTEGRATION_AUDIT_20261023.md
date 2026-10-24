# SINDI integration audit and GT erratum

Status: **bounded diagnostics; not baseline acceptance or paper performance**.
The frozen CABS implementation/defaults and all historical measurement bundles
are unchanged. SINDI upstream remains pinned to
`93e034f920334ea9e0d92784bb1d8964fb7322fa`.

## Correction to the previous checkpoint

The first SINDI smoke reported Recall 0.343333 against an invalid generated GT.
`test_sindi_native.py` passed a list of coordinate/value pairs to `score()`, which
expects a coordinate-to-weight mapping. The membership checks silently failed,
all scores became zero, and GT selected the first 50 IDs. This was an integration
script defect, not evidence of SINDI quality.

The script now explicitly constructs the mapping; the scorer rejects the wrong
type and a regression test checks that rejection. The same unchanged native
executables/library report Recall 0.971667 against corrected GT. All 600 GT items
match an independent C++ exact sparse-inverted reference, which uses every
original float32 value with ascending-coordinate binary64 accumulation and no
candidate pruning. The C++ reference also matches direct Python scoring on random,
tie, empty/zero-row and rounding fixtures.

Old artifacts were retained in `experiments/delivery/native_smoke/sindi/` and are
now explicitly classified `SUPERSEDED_INVALID_GT_DIAGNOSTIC`. Corrected evidence
is in `experiments/delivery/native_smoke/sindi_corrected_gt/`. The saved-record
checker proves that the former GT is invalid and the latter GT is valid; checksum
success alone is not presented as GT correctness.

## Separate native candidate underfill

A public-API diagnostic requests `KnnSearch(k=gamma)` with gamma held fixed. In
the pinned source, k affects the final float reranking capacity and the clamp
`gamma=max(gamma,k)`; k=gamma therefore exposes the same admitted gamma pool.
An initialized diagnostic allocator makes unwritten result slots explicit. The
original native top-50 results are separately recorded and checked for containment
in that pool. No exact scan fills missing candidates.

For the same 160-document fixture, alpha=1, beta=1 and gamma=160:

| Original query ID | Exposed unique candidates | Unwritten slots | Duplicate slots |
| --- | ---: | ---: | ---: |
| 9 | 140 | 20 | 0 |
| 2 | 82 | 78 | 0 |
| 11 | 76 | 84 | 0 |
| 0 | 33 | 127 | 0 |

The fourth query cannot provide 50 valid results from 33 candidates. The original
native output buffers are allocated without initialization, so the native CLI
can read unwritten result slots. The diagnostic correctly exits with an underfill
error; it is a preserved failed experiment, not an accepted benchmark.
Raw counts, candidate IDs, original native results, the exact compiled diagnostic
source and build receipt are under
`experiments/delivery/diagnostics/sindi_unpatched/`.

## Disclosed native repairs — fixture passed on 2026-10-24

`experiments/baselines/sindi_correctness.patch` is optional and separate from the
official source pin. Before/after hashes of both affected files are registered in
`sindi_correctness.json`. A fresh native build is required with
`build_sindi.py --correctness-repairs`; the original binaries/library are retained.

The patch delays heap-threshold filtering until gamma entries are present,
rejects zero/reset accumulator entries that would reinsert candidates, initializes
unused return slots to -1/NaN, respects the declared trailing dimension, caps the
non-reranking output branch at k, checks padded-window IDs, and releases the native
forward-array allocation. It retains document/query pruning, segmented SIMD
accumulation, gamma admission and native float reranking. It is not labeled an
unmodified official release or silently folded into paper results.

The fresh repaired build and public-API adapter passed the bounded fixture. Each
of the four selected queries exposes 160 unique candidates with no duplicate or
unwritten slots. Original-ID rankings and binary64 scores match the independent
oracle (Recall@50 = 1.0). Native float scores agree within the declared tolerance.
Ordinary allocation and both initialized diagnostic allocation patterns give the
same candidates/rankings. Separate tests pass the original-ID tie rule,
gamma greater than k in the non-reranking branch, padded-window bounds, rejection
of insufficient candidates without exact fallback, invalid query IDs and declared
trailing dimensions. No SINDI performance claim follows from this synthetic test.

The original and repaired native libraries remain separate. The exact compiled
adapter source hash and native-library hash are recorded. Public primary, tie and
non-reranking raw records are in `experiments/delivery/fixtures/sindi_repaired*/`.
The receipt checker independently recomputes their GT/rankings and checks native
scores, complete-pool counts and original native result containment. The build
and test receipts are `sindi-repaired_native_build.json`,
`sindi-repaired_adapter_build.json` and `sindi-repaired_fixture_parity.json` under
`experiments/delivery/receipts/`.

Retained IS, fair timing, dependency/ISA portability, broader native/repaired
controls and common selection/evaluation locks still require acceptance. The
local build retains upstream global AVX-512 flags on a supported host. The
separate `--portable-isa` build option removes those global flags while retaining
the original per-ISA objects and runtime dispatch; its clean-build verification
is not inferred from the local fixture. Full-native sanitizer coverage is also
pending. Frozen CABS production sources and defaults remain unchanged.
