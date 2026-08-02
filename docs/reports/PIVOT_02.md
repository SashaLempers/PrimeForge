# PIVOT-02 — target measurement and independent watchdog

Date: 2026-08-02. Status: PASS at commit `10699d3`, locally and in private
Windows/Linux CI.

## Outcome

Four C++23 executables and their reusable `PrimeForge::runtime` API are complete:

- `hardware_monitor`;
- `benchmark_logger`;
- `checkpoint_manager`;
- `benchmark_watchdog`.

The watchdog is an operating-system process distinct from the worker. The local
target reports CPU mean current MHz, RAM, GPU temperature, power/limit, SM and
memory clocks, utilization, VRAM and NVIDIA throttle reasons. CPU temperature,
CPU power and GPU memory temperature remain `UNKNOWN`. No threshold is invented,
no TDP is used, and no massive or prolonged benchmark ran.

The logger is append-only, sequence-resumable and durably flushed. Checkpoints
are canonical, SHA-256 verified and atomically replaced. The watchdog accepts an
operator stop, reported throttling, required-sensor loss and explicitly configured
temperature/power thresholds; it requests graceful stop before a timed forced
termination.

## Faults found and corrected

The first strict compilation stopped on duplicated Windows macros and an
undocumented SDK structure name. CMake remains the single macro definition site,
and the documented `CallNtPowerInformation` binary layout is now represented by
an internal structure. No warning policy changed.

The first process-level test produced NR-0028: Windows `cmd.exe` rejected its
quoted relative leading executable. The conservative test launcher now rejects
shell metacharacters and passes the known project/CI paths directly. The corrected
test exercises both actual process paths.

## Local gate

Command:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\run_all.ps1 -Clean
```

Final clean results:

- Debug configure/build: PASS, 78/78 build steps, zero PrimeForge warnings;
- Debug CTest: 24/24 PASS in 46.23 s;
- Release configure/build: PASS, 78/78 build steps, zero PrimeForge warnings;
- Release CTest: 24/24 PASS in 13.46 s;
- explicit Debug and Release self-tests: PASS;
- script result: `PrimeForge complete local verification: PASS`.

`primeforge.watchdog_process` passed in 0.63 s Debug and 0.62 s Release. The
in-process fault suite passed in 0.04 s for both configurations.

Private CI run `30767609057` passed Linux in 1 min 18 s and Windows in 5 min
46 s. PIVOT-03 began only after both jobs completed successfully.

## Contribution directe au logiciel final

This milestone gives the final prime-search executable the exact safety boundary
needed for uninterrupted multi-hour or multi-day candidate generation, sieving,
CPU/GPU work and proof: durable progress, resume, raw evidence, real hardware
state, clean cancellation and containment of a hung worker. Without it, an
optimized engine could lose search coverage or continue through invalid thermal
or throttled samples.

The infrastructure is deliberately small and is complete as a standalone
boundary. It must later be wired to the engine's real progress payload and
workload-specific stop points. New dashboards, databases and generic machine
management are out of scope. After CI closes this milestone, work returns
immediately to the Ryzen engine path in PIVOT-03.
