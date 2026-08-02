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

The corrected gate probes selection permission and remains strict whenever it is
available, including on the target machine. On restricted hosts it verifies that
the exact applied count is reported and that no pinning success is fabricated.
This is a portability correction only; it does not change placement or sieve
semantics.

The corrected clean target gate built 83/83 steps in both configurations with
zero PrimeForge warnings and passed 26/26 tests in Debug (46.31 s) and Release
(13.38 s). Both C++23 self-tests passed. The target topology diagnostic still
reports 16 physical cores across two L3 domains and the same 16-worker
interleaved physical plan.
