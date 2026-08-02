# Stage 6 — validated CPU sieve foundation

## Outcome

PrimeForge now has a dependency-free C++23 reference sieve, deterministic segmented sieve, optional wheel-30/bit-packed/bucket mechanisms, cache-line-separated segment results, portable 128-bit multiplication, a CLI, and an exhaustive regression target. All options are separately selectable. The default remains the simpler byte-storage path because the available timings are not claim-eligible.

The scientific gate covers exact interval membership/order, thread-count determinism, known prime-count values, all 68 stage-4 corpus cases, exhaustive classification from 0 through 100000, boundary arithmetic, and 200000 fixed-seed random 128-bit differential cases.

## Independent comparisons

`validate_stage6_oracles.ps1` returned PASS:

- primesieve 12.15: `[0,1000)` = 168; `[999983,1001000)` = 76; `[1000000000,1000100000)` = 4832; `[1000000000000,1000000100000)` = 3614;
- FLINT 3.6.0: agreement on all 68 versioned corpus values.

These binaries are local external oracles under ignored `out/` paths. They are neither linked nor redistributed.

## Performance boundary

The stage-6 script schedules nine variants over three 10000000-integer ranges with seven randomized repetitions after warmup. It records integer process elapsed time, exact counts, commit/dirty state, and unknown telemetry. Process startup is included. Temperature, frequency, power, wall energy, and ambient temperature are `UNKNOWN`; `performance_valid=NO` and `performance_claim=NONE` apply to every row.

No result claims that PrimeForge is faster than primesieve. Wheel 210 and SIMD were not retained. The first preliminary timing run had invalid PowerShell aggregation and is recorded as NR-0016.

The retained corrected run is generated only after the canonical-LF evidence writer itself is committed and the worktree is clean. Every retained row remains ineligible for performance use.

## Build status

The final clean local pass compiled Debug and Release with zero PrimeForge warnings. Debug passed 8/8 tests in 1.21 s. Release passed 7/8 tests in 0.75 s; only the unchanged self-test executable was blocked before start by Windows Application Control, while unit, corpus, sieve, benchmark, and licensing tests all passed. This enforced host limitation is NR-0017/NR-0009; no policy was weakened.

Private workflow run `30762227654` passed at implementation commit `52c6d58e1ee9cf13587f7f107c7c6fbbd92f6619`: Linux/GCC in 19 s and Windows/MSVC, including clean Debug and Release via `run_all.ps1`, in 1 min 16 s. Hosted Windows therefore supplies the complete independent 8/8 Release execution gate that local Application Control prevented.

## Commands

```text
cmake --preset msvc-debug
cmake --build --preset msvc-debug --parallel
ctest --preset msvc-debug --output-on-failure
cmake --preset msvc-release
cmake --build --preset msvc-release --parallel
ctest --preset msvc-release --output-on-failure
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_all.ps1 -Clean
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/validate_stage6_oracles.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_stage6_benchmarks.ps1 -OutputDirectory benchmarks/evidence/stage6 -Repetitions 7 -Threads 4
```

## Limitations

- The implementation generates a vector even in count mode; later scale work may add a count-only reduction after equivalence tests.
- Base-prime storage is bounded by addressable memory and has not been validated over extreme near-`2^64` spans.
- There is no wheel 210, SIMD dispatch, GPU path, external engine integration, or performance claim.
- Electrical consumption and energy are `UNKNOWN`; no TDP estimate is used.
