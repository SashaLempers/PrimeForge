# Stage 10 — CPU family-sieve experiment matrix

## Implementation status

PrimeForge now supplies the stage-10 family-sieve API, an immutable scalar reference, eighteen one-factor configurations including the baseline, a benchmark executable, and a clean-worktree evidence script. The unit gate checks all fifteen requested experiment categories and requires exact equality with the scalar elimination bitset.

The first build attempt stopped because the new Windows source redundantly defined two macros already supplied by CMake. The duplicate definitions were removed; `/W4`, `/WX`, and `/permissive-` remain unchanged. The corrected local gate compiled with zero PrimeForge warnings and passed all 15 tests: Debug in 36.44 s and Release in 3.58 s. `primeforge.family_sieve` passed in 0.85 s Debug and 0.11 s Release.

## Scientific gate

The optimization gate is not yet claimed. Retained timing must be generated from a clean implementation commit. Even after collection, the current lack of reliable temperature, effective-frequency, throttle, hardware-error, and energy telemetry means the data is diagnostic only under the stage-5 protocol. PrimeForge will not name a production optimum from ineligible samples.

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
