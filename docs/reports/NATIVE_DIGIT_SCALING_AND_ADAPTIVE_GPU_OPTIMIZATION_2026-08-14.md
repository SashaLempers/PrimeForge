# Native digit scaling and adaptive GPU optimization

**Date:** 2026-08-14<br>
**Scope:** bounded closed benchmark; no discovery campaign<br>
**Target:** NVIDIA GeForce RTX 5080, OpenCL 3.0 platform / OpenCL C 1.2<br>
**Baseline commit:** `93b9b551791b86b68db4eb7e474a2298fd5c2f47`<br>
**Baseline executable SHA-256:**
`90076cb25709b039f437b2de4d9dcbf9393e449e03b90c0f6dd2e5654a78d028`

`100k_digits` means Proth numbers containing about 100,000 decimal digits.
It does not mean `100k_candidates`. Every measured batch contained 32 closed
composite candidates. The complete five-size baseline and all optimization
experiments were short and bounded.

## Reproducible corpus

`scripts/generate_native_digit_scaling_corpus.ps1` derives each corpus from:

```text
PrimeForge|primeforge.native-digit-scaling-corpus.v1|
base=93b9b551791b86b68db4eb7e474a2298fd5c2f47|digits=<digits>|batch=32
```

SHA-256 selects an odd starting `k` in `10000001..99999999` and an exponent
that gives the requested exact decimal digit count. A deterministic ascending
scan retains the first 32 odd `k` values accepted by the pinned Proth20 witness
preflight. The data are explicitly marked
`CLOSED_NON_DISCOVERY_SCALING_BENCHMARK`.

The corpus manifest SHA-256 is
`c842457e5c6985dda7252d8dcc84af2dc8170767d2b494a6a5e61334ccb510a5`.

| Digits | Exponent | Accepted k interval | B32 transform |
|---:|---:|---:|---:|
| 20,000 | 66,413 | 13,570,561..13,570,671 | 8,192 |
| 40,000 | 132,850 | 97,563,719..97,563,835 | 16,384 |
| 60,000 | 199,290 | 13,295,649..13,295,761 | 32,768 |
| 80,000 | 265,727 | 86,393,631..86,393,739 | 32,768 |
| 100,000 | 332,165 | 60,558,335..60,558,443 | 32,768 |

## Method

Each size used one warm-up, one unprofiled wall-time run with NVIDIA telemetry,
and one kernel-profiled run. B32 fit at every size, so B16 and B8 were not run:
the directive required them only as fallbacks if B32 failed. A per-process
timeout prevented an unbounded experiment.

The acceptance experiments used the same input bytes in A/B/B/A order. An
optimization was accepted only when classification, witness and RES64 were
identical and every Gerbicz check passed. Complete candidates/hour, not GPU
utilization, is the objective.

## Baseline scaling

| Digits | Transform | Wall / B32 | Candidates/h | Reduction ms (% GPU) | poly2int ms (% GPU) | Exact OpenCL buffers | Measured VRAM max | GPU mean | Power mean | Temp max |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 20k | 8,192 | 10.539 s | 10,930.51 | 7,702.810 (50.305%) | 3,704.456 (24.193%) | 15.334 MiB | 3,245 MiB | 41.50% | 105.22 W | 59 C |
| 40k | 16,384 | 19.156 s | 6,013.93 | 15,044.819 (47.186%) | 6,649.251 (20.855%) | 30.584 MiB | 3,368 MiB | 50.07% | 135.05 W | 62 C |
| 60k | 32,768 | 29.378 s | 3,921.31 | 21,997.420 (45.941%) | 11,087.727 (23.157%) | 61.084 MiB | 3,284 MiB | 63.20% | 187.37 W | 60 C |
| 80k | 32,768 | 38.354 s | 3,003.58 | 28,958.352 (46.143%) | 14,505.156 (23.113%) | 61.084 MiB | 3,271 MiB | 79.58% | 215.25 W | 63 C |
| 100k | 32,768 | 47.092 s | 2,446.25 | 36,089.628 (47.471%) | 16,763.983 (22.051%) | 61.084 MiB | 3,266 MiB | 63.69% | 196.19 W | 61 C |

