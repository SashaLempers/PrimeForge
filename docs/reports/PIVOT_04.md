# PIVOT-04 milestone report

**Status:** COMPLETE

**Date:** 2026-08-03

**Scope:** optional official CUDA installation and target validation only

## Pinned target and toolchain

- Toolkit root: `C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.3`
- CUDA SDK: 13.3.1
- NVCC: 13.3.73 (`V13.3.73`, build `compiler.38244171_0`)
- CUDA runtime: 13.3.29; runtime API observed by the executable: 13030
- NVIDIA driver: 610.74; driver API observed by the executable: 13030
- GPU: NVIDIA GeForce RTX 5080
- Compute capability: 12.0; 84 multiprocessors; warp size 32
- GPU global memory reported by CUDA: 17,094,475,776 bytes
- MSVC: 19.51.36252.0 (`_MSC_VER=1951`, `_MSC_FULL_VER=195136252`)
- CUDA translation-unit language: C++20; every ordinary PrimeForge target: C++23

Pinned local SHA-256 values:

- `nvcc.exe`: `92D993C6E7025E1597D0D895E65E8658F5F1D41576A477656647A7EECE2CD35F`
- `EULA.txt`: `7771F1EF8E9300BA175CF58E9DCC1DF55D9DD82D3FC960C4C6DB0C66354E418C`
- `LICENSE`: `088381BC2D891E719A2A9398645B00BB45F3B24231473A8283AC7E3E66B8A028`
- `version.json`: `6BEBB9CC1511781AE4F86B1906D2D1C2835A322A5014796157438AFE2471B063`
- `cudart64_13.dll`: `B00CA6F53699120DA815BF3E06E2E4285FAE2F201235B883DCBB50EEC51E2A2A`
- `cudart.lib`: `36A9DDCB459D2A79555282DA3BB19E9720B7EB6DC78D3D0694C701B5B73807CB`
- `cudart_static.lib`: `6CED79233DBCF5846869E089656AE785C95EF536D06CEC1715027853D0DECC05`

The toolkit has its own NVIDIA CUDA Toolkit EULA. `TOOL-0008` records the
individual license decision. No toolkit file or CUDA validation binary is
authorized for the PrimeForge package.

## Implementation

`PRIMEFORGE_ENABLE_CUDA` is `OFF` by default. The explicit
`msvc-cuda-release` preset pins toolkit 13.3.x and architecture 120. Its single
local validator performs H2D transfer, a deterministic unsigned-integer kernel,
synchronization, D2H transfer and a word-for-word CPU comparison over 4096 fixed
vectors including integer boundaries. CUDA and host warnings remain errors except
for C4211, disabled only on the NVCC-host-compiled target because NVCC 13.3 emits
it in generated registration code. Ordinary PrimeForge targets have no suppression.
`scripts/run_cuda_validation.ps1` supplies the supported ordinary-PowerShell
entry point: it discovers Visual Studio, loads the developer environment and
propagates every configure, build, test, validator and sanitizer exit code.

Observed validator result:

```text
cuda.device.name=NVIDIA GeForce RTX 5080
cuda.device.compute_capability=12.0
cuda.driver_api_version=13030
cuda.runtime_version=13030
cuda.validation.vectors=4096
cuda.validation.checksum=1797897575905442555
cuda.validation.status=PASS
```

Compute Sanitizer memcheck repeated the same exact result and reported:

```text
========= ERROR SUMMARY: 0 errors
```

## Gates

- Clean ordinary Debug build: PASS, zero PrimeForge warnings.
- Debug CTest: 31/31 passed, 0 failed, 48.24 seconds in the final clean script gate.
- Debug self-test: PASS, C++23.
- Clean ordinary Release build: PASS, zero PrimeForge warnings.
- Release CTest: 31/31 passed, 0 failed, 14.41 seconds in the final clean script gate.
- Release self-test: PASS, C++23.
- Clean CUDA configure/build: PASS; 95/95 build steps, zero source warnings.
- CUDA CTest: 32/32 passed, 0 failed, 14.48 seconds; CUDA test 0.13 seconds.
- CUDA validator: PASS; checksum `1797897575905442555`.
- Compute Sanitizer memcheck: PASS; zero errors.
- `scripts/run_all.ps1 -Clean`: PASS; 93/93 build steps in each configuration,
  both CTest gates and both C++23 self-tests passed.
- Closed package directory and ZIP: PASS; 15 files, zero external binaries,
  manifest SHA-256 `7eb5549cf694e5d9a6fd4718cf8c4459d039442dd709ab8aa0a97887cdd12ac7`.
- Diagnostic package ZIP SHA-256:
  `1a48b587584d43608d3c0fc76f0158ad3a96d1bbed480e29036537de6ebcfc0c`.

`dumpbin /dependents` shows no CUDA DLL dependency in either
`primeforge-cuda-validation.exe` or `primeforge.exe`. The validator PE contains
NVIDIA fatbinary sections and a statically selected runtime, so the entire local
validator is quarantined. `primeforge.exe` and the package contain no NVIDIA or
CUDA binary.

## Safety and limitations

Only short correctness checks ran. No CUDA benchmark, search campaign or
performance experiment ran. The post-sanitizer snapshot reported GPU 51 degrees
Celsius, 50.18 W, no throttling, 13,617 MiB free VRAM and 43,621,511,168 bytes
available RAM. WHEA events in the preceding two hours: zero. CUDA errors: zero;
all recorded exit codes: zero for the accepted gates.

CPU temperature, CPU package power and GPU memory temperature remain `UNKNOWN`
because no validated provider exposes them. Consequently this milestone makes no
performance claim and does not authorize a prolonged load. The fixed RTX 5080 and
CUDA 13.3 checks are intentionally target-specific; hosted CI validates only that
the default non-CUDA build remains portable.

Shell/environment failures NR-0040 through NR-0042 occurred before or outside project
execution, were corrected conservatively and did not weaken a project gate.

## Contribution directe au logiciel final

This milestone proves the exact compiler, device, memory-transfer, kernel-launch,
synchronization and error-reporting path that the future GPU arithmetic backend
needs. It is necessary because PIVOT-05 must build on a verified target rather
than treating driver-advertised CUDA support as sufficient. The acquisition and
minimal validation work is now finished; future work returns directly to the
engine by implementing bounded modular arithmetic behind a narrow CUDA backend.
Toolkit/license hashes must be revisited only when the toolkit changes.
