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

## Current evidence and unknowns

Earlier stage-1 evidence detected the named Ryzen processor, 16 physical/32
logical processors, SSE2, AVX, AVX2, AVX-512F and BMI2, plus an RTX 5080 and AMD
integrated graphics. Stage-5 evidence detected 66,184,978,432 bytes of installed
RAM. These are historical observations and are not the PIVOT-01 profile.

The fresh profile must determine what the current host actually exposes. CPU
cache topology, CCD/cache asymmetry, processor groups, sustained frequencies,
memory channels/speed/timings, usable RAM, GPU compute capability, exact VRAM,
CUDA toolkit, temperature sensors, power sensors, throttling and hardware-error
feeds remain `UNKNOWN` until recorded by a named source. TDP is never power.

## Safety boundary

PrimeForge does not change BIOS settings, EXPO, clock offsets, voltages, durable
power limits, fan curves, security controls or drivers. It may heavily use the
target only after an independent watchdog, documented thresholds, checkpointed
work and a tested stop path exist. Duration alone is not a stop condition.
