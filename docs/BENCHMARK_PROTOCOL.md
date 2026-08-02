# PrimeForge benchmark protocol

## Claim boundary

`primeforge-benchmark` is a measurement harness established before the numerical engine. Its stage-5 `reference-a` and `reference-b` variants intentionally execute the same deterministic validation workload over the same 68 corpus values. Their timings validate scheduling, measurement, serialization, statistics, and compatibility checks; they do not compare algorithms and support no performance claim.

Every output contains `performance_claim=NONE`. If telemetry is unavailable, each sample also contains `valid_for_performance=NO` and `invalid_reason=TELEMETRY_UNAVAILABLE`. A later benchmark may support a scientific claim only after all environment controls and telemetry gates in this document are satisfied.

## Stable-measurement sequence

Startup, candidate parsing, output-directory creation, and initial allocation occur before the stable region. Each variant receives three warmup executions. For every one of at least seven repetitions, a deterministic splitmix64/Fisher–Yates schedule randomizes variant order using the recorded seed. Every variant sees the exact candidate file whose SHA-256 is recorded in every row.

Each sample records separate integer nanoseconds for:

- transfer/copy preparation;
- kernel workload;
- proof/checksum verification;
- row serialization I/O preparation;
- their integer sum.

File creation outside the sample loop is not folded into kernel time. Any future accelerator adapter must additionally measure actual host/device transfer and synchronization rather than reusing the current CPU copy field.

## Raw and summary data

Each run produces raw CSV and canonical compact JSON, plus summary CSV and JSON. JSON is UTF-8 without BOM or final newline, has deterministic key order, contains no floats, and encodes potentially large checksums as decimal strings. CSV uses UTF-8 and LF.

For each variant the summary reports count, minimum, maximum, lower median, median absolute deviation, and a conservative distribution-free median interval. At seven or more independent continuous samples, `[minimum, maximum]` has at least 98.4375% coverage for the population median; PrimeForge labels it a conservative 95% interval. This wide interval prevents small noisy differences from being overstated.

Two successive runs are compatible only when every variant has the same candidate hash, identical repetition policy, and overlapping conservative intervals. The gate reports absolute median difference beside the larger MAD noise floor. It never converts a difference smaller than noise into a gain.

## Environment record

`scripts/run_benchmark_gate.ps1` records the Git commit and dirty state, exact compiler path/version, Windows version and architecture, CPU, installed RAM, BIOS manufacturer/version, GPU names and driver versions, active Windows power-plan GUID, process count, hardware-setting statement, ventilation statement, and approximate ambient temperature when supplied.

Unknown values remain `UNKNOWN`. TDP is never substituted for measured power. The script accepts an optional non-negative wall-energy value only together with a meter identifier; otherwise wall energy and power remain `UNKNOWN`. Initial hardware limits are reported as default but unverified unless an operator supplies separately verified evidence. PrimeForge never closes background applications or changes the Windows profile automatically.

## Telemetry and invalidation

The benchmark library models CPU/GPU temperature, frequency, instantaneous power, hardware errors, and explicit throttle flags as optional telemetry. Tests prove that a hardware error, throttle flag, excessive temperature, or frequency collapse invalidates a sample. If no reliable provider exists, assessment is `UNAVAILABLE`, not `VALID`; the produced dataset is restricted to harness validation.

A claim-eligible future run must provide calibrated telemetry, thresholds fixed before measurement, no hardware errors, no throttle invalidations, acceptable temperatures, stable frequencies, a controlled background load, approximate ambient temperature, unchanged ventilation, and the same candidate set. Overclocked or undervolted settings are allowed only after the measurable stability gate defined in `DECISIONS.md`.

## Windows command

From the repository root:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_benchmark_gate.ps1 `
  -OutputDirectory benchmarks\evidence\stage5 `
  -Repetitions 9
```

For a reliable wall meter, add its measured integer millijoules and stable identifier. Do not enter TDP or a software estimate as wall energy.
