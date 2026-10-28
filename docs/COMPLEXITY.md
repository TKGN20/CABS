# Cost of the supplied implementation

This note distinguishes the paper's abstract data structures from the actual
public implementation. It does not change the retrieval algorithm.

Let n_a be the number of allocated internal ID slots, including tombstones,
n_s the number retained in main-index buckets, d the dimension, K the cluster
count, C the cumulative verified set and A_i the admissions list in visited
cluster i. Let b_it be the number of keys in local table t of cluster i.

## Query time

- SOS encoding visits the expanded slots of query support.
- Dense-centroid routing, bounds and sorting cost O(K nnz(q) + K log K).
- Each local signature scans the query sketch. Bucket lookup uses binary search
  in sorted key arrays, O(log(b_it+1)); it is not constant-time dictionary lookup.
  The additions dictionary used by maintenance has expected constant-time lookup.
- Collision visits and heap maintenance have O(Z_q log(B+1)) upper bound.
  Sorting admissions adds O(sum_i A_i log(A_i+1)) in the current public CABS
  implementation.
- Exact scoring uses a dense query lookup vector and scans each document's sorted
  coordinates, stopping after the query's maximum coordinate. A safe bound is
  O(sum_{x in C} nnz(x) + |C| log(k+1)), not O(|C| nnz(q)) for this representation.
  Scores still accumulate in ascending matching-coordinate order using double.
- Empty-query-sketch fallback executes an exact scan. If fewer than k candidates
  have been verified, the remaining active collection is checked. Worst-case
  verification therefore reaches all active documents.

The first query, ID-array growth and generation wrap can initialize O(n_a)
workspace entries. The query lookup vector initially needs O(d) initialization.
Steady-state queries reuse these arrays; clearing the previous query's nonzero
coordinates costs O(nnz(q_previous)). Generation stamps avoid a routine full
clear; they do not remove allocated memory.

## Space

Each query thread keeps several dense arrays indexed by allocated ID and a dense
query-value vector. Routing, pool and result buffers are also retained. The actual
auxiliary bound is O(n_a + d + K + B + k), including cumulative/touched IDs;
it must not be abbreviated to O(B).

Centroids are stored as K dense length-d vectors, including zero coordinates.
Their allocated space is O(Kd). Main hash tables retain at most two memberships
per main-index ID and O(n_s m_max) postings. Persistent structures also include
original stored sparse vectors, O(n_a) record/cardinality/membership metadata,
O(d) encoding metadata, hash descriptors and retained calibration buffers.
Cached construction sketches, when retained, require their additional capacity.

Refresh removes tombstones from bucket memberships but does not compact internal
ID arrays or the original memory mapping. Thus n_s can return to the active count
while n_a remains larger. IT includes temporary calibration partitions/indexes;
IS excludes freed temporary storage. IS is a capacity-based retained payload
inventory including required original CSR, not allocator/node overhead, RSS,
query workspace, disk serialization size or peak construction memory.

An abstract coordinate-dictionary scorer or sparse-centroid implementation can
have different bounds. Such a bound must be labeled as an abstract alternative,
not as the exact cost of this code. Paper revisions should use the implementation
bound when describing the supplied executable.
