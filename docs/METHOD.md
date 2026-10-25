# Implementation contract

| Component | Implementation |
| --- | --- |
| Fixed expansion capacity | `sketch/adaptive_sos.hpp`: each dimension gets at least one slot; largest-remainder allocation sums exactly to `d*ell`. Capacity is not activated sketch cardinality. |
| IDF-aware SOS | Observed document frequencies determine allocation; document/query encodings share slots and use separated seed domains. |
| Spherical partitions | `clustering/spherical_partition.hpp`: final centroid assignment, primary memberships, at most one eligible secondary membership using frozen construction radius. |
| Automatic parameters | `search/epoch_index.hpp`: all candidate granularities are evaluated, local positive calibration floors determine uncapped `m_i`, and prefix ranks determine global `B`. |
| Temporary/final separation | Calibration and final MinHash functions use distinct seed domains. |
| Search | `search/query_workspace.hpp`: cosine routing, a global unique-ID candidate pool, cumulative exact verification, original inner-product reranking and deterministic ID ties. |
| Stopping | Current-inclusive suffix bounds; strict upper-bound comparison. Previously missed candidates are not recovered by this rule. |
| Maintenance API | Tombstones, fresh internal IDs, delta handling and full refresh are retained; concurrent/crash-consistent service operation is not implemented. |

The MinHash implementation uses collision-free keyed pseudorandom slot ranks.
Reproducible seed separation does not prove exact minwise independence or the
ideal independent random assumptions used in probability analysis. The code
reports its practical hash model explicitly in each run.

Construction timers are wall-clock intervals; overlapping stage labels must not
be added together. Single-threaded QPS includes exact candidate scoring but not
validation/logging. Retained vector capacities are inventoried separately from
original CSR bytes and process peak RSS. No on-disk index persistence is claimed:
the reproduction commands build a fresh in-memory index and persist its resolved
configuration and measurements.

The full source/test mapping is in PAPER_IMPLEMENTATION.md. COMPLEXITY.md records
the actual dense workspace/centroid, binary bucket lookup and document-scanning
scorer costs. CALIBRATION_AUDIT.md describes separate diagnostics that leave the
paper's default calibration objective unchanged.
