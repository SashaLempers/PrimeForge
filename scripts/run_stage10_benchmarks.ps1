[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [string]$OutputDirectory = 'benchmarks\evidence\stage10',
    [ValidateRange(7, 99)][int]$Repetitions = 7
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot
if (@(& git -C $root status --porcelain).Count -ne 0) {
    throw 'Stage 10 retained evidence requires a clean Git worktree.'
}
$commit = (& git -C $root rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve the benchmark commit.' }

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$visualStudio = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if ([string]::IsNullOrWhiteSpace($visualStudio)) {
    throw 'No Visual Studio C++ installation was found.'
}
& (Join-Path $visualStudio 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$cmake = Join-Path $visualStudio 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $cmake --preset msvc-release
if ($LASTEXITCODE -ne 0) { throw 'Release configuration failed.' }
& $cmake --build --preset msvc-release --parallel
if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }

$output = if ([System.IO.Path]::IsPathRooted($OutputDirectory)) {
    [System.IO.Path]::GetFullPath($OutputDirectory)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $root $OutputDirectory))
}
New-Item -ItemType Directory -Path $output -Force | Out-Null
$computer = Get-CimInstance Win32_ComputerSystem
$bios = Get-CimInstance Win32_BIOS
$os = Get-CimInstance Win32_OperatingSystem
$cpu = Get-CimInstance Win32_Processor | Select-Object -First 1
$gpus = @(Get-CimInstance Win32_VideoController | Sort-Object Name)
$compiler = (Get-Command cl.exe).Source
$lines = @(
    "key`tvalue",
    "schema_version`t1",
    "commit`t$commit",
    "repository_dirty`tNO",
    "captured_utc`t$([DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ'))",
    "compiler_path`t$compiler",
    "compiler_version`t$((Get-Item -LiteralPath $compiler).VersionInfo.FileVersion)",
    "os_caption`t$($os.Caption)",
    "os_version`t$($os.Version)",
    "os_architecture`t$($os.OSArchitecture)",
    "cpu_name`t$(([string]$cpu.Name).Trim())",
    "physical_cores`t$($cpu.NumberOfCores)",
    "logical_cores`t$($cpu.NumberOfLogicalProcessors)",
    "memory_bytes`t$([uint64]$computer.TotalPhysicalMemory)",
    "bios_manufacturer`t$($bios.Manufacturer)",
    "bios_version`t$($bios.SMBIOSBIOSVersion)",
    "gpu_adapters`t$((@($gpus | ForEach-Object { "$($_.Name)@$($_.DriverVersion)" }) -join ';'))",
    "sampling_profiler`tUNKNOWN",
    "hardware_counters`tUNKNOWN",
    "cpu_temperature_celsius`tUNKNOWN",
    "cpu_frequency_hz`tUNKNOWN",
    "electrical_power_watts`tUNKNOWN",
    "wall_energy_joules`tUNKNOWN",
    "hardware_errors`tUNKNOWN",
    "throttling`tUNKNOWN",
    "timed_region`tcongruence compilation; complete family sieve; result hashing",
    "repetitions`t$Repetitions",
    "performance_valid`tNO",
    "performance_claim`tNONE"
)
$utf8 = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText(
    (Join-Path $output 'environment.tsv'),
    (($lines -join "`n") + "`n"),
    $utf8
)

$executable = Join-Path $root 'out\build\msvc-release\primeforge-family-sieve-benchmark.exe'
& $executable --output-dir $output --repetitions $Repetitions
if ($LASTEXITCODE -ne 0) { throw 'Stage 10 experiment executable failed.' }

$hashLines = @('sha256  file')
foreach ($name in @('environment.tsv', 'raw.tsv', 'summary.tsv')) {
    $hashLines += "$((Get-FileHash -LiteralPath (Join-Path $output $name) -Algorithm SHA256).Hash.ToLowerInvariant())  $name"
}
[System.IO.File]::WriteAllText(
    (Join-Path $output 'SHA256SUMS'),
    (($hashLines -join "`n") + "`n"),
    $utf8
)
Write-Host "Stage 10 retained diagnostic collection: PASS; performance claim NONE; output $output"
