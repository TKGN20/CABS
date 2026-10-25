# Response to the independent repository review

Review received 2026-10-22. This response distinguishes method completeness,
implementation validation and paper performance evidence. Journal acceptance is
an editorial decision; passing the repository checks is not a TKDE certificate.

The current manuscript snapshot checked locally is the 2026-10-21 bibliography
revision. Its IV-D uses a routing/hash/bucket objective for K, and its VI-A defines
IS as persistent search memory including required vector storage. The independent
review's statement that current manuscript IS excludes CSR describes an earlier
definition. The repository keeps structure-only and total bytes separately so
either quantity can be inspected without relabeling measurements.

| Review item | Disposition |
| --- | --- |
| Complete candidate 1M task at PR head b327ec35 | Completed in a detached b327ec35 snapshot, identified independently by source/pipeline hashes; all 6,976 queries over three repeats are published separately |
| K objective differs from full query cost | Valid performance concern, not a missing step relative to current IV-D; retain the specified objective and diagnose the cost gap separately |
| B and verification overhead | Report actual B, touched IDs, cumulative verified IDs and cluster visits; no manual cap or silent change to k_max/calibration queries |
| Repeated offline K calibration | Count all necessary work in IT; nested stage labels remain non-additive; do not replace it with final-table build time |
| IS boundary | Retain total payload including CSR and structure-only components; same definition across task sizes; payload is not RSS |
| Query workspace and practical hash model | Expanded in PAPER_IMPLEMENTATION.md and COMPLEXITY.md, including dense centroids, actual scorer and sorted-key lookup |
| Missing active CI | Deploy .github/workflows/ci.yml; Linux Release and ASan/UBSan jobs include CTest and published-record audits |
| Multi-cluster mechanism coverage | Add deterministic mechanism_contract tests for local floors/fallback, spillover, fresh hashes, minimum B, bucket construction, reference traces and maintenance after cache release |
| 0.1M mean document nnz | Actual input has 12,729,954 nonzeros / 100,000 documents = 127.29954; do not equate it with an unverified 126.3 slice statistic |
| New Xeon experiments | Not established by local measurements; require execution on that machine before adding server result claims |

Machine changes can alter latency, throughput and construction time. Timing-based
K selection can also change the automatically chosen configuration, so a recall
comparison needs the realized configuration and seeds as well as hardware.
Environment difference alone does not establish a paper speedup or explain a
different space definition. The repository publishes measurements, not target
numbers substituted for execution.

The complete 1M run measured Recall@50 0.9957167431, 17.4717273 QPS,
747.4427805 s IT and 1.746098882 GB IS. Its original WSL Git metadata capture was
null because the Windows-created worktree used an absolute Windows gitdir.
The snapshot attestation checks all source and pipeline hashes without editing
that original metadata. All GT, ranked results and non-timing fields match the
previous full 1M record. This closes the missing full candidate-run evidence;
it does not establish the manuscript's server speedups.

New diagnostics found B=51385 is driven by query 45's target 242252, exact rank
88, whose best temporary proxy-prefix rank is 51385. This follows the declared
top-100 calibration target, rather than a manually chosen cap. Every full 1M query
probed all eight clusters on this realization. Stopping is implemented but no
early-stopping speedup is evidenced by this run. The 0.1M all-K sample has matching
proxy/full-cost minima at K=1; a cost-objective error is not demonstrated there.

## Artifact expectations

The repository provides platform/compiler requirements, data URLs and hashes,
step-by-step build/run commands, parameter files, expected output schemas, a
license and result provenance. Questions can be filed in the repository's issue
tracker. The public experimental scope remains the two SPLADE tasks; all twelve
workloads and comparator experiments are not claimed as one-command deliverables.

These choices follow the transparency and documentation goals in the official
[IEEE reproducibility guidance](https://journals.ieeeauthorcenter.ieee.org/create-your-ieee-journal-article/research-reproducibility/)
and [supplementary-material README guidance](https://journals.ieeeauthorcenter.ieee.org/create-your-ieee-journal-article/prepare-supplementary-materials/).
They are project acceptance criteria, not a claim that IEEE mandates this exact
CI matrix or numerical tolerance across machines.
