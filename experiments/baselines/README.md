# Baseline integration for Issue #6

This directory is work in progress, not a four-method benchmark acceptance.
The [delivery matrix](../../DELIVERY_STATUS.md) controls the completion decision.

## Version and source receipt

`sources.json` records concrete candidates. Fetches are anonymous, commit-pinned
official archives; neither installed binaries nor old working directories are
accepted as upstream provenance. Full per-file SHA-256 receipts are produced next
to each archive. Do not vendor upstream datasets, binary executables or papers
into this repository; their presence in an upstream archive does not make them
experiment inputs. Each baseline retains its upstream terms and attribution.

```bash
python3 experiments/baselines/fetch_upstreams.py --out build-mission/upstreams
python3 experiments/baselines/test_oracle.py
python3 experiments/baselines/build_sosia.py --upstreams build-mission/upstreams --out build-mission/sosia-release
python3 experiments/baselines/test_sosia.py --binary build-mission/sosia-release/sosia_fixed --out build-mission/sosia-release/parity.json
```

Use fresh build/output directories. Build with `--sanitize`, then run the same
test with `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1` and
`UBSAN_OPTIONS=halt_on_error=1` for memory/undefined-behavior checks.

## Common small-fixture oracle

`oracle.py` imports no CABS scorer or reported ground truth. It independently
reads float32 CSR values, accumulates sparse inner products in ascending-coordinate
binary64 order, enumerates fixture GT, verifies original IDs, ranks and ties, and
recomputes Recall and pooled timing. Inputs above the fixed small-fixture operation
limit are refused; full GT needs a separately authorized scalable oracle.

Adapters emit `retrieved.csv` (`query_id,rank,identifier,score`) with original IDs
and `queries.csv` (`repeat,query_id,seconds`, optional checked `recall`). Repeats
start at zero. Missing/truncated output, duplicate or out-of-range IDs, incorrect
scores/ties, missing timing and nonfinite values are failures, not repaired output.

## SOSIA candidate adapter — bounded validation

The pinned upstream README identifies its ICDE 2024 SOSIA implementation. The
source has a shared RNG inside parallel transformation and uses time-dependent
hash shuffles. The reviewable `sosia_runtime.patch` replaces these with seeded,
domain-separated deterministic streams, suppresses automatic benchmark creation,
fixes temporary allocation lifecycle and uses actual compiler ISA macros. The
SOS/minhash/proxy heap, one-index structure and native budget cap remain. This is
a disclosed reproducible adaptation, not byte-identical execution of an arbitrary
historical random realization. Patch/source/binary hashes and flags are recorded.

The added `candidateSearch` entry exposes all candidates admitted by the upstream
heap before native float scoring/top-k truncation. The original `knnSearch2`
entry remains available; untimed checks require equal candidate costs and native
returned IDs contained in the exposed candidate set. These checks do not prove
all-input equivalence or a paper-performance replication.

The adapter scores every exposed candidate using original float32 values with
binary64 accumulation and ranks by descending score / ascending original ID.
Query allocation, SOS, hashing, heap handling, this exact reranking and cleanup
are inside the timer. It avoids rescoring just the native top-k, which could
conceal candidate-ranking differences. No exact fallback fills underfull output.
Both the common-score adaptation and native candidate policy must be disclosed
in any eventual comparison; baseline matching is not accepted from fixture tests.

`-mavx` is currently required by upstream code; fast-math and `-march=native` are
not used. Build/query execution is single threaded in the current adapter.
The independent fixture checks k=50 at nonconsecutive original query IDs and
same-seed determinism. Small-fixture Recall is not a throughput result.

**Not complete:** retained IS inventory is explicitly `NOT_YET_VALIDATED`; common
three-way lock integration, preregistered grids, broader parity tests and real
validation are pending. No complete four-method comparison is accepted yet.
The selected SEISMIC commit is v0.5.4 (Rust 2024/nightly), whereas a local historical
archive was v0.4.0; old patches/build receipts cannot be transplanted as evidence.

All original archives and failed build attempts remain separate. No new server
run is performed: the owner confirmed the server is not ready on 2026-10-23.

## SEISMIC candidate adapter — bounded validation

The v0.5.4 pin requires a newer Rust toolchain than the local system installation.
The helper installs an explicitly dated nightly into a fresh project-local
directory, without changing shell profiles or global toolchains. Upstream has no
Cargo.lock at this pin; the registered lock records the initial dependency
resolution. Subsequent builds use `--locked`. The source archive, lock, toolchain,
patch, adapter and binary have separate hashes in build receipts.

