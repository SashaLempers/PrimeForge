# PIVOT-07 milestone report

**Status:** COMPLETE - INCONCLUSIVE

**Date:** 2026-08-03

## Decision

PrimeForge does not select a global performance profile at this milestone. The
decision is not caused by a correctness failure: CPU sieve variants, exact CUDA
modular batches and one/two/three-slot recovery all pass. It is caused by the
scientific prerequisites for a performance choice:

- CPU temperature and package power are still `UNKNOWN`;
- the PIVOT-03 held-out rankings were not stable across regimes;
- the CUDA kernel is deliberately the correctness-first add/double algorithm;
- PIVOT-05/06 task sets validate arithmetic and recovery, not a prime family;
- PIVOT-08 has not yet selected the family that defines representative end-to-end
  candidate, sieve, PRP, proof and verification work.

A parameter sweep here would optimize a synthetic intermediate and risk steering
the engine away from its final workload. It would also violate the preregistered
requirement that timing influence selection only with complete stability evidence
and disjoint validation.

## Retained fallback

The conservative product values remain:

- CPU sieve: one worker, scheduler placement, static scheduling and 8192-candidate
  segments;
- CUDA modular adapter: one backend, one stream and an 8192-task capacity;
- pipeline: bounded one-slot execution with CPU verification and a checkpoint at
  every committed batch.

Double and triple buffering remain correctness-proven options, not performance
defaults. No timing, utilization or energy claim is recorded.

## Verification

PIVOT-07 changes governance and documentation only. Its baseline is the completed
PIVOT-06 gate: clean Debug/Release builds, 32/32 tests in both configurations,
35/35 optional CUDA tests, exact stop/resume, and zero Compute Sanitizer errors.
After this decision was documented, Debug and Release required no new compilation
and passed 32/32 tests in 47.40 and 14.06 seconds. Both C++23 self-tests passed.
The optional CUDA configuration also required no rebuild, passed 35/35 tests in
14.67 seconds and repeated all three executables under Compute Sanitizer with zero
errors. The documentation-only commit is still required to pass the private
Windows/Linux CI before PIVOT-08 implementation is retained.

## Contribution directe au logiciel final

This milestone protects the final engine from a premature tuning choice and avoids
spending the night on infrastructure that cannot yet improve prime-search work.
It makes the safe fallback explicit and preserves all proven multi-stream options.
The autotuning decision is not finished permanently: it must be revisited after a
target family and full workload exist, but no additional tuner code is justified
now. Development returns directly to the family, candidate, sieve and proof path.
