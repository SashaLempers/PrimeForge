# Stage 1 — Reproducible C++23 build

**Status:** PASS locally

**Date:** 2026-08-02

**CI status:** pending milestone push

## Toolchain and machine observed

| Component | Exact observed value |
|---|---|
| Visual Studio | Community 2026 18.8.2 |
| MSVC | 19.51.36252.0 |
| `_MSC_VER` | 1951 |
| `_MSC_FULL_VER` | 195136252 |
| `__cplusplus` | 202400 (`C++23`) |
| CMake | 4.3.1-msvc1 |
| Ninja | 1.13.2 |
| Windows SDK | 10.0.26100.0 |
| Git | 2.53.0.windows.3 |
| GitHub CLI | 2.97.0 |
| OS | Microsoft Windows 11 Famille, 10.0.26200, x86_64 |
| CPU | AMD Ryzen 9 9950X3D 16-Core Processor; 16 physical, 32 logical cores |
| CPU capabilities | SSE2 yes; AVX yes; AVX2 yes; AVX-512F yes; BMI2 yes |
| GPU 0 | NVIDIA GeForce RTX 5080; driver 32.0.16.1074 (NVIDIA 610.74); 16303 MiB reported by `nvidia-smi` |
| GPU 1 | AMD Radeon(TM) Graphics; driver 32.0.21042.62 |
| Electrical consumption | `UNKNOWN` |

GPU availability and names are inventory only. No GPU computing backend or power estimate is present.

## Required gate commands and exact outcomes

```powershell
cmake --preset msvc-debug
cmake --build --preset msvc-debug --parallel
ctest --preset msvc-debug --output-on-failure
& .\out\build\msvc-debug\primeforge-selftest.exe
cmake --preset msvc-release
cmake --build --preset msvc-release --parallel
ctest --preset msvc-release --output-on-failure
& .\out\build\msvc-release\primeforge-selftest.exe
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts\run_all.ps1 -Clean
```

- Debug configure: PASS.
- Debug build: PASS, 8 Ninja steps, zero PrimeForge compiler warnings.
- Debug CTest: PASS, 2/2 tests.
- Debug self-test: PASS.
- Release configure: PASS.
- Release build: PASS, 8 Ninja steps, zero PrimeForge compiler warnings.
- Release CTest: PASS, 2/2 tests.
- Release self-test: PASS.
- Clean automation: PASS; Debug and Release rebuilt, 2/2 CTest tests passed in each configuration, both self-tests printed `selftest.status=PASS`, final line `PrimeForge complete stage-1 verification: PASS`.

The first clean automation attempt reached and passed Debug CTest, then stopped because PowerShell rejected an explicitly bound empty mandatory argument array before launching the self-test. `Invoke-External` was corrected to default its argument array and the entire clean gate was rerun successfully from the beginning.

## Verified contracts

- C++23 requested through `cxx_std_23`, extensions disabled.
- `/W4`, `/permissive-`, `/Zc:__cplusplus`, and `/WX` applied to original PrimeForge targets.
- `PrimalityStatus`, `VerificationStatus`, and `NoveltyStatus` remain independent.
- `PROBABLE_PRIME` is never promoted to `PROVEN_PRIME`.
- The fake `EngineAdapter` passes its support and result contract tests.
- Canonical JSON rules are documented; no floating-point values are permitted.
- The internal SHA-256 interface and digest representation pass tests; no cryptographic backend has been integrated yet.
- No OpenSSL, primesieve, GMP, FLINT, CUDA, or external numerical engine was installed or linked.
- `actions/checkout` is pinned to immutable commit `11d5960a326750d5838078e36cf38b85af677262` with an individual license decision.
- The specification's staged Git blob and working-tree file both hash to `A87A3DDDF710E3080C9A3F1247644FF9505ECBCF470358015E9EDF19214A01C1` after disabling text normalization for specifications.

## Limitations at this gate

- The Linux workflow is compile-testable by design but its hosted CI result is not recorded until after the milestone push.
- Electrical consumption is `UNKNOWN`; TDP was not used as a substitute.
- No performance benchmark or performance claim exists.
- Canonical JSON has a normative format document but no full encoder yet.
- SHA-256 has an injectable interface but no production cryptographic implementation yet.
