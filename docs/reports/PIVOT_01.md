# PIVOT-01 — real hardware profile

Date: 2026-08-02. Status: PASS locally; private Windows/Linux CI required for
closure.

## Outcome

`scripts/collect_hardware_profile.ps1` creates a canonical, source-labelled and
privacy-reduced `profiles/hardware_profile.json`. It separates `DECLARED`,
`DETECTED`, `MEASURED` and `UNKNOWN`, excludes dynamic readings from identity,
and hashes exact canonical identity bytes. No external library or CUDA toolkit was
installed.

The local profile id is:

```text
sha256:a7a1af6ead57778ce7cadc240c7f4f14e103701d4e791228119ba2e901ae5648
```

The complete profile file SHA-256 is
`afac31ac41176cd7f5dafbc3b10de8eedc6d8b61015e13e0af39f5acf3eca7b6`.
Two consecutive collections produced identical profile and identity bytes. The
file has no UTF-8 BOM or trailing newline.

## Principal detected facts

- Ryzen 9 9950X3D, 16 physical/32 logical processors, one active processor group;
- OS-enabled SSE2, AVX, AVX2, AVX-512F and BMI2;
- RTX 5080, NVIDIA driver 610.74, 16,303 MiB, compute capability string 12.0;
- two 32 GiB-class G.Skill modules, module sum 68,719,476,736 bytes, Windows
  physical total 66,184,978,432 bytes;
- Windows 10.0.26200, MSVC 19.51.36252.0, CMake 4.3.1-msvc1 and Ninja 1.13.2;
- `nvcc`/CUDA toolkit not detected.

CPU temperature/effective frequency/power, GPU hotspot/VRAM temperature and
memory timings remain `UNKNOWN`. NVIDIA GPU temperature, SM clock, utilization,
power and free-VRAM queries are available, but PIVOT-01 records availability only.
No safe limit or performance value is inferred.

## New test gate

`primeforge.hardware_profile` runs the collector twice and checks exact byte
identity, canonical profile id, no BOM/trailing newline, source-state consistency,
and the required `UNKNOWN` fields. It is a Windows inventory test. Linux/GCC keeps
the portable 17-test correctness suite and does not fabricate Windows hardware.

## Clean local gate

Command:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\run_all.ps1 -Clean
```

Results:

- Debug configure PASS, build 59/59, zero PrimeForge warnings;
- Debug CTest 18/18 PASS in 41.69 s; hardware-profile test 4.09 s;
- Debug explicit self-test PASS;
- Release configure PASS, build 59/59, zero PrimeForge warnings;
- Release CTest 18/18 PASS in 8.54 s; hardware-profile test 4.10 s;
- Release explicit self-test PASS;
- final script result: `PrimeForge complete local verification: PASS`.

The collector performs short read-only inventory commands. No benchmark,
autotuning, CUDA kernel or prolonged load ran in this milestone.
