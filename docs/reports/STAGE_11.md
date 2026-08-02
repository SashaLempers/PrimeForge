# Stage 11 — adaptive sieve-bound study

## Implementation

PrimeForge now has an integer-only offline cost model, a marginal online stop rule, fixed low/medium/high fallbacks, a base-2 strong-PRP measurement workload, calibration/validation separation, conservative interval comparison, and a clean evidence collector.

The first test vector used 2047 as a pseudoprime, but the deliberately retained small-prime prefilter found factor 23 before Miller–Rabin. The corrected vector 1,373,653 reaches and passes the base-2 strong PRP while the deterministic 64-bit reference classifies it composite. No warning or test policy was changed.

The corrected local gate compiled with zero PrimeForge warnings and passed 16/16 tests. Debug completed in 36.57 s and Release in 3.62 s. `primeforge.adaptive_bound` passed in 0.03 s Debug and 0.02 s Release.

## Evidence state

Retained evidence must be produced from a clean implementation commit. Until then H2 remains unevaluated for the stage-11 regimes. Regardless of the raw timings, unavailable temperature, frequency, hardware-error, throttle, power, and energy telemetry prohibits a performance claim.

## Commands

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_all.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_stage11_benchmarks.ps1 -Repetitions 7
```
