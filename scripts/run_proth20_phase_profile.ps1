[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [string]$CandidateFile = 'benchmarks\profiles\proth20_n66411_profile20.txt',
    [string]$OutputDirectory = 'out\benchmarks\proth20-phase-profile',
    [string]$Proth20Executable = 'out\oracles\proth20-batch\proth20-batch.exe',
    [string]$HardwareMonitorExecutable = 'out\build\msvc-release\hardware_monitor.exe',
    [ValidateRange(20, 100)][int]$CandidateCount = 20,
    [ValidateRange(0, 31)][int]$Device = 0,
    [ValidateRange(1, 10)][int]$DirectBaselineCount = 3,
    [ValidateRange(250, 5000)][int]$TelemetryIntervalMs = 1000,
    [ValidateRange(10, 600)][int]$IdleWaitSeconds = 120,
    [ValidateRange(0, 50)][int]$MaximumIdleGpuUtilizationPercent = 30,
    [ValidateRange(25.0, 150.0)][double]$MaximumIdleGpuPowerW = 80.0,
    [ValidateRange(40, 80)][int]$MaximumStartingGpuTemperatureC = 65,
    [switch]$AllowBusyGpu,
    [switch]$Resume
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$utf8 = [Text.UTF8Encoding]::new($false)
$culture = [Globalization.CultureInfo]::InvariantCulture
$repositoryRoot = Split-Path -Parent $PSScriptRoot

function Resolve-ProjectPath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $repositoryRoot $Path))
}

function Get-GpuSnapshot {
    $output = @(& nvidia-smi -i $Device `
        '--query-gpu=uuid,name,driver_version,temperature.gpu,power.draw,utilization.gpu,memory.used,memory.free,pstate' `
        '--format=csv,noheader,nounits' 2>$null)
    $nvidiaSmiExitCode = $LASTEXITCODE
    $line = @($output | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Select-Object -First 1)
    if ($nvidiaSmiExitCode -ne 0 -or $line.Count -ne 1) { throw 'nvidia-smi GPU snapshot failed.' }
    $fields = @($line[0] -split ',' | ForEach-Object { $_.Trim() })
    if ($fields.Count -ne 9) { throw "Unexpected nvidia-smi field count: $($fields.Count)" }
    return [pscustomobject][ordered]@{
        utc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ss.fffZ')
        uuid = $fields[0]
        name = $fields[1]
        driver_version = $fields[2]
        temperature_c = [int]$fields[3]
        power_w = [double]::Parse($fields[4], $culture)
        utilization_percent = [int]$fields[5]
        memory_used_mib = [uint64]$fields[6]
        memory_free_mib = [uint64]$fields[7]
        pstate = $fields[8]
    }
}

function Wait-GpuReady([Nullable[int]]$ReferenceTemperatureC) {
    $deadline = [DateTime]::UtcNow.AddSeconds($IdleWaitSeconds)
    do {
        $snapshot = Get-GpuSnapshot
        $temperatureLimit = if ($null -ne $ReferenceTemperatureC) {
            [Math]::Min($MaximumStartingGpuTemperatureC, [int]$ReferenceTemperatureC + 3)
        } else { $MaximumStartingGpuTemperatureC }
        $idle = $snapshot.utilization_percent -le $MaximumIdleGpuUtilizationPercent -and
            $snapshot.power_w -le $MaximumIdleGpuPowerW
        $cool = $snapshot.temperature_c -le $temperatureLimit
        if (($idle -and $cool) -or $AllowBusyGpu) { return $snapshot }
        Start-Sleep -Seconds 2
    } while ([DateTime]::UtcNow -lt $deadline)
    throw ("GPU did not become benchmark-ready: utilization={0}%, power={1} W, temperature={2} C. " +
        'No next PrimeForge workload was started, because the measurement would be invalid.') -f
        $snapshot.utilization_percent, $snapshot.power_w.ToString('0.00', $culture), $snapshot.temperature_c
}

