# Baseline cost accounting — source audit, not accepted IS measurements

Scope: pinned sources and current experimental adapters. This audit does not
provide missing index-space numbers or certify fair performance. The public
metric remains **index space (IS)**, using the existing VI-A/server-protocol
definition: retained index payload plus required original-vector storage;
report peak RSS and serialized bytes separately. Count shared storage once and
actual duplicate retained storage separately. Exclude query scratch and released
construction temporaries. Capacity, object-header and metadata rules must match
the current CABS inventory before comparing methods.

## Findings from the pinned source

| Method | Observed retained structures / interface | Required acceptance work |
| --- | --- | --- |
| CABS | `retained_index_bytes()` includes explicit headers, allocated capacities, encoding metadata, cardinalities, collection metadata, partition, MinHash tables and retained calibration buffers; fixed driver adds original CSR bytes and releases the sketch cache | Export the component inventory in the common benchmark record, retain the cache-release proof and check that current static-only restrictions hold |
| SOSIA | Original CSR, expanded SOS vectors, hash permutations, per-vector hash values, norm/cardinality ordering and nested bucket vectors remain alive in the current adapter | Add a reviewable read-only component inventory; include retained SOS/hash buffers rather than assuming they have been released; check nested capacities/headers |
| SINDI | `GetMemoryUsage()` calls `cal_serialize_size()`, which returns zero at the pin. Native inverted lists own IDs, values and per-window offsets; `use_reorder=true` also retains copied forward vectors. The adapter retains original CSR for the common exact tail | Do not report the zero API value as IS. Inventory native structures and adapter originals, including real duplicate vector storage and native metadata; use serialization as a separate cross-check only |
| SEISMIC | Native `SpaceUsage` sums forward data, posting lists and optional kNN data. Posting lists count packed blocks, offsets and summaries. The adapter also retains original document-offset mapping | Audit `SpaceUsage` conventions and vectorium dependencies against CABS capacities/header policy; do not add original vectors again when already included in the forward store; include the retained offset mapping |

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
