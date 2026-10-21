# Migration to the static TKDE reproduction entry

The previous public main branch used fixed `K`, `n_probe`, spillover thresholds
and a global MinHash count. Its tracked executable, IDE settings, baseline/sweep
entry points and historical result files have been removed from the working
tree. All remain recoverable from Git at commit
`b6fe9e2e6460a660a15d20edf932918877880226`.

The modular local CABS implementation was reused rather than rewritten for
directory layout. Its pre-migration core hashes are recorded in
`source_provenance.json`. Changes in this branch add portable CMake builds, two
explicit static-task configurations, verified downloads, fresh-run orchestration,
independent output validation and a retained-memory inventory. Reporting changes
fix the hardcoded SPLADE-1M output label, separate calibration GT from evaluation
GT for IT accounting, and retain a separate original-vector byte count. Invalid
CSR construction now releases acquired mappings/file descriptors before throwing.

The externally requested defaults are `ell=24`, `delta_h=0.2`; previous source
package runs used other delta settings and are not reused as default results.
Seed, calibration count, `k_max`, build threads, repetitions and Lloyd budget
are traceable to that source package. Both public default entries use every
non-calibration query. No target performance value is encoded in the algorithm.

Sensitivity/ablation sweeps, baseline comparisons and dynamic benchmarks are
outside the minimal public entry. Dynamic data structures are retained in the
core so that the algorithm implementation is not replaced by a demonstration.
No pretrained index or machine-specific data path is required.

The optional GitHub Actions configuration is supplied as `ci-template.yml` in
this directory. It is not an active workflow: publishing credentials do not
grant GitHub's `workflow` scope. Release and sanitizer tests were run locally;
no GitHub-hosted CI success is claimed. Repository maintainers can activate the
template later with appropriately authorized credentials.

The original local verification commits are retained locally but consolidated
for publication. Measured provenance preserves their original commit identifiers
and byte hashes; canonical published-source hashes account for Git LF conversion.

The preparation proposal excluded shared CSR from IS, whereas the current
manuscript Section VI-A explicitly includes vector storage required for search.
The headline `IS_bytes` follows Section VI-A and includes the original CSR.
`index_structure_bytes` reports the proposal's excluding-CSR scope separately;
neither is the former bucket-only byte count. The manuscript's existing estimate
table has not been rewritten to match either newly measured inventory.
