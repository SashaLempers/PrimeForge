# Licensing policy

## Original PrimeForge code

Original source code authored specifically for PrimeForge under `include/`, `src/`, `tests/`, `cmake/`, and `scripts/`, together with original build configuration, is licensed under Apache License 2.0 unless a file states otherwise. The full license text is in `LICENSE`.

## Material outside that grant

The Apache-2.0 grant does not automatically cover:

- the user-provided research specification under `docs/specifications/`;
- third-party source, binary, documentation, certificates, or datasets;
- generated benchmark data, proof artifacts, coverage databases, checkpoints, or release assets;
- trademarks or rights held by third parties.

Such material requires an explicit, separate notice or license.

## Dependencies

Every dependency is reviewed individually. The review records its authoritative source, exact revision and hashes, license text and hash, provenance, redistribution terms, linking or process relationship, build options, and any patches. An external-process boundary is an architectural fact, not a license conclusion.

## Stage 3 license choice review

Apache License 2.0 remains the license for original PrimeForge code. This is a project governance decision, not legal advice. The review retained it because the unmodified standard license is reusable, includes explicit copyright/patent terms, permits broad use of the original code, and has a well-defined notice mechanism. The authoritative English license text remains in LICENSE; NOTICE identifies the original work.

Primary guidance reviewed:

- Apache Software Foundation, applying Apache License 2.0: https://www.apache.org/legal/apply-license
- Apache licensing and distribution FAQ: https://www.apache.org/foundation/license-faq.html
- Apache explanation of one-way GPLv3 compatibility: https://www.apache.org/licenses/GPL-compatibility
- GNU license list and Apache-2.0 compatibility note: https://www.gnu.org/licenses/license-list.html#apache2

Compatibility is not inferred transitively. In particular, the Apache guidance describes Apache-2.0/GPLv3 compatibility as one-way and states that Apache-2.0 is not compatible with GPLv2-only. PrimeForge therefore isolates strong-copyleft/custom-EULA candidates as external tools unless a later, component-specific distribution review reaches a different documented conclusion.

## Machine-checked distribution scope

licenses/DISTRIBUTION_MANIFEST.tsv classifies every audited program as ORIGINAL, LINKED, EXTERNAL, REFERENCE, REJECTED, CI_ONLY, or DEVELOPMENT_TOOL, independently of whether it is redistributed. Every redistributed row must point to a non-empty license file whose SHA-256 is pinned. Required notice files must also exist.

The CMake distribution check and its negative CTest fixture make the build gate fail if a required license is missing or changed. THIRD_PARTY_NOTICES.txt is generated from the same manifest.

## Copyright and reimplementation

Copyright notices from third-party material must be retained verbatim when its license requires them. PrimeForge contributors may learn from documented algorithms, mathematical papers, public interfaces, and black-box behavior, but must not copy third-party function bodies, comments, tables, constants, tests, or distinctive structure without an explicit compatible grant and attribution.

Any clean-room reimplementation follows docs/PROVENANCE_POLICY.md and is entered in docs/CLEAN_ROOM_LOG.md before merge.

## Stage 4 oracle quarantine

FLINT 3.6.0, GMP 6.3.0, MPFR 4.2.2, pthreads4w 3.0.0, PARI/GP 2.17.4, and proth20 0.9.1 are used only as local correctness oracles. Their binaries and DLLs live under ignored `out/oracles`, are not linked into PrimeForge targets, and are not redistributed.

The pinned FLINT upstream source identifies the library as LGPL-3.0-or-later, while the generated vcpkg package SPDX record declares GPL-3.0-only. This discrepancy is unresolved for binary distribution. PrimeForge therefore applies the conservative common result: the local FLINT oracle and every transitive DLL are prohibited from release packaging. An external or development-only process boundary is not treated as a license exemption.

## PIVOT-04 CUDA validation quarantine

The official local NVIDIA CUDA Toolkit is governed by its own NVIDIA CUDA
Toolkit EULA, not by PrimeForge's Apache-2.0 grant. The exact local EULA hash,
compiler hash, toolkit/runtime versions, provenance and non-redistribution
decision are recorded as `TOOL-0008` in `SOURCES.lock`.

CUDA is disabled by default. The optional `msvc-cuda-release` preset builds a
target-machine validation executable only under the ignored build tree. Binary
dependency inspection shows no dynamic CUDA runtime DLL: the local validator
contains the statically linked runtime selected by `CUDA::cudart`. PrimeForge
therefore excludes that entire executable from release packaging. The ordinary
`primeforge.exe`, source distribution scope and private MVP ZIP contain no CUDA
binary, toolkit header, library or NVIDIA license material. This is a conservative
component decision and does not infer any license conclusion from a process or
link boundary.
