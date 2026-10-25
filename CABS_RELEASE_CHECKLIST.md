# CABS open-source release checklist

Scope authority: [Issue #6](https://github.com/TKGN20/CABS/issues/6), revised
2026-10-25. Only CABS implementation, usability, tests, its own reproduction
tools and documentation are acceptance requirements. PR #5 remains separate
experimental material; no baseline, server or paper-speedup gate applies.

Production baseline: `e26de385fe78d3589d6d861c3560c443e254f9cc` (frozen PR #4).
Tested code: `7aa8c540e6b514481bcf48047bbfbd48af7f1b9f`.
Delivery: [PR #7](https://github.com/TKGN20/CABS/pull/7), branch
`codex/opensource-final-20261025`, based directly on the production baseline.
Owner-approved main merge: `4fa179b154d09fb922162b676a6f7916db472a86`.

**Decision: CABS-only open-source delivery FINAL PASS.** The owner approved and
PR #4 and PR #7 were merged sequentially on 2026-10-25. PR #5 remains separate.
PASS is limited to the named evidence. No tag or GitHub Release is claimed.

| Requirement | Status | Evidence |
| --- | --- | --- |
| Section IV-A–IV-E source present | PASS | `docs/PAPER_IMPLEMENTATION.md`, `docs/METHOD.md`, mechanism/production-path CTests; 19-file production/default freeze unchanged |
| Automatic K/m/B and maintenance | PASS | Mechanism contract, selftest, maintenance example; insert/delete/update/refresh and cache lifecycle checks; no universal recall guarantee |
| Minimal example without external vectors | PASS | `examples/tiny.py`, static and maintenance; 16 independent GT queries in each, exact returned scores, overwrite refusal |
| Clean Linux Release build/test | PASS | Public clone at c6d6956; all five CTests; rerun at 7aa8c54 with frozen C++/build files identical |
| Clean Debug ASan/UBSan build/test | PASS | Same clean clone, all five CTests; leak and undefined-behavior halting enabled; rerun at 7aa8c54 |
| GitHub CI | PASS | [Main merge run 40175560510](https://github.com/TKGN20/CABS/actions/runs/40175560510), Release and Debug ASan/UBSan successful; Release audits saved measurements; candidate runs retained in report |
| Public data and CABS-only commands | PASS | Three download URLs respond 200; local 0.1M/1M/query headers, sizes and SHA-256 match; public query gzip downloaded/decompressed/verified; no new full-corpus benchmark |
| Input/error regression tests | PASS | Eleven Python reporting/input tests; missing offline file, mismatched hash, truncated header/size, reporting integrity and cross-version rounding covered |
| Existing CABS measurements recomputable | PASS, saved-record scope | 185 artifact members and 19 frozen files verified; Linux Python 3.10 and Python-only verifier under 3.14; historical measurements unchanged |
| README, parameters, complexity, license/citation, limits | PASS | Clone/build/examples, CLI/automatic configuration, metric definitions, API boundaries, `docs/COMPLEXITY.md`, MIT license and repository citation |
| Public files and sizes | PASS with disclosed historical metadata | At tested code: 284 files, 66,551,985 payload bytes, largest 10,348,499; no tracked corpus/binary; common credential-pattern scan empty; 17 historical path-bearing records preserved |
| Fresh public clone follows usage path | PASS | Exact revisions, environment and command outputs in `docs/release_checks/`; ordinary CABS build has no baseline dependency |
| Final report and recommended revision | PASS | `docs/OPEN_SOURCE_ACCEPTANCE.md`; tested code 7aa8c54 plus final documentation/evidence commit recommended for owner review |

Execution evidence is in [the report](docs/OPEN_SOURCE_ACCEPTANCE.md),
[normalized log](docs/release_checks/clean_clone.log) and
[receipt](docs/release_checks/receipt.json). Absolute checkout paths in the new
log are replaced by `$REPO`; original experimental bundles are not rewritten.
Build logs retain an existing unused-function warning and WSL timestamp warnings;
both builds and all executed checks completed successfully.

Publication completed: frozen PR #4 merged as `4899be9`; PR #7 was retargeted to
main, its ten-file delta checked, then merged as `4fa179b`. That merge's tree is
identical to approved candidate `a534b15`. The default-main clean-clone checks are
recorded in `docs/release_checks/main_receipt.json` and `main_clone.log`.
This status update changes documentation/evidence only. PR #5 is not merged;
there are no remaining baseline/server/paper experimental requirements.
