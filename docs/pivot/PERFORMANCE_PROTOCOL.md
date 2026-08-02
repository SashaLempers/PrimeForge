# Target performance and prolonged-run protocol

This document specializes `docs/BENCHMARK_PROTOCOL.md` for the target machine.
Duration alone is never a reason to stop. Safety, mathematical validity and
reproducibility are the gates.

## Continuous observations

When genuinely accessible, record CPU temperature/effective frequency/utilization/
power, GPU temperature/hotspot/VRAM temperature/effective frequency/utilization/
power, RAM and VRAM used/free, disk activity, CUDA/calculation errors, Windows
hardware errors and thermal/power throttling. Each field includes source and
timestamp. Inaccessible values are exactly `UNKNOWN`; TDP is not a measurement.

## Independent watchdog

A separate process must survive a blocked benchmark, poll resources, append raw
measurements, request graceful stop, force termination after a configured timeout,
and preserve the last durable work unit and completed results. Watchdog loss is a
stop condition. Before any prolonged campaign, tests deliberately exercise a hung
worker, graceful-stop failure, sensor loss, threshold breach and restart from the
last checkpoint.

## Thresholds and response

Every campaign stores warning, load-reduction, graceful-stop and forced-stop
thresholds plus maximum over-threshold duration and recovery hysteresis. Values
must come from documented device limits or explicitly approved settings; no
numeric temperature or power limit is invented.

On excessive temperature: reduce load, reduce CPU workers, reduce concurrent GPU
tasks, stop issuing batches, wait for recovery, resume less aggressively, then
stop if the condition persists. Every transition is logged.

## Competing processes

External CPU/GPU/RAM/VRAM/disk load is detected and can pause or reduce PrimeForge.
Automatic closure requires an explicit campaign allowlist, same-user-session
ownership, and no known unsaved work; the action is logged. PrimeForge never
automatically closes Windows/system/security/driver/hardware services, Visual
Studio, Codex, Git, project terminals, unknown processes or applications that may
contain unsaved data. Doubt means pause, not close.

## Performance validity

A series has `performance_valid=NO` after significant competing load, throttling,
threshold breach, interruption, calculation/CUDA error, abnormal frequency
instability, paging, watchdog loss or reference disagreement. Invalid data is
retained for diagnosis and excluded from tuning selection.

At least seven repetitions are used for short tests and at least three for costly
tests, with warmup, randomized order, raw data, median, dispersion and conservative
intervals. Microbenchmarks cannot justify an end-to-end claim.

## Campaign report

Report total and valid compute durations; configurations; temperature minima,
medians and maxima; frequencies; CPU/GPU use; measured consumption or `UNKNOWN`;
competing/closed processes; thermal pauses; throttling; errors; invalid series;
best stable configuration; long-run recommendation; exact command; raw files and
SHA-256 values. No “fastest” wording extends beyond the exact compared workload,
machine, versions and proof level.

## Prohibited automatic changes

Without separate explicit authorization PrimeForge never changes BIOS, EXPO,
overclock/undervolt, voltages, durable power limits, fan curves, thermal
protections, Windows Defender/security or the installed driver merely to improve a
benchmark.