```bash
python3 experiments/baselines/bootstrap_rust.py --nightly-date 2026-10-23 --out build-mission/rust-nightly
python3 experiments/baselines/build_seismic.py --upstreams build-mission/upstreams --toolchain build-mission/rust-nightly --out build-mission/seismic-adapter --adapter
python3 experiments/baselines/test_seismic.py --binary build-mission/seismic-adapter/target/release/cabs_fixed_queries --out build-mission/seismic-adapter/parity.json
```

Without `--adapter`, the builder compiles the original native build/search CLIs.
The adapter uses explicit u32 components and float32 values, not the native CLI's
quantized defaults. A reviewable source diff exposes the visited candidate-offset
set from a duplicate of upstream search. The original search method remains
available. Both paths retain native float pruning/scoring; untimed checks require
identical native ranked outputs and native returned IDs within the candidate set.
Original CSR offsets map candidates back to original document IDs. The timed
tail scores all visited candidates in binary64 and applies the common ID tie rule;
it does not exactly scan the collection to repair underfilled results.

The three native construction RNG sites consume `CABS_BENCH_INDEX_SEED` when set,
otherwise retaining upstream defaults. The adapter requires that value to equal
its explicit seed argument and `RAYON_NUM_THREADS=1`. This is an experimental seed
and scoring adaptation, not evidence of paper-version or native-default parity.
The small fixture verifies IDs, scores, native/candidate agreement, same-seed
repeatability, invalid IDs and seed mismatch rejection. It does not certify
index-space accounting, real-data performance or the common validation lock.

## SINDI native build — dependency repair, acceptance pending

```bash
python3 experiments/baselines/fetch_upstreams.py --method ThreadPool --out build-mission/upstreams
python3 experiments/baselines/build_sindi.py --upstreams build-mission/upstreams --out build-mission/sindi-native
python3 experiments/baselines/test_sindi_native.py --native build-mission/sindi-native --out build-mission/sindi-smoke
```

The native project references a floating ThreadPool archive whose current bytes
fail its original MD5. The builder substitutes a commit-pinned official archive
with a registered SHA-256. `-Ofast` is replaced with `-O3`; the reviewed diff,
configure/build logs and compile command hash are retained. No native search
algorithm is changed by this build helper. Successful compilation alone is not
ID/score/IS acceptance. In particular, upstream hard-codes an AVX-512 flag and
its `GetMemoryUsage()` returns zero; host ISA suitability and retained-space
accounting remain explicit audit items before real comparisons.

The native CLI smoke performs build/serialize/reload/search with independently
generated synthetic GT. It saves native stdout/stderr and exact executable/library
hashes, but the CLI does not export per-query IDs for this test. Its printed
aggregate Recall/QPS is not accepted as independently audited four-method evidence.
On the first local smoke it reported Recall 0.343333 with alpha=1, beta=1,
gamma=160 and 160 documents, but that run used invalid GT due to a query-type
mistake in the integration script. It is retained and explicitly superseded.
Corrected GT crosschecked against the independent C++ reference gives 0.971667
with the unchanged native binaries. A separate trace confirms native candidate
underfill. See the [audit/erratum](../../docs/SINDI_INTEGRATION_AUDIT_20261023.md).
No exact fallback repairs insufficient native candidates.

`build_sindi.py --correctness-repairs` creates a separate native version with the
registered `sindi_correctness.patch`; the two-file before/after hashes are checked.
This is a disclosed repair of heap warmup, reset/zero admissions, output-slot
initialization/bounds, trailing dimension and an allocation lifecycle. It is not
an unmodified official release. The repaired build passed the bounded candidate
fixture on 2026-10-24; IS, common locks and real-data acceptance remain pending.
`build_sindi_adapter.py` links a public-API candidate adapter against a verified
native build without modifying that library. It exposes the gamma pool through
`KnnSearch(k=gamma)` while retaining the same gamma, then reranks all exposed
unique original IDs in binary64. Native float scoring and the exact tail are both
inside timing. Diagnostic allocators make unwritten slots visible; they must
agree with ordinary native allocation before acceptance. The repaired native
runtime initializes its own unused result slots. Candidate shortages fail;
records from the original failed diagnostic remain separate.

