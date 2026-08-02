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
