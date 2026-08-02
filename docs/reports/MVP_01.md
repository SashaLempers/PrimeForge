# MVP-01 - Unified CLI and exact campaign plan

Date: 2026-08-02. Status: PASS.

MVP-01 adds the first product executable, `primeforge.exe`, with operational
`selftest` and `inspect` commands. It adds a closed dependency-free YAML subset,
an exact finite Proth-domain preflight, deterministic k-major/n-minor candidate
mapping, canonical campaign identity and reuse of the retained hashed work-unit
and coverage verifier.

The first attempted gate correctly rejected the example domain because
`k_max=31` and `n_min=3` violated `k < 2^n`. The example was conservatively
corrected to `n=5..14`, preserving the intended 160 candidates. This is a
configuration correction, not a filtered or silently changed search domain.

The clean local gate used MSVC 19.51.36252 in C++23 mode. Debug passed 30/30
tests in 42.31 s and Release passed 30/30 tests in 13.53 s. Both explicit
self-tests passed. Builds emitted no PrimeForge warning. The known campaign has
160 candidates, five exact work units and canonical configuration SHA-256
`32964b70c8c51c230b5fdfe776edf5c0531a8963d002ca63b209c86b1b015c41`.

The local inspection verified the pinned PARI/GP and FLINT executable hashes. It
did not execute them and made no primality, proof, novelty or performance claim.
A post-gate sensor snapshot reported CPU temperature and CPU power `UNKNOWN`,
GPU 53 C at 45.28 W, no reported throttling, 44,088,401,920 RAM bytes available
and 13,663 MiB VRAM free. This single observation is not a maximum-temperature
measurement or a benchmark.

## Contribution directe au logiciel final

This milestone turns previously separate libraries into the front door of the
final Windows product. A user can now validate the host, load one explicit
search family, prove its exact finite ownership plan and verify the provenance
of the two proof engines before computation starts.

The configuration and planning layer is complete for the restricted uint64 Proth
MVP. It should be extended only after the end-to-end release if another family or
larger integer domain is admitted. The executable itself is not complete:
MVP-02 must connect this plan to sieve, PRP, proof and independent verification.
