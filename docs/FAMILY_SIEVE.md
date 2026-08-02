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
- scheduler-managed, physical-core-spread or logical-processor-spread Windows CPU
  sets, with physical cores filled before SMT siblings and L3 domains interleaved;
- physical-core or logical/SMT thread counts.

AVX2 and AVX-512 currently vectorize only the deterministic merge of per-worker bitsets. This limited scope is recorded so a capability flag cannot be misreported as a sieve-wide speedup. Unsupported SIMD paths fall back to scalar. The huge-page option probes whether a reversible large-page allocation is permitted and immediately releases it; it does not request privileges and is never a dependency. Linux remains compilable, but pinning and huge-page probing currently report not applied there.

The optional `retain_factor_witnesses` mode returns one canonical smallest proper
factor for every eliminated candidate in the same k-major index space. Worker,
wheel and CRT paths are differential-tested against scalar factors. CRT-marked
candidates examine only matching rules below the already valid CRT factor, which
makes the witness canonical without replaying the entire congruence table. The
MVP search enables this mode and no longer invokes `apply_compiled_table` a
second time solely to reconstruct factors. Callers that need only the bitset keep
the option disabled and pay no factor-vector allocation.

Dense k-major segments whose size is divisible by 64 own complete canonical
bitset words. Workers write those words and their optional factor witnesses
directly into the result. This removes one full bitset per worker and the final
per-worker merge. The optimization is deliberately disabled for `by_n`
traversal and non-word-aligned segments, because those partitions can share a
canonical word; these cases keep the worker-local merge path. Consequently AVX2
and AVX-512 merge modes are relevant only to the fallback path, not to the
aligned production path where no merge remains.

The canonical compressed prime-major path also consumes each compiled
`(k-index,n-index)` residue class as an arithmetic progression. It enumerates
only matching pairs inside the current segment, then rechecks divisibility and
proper-factor magnitude exactly as before. It does not trust a table match as a
proof by itself. `enumerate_residue_classes=false` retains the complete rule by
candidate scan, and transposed, candidate-major, SoA, prefetch and uncompressed
experiments use that general path automatically. This gives every fast result an
independent differential fallback.

## Measurement boundary

`primeforge-family-sieve-benchmark` performs warmup, uses a fixed-seed randomized schedule, runs at least seven repetitions, and times congruence compilation, the complete sieve, and result hashing. It compares every timed result to the scalar output before accepting a row. The retained matrix changes one variable from the baseline at a time over small, medium, and large finite regimes.

The `pivot03-calibration` and `pivot03-validation` suites use the same 15 CPU
profiles but nonoverlapping k/n domains. Every profile retains factor witnesses;
the study therefore measures the complete optimized sieve result rather than a
bitset-only microkernel. Run either suite with:

```powershell
& .\out\build\msvc-release\primeforge-family-sieve-benchmark.exe `
  --output-dir out\benchmarks\pivot03-cpu-calibration `
  --suite pivot03-calibration --repetitions 7
```

Replace both `calibration` occurrences with `validation` for the held-out suite.
The executable never upgrades telemetry eligibility by itself; watchdog logs are
separate evidence and missing CPU stability sensors keep every row invalid.

The historical stage-10 evidence had no integrated reliable provider for
temperature, effective frequency, throttling, hardware errors or wall energy.
Those retained rows remain `performance_valid=NO` and `performance_claim=NONE`.
PIVOT-02 now supplies CPU frequency plus NVIDIA telemetry/throttling, but CPU
temperature and package power remain `UNKNOWN`; old timings are never promoted.
PIVOT-03 must collect new disjoint calibration/validation data before selecting a
production configuration. That collection is now complete and `INCONCLUSIVE`:
the conservative product profile is unchanged.

The harness includes `mvp-factor-witnesses` and
`mvp-legacy-factor-second-pass` diagnostic variants. The latter deliberately
recreates the removed second traversal. Their timing rows remain
`performance_valid=NO` and `performance_claim=NONE`; structural removal of a
complete redundant pass does not turn those rows into a benchmark claim.
Raw rows also state whether `direct_bitset_writes_applied`; this is an execution
fact, not a performance assertion. They similarly record
`residue_enumeration_applied` and include a `full-rule-scan` variant.

Run a clean retained diagnostic collection with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_stage10_benchmarks.ps1 -Repetitions 7
```

No external engine, numerical library, profiler, driver API, or telemetry package is linked by this stage.
