# PrimeForge

PrimeForge is a C++23 prime-search engine being specialized for one performance target: AMD Ryzen 9 9950X3D, NVIDIA GeForce RTX 5080, 64 GB-class DDR5, and Windows/MSVC/CUDA. Correctness, evidence, reproducibility and safe recovery take precedence over optimization.

The project currently makes no claim of superior performance, mathematical novelty, or discovery of a new prime. A probable prime is never reported as a proven prime.

## Current milestone: end-to-end MVP

The immediate priority is a usable `primeforge.exe` that completes a small known
`k*2^n+1` campaign from configuration through proof, independent verification,
checkpoint/resume and final manifest. The governing plan and exact gap inventory
are `docs/mvp/ROADMAP.md` and `docs/mvp/INVENTORY.md`. Advanced autotuning and
additional CPU/GPU optimization are paused until the private MVP release passes.

Windows/MSVC is the product path. Linux/GCC remains mandatory portable-correctness
CI. CUDA stays available for the post-MVP performance path but is not allowed to
delay the first complete CPU campaign.

MVP-01 now provides the unified executable front door and exact campaign
inspection:

```powershell
& .\out\build\msvc-release\primeforge.exe selftest
& .\out\build\msvc-release\primeforge.exe inspect `
  --config examples\mvp\search.yaml
```

The strict schema and current command boundary are documented in
`docs/mvp/SEARCH_CONFIG.md`. `search`, `resume` and `verify` remain fail-closed
until the following MVP milestones validate their end-to-end semantics.

PIVOT-00 changes priorities and documentation, not mathematical results. The
pre-pivot baseline is secured at commit `35dde4a`. PIVOT-01 provides the fresh,
source-labelled `profiles/hardware_profile.json`, documented in
`docs/pivot/HARDWARE_PROFILE.md`; historical observations are not silently reused
as current measurements. The profile now discovers the installed CUDA Toolkit
13.3, exact compiler/runtime/driver versions, RTX 5080 compute capability and all
detection paths without making CUDA mandatory on CI hosts.

Prolonged benchmarks may eventually run for hours or days without an arbitrary
duration ceiling, but only after an independent watchdog, continuous monitoring,
documented thresholds, tested stop/recovery paths and progressive checkpoints are
implemented. See `docs/pivot/PERFORMANCE_PROTOCOL.md`.

## Preserved components

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
- `primeforge-family-sieve-tests`: one-factor CPU-option matrix and scalar-equivalence gate;
- `primeforge-family-sieve-benchmark`: randomized complete-path diagnostic collector.
- `primeforge-adaptive-bound-tests`: measured-selectivity model and PRP/proof-separation gate;
- `primeforge-adaptive-bound-benchmark`: calibration/validation study for five bound strategies.
- `hardware_monitor`: constant-memory live CPU/RAM/NVIDIA telemetry with explicit `UNKNOWN` fields;
- `benchmark_logger`: durable append-only JSONL campaign events;
- `checkpoint_manager`: atomic SHA-256-verified save/load for opaque engine progress;
- `benchmark_watchdog`: independent worker supervision with graceful and forced stop.
- `primeforge-cpu-topology`: Ryzen CPU-set, physical-core and L3-domain placement diagnostic.

No external numerical, JSON, cryptographic, CUDA or GPU library is linked at this
milestone. Stage 8 uses a small original arbitrary-precision integer solely for
family-definition correctness; it is not presented as a performance engine. The
partial external-process adapter is pre-pivot reference material, not an accepted
engine integration.

The target, pipeline, memory, CUDA, autotuning and measurement designs are under
`docs/pivot/`. The adaptive-bound decision remains in `docs/ADAPTIVE_BOUND.md`;
the CPU experiment boundary is in `docs/FAMILY_SIEVE.md`; the congruence rules and
local compositeness proof are in `docs/CONGRUENCE_COMPILER.md`; the closed,
bounded family grammar is in `docs/FAMILY_LANGUAGE.md`. Work-unit identity and
coverage are in `docs/WORK_UNITS.md`; the scalar/portable sieve is in
`docs/SIEVE.md`; audit evidence is indexed by `audits/INDEX.md`.

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

Regenerate the stable local hardware profile after a Release build:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File scripts\collect_hardware_profile.ps1 `
  -OutputPath profiles\hardware_profile.json `
  -SelfTestPath out\build\msvc-release\primeforge-selftest.exe
```

See `docs/BUILD_WINDOWS.md` and `docs/CANONICAL_JSON.md`.
The guarded long-run component contract and exact commands are in
`docs/pivot/RUNTIME_SAFETY.md`. No massive or prolonged benchmark has run yet.

Milestone evidence is kept in `docs/reports/`. Stage 11 remains a scoped negative
result: offline adaptation reproduced fixed-low and online exploration added cost
on its retained regimes. Old telemetry-incomplete timings keep
`performance_claim=NONE`. No search campaign has started. primesieve, FLINT,
PARI/GP and proth20 remain isolated local oracles only.

## Governance and licensing

Read `CHARTER.md`, `CLAIMS.tsv`, `DECISIONS.md`, and the versioned specification before interpreting any result. Original PrimeForge code is Apache-2.0. Research specifications, data, generated artifacts, and third-party material are not automatically covered; see `LICENSING.md`.
