# PIVOT-03 — Ryzen CPU autotuning

Date: 2026-08-02. Status: IN PROGRESS.

## First engine tranche

PrimeForge now discovers Windows CPU sets and constructs deterministic
physical-core and logical/SMT placement plans that interleave the two target L3
domains. The family sieve consumes those plans instead of treating consecutive
logical processor numbers as consecutive physical cores. Affinity success counts
are retained per run and in raw benchmark rows.

The target diagnostic reports 32 CPU sets, 16 physical cores and two L3 domains.
The physical plan covers all 16 cores and alternates the domains. The clean
kickoff gate built 83/83 steps with zero PrimeForge warnings, then passed 26/26
tests in Debug (45.91 s) and Release (13.33 s); both explicit self-tests passed.
No timing result is retained yet and no configuration is called fastest.

## Contribution directe au logiciel final

This change directly improves the prime-search engine's CPU execution path. It
prevents a 16-worker sieve from accidentally occupying eight cores plus SMT while
leaving half the Ryzen idle, and gives the autotuner explicit CCD/L3-aware and SMT
choices. It preserves exact witnesses and canonical outputs.

This tranche is complete as a topology and placement primitive. PIVOT-03 itself
is not complete: thread count, placement, segment, scheduling and SIMD choices
still require disjoint monitored calibration/validation. The safe default remains
Windows scheduler management until those measurements pass.

## Hosted Windows capability correction

Private workflow run `30768052506` compiled all 83 Windows steps without a
PrimeForge warning and passed 25/26 tests, but its hosted job exposed CPU-set
topology while refusing thread CPU-set selection. Linux passed 26/26. The test
had incorrectly equated Windows API presence with runtime permission.

The corrected portable gate verifies exact plan capacity and applied counts
without assuming that a hosted Windows job represents the target. A separate
target-specific gate recognizes the Ryzen 9 9950X3D, runs 16 physical workers and
requires 16/16 selections. No restricted host can fabricate pinning success, and
no hosted-runner limitation weakens the target requirement.
This is a portability correction only; it does not change placement or sieve
semantics.

The corrected clean target gate built 83/83 steps in both configurations with
zero PrimeForge warnings and passed 26/26 tests in Debug (46.31 s) and Release
(13.38 s). Both C++23 self-tests passed. The target topology diagnostic still
reports 16 physical cores across two L3 domains and the same 16-worker
interleaved physical plan.

## Second engine tranche: bounded proper-factor checks

The family sieve no longer constructs a complete arbitrary-precision candidate
merely to decide whether a known divisor is proper when `k`, `b` and `c` are
nonnegative. After the modular divisibility check, saturating exponentiation,
multiplication and addition compare `k*b^n+c` exactly with `q`. A value equal to
`q` is retained; only a value strictly greater than `q` is eliminated. Signed
families conservatively use the unchanged `BigInteger` implementation.

The retained differential corpus exercised 3,108 bounded checks with zero
`BigInteger` construction. Its canonical elimination bitset equals the
arbitrary-precision scalar reference. A signed corpus exercised 3,131
`BigInteger` fallbacks and also matched exactly. The benchmark TSV now records
`bounded_magnitude_checks` and `big_integer_checks`, so future timing evidence can
attribute the path used. No performance result or fastest claim is made here.

The final clean save gate built 83/83 steps in Debug and Release with zero
PrimeForge warnings, passed 26/26 Debug tests in 46.27 s and 26/26 Release tests
in 13.62 s, and passed both explicit C++23 self-tests. The target-specific test
reported `target_16_core_affinity_checked=YES` and
`target_16_core_affinity_applied=16`.

## Contribution directe au logiciel final

This tranche removes arbitrary-precision allocation and multiplication from a
repeated sieve decision for the ordinary nonnegative prime families while
preserving the exact proper-factor obligation. It directly reduces work in the
candidate-elimination engine and leaves the general signed language correct.

The bounded proof primitive is complete and should not grow into separate
infrastructure. PIVOT-03 remains open only for monitored, disjoint calibration
and validation of thread count, placement, segmentation, scheduling and SIMD.
Those remaining optimization experiments are explicitly paused until the
end-to-end PrimeForge MVP and its small known campaign have been delivered.
