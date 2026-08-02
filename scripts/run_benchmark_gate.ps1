[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [string]$OutputDirectory = 'benchmarks\evidence\stage5',
    [ValidateRange(7, 999)][int]$Repetitions = 9,
    [string]$AmbientTemperatureMillicelsius = 'UNKNOWN',
    [string]$HardwareSettings = 'DEFAULT_LIMITS_REPORTED_UNVERIFIED',
    [string]$Ventilation = 'UNCHANGED_REPORTED_UNVERIFIED',
    [string]$WallEnergyMillijoules = 'UNKNOWN',
    [string]$WallEnergySource = 'NONE'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$resolvedOutput = if ([System.IO.Path]::IsPathRooted($OutputDirectory)) {
    [System.IO.Path]::GetFullPath($OutputDirectory)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $OutputDirectory))
}
$candidatePath = Join-Path $repositoryRoot 'corpus\v1\cases.tsv'
$candidateHash = (Get-FileHash -LiteralPath $candidatePath -Algorithm SHA256).Hash
$commit = (& git -C $repositoryRoot rev-parse HEAD).Trim()
$dirty = if (@(& git -C $repositoryRoot status --porcelain).Count -eq 0) { 'NO' } else { 'YES' }
if ($WallEnergyMillijoules -ne 'UNKNOWN') {
    $parsedEnergy = 0L
    if (-not [int64]::TryParse($WallEnergyMillijoules, [ref]$parsedEnergy) -or $parsedEnergy -lt 0) {
        throw 'WallEnergyMillijoules must be UNKNOWN or a non-negative integer.'
    }
    if ($WallEnergySource -eq 'NONE') {
        throw 'A reliable meter identifier is required with a wall-energy value.'
    }
} elseif ($WallEnergySource -ne 'NONE') {
    throw 'WallEnergySource must be NONE when wall energy is UNKNOWN.'
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$visualStudioPath = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if ([string]::IsNullOrWhiteSpace($visualStudioPath)) {
    throw 'No Visual Studio installation with the x64 C++ toolchain was found.'
}
$developerShell = Join-Path $visualStudioPath 'Common7\Tools\Launch-VsDevShell.ps1'
& $developerShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation

$cmake = Join-Path $visualStudioPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $cmake --preset msvc-release
if ($LASTEXITCODE -ne 0) { throw 'Release configuration failed.' }
& $cmake --build --preset msvc-release --parallel
if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }

$benchmark = Join-Path $repositoryRoot 'out\build\msvc-release\primeforge-benchmark.exe'
if (-not (Test-Path -LiteralPath $benchmark)) {
    throw "Benchmark executable not found: $benchmark"
}

New-Item -ItemType Directory -Force -Path $resolvedOutput | Out-Null
$computer = Get-CimInstance Win32_ComputerSystem
$bios = Get-CimInstance Win32_BIOS
$operatingSystem = Get-CimInstance Win32_OperatingSystem
$processor = Get-CimInstance Win32_Processor | Select-Object -First 1
$videoControllers = @(Get-CimInstance Win32_VideoController | Sort-Object Name)
$compilerPath = (Get-Command cl.exe).Source
$compilerVersion = (Get-Item -LiteralPath $compilerPath).VersionInfo.FileVersion
$powerProfileRaw = ((& powercfg.exe /getactivescheme) | Out-String)
$powerProfileMatch = [regex]::Match($powerProfileRaw, '[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}')
$powerProfile = if ($powerProfileMatch.Success) { $powerProfileMatch.Value.ToLowerInvariant() } else { 'UNKNOWN' }
$gpuDrivers = @($videoControllers | ForEach-Object { "$($_.Name)@$($_.DriverVersion)" })

