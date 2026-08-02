# PrimeForge Decision Register

Decisions are conservative defaults. A later change must add a new decision that supersedes the previous one; history is not rewritten.

## D-0001 — Three independent result axes

**Status:** Accepted — 2026-08-02

Primality, verification, and novelty are stored independently using the values defined in `CHARTER.md`. Text in a filename or external log never promotes a status.

## D-0002 — Restricted canonical JSON

**Status:** Accepted — 2026-08-02

PrimeForge will define a UTF-8, deterministic, no-floating-point JSON subset. Keys are unique, normalized, and deterministically ordered. Mathematical large integers are canonical decimal strings. Hashes cover the exact canonical bytes and must be identical on Windows and Linux for identical logical content. The precise wire rules are versioned before the first hashed work unit is implemented.

## D-0003 — Internal SHA-256 abstraction

**Status:** Accepted — 2026-08-02

The core exposes a backend-independent 32-byte digest and injectable SHA-256 provider. OpenSSL is not added until a concrete cross-platform backend requirement justifies it. Governance hashes may initially be produced by trusted platform tools and recorded with exact commands.

## D-0004 — License decisions are per dependency

**Status:** Accepted — 2026-08-02

Apache-2.0 covers only original PrimeForge code. Each dependency requires an individual decision covering source, exact revision, license text and hash, provenance, redistribution, linking or process mode, and compatibility. Running software as an external process does not automatically resolve license obligations.

## D-0005 — Unknown energy remains unknown

**Status:** Accepted — 2026-08-02

Unavailable power or energy is recorded as `UNKNOWN`. PrimeForge never derives real consumption from TDP. Estimates, if later used, must be explicitly labelled and must not be mixed with measurements.

## D-0006 — Hardware settings require measured stability

**Status:** Accepted — 2026-08-02

Stock settings are the initial baseline. Alternative settings are eligible only after prolonged stress tests, acceptable temperatures, no hardware errors, and zero unexplained computational divergence. The exact setting is part of benchmark provenance.

## D-0007 — No unbenchmarked performance claim

**Status:** Accepted — 2026-08-02

Performance language requires reproducible end-to-end benchmarks, raw results, versions, workload hashes, repetitions, environment metadata, and uncertainty. Until then the relevant claim is `UNKNOWN` or `HYPOTHESIS`.

## D-0008 — Probable primality is not proof

**Status:** Accepted — 2026-08-02

`PROBABLE_PRIME` cannot be displayed or exported as `PROVEN_PRIME`. Proof status requires a rigorous accepted method and successful certificate or deterministic verification under the documented policy.

## D-0009 — Initial implementation order

**Status:** Accepted — 2026-08-02

PrimeForge follows numbered research stages 0 through 20. Conditional stages are evaluated explicitly; a rejected gate produces a documented negative result instead of being omitted.

## D-0010 — C++23 compiler baseline

**Status:** Accepted — 2026-08-02

The build requests the CMake `cxx_std_23` compile feature and disables language extensions. PrimeForge records the compiler actually used instead of requiring a particular MSVC patch version without a technical need. Runtime reporting accepts `__cplusplus >= 202100L` as C++23 mode because supported GCC and Clang releases use that value while current MSVC reports `202400L`.

## D-0011 — Immutable CI action references

**Status:** Accepted — 2026-08-02

Third-party CI actions are pinned to immutable commits. Their license, provenance, redistribution decision, and integration mode are tracked like every other dependency. The stage 1 workflow uses `actions/checkout` only as a CI service component and does not link or redistribute it with PrimeForge.

## D-0012 — Audit evidence levels are not interchangeable

**Status:** Accepted — 2026-08-02

DOC_VERIFIED records a check against pinned official documentation. SOURCE_AUDITED additionally records inspection of the pinned source. REPRODUCED is reserved for an exact local build or official executable whose relevant supplied tests or simple case actually ran. NOT_RUN remains visible and never inherits a stronger level from a related project.

## D-0013 — Stage 2 integration boundary

**Status:** Accepted — 2026-08-02

primesieve is the only stage 2 component approved as a future linked dependency, subject to the stage 3 distribution gate. GMP and FLINT are linked candidates deferred until an oracle requires them. PARI/GP, Prime95/gwnum, gpuowl, Mlucas, Genefer22, OpenPFGW, GMP-ECM, mfaktc, and mfakto are external-adapter candidates. CGBN, Frey-PRPLL, proth20, and historical sieves are references. LLR2 and PSieve-CUDA are rejected for new integration.

