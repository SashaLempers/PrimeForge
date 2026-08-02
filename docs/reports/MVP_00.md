# MVP-00 — Roadmap and gap inventory

Date: 2026-08-02. Status: PASS.

The MVP directive is now the governing implementation priority. The first release
is restricted to a small, known, local `k*2^n+1` campaign below `2^64`. It reuses
the validated family, coverage, congruence, sieve, status and checkpoint modules,
and completes the missing orchestration around the already audited local PARI/GP
and FLINT processes.

The roadmap contains four short milestones: unified CLI/plan, complete search,
recovery/final verification, then known campaign/private release. Advanced CPU,
memory and GPU optimization is frozen until all four pass.

The documentation milestone reused the clean build trees and passed 26/26 Debug
tests in 42.49 s and 26/26 Release tests in 12.40 s; both explicit C++23
self-tests passed. A post-test hardware snapshot reported CPU temperature
`UNKNOWN`, GPU 53 C at 48.48 W, no reported throttling, 43,977,515,008 RAM bytes
available and 13,605 MiB VRAM free. This single snapshot is operational context,
not a maximum-temperature measurement or benchmark.

## Contribution directe au logiciel final

This milestone removes implementation-order ambiguity and identifies the shortest
path to the actual Windows product: one executable that can plan, search, prove,
resume and verify. It is necessary because the repository had many validated
primitives but no campaign orchestrator.

The roadmap/inventory work is complete. It should be revisited only if a measured
MVP blocker invalidates one of the stated reuse decisions; ordinary design details
must not reopen infrastructure or autotuning work.
