# PrimeForge

PrimeForge is a C++23 research platform for auditable prime search, proof, verification, and coverage workflows. Correctness, evidence, and reproducibility take precedence over optimization.

The project currently makes no claim of superior performance, mathematical novelty, or discovery of a new prime. A probable prime is never reported as a proven prime.

## Current milestone: stage 4

- `primeforge_core`: portable core interfaces and foundational types;
- `primeforge-selftest`: compiler, OS, architecture, CPU, GPU-presence, and policy checks;
- `primeforge-tests`: dependency-free unit and contract tests.
- `primeforge-corpus-tests`: versioned correctness corpus, oracle-evidence, and exhaustive-reference regression.

No external numerical, JSON, cryptographic, or GPU library is linked at this stage. The source audit selects only future integration modes; it does not add runtime dependencies.

The state-of-the-art inventory is in `docs/STATE_OF_THE_ART.md`. The 68-case correctness corpus and its independent evidence are documented in `docs/CORPUS.md`. Pinned component evidence and unresolved gaps are indexed by `audits/INDEX.md`.

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

Milestone evidence is kept in `docs/reports/`. Stage 4 adds a dependency-free automatic corpus regression. FLINT, PARI/GP, and proth20 are isolated local oracles only; no external numerical dependency is linked into or distributed with PrimeForge.

## Governance and licensing

Read `CHARTER.md`, `CLAIMS.tsv`, `DECISIONS.md`, and the versioned specification before interpreting any result. Original PrimeForge code is Apache-2.0. Research specifications, data, generated artifacts, and third-party material are not automatically covered; see `LICENSING.md`.
