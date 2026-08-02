# PrimeForge

PrimeForge is a C++23 research platform for auditable prime search, proof, verification, and coverage workflows. Correctness, evidence, and reproducibility take precedence over optimization.

The project currently makes no claim of superior performance, mathematical novelty, or discovery of a new prime. A probable prime is never reported as a proven prime.

## Current milestone: stage 9

- `primeforge_core`: portable core interfaces and foundational types;
- `primeforge-selftest`: compiler, OS, architecture, CPU, GPU-presence, and policy checks;
- `primeforge-tests`: dependency-free unit and contract tests.
- `primeforge-corpus-tests`: versioned correctness corpus, oracle-evidence, and exhaustive-reference regression.
- `primeforge-benchmark`: pre-engine measurement harness with raw CSV/JSON and conservative statistics;
- `primeforge-benchmark-tests`: protocol, scheduling, compatibility, and throttling-invalidation tests.
- `primeforge-sieve`: deterministic half-open interval prime generation/count CLI;
- `primeforge-sieve-tests`: exhaustive option-matrix, corpus, and 128-bit differential tests.
- `primeforge-work-units`: deterministic partition and coverage-report CLI;
- `primeforge-work-unit-tests`: SHA-256, canonical identity, fault detection, and atomic-checkpoint tests.
- `primeforge-family`: inspect, canonicalize, hash, and evaluate a bounded family definition;
- `primeforge-family-tests`: parser, AST, domain, canonicalization, and 2,000,000-case exact/modular gate.
- `primeforge-congruence`: compile and audit compressed factor-witness rules for `k*b^n+c`;
- `primeforge-congruence-tests`: scalar/compiled differential, fuzz, mutation, and zero-prime-elimination gate.

No external numerical, JSON, cryptographic, or GPU library is linked at this stage. Stage 8 uses a small original arbitrary-precision integer solely for family-definition correctness; it is not presented as a performance engine. The source audit selects only future integration modes and adds no runtime dependency.

The congruence rules and local compositeness proof are specified in `docs/CONGRUENCE_COMPILER.md`; the closed, bounded family grammar is in `docs/FAMILY_LANGUAGE.md`. The work-unit identity and mathematical coverage proof are in `docs/WORK_UNITS.md`; the stage-6 algorithm and toggle boundary are in `docs/SIEVE.md`. The state-of-the-art inventory is in `docs/STATE_OF_THE_ART.md`. The 68-case correctness corpus and its independent evidence are documented in `docs/CORPUS.md`. The claim-safe measurement rules are in `docs/BENCHMARK_PROTOCOL.md`. Pinned component evidence and unresolved gaps are indexed by `audits/INDEX.md`.

The licensing/provenance boundary is documented in `LICENSING.md`, `docs/PROVENANCE_POLICY.md`, and `licenses/DISTRIBUTION_MANIFEST.tsv`. Verify a distribution with:

```powershell
cmake --build --preset msvc-debug --target primeforge-distribution-check
```

## Build and test

Open Developer PowerShell for Visual Studio:

```powershell
cmake --preset msvc-debug
cmake --build --preset msvc-debug --parallel
ctest --preset msvc-debug --output-on-failure
& .\out\build\msvc-debug\primeforge-selftest.exe

cmake --preset msvc-release
cmake --build --preset msvc-release --parallel
ctest --preset msvc-release --output-on-failure
& .\out\build\msvc-release\primeforge-selftest.exe
```

Complete clean verification from any PowerShell prompt:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_all.ps1 -Clean
```

See `docs/BUILD_WINDOWS.md` and `docs/CANONICAL_JSON.md`.

Milestone evidence is kept in `docs/reports/`. Stage 9 compiles and validates factor rules but does not start a search campaign or call a primality engine. A survivor is never promoted to probable or proven prime. primesieve, FLINT, PARI/GP, and proth20 remain isolated local oracles only.

## Governance and licensing

Read `CHARTER.md`, `CLAIMS.tsv`, `DECISIONS.md`, and the versioned specification before interpreting any result. Original PrimeForge code is Apache-2.0. Research specifications, data, generated artifacts, and third-party material are not automatically covered; see `LICENSING.md`.
