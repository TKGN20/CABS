# CABS open-source acceptance

Scope: [Issue #6](https://github.com/TKGN20/CABS/issues/6), revised 2026-10-25.
Only the CABS implementation, its usability, tests, documentation and own
reproduction tools are required. No baseline or server comparison is a gate.

## Candidate and frozen implementation

- Production baseline: `e26de385fe78d3589d6d861c3560c443e254f9cc` (PR #4).
- Delivery: [PR #7](https://github.com/TKGN20/CABS/pull/7), branch
  `codex/opensource-final-20261025`, directly based on that baseline.
- PR #5 remains separate experimental material. Neither PR #4 nor #5 is merged
  by this delivery. Owner approval is still required for merges.
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

The following revision also adds explicit regression checks for missing offline
inputs, wrong checksums and truncated CSR headers/sizes. Its final commit and
CI evidence will be recorded after publication. The current checklist is the
authoritative remaining-work list, not a claim that unexecuted checks passed.

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

At `c6d6956`, 283 tracked files contain 66,543,714 payload bytes (not Git history
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

Code acceptance and merging are separate. The intended release route is owner
approval of frozen PR #4, then retarget/review the small PR #7 delta against main.
Do not merge PR #7 into the frozen review branch or merge PR #5 wholesale. No tag
or GitHub Release is claimed to exist until the owner approves publication.
