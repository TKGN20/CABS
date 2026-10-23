# Paper-to-implementation correspondence

This is a source-level contract for the public CABS research implementation.
It is not a certification of journal acceptance or of every paper experiment.
The public tasks are SPLADE-0.1M and SPLADE-1M. They execute construction and
search rather than loading the checked-in answers.

| Paper mechanism | Source | Executable evidence |
| --- | --- | --- |
| IV-A: observed-dimension IDF weights, one-slot reservation and largest remainders | `sketch/adaptive_sos.hpp`, `Expansion::allocate` | `semantic`: fixed examples, unseen dimensions, exact total capacity |
| IV-A: corpus scaling, query scaling and SOS activation | `Expansion::encode`, `Index::rebuild` | `semantic`, production-path CSR test; query and document seeds use separate domains |
| IV-B: spherical clustering, primary assignment and one eligible secondary membership | `clustering/spherical_partition.hpp` | `mechanism_contract`: explicit three-cluster fixture, primary/secondary membership and frozen-radius eligibility |
| IV-C: positive local similarity floors, global fallback and local hash counts | `Index::configure_floors`, `hash_count` | `mechanism_contract`: independent set Jaccard, fallback clusters and minimal collision-count inequality |
| IV-C/D: temporary/final hash separation | `Index::rebuild`, `Table` | `mechanism_contract`: distinct realized seeds and direct-signature bucket reconstruction |
| IV-D: all candidate K, routing/hash/bucket calibration cost | `Index::candidate_cost`, `rebuild` | `candidates.csv`, stage timings and resolved K; no manually forced K |
| IV-D: minimum B admitting every touched positive target at some replay prefix | `Index::calibrate_pool` | `mechanism_contract`: exhaustive capacity search with independently sorted prefix proxies |
| IV-E: routing, maximum proxy across memberships, unique-ID pool and cumulative verification | `search/query_workspace.hpp` | `final_equivalence.hpp` and `mechanism_contract`: per-cluster Pool/C/gamma/suffix, counters and exact ranked results against the reference path |
| IV-E: strict suffix stopping, active delta scan and empty-sketch fallback | query workspace and `Index` maintenance | `semantic`, reference traces, angular bounds and production-path tests |
| IV-D: insert, tombstones, fresh update IDs and refresh | `search/epoch_index.hpp`, `Collection` | `semantic` and `mechanism_contract`: operations after cache release, stale-version filtering and active refreshed buckets |
| VI-A: measurements and provenance | `src/main.cpp`, `reporting.hpp`, `scripts/reproduce.py`, `evaluate.py` | full records, input/source/binary hashes, independent metric recomputation and published-file integrity checks |

`mechanism_contract` fixes three clusters only inside a small unit fixture to
exercise branches deterministically. Public construction still selects K by
evaluating the full candidate set. Its tests do not prove general recall or a
performance advantage of clustering.

## Configuration is part of the method

The current paper IV-D specifies routing, MinHash evaluation and matched-bucket
access as the K calibration objective. The implementation follows that objective.
It is not an end-to-end query-time optimizer: pool updates, original-vector
verification, top-k and cleanup can change which K is fastest in a full search.
Changing this objective would be a method/configuration revision, not a bug fix.
K=1 is an allowed outcome and demonstrates no multi-cluster routing speedup on
that realized index.

B is a pool capacity, not a bound on the cumulative verified set. A document can
be admitted and verified before eviction, so `verified` can exceed B. Calibration
uses touched positive top-k_max targets, all replay prefixes and no suffix stop.
An untouchable zero-overlap target cannot be recovered by enlarging B.

The two published task files use k=50, k_max=100 and four disjoint calibration
queries. These are explicit experimental settings, not a universal prescription.
They must be held fixed in a claim of identical configuration. Changing k_max,
the calibration workload or the seed changes the experiment and can change B,
local hash counts and recall. K itself also depends on measured calibration time.

## Probability and exactness

The paper's probability analysis assumes ideal independent SOS activation and
MinHash randomness. The implementation uses deterministic, seed-separated
pseudorandom streams and collision-free keyed 64-bit slot ranks. This is the
practical sampling/hash model; domain separation alone is not a proof of ideal
independence or exact minwise probabilities. Every run reports this distinction.
The finite-state hash model does not remove any of the candidate-generation
steps in the paper, but its ideal probability statements remain assumptions.

delta_h controls the calibrated sketch-pair collision-failure setting. It does
not directly bound Recall@50. Exact reranking gives exact original-vector scores
for verified documents. It does not make the candidate generator exact. A safe
suffix bound rules out remaining clusters; it cannot recover a document already
missed within a visited cluster. Floating-point bounds are computed conservatively
and tested on the supplied cases, not formally verified for every machine/input.

## Storage and lifecycle

The static task releases per-document SOS construction caches after final tables
are built. Cardinalities, final buckets and original vectors remain. The release
cost is counted in IT and its before/after retained-payload receipt is saved.
The optional cache-retention flag does not change query results on one fixed
index. It changes retained payload, not necessarily RSS or build peak memory.

In-range insertions can create new cached sketches without restoring old ones.
The cache-state guard prevents using that partial cache for low-level table or
calibration reconstruction. Full refresh regenerates sketches and recalibrates.
Refresh removes inactive bucket memberships and releases inactive owned rows;
internal ID slots, metadata and mapped original CSR storage are not compacted.
Fresh IDs are not reused. These retained ID slots matter for actual space bounds.

No index persistence, crash recovery or concurrent-write transaction guarantee
is claimed. The maintenance API and tests implement the specified valid-operation
workflow, not a deployed mutable search service.

See [COMPLEXITY.md](COMPLEXITY.md) for the actual dense workspace, centroid,
bucket-lookup and scoring costs, and [REVIEW_RESPONSE.md](REVIEW_RESPONSE.md)
for the disposition of the independent review.
