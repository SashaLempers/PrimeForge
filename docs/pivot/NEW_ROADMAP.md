# Hardware-specialized roadmap

This roadmap supersedes the former generic stage 0-20 execution order. Each
milestone requires clean Debug and Release builds, all applicable tests, updated
documentation, a milestone commit, a push to the private repository and green
Windows/Linux CI. Linux validates portable correctness; Windows validates the
target fast path.

Current progress: PIVOT-00 is accepted at commit `ce22b91`. PIVOT-01, including
CUDA Toolkit 13.3 detection, is accepted at commit `9379303`. PIVOT-02 is accepted
at commit `10699d3`; private Windows/Linux CI run `30767609057` is green. The
private end-to-end MVP is released at `v0.1.0-mvp`. PIVOT-03 has validated
topology-aware Ryzen placement, bounded nonnegative proper-factor checks and
single-pass factor evidence. Aligned canonical segments eliminate per-worker
full bitsets and their merge, while compiled residue classes eliminate the full
rule-by-candidate scan; no fastest configuration is claimed. Its next
work is monitored, disjoint selection of thread, placement, segment and SIMD
parameters on the complete sieve path.

## PIVOT-00 — Audit and reorientation

Classify all existing work, establish the exclusive hardware target, preserve
scientific guarantees, define the CPU/GPU/memory architecture, autotuning,
performance and CUDA protocols, and subordinate the old roadmap. Gate: the eight
pivot documents and governance updates agree; all existing tests still pass.

## PIVOT-01 — Real hardware profile

Produce canonical `hardware_profile.json` and a deterministic profile id. Record
source and confidence for CPU/GPU/RAM, topology, toolchain, driver, toolkit and
available sensors. Values are `DECLARED`, `DETECTED`, `MEASURED` or `UNKNOWN`.
Gate: stable bytes/id across repeated collection and redacted, non-secret output.

## PIVOT-02 — Target measurement and independent watchdog

Implement continuous telemetry, raw append-only logs, configurable documented
thresholds, performance invalidation, graceful/forced stop, checkpoint
preservation and recovery. The watchdog is a distinct process and is deliberately
fault-tested before prolonged work. Gate: simulated hangs, sensor loss, threshold
violations and corrupt checkpoints are handled safely. Detailed competing-process
attribution waits for a concrete engine benchmark; PIVOT-02 does not grow into a
generic machine-management subsystem.

## PIVOT-03 — Ryzen CPU autotuning

Measure threads, SMT, processor placement, segment sizes, cache regimes, SIMD,
allocation and scheduling on disjoint calibration/validation loads. Gate: exact
reference agreement and a stable profile; an inconclusive result keeps the safe
reference configuration.

## PIVOT-04 — Official CUDA installation and validation

Only now install a compatible official toolkit after pinning version, source and
license. Compile a minimal program, query the device, test transfers and a trivial
kernel, and compare results with CPU. Gate: optional CMake CUDA configuration,
clean non-CUDA build, exact vectors and no distribution ambiguity.

## PIVOT-05 — Modular CUDA backend

Implement bounded fixed-size arithmetic/batches, buffers and streams behind a
small interface. Keep the scalar CPU reference. Gate: exhaustive boundary vectors
and large fixed-seed differential suites with zero disagreement.

## PIVOT-06 — Asynchronous CPU/GPU pipeline

Add bounded queues, double/triple buffering experiments, transfer/compute overlap,
back-pressure, cancellation, append-only events and resumable checkpoints. Gate:
no lost/duplicated work under injected interruption and exact end-to-end outputs.

## PIVOT-07 — Global autotuner

Tune CPU, GPU, memory and pipeline parameters jointly without validation leakage.
The profile is bound to hardware, software and commit hashes. Gate: stable validated
selection; maximum utilization is not an objective by itself.

## PIVOT-08 — Select one target family

Compare candidate families on sieve cost, PRP/proof path, known coverage,
licensing and CPU/GPU suitability. This is the point for any materially
incompatible mathematical choice. Gate: bounded family, proof plan, novelty plan
and no external assignment/contact without separate authorization.

## PIVOT-09 — Honest reference comparison

Run equivalent work, proof level, checkpoints and environmental controls against
the relevant pinned engines. Keep raw data and losses as well as wins. Gate: only
scoped, statistically supported claims; never a general “fastest” claim.

## PIVOT-10 — Limited real campaign

Run a small known or locally controlled range with complete ledger, recovery,
independent verification and cost measurement. Gate: exact coverage and bounded
recovery loss. No publication or external assignment is implicit.

## PIVOT-11 — Bottleneck-only optimization

Profile the complete pipeline, identify the dominant measured bottleneck, change
only that component and repeat the reference comparison. Gate: reproducible
end-to-end benefit with unchanged correctness; otherwise record a negative result.

## PIVOT-12 — Prolonged campaign

Duration has no arbitrary maximum: hours, nights or days are allowed after the
PIVOT-02 watchdog gate and preceding correctness gates pass. Continuous monitoring,
documented thresholds, progressive checkpointing, recovery, interference
invalidation and a final hashed report are mandatory. A thermally stable slightly
slower setting may beat an unstable peak setting. Publication, third-party contact,
public repository visibility, external attribution and invasive hardware changes
remain separate actions and are not authorized by duration.

## Universal stop conditions

Stop or reduce load on mathematical divergence, CUDA error, hardware error,
resource exhaustion, threshold breach, persistent throttling, lost watchdog or
failed checkpoint. Sensor absence is `UNKNOWN`; it cannot be replaced by an
estimate. If safety cannot be established, prolonged execution is disabled.
