# PIVOT-03 — Ryzen CPU autotuning

Date: 2026-08-02. Status: COMPLETE - INCONCLUSIVE PROFILE SELECTION.

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

## Third engine tranche: single-pass proper-factor evidence

After the private MVP release, profiling the real source path exposed a complete
duplicated operation: `search` first ran `family_sieve::run` to obtain the
elimination bitset, then called `apply_compiled_table` over the whole table only
to reconstruct factor values. The family sieve can now optionally retain the
canonical smallest proper factor while it performs the original divisibility
checks. MVP search consumes those witnesses directly and revalidates each factor
against the exact uint64 candidate before serializing it.

All storage, orientation, loop, metadata, segment, scheduling, compressed/direct,
wheel, CRT, AVX2, AVX-512, prefetch, huge-page, placement and thread variants
produce the reference bitset and canonical reference factors. The CRT test first
found that a valid premark factor was not always the smallest; the corrected path
checks only lower matching rules and now preserves canonical evidence without a
full replay. Witness collection is opt-in, so bitset-only callers allocate no
factor vector.

The real 160-candidate campaign was regenerated and independently verified: 34
proven primes, 126 composites and 208 manifest files. Its result and manifest
hashes remain exactly
`4C0BF7E5554A257BE36C9F4CA54FD2E7D7B601CF848C1A3AAC48610254361C1A`
and `2CC3A3E38BFFA4BE2CBA8938547239B3B994B5A4AB803D9901E2ADE9698E4137`.

A short 420-row diagnostic compared the retained single pass with a deliberate
legacy replay on 1,024, 4,096 and 16,384 candidates. Both paths produced the same
353, 1,438 and 5,836 canonical factors. Observed medians were respectively
2.14/4.22/7.11 ms and 3.19/7.08/17.91 ms. CPU temperature, CPU power and complete
stability telemetry were unavailable, so every row remains
`performance_valid=NO` and `performance_claim=NONE`; these timings support no
speed claim or optimum selection.

The ignored local diagnostic remains at
`out/benchmarks/pivot03-factor-witness-dev1`: `raw.tsv` SHA-256
`6CE1DFB41658B8E1EDFE9037865928CA64916F5C3D44C764A560D99964516C90`
and `summary.tsv` SHA-256
`2BFE63E52CAD0D137B97851F7BAC4A5AFF1907BD4A2B3E6932B6641D23834D8D`.

The final clean gate passed 31/31 Debug tests in 43.71 s and 31/31 Release
tests in 14.38 s, with both explicit C++23 self-tests passing and no PrimeForge
warning. The final snapshot reported GPU 51 C, 46.39 W, no throttling,
43,717,922,816 RAM bytes available and 13,639 MiB VRAM free. CPU temperature
and power remain `UNKNOWN`; no prolonged load ran.

## Contribution directe au logiciel final

This tranche removes a whole congruence-table traversal from the actual
configuration-to-proof executable while keeping the exact factor evidence needed
by results and independent verification. It directly shortens the candidate
classification pipeline instead of optimizing unused infrastructure.

The duplicate pass is permanently removed for the MVP path. Factor retention may
later need a more compact representation for billion-candidate batches, but it is
optional and does not affect bitset-only sieving. PIVOT-03 remains open for the
measured thread, segment, placement and SIMD selection required by the target.

## Fourth engine tranche: disjoint direct result words

The dense k-major sieve previously allocated a complete candidate bitset for
every active worker and OR-merged all of them after joining. A word-aligned
segment is owned by one worker, so PrimeForge now writes that segment's canonical
64-bit words and optional factor entries directly. The production path removes
both the `threads * candidate_count` scratch-bitset growth and the complete
per-worker merge. Non-word-aligned, list and transposed configurations retain the
old path automatically.

