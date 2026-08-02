# PIVOT-03 CPU topology and sieve placement

PIVOT-03 starts with a direct engine correction. The previous experimental
`pinned` mode mapped worker `i` to logical processor bit `i`. On the target,
logical indices 0–15 cover SMT pairs from only the first eight cores, so a
16-worker run could fail to use all 16 physical cores and remain concentrated in
one CCD/L3 domain.

`primeforge-cpu-topology` now queries Windows CPU sets. The target reports:

- 32 usable CPU sets;
- 16 physical cores;
- two last-level-cache domains, indexed 0 and 16;
- one Windows processor group.

The physical plan chooses logical indices
`0,16,2,18,4,20,6,22,8,24,10,26,12,28,14,30`. This is one hardware thread per
physical core and alternates the two L3 domains. The logical plan emits those 16
targets first, then their SMT siblings. Parked CPU sets and sets allocated away
from PrimeForge are excluded.

The family sieve exposes three explicit modes:

- `scheduler-managed` (still the conservative default);
- `physical-core-spread`;
- `logical-processor-spread`.

Every worker records whether its CPU-set selection succeeded. Affinity is cleared
before the worker exits. Synthetic tests cover L3 interleaving, physical-before-
SMT ordering, worker limits and unavailable portable topology. Windows tests
require every requested affinity to apply and every sieve result to equal the
scalar reference.

Run the target diagnostic with:

```powershell
& .\out\build\msvc-release\primeforge-cpu-topology.exe
```

This establishes valid candidate configurations, not their performance ranking.
The next PIVOT-03 tranche must use disjoint calibration and validation workloads,
PIVOT-02 telemetry and no-divergence gates. Scheduler-managed remains selected if
the measurements are invalid or inconclusive.
