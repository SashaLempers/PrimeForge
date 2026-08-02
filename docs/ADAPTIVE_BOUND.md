# Adaptive sieve-bound study

Stage 11 evaluates how far a family should be sieved before an explicitly separate probable-prime workload. It does not change a candidate's scientific status merely because that candidate survives.

## Model inputs

The model consumes cumulative observations containing an upper prime bound, complete sieve time, exact candidate count, and measured eliminations. Survivor counts are computed from those observations. No `1/q` selectivity assumption exists in the model; tests deliberately use an observation that contradicts such an assumption.

The offline selector minimizes the measured projected total

```text
complete sieve nanoseconds + measured survivors * measured next-test nanoseconds per survivor
```

using integer arithmetic and conservative saturation. The online selector accepts another block only while its observed marginal sieve cost is below the measured cost avoided by its additional eliminations. Invalid or nonmonotonic curves are rejected. A fixed-medium bound remains the fallback.

## Calibration and validation separation

The retained harness uses two calibration families and two distinct validation families over small, medium, and large finite domains. Bounds 7, 19, and 43 form the low, medium, and high fixed strategies. It compares those with offline-adaptive and online-adaptive strategies using at least seven randomized repetitions. Calibration rows, model choices, raw validation rows, summary intervals, and per-regime gate decisions are separate files.

Every complete validation sample includes congruence compilation, family sieving, exact survivor construction, and the next probable-prime workload. Energy is recorded as `UNKNOWN`, never inferred from TDP.

## Next-engine boundary

The measured next engine is an internal unsigned-64-bit base-2 strong probable-prime test. Its positive output is exactly `PROBABLE_PRIME`; it is never a proof. The regression corpus includes 1,373,653, which the base-2 PRP accepts while the independent deterministic 64-bit classifier proves it composite. This test makes a PRP-to-proof promotion a gate failure.

This internal workload exists to make the stage-11 cost interface measurable before external adapters are introduced in stage 12. It is not presented as a competitive PRP engine, and it supports no result or performance claim.

## Statistical decision

The comparison uses the distribution-free intervals from the stage-5 protocol. An adaptive strategy is `BETTER` only when its entire interval lies below the fixed-medium fallback interval. Overlap is `INCONCLUSIVE`. Because current stability and energy telemetry are unavailable, the retained stage decision remains claim-ineligible even if intervals happen not to overlap. A regime without a claim-eligible robust gain is marked `FAILED`, as required by the research specification.

Run the clean collector with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_stage11_benchmarks.ps1 -Repetitions 7
```
