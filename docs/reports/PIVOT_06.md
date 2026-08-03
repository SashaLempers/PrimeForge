# PIVOT-06 milestone report

**Status:** COMPLETE

**Date:** 2026-08-03

## Delivered engine path

- portable `ModularBatchPipeline` accepting injected arithmetic backends;
- bounded one-backend-per-slot submission with explicit back-pressure;
- real double and triple CUDA stream execution;
- portable CPU verification before every durable result;
- deterministic append-only JSONL result ledger and lifecycle event log;
- atomic checkpoint binding campaign, task identity, progress, ledger bytes and
  ledger SHA-256;
- cooperative stop that drains already submitted batches;
- exact resume and rollback of only an unauthenticated suffix.

This layer directly composes the PIVOT-05 engine primitive. It does not attempt a
generic job system, and it makes no utilization or performance claim.

## Correctness evidence

The portable test uses deliberately out-of-order fake backends and proves that
one, two and three slots produce the same ledger bytes. It checks the in-flight
bound, a pre-requested stop, a three-batch cooperative stop, exact resume, suffix
rollback, CPU/backend divergence, backend exception, changed task identity,
checkpoint mutation and durable-ledger mutation.

The real target test uses three independent CUDA backends, each with persistent
buffers and a nonblocking stream. It reported:

```text
cuda.pipeline.tasks=4097
cuda.pipeline.batch_size=257
cuda.pipeline.buffers_tested=1,2,3
cuda.pipeline.interrupted_after=771
cuda.pipeline.resumed_from=771
cuda.pipeline.status=PASS
```

The uninterrupted one/two/three-stream ledgers and the interrupted/resumed ledger
are byte-identical. Every result is independently recomputed on CPU before it can
be appended. The CUDA executable repeated under Compute Sanitizer with
`ERROR SUMMARY: 0 errors`.

The final clean ordinary builds completed 97/97 steps in each configuration with
zero PrimeForge warnings. Debug passed 32/32 tests in 48.73 seconds and Release
passed 32/32 in 14.98 seconds; both self-tests reported C++23. The clean CUDA
build completed 105/105 steps, then passed 35/35 tests in 15.70 seconds; the real
CUDA pipeline test took 0.40 seconds. All three CUDA executables passed Compute
Sanitizer with zero errors.

After narrowing stopped results to the committed prefix, the affected pipeline
source rebuilt in Debug, Release and CUDA. The complete follow-up gates again
passed 32/32 in 47.63 seconds, 32/32 in 13.98 seconds and 35/35 in 14.71 seconds;
the CUDA pipeline took 0.41 seconds and its sanitizer again reported zero errors.

The closed CPU package directory and ZIP each passed with 15 files and zero
external binaries. Content-manifest SHA-256:
`c7554a3423db56cef645dea609505d9231b07309289488b50531d5803bea7ab6`.
Diagnostic ZIP SHA-256:
`8ca3200604394e7d35937fb170ffc730807a66048cfafbc21d06273382de2001`.

## Safety and limitations

Only short correctness suites ran. The final snapshot reported GPU 51 degrees
Celsius, 46.42 W, no throttling, 13,579 MiB free VRAM and 43,110,023,168 bytes
available RAM. WHEA events in the preceding two hours: zero. CUDA/sanitizer
errors: zero. CPU temperature, CPU package power and GPU memory temperature remain
`UNKNOWN`; no performance result is valid or claimed.

Checkpoint hashing currently rehashes the accumulated result prefix after every
batch. This is conservative and simple, but may need an incremental or coarser
policy if profiling later proves it dominant. PIVOT-07 must not select buffer or
batch counts from these correctness-test durations.

## Contribution directe au logiciel final

This milestone gives the final engine its first crash-safe CPU/GPU execution
route: bounded concurrent GPU work, CPU distrust/verification, deterministic
durable results and recovery without omission or duplication. It is necessary for
real prime-search campaigns because a fast kernel without ownership and recovery
cannot produce trustworthy coverage. The core pipeline contract is complete;
future work tunes it and connects family-specific candidate batches rather than
rebuilding its durability rules.
