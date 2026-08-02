# PrimeForge family-sieve experiments

Stage 10 turns a validated congruence table into a canonical bitset of locally witnessed eliminations. A set bit means that PrimeForge re-evaluated the candidate, verified divisibility by a prime `q`, checked `F > 1`, and checked `abs(F) > q`. A clear bit makes no primality statement.

## Preserved reference

`reference_eliminated_words` calls the stage-9 scalar evaluator. Every optimized option is differential-tested against that reference. The result bitset is always serialized in `k`-major order and hashed over an explicit little-endian byte representation, independently of traversal orientation, scheduling, or host byte order.

The public options isolate the planned variables:

- candidate list or dense bitset accumulation;
- `k`-major or `n`-major traversal;
- prime-major or candidate-major loops;
- array-of-structures or structure-of-arrays rule metadata;
- segment sizes representing L1-, L2-, and L3-scale experiments;
- static or dynamic segment scheduling;
- compressed congruence classes or direct modular scans;
- a precomputed union for small-prime wheel experiments;
- a bounded periodic CRT residue template, rejected if its memory estimate exceeds the configured cap;
- scalar, AVX2, or AVX-512 dense-bitset merge with runtime dispatch;
- explicit prefetch;
- a nonmandatory huge-page allocation probe;
- scheduler-managed or temporarily pinned Windows worker threads;
- physical-core or logical/SMT thread counts.

AVX2 and AVX-512 currently vectorize only the deterministic merge of per-worker bitsets. This limited scope is recorded so a capability flag cannot be misreported as a sieve-wide speedup. Unsupported SIMD paths fall back to scalar. The huge-page option probes whether a reversible large-page allocation is permitted and immediately releases it; it does not request privileges and is never a dependency. Linux remains compilable, but pinning and huge-page probing currently report not applied there.

## Measurement boundary

`primeforge-family-sieve-benchmark` performs warmup, uses a fixed-seed randomized schedule, runs at least seven repetitions, and times congruence compilation, the complete sieve, and result hashing. It compares every timed result to the scalar output before accepting a row. The retained matrix changes one variable from the baseline at a time over small, medium, and large finite regimes.

The host currently has no integrated reliable provider for temperature, effective frequency, throttling, hardware errors, or wall energy. Those fields remain `UNKNOWN`, telemetry is `UNAVAILABLE`, every row has `performance_valid=NO`, and `performance_claim=NONE`. Therefore diagnostic curves may locate work-count changes, but they cannot establish an optimal production configuration or support a performance assertion.

Run a clean retained diagnostic collection with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_stage10_benchmarks.ps1 -Repetitions 7
```

No external engine, numerical library, profiler, driver API, or telemetry package is linked by this stage.
