# CUDA integration plan

No heavy CUDA implementation belongs to PIVOT-00 or PIVOT-01. The current main
build remains dependency-free and must succeed without a CUDA toolkit.

## PIVOT-04 acquisition gate — complete

Use only NVIDIA's official toolkit distribution compatible with the detected
driver, Windows and MSVC. Pin version, official source, installer/package hash,
license, provenance, redistribution decision and integration mode in the existing
governance files before use. A driver-exposed CUDA capability is not evidence that
`nvcc` or the toolkit is installed.

Add CUDA as an optional CMake language/profile, never an unconditional configure
requirement. Validate exact compiler/runtime/device versions, a minimal kernel,
host-to-device/device-to-host copies, synchronization, error propagation and a
CPU-computed golden vector. The non-CUDA Debug/Release and Linux reference gates
must remain green.

The target gate is pinned to the existing official installation at
`C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.3`: SDK 13.3.1,
NVCC 13.3.73, CUDA runtime 13.3.29 and RTX 5080 compute capability 12.0.
`PRIMEFORGE_ENABLE_CUDA` remains `OFF` by default; the explicit
`msvc-cuda-release` preset enables only the local validator. It transfers 4096
fixed boundary vectors, runs a deterministic integer kernel, compares every word
with the CPU reference and is checked with Compute Sanitizer memcheck. It is not
packaged or redistributed. Exact evidence is in `docs/reports/PIVOT_04.md`.

## Backend boundaries

CUDA code lives behind a narrow adapter. Versioned batch schemas use explicit
integer widths and byte order. Fixed-size arithmetic begins with small bounded
operations and carry edge cases. Every kernel has a scalar CPU reference and
fixed-seed differential tests. GPU output is untrusted until CPU verification.

PIVOT-05 now provides `ModularBatchBackend` version
`primeforge.cuda.modular-u64.v1`. It owns persistent fixed-capacity device buffers
and a nonblocking stream behind a synchronous host contract. The first exact
kernel uses overflow-safe add/double modular multiplication for every nonzero
64-bit modulus. One thousand Cartesian boundary vectors and 100,000 fixed-seed
vectors agree with both CPU references and pass Compute Sanitizer. This closes
the bounded arithmetic foundation; it deliberately makes no throughput claim.

PIVOT-06 composes multiple v1 backends without exposing CUDA handles. Each slot
owns its persistent buffers and nonblocking stream. The portable scheduler bounds
in-flight work, verifies GPU residues on CPU and commits only a contiguous durable
prefix. RTX 5080 tests cover one, two and three streams plus stop after three
submitted batches, authenticated suffix rollback and exact resume. All CUDA
pipeline runs are repeated under Compute Sanitizer.

Streams, pinned memory, cooperative groups, block dimensions, limb widths and
resident batch sizes are autotuned only after correctness. CGBN and other sources
remain audited references unless an individual license/integration decision
selects them; implementation details are not copied by default.

## Deferred work

Generic multiprecision, giant FFT/NTT, family-independent GPU frameworks and
production proof kernels are explicitly deferred. They begin only when a selected
target family demonstrates a measured need and an end-to-end opportunity. A
backend that does not beat its relevant complete-path reference remains a
documented negative result, not a default.

CUDA errors, device loss, watchdog loss, reference divergence and unsafe thermal
state stop new submissions and invoke checkpointed shutdown. No long GPU load runs
before PIVOT-02 monitoring is implemented and fault-tested.
