# Target CPU/GPU pipeline

## Data flow

The CPU canonicalizes a bounded work unit, compiles congruences and produces
compact candidate batches. The GPU receives only versioned batch formats suited
to its validated kernels. CPU verification consumes GPU outputs, reconstructs
witnesses or reruns reference checks, and the storage stage atomically commits
results and progress.

```text
work ledger -> CPU prepare/sieve -> bounded H2D queue -> GPU batches
     ^                                                    |
     |                                                    v
checkpoint <- durable writer <- CPU verify <- bounded D2H queue
```

Preparation, transfer, GPU execution, verification and durable I/O have separate
timers and queue-depth counters. A batch is complete only after CPU validation and
durable ledger update. GPU completion alone never promotes primality or
verification status.

## Concurrency contract

Queues are bounded by count and bytes. Back-pressure propagates toward the work
planner; allocation failure never triggers unbounded buffering. Buffer ownership
has explicit states, sequence ids and content hashes. One, two and three-buffer
configurations are measured, not assumed. CUDA streams and CPU workers are
autotuned while reserving enough CPU capacity for the driver, watchdog,
verification and durable writer.

Cancellation first stops new work, then synchronizes or abandons in-flight GPU
work, validates completed outputs, writes a checkpoint and exits. The independent
watchdog may request this sequence and force termination after a recorded timeout.
On restart, the ledger replays only durably completed ids and safely resubmits the
rest.

## Correctness and observability

Every optimized batch format has a scalar decoder and CPU reference path. Fixed
seed, edge, carry-chain and mutation vectors cross both implementations. Raw CUDA
errors, device resets, sequence gaps, duplicate ids and mismatched hashes make the
run invalid and are retained for diagnosis.

The pipeline emits queue occupancy, time per stage, blocked time, RAM/VRAM use,
transfer bytes, kernel ids, checkpoint ids and telemetry assessments. Utilization
is diagnostic: 100% CPU or GPU usage is neither required nor evidence of optimal
end-to-end performance.

## Implemented pipeline

PIVOT-05 implements the bounded arithmetic adapter and PIVOT-06 composes one
adapter/stream per slot. One, two and three slots have exact portable and RTX 5080
tests. Submission is concurrent and bounded by the slot count; completion is
committed at one ordered frontier only after CPU verification. Canonical JSONL
results are appended durably before an atomic checkpoint authenticates their byte
length, SHA-256 and task-set identity.

A cooperative stop disables new submissions and drains already submitted slots.
Resume verifies the complete checkpointed prefix and rolls back only a later
unauthenticated suffix. Divergence, backend error, task mutation, checkpoint
mutation and ledger mutation all fail closed. The portable synchronous adapter
and one-slot pipeline remain reference fallbacks.
