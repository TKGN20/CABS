# Public research interfaces

Checked on 2026-10-21 against the official
[SOSIA README](https://github.com/Jacyhust/SOSIA/blob/55433a618f39e3fbc98dc34a1ffa1433d9b631b9/README.md)
and [SINDI sparse-branch README](https://github.com/Roxanne0321/vsag/blob/93e034f920334ea9e0d92784bb1d8964fb7322fa/README.md).
This comparison describes documented interfaces, not a performance comparison.

| Item | CABS | SOSIA | SINDI research branch |
| --- | --- | --- | --- |
| Build | CMake / Make, C++17 | Make, C++17 | VSAG `make release` |
| Environment | Linux / WSL | Linux; MSVC documented | VSAG source environment |
| Input | Original sparse CSR | Sparse CSR | Sparse CSR |
| Public data | 0.1M and 1M tasks | small, 1M, full | small, 1M, full links |
| Construction and search | One fresh-run pipeline; core CLI | Mode-based CLI | Separate build and search tools |
| Exact ground truth | Fresh per-corpus GT in pipeline | Evaluation through experiment modes | Separate `generate_gt` command |
| Configuration | External ell/delta; automatic K/m/B | Modes 0–4 and parameters | lambda/alpha/beta/gamma and threads |
| Reproduction | Two dataset commands | `run.sh datasetID` | Documented individual commands |
| Results | Metrics, IDs, timing, GT, hashes | `results/Running_result.txt` | Search evaluation output |
| Index save/load | Not implemented | Not described in linked README | Explicit index-path arguments |
| Scope | Static CABS research tasks | Defaults and additional experiment modes | VSAG-integrated research prototype |
| Correctness checks | Semantic, production-path, result-integrity tests | Not described in linked README | Broader VSAG test infrastructure |

CABS covers the basic research workflow of compiling, obtaining data, building
an index, running queries and evaluating results. It does not claim identical
features, experimental coverage or deployment support. The README follows
SOSIA's build/data/usage/reproduction/results organization while documenting the
smaller public task scope explicitly.
