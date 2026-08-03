# PIVOT-01 hardware profile

The canonical machine profile is `profiles/hardware_profile.json`. It is UTF-8
without BOM or trailing newline, uses deterministic key ordering, has no JSON
floating-point values, and labels every leaf `DECLARED`, `DETECTED`, `MEASURED`
or `UNKNOWN` with its source.

Profile id:

```text
sha256:dfa1e747642a0d2028c143a929eb9b0ca44bff360ff9a30fa93695b95e4037b7
```

The id hashes the canonical profile identity before insertion of `profile_id`.
The complete tracked file SHA-256 is
`902017b999f8c846d005b3f1177e7468fdb96f05486b4143c75124cb135c3755`.
Two immediate collections produced identical identity and profile bytes.

## Detected target

- motherboard: ASUSTeK ROG CROSSHAIR X870E HERO, Rev 1.xx;
- BIOS: American Megatrends 1605, reported release date 2025-07-15;
- CPU: AMD Ryzen 9 9950X3D, 16 physical cores, 32 logical processors, one
  active Windows processor group containing 32 logical processors;
- CPU features enabled by hardware/OS state: SSE2, AVX, AVX2, AVX-512F and BMI2;
- WMI aggregate caches: 1,280 KiB L1, 16,384 KiB L2 and 131,072 KiB L3;
- target GPU: NVIDIA GeForce RTX 5080, driver 610.74, 16,303 MiB reported memory,
  compute capability string 12.0, VBIOS 98.03.6c.00.36;
- CUDA Toolkit release 13.3, exact `nvcc` build 13.3.73, CUDA runtime 13.3,
  driver API capability 13.3 and NVIDIA UMD 13.3;
- CUDA runtime device query: RTX 5080 compute capability 12.0, 84
  multiprocessors, 17,094,475,776 bytes of global memory, 64 MiB L2, 256-bit
  memory bus, concurrent kernels, unified addressing, managed memory,
  cooperative launch and memory pools available;
- secondary adapter: AMD Radeon Graphics, driver 32.0.21042.62;
- memory modules: two WMI-reported 34,359,738,368-byte G.Skill
  F5-6000J3040G32G modules, SMBIOS memory type 34, configured clock field 6000
  MHz and speed field 4800 MHz;
- module-capacity sum: 68,719,476,736 bytes; Windows usable physical-memory
  report: 66,184,978,432 bytes;
- OS: Microsoft Windows 11 Home/Famille, 10.0.26200, 64-bit;
- Visual Studio 18.8.12023.21, MSVC binary 19.51.36252.0,
  `_MSC_FULL_VER=195136252`, CMake 4.3.1-msvc1, Ninja 1.13.2 and Git
  2.53.0.windows.3.

Every number above is a detected provider field, not a measured sustained value.
In particular, firmware maximum clock, WMI cache aggregation and memory clock
fields are not benchmark conclusions.

## Availability and unknowns

`nvidia-smi` exposes GPU temperature, SM clock, utilization, board power and free
VRAM queries. This records sensor availability only; PIVOT-01 retains no dynamic
sample and establishes no safe threshold.

On this target, the already-installed L-Connect 3 service exposes fresh CPU
package temperature, package power and effective clock through its local-only
`SystemResource` response. The profile records only `AVAILABLE`, never the
changing reading. The collector rejects stale, nonfinite, zero and out-of-range
values; `-DisableLConnectTelemetry` proves that absence remains `UNKNOWN`.

GPU hotspot temperature, GPU memory temperature and memory timings remain
`UNKNOWN`. WHEA and available-RAM sources are queryable but require campaign
policy before prolonged work.

CUDA discovery succeeded through the `v13.3` NVIDIA registry entry. The profile
records the exact toolkit root, `nvcc`, `cudart64_13.dll`, `nvidia-smi.exe`, MSVC
host compiler, probe source and generated probe paths. The generated probe stays
under ignored `out/`; no NVIDIA file is linked into or redistributed with
PrimeForge. `-DisableCudaToolkit`, `-DisableNvidiaSmi` and
`-DisableLConnectTelemetry` test the genuine unavailable paths, which must retain
`UNKNOWN`.

## Reproduction

After building Release:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File scripts\collect_hardware_profile.ps1 `
  -OutputPath profiles\hardware_profile.json `
  -SelfTestPath out\build\msvc-release\primeforge-selftest.exe
```

The collector deliberately excludes serial numbers, GPU UUIDs and unique PNP
instance suffixes. It performs only short inventory queries and launches no
benchmark or sustained load.
