# Stage 10 — CPU family-sieve experiment matrix

## Implementation status

PrimeForge now supplies the stage-10 family-sieve API, an immutable scalar reference, eighteen one-factor configurations including the baseline, a benchmark executable, and a clean-worktree evidence script. The unit gate checks all fifteen requested experiment categories and requires exact equality with the scalar elimination bitset.

The first build attempt stopped because the new Windows source redundantly defined two macros already supplied by CMake. The duplicate definitions were removed; `/W4`, `/WX`, and `/permissive-` remain unchanged. The corrected local gate compiled with zero PrimeForge warnings and passed all 15 tests: Debug in 36.44 s and Release in 3.58 s. `primeforge.family_sieve` passed in 0.85 s Debug and 0.11 s Release.

## Retained diagnostic evidence

The clean-worktree collector ran at implementation commit `bfac505a1097ca81e63c2248ded35db0713fe66c`. It retained 357 complete samples: 17 configurations, three regimes, and seven fixed-seed randomized repetitions. Every configuration produced the same result hash within its regime. Candidate/elimination counts were 1024/353, 4096/1438, and 16384/5836.

The diagnostic medians expose a segment-parallelism bottleneck. With 16 physical workers, the L1-scale setting creates 1, 4, and 16 segments across the three regimes; the L2 baseline creates 1, 1, and 2, while the L3 setting creates one. The observed large-regime medians were 3,753,700 ns (L1-scale), 11,227,500 ns (L2-scale), and 20,896,100 ns (L3-scale). These are recorded observations, not performance claims. The uncompressed path changed the operation mix from 1,441,792 rule-class checks plus 9,240 modular checks to 180,224 prime/candidate checks plus 90,112 modular checks.

AVX2 and AVX-512 dispatch were applied, the bounded CRT template was applied, and Windows affinity was applied. The large-page probe returned unavailable. Temperature, effective frequency, power, energy, hardware errors, throttling, sampling profiler, and hardware counters are all `UNKNOWN`.

Evidence SHA-256 values are:

- `environment.tsv`: `3bd2ada52aec71f59f29f9f79ce0e8144be66c24f3923c03194e3494971a9b12`;
- `raw.tsv`: `9b9509e60de1e47aaf6570537fe71ab1efae2ac8d2181020d3daac5629800a36`;
- `summary.tsv`: `658a1bb80504a6da6713445f61480d501d145d425d7886e4c5f8313cd1f2fdf8`.

All retained files are UTF-8/LF and contain zero carriage-return bytes.

## Scientific gate

Correctness and bottleneck diagnosis pass. The requested optimal configuration per regime is **INCONCLUSIVE**, because every sample is `performance_valid=NO` and `performance_claim=NONE` under the stage-5 protocol. The reference-safe configuration therefore remains the default. PrimeForge does not turn the diagnostic medians into a speedup or optimum assertion.

## Safety and interpretation

- Every elimination has a reconstructed proper prime factor; survivors remain untested.
- AVX dispatch is conditional on OS-enabled CPUID capability.
- Huge pages are a reversible availability probe, never a requirement.
- Windows affinity is temporary and restored at worker completion.
- Electrical power and energy remain `UNKNOWN`; TDP is not used.
- No external engine or library was installed or integrated.

## Commands

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_all.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_stage10_benchmarks.ps1 -Repetitions 7
```