The first watchdog-supervised diagnostic caught a real boundary error: `by_n`
segments are disjoint in traversal order but can share a k-major output word.
That attempt was rejected and recorded as NR-0035. Direct writes are now gated on
canonical `by_k` orientation. Twenty repeated 16-worker dynamic runs, every
option variant and the 420-row randomized diagnostic agree with the scalar
bitset and canonical smallest factors. The diagnostic raw and summary SHA-256
values are `FD25120BD643C984EFE39C94E2A4CD3CC1DA8F5B59114D966B715033FE874EE1`
and `CD172B7BECA75F2892E1253830D4888EA8F000DD546E7DCF31FA58FB82BECA02`.

The watchdog sampled eight times: GPU temperature remained 52 C, power ranged
from 43.74 W to 55.65 W, no throttling occurred, and available RAM stayed above
43.9 GB. CPU frequency was detected at 4300 MHz; CPU temperature and package
power remain `UNKNOWN`. Every timing row therefore remains
`performance_valid=NO` and `performance_claim=NONE`.

## Contribution directe au logiciel final

This tranche directly reduces the memory and finalization work of the actual
multi-thread candidate sieve. It also lets already eliminated candidates skip
later rules inside their owning segment while preserving the smallest factor.
The disjoint-word primitive is complete for the canonical aligned layout; its
fallback boundary is now a tested contract. PIVOT-03 still needs disjoint
calibration/validation for thread count, segment size, scheduling and placement.
SIMD merge tuning is no longer relevant to the aligned production path because
that merge has been removed.

The final clean gate compiled 93/93 targets with no PrimeForge warning in both
Debug and Release. Debug passed 31/31 tests in 44.44 s and Release passed 31/31
in 14.48 s; both explicit self-tests reported C++23 and PASS. A fresh real
160-candidate campaign then completed under the independent watchdog: 117 sieve
composites, 9 base-2 negative witnesses, 34 proven primes and 126 composites in
total. Independent verification accepted all 160 records and 208 manifest files.
Comparison with the previously retained campaign found zero semantic record
differences across coordinates, values, classifications, factors and all three
status axes.

The campaign watchdog recorded 24 samples, maximum GPU temperature 52 C,
maximum GPU power 50.32 W, minimum available RAM 43,824,496,640 bytes, minimum
free VRAM 13,637 MiB and zero throttle samples. The System WHEA query found no
matching event in the gate interval. CUDA was not invoked by this CPU campaign.
Worker, watchdog, search and verification all exited zero. CPU temperature and
package power remain `UNKNOWN`, so this was a short correctness campaign rather
than a prolonged load or performance validation.

## Fifth engine tranche: direct compiled-residue enumeration

The production sieve still scanned every candidate in a segment for every
compiled forbidden rule, only to test whether the candidate belonged to the
rule's already known k/n residue classes. The canonical compressed path now
enumerates those arithmetic progressions directly. Each enumerated pair is still
rechecked modulo the prime and against the exact proper-factor boundary before
it can set a result bit. The explicit `full-rule-scan` mode and every
noncanonical traversal retain the old path as a differential oracle.

Across the 1,024-, 4,096- and 16,384-candidate diagnostic regimes, the optimized
path visited exactly 1,128, 4,492 and 17,978 rule-candidate pairs; the full scan
visited 90,112, 360,448 and 1,441,792. Both produced the same 353, 1,438 and
5,836 eliminated candidates, identical result hashes, identical modular/exact
check counts and identical canonical factor vectors when requested. These exact
operation counts establish removed work without asserting a timing speedup.

The watchdog-supervised 441-row diagnostic passed with raw SHA-256
`33583A499839BA363B0F1019438B43D2B0F8450AF350DAE1825A1E22C72B0327`
and summary SHA-256
`4F9A79274FF55B2EB2E352B4C6E46F379FD03C7D90E6435A6473EBD3C02C0F1F`.
Five telemetry samples recorded maximum GPU temperature 52 C, maximum GPU power
47.26 W, minimum available RAM 43,667,378,176 bytes, minimum free VRAM 13,639
MiB and zero throttling. CPU temperature and package power remain `UNKNOWN`, so
all timing rows continue to state `performance_valid=NO` and
`performance_claim=NONE`.

