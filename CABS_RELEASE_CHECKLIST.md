# CABS open-source release checklist

Scope authority: [Issue #6](https://github.com/TKGN20/CABS/issues/6), revision
2026-10-25. This replaces the former TKDE/baseline/server delivery mission.
Only CABS code, usability, tests, its own reproduction tools and documentation
are release requirements. PR #5 remains separate experimental material.

Production baseline: `e26de385fe78d3589d6d861c3560c443e254f9cc` (frozen PR #4).
Delivery branch: `codex/opensource-final-20261025`, based directly on that SHA.
No production/default changes or PR merges are authorized by this checklist.

PASS is limited to the evidence named in its row. FAIL includes work not yet
executed; it does not mean an observed algorithm failure. BLOCKED requires a
specific unavailable prerequisite. The final report will record exact candidate
SHA, clean-clone logs and CI links before requesting release approval.

| Requirement | Status | Current evidence / work remaining |
| --- | --- | --- |
| Section IV-A–IV-E source present | PASS (mapped and tested) | `docs/PAPER_IMPLEMENTATION.md`, `docs/METHOD.md`; five CTests passed in local Release and ASan/UBSan builds; frozen production unchanged |
| Automatic K/m/B and maintenance mechanisms | PASS (tested contract) | `tests/mechanism_contract.cpp`, `selftest`, and tiny maintenance execution passed; not a universal recall guarantee |
| Minimal example without external vectors | PASS (local execution) | `examples/tiny.py` static and maintenance modes passed, including 16 independent GT queries and refusal to overwrite outputs; also exercised by end-to-end CTest |
| Clean Linux Release build and CTest | FAIL | No clean-clone execution yet for this release candidate |
| Clean ASan/UBSan build and CTest | FAIL | No clean-clone execution yet for this release candidate |
| Current candidate CI | FAIL | Candidate has not yet been published; PR #4 CI is historical evidence only |
| CABS SPLADE-0.1M/1M data and run instructions | PASS (existing entry points) | `configs/datasets.json`, `scripts/prepare_data.py`, `scripts/reproduce.sh`; verify checksums/commands and error paths |
| Saved CABS measurements independently recomputable | PASS (saved-record scope) | `scripts/verify_artifacts.py`: 185 artifact members and 19 frozen files passed on Linux Python 3.10 and read-only Python 3.14; no full performance rerun claimed |
| README, parameters, complexity, license and limitations | FAIL | Existing material is present; simplify release entry points and distinguish release scope from historical research plans |
| Public-file, license and artifact-size audit | FAIL | Inventory tracked payloads and known historical path metadata; no original corpus or credentials may be added |
| Fresh public clone follows README successfully | FAIL | Execute against the final published candidate; preserve exact revision and logs |
| Final acceptance report / recommended revision | FAIL | Requires the above execution evidence; no self-merge or fabricated release claim |

Cancelled requirements: third-party baseline work, comparison infrastructure,
matched-recall grids, new server runs, twelve-workload experiments and paper
speedup targets. None may block or extend this CABS-only release.

Local checks completed before publication: five Release CTests, five Debug
ASan/UBSan CTests, nine Python reporting tests, static/maintenance tiny examples
and saved-record verification. The Python 3.14 aggregate-rounding mismatch was
reproduced and fixed only for declared floating aggregate fields at 1e-12;
record hashes, IDs, counts, source freeze and original measurement bytes remain
strict. Windows-native CABS is still unsupported; Python-only record verification
is a separate scope. Clean public-clone and candidate-CI acceptance remain open.
