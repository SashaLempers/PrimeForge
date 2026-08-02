# Memory strategy for the target

The user-declared target has 64 GB-class DDR5 and 16 GB-class GPU memory. PIVOT-01
records actual detected capacities; algorithms use available bytes, never the
declaration as an allocation guarantee.

## Host memory

PrimeForge partitions memory into immutable input metadata, CPU sieve state,
bounded candidate queues, pinned transfer buffers, verification state, checkpoint
staging and operating-system/user headroom. Limits are configured in bytes and as
a conservative fraction of currently available memory. The planner rejects a work
unit whose worst-case budget exceeds the campaign ceiling.

Large reusable buffers are preferred to repeated allocation. Alignment, huge
pages, NUMA placement and SoA/AoS layouts remain measurable experiments. Huge
pages are optional and reversible. Queue back-pressure prevents candidate bursts
from consuming unbounded RAM.

## Device and transfer memory

CUDA allocations use queried free/total VRAM with reserved headroom for the
display driver and other applications. Batch size contracts after allocation
failure or external VRAM pressure. Pinned host memory has its own strict ceiling;
it is not allowed to starve the OS or checkpoint writer. One, two and three-buffer
layouts are benchmarked under identical work.

## Validity and recovery

RAM/VRAM used and available, page faults and disk paging are sampled where
possible. Significant paging, allocation failure or memory-pressure intervention
invalidates performance results. Correctness outputs already validated remain
durable.

Checkpoints contain logical progress and hashes, not opaque transient buffer
addresses. Restart reconstructs buffers and safely replays incomplete batches.
Raw evidence and completed proofs are periodically flushed independently of large
working sets.

DDR5 channels, module count, data rate, timings, bandwidth, latency and NUMA/cache
interaction remain `UNKNOWN` until PIVOT-01 detection or PIVOT-03 measurement.
More allocated RAM is never presumed faster.
