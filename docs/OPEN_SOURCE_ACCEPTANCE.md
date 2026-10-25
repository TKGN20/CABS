# CABS open-source acceptance

Scope: [Issue #6](https://github.com/TKGN20/CABS/issues/6), revised 2026-10-25.
Only the CABS implementation, its usability, tests, documentation and own
reproduction tools are required. No baseline or server comparison is a gate.

## Candidate and frozen implementation

- Production baseline: `e26de385fe78d3589d6d861c3560c443e254f9cc` (PR #4).
- Delivery: [PR #7](https://github.com/TKGN20/CABS/pull/7), branch
  `codex/opensource-final-20261025`, directly based on that baseline.
- The owner approved the sequential merges on 2026-10-25. PR #4 merged as
  `4899be960267cc8854d51c5d6ac5c035a35b11b7`, then PR #7 was retargeted to main,
  checked and merged as `4fa179b154d09fb922162b676a6f7916db472a86`.
  PR #5 remains separate experimental material and is not merged.
- The 19-file production/default freeze remains enforced. The release work adds
  a public tiny example, reporting/input-validation tests and documentation;
  it does not change K/m/B, routing, scoring or default configurations.

## Verified execution

The first public clean-clone acceptance used exactly
`c6d6956753505a42eb895f20976a444a6eda5428`, cloned from GitHub, without external
vectors, any baseline library, prebuilt binaries or an existing build directory.
Environment: Ubuntu 22.04 under WSL, GCC 11.4, Python 3.10.12, CMake/OpenMP.

| Check | Result | Evidence |
| --- | --- | --- |
| Release build and five CTests | PASS | Clean-clone configure/build/test logs |
| Debug ASan/UBSan build and five CTests | PASS | Clean-clone configure/build/test logs; leak/error halting enabled |
| Public static and maintenance examples | PASS | Both report 16 independently checked exact-GT queries; maintenance checkpoints verified |
| Saved CABS records and source freeze | PASS | 185 artifact members, 19 frozen files; no new whole-corpus run |
| GitHub Release and sanitizer CI | PASS at this revision | [Run 40174060235](https://github.com/TKGN20/CABS/actions/runs/40174060235) |
| Public data links | PASS on 2026-10-25 | Three HTTP HEAD responses were 200 |
| Data content | PASS | Existing local 0.1M, 1M and query files match manifest headers, sizes and SHA-256; public query gzip independently downloaded/decompressed and verified |

The final tested code revision is
`7aa8c540e6b514481bcf48047bbfbd48af7f1b9f`. It adds regression checks for missing
offline inputs, wrong checksums and truncated CSR headers/sizes. The clean public
clone was fast-forwarded to this exact revision; both five-test CTest suites,
all eleven Python reporting/input tests and both examples passed again. The
production/default/build files are identical, so the clean-built C++ binaries
remain applicable. GitHub also rebuilt this exact revision from scratch:
[run 40174695971](https://github.com/TKGN20/CABS/actions/runs/40174695971), Release
and Debug ASan/UBSan both successful.

Public evidence: [normalized execution log](release_checks/clean_clone.log),
[machine-readable receipt](release_checks/receipt.json), and
[public payload audit](release_checks/public_files.json). The final documentation
commit adds only this report, checklist and execution evidence. Recommend the
tested code revision above for acceptance; include its subsequent documentation
commit when merging. No release tag has been created.

Builds emitted an existing unused-function warning in the diagnostic target and
WSL mounted-filesystem clock-skew warnings. Both builds completed and their
executables passed the tests; these warnings are retained in the acceptance logs.

## Review entry points

- [Quick start, parameters, metrics and errors](../README.md).
- [Section IV mechanism-to-source/test map](PAPER_IMPLEMENTATION.md),
  [implementation contract](METHOD.md), [actual complexity](COMPLEXITY.md).
- [Public input manifest](../configs/datasets.json) and
  [data format/terms](../datasets/README.md).
- [Tiny static/maintenance example](../examples/tiny.py),
  [CABS-only runner](../scripts/reproduce.py),
  [independent output evaluator](../scripts/evaluate.py),
  [saved-record verifier](../scripts/verify_artifacts.py).
- [MIT code license](../LICENSE). Dataset rights remain with their providers;
  no original corpus is redistributed. The repository citation is in README.

## Public payload audit

At `7aa8c54`, 284 tracked files contain 66,551,985 payload bytes (not Git history
or checkout disk allocation). The largest is a 10,348,499-byte compressed GT
record. There are no tracked CSR corpora, executable binaries, object files or
shared libraries. The ordinary build needs only the C++/Python standard tools.
The new release delta contains no third-party baseline code.

A scan for common GitHub/AWS token formats and private-key headers found no
matches. This is a bounded pattern audit, not a proof that arbitrary secrets
cannot exist. Seventeen pre-existing historical logs/manifests contain local
working-directory metadata. Those provenance records are preserved unchanged;
their paths are not required by the public commands. New public acceptance logs
replace the checkout path with `$REPO` and identify that normalization explicitly.
No historical measurements, manifests or hashes are silently rewritten.

## Limits and publication decision

Functional execution, CI and saved-record recomputation are different evidence
classes. This delivery does not rerun the complete SPLADE corpus, verify paper
speedups or guarantee hardware-independent QPS. The public scripts let a user
perform fresh CABS runs and retain the required evidence themselves.

The implementation uses practical keyed pseudorandom MinHash ranks; exact ideal
minwise independence is not proved. Calibration does not guarantee every future
query's Recall@50. Indexes are in memory; concurrent maintenance, crash recovery,
serialization and native Windows builds are outside the supported interface.

## Completed publication on main

The approved route is complete: PR #4 merged first; PR #7 was retargeted to main
and its ten-file delta checked before merging. Main merge `4fa179b` has exactly
the same tracked tree as approved candidate `a534b15`; production/default/build
files remain identical to the frozen PR #4 implementation. PR #5 is not merged.

[Main CI run 40175560510](https://github.com/TKGN20/CABS/actions/runs/40175560510)
rebuilt Release and Debug ASan/UBSan successfully. A new default-branch public
clone also built Release, passed all five CTests, ran static and maintenance
examples, and verified all 185 artifact members, 164 historical source-file
checks and 19 frozen files against current content and baseline Git blobs.
See the [main receipt](release_checks/main_receipt.json) and
[normalized main execution log](release_checks/main_clone.log).

Decision: **CABS-only open-source delivery FINAL PASS.** The repository's default
main now contains the accepted delivery. This final status synchronization adds
documentation/evidence only. No tag, GitHub Release or additional performance
experiment is needed or claimed for Issue #6.
