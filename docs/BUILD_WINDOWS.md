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

The script locates Visual Studio with `vswhere`, loads its developer environment, deletes only the two known build directories when `-Clean` is supplied, and stops at the first failure.
