# PrimeForge provenance policy

## Classification

Every software component has two separate attributes:

1. relationship: ORIGINAL, LINKED, EXTERNAL, REFERENCE, REJECTED, CI_ONLY, or DEVELOPMENT_TOOL;
2. distribution: YES or NO.

EXTERNAL does not mean license-free. REFERENCE does not permit copying. A process adapter changes technical coupling only.

## Required evidence before selection

A component may be selected only when SOURCES.lock records its primary source, exact revision/archive, SHA-256, license, license-file hash, provenance, redistribution decision, integration mode, audit date, and status. The component audit must agree with that row. An UNKNOWN license blocks selection.

Before redistributed changes are merged:

1. add the exact license text and required copyright/NOTICE material;
2. update the distribution manifest to YES and pin the license SHA-256;
3. record source-code offer or relinking requirements when applicable;
4. regenerate THIRD_PARTY_NOTICES.txt;
5. run primeforge-distribution-check and all CTest tests;
6. review the produced archive separately from the build tree.

## Original-code rule

Original PrimeForge source and build logic carries SPDX-License-Identifier: Apache-2.0. User-provided specifications, third-party artifacts, generated data, benchmark results, checkpoints, proofs, and research-source downloads do not acquire that license by directory proximity.

No third-party function, comment, table, test vector collection, or distinctive source structure may be pasted into original code merely because the repository is visible. Attribution is necessary when a license requires it, but attribution alone is not permission.

## Clean-room reimplementation

A clean-room change has two roles when practical:

- specification reviewer: records public mathematical/behavioral requirements and excludes expressive source details;
- implementer: works from that neutral specification and independently created tests, without reading the restricted implementation.

Each clean-room record must identify:

- feature and reason for reimplementation;
- primary mathematical/interface references;
- third-party sources seen by each role;
- neutral behavior specification hash;
- independently created test/corpus hashes;
- implementer and reviewer;
- commit and date;
- residual similarity or patent/trademark risks.

If the same person has already read the source, the change is not represented as strict clean-room work. It may still be an independent reimplementation, but the exposure and reasoning must be disclosed and a source-similarity review is required.

## Review and audit trail

SOURCES.lock is append-oriented: revisions do not silently change. DECISIONS.md records policy changes, CLAIMS.tsv records evidence level, and NEGATIVE_RESULTS.md records blocked license/integration attempts. Original copyright notices are not rewritten to make provenance look simpler.

This policy is a conservative engineering control and not a substitute for advice from a qualified lawyer when a distribution decision remains materially uncertain.
