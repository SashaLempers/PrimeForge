# PIVOT-01 — real hardware profile

Date: 2026-08-02. Status: PASS locally and in private Windows/Linux CI, including
the CUDA inventory correction at commit `9379303`.

## Outcome

`scripts/collect_hardware_profile.ps1` creates a canonical, source-labelled and
privacy-reduced `profiles/hardware_profile.json`. It separates `DECLARED`,
`DETECTED`, `MEASURED` and `UNKNOWN`, excludes dynamic readings from identity,
and hashes exact canonical identity bytes. The already-installed official CUDA
Toolkit is detected and used only to build a short, generated inventory probe;
no NVIDIA binary is redistributed or linked into PrimeForge.

The local profile id is:

```text
sha256:f670d2a92f7fe817b6d550c7e9adc2da353d874e75d7469dc4438982cf54d32b
```

The complete profile file SHA-256 is
`209a438fe234cce2577a59439728fb75d35abc3b4c365cdeaea92fad6809e570`.
Two consecutive collections produced identical profile and identity bytes. The
file has no UTF-8 BOM or trailing newline.

## Principal detected facts

- Ryzen 9 9950X3D, 16 physical/32 logical processors, one active processor group;
- OS-enabled SSE2, AVX, AVX2, AVX-512F and BMI2;
- RTX 5080, NVIDIA driver 610.74, 16,303 MiB, compute capability string 12.0;
- two 32 GiB-class G.Skill modules, module sum 68,719,476,736 bytes, Windows
  physical total 66,184,978,432 bytes;
- Windows 10.0.26200, MSVC 19.51.36252.0, CMake 4.3.1-msvc1 and Ninja 1.13.2;
- CUDA Toolkit 13.3, `nvcc` 13.3.73, runtime 13.3, driver API 13.3,
  NVIDIA UMD 13.3 and RTX 5080 runtime compute capability 12.0;
- exact CUDA discovery paths are stored for `nvcc`, toolkit root, CUDART,
  `nvidia-smi`, the MSVC host compiler and the generated probe.

CPU temperature/effective frequency/power, GPU hotspot/VRAM temperature and
memory timings remain `UNKNOWN`. NVIDIA GPU temperature, SM clock, utilization,
power and free-VRAM queries are available, but PIVOT-01 records availability only.
No safe limit or performance value is inferred.

## New test gate

`primeforge.hardware_profile` runs the collector twice and checks exact byte
identity, canonical profile id, no BOM/trailing newline, source-state consistency,
and the required `UNKNOWN` fields. It disables `nvidia-smi` and CUDA toolkit
detection independently, requiring empty inventories and `UNKNOWN` values. When
`nvcc` exists, it requires complete toolkit/runtime metadata, a valid compute
capability and a named CUDA device. It is a
Windows inventory test. Linux/GCC keeps the portable 17-test correctness suite and
does not fabricate Windows hardware.

## Clean local gate

Command:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\run_all.ps1 -Clean
```

Results:

- Debug configure PASS, build 59/59, zero PrimeForge warnings;
- Debug CTest 18/18 PASS in 46.79 s; hardware-profile test 8.33 s;
- Debug explicit self-test PASS;
- Release configure PASS, build 59/59, zero PrimeForge warnings;
- Release CTest 18/18 PASS in 12.61 s; hardware-profile test 8.24 s;
- Release explicit self-test PASS;
- final script result: `PrimeForge complete local verification: PASS`.

The collector performs short inventory commands and compiles/runs only its
runtime API inventory probe. No benchmark, autotuning, CUDA workload kernel or
prolonged load ran in this milestone.

## Initial hosted failure and correction

Private run `30765994946` passed Linux/GCC in 1 min 01 s and the first 17 Windows
tests, then failed the hardware-profile test because the GPU-less runner caused an
empty PowerShell conditional result to become `$null`; strict-mode `.Count`
correctly stopped the collector. NR-0027 records the failure. The collector now
materializes an explicit array, and the local test forces this GPU-less path. A
corrected private run `30766235479` passed Linux in 1 min 04 s and Windows in
4 min 39 s. No warning or assertion was relaxed.

CUDA correction run `30766835906` then passed Linux in 1 min 04 s and Windows in
5 min 00 s. Hosted runners without CUDA retain `UNKNOWN`; the target machine
detects the complete installed stack.
