# CABS / TKDE delivery acceptance

Mission: [Issue #6](https://github.com/TKGN20/CABS/issues/6).
Status: **NOT FINAL PASS — not submission-ready**.
Last initial assessment: 2026-10-23; later milestones are appended to the evidence log.

| Phase | Gate | Status | Evidence / next action |
| --- | --- | --- | --- |
| Preflight | Frozen algorithm/default contract | PASS | PR #4 head e26de385fe78d3589d6d861c3560c443e254f9cc; 19-file historical freeze check; no merges authorized |
| Preflight | Existing CABS experiment driver | PASS (bounded scope) | PR #5 f77586337535d85757ba7ed907b91247dd905d97; synthetic original-ID and split/seed/lock tests; not full server delivery |
| Preflight | Local two-scale inputs / query identity | PASS (header/size/hash) | 100,000 / 1,000,000 rows and 6,980 queries; WSL symlink target verified; [receipt](experiments/delivery/receipts/preflight_20261023.json) |
| A | All four methods build and independently checked IDs/scores | FAIL / incomplete | CABS and bounded SOSIA fixtures pass; 9 oracle tests pass; SOSIA IS/common-lock audit pending; SINDI and SEISMIC only source-pinned/downloaded |
| B | Common adapters, registered grids and real validation-only selection | FAIL / incomplete | CABS-only three-way driver exists; four-method protocol and real-data locks pending |
| C | Confirmed paper hardware and 3 builds × 3 timings | BLOCKED | Owner explicitly confirmed on 2026-10-23 that server is not ready; no target-server runs performed |
| D | Crossed 3 calibration × 3 index seeds and mechanism diagnostics | FAIL / incomplete | Existing diagnostic subsets do not satisfy the required complete experiment |
| E | Full manuscript workload/ablation matrix | FAIL / incomplete | Actual manuscript/data inventory and evidence mapping pending; old forecasts are not measurements |
| F | Independent final audit, paper-ready evidence, clean-checkout reproduction | FAIL / incomplete | Saved-record audit/CI is narrower than independent full-run replication; complete delivery bundle pending |

No speedup or paper table can be approved from a green fixture/CI alone. Preserve
negative, failed and unmatched outcomes. Do not relabel historical local results
as current server measurements. The issue remains open until every mandatory gate
has verifiable evidence; journal acceptance is not an engineering guarantee.

## Roadmap and continuation

1. Pin baseline sources and establish upstream/patch provenance; inventory data
   and resources. Existing archives are read-only candidates, not approved imports.
2. Build minimal native adapters, emit original IDs/raw timings/native scores and
   exact reranked scores with timing scope disclosed. Validate against independent
   CSR scoring, including ties, underfill, missing queries and errors.
3. Unify adapters with the fixed selection/evaluation contract; register official
   parameter grids before evaluating final queries. Keep baseline build and search
   controls distinct from CABS self-configuration.
4. Once server access/resources are explicitly available, run the registered pilot
   first, then the independent-build matrix and robustness studies.
5. Reconcile every manuscript mechanism/metric/figure with exact evidence, extend
   the complete workload matrix, and perform clean-checkout/independent audits.

Local preflight command (fresh output directory):

```bash
python3 experiments/delivery/preflight.py --out build-mission/preflight-20261023
```

Execution events: [append-only evidence log](experiments/delivery/evidence.jsonl).
Raw environment/data receipts are created by the command; a written plan is not
a receipt of execution. Source changes remain on the independent experimental
branch. PR #4 and historical result bundles are preserved.
