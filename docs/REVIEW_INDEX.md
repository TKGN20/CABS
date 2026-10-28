# PR #4 review index and frozen scope

> **Archived review record.** This document describes the review state at the
> time it was written. PR #4 has since been merged into `main` (2026-10-25).
> For current user instructions and measured-result provenance, see
> [README](../README.md) and the [Reproducibility guide](REPRODUCIBILITY.md).
> Historical decisions and raw evidence are preserved unchanged.

Approval baseline: [`fa2455ed83de47fec1de60563949e32eb1d34c6c`](https://github.com/TKGN20/CABS/commit/fa2455ed83de47fec1de60563949e32eb1d34c6c).
Base main for this review: `d091458df71270f8bcd064d7997af9edaddec0e9`.
The third-round revision changes review documentation and the saved-record verifier;
it adds no benchmark observations or production algorithm changes.

| Category | Review entry points | What to check |
| --- | --- | --- |
| Production implementation | [epoch index](../include/cabs/search/epoch_index.hpp), [query workspace](../include/cabs/search/query_workspace.hpp), [executable](../src/main.cpp), [paper mapping](PAPER_IMPLEMENTATION.md) | Static sketch-cache lifecycle, ordered exact verification, search and maintenance behavior; these three source files contain the production changes relative to the base main |
| Defaults and build | [0.1M](../configs/splade_0.1m.json), [1M](../configs/splade_1m.json), [CMake](../CMakeLists.txt), [freeze manifest](approval_baseline.json) | Explicit task settings and cache policy; retain automatic K/m/B and nc=4/k_max=100 |
| Tests / CI | [mechanism contract](../tests/mechanism_contract.cpp), [audit replay smoke](../tests/calibration_audit_smoke.py), [report checks](../tests/test_reporting.py), [workflow](../.github/workflows/ci.yml) | Reference-path equality, actual stopping fixture, maintenance semantics, sanitizer scope; CI verifies saved records but does not rerun 1M |
| Reproducibility scripts | [reproduce](../scripts/reproduce.py), [evaluate](../scripts/evaluate.py), [verify artifacts](../scripts/verify_artifacts.py) | Timing/IS definitions, raw-record aggregation, hashes, fixed source history and frozen production scope |
| Diagnostics | [calibration executable](../benchmarks/calibration_audit.cpp), [audit runner](../scripts/audit_calibration.py), [pilot](../scripts/calibration_pilot.py), [analysis](../scripts/approval_analysis.py), [guide](CALIBRATION_AUDIT.md) | Instrumented versus uninstrumented timing, common held-out queries, routing bounds and frozen-prefix budget scope |
| Published measurements | [historical full runs](../results/measured), [candidate and diagnostic records](../results/engineering) | Immutable versioned evidence, per-directory manifests, all measured cases retained; tables are not production code |
| Documentation | [approval report](APPROVAL_REVIEW_ZH.md), [third-round response](THIRD_REVIEW_RESPONSE.md), [complexity](COMPLEXITY.md), [server protocol](SERVER_VALIDATION_PROPOSAL.md) | Claims match evidence, implementation limitations, independent validation/evaluation and pending experiments |

Review the production delta first, then its tests and scripts; use the saved
measurements to verify claims rather than reading thousands of CSV lines as code.
No old result bundle is replaced by this revision. Existing `docs/approval_checks/`
logs and counts remain records of the baseline validation, not new measurements.

## Freeze contract

`approval_baseline.json` covers every file under `include/`, `src/`, and `configs/`,
plus `CMakeLists.txt` and `Makefile` (19 files at the baseline). Hashes use SHA-256
after CRLF-to-LF normalization only. The verifier rejects added/missing files or
changed content in that scope. With `--source-check`, it also compares the hashes
to the fixed baseline Git blobs. The manifest and verifier are reviewable controls,
not a substitute for reviewing any future change to the baseline itself.

```bash
git diff fa2455ed83de47fec1de60563949e32eb1d34c6c HEAD -- include src configs CMakeLists.txt Makefile
python3 scripts/verify_artifacts.py --source-check
```

The first command must show no production/default/build delta for this third-round
revision. Use the full revision SHA recorded in the PR description to check out
this amendment; the original baseline verification command is fixed in the
[approval report](APPROVAL_REVIEW_ZH.md#approval-baseline).

Saved-record audit, CI mechanism checks, independent full-data reruns, and
paper-server performance validation are separate acceptance items. A mergeable
PR has no conflicts; it does not by itself have an independent approval review.
