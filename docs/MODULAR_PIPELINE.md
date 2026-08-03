# Bounded modular CPU/GPU pipeline

PIVOT-06 turns the exact modular CUDA backend into a bounded, resumable engine
path. The orchestration layer is portable and accepts injected backends, so Linux
and ordinary Windows builds test scheduling and durability without CUDA. The
target-only test supplies one to three real RTX 5080 backends.

## Ordering and back-pressure

Every backend instance owns one fixed-capacity device buffer pair and stream. The
pipeline submits at most one batch to each instance, so in-flight batch count and
memory are bounded by construction. Batches may finish out of order, but only the
oldest pending batch can advance the durable frontier. Its complete output is
recomputed with `multiply_mod_portable_reference` before acceptance.

For each accepted task the pipeline appends one deterministic JSONL record with
string-encoded unsigned integers. It flushes that batch to stable storage and
then atomically writes a checkpoint containing campaign id, next index, ledger
byte length, ledger SHA-256 and canonical task-set SHA-256. Append-only lifecycle
events use the existing durable logger.

## Stop and resume

A `std::stop_token` is observed at submission and commit boundaries. Once set,
no new batch is submitted; all already submitted batches are collected in index
order, CPU-verified and checkpointed. A stopped result exposes only that verified
committed residue prefix. Resume requires the same campaign, backend
schema and exact task identity. It validates the checkpoint, truncates only bytes
after its authenticated ledger length, validates the prefix hash, parses every
record and recomputes every stored residue before submitting remaining work.

The portable suite proves deterministic ledgers across one, two and three slots,
cooperative stop, exact resume, unauthenticated-suffix rollback, divergence and
backend failure, changed tasks, corrupt checkpoints and corrupt ledgers. The CUDA
suite repeats 1/2/3 streams on 4,097 tasks, stops after three 257-task batches and
resumes at 771. Both uninterrupted and resumed runs produce identical bytes.

## Current boundary

Checkpoint hashing currently covers the accumulated ledger at every committed
batch. This maximizes simple auditability but can become unnecessary work for a
very large campaign. It will be changed only if complete-pipeline profiling makes
it a measured bottleneck; increasing batch/checkpoint granularity is already a
safe reversible option. PIVOT-07 may select buffer and batch counts only with
disjoint, safety-valid evidence. Test timings are not performance claims.
