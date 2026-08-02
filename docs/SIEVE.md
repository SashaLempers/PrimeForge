# PrimeForge stage-6 sieve

## Correctness boundary

Every public interval is half-open: `[begin,end)`. Empty and reversed intervals return an empty result. Prime generation and count mode share the same implementation, and segment results are concatenated in ascending segment order after all workers finish. Thread scheduling therefore cannot alter either membership or order.

`generate_primes_reference` is the intentionally simple, single-threaded byte sieve used only for bounded validation. `generate_primes` precomputes base primes, partitions the requested interval, and gives each worker exclusive ownership of its segment state. `is_prime_u64` is a deterministic 64-bit Miller-Rabin classifier with the fixed complete witness set; it never labels a generic PRP outside that bounded guarantee as proven.

## Separately selectable mechanisms

`SieveOptions` exposes every experimental mechanism independently:

- configurable segment span and worker count;
- modulo-30 candidate wheel on/off;
- packed-bit or one-byte composite storage;
- bucket handling on/off for sieve primes whose odd step exceeds one segment;
- structure-of-arrays or array-of-structures bucket layout;
- explicit prefetch on/off.

Per-segment output holders occupy distinct 64-byte-aligned cache lines. Workers never mutate another segment's composite map or prime list. The conservative defaults keep wheel, bit packing, buckets, and prefetch off until a claim-eligible multi-range protocol establishes a retained gain. Wheel 210 and SIMD dispatch are not present because the stage-6 measurements have missing telemetry and include process startup; adding code without evidence would violate the retention rule.

## Portable 128-bit multiplication

`multiply_full` and `multiply_mod` have three internal paths:

- MSVC x64: `_umul128` and `_udiv128`;
- GCC/Clang with native 128-bit integers: `unsigned __int128` behind the internal alias;
- portable reference: four 32-by-32 partial products for the full product and overflow-safe add/double modular multiplication.

The API never exposes a compiler-specific integer type. Tests cover boundary vectors and 200000 fixed-seed random triples against the portable reference.

## Commands

After a Release build:

```powershell
& .\out\build\msvc-release\primeforge-sieve.exe --begin 0 --end 1000000 --threads 4
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\validate_stage6_oracles.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\run_stage6_benchmarks.ps1 -Repetitions 7 -Threads 4
```

The external validator requires the locally audited primesieve 12.15 and FLINT 3.6.0 oracle under ignored `out/` paths. Neither is linked or redistributed. The benchmark emits integer TSV data, records missing temperature/frequency/power/energy as `UNKNOWN`, includes process startup in the measured region, and always writes `performance_claim=NONE`.
