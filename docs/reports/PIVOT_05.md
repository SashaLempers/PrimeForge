# PIVOT-05 milestone report

**Status:** COMPLETE

**Date:** 2026-08-03

## Delivered engine primitive

- public `ModularBatchBackend` interface with stable v1 id;
- explicit unsigned 64-bit task schema and nonzero-modulus contract;
- fixed-capacity persistent input/output device buffers;
- one internal nonblocking CUDA stream;
- exact overflow-safe modular multiplication kernel;
- synchronous error-propagating host boundary;
- portable and optimized CPU differential oracle;
- deterministic boundary, random, repeat and invalid-request tests.

The first implementation chooses correctness over a premature reduction trick.
It is a direct engine component, not a generic GPU framework. PIVOT-06 can wrap
this synchronous primitive in the bounded resumable pipeline without changing its
mathematical contract.

## Exact local evidence

The clean CUDA script configured NVCC 13.3.73 with MSVC 19.51.36252.0 for compute
capability 12.0 and completed 99/99 build steps without a PrimeForge warning.
CTest passed 33/33 tests in 14.83 seconds; the modular test itself took 0.14
seconds. The explicit modular executable reported:

```text
cuda.modular.backend_id=primeforge.cuda.modular-u64.v1
cuda.modular.capacity=8192
cuda.modular.boundary_vectors=1000
cuda.modular.random_vectors=100000
cuda.modular.total_vectors=101000
cuda.modular.status=PASS
```

The executable was then repeated under Compute Sanitizer memcheck with the same
counts and status. Sanitizer result: `ERROR SUMMARY: 0 errors`.

The final ordinary non-CUDA gate rebuilt 93/93 targets in both configurations,
then passed Debug 31/31 tests in 47.17 seconds and Release 31/31 tests in 14.51
seconds. Both self-tests reported C++23. The closed package directory and ZIP each
passed with 15 files and zero external binaries; content-manifest SHA-256 was
`b4e728282568a95576ebd6d40572ae71e4da05301fc7f0ad446a8666fd82ab7d`
and diagnostic ZIP SHA-256 was
`cbdbf790affa0b1f398c8759215f7facb912f0e36dd15eb9231f9ae012e9a61a`.

Contract gates reject zero and excessive capacities, a negative device index,
mismatched input/output lengths, zero modulus and a batch one item beyond
capacity. Valid tasks are processed across 13 reused-buffer batches plus an exact
repeat batch. Every GPU residue equals both independent CPU paths and is below its
modulus.

## Safety and claim boundary

This was a short correctness run, not a performance benchmark or search campaign.
The post-run snapshot reported GPU 51 degrees Celsius, 45.31 W, no throttling,
13,589 MiB free VRAM and 43,570,868,224 bytes available RAM. WHEA events in the
preceding two hours: zero. Compute Sanitizer errors: zero. CPU temperature, CPU
package power and GPU memory temperature remain `UNKNOWN`; no performance claim
is made.

CUDA remains disabled by default. The local static backend, CUDA runtime content
and test executables are excluded from `primeforge.exe` and from the private MVP
package under the individual `TOOL-0008` license decision.

## Contribution directe au logiciel final

This milestone supplies the first real GPU computation layer the final prime
engine can call: exact fixed-width modular arithmetic with reusable device memory
and a stable adapter boundary. Modular multiplication is a direct building block
for sieving, PRP and proof arithmetic. The correctness foundation is finished for
u64 batches; it will need faster reductions and wider limbs only after the
end-to-end pipeline identifies the required family and measured bottleneck.
