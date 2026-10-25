# SOSIA retained-space accounting — 2026-10-24

Decision: **component accounting checked; common four-method IS not accepted**.
The source pin and original runtime adaptation are unchanged. The separate
`sosia_space.patch` adds one const introspection method without adding object
fields or modifying build/search algorithms. Registered before/after hashes
check the patch against the exact previously adapted header. Build receipts also
bind the added adapter header `sosia_space.hpp`.

## Counting model

The output `index_space.json` counts one logical original CSR store (including
its file-format header) plus retained index payload. Explicit index/preprocessing
object headers and allocated vector capacities are included. Evaluation query
storage, CSR loader wrappers, executable/global code, allocator bookkeeping and
query scratch are excluded. This is a logical inventory, not process RSS or an
index serialization size. The model's wrapper/header conventions still require
the final common-method policy audit.

The 16 component fields cover SOS vectors and active-slot capacity, hash
permutations and per-vector hashes (including pointer arrays), nested bucket
headers and slot capacity, sorted cardinalities, preprocessing square norms,
object headers and external string buffers. The existing retained SOS/hash
buffers are counted; this change does not release them to reduce IS.

Introspection runs after all timed queries and does not enter query or build
timing. The adapter also validates finite nonnegative data, a positive collection
maximum and a positive maximum for each measured query before native indexing.
Validation work is inside the reported build cost. Source-specific input and
precision policies remain explicit parts of future fair-comparison acceptance.

## Executed verification

Release and full SOSIA adapter ASan/UBSan builds pass the expanded fixture suite:

- Original-ID results/scores and Recall@50 remain identical to the previous
  adapter on the four-query random fixture (Recall 0.95).
- Same-seed repeated output agrees; positive-query ties follow original ID order.
- The tied fixture exercises unused bucket capacity, so capacity is checked
  separately from logical posting size.
- A positive-query fixture has 159 empty expanded document rows and one nonempty
  row; its top-1 and component counts pass. No exact fallback supplies candidates.
- A corrupted component value and overflowing expansion domain are rejected.
- Zero queries are explicitly rejected before the native search entry.

The old ASan adapter independently reproduces a heap-buffer-overflow for a zero
query: query MinHash initialization leaves the hash at the domain length, beyond
the last bucket. Its exact compiled source, binary/build receipts, CSR fixture and
raw sanitizer trace are preserved in `experiments/delivery/diagnostics/sosia_zero_query/`.
This is a degenerate-input boundary, not evidence that ordinary positive-query
benchmark results fail. We keep the native candidate/search policy and refuse
unsupported zero queries; no query is silently dropped or replaced.

## Local real-data accounting

The previous four-query SPLADE-0.1M smoke was rerun with exactly ell=4, m=16,
candidate budget=1000 and seed=20261017. Ranked output is byte-identical to its
saved previous output; original-vector scores and independent GT still pass.

| Component scope | Bytes |
| --- | ---: |
| Retained structure | 114,976,071 |
| One original CSR store | 102,639,664 |
| Sum | 217,615,735 |

This small smoke configuration is neither a native default nor a tuned frontier.
No manuscript IS value or speedup is replaced with this measurement. The raw
inventory, timing/resource logs and receipts are under
`experiments/delivery/space_inventory/sosia_real_0.1m_20261024/`.

`space_inventory.py` checks analytic byte/count/ABI identities, n*m postings,
expanded-slot bounds, capacities and exact summation. Saved-record CI rechecks
the real inventory against the registered input header/hash; rescoring original
vectors requires the external dataset. Early derived records used a fixture-named
status; `current_space_audit.json` records the general accounting status without
changing numeric values or raw observations.

```bash
python3 experiments/baselines/build_sosia.py --upstreams build-mission/upstreams --out build-mission/sosia-space
python3 experiments/baselines/test_sosia.py --binary build-mission/sosia-space/sosia_fixed --out build-mission/sosia-space/parity.json
python3 experiments/baselines/space_inventory.py --directory method-run --base base.csr --ell 4 --m 16 --out component-audit.json
python3 experiments/delivery/verify_receipts.py
```

Use fresh output directories. Index space for SINDI/SEISMIC, common resource and
timer policy, independent official-grid selection and actual server measurement
remain open gates. This inventory alone does not close Phase A or B.