$metadata = [ordered]@{
    ambient_temperature_millicelsius = $AmbientTemperatureMillicelsius
    background_policy = 'NOT_CONTROLLED; RESULTS_ARE_HARNESS_VALIDATION_ONLY'
    background_process_count = @(Get-Process).Count
    bios_manufacturer = [string]$bios.Manufacturer
    bios_version = [string]$bios.SMBIOSBIOSVersion
    captured_utc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    commit = $commit
    compiler_path = $compilerPath
    compiler_version = $compilerVersion
    cpu_name = ([string]$processor.Name).Trim()
    gpu_drivers = $gpuDrivers
    hardware_settings = $HardwareSettings
    memory_bytes = ([uint64]$computer.TotalPhysicalMemory).ToString()
    os_architecture = [string]$operatingSystem.OSArchitecture
    os_caption = [string]$operatingSystem.Caption
    os_version = [string]$operatingSystem.Version
    power_measurement = if ($WallEnergyMillijoules -eq 'UNKNOWN') { 'UNKNOWN' } else { 'WALL_METER' }
    power_profile = $powerProfile
    repository_dirty = $dirty
    schema_version = 1
    ventilation = $Ventilation
    wall_energy_millijoules = $WallEnergyMillijoules
    wall_energy_source = $WallEnergySource
    windows_profile = 'CURRENT_USER_PROFILE_RECORDED; NO_PROFILE_CHANGE_BY_PRIMEFORGE'
}

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
$metadataPath = Join-Path $resolvedOutput 'environment.json'
$metadataJson = $metadata | ConvertTo-Json -Depth 4 -Compress
[System.IO.File]::WriteAllText($metadataPath, $metadataJson, $utf8NoBom)

foreach ($runId in @('stage5-a', 'stage5-b')) {
    & $benchmark `
        --output-dir $resolvedOutput `
        --candidates $candidatePath `
        --run-id $runId `
        --commit $commit `
        --candidate-set-sha256 $candidateHash `
        --environment-metadata-file 'environment.json' `
        --repetitions $Repetitions `
        --seed 20260802
    if ($LASTEXITCODE -ne 0) {
        throw "Benchmark run failed: $runId"
    }
}

$summaryA = @(Import-Csv -LiteralPath (Join-Path $resolvedOutput 'stage5-a-summary.csv'))
$summaryB = @(Import-Csv -LiteralPath (Join-Path $resolvedOutput 'stage5-b-summary.csv'))
if ($summaryA.Count -ne $summaryB.Count -or $summaryA.Count -eq 0) {
    throw 'Successive benchmark summaries have different variant sets.'
}

$variantResults = New-Object System.Collections.Generic.List[object]
foreach ($left in $summaryA) {
    $right = $summaryB | Where-Object { $_.variant -eq $left.variant } | Select-Object -First 1
    if ($null -eq $right) {
        throw "Variant missing from second run: $($left.variant)"
    }
    $overlap = [uint64]$left.confidence_low_ns -le [uint64]$right.confidence_high_ns -and `
               [uint64]$right.confidence_low_ns -le [uint64]$left.confidence_high_ns
    $difference = [Math]::Abs([int64]$left.median_ns - [int64]$right.median_ns)
    $noise = [Math]::Max([uint64]$left.mad_ns, [uint64]$right.mad_ns)
    $variantResults.Add([ordered]@{
        compatible = if ($overlap) { 'YES' } else { 'NO' }
        median_difference_ns = $difference
        noise_floor_ns = $noise
        run_a_median_ns = [uint64]$left.median_ns
        run_b_median_ns = [uint64]$right.median_ns
        variant = $left.variant
    })
}

$compatible = @($variantResults | Where-Object { $_.compatible -ne 'YES' }).Count -eq 0
$compatibility = [ordered]@{
    candidate_set_sha256 = $candidateHash
    compatible = if ($compatible) { 'YES' } else { 'NO' }
    performance_claim = 'NONE'
    reason = if ($compatible) { 'CONSERVATIVE_CONFIDENCE_INTERVALS_OVERLAP' } else { 'SUCCESSIVE_RUN_INTERVALS_DO_NOT_OVERLAP' }
    run_a = 'stage5-a'
    run_b = 'stage5-b'
    schema_version = 1
    telemetry_status = 'UNAVAILABLE'
    variants = $variantResults
}
[System.IO.File]::WriteAllText(
    (Join-Path $resolvedOutput 'compatibility.json'),
    ($compatibility | ConvertTo-Json -Depth 5 -Compress),
    $utf8NoBom
)

if (-not $compatible) {
    throw 'Successive benchmark executions are not statistically compatible.'
}

Write-Host 'PrimeForge benchmark protocol gate PASS: successive runs compatible; performance claims NONE.'
