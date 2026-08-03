# Windows build

## Requirements

- Windows 11 x64;
- Visual Studio with Desktop development with C++;
- bundled or compatible CMake 3.25 or newer;
- Ninja;
- PowerShell 5.1 or newer.

No third-party PrimeForge library dependency is required at stage 1.

## Developer PowerShell commands

Open **Developer PowerShell for Visual Studio**, change to the repository root, then run:

```powershell
cmake --preset msvc-debug
cmake --build --preset msvc-debug --parallel
ctest --preset msvc-debug --output-on-failure
& .\out\build\msvc-debug\primeforge-selftest.exe

cmake --preset msvc-release
cmake --build --preset msvc-release --parallel
ctest --preset msvc-release --output-on-failure
& .\out\build\msvc-release\primeforge-selftest.exe
```

## Complete clean verification

From an ordinary PowerShell prompt:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/run_all.ps1 -Clean
```

The script locates Visual Studio with `vswhere`, loads its developer environment, deletes only the two known build directories when `-Clean` is supplied, and stops at the first failure. During CMake generation, PrimeForge repairs the specific double-encoded non-breaking-space sequence observed in CMake 4.3's detection of localized MSVC `/showIncludes` output. Correctly detected locale prefixes are left unchanged. This stabilizes Ninja header dependency metadata; it does not suppress or downgrade diagnostics.

## Optional local CUDA validation

The ordinary presets do not require CUDA. On the target machine only, the pinned
official CUDA 13.3 installation can be validated with:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
  -File scripts\run_cuda_validation.ps1 -Clean
```

The preset requires `C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v13.3`
and compiles only for compute capability 12.0. The CUDA target is local-only,
uses CUDA C++20 because that is the validated NVCC language level, and is never
included in `scripts/package_mvp.ps1`; all CPU PrimeForge targets remain C++23.
The script discovers Visual Studio with `vswhere`, loads Developer PowerShell,
runs configure/build/CTest, executes the validator explicitly and repeats it
under Compute Sanitizer memcheck.

## Smart App Control / Application Control

On a Windows host with Smart App Control or an enterprise Application Control policy in enforcement mode, a freshly linked unsigned development executable can be blocked before its main function runs. CTest then reports BAD_COMMAND or “Process not started”; the CodeIntegrity/Operational log records event 3077.

PrimeForge does not disable this protection. Microsoft documents that unknown unsigned code is blocked by default and that a trusted CA signature is the supported trust path:

https://learn.microsoft.com/windows/apps/develop/smart-app-control/overview

Use the private hosted Windows CI, an authorized development VM/policy, or an approved CA-backed signing process. Do not classify an Application Control launch block as a failed mathematical test, and do not claim a local PASS when the executable did not run.
