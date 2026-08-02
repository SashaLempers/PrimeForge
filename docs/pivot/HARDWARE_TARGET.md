# PrimeForge exclusive performance target

## Scope

PrimeForge's only production performance target is the following user-declared
machine:

- AMD Ryzen 9 9950X3D, 16 physical cores and 32 logical processors;
- NVIDIA GeForce RTX 5080 with 16 GB-class VRAM;
- 64 GB-class DDR5 system memory;
- Windows 11, MSVC, CMake and CUDA.

These descriptions define the target, not a substitute for observation. PIVOT-01
must collect the actual device names, capacities, topology, firmware-visible
properties, drivers and tool versions. A declared or marketing value is labelled
`DECLARED`; an operating-system or device query is `DETECTED`; an experiment is
`MEASURED`; unavailable data is `UNKNOWN`. Conflicting sources are retained and
reported, never silently reconciled.

## Platform policy

Windows/MSVC/CUDA is the optimized fast path. Target-only translation units,
intrinsics, processor-group handling, pinned host memory and CUDA features are
permitted behind explicit interfaces and runtime checks. Linux/GCC remains a
correctness and serialization CI path. It must continue to compile the portable
reference, but it does not veto a measured Windows target optimization.

The scalar reference, exact/modular differential tests, factor witnesses,
canonical identities, independent status axes and checkpoint guarantees remain
mandatory on every path. A result from an optimized path is rejected on any
disagreement with its reference.

## Current profile

PIVOT-01 produced the source-labelled canonical profile documented in
`HARDWARE_PROFILE.md`. It detects the target CPU, GPU, memory modules, one Windows
processor group, toolchain and available NVIDIA queries. The profile id is
`sha256:a7a1af6ead57778ce7cadc240c7f4f14e103701d4e791228119ba2e901ae5648`.

CCD/cache asymmetry, sustained frequencies, measured bandwidth/latency, memory
timings, CPU temperature/power, GPU hotspot/VRAM temperature, safe thresholds and
throttling state remain `UNKNOWN`. The CUDA toolkit is not detected because
`nvcc` is absent. TDP is never power.

## Safety boundary

PrimeForge does not change BIOS settings, EXPO, clock offsets, voltages, durable
power limits, fan curves, security controls or drivers. It may heavily use the
target only after an independent watchdog, documented thresholds, checkpointed
work and a tested stop path exist. Duration alone is not a stop condition.
