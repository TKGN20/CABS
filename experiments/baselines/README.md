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
```

The native project references a floating ThreadPool archive whose current bytes
fail its original MD5. The builder substitutes a commit-pinned official archive
with a registered SHA-256. `-Ofast` is replaced with `-O3`; the reviewed diff,
configure/build logs and compile command hash are retained. No native search
algorithm is changed by this build helper. Successful compilation alone is not
ID/score/IS acceptance. In particular, upstream hard-codes an AVX-512 flag and
its `GetMemoryUsage()` returns zero; host ISA suitability and retained-space
accounting remain explicit audit items before real comparisons.

The CABS repository license does not replace upstream licenses or attribution.
SINDI/VSAG preserves its Apache notices; SEISMIC's upstream citation requirements
also remain applicable. Downloaded third-party sources and dependency caches
stay outside tracked source files.