## Contribution directe au logiciel final

This tranche replaces the dominant generic nested scan with the sparse search
space already produced by PrimeForge's congruence compiler. It directly reduces
candidate-generation work while preserving an executable full-scan oracle and
all local proof obligations. The residue iterator is complete for the canonical
production layout. Further PIVOT-03 work should now measure thread/segment
selection on this new complete path rather than optimize the removed scan.

The final clean gate compiled 93/93 targets without a PrimeForge warning in
both configurations. Debug passed 31/31 tests in 45.84 s and Release passed
31/31 in 15.57 s; both explicit self-tests reported C++23 and PASS. A fresh
watchdog-supervised real campaign again produced 117 sieve composites, 9 base-2
negative witnesses, 34 proven primes and 126 composites. Independent
verification accepted 160/160 records and 208 manifest files, with zero
semantic record difference from the retained campaign.

The campaign watchdog recorded 27 samples, maximum GPU temperature 52 C,
maximum GPU power 60.71 W, minimum available RAM 43,528,187,904 bytes, minimum
free VRAM 13,633 MiB and zero throttle samples. The gate interval contained zero
WHEA events; CUDA was not invoked. Worker, watchdog, search and verification
exited zero. CPU temperature and package power remain `UNKNOWN`; no prolonged
load or performance-valid benchmark was run.

The preceding direct-word commit `d35425e` is independently green in private
workflow `30772608766`: Linux/GCC completed in 1 min 6 s and Windows/MSVC,
including the closed MVP package gate, in 7 min 15 s.

## Final CPU calibration and validation gate

Two explicit suites use the same 15 thread, segment, placement and scheduling
profiles on disjoint domains. Calibration covers k indices beginning at 1 and n
18 through 33; validation begins at k 100,001 and uses n 34 through 49. Each has
small, medium and large regimes, seven fixed-seed randomized repetitions and a
scalar bitset plus canonical-factor oracle. All 315 calibration and 315
validation executions passed.

The lowest observed elapsed-time profile was not stable across regimes.
Calibration produced 2-thread scheduler, 16-thread physical/4096-segment and
32-thread logical minima; validation produced 4-thread scheduler, 16-thread
scheduler and 32-thread logical minima. More importantly, every row remains
`performance_valid=NO`: CPU temperature and package power are unavailable, so
the stability gate is incomplete. PIVOT-03 therefore closes `INCONCLUSIVE` and
retains the one-thread, 8192-segment, scheduler-managed static product profile.

Calibration raw/summary SHA-256 values are
`4E9022812784DD5CE7286D5CBC63E1AA8A4C4D133353C11E185B41D83120438E`
and `290739A85F178492D105F3752ACD61435BEF5B18790DD0FC584E301BB8622AC1`.
Validation raw/summary SHA-256 values are
`A65E692B7C37FFA0B79A536311179C3B525DD63C7679867576B259FC66B6132D`
and `A61B2D12A46365857463D52F431F31324D844FFA2C759A951535B3FE74CAAEDB`.
Across both short studies, maximum GPU temperature was 52 C, maximum power
49.24 W, minimum available RAM 43,695,570,944 bytes, minimum free VRAM 13,649
MiB and no throttling was detected. Both workers and watchdogs exited zero.

The closing clean gate compiled 93/93 targets without a PrimeForge warning in
both configurations, passed 31/31 Debug tests in 45.38 s and 31/31 Release tests
in 15.45 s, and passed both explicit C++23 self-tests.

## Contribution directe au logiciel final

This final tranche prevents an unvalidated CPU timing choice from entering the
product while proving that all candidate thread/placement configurations preserve
the complete sieve evidence. PIVOT-03's topology, bounded-factor, single-pass
witness, disjoint-word and compiled-residue engine changes are finished and stay
enabled. CPU profile selection may be revisited only when complete stability
telemetry exists; it no longer blocks the CUDA engine path. PIVOT-04 can begin.