The VRAM column is the maximum NVIDIA process/device reading and includes
driver/runtime allocations. The exact buffer column is derived from the known
OpenCL allocations and transform size. H2D was 0.992..3.908 ms per batch and
D2H was 0.717..1.105 ms: PCIe transfer is not the limiting phase.

At 100k, the unprofiled phase counters were: parameter construction 206.567
ms, witness selection 7.152 ms, `a^k` 1,120.563 ms, main NTT loop 45,512.424
ms, Gerbicz 89.238 ms, final reduction 0.293 ms and total worker 46,942.161
ms. These counters overlap conceptually with the kernel grouping and therefore
must not be added to the profiled kernel totals.

## Empirical scaling and transform cliffs

Across 20k to 100k, descriptive exponents in `T ~ digits^alpha` are:

| Phase | Global alpha |
|---|---:|
| Pointwise square | 1.215 |
| Forward NTT | 1.080 |
| Inverse NTT | 1.012 |
| Reduction | 0.960 |
| poly2int | 0.938 |
| End-to-end wall | 0.930 |

Pointwise square is the fastest-growing measured phase and its GPU-time share
grows the most: 8.768% at 20k to 12.482% at 100k. This is not a universal
complexity law. The local exponents vary sharply when the selected transform
changes, and only three observations share transform 32,768.

The first transform cliff is 20k to 40k (8,192 to 16,384); the second is 40k
to 60k (16,384 to 32,768). There is no further transform-size cliff from 60k
through 100k. The exact buffers double at both cliffs and then remain constant.
B32 therefore has no observed memory limit at or below 100k digits on this
RTX 5080.

Reduction plus poly2int accounts for 74.497% of profiled GPU time at 20k and
69.522% at 100k. Its domination declines slightly rather than worsening, but
it remains by far the largest optimizable cost at 100k. Baseline reduction
made six full-range kernel passes per loop boundary: input normalization,
upsweep, top sweep, downsweep, output normalization and final normalization.
The identifiable redundant memory passes justified optimizing reduction before
the smaller but faster-growing square kernel.

## Retained adaptive implementation

The production patch is
`patches/proth20-adaptive-reduction-poly2int.patch` (SHA-256
`f6aae22cf4ad0d45c2d82a02d8ec7aee8339fd8deb413ed4d4e3b6f194b0ce40`).
It is applied to pinned upstream Proth20 revision
`6771325939a7ceef2c75644c79981c7df4a61882` and the generated OpenCL header is
recreated deterministically from `reduce.cl`.

The changes are classified as follows:

- **GENERIC:** fold input and output reduction into the adjacent scan kernels,
  then reconstruct the final high component during output. Three full-buffer
  launches/passes are removed while the globally synchronized up/top/down scan
  remains intact.
- **REGIME_SPECIFIC:** fold the rare poly2int correction only for measured
  B32 transforms up to 16,384 on the RTX 5080. The correction is still exact;
  the error flag selects corrected input digits and the top sweep performs the
  bounded sequential repair. Transform 32,768 keeps the separate safe path.
- **PARAMETRIC:** a central RTX 5080+B32 table selects measured plans by
  transform. Any other GPU, batch or transform uses the existing bounded safe
  autotuner rather than an unvalidated hard-coded choice.

| Transform | Square plan | poly2int plan | poly2int correction |
|---:|---|---|---|
| 8,192 | `1024_4 sq_8` | `p2i_4_64` | inline rare path |
| 16,384 | `256_8 sq_64` | `p2i_8_16` | inline rare path |
| 32,768 | `64_16 sq_512` | `p2i_16_16` | separate safe path |

The selected configuration is emitted at runtime, including GPU profile,
transform, batch, fusions, poly2int path and plan source. The table is small
and evidence-based; it is not a claim that these values are optimal on another
GPU. A future persistent tuner must key at least GPU identity, driver, kernel
source, transform and batch, and must require a minimum repeatable gain.

## A/B/B/A decisions