```bash
python3 experiments/baselines/build_sindi.py --upstreams build-mission/upstreams --out build-mission/sindi-repaired --correctness-repairs --portable-isa
python3 experiments/baselines/build_sindi_adapter.py --native build-mission/sindi-repaired --upstreams build-mission/upstreams --out build-mission/sindi-adapter
python3 experiments/baselines/test_sindi.py --binary build-mission/sindi-adapter/sindi_fixed --out build-mission/sindi-adapter/parity.json
```

The local accepted fixture used the supported host's original global AVX-512
flags. `--portable-isa` removes only the two unconditional library flags while
retaining upstream per-ISA objects and CPU dispatch. That separate clean build
must pass before portability is accepted. Tests include all 160 candidates,
native scores, ordinary versus two diagnostic allocators, original-ID ties,
non-reranking gamma>k/padded windows, invalid IDs, insufficient candidates and
trailing dimensions. This is not full-native sanitizer or large-data acceptance.

## Independent exact ground truth

```bash
cmake -S experiments -B build-experiment -DCMAKE_BUILD_TYPE=Release
cmake --build build-experiment --parallel 2
ctest --test-dir build-experiment --output-on-failure
mkdir build-mission/exact-reference
build-experiment/independent_exact_gt base.csr queries.csr original_ids.txt 50 build-mission/exact-reference
```

The standard-C++ tool imports no CABS or baseline scorer. It builds complete
postings from original float32 CSR values and visits every shared coordinate with
binary64 accumulation in ascending-coordinate order. There is no term, document,
block or score pruning. Zero-score ties include the smallest original IDs, even
for empty/zero queries. It emits original IDs and 17-digit scores and refuses to
overwrite outputs. Compile with `-ffp-contract=off`, as the experimental CMake does.
The fixture compares every ranked item against independent Python direct scoring
for random, tie/empty/zero and rounding cases. `--reference-binary` on the SINDI
native smoke additionally checks all native-format GT items against this tool.

GT utility time and space are separate from retrieval benchmarks. No large
corpus run has been performed or certified. `prepare_gt.py` binds generation to
input/source/binary hashes and the existing three-way query plan. Calibration
uses its independent seed; selection uses only the plan's selection IDs;
evaluation generation requires a matching validation lock and sidecar hash,
including locked k. It refuses reuse of output directories and checks saved
rank/order/ID/artifact integrity. `test_prepare_gt.py` uses a clearly labeled
synthetic lock to test this guard; it is not real parameter selection.

```bash
python3 experiments/baselines/prepare_gt.py --data base.csr --queries queries.csr --plan plan.json --binary build-experiment/independent_exact_gt --out build-mission/selection-gt --role selection --k 50
python3 experiments/baselines/prepare_gt.py --data base.csr --queries queries.csr --plan plan.json --binary build-experiment/independent_exact_gt --out build-mission/evaluation-gt --role evaluation --k 50 --lock validation-lock.json
```

Both Release and Debug ASan/UBSan experimental builds passed all six CTests
locally. This includes the independent GT and role/lock tests, not instrumentation
of the SINDI native library. Four-method validation drivers, registered grids,
real-data locks and scalable server execution are still required.

## Common record audit and cost boundary

```bash
python3 experiments/baselines/audit_run.py --data base.csr --queries queries.csr --plan plan.json --ground-truth build-mission/selection-gt --results method-run --method SINDI --repeats 3 --out audited-record.json
```

The auditor accepts all four current record formats, verifies role/query-plan
bindings, original IDs, exact scores, ranking, GT recall and complete raw timings,
and refuses changed records or an unrelated final-evaluation lock. Pass `--lock`
for evaluation. It does not select configurations, certify binary provenance,
measure IS or establish fair timer boundaries. The test audits saved baseline
fixtures and executes CABS freshly against independent GT, including corrupted
score/timing/method/lock cases. A record audit is narrower than a fresh baseline
reproduction. The [cost audit](../../docs/BASELINE_COST_AUDIT_20261024.md) identifies
the remaining retained-space and timing work; native SINDI's zero memory API is
not used as a measured IS value.

The CABS repository license does not replace upstream licenses or attribution.
SINDI/VSAG preserves its Apache notices; SEISMIC's upstream citation requirements
also remain applicable. Downloaded third-party sources and dependency caches
stay outside tracked source files.
