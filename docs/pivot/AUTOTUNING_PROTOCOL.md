# Target autotuning protocol

## Search space

CPU variables include worker count, SMT, processor placement, segment size, batch
size, SIMD mode, allocation/alignment and static/dynamic scheduling. GPU variables
include threads per block, work per thread, batch size, stream count and buffer
count. Memory variables include queue capacities, pinned-host allocation and
working-set ceilings. Pipeline variables include stage concurrency, CPU reserves,
back-pressure watermarks and checkpoint cadence.

Unsupported choices are excluded by capability checks, not treated as slow
samples. Safety limits and memory ceilings are fixed before exploration.

## Method

Autotuning uses a versioned workload suite and deterministic randomized order.
Calibration selects candidates; disjoint validation decides retention. Short
screening is followed by repeated steady-state validation. Search overhead is
reported separately. All candidates must produce the same canonical result as the
reference before timing can influence selection.

The objective is stable end-to-end time, optionally energy when actually measured,
subject to zero divergence, no hardware/CUDA error, valid telemetry, bounded RAM
and VRAM, and no persistent throttling. Maximum device utilization is not an
objective. Ties within noise select the simpler, cooler or lower-resource setting.

Long tuning runs have no arbitrary duration ceiling after PIVOT-02. They require
the independent watchdog, progressive durable output, resumable search state and
the invalidation policy in `PERFORMANCE_PROTOCOL.md`.

## Profile identity

The selected canonical profile records the hardware-profile id, OS, driver,
toolkit, compiler, build options, PrimeForge commit, workload hashes, search space,
raw-data hashes, thresholds and selected parameters. It is reused only on an exact
compatibility match. A mismatch triggers validation or retuning, never silent use.

If no candidate is robustly better, the outcome is `INCONCLUSIVE` or `FAILED` and
the conservative reference remains active.

## PIVOT-03 outcome

The 2026-08-02 CPU study used 315 calibration and 315 held-out validation
executions. All canonical outputs were exact, but the CPU temperature and package
power fields remained `UNKNOWN` and regime rankings were inconsistent. The
outcome is therefore `INCONCLUSIVE`; no target profile or performance claim was
retained. See `docs/reports/PIVOT_03.md` and NR-0036.
