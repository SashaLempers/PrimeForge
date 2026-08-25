# PrimeForge

PrimeForge is a C++23 prime-search engine being specialized for one performance target: AMD Ryzen 9 9950X3D, NVIDIA GeForce RTX 5080, 64 GB-class DDR5, and Windows/MSVC/CUDA. Correctness, evidence, reproducibility and safe recovery take precedence over optimization.

The project currently makes no claim of superior performance, mathematical novelty, or discovery of a new prime. A probable prime is never reported as a proven prime.

## Current milestone: private Windows MVP release

The immediate priority is a usable `primeforge.exe` that completes a small known
`k*2^n+1` campaign from configuration through proof, independent verification,
checkpoint/resume and final manifest. The complete engine path and recovery gate
now pass locally and in private Windows/Linux CI. MVP-04 packages that executable
without redistributing either external proof engine. The governing plan and exact
inventory are `docs/mvp/ROADMAP.md` and `docs/mvp/INVENTORY.md`.

The private prerelease `v0.1.0-mvp` is attached to commit `8c7a304`. Its Windows
ZIP SHA-256 is
`EE050AB7819E0C6C66DB956AA2AE222B31266B0994D563221A466B1CFD49F710`.
The asset was downloaded again and passed the closed package verifier. The
repository and release remain private.

PIVOT-10 now routes surviving Proth candidates through PrimeForge's native exact
certificate path before independent FLINT verification. The pinned PARI/GP path
is retained only as a fail-closed fallback when the bounded native witness search
returns `UNTESTED`. The first limited known-range gate stopped cleanly at 37/160,
resumed to 160/160 and independently verified 34 proven primes plus 126
composites. Novelty remains `NOT_CHECKED`; this is not a discovery campaign.

PIVOT-11 adds a Jacobi prefilter to the bounded native prover. On the 34 native
proofs used by the known campaign, the exact modular-exponentiation count falls
from 132 to 34 with byte-identical certificates. This is an algorithmic operation
reduction only; no wall-time or fastest-engine claim is made.

Target-local campaign safety now reads fresh CPU package temperature, power and
clock from the already-installed L-Connect service over loopback, alongside the
existing NVIDIA, RAM and VRAM telemetry. Missing, stale or invalid CPU data stays
`UNKNOWN`, and the independent watchdog can require both sensors and stop above
92 °C. It also gates recent WHEA events, available RAM/VRAM and nonzero worker
exit codes. No L-Connect or HWiNFO binary is linked, copied or redistributed.
This does not itself authorize the pending 24-hour PIVOT-12 campaign.

Post-release PIVOT-03 work now keeps proper-factor evidence in the original
sieve traversal and lets aligned k-major segments write disjoint canonical
bitset words directly. That same path now enumerates the compiled forbidden
residue pairs instead of scanning every candidate for every rule. The released
archive is unchanged; the optimized source
path remains under Debug/Release, differential and private-CI validation before
a later release. No timing is promoted to a performance claim.

PIVOT-03 CPU selection is complete but `INCONCLUSIVE`: during those 630 disjoint
calibration/validation executions, CPU temperature and package power were not
available and profile ranks varied by regime. The later telemetry integration
does not retroactively make those historical timings performance-valid. The
safe one-thread product profile is unchanged. Work therefore advances to the
bounded CUDA validation milestone rather than inventing a CPU optimum.

Windows/MSVC is the product path. Linux/GCC remains mandatory portable-correctness
CI. CUDA stays available for the post-MVP performance path but is not allowed to
delay the first complete CPU campaign.

### Windows graphical launcher

The Windows build produces `primeforge-launcher.exe` beside `primeforge.exe`.
Double-click it to start the configured campaign. It automatically starts a new
search, resumes an authenticated checkpoint, or verifies a completed result
ledger. A CUDA-enabled build uses `auto` routing between CPU and CUDA.

