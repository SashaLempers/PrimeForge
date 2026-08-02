# Stage 7 — work units and coverage proof

## Outcome

Stage 7 implements deterministic half-open partitioning, content-addressed work-unit identifiers, schema-specific canonical JSON, a dependency-free portable SHA-256 backend, coverage verification, duplicate detection, canonical coverage reports, an output CLI, and durable atomic checkpoint replacement on Windows and Linux.

The proof in `docs/WORK_UNITS.md` establishes pairwise disjointness, complete coverage, termination, and overflow avoidance. The implementation verifies those invariants independently of input order.

## Gate evidence

`primeforge-work-unit-tests` covers:

- standard SHA-256 vectors for empty, `abc`, and multi-block messages;
- canonical work-unit golden id `04c5b090dd357de83ec6f98eae169e7d2ae7c6f7dfbf56c08b8ab19203975e7a`, independently calculated with the Windows cryptographic provider;
- 1000 consecutive parameter values visited exactly once across eight units;
- identical ids across repeated partitions;
- deliberate deletion, duplication, and interval/content corruption, all rejected;
- interruption after temporary write and after durable flush, both preserving the old checkpoint;
- successful atomic replacement after either interruption.

`primeforge-work-units` is also a CTest smoke test and writes a canonical JSONL unit list plus a canonical coverage report under the build tree.

## Conservative boundaries

- User-provided canonical fields are restricted to ASCII until complete Unicode NFC normalization is justified and implemented.
- Checkpoint durability is established for the native local-filesystem paths used by Windows and Linux CI, not for arbitrary network filesystems.
- SHA-256 is used for identity and integrity, not signatures or authentication.
- No search campaign, remote assignment, external transmission, or novelty claim occurs in this stage.

## Build and tests

MSVC 19.51.36252.0 compiled both clean Debug and Release trees with zero PrimeForge warnings. The initial non-clean Debug gate passed 10/10 in 1.67 s. After a clean relink, Windows Application Control blocked only the unchanged Debug `primeforge-tests.exe` before start, while 9/10 passed and both new stage-7 targets passed; this host-policy event is NR-0020. Release passed 10/10 in 3.97 s, and both explicit self-tests reported C++23 and PASS.

Private workflow run `30762598995` passed at commit `7861345839ff9848d746e99afb8f0eb72443f5ed`: Linux/GCC in 18 s and Windows/MSVC, including clean Debug and Release, in 1 min 31 s. Stage 7 is closed.

## Commands

```text
cmake --preset msvc-debug
cmake --build --preset msvc-debug --parallel
ctest --preset msvc-debug --output-on-failure
cmake --preset msvc-release
cmake --build --preset msvc-release --parallel
ctest --preset msvc-release --output-on-failure
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_all.ps1 -Clean
```