## D-0014 — Missing or ambiguous license blocks selection

**Status:** Accepted — 2026-08-02

PRST has useful modern Proth/Riesel functionality, but no unambiguous project-wide license grant was found at the pinned revision. It remains REFERENCE_ONLY and cannot be built into a distributed PrimeForge workflow. The same conservative rule applies to historical NewPGen/sr2sieve archives whose exact provenance or license remains unknown.

## D-0015 — External work services require separate authorization

**Status:** Accepted — 2026-08-02

PrimeGrid and GIMPS are treated as external services, not dependencies. A local adapter or file parser does not authorize requesting work, claiming an assignment, transmitting results, or asserting novelty/coverage. Those actions require the explicit user authorization already defined by the project.

## D-0016 — Stage 2 diagnostic timing is not a benchmark

**Status:** Accepted — 2026-08-02

The short primesieve process timings satisfy the stage 2 request for a simple observed case only. They include process startup, have three repetitions, and do not use the stage 5 protocol. They cannot support comparative, throughput, energy, or “best” claims.

## D-0017 — Apache-2.0 retained for original PrimeForge code

**Status:** Accepted — 2026-08-02

After reviewing the standard Apache guidance and GNU compatibility notes, Apache-2.0 remains the unchanged license for original PrimeForge source/build logic. LICENSE and NOTICE accompany distributions. This is a governance decision rather than legal advice and grants no rights in specifications, data, generated artifacts, or third-party material.

## D-0018 — Distribution classification and redistribution are independent

**Status:** Accepted — 2026-08-02

The machine-readable distribution manifest separates relationship (ORIGINAL, LINKED, EXTERNAL, REFERENCE, REJECTED, CI_ONLY, or DEVELOPMENT_TOOL) from redistribution (YES or NO). An external process never implies license approval. Every YES row requires a present, non-empty, hash-pinned license and any declared notice file.

## D-0019 — Missing-license behavior is a tested gate

**Status:** Accepted — 2026-08-02

primeforge-distribution-check validates the real manifest. CTest also runs a deliberately invalid fixture under WILL_FAIL, proving that a missing required license returns failure. Future distribution automation must invoke this target before packaging.

## D-0020 — Corpus outcomes are separate from engine status

**Status:** Accepted — 2026-08-02

The correctness corpus uses `REJECTED_NON_CANDIDATE`, `COMPOSITE`, and `PROVEN_PRIME` outcomes. This prevents 0 and 1 from being called composite. Corpus validation rejects such values before an engine status is assigned; it does not modify or conflate the three independent runtime status axes.

## D-0021 — Oracles remain outside the product dependency graph

**Status:** Accepted — 2026-08-02

PARI/GP, FLINT, and proth20 are independent local validation tools. Their binaries and DLLs stay under ignored `out/oracles`, are never linked into `primeforge_core`, and are not redistributed. Tracked oracle adapters contain only original PrimeForge glue code. Every verdict is stored with exact versions and hashes.

## D-0022 — Conflicting license metadata is quarantined

**Status:** Accepted — 2026-08-02

Pinned FLINT 3.6.0 upstream source states LGPL-3.0-or-later, while the vcpkg package SPDX record declares GPL-3.0-only. PrimeForge does not choose the more permissive interpretation. The local dynamic oracle is treated conservatively as non-redistributable and cannot enter a release until a later distribution review resolves the exact binary obligations.

## D-0023 — Benchmark arithmetic and confidence output are integer-only

**Status:** Accepted — 2026-08-02

Raw phase durations are integer nanoseconds. Stage-5 summaries use lower median, median absolute deviation, minimum, maximum, and a conservative distribution-free median interval. JSON contains no floating-point values. A smaller median is not called a gain when intervals overlap or when the difference is within the recorded noise floor.

## D-0024 — Missing telemetry disables performance claims

**Status:** Accepted — 2026-08-02

Temperature, frequency, power, wall energy, and throttling data are never inferred. Missing values are `UNKNOWN` or `UNAVAILABLE`, and the affected samples remain useful only for validating the harness. Hardware errors, reported throttling, unacceptable temperature, or frequency collapse invalidate samples automatically under an injected policy.

