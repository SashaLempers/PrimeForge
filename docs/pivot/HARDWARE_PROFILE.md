# PIVOT-01 hardware profile

The canonical machine profile is `profiles/hardware_profile.json`. It is UTF-8
without BOM or trailing newline, uses deterministic key ordering, has no JSON
floating-point values, and labels every leaf `DECLARED`, `DETECTED`, `MEASURED`
or `UNKNOWN` with its source.

Profile id:

```text
sha256:a7a1af6ead57778ce7cadc240c7f4f14e103701d4e791228119ba2e901ae5648
```

The id hashes the canonical profile identity before insertion of `profile_id`.
The complete tracked file SHA-256 is
`afac31ac41176cd7f5dafbc3b10de8eedc6d8b61015e13e0af39f5acf3eca7b6`.
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

CPU temperature, CPU effective frequency, CPU power, GPU hotspot temperature,
GPU memory temperature and memory timings remain `UNKNOWN`. `nvcc` is absent from
the current environment, so the CUDA toolkit is `UNKNOWN`/not detected; the
NVIDIA driver and compute capability do not imply toolkit installation. WHEA and
available-RAM sources are queryable but are not yet a watchdog.

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