| Change | 20k result | 100k result | Decision |
|---|---:|---:|---|
| Boundary reduction fusion | +15.13% | +17.39% | retain, generic |
| Final reduction fusion | +11.65% | +11.34% | retain, generic |
| poly2int rare-fix folding | +13.32% | -2.10% | dispatch only at transform <=16,384 |
| Measured plan table vs live auto choice | +7.87% | +1.61% | retain, parametric |
| Complete baseline to final stack | **+45.33%** | **+28.02%** | retain |

The direct baseline/final bookend used independent A/B/B/A runs:

- 20k: 12,689.478 to 18,441.972 complete validated candidates/hour,
  speedup 1.453327848.
- 100k: 2,502.822 to 3,204.174 complete validated candidates/hour,
  speedup 1.280224476.

All runs returned identical mathematical records, witnesses and RES64 values;
Gerbicz was `PASS`. The large-transform poly2int variant was deliberately not
forced globally after its measured regression.

## Remaining profile

The last detailed post-fusion profile, taken before the final static-plan
bookend, shows at 100k: reduction 32.968%, poly2int 28.516%, pointwise square
14.281%, forward NTT 12.761% and inverse NTT 11.294% of profiled GPU event
time. Reduction plus poly2int remains dominant at 61.484%, but the remaining
reduction work is the three-stage globally synchronized scan. OpenCL C 1.2
does not provide a safe device-wide barrier inside one kernel, so blindly
fusing those stages would be a correctness risk.

The next high-value prototype should therefore address the remaining
large-transform conversion/scan traffic with a distinct hierarchical or
multi-candidate layout, and compare it against pointwise-square work at the
same transform. It must remain a bounded A/B/B/A experiment at both 20k and
100k and be integrated through the central dispatch if regime-specific.

## Recovery and product gates

The ordinary Windows Debug and Release clean build completed with zero observed
PrimeForge warnings. CTest passed 43/43 in Debug and 43/43 in Release; both
self-tests passed with C++23 reported. The five-candidate production recovery
test stopped after 2 durable results, resumed to 5, produced zero gaps and zero
duplicates, and agreed with PARI/GP 2.17.4 on all 5. Its status is `PASS` and
its summary SHA-256 is
`1880475dc7338b171806c50663c34e55d4ab624cc695499da1fe13f51a2f7ecd`.

The freshly rebuilt production oracle reproduced both 20k and 100k reference
result hashes. No discovery campaign was launched.

## Projection for a 100k-digit campaign

This is a projection from measured full-candidate throughput, not a campaign
result. For the existing illustrative 100k-digit portfolio row with 344,897
raw candidates, the measured baseline projects 137.80 GPU-hours and the final
adaptive version 107.64 GPU-hours. If a 10-million prime sieve leaves the
previously measured 24,043 survivors, the corresponding Proth20-only projection
is 9.61 versus 7.50 GPU-hours. Sieve, controller, proof, retry and thermal costs
are not included, and the actual survivor count at a future selected depth may
differ. No authorization to run such a campaign is implied.

## Measurement limitations

- NVIDIA OpenCL does not expose per-kernel register allocation here: `UNKNOWN`.
- Exact synchronization wait time is not exposed by the OpenCL 1.2 fast queue:
  `UNKNOWN`.
- Bytes moved per kernel are derived only where buffer topology is known; no
  hardware memory-traffic counters were available.
- GPU power is NVIDIA telemetry, not whole-machine energy. CPU/package energy
  unavailable to this experiment remains `UNKNOWN`; no TDP substitution is
  made.
- Plan selection varied across fresh processes, which is why only repeated
  measured entries were promoted into the central table.

## Contribution directe au logiciel final

This milestone removes three large-buffer reduction passes, retains a safe
poly2int specialization only where it wins, and makes plan selection aware of
GPU, batch and transform. It directly increases complete validated candidate
throughput at both 20k and 100k while preserving checkpoint/recovery behavior.
The reduction-fusion work is complete for the current three-stage scan design.
Future work is justified only as a structural scan/layout change or as work on
the now-visible square/NTT cost; further local micro-tuning is not justified by
this profile.