The **Arrêter proprement** button, `Ctrl+C` in the launcher window, and closing
the window during a search all use the same cooperative stop protocol. The
engine finishes its current bounded work, durably commits the result prefix and
checkpoint, and exits with `search.status=STOPPED`. The next launch clears the
one-shot stop request and resumes that exact checkpoint automatically.

After a successful search or resume, final verification runs automatically in
the same window. If verification identifies the exact legacy campaign-identity
mismatch caused by an older PrimeForge pipeline, the launcher preserves that
directory as `<campaign>.incompatible-<UTC timestamp>`, starts a clean campaign,
and verifies the new result. Other verification failures remain fail-closed.

```powershell
& .\out\build\msvc-cuda-release\primeforge-launcher.exe

# Select another campaign configuration:
& .\out\build\msvc-cuda-release\primeforge-launcher.exe `
  --config C:\path\to\search.yaml
```

MVP-01 now provides the unified executable front door and exact campaign
inspection:

```powershell
& .\out\build\msvc-release\primeforge.exe selftest
& .\out\build\msvc-release\primeforge.exe inspect `
  --config examples\mvp\search.yaml
```

The strict schema is documented in `docs/mvp/SEARCH_CONFIG.md`. The complete
MVP-02 search/proof path is now available:

```powershell
& .\out\build\msvc-release\primeforge.exe search `
  --config examples\mvp\search.yaml

# Deterministic cooperative-stop gate (useful for recovery testing):
& .\out\build\msvc-release\primeforge.exe search `
  --config benchmarks\pivot10\known_proth_small.yaml --stop-after 37
```

The output/status contract is in `docs/mvp/RESULTS.md`. MVP-03 adds durable
checkpoints, deterministic finalization and independent verification:

```powershell
& .\out\build\msvc-release\primeforge.exe resume `
  --checkpoint out\campaigns\known-proth-small\campaign.checkpoint.json

& .\out\build\msvc-release\primeforge.exe verify `
  --result out\campaigns\known-proth-small\results.jsonl
```

`Ctrl+C` requests a clean stop after the current candidate. `resume` validates
the checkpoint and the exact committed result prefix before continuing. A
completed campaign contains `coverage_report.json` and `MANIFEST.sha256`; verify
fails closed on a missing, extra or modified file. The complete recovery and
verification contract is in `docs/mvp/RECOVERY_AND_VERIFICATION.md`.

Build and verify the closed Windows package after a Release build:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File scripts\package_mvp.ps1 -Version 0.2.0-launcher
```

The package contains `primeforge.exe`, configuration, documentation and original
PrimeForge license material only. PARI/GP, FLINT and FLINT's runtime DLLs are
listed with exact hashes in `packaging/ORACLES.md` and remain separately supplied
local tools. After placing them at those paths, the packaged known campaign is a
single command:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\run_known_campaign.ps1
```

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
- `primeforge-bench`: product-path PRP benchmark with versioned profiles, integer stage metrics,
  stable CSV/JSONL output, and exact verdict hashes;
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
- `primeforge_cuda`: optional local RTX 5080 modular-batch backend with persistent buffers and an internal CUDA stream;
- `primeforge-cuda-modular-tests`: 101,000-vector CPU/GPU differential and contract gate.
- `primeforge_pipeline`: bounded multi-backend submission, CPU verification, ordered durable ledger and authenticated resume;
- `primeforge-modular-pipeline-tests`: portable 1/2/3-buffer, stop/recovery and corruption gate;
- `primeforge-cuda-pipeline-tests`: real RTX 5080 multi-stream interruption/resume differential gate.

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

## Product baseline benchmark

The frozen 64-bit reference is tag `baseline-u64-b0a3b96`. The measured product
path reports generation, congruence compilation, sieve, packing, H2D, CUDA
kernel, D2H, CPU PRP, proof, independent verification, result I/O, checkpoint,
and total time as integer nanoseconds. Raw rows follow
`benchmarks/schemas/raw-v1.md`.

