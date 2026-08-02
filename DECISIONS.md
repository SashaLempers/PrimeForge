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
