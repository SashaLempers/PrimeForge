# PIVOT-00 audit report

Date: 2026-08-02. Status: implementation audit complete; validation and CI are
recorded by the PIVOT-00 milestone report.

## Secured baseline

The clean pre-pivot baseline is commit
`35dde4a02830e37dafdee05f5db911d810773859`, pushed to private `origin/main`.
It contains a deliberately incomplete external-adapter scaffold, clearly marked
as a backup rather than stage-12 acceptance. The historical specification remains
byte-preserved at SHA-256
`a87a3dddf710e3080c9a3f1247644ff9505ecbcf470358015e9edf19214a01c1`.

The audit covered governance, all stage reports, the specification, public
interfaces, CMake targets, test gates, benchmark collectors, scripts and retained
evidence schemas. Large raw benchmark tables were checked through their recorded
row counts, summaries and SHA-256 manifests; they were not reinterpreted as valid
performance evidence.

## Completed-stage classification

| Former stage | Pivot classification | Decision |
|---|---|---|
| 0 governance | acquired essential | Keep claims, decisions, negative results, provenance and private history. |
| 1 reproducible build | acquired essential, adapt | Keep C++23/CMake/CTest; make Windows/MSVC the fast path and Linux/GCC correctness CI. |
| 2 source audit | useful reference | Retain pinned research; revisit only target-relevant engines and versions before use. |
| 3 licensing | acquired essential | Keep individual dependency and redistribution gates unchanged. |
| 4 corpus | acquired essential | Keep as a minimum regression corpus; extend with GPU and target-family vectors later. |
| 5 benchmark laboratory | useful foundation, specialize | Reuse formats/statistics; replace missing telemetry with PIVOT-02 monitoring and watchdog. |
| 6 64-bit CPU sieve | acquired reference, specialize | Preserve scalar/portable paths; create separately gated Ryzen fast paths only after measurement. |
| 7 work units/recovery | acquired essential | Keep content addressing, coverage proof and atomic checkpoint replacement. |
| 8 family language | secondary but retained | Freeze broad grammar work; use only what the selected target family requires. |
| 9 congruence compiler | acquired essential | Retain factor-witness correctness; specialize layout and batching for the target. |
| 10 CPU option matrix | useful diagnostic reference | Retain negative/inconclusive evidence; do not spend more time on unmeasured generic variants. |
| 11 adaptive bound | scoped negative reference | Preserve the failed regimes; revisit only with a real downstream engine and valid telemetry. |
| 12 partial adapter backup | reference, selectively adapt | Keep isolation, raw output, hashes and conservative parsing; do not resume the generic adapter order. |
| 13-20 unfinished | superseded | Do not execute as the governing sequence. Relevant requirements are reintroduced explicitly in PIVOT-00..12. |

## Module disposition

Unchanged foundations: `core/status`, canonical SHA-256, corpus, `math/mul128`,
bounded family semantics, congruence witness validation, work units/checkpoints,
license distribution checks, scalar sieves and raw evidence retention.

Specialize: `core/system_info`, benchmark telemetry, CPU family-sieve dispatch,
memory allocation, scheduling/affinity, batch formats, the engine boundary and
the future CPU/GPU pipeline. Target code may be nonportable but must be optional
at configure time and reference-checked.

Reference-only unless reactivated by evidence: broad multi-family expansion,
generic portfolio routing, online adaptive-bound exploration, generic GPU big
integers, giant FFT/NTT work, historical engine adapters and unsupported legacy
sieves. No source is deleted during this pivot; removal requires a later small,
reviewable commit after dependency analysis.

## Explicit gaps

For the RTX 5080: exact driver/toolkit compatibility, `nvcc`, CUDA runtime and
compute capability, VRAM capacity/temperature availability, power/throttle
telemetry, pinned-memory limits, transfer bandwidth, kernel launch correctness and
CPU/GPU differential vectors.

For the Ryzen 9 9950X3D: Windows processor groups, core/cache topology, CCD/cache
asymmetry, reliable temperature/effective-clock/power sources, hardware-error
events, affinity safety, SIMD downclock behavior, SMT value and sustained-load
stability.

For 64 GB DDR5: actual module/channel/speed/timing data, available memory,
bandwidth and latency, NUMA exposure, page-fault/swap detection, safe working-set
headroom, pinned host-memory pressure and checkpoint I/O interaction.

## Guarantees preserved

No PRP/proof promotion, no performance claim from incomplete telemetry, no
invented hardware value, no TDP energy proxy, no unlicensed dependency, no
optimized result without scalar comparison, and no long campaign without tested
checkpoint/recovery and an independent watchdog.