The retained raw evidence and conclusions are in
[`docs/reports/COMMIT_A.md`](docs/reports/COMMIT_A.md) and
[`benchmarks/baselines/commit-a/`](benchmarks/baselines/commit-a/).

The first retained optimization removes redundant external-installation hashes.
Its before/after evidence is in
[`docs/reports/OPTIMIZATION_01.md`](docs/reports/OPTIMIZATION_01.md) and
[`benchmarks/baselines/optimization-01/`](benchmarks/baselines/optimization-01/).

The second retained optimization runs independent FLINT checks in a bounded
eight-process wave. See
[`docs/reports/OPTIMIZATION_02.md`](docs/reports/OPTIMIZATION_02.md) and
[`benchmarks/baselines/optimization-02/`](benchmarks/baselines/optimization-02/).

The third retained optimization aligns durable ledger flushes with authenticated
checkpoint boundaries. See
[`docs/reports/OPTIMIZATION_03.md`](docs/reports/OPTIMIZATION_03.md) and
[`benchmarks/baselines/optimization-03/`](benchmarks/baselines/optimization-03/).

The fourth retained optimization classifies ordered FLINT survivors in bounded
single-process batches. See
[`docs/reports/OPTIMIZATION_04.md`](docs/reports/OPTIMIZATION_04.md) and
[`benchmarks/baselines/optimization-04/`](benchmarks/baselines/optimization-04/).

The fifth retained optimization overlaps CPU Proth witness search with the
independent FLINT batch while keeping all durable writes ordered. See
[`docs/reports/OPTIMIZATION_05.md`](docs/reports/OPTIMIZATION_05.md) and
[`benchmarks/baselines/optimization-05/`](benchmarks/baselines/optimization-05/).

The sixth retained optimization uses Windows CNG behind the internal SHA-256
interface and sized binary reads for product provenance checks. See
[`docs/reports/OPTIMIZATION_06.md`](docs/reports/OPTIMIZATION_06.md) and
[`benchmarks/baselines/optimization-06/`](benchmarks/baselines/optimization-06/).

Run a focused CPU PRP sample:

```powershell
& .\out\build\msvc-release\primeforge-bench.exe run `
  --profile benchmarks\profiles\s64_prp_65536.json `
  --backend cpu `
  --output out\benchmarks\manual-cpu `
  --warmup 3 `
  --repetitions 7
```

Run the complete short CPU/CUDA/automatic baseline and generate summaries and
PNG/SVG reports without installing Python:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File scripts\run_commit_a_baseline.ps1
```

The 256–4096-bit profiles are deliberately marked `implemented=false` until
the multiprecision commits provide real backends. `primeforge-bench` fails
closed if one of those profiles is requested today.

Optional target-machine CUDA 13.3 validation (not part of the product package):

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File scripts\run_cuda_validation.ps1 -Clean
```

This preset requires the pinned local official toolkit and RTX 5080. CUDA stays
disabled in the normal Debug/Release and Linux paths. The validation executable
is local-only and excluded from release packaging. The script discovers Visual
Studio, loads its developer environment and stops on the first failed command.

Regenerate the stable local hardware profile after a Release build:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File scripts\collect_hardware_profile.ps1 `
  -OutputPath profiles\hardware_profile.json `
  -SelfTestPath out\build\msvc-release\primeforge-selftest.exe
```

