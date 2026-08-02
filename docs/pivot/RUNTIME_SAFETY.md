# PIVOT-02 runtime safety components

PIVOT-02 supplies the minimum runtime boundary required before a long engine
benchmark. It does not run a search and contains no performance claim.

## Components

`hardware_monitor` samples one record at a time and retains no history in memory.
On Windows it reads mean current logical-processor MHz through
`CallNtPowerInformation`, RAM through `GlobalMemoryStatusEx`, and NVIDIA data
through a fixed `nvidia-smi` query. On the target, the available fields are GPU
core temperature, board power and limit, SM/memory clocks, GPU/memory utilization,
VRAM used/free, RAM used/free and CPU frequency. NVIDIA software power cap,
software thermal slowdown, hardware thermal slowdown and hardware power-brake
reasons are checked explicitly. CPU temperature, CPU power and GPU memory
temperature currently remain `UNKNOWN`; no TDP or inferred value replaces them.

`benchmark_logger` writes one UTF-8 JSON object per line. Every event has a
campaign id, event type, opaque JSON payload, decimal sequence and UTC timestamp.
Each append is flushed to the operating system and durably synchronized. Reopen
continues the sequence; a different campaign or truncated final record is
rejected. Memory use is constant as the log grows.

`checkpoint_manager` stores campaign id, decimal progress, sequence and an opaque
engine payload. SHA-256 covers the canonical body. The existing cross-platform
atomic write/flush/replace primitive preserves the last durable checkpoint.
Noncanonical, truncated, mutated or hash-mismatched files are rejected before
resume.

`benchmark_watchdog` is a separate process. It polls the monitor, writes telemetry
through the logger, evaluates only explicitly supplied limits, and reacts to
reported throttling. A required sensor becoming unavailable is a stop condition.
It first atomically creates the worker stop file, then force-terminates a worker
that remains alive beyond `--grace-ms`. Ctrl+C or the watchdog control file uses
the same graceful path. Optional `--checkpoint` must validate before monitoring
starts. State and elapsed time are fixed-size and use a 64-bit steady clock; the
test suite advances it by seven days without sleeping.

## Exact commands

One live sample:

```powershell
& .\out\build\msvc-release\hardware_monitor.exe --once
```

Continuous samples until Ctrl+C (no benchmark is launched):

```powershell
& .\out\build\msvc-release\hardware_monitor.exe --samples 0 --interval-ms 1000
```

Create and validate a resumable checkpoint:

```powershell
& .\out\build\msvc-release\checkpoint_manager.exe write `
  --path out\campaigns\example\checkpoint.json --campaign example `
  --sequence 1 --progress 0 --payload initial
& .\out\build\msvc-release\checkpoint_manager.exe read `
  --path out\campaigns\example\checkpoint.json
```

Append a campaign event:

```powershell
& .\out\build\msvc-release\benchmark_logger.exe `
  --log out\campaigns\example\events.jsonl --campaign example `
  --event operator_note --payload-json '{"note":"ready"}'
```

Guard an already-running worker whose PID is `$worker.Id`:

```powershell
& .\out\build\msvc-release\benchmark_watchdog.exe `
  --pid $worker.Id `
  --stop-file out\campaigns\example\worker.stop `
  --watchdog-stop-file out\campaigns\example\operator.stop `
  --checkpoint out\campaigns\example\checkpoint.json `
  --log out\campaigns\example\events.jsonl `
  --campaign example --interval-ms 1000 --grace-ms 30000 `
  --require-gpu-temperature --require-gpu-power `
  --max-gpu-temp-c DOCUMENTED_LIMIT `
  --max-gpu-power-w DOCUMENTED_LIMIT
```

`DOCUMENTED_LIMIT` is intentionally not a default. A real campaign must replace
it with a source-backed or explicitly approved value. To stop cleanly, create
`operator.stop`; the watchdog requests the engine checkpoint and stop, then
enforces the grace timeout.

## Fault gate

`primeforge.runtime` tests available/missing telemetry, all exposed metric
families, throttle invalidation, required-sensor loss, durable logger reopen,
truncated log rejection, checkpoint round trip/corruption, operator stop,
threshold stop, forced stop and simulated multi-day elapsed time.

`primeforge.watchdog_process` launches actual separate watchdog and worker
processes. One worker obeys the stop file; another deliberately ignores it and is
force-terminated. Both paths must leave durable events. Debug, Release, Windows
and Linux CI run these tests. No massive benchmark is part of this gate.

## Remaining boundary

CPU temperature and package power need a reliable, licensed provider before a
campaign may require them. GPU memory temperature is unavailable from the current
driver query. No automatic process closure exists. Concrete engine benchmarks may
later add workload-specific interference observation, but PIVOT-02 is otherwise
finished and should not become an infrastructure project.