function Stop-OwnedProcess($Process) {
    if ($null -eq $Process) { return }
    try {
        $Process.Refresh()
        if (-not $Process.HasExited) {
            Stop-Process -Id $Process.Id -Force -ErrorAction SilentlyContinue
            $Process.WaitForExit(5000) | Out-Null
        }
    } catch {
        Write-Warning "Could not stop owned process $($Process.Id): $($_.Exception.Message)"
    }
}

function Start-Telemetry([string]$RunPath) {
    $stdout = Join-Path $RunPath 'telemetry.jsonl'
    $stderr = Join-Path $RunPath 'telemetry.stderr.log'
    $process = Start-Process -FilePath $monitorPath -ArgumentList @(
        '--samples', '1000000', '--interval-ms', [string]$TelemetryIntervalMs
    ) -WorkingDirectory $RunPath -RedirectStandardOutput $stdout -RedirectStandardError $stderr `
        -PassThru -NoNewWindow
    return [pscustomobject]@{ process=$process; stdout=$stdout; stderr=$stderr }
}

function Write-CandidateFile([string]$Path, [object[]]$Candidates) {
    $text = (($Candidates | ForEach-Object { "$($_.k) $($_.n)" }) -join "`n") + "`n"
    [IO.File]::WriteAllText($Path, $text, $utf8)
}

function Assert-WorkerOutput([string]$RunId, [object[]]$Workers, [int]$ExpectedCount) {
    $verdicts = @{}
    foreach ($worker in $Workers) {
        if ((Get-Item -LiteralPath $worker.stderr).Length -ne 0) {
            throw "$RunId worker wrote stderr: $($worker.stderr)"
        }
        foreach ($line in ([IO.File]::ReadAllText($worker.stdout) -split '[\r\n]+')) {
            if ($line.Trim() -match '^PRIMEFORGE_BATCH_COMPLETE\t[0-9]+\t([0-9]+)\t([0-9]+)\t(PROVEN_PRIME|COMPOSITE)$') {
                $key = "$($Matches[1])/$($Matches[2])"
                if ($verdicts.ContainsKey($key)) { throw "$RunId duplicated candidate $key." }
                $verdicts[$key] = $Matches[3]
            }
        }
    }
    if ($verdicts.Count -ne $ExpectedCount) {
        throw "$RunId completed $($verdicts.Count)/$ExpectedCount candidates."
    }
    return $verdicts
}

function Invoke-BatchRun([string]$RunId, [int]$WorkerCount, [Nullable[int]]$ReferenceTemperatureC) {
    $runPath = Join-Path $outputPath $RunId
    if ($Resume -and (Test-Path -LiteralPath $runPath -PathType Container)) {
        $existingWorkers = @(Get-ChildItem -LiteralPath $runPath -Directory -Filter 'worker-*' |
            Sort-Object Name | ForEach-Object {
                [pscustomobject]@{
                    stdout = Join-Path $_.FullName 'profile.stdout.log'
                    stderr = Join-Path $_.FullName 'profile.stderr.log'
                }
            })
        $telemetryPath = Join-Path $runPath 'telemetry.jsonl'
        $preflightPath = Join-Path $runPath 'preflight-gpu.json'
        if ($existingWorkers.Count -eq $WorkerCount -and
            (Test-Path -LiteralPath $telemetryPath -PathType Leaf) -and
            (Test-Path -LiteralPath $preflightPath -PathType Leaf)) {
            $verdicts = Assert-WorkerOutput -RunId $RunId -Workers $existingWorkers `
                -ExpectedCount $CandidateCount
            $telemetryLines = @([IO.File]::ReadLines($telemetryPath) |
                Where-Object { $_.Trim().Length -ne 0 })
            if ($telemetryLines.Count -ne 0) {
                return [pscustomobject][ordered]@{
                    run = $RunId
                    workers = $WorkerCount
                    candidates = $CandidateCount
                    elapsed_seconds = 'RECORDED_IN_PHASE_LOGS'
                    candidates_per_hour = 'RECORDED_IN_PHASE_LOGS'
                    preflight_gpu = Get-Content -Raw -LiteralPath $preflightPath | ConvertFrom-Json
                    telemetry_samples = $telemetryLines.Count
                    verdicts = $verdicts
                    status = 'REUSED_COMPLETED'
                }
            }
        }
        throw "Cannot resume incomplete or inconsistent run: $runPath"
    }
    New-Item -ItemType Directory -Path $runPath -Force | Out-Null
    $preflight = Wait-GpuReady -ReferenceTemperatureC $ReferenceTemperatureC
    [IO.File]::WriteAllText((Join-Path $runPath 'preflight-gpu.json'),
        ($preflight | ConvertTo-Json -Depth 4) + "`n", $utf8)

    $groups = @()
    for ($workerIndex = 0; $workerIndex -lt $WorkerCount; ++$workerIndex) {
        $groups += ,@($candidates | Where-Object { $_.ordinal % $WorkerCount -eq $workerIndex })
    }
    $workers = [Collections.Generic.List[object]]::new()
    $telemetry = $null
    $timer = [Diagnostics.Stopwatch]::StartNew()
    try {
        $telemetry = Start-Telemetry -RunPath $runPath
        Start-Sleep -Milliseconds $TelemetryIntervalMs
        for ($workerIndex = 0; $workerIndex -lt $WorkerCount; ++$workerIndex) {
            $workerPath = Join-Path $runPath ('worker-{0:d2}' -f ($workerIndex + 1))
            New-Item -ItemType Directory -Path $workerPath -Force | Out-Null
            $batchPath = Join-Path $workerPath 'batch.txt'
            Write-CandidateFile -Path $batchPath -Candidates $groups[$workerIndex]
            $stdout = Join-Path $workerPath 'profile.stdout.log'
            $stderr = Join-Path $workerPath 'profile.stderr.log'
            $process = Start-Process -FilePath $prothPath -ArgumentList @(
                '--phase-profile', '--device', [string]$Device, '--batch', ('"' + $batchPath + '"')
            ) -WorkingDirectory $workerPath -RedirectStandardOutput $stdout `
                -RedirectStandardError $stderr -PassThru -NoNewWindow
            $workers.Add([pscustomobject]@{ process=$process; stdout=$stdout; stderr=$stderr })
        }
        foreach ($worker in $workers) {
            $worker.process.WaitForExit()
            $worker.process.Refresh()
            $exitCode = $worker.process.ExitCode
            if ($null -ne $exitCode -and $exitCode -ne 0) {
                throw "$RunId worker exited with code $exitCode."
            }
        }
    } catch {
        foreach ($worker in $workers) { Stop-OwnedProcess $worker.process }
        throw
    } finally {
        $timer.Stop()
        if ($null -ne $telemetry) { Stop-OwnedProcess $telemetry.process }
    }
    if ((Get-Item -LiteralPath $telemetry.stderr).Length -ne 0) {
        throw "$RunId hardware monitor wrote stderr: $($telemetry.stderr)"
    }
    $telemetryLines = @([IO.File]::ReadLines($telemetry.stdout) | Where-Object { $_.Trim().Length -ne 0 })
    if ($telemetryLines.Count -eq 0) { throw "$RunId hardware monitor produced no samples." }
    $verdicts = Assert-WorkerOutput -RunId $RunId -Workers $workers.ToArray() -ExpectedCount $CandidateCount
    return [pscustomobject][ordered]@{
        run = $RunId
        workers = $WorkerCount
        candidates = $CandidateCount
        elapsed_seconds = $timer.Elapsed.TotalSeconds
        candidates_per_hour = 3600.0 * $CandidateCount / $timer.Elapsed.TotalSeconds
        preflight_gpu = $preflight
        telemetry_samples = $telemetryLines.Count
        verdicts = $verdicts
        status = 'PASS'
    }
}

function Invoke-DirectBaseline([Nullable[int]]$ReferenceTemperatureC) {
    $runId = 'direct-baseline'
    $runPath = Join-Path $outputPath $runId
    if ($Resume -and (Test-Path -LiteralPath $runPath -PathType Container)) {
        $candidateOutputs = @(Get-ChildItem -LiteralPath $runPath -Directory -Filter 'candidate-*' |
            ForEach-Object { Join-Path $_.FullName 'profile.stdout.log' })
        $completeOutputs = @($candidateOutputs | Where-Object {
            (Test-Path -LiteralPath $_ -PathType Leaf) -and
            ([IO.File]::ReadAllText($_) -match 'PRIMEFORGE_PHASE\t1\t[0-9]+\t[0-9]+\tCANDIDATE_TOTAL\t[0-9]+')
        })
        $preflightPath = Join-Path $runPath 'preflight-gpu.json'
        if ($completeOutputs.Count -eq $DirectBaselineCount -and
            (Test-Path -LiteralPath $preflightPath -PathType Leaf)) {
            return [pscustomobject][ordered]@{
                run = $runId
                workers = 1
                candidates = $DirectBaselineCount
                elapsed_seconds = 'RECORDED_IN_PHASE_LOGS'
                candidates_per_hour = 'RECORDED_IN_PHASE_LOGS'
                preflight_gpu = Get-Content -Raw -LiteralPath $preflightPath | ConvertFrom-Json
                status = 'REUSED_COMPLETED'
            }
        }
        throw "Cannot resume incomplete direct baseline: $runPath"
    }
    New-Item -ItemType Directory -Path $runPath -Force | Out-Null
    $preflight = Wait-GpuReady -ReferenceTemperatureC $ReferenceTemperatureC
    [IO.File]::WriteAllText((Join-Path $runPath 'preflight-gpu.json'),
        ($preflight | ConvertTo-Json -Depth 4) + "`n", $utf8)
    $telemetry = $null
    $timer = [Diagnostics.Stopwatch]::StartNew()
    try {
        $telemetry = Start-Telemetry -RunPath $runPath
        Start-Sleep -Milliseconds $TelemetryIntervalMs
        foreach ($candidate in @($candidates | Select-Object -First $DirectBaselineCount)) {
            $candidatePath = Join-Path $runPath ('candidate-{0}-{1}' -f $candidate.k, $candidate.n)
            New-Item -ItemType Directory -Path $candidatePath -Force | Out-Null
            $stdout = Join-Path $candidatePath 'profile.stdout.log'
            $stderr = Join-Path $candidatePath 'profile.stderr.log'
            $expression = "$($candidate.k)*2^$($candidate.n)+1"
            $process = Start-Process -FilePath $prothPath -ArgumentList @(
                '--phase-profile', '--device', [string]$Device, '-q', $expression
            ) -WorkingDirectory $candidatePath -RedirectStandardOutput $stdout `
                -RedirectStandardError $stderr -PassThru -NoNewWindow -Wait
            $process.Refresh()
            if ($null -ne $process.ExitCode -and $process.ExitCode -ne 0) {
                throw "Direct baseline candidate exited with $($process.ExitCode)."
            }
            if ((Get-Item -LiteralPath $stderr).Length -ne 0) {
                throw "Direct baseline candidate wrote stderr: $stderr"
            }
        }
    } finally {
        $timer.Stop()
        if ($null -ne $telemetry) { Stop-OwnedProcess $telemetry.process }
    }
    return [pscustomobject][ordered]@{
        run = $runId
        workers = 1
        candidates = $DirectBaselineCount
        elapsed_seconds = $timer.Elapsed.TotalSeconds
        candidates_per_hour = 3600.0 * $DirectBaselineCount / $timer.Elapsed.TotalSeconds
        preflight_gpu = $preflight
        status = 'PASS'
    }
}

$candidatePath = Resolve-ProjectPath $CandidateFile
$outputPath = Resolve-ProjectPath $OutputDirectory
$prothPath = Resolve-ProjectPath $Proth20Executable
$monitorPath = Resolve-ProjectPath $HardwareMonitorExecutable
foreach ($required in @($candidatePath, $prothPath, $monitorPath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) { throw "Required file missing: $required" }
}
if ($DirectBaselineCount -gt $CandidateCount) { throw 'DirectBaselineCount exceeds CandidateCount.' }
if (Test-Path -LiteralPath $outputPath) {
    if (-not $Resume -and @(Get-ChildItem -LiteralPath $outputPath -Force).Count -ne 0) {
        throw "Output directory must be new or empty: $outputPath"
    }
} else {
    New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
}

$candidates = @([IO.File]::ReadLines($candidatePath) | Where-Object {
    -not [string]::IsNullOrWhiteSpace($_) -and -not $_.TrimStart().StartsWith('#')
} | Select-Object -First $CandidateCount | ForEach-Object -Begin { $ordinal = 0 } -Process {
    if ($_ -notmatch '^\s*([0-9]+)\s+([0-9]+)\s*$') { throw "Malformed candidate line: $_" }
    [pscustomobject][ordered]@{
        ordinal = $ordinal++
        k = [uint32]$Matches[1]
        n = [uint32]$Matches[2]
    }
})
if ($candidates.Count -ne $CandidateCount) {
    throw "Candidate file supplied $($candidates.Count)/$CandidateCount candidates."
}
$candidateKeys = @($candidates | ForEach-Object { "$($_.k)/$($_.n)" } | Sort-Object -Unique)
if ($candidateKeys.Count -ne $CandidateCount) { throw 'Candidate cohort contains duplicates.' }

$initial = Wait-GpuReady -ReferenceTemperatureC $null
$referenceTemperature = [Nullable[int]]([int]$initial.temperature_c)
$records = [Collections.Generic.List[object]]::new()
$records.Add((Invoke-DirectBaseline -ReferenceTemperatureC $referenceTemperature))
foreach ($variant in @(
    [pscustomobject]@{ id='A1-one-worker'; workers=1 },
    [pscustomobject]@{ id='B1-two-workers'; workers=2 },
    [pscustomobject]@{ id='B2-two-workers'; workers=2 },
    [pscustomobject]@{ id='A2-one-worker'; workers=1 }
)) {
    $records.Add((Invoke-BatchRun -RunId $variant.id -WorkerCount $variant.workers `
        -ReferenceTemperatureC $referenceTemperature))
}

$referenceVerdicts = $records[1].verdicts
foreach ($record in @($records | Select-Object -Skip 2)) {
    foreach ($key in $referenceVerdicts.Keys) {
        if (-not $record.verdicts.ContainsKey($key) -or $record.verdicts[$key] -ne $referenceVerdicts[$key]) {
            throw "AB/BA verdict mismatch for $key in $($record.run)."
        }
    }
}

$manifest = [pscustomobject][ordered]@{
    schema = 'primeforge.proth20.phase-profile.run.v1'
    recorded_utc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    git_commit = (& git -C $repositoryRoot rev-parse HEAD).Trim()
    proth20_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $prothPath).Hash.ToLowerInvariant()
    candidate_file = $candidatePath
    candidate_file_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $candidatePath).Hash.ToLowerInvariant()
    candidate_count = $CandidateCount
    device = $Device
    initial_gpu = $initial
    allow_busy_gpu = [bool]$AllowBusyGpu
    schedule = @($records)
    status = 'PASS'
}
[IO.File]::WriteAllText((Join-Path $outputPath 'run-manifest.json'),
    ($manifest | ConvertTo-Json -Depth 12) + "`n", $utf8)

& (Join-Path $PSScriptRoot 'analyze_proth20_phase_profile.ps1') `
    -InputDirectory $outputPath -ExpectedCandidates $CandidateCount `
    -ExpectedResultsFile 'benchmarks\profiles\proth20_n66411_profile20_expected.tsv'
if ($LASTEXITCODE -ne 0) { throw "Phase-profile analyzer failed with code $LASTEXITCODE." }

Write-Host "proth20.profile.run.output=$outputPath"
Write-Host 'proth20.profile.run.status=PASS'