See `docs/BUILD_WINDOWS.md` and `docs/CANONICAL_JSON.md`.
The measured Proth20 phase decomposition and the resulting optimization
decision are documented in `docs/reports/BREAKTHROUGH_JALON_A.md`.
The retained direct-inverse/segmented Proth sieve, measured depth decision and
same-process Proth20 plan cache are documented in
`docs/reports/BREAKTHROUGH_JALON_B.md`.
The bounded invariant-context prototype is exact but rejected after a measured
2.320800% complete-throughput regression; see
`docs/reports/BREAKTHROUGH_JALON_C.md`. It is not enabled in the production
Proth20 build. The native multi-candidate NTT prototype then passes its exact
end-to-end gate: B=8 reaches 3,100.474460 complete candidates/hour on the
retained 20,000-digit corpus, with all witnesses, RES64 values and Gerbicz
checks identical to the two-process reference. See
`docs/reports/BREAKTHROUGH_JALON_D.md`. The patch remains experimental pending
checkpoint-safe scheduler integration.
The bounded 20k-to-100k digit scaling study and the retained adaptive B32
reduction/poly2int work are documented in
`docs/reports/NATIVE_DIGIT_SCALING_AND_ADAPTIVE_GPU_OPTIMIZATION_2026-08-14.md`.
The direct A/B/B/A bookends improve complete validated throughput by 45.33% at
20k digits and 28.02% at 100k digits with identical witnesses, RES64 values and
Gerbicz PASS. This is `100k_digits`, not 100,000 candidates, and no discovery
campaign was run.
The guarded long-run component contract and exact commands are in
`docs/pivot/RUNTIME_SAFETY.md`. No massive or prolonged benchmark has run yet.

The large-scaling optimization removes the native campaign's quadratic result
rewrites with a durable append-only journal and checkpoint hash chain. Optional
`--batch-size auto` dispatches the measured RTX 5080 regimes by transform length
(B32 through 65,536, B16 through 131,072, B12 at 262,144 and B6 at 524,288) and uses B1 for
unmeasured hardware or transforms. Exact A/B/B/A persistence measurements and
the rejected bounded GPU prototypes are in
`docs/reports/LARGE_SCALING_OPTIMIZATION_20260822.md`.

The measured 500k-digit production profile uses B12 with
`256_8 sq_1024 p2i_8_64` and a radix-256/WG128 reduction on the RTX 5080. It
improves the same-session complete-throughput baseline by 15.648537%, reproduces
121.540999 candidates/hour from a fresh pinned-source build, and keeps exact
RES64 records and Gerbicz PASS. Scope, rejected variants and raw evidence indexes
are documented in `docs/reports/500K_GPU_SCALING_OPTIMIZATION_20260823.md`.

The 500k campaign-time profile raises the exact sieve beyond 4 billion and
partitions each prime interval deterministically across CPU workers. Progressive
closed-corpus segments have been measured through 2,048 trillion: 54,633 GPU
survivors remain versus 87,124 at 4G. The current 465.468-hour campaign estimate
is derived from measured sieve segments plus the separately measured GPU rate;
it is not presented as an executed discovery campaign. See
`docs/reports/500K_DEEP_SIEVE_OPTIMIZATION_20260823.md`.

The next NTT cliff is now measured rather than extrapolated. On the closed
830k-digit corpus, the RTX 5080 B6 profile with `256_4 sq_2048 p2i_8_64` and
radix-256/WG128 reaches 29.617120 complete candidates/hour versus 10.699900 for
the former B1 fallback (2.767981x), with exact RES64 records and Gerbicz PASS.
The bounded activation, rejected batch/plan variants and evidence are in
`docs/reports/NTT524288_SCALING_OPTIMIZATION_20260823.md`.

Milestone evidence is kept in `docs/reports/`. Stage 11 remains a scoped negative
result: offline adaptation reproduced fixed-low and online exploration added cost
on its retained regimes. Old telemetry-incomplete timings keep
`performance_claim=NONE`. Only small known correctness campaigns have run; no
novel or prolonged search campaign has started. primesieve 12.15 is now a
pinned, statically linked BSD-2-Clause dependency of the Proth discovery sieve;
FLINT, PARI/GP and proth20 remain isolated local oracles only.

## Governance and licensing

Read `CHARTER.md`, `CLAIMS.tsv`, `DECISIONS.md`, and the versioned specification before interpreting any result. Original PrimeForge code is Apache-2.0. Research specifications, data, generated artifacts, and third-party material are not automatically covered; see `LICENSING.md`.
