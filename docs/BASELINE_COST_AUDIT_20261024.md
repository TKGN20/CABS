# Baseline cost accounting — component records, acceptance still open

Scope: pinned sources and current experimental adapters. Executed component
records and final fair-performance acceptance are distinct. The public
metric remains **index space (IS)**, using the existing VI-A/server-protocol
definition: retained index payload plus required original-vector storage;
report peak RSS and serialized bytes separately. Count shared storage once and
actual duplicate retained storage separately. Exclude query scratch and released
construction temporaries. Capacity, object-header and metadata rules must match
the current CABS inventory before comparing methods.

## Findings from the pinned source

| Method | Observed retained structures / interface | Required acceptance work |
| --- | --- | --- |
| CABS | Experimental exporter now splits 29 components, preserves the frozen nine-part aggregate, adds full stage-map node bookkeeping and verifies release of the sketch cache and temporary calibration input | Bounded and one-real-control checks passed; complete timer/warmup fairness and broader acceptance |
| SOSIA | Original CSR, expanded SOS vectors, hash permutations, per-vector hash values, norm/cardinality ordering and nested bucket vectors remain alive; 16-component inventory now emitted and checked | Component/ABI/capacity tests and one real smoke passed; finalize common policy and broader acceptance; see the SOSIA space audit |
| SINDI | Zero `GetMemoryUsage()` stub preserved. Registered read-only `GetStats()` now inventories lists, windows, native forward copies and retained mutexes; adapter originals/views counted separately | Component and independent serialization/reload checks plus one real control passed; complete common header/capacity and broader acceptance |
| SEISMIC | Registered read-only traversal adds retained object headers, boxed payloads and compressed-structure capacity accounting; native `SpaceUsage` remains separately reported; forward vectors counted once and adapter offset map added | Component and serialization/reload checks plus one real control passed; finalize common forward-store/header convention and broader acceptance |

Source locations are in the registered pins: SOSIA `src/method.h` and
`src/sosia.h`; SINDI `src/index/sindi.h` / `sindi.cpp`; SEISMIC
`src/inverted_index.rs` / `posting_list.rs`. Full archives and source hashes are
in the fetch receipts. A zero stub, process RSS and an index file size are three
different quantities; none replaces a component inventory.

## Timing boundary

Current adapters include document/query loading and native index construction in
`build_seconds_including_loading`. CABS additionally includes required calibration
GT, automatic configuration and cache release in IT. Independent final-evaluation
GT and result auditing are utility work outside retrieval timing. Common exact
binary64 candidate scoring remains inside each query timer; native float pruning
and scoring remain active in the disclosed baseline adapters.

Before server acceptance, record and compare build-thread budgets, native thread
pools, warmup and query-allocation costs. Current native/candidate diagnostics are
outside the query timer; any persistent diagnostic allocation or retained buffer
must still be disclosed. The common record auditor checks reported IDs, scores,
recall and raw timing arithmetic. It does not prove a timer excludes no work or
that build/runtime dependencies are equivalent.

## Required evidence to close this gate

1. Per-method runtime component JSON, with exact summation and shared/duplicate
   storage explanation. Preserve original IS definitions and raw byte counts.
2. Small-fixture independent count checks, build/serialize/reload controls where
   supported, and explicit unsupported cases rather than guessed memory numbers.
3. Build and query resource receipts, method-specific seed semantics, identical
   registered timing policy and actual target-server execution.

Status: **OPEN**. No new IS values have been inserted into the manuscript or
historical tables by this source audit.

Continuation: [SOSIA component accounting](SOSIA_SPACE_AUDIT_20261024.md) and
[three-baseline component checkpoint](BASELINE_SPACE_AUDIT_20261024.md) provide
executed fixture and one-real-configuration evidence for each baseline. Missing
baseline exporters are now implemented; the common four-method IS gate is open.
The [2026-10-25 common cost checkpoint](COMMON_COST_CHECKPOINT_20261025.md) adds
CABS's exporter, legacy reconciliation and twelve unified cost records. Timer,
warmup, broader provenance and target-server acceptance remain outstanding.
