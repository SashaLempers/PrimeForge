# Stage 11 — adaptive sieve-bound study

## Implementation

PrimeForge now has an integer-only offline cost model, a marginal online stop rule, fixed low/medium/high fallbacks, a base-2 strong-PRP measurement workload, calibration/validation separation, conservative interval comparison, and a clean evidence collector.

The first test vector used 2047 as a pseudoprime, but the deliberately retained small-prime prefilter found factor 23 before Miller–Rabin. The corrected vector 1,373,653 reaches and passes the base-2 strong PRP while the deterministic 64-bit reference classifies it composite. No warning or test policy was changed.

The corrected local gate compiled with zero PrimeForge warnings and passed 16/16 tests. Debug completed in 36.57 s and Release in 3.62 s. `primeforge.adaptive_bound` passed in 0.03 s Debug and 0.02 s Release.

## Retained evidence

The clean collector ran at implementation commit `0b2d62695bfffa5809604c89c47052a456c2639a`. It retained 126 calibration rows and 210 randomized validation rows: two calibration families, two distinct validation families, three sizes, three fixed bounds, two adaptive strategies, and seven repetitions.

Measured calibration eliminated 802/1024, 3217/4096, and 12871/16384 candidates at bound 7. The measured base-2 strong-PRP costs were 828, 897, and 874 ns per tested survivor. The offline projected model selected bound 7 in every regime. It therefore reproduced the fixed-low strategy rather than demonstrating an adaptive advantage. The online exploration selected bound 19 and paid for multiple cumulative sieve executions.

Diagnostic median complete times in nanoseconds were:

| Regime | fixed low | fixed medium fallback | fixed high | offline | online |
|---|---:|---:|---:|---:|---:|
| small | 2,321,400 | 3,292,100 | 4,893,900 | 2,336,200 | 5,591,000 |
| medium | 2,467,900 | 3,462,000 | 5,061,300 | 2,449,100 | 5,937,600 |
| large | 3,481,500 | 4,813,900 | 6,756,500 | 3,452,800 | 8,110,200 |

These are recorded diagnostic observations, not performance claims. Every validation row states `energy_joules=UNKNOWN`, `performance_valid=NO`, and `performance_claim=NONE`. Temperature, effective frequency, throttling, and hardware-error telemetry are also unavailable.

Evidence SHA-256 values are:

- `environment.tsv`: `291ce179bb746654c0210ff3365204a492a7d9b5feb0cb9bbe880fba5efde680`;
- `calibration.tsv`: `4fe65f7636ddfd1ffecc493fddf63bdbd6467cfa5827f4155eaa76e729fae208`;
- `model.tsv`: `4c35dfa900dcff9e0d42f06aac9015f3e0d4f99edfe232eb9dda573406ab931d`;
- `raw.tsv`: `f88624005a1b13d8123dddd2d18b735335c60708b322f9731e47688529c3a889`;
- `summary.tsv`: `39d1d0570d2be06a61a71a1f36d932beaa2c4807ce84be9313e9730c896db837`;
- `gate.tsv`: `e1ccb4435ca189137cacedfd95a611979174d586a83553d1b7679a21f86aaa8d`.

All evidence files are UTF-8/LF with zero carriage-return bytes.

## Scientific gate

H2 is `FAILED` for the retained small, medium, and large validation regimes. Offline adaptation merely selected the same low fixed bound in all three, online exploration was not competitive, and no sample is claim-eligible. This scoped failure does not assert that adaptive bounds can never help another family or a materially more expensive downstream engine. The fixed-medium strategy remains the documented conservative fallback; fixed-low is the observed diagnostic choice for these bounded fixtures only.

## Commands

```text
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_all.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_stage11_benchmarks.ps1 -Repetitions 7
```
