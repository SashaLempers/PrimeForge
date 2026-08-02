# PrimeForge Charter

## Mission

Build an auditable, reproducible and aggressively specialized prime-search engine
for the single target machine defined in `docs/pivot/HARDWARE_TARGET.md`. Its CPU,
GPU and memory operate as one bounded, checkpointed pipeline that eliminates
composites with witnesses, preserves evidence, and distinguishes probable
primality from proof and independent verification.

The prime-search, proof and verification engine is the product. Hardware
specialization is a means to make that engine faster on the target; infrastructure
exists only to make the engine safe, reproducible and operable. Decisions are
ordered accordingly: engine algorithms and end-to-end capability first, target
CPU/GPU optimization second, minimum necessary infrastructure third.

## Initial scope

- Windows 11, C++23, MSVC and CUDA form the only performance path.
- AMD Ryzen 9 9950X3D, NVIDIA GeForce RTX 5080 and 64 GB-class DDR5 form the exclusive performance target; actual properties are detected rather than assumed.
- Linux/GCC validates portable correctness and stable formats but does not veto target-specific optimization.
- Correctness and reproducibility before optimization.
- Prime families introduced in the order specified by the research program.
- CPU/GPU acceleration accepted only after end-to-end reproducible benchmarks.

## Non-goals

- No claim that PrimeForge is already the fastest implementation.
- No GUI before the command-line formats and engine are stable.
- No public distributed campaign before local proof, verification, recovery, and coverage checks pass.
- No AI system is treated as a mathematical oracle.
- No PRP is reported as a proven prime.
- No return to a universal multi-platform performance product without explicit authorization.
- No invasive BIOS, voltage, clock, fan, security or driver change as part of ordinary tuning.

## Scientific truth labels

Every scientific or performance claim uses exactly one of: `DOC_VERIFIED`, `SOURCE_AUDITED`, `REPRODUCED`, `BENCHMARKED`, `PROVED`, `HYPOTHESIS`, `FAILED`, or `UNKNOWN`.

`BENCHMARKED` requires a versioned workload, raw data, exact commands, environment metadata, repeated measurements, and a stated uncertainty method. No performance conclusion may be inferred from a microbenchmark alone.

## Independent result axes

PrimeForge records three independent axes. No axis automatically promotes another:

- `primality_status`: `UNTESTED`, `COMPOSITE`, `PROBABLE_PRIME`, `PROVEN_PRIME`;
- `verification_status`: `UNVERIFIED`, `SELF_VERIFIED`, `INDEPENDENTLY_VERIFIED`;
- `novelty_status`: `NOT_CHECKED`, `CHECK_IN_PROGRESS`, `DUE_DILIGENCE_COMPLETE`, `PREVIOUSLY_KNOWN`.

## Coverage

A domain is covered only when its canonical, bounded definition is partitioned into deterministic, disjoint work units whose union equals that domain, with any intentional duplicate explicitly identified as verification work.

## Hardware and energy

Unavailable electrical measurements are recorded as `UNKNOWN`. TDP is never substituted for measured energy. Any stock, tuned, undervolted, or overclocked configuration must pass prolonged stability tests, show no unexplained divergence or hardware error, remain within documented temperature limits, and be identified in every benchmark.

## Prolonged-run safety

Duration is not itself a stop condition. A benchmark or autotuning campaign may
run for hours or days only after an independent watchdog, continuous available
telemetry, documented configurable thresholds, progressive checkpoints, tested
graceful/forced stop and tested recovery exist. Missing data is `UNKNOWN`.
Reference divergence, hardware/CUDA error, unsafe resource state, persistent
throttling, failed checkpoint or lost watchdog stops the run.

## Gate rule

A pivot milestone advances only when its versioned criteria pass. A failed
hypothesis or optimization is preserved in `NEGATIVE_RESULTS.md`; failure is an
acceptable scientific result, but it is not silently converted into success. The
governing sequence is `docs/pivot/NEW_ROADMAP.md`.

Before each milestone, the implementation must state how it advances the final
Windows engine. If successive milestones cease to improve the engine directly,
work returns to candidate generation, congruence compilation, sieving, routing,
CPU/GPU execution, proof, verification or recovery. Every new milestone report
contains a `Contribution directe au logiciel final` section and says whether its
infrastructure is complete or requires a specific later integration.