## D-0025 — Sieve intervals and concurrency are deterministic

**Status:** Accepted — 2026-08-02

Prime generation uses half-open intervals `[begin,end)`. Each segment has exclusive storage and a stable numeric index; workers may finish in any order, but results are concatenated by index. Segment size, thread count, wheel 30, bit packing, buckets, prefetch, and bucket layout are independently selectable and must produce identical ordered output.

## D-0026 — Stage-6 optimizations require multi-range evidence

**Status:** Accepted — 2026-08-02

The simplest byte-storage segmented path is the default. Wheel 30, bit packing, bucket layout, and prefetch remain implemented as experimental toggles. Wheel 210 and AVX2/AVX-512 dispatch are not retained because no claim-eligible, telemetry-complete benchmark demonstrated a gain on multiple ranges. Hardware capability alone is not evidence of an end-to-end improvement.

## D-0027 — Compiler-specific 128-bit arithmetic stays internal

**Status:** Accepted — 2026-08-02

The public API uses pairs of 64-bit words. MSVC x64 uses `_umul128`/`_udiv128`; GCC and Clang may use their native unsigned 128-bit extension internally; every build retains a portable 32-bit-limb and add/double reference. Boundary and fixed-seed differential tests are mandatory before later modular algorithms may use the fast path.

## D-0028 — Work-unit identity is content-addressed

**Status:** Accepted — 2026-08-02

`work_unit_id` is the lowercase SHA-256 of the canonical work-unit object with its id omitted. Constraints are sorted and deduplicated before hashing, parameter intervals are half-open, and large values are decimal strings. The stage-7 serializer rejects non-ASCII user strings; ASCII is an NFC-safe strict subset, while silent incomplete Unicode normalization is not acceptable.

## D-0029 — SHA-256 is internal and test-vector gated

**Status:** Accepted — 2026-08-02

Stage 7 technically requires real SHA-256 for identifiers, so PrimeForge supplies an internal portable backend behind the existing injectable interface. It passes standard empty, short, and multi-block vectors plus an independently calculated work-unit vector. OpenSSL remains unjustified and absent.

## D-0030 — Checkpoint replacement preserves the last durable state

**Status:** Accepted — 2026-08-02

Checkpoint updates use a same-directory `.new` file, native durable flush, close, and atomic replace. Injected failures before flush and before replace must leave the previous target unchanged. The guarantee applies to tested local filesystems; remote/network filesystem semantics are not assumed.

## D-0031 — Family language v1 is closed and finite

**Status:** Accepted — 2026-08-02

Family definitions contain only bounded signed-64-bit parameters, integer expressions, controlled constant-base powers, named constraints, a primality objective, and an explicit proof policy. There are no loops, arbitrary calls, floats, unbounded domains, I/O, or nondeterministic operations. Multiplication by a nonconstant expression requires a direct parameter factor named by `allow_product`.

## D-0032 — Canonical equivalence is explicit rather than symbolic

**Status:** Accepted — 2026-08-02

Stage-8 canonical identity covers statement/parameter/constraint ordering, duplicate constraints, commutation of a single addition or multiplication node, commutation of gcd operands, and subtraction-as-negation. It intentionally does not claim general algebraic equivalence, reassociation, or distributivity. The exact supported set is documented and golden-vector gated on Windows and Linux.

## D-0033 — Definition arithmetic remains dependency-free and size-gated

**Status:** Accepted — 2026-08-02

PrimeForge uses a small original signed base-10^9 integer for exact family-definition evaluation and its existing 128-bit modular primitive for modular evaluation. This code is a correctness baseline, not a performance engine. Exact values and exact constraint operands are rejected before construction when the conservative domain-derived estimate exceeds 10,000,000 bits. GMP and FLINT remain external until a later stage technically justifies an individually licensed integration.

## D-0034 — Localized MSVC dependency prefixes are normalized narrowly

**Status:** Accepted — 2026-08-02

Ninja's MSVC dependency extraction depends on the exact `/showIncludes` prefix. On the French host, CMake 4.3 detected the prefix but double-encoded its non-breaking spaces. PrimeForge repairs only that byte sequence after compiler detection and leaves all correctly detected locale prefixes unchanged. The normalization never relaxes `/W4`, `/WX`, `/permissive-`, Application Control, or any test gate.
