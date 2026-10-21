# Manuscript and code alignment

## What is aligned

The static implementation follows the current Section IV mechanisms: IDF-aware
allocation with exact total capacity d*ell; spherical primary/secondary
memberships; automatic K, local m_i and B; separate temporary/final hashes; a
global candidate pool; exact original-vector scores; and strict bound-based
stopping. Public external defaults are ell=24 and delta_h=0.2. Source provenance
and the practical pseudorandom hashing assumptions are documented separately.

Headline IS includes required original CSR to follow current VI-A. Structure-only
and bucket-only bytes remain separate. IT includes exact calibration targets and
all required temporary/automatic construction, excluding evaluation GT.

## Numerical scope

The checked-in results are actual local WSL measurements of the protocol in this
repository. They are not declared reproductions of the manuscript's numerical
claims and do not establish baseline superiority. Exact file identities,
query/calibration split, source version, automatic configuration, hardware, and
metric boundaries must match before making such a claim.

The public 0.1M file has 127.29954 mean document nonzeros. The query protocol uses
four calibration queries and all 6,976 remaining queries, with execution settings
traced to prior audited records. Different slices or query protocols need their
own exact ground truth and measurements.

## Required next evidence

To claim numerical agreement, use the exact paper input slice and query IDs,
calibration protocol, hardware/compiler, and matching IT/IS definitions. Preserve
all measured records and report actual internally selected configurations. If
measurements differ, revise the paper's numerical claims or make and validate an
explicit method change with corresponding manuscript updates. Container/code
cleanup alone is not evidence that the draft targets have been recovered.

The repository provides a runnable implementation and auditable observations;
numerical reproduction of manuscript claims is a separate validation requirement.
