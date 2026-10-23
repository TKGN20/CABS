# PR #4 third-round revision — 2026-10-23

The production and evidence approval baseline is
`fa2455ed83de47fec1de60563949e32eb1d34c6c`.
This revision addresses the third review's documentation and experiment-protocol
requests. It does not add new performance measurements, alter production code,
change defaults, or merge the PR.

| Review request | Revision | Verification / remaining scope |
| --- | --- | --- |
| P0: immutable approval reference | The approval report checks out the full baseline SHA; [baseline manifest](approval_baseline.json) records 19 production/default/build hashes | Current verifier checks inventory/content; optional historical verification checks the fixed Git blobs. Review the new amendment at the full SHA recorded in the PR description |
| P0: verification scope | Added separate rows for CTest, ASan/UBSan, local historical-source audit, Release CI saved-record audit, independent full-data rerun and target-server benchmarking | CI omits `--source-check`; 185 artifact and 164 source checks are baseline local counts, with the new 19-file freeze reported separately |
| P1: review burden | [Review index](REVIEW_INDEX.md) groups production, tests/CI, scripts, diagnostics, measurements and docs; PR description links the groups | Existing raw evidence stays intact; no new diagnostic result bundles |
| P1: independent selection set | [Server plan](SERVER_VALIDATION_PROPOSAL.md) reserves separate calibration, selection and final evaluation IDs and locks search configurations before evaluation | Three-way explicit-ID execution is pending experiment-driver work; current two-way CLI is not relabeled as supporting it |
| P1: unmatched policy | No validation match or out-of-band final recall is marked unmatched | No final-set reselection, tolerance relaxation or interpolated speedup |
| P1: routing/calibration evidence | Distinguishes routing correctness from selectivity; plans probe-depth/tightness reporting and crossed calibration/index seeds | No claim of a real-data early-stop benefit or universal optimal default |
| P2: new upper bound / K objective | Keeps these as separate proposed research | Neither enters production or the frozen Section IV method in this revision |

The source check deliberately accepts only CRLF/LF differences. Changes to the
freeze manifest itself still require review; a passing check is not an approval
decision. GitHub mergeability similarly does not imply an independent submitted
review. As inspected at the start of this revision, PR #4 was open and had no
submitted GitHub reviews.

The third-round local validation checks the freeze against Git history, exercises
content/addition/deletion detection in an isolated temporary copy, and reruns the
public saved-record verifier. The existing Release and sanitizer workflow then
checks the uploaded revision. Commit-specific CI links and the final revision SHA
are recorded in the PR description and handoff report; this document does not
predeclare a future CI outcome.

The proposed server comparisons, multiple calibration seeds and independent 1M
rebuild remain unexecuted acceptance work. The 1M record of 17.4717 QPS at
Recall@50 0.995717 remains the measured local candidate result, not proof of the
paper's server-throughput targets.
