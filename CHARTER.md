# PrimeForge Charter

## Mission

Build an auditable command-line system that describes bounded number families, partitions their domains without gaps, eliminates composites with reproducible witnesses, routes survivors to appropriate engines, preserves evidence, and distinguishes probable primality from proof and independent verification.

## Initial scope

- Windows 11 and C++23 first, with portable core interfaces.
- Correctness and reproducibility before optimization.
- Prime families introduced in the order specified by the research program.
- CPU/GPU acceleration accepted only after end-to-end reproducible benchmarks.

## Non-goals

- No claim that PrimeForge is already the fastest implementation.
- No GUI before the command-line formats and engine are stable.
- No public distributed campaign before local proof, verification, recovery, and coverage checks pass.
- No AI system is treated as a mathematical oracle.
- No PRP is reported as a proven prime.

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

## Gate rule

A stage advances only when its versioned criteria pass. A failed hypothesis or optimization is preserved in `NEGATIVE_RESULTS.md`; failure is an acceptable scientific result, but it is not silently converted into success.
