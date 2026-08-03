[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [string]$OutputDirectory = 'out\benchmarks\optimization-08\flint-process-sweep',
    [ValidateRange(7, 25)][int]$Repetitions = 7,
    [ValidateRange(1, 10)][int]$Warmups = 1,
    [int]$Seed = 20260804
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$outputRoot = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
    [IO.Path]::GetFullPath($OutputDirectory)
} else {
    [IO.Path]::GetFullPath((Join-Path $repositoryRoot $OutputDirectory))
}
$optimizationRootPath = [IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'out\benchmarks\optimization-08')
).TrimEnd('\')
$optimizationRootPrefix = $optimizationRootPath + '\'
if (-not $outputRoot.StartsWith($optimizationRootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Output directory must remain inside $optimizationRootPrefix"
}
if (Test-Path -LiteralPath $outputRoot) {
    throw "Output directory already exists: $outputRoot"
}

$gitStatus = @(& git -C $repositoryRoot status --porcelain --untracked-files=all)
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect the Git worktree.' }
if ($gitStatus.Count -ne 0) {
    throw 'Optimization-08 retained FLINT-process evidence requires a clean Git worktree.'
}

$profile = Join-Path $repositoryRoot 'benchmarks\profiles\full_u64_high_32768.yaml'
$primeforge = Join-Path $repositoryRoot 'out\build\msvc-cuda-release\primeforge.exe'
$watchdog = Join-Path $repositoryRoot 'out\build\msvc-cuda-release\benchmark_watchdog.exe'
$hardwareMonitor = Join-Path $repositoryRoot 'out\build\msvc-cuda-release\hardware_monitor.exe'
foreach ($required in @($profile, $primeforge, $watchdog, $hardwareMonitor)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required benchmark input is absent: $required"
    }
}

# The profile's output directory is part of its logical identity. Keep that
# fixed path while placing all Optimization-08 evidence beneath its own root.
$currentCampaign = Join-Path $repositoryRoot 'out\benchmarks\optimization-07\current-campaign'
if (Test-Path -LiteralPath $currentCampaign) {
    throw "Archive the existing fixed-path campaign before the sweep: $currentCampaign"
}
$expectedHashes = [ordered]@{
    'results.jsonl' = '1e003a08d10a97fcf0d0bcf2e86bdd1a654a07e1fcc353afdc0307d949a161c3'
    'campaign.checkpoint.json' = 'cd6b446ffb998f0d2397686ff6da1c9cd991474b1bf14f829c17ab831692e4c8'
    'MANIFEST.sha256' = 'fdb1f6b0fc752d55ac3d1a9701a52b8d83086ffa5e91fe739dd5bc195e77a7f0'
    'coverage_report.json' = '2d207b28472de89fd742e379cee946f00aa40584c0943f983cbc7387512f3b00'
    'search.yaml' = '34fe76303a1f8ea4aa5bfd28e4140db1062753044c56762034a878fbc6342a2e'
}
$expectedCampaignId = 'sha256:26662e149e7f97d7b67ebbfabef95e7151165d986f026221c8fc375f63c3cc2a'
$processVariants = @(1, 2, 4, 8)
$baselineComparisonCount = $processVariants.Count - 1
$baselineFamilyTailProbability = 0.025 / [double]$baselineComparisonCount
$baselineIndividualConfidencePercent =
    100.0 * (1.0 - 2.0 * $baselineFamilyTailProbability)
$utf8NoBom = [Text.UTF8Encoding]::new($false)
$invariant = [Globalization.CultureInfo]::InvariantCulture

function Convert-ToDouble {
    param([Parameter(Mandatory = $true)][object]$Value)
    if ($Value -is [string]) {
        return [double]::Parse(
            [string]$Value,
            [Globalization.NumberStyles]::Float,
            $invariant
        )
    }
    return [Convert]::ToDouble($Value, $invariant)
}

function Convert-ToInvariantCsvLines {
    param([Parameter(Mandatory = $true)][object[]]$Rows)
    $normalized = @(
        foreach ($row in $Rows) {
            $properties = [ordered]@{}
            foreach ($property in $row.PSObject.Properties) {
                $value = $property.Value
                if ($value -is [double]) {
                    $value = $value.ToString('R', $invariant)
                } elseif ($value -is [single]) {
                    $value = $value.ToString('R', $invariant)
                } elseif ($value -is [decimal]) {
                    $value = $value.ToString('G29', $invariant)
                }
                $properties[$property.Name] = $value
            }
            [pscustomobject]$properties
        }
    )
    return @($normalized | ConvertTo-Csv -NoTypeInformation)
}

function Write-InvariantCsv {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][object[]]$Rows
    )
    $lines = @(Convert-ToInvariantCsvLines -Rows $Rows)
    [IO.File]::WriteAllText($Path, (($lines -join "`n") + "`n"), $utf8NoBom)
}

function Write-Json {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][object]$Value,
        [int]$Depth = 8
    )
    [IO.File]::WriteAllText(
        $Path,
        (($Value | ConvertTo-Json -Depth $Depth -Compress) + "`n"),
        $utf8NoBom
    )
}

function Write-JsonLines {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][object[]]$Rows
    )
    $lines = @($Rows | ForEach-Object { $_ | ConvertTo-Json -Depth 8 -Compress })
    [IO.File]::WriteAllText($Path, (($lines -join "`n") + "`n"), $utf8NoBom)
}

function Get-Median {
    param([Parameter(Mandatory = $true)][double[]]$Values)
    if ($Values.Count -eq 0) { throw 'Cannot compute a median of an empty sample.' }
    $ordered = @($Values | Sort-Object)
    $middle = [int][Math]::Floor($ordered.Count / 2)
    if ($ordered.Count % 2 -eq 1) { return [double]$ordered[$middle] }
    return ([double]$ordered[$middle - 1] + [double]$ordered[$middle]) / 2.0
}

function Get-Mad {
    param([Parameter(Mandatory = $true)][double[]]$Values)
    $median = Get-Median $Values
    return Get-Median ([double[]]@($Values | ForEach-Object {
        [Math]::Abs($_ - $median)
    }))
}

function Get-BootstrapMedianInterval {
    param(
        [Parameter(Mandatory = $true)][double[]]$Values,
        [Parameter(Mandatory = $true)][Random]$Random,
        [Parameter(Mandatory = $true)][double]$TailProbability
    )
    if ($Values.Count -eq 0) { throw 'Cannot bootstrap an empty paired sample.' }
    $samples = [Collections.Generic.List[double]]::new()
    for ($sample = 0; $sample -lt 20000; ++$sample) {
        $resample = [double[]]::new($Values.Count)
        for ($index = 0; $index -lt $resample.Count; ++$index) {
            $resample[$index] = $Values[$Random.Next($Values.Count)]
        }
        $samples.Add((Get-Median $resample))
    }
    $ordered = @($samples | Sort-Object)
    $lowIndex = [int][Math]::Floor($TailProbability * ($ordered.Count - 1))
    $highIndex = [int][Math]::Ceiling((1.0 - $TailProbability) * ($ordered.Count - 1))
    return [pscustomobject][ordered]@{
        low = [double]$ordered[$lowIndex]
        high = [double]$ordered[$highIndex]
    }
}

function Get-KeyValues {
    param([Parameter(Mandatory = $true)][string]$Path)
    $result = @{}
    foreach ($line in Get-Content -LiteralPath $Path) {
        $separator = $line.IndexOf('=')
        if ($separator -le 0) { continue }
        $result[$line.Substring(0, $separator)] = $line.Substring($separator + 1)
    }
    return $result
}

function Get-CommandText {
    param(
        [Parameter(Mandatory = $true)][string]$Command,
        [string[]]$Arguments = @()
    )
    try {
        $text = (& $Command @Arguments 2>&1 | Out-String).Trim()
        if ($LASTEXITCODE -ne 0) { return "ERROR(exit=$LASTEXITCODE): $text" }
        return $text
    } catch {
        return "UNAVAILABLE: $($_.Exception.Message)"
    }
}

function Request-ProcessStop {
    param(
        [Parameter(Mandatory = $true)][Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][string]$StopFile,
        [Parameter(Mandatory = $true)][int]$GraceMilliseconds,
        [Parameter(Mandatory = $true)][string]$Role
    )
    if ($Process.HasExited) {
        $Process.WaitForExit()
        return
    }
    $stopRequestError = $null
    try {
        [IO.File]::WriteAllText($StopFile, "STOP`n", $utf8NoBom)
    } catch {
        $stopRequestError = $_.Exception.Message
    }
    if ($null -eq $stopRequestError -and $Process.WaitForExit($GraceMilliseconds)) {
        $Process.WaitForExit()
        return
    }
    # Kill only the exact process object created by this sweep. Never kill by
    # executable name because an unrelated process may belong to the user.
    try {
        $Process.Kill()
    } catch {
        if (-not $Process.HasExited) { throw }
    }
    if (-not $Process.WaitForExit(5000)) {
        throw "$Role process $($Process.Id) did not exit after bounded shutdown."
    }
    $Process.WaitForExit()
    if ($null -ne $stopRequestError) {
        throw "$Role stop request failed before forced shutdown: $stopRequestError"
    }
}

function Wait-ForSafeIdle {
    while ($true) {
        $heavy = @(Get-Process -Name primeforge, primeforge-bench, ninja, cl, nvcc `
            -ErrorAction SilentlyContinue)
        if ($heavy.Count -gt 0) {
            Start-Sleep -Seconds 2
            continue
        }
        $snapshotText = (& $hardwareMonitor --once | Out-String).Trim()
        if ($LASTEXITCODE -ne 0) { throw 'Hardware preflight failed.' }
        $snapshot = $snapshotText | ConvertFrom-Json
        foreach ($metric in @(
            'cpu_temperature_celsius', 'cpu_power_watts',
            'gpu_temperature_celsius', 'gpu_power_watts',
            'ram_available_bytes', 'vram_free_mib', 'whea_errors_recent'
        )) {
            if ($snapshot.$metric.status -ne 'DETECTED') {
                throw "Required preflight metric is unavailable: $metric"
            }
        }
        $cpuTemperature = Convert-ToDouble $snapshot.cpu_temperature_celsius.value
        $ramAvailable = Convert-ToDouble $snapshot.ram_available_bytes.value
        $vramFree = Convert-ToDouble $snapshot.vram_free_mib.value
        $whea = Convert-ToDouble $snapshot.whea_errors_recent.value
        if (-not [bool]$snapshot.throttling_detected -and $whea -eq 0.0 -and
            $cpuTemperature -le 70.0 -and $ramAvailable -ge 8589934592.0 -and
            $vramFree -ge 2048.0) {
            return $snapshot
        }
        Start-Sleep -Seconds 5
    }
}

function Shuffle-Values {
    param(
        [Parameter(Mandatory = $true)][int[]]$Values,
        [Parameter(Mandatory = $true)][Random]$Random
    )
    $copy = @($Values)
    for ($index = $copy.Count - 1; $index -gt 0; --$index) {
        $swap = $Random.Next($index + 1)
        $temporary = $copy[$index]
        $copy[$index] = $copy[$swap]
        $copy[$swap] = $temporary
    }
    return $copy
}

if ([Math]::Abs((Convert-ToDouble ([double]70.875)) - 70.875) -gt 0.000001 -or
    [Math]::Abs((Convert-ToDouble '70.875') - 70.875) -gt 0.000001) {
    throw 'Invariant floating-point conversion self-test failed.'
}

# This shared exclusive lock prevents two Optimization-08 sweeps from passing
# the idle preflight simultaneously. The lock file may remain; only its open
# handle represents ownership.
New-Item -ItemType Directory -Path $optimizationRootPath -Force | Out-Null
$lockPath = Join-Path $optimizationRootPath '.flint-process-sweep.lock'
$sweepLock = $null
try {
    try {
        $sweepLock = [IO.File]::Open(
            $lockPath,
            [IO.FileMode]::OpenOrCreate,
            [IO.FileAccess]::ReadWrite,
            [IO.FileShare]::None
        )
    } catch {
        throw "Another Optimization-08 sweep owns the exclusive lock: $lockPath"
    }
    if (Test-Path -LiteralPath $outputRoot) {
        throw "Output directory appeared while waiting for the lock: $outputRoot"
    }
    New-Item -ItemType Directory -Path $outputRoot | Out-Null

    $random = [Random]::new($Seed)
    $schedule = [Collections.Generic.List[object]]::new()
    $sequence = 0
    for ($warmup = 1; $warmup -le $Warmups; ++$warmup) {
        $order = 0
        foreach ($processes in (Shuffle-Values $processVariants $random)) {
            ++$sequence
            ++$order
            $schedule.Add([pscustomobject][ordered]@{
                sequence = $sequence
                phase = 'warmup'
                round = $warmup
                order = $order
                flint_processes = $processes
            })
        }
    }
    for ($round = 1; $round -le $Repetitions; ++$round) {
        $order = 0
        foreach ($processes in (Shuffle-Values $processVariants $random)) {
            ++$sequence
            ++$order
            $schedule.Add([pscustomobject][ordered]@{
                sequence = $sequence
                phase = 'measure'
                round = $round
                order = $order
                flint_processes = $processes
            })
        }
    }
    Write-InvariantCsv -Path (Join-Path $outputRoot 'schedule.csv') -Rows @($schedule)

    $commit = (& git -C $repositoryRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve benchmark commit.' }
    $expectedExecutableHash =
        (Get-FileHash -LiteralPath $primeforge -Algorithm SHA256).Hash.ToLowerInvariant()
    $initialPreflight = Wait-ForSafeIdle
    $environment = [ordered]@{
        schema = 'primeforge.optimization08.flint-process-environment.v1'
        captured_utc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ss.fffZ')
        commit = $commit
        repository_dirty = 'NO'
        seed = $Seed
        repetitions = $Repetitions
        warmups = $Warmups
        candidate_profile = 'benchmarks/profiles/full_u64_high_32768.yaml'
        candidate_profile_sha256 =
            (Get-FileHash -LiteralPath $profile -Algorithm SHA256).Hash.ToLowerInvariant()
        executable_sha256 = $expectedExecutableHash
        watchdog_sha256 =
            (Get-FileHash -LiteralPath $watchdog -Algorithm SHA256).Hash.ToLowerInvariant()
        hardware_monitor_sha256 =
            (Get-FileHash -LiteralPath $hardwareMonitor -Algorithm SHA256).Hash.ToLowerInvariant()
        script_sha256 =
            (Get-FileHash -LiteralPath $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
        command = 'primeforge search --config benchmarks\profiles\full_u64_high_32768.yaml --prp-backend auto --prp-batch-candidates 8192 --proof-workers 4 --flint-processes {1|2|4|8}'
        prp_backend_requested = 'auto'
        prp_batch_candidates = 8192
        proof_workers = 4
        flint_process_variants = $processVariants
        primary_metric = 'metrics.total_ns'
        secondary_metric = 'metrics.verification_ns'
        baseline_family_comparisons = $baselineComparisonCount
        baseline_family_confidence_percent = 95.0
        baseline_individual_interval_confidence_percent =
            $baselineIndividualConfidencePercent
        baseline_family_tail_probability = $baselineFamilyTailProbability
        baseline_gate = 'PAIRED_MEDIAN_GAIN_GE_3_PERCENT;DELTA_OF_MEDIANS_GT_2_MAX_MAD;PAIRED_BOOTSTRAP_TWO_SIDED_FAMILY_95_PERCENT_BONFERRONI_3_BASELINE_COMPARISONS_POSITIVE'
        cpu_temperature_stop_celsius = 92
        minimum_ram_available_bytes = 8589934592
        minimum_vram_free_mib = 2048
        full_verification = 'FIRST_RUN_PER_VARIANT'
        git_version = Get-CommandText -Command 'git' -Arguments @('--version')
        cmake_version = Get-CommandText -Command 'cmake' -Arguments @('--version')
        ninja_version = Get-CommandText -Command 'ninja' -Arguments @('--version')
        nvcc_version = Get-CommandText -Command 'nvcc' -Arguments @('--version')
        nvidia_smi = Get-CommandText -Command 'nvidia-smi' -Arguments @(
            '--query-gpu=name,driver_version,compute_cap', '--format=csv,noheader'
        )
        initial_hardware_snapshot = $initialPreflight
        expected_campaign_id = $expectedCampaignId
        expected_logical_hashes = $expectedHashes
    }
    Write-Json -Path (Join-Path $outputRoot 'environment.json') -Value $environment -Depth 12

    $raw = [Collections.Generic.List[object]]::new()
    $determinismHashes = [Collections.Generic.List[object]]::new()
    $fullyVerifiedProcesses = [Collections.Generic.HashSet[int]]::new()
    Push-Location $repositoryRoot
    try {
        foreach ($item in $schedule) {
            if (Test-Path -LiteralPath $currentCampaign) {
                throw "Fixed campaign path unexpectedly exists before run $($item.sequence)."
            }
            $currentExecutableHash =
                (Get-FileHash -LiteralPath $primeforge -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($currentExecutableHash -ne $expectedExecutableHash) {
                throw "PrimeForge executable changed before run $($item.sequence)."
            }
            $preflight = Wait-ForSafeIdle
            $runId = '{0:D2}-{1}-r{2:D2}-o{3:D2}-p{4:D2}' -f `
                [int]$item.sequence, $item.phase, [int]$item.round, [int]$item.order,
                [int]$item.flint_processes
            $runDirectory = Join-Path $outputRoot $runId
            New-Item -ItemType Directory -Path $runDirectory | Out-Null
            $searchStdout = Join-Path $runDirectory 'search.stdout.txt'
            $searchStderr = Join-Path $runDirectory 'search.stderr.txt'
            $watchdogStdout = Join-Path $runDirectory 'watchdog.stdout.txt'
            $watchdogStderr = Join-Path $runDirectory 'watchdog.stderr.txt'
            $telemetryPath = Join-Path $runDirectory 'telemetry.jsonl'
            $workerStop = Join-Path $runDirectory 'worker.stop'
            $operatorStop = Join-Path $runDirectory 'operator.stop'
            $arguments = @(
                'search', '--config', 'benchmarks\profiles\full_u64_high_32768.yaml',
                '--prp-backend', 'auto', '--prp-batch-candidates', '8192',
                '--proof-workers', '4', '--flint-processes', [string]$item.flint_processes,
                '--stop-file', $workerStop
            )
            $watchdogArguments = @(
                '--pid', '', '--stop-file', $workerStop,
                '--watchdog-stop-file', $operatorStop, '--log', $telemetryPath,
                '--campaign', $runId, '--interval-ms', '500', '--grace-ms', '60000',
                '--max-cpu-temp-c', '92', '--max-gpu-temp-c', '88',
                '--require-cpu-temperature', '--require-cpu-power',
                '--require-gpu-temperature', '--require-gpu-power',
                '--require-ram-available', '--min-ram-available-bytes', '8589934592',
                '--require-vram-free', '--min-vram-free-mib', '2048',
                '--require-whea-status'
            )
            $worker = $null
            $watchdogProcess = $null
            $workerExitCode = $null
            $watchdogExitCode = $null
            $timer = [Diagnostics.Stopwatch]::new()
            $primaryFailure = $null
            try {
                $timer.Start()
                $worker = Start-Process -FilePath $primeforge -ArgumentList $arguments `
                    -WorkingDirectory $repositoryRoot -RedirectStandardOutput $searchStdout `
                    -RedirectStandardError $searchStderr -WindowStyle Hidden -PassThru
                $watchdogArguments[1] = [string]$worker.Id
                $watchdogProcess = Start-Process -FilePath $watchdog `
                    -ArgumentList $watchdogArguments -WorkingDirectory $repositoryRoot `
                    -RedirectStandardOutput $watchdogStdout `
                    -RedirectStandardError $watchdogStderr -WindowStyle Hidden -PassThru

                while (-not $worker.WaitForExit(200)) {
                    if ($watchdogProcess.HasExited -and -not $worker.HasExited) {
                        $watchdogProcess.WaitForExit()
                        throw "Watchdog exited before worker $($worker.Id) for $runId " +
                            "with code $($watchdogProcess.ExitCode)."
                    }
                }
                $worker.WaitForExit()
                $timer.Stop()
                if (-not $watchdogProcess.WaitForExit(10000)) {
                    [IO.File]::WriteAllText($operatorStop, "STOP`n", $utf8NoBom)
                    if (-not $watchdogProcess.WaitForExit(10000)) {
                        throw "Watchdog did not stop after worker exit for $runId."
                    }
                }
                $watchdogProcess.WaitForExit()
                $workerExitCode = $worker.ExitCode
                $watchdogExitCode = $watchdogProcess.ExitCode
                # Windows PowerShell 5.1 may expose null ExitCode after stream
                # redirection. The independent watchdog event is validated below.
                if ($null -ne $workerExitCode -and [int]$workerExitCode -ne 0) {
                    throw "PrimeForge worker failed for $runId with code $workerExitCode."
                }
                if ($null -ne $watchdogExitCode -and [int]$watchdogExitCode -ne 0) {
                    throw "Watchdog failed for $runId with code $watchdogExitCode."
                }
            } catch {
                $primaryFailure = $_.Exception
                throw
            } finally {
                if ($timer.IsRunning) { $timer.Stop() }
                $cleanupErrors = [Collections.Generic.List[string]]::new()
                if ($null -ne $worker) {
                    try {
                        Request-ProcessStop -Process $worker -StopFile $workerStop `
                            -GraceMilliseconds 60000 -Role 'PrimeForge worker'
                    } catch {
                        $cleanupErrors.Add($_.Exception.Message)
                    }
                }
                if ($null -ne $watchdogProcess) {
                    try {
                        Request-ProcessStop -Process $watchdogProcess -StopFile $operatorStop `
                            -GraceMilliseconds 10000 -Role 'PrimeForge watchdog'
                    } catch {
                        $cleanupErrors.Add($_.Exception.Message)
                    }
                }
                if ($cleanupErrors.Count -ne 0) {
                    $cleanupMessage = "Per-run process cleanup failed: $($cleanupErrors -join '; ')"
                    if ($null -ne $primaryFailure) {
                        [Console]::Error.WriteLine(
                            "Primary failure preserved ($($primaryFailure.Message)); $cleanupMessage"
                        )
                    } else {
                        throw $cleanupMessage
                    }
                }
            }

            $events = @(Get-Content -LiteralPath $telemetryPath | ForEach-Object {
                $_ | ConvertFrom-Json
            })
            $exitEvent = @($events | Where-Object event_type -eq 'worker_exited') |
                Select-Object -Last 1
            if ($null -eq $exitEvent -or [string]$exitEvent.payload.exit_code -ne '0') {
                throw "Worker exit was not clean for $runId."
            }
            if (@($events | Where-Object event_type -in @(
                'graceful_stop_requested', 'forced_stop'
            )).Count -ne 0) {
                throw "Watchdog rejected run $runId."
            }
            if ((Get-Item -LiteralPath $watchdogStderr).Length -ne 0L -or
                -not (Select-String -LiteralPath $watchdogStdout -SimpleMatch `
                    'benchmark_watchdog.decision=WORKER_EXITED reason=NONE' -Quiet)) {
                throw "Watchdog completion contract failed for $runId."
            }
            if ((Get-Item -LiteralPath $searchStderr).Length -ne 0L) {
                throw "PrimeForge wrote to stderr for successful run $runId."
            }

            $keyValues = Get-KeyValues $searchStdout
            if ($keyValues['search.status'] -ne 'PASS' -or
                $keyValues['search.commit_sha'] -ne $commit -or
                $keyValues['search.campaign_id'] -ne $expectedCampaignId -or
                $keyValues['search.proof_workers'] -ne '4' -or
                $keyValues['search.flint_processes'] -ne [string]$item.flint_processes -or
                $keyValues['search.prp_backend'] -ne
                    'primeforge.auto.cpu-cuda.base2-strong-prp-u64.v1[min=512]' -or
                $keyValues['search.candidates'] -ne '32768' -or
                $keyValues['search.proven_primes'] -ne '1506' -or
                $keyValues['search.composites'] -ne '31262') {
                throw "Search output contract failed for $runId."
            }
            $observedHashes = [ordered]@{}
            foreach ($entry in $expectedHashes.GetEnumerator()) {
                $path = Join-Path $currentCampaign $entry.Key
                if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
                    throw "Missing deterministic artifact $($entry.Key) for $runId."
                }
                $observed =
                    (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
                if ($observed -ne $entry.Value) {
                    throw "Deterministic hash mismatch for $($entry.Key) in $runId."
                }
                $observedHashes[$entry.Key] = $observed
            }

            $fullVerify = -not $fullyVerifiedProcesses.Contains([int]$item.flint_processes)
            $verifyStatus = 'REFERENCE_HASH_IDENTITY'
            $campaignArchive = Join-Path $runDirectory 'campaign'
            if ($fullVerify) {
                # Archive first, then exercise the documented replay procedure:
                # restore to the canonical path encoded by search.yaml, verify,
                # and put the campaign back in its durable run archive in finally.
                Move-Item -LiteralPath $currentCampaign -Destination $campaignArchive
                $verifyStdout = Join-Path $runDirectory 'replay.verify.stdout.txt'
                $verifyStderr = Join-Path $runDirectory 'replay.verify.stderr.txt'
                $replayFailure = $null
                $replayStatus = 'FAIL'
                $archiveRestoreStatus = 'NOT_ATTEMPTED'
                $postReplayHashStatus = 'NOT_CHECKED'
                $verifyExitCode = $null
                try {
                    Move-Item -LiteralPath $campaignArchive -Destination $currentCampaign
                    & $primeforge verify --result (Join-Path $currentCampaign 'results.jsonl') `
                        1> $verifyStdout 2> $verifyStderr
                    $verifyExitCode = $LASTEXITCODE
                    if ($verifyExitCode -ne 0 -or
                        (Get-Item -LiteralPath $verifyStderr).Length -ne 0L -or
                        -not (Select-String -LiteralPath $verifyStdout -SimpleMatch `
                            'verify.status=PASS' -Quiet)) {
                        throw "Independent replay verification failed for $runId."
                    }
                    $replayStatus = 'PASS'
                } catch {
                    $replayFailure = $_.Exception
                } finally {
                    try {
                        if (Test-Path -LiteralPath $currentCampaign) {
                            if (Test-Path -LiteralPath $campaignArchive) {
                                throw 'Replay restore found both canonical and archive paths.'
                            }
                            Move-Item -LiteralPath $currentCampaign `
                                -Destination $campaignArchive
                        }
                        if (-not (Test-Path -LiteralPath $campaignArchive -PathType Container) -or
                            (Test-Path -LiteralPath $currentCampaign)) {
                            throw 'Replay restore did not leave exactly the archived campaign.'
                        }
                        $archiveRestoreStatus = 'PASS'
                    } catch {
                        $archiveRestoreStatus = 'FAIL'
                        $restoreFailure = $_.Exception
                        if ($null -eq $replayFailure) {
                            $replayFailure = $restoreFailure
                        } else {
                            $replayFailure = [InvalidOperationException]::new(
                                "Replay failed ($($replayFailure.Message)); archive restore " +
                                "also failed ($($restoreFailure.Message))."
                            )
                        }
                    }
                }

                if ($archiveRestoreStatus -eq 'PASS') {
                    try {
                        foreach ($entry in $expectedHashes.GetEnumerator()) {
                            $archivedPath = Join-Path $campaignArchive $entry.Key
                            $archivedHash = (Get-FileHash -LiteralPath $archivedPath `
                                -Algorithm SHA256).Hash.ToLowerInvariant()
                            if ($archivedHash -ne $entry.Value) {
                                throw "Post-replay hash mismatch for $($entry.Key)."
                            }
                        }
                        $postReplayHashStatus = 'PASS'
                    } catch {
                        $postReplayHashStatus = 'FAIL'
                        if ($null -eq $replayFailure) {
                            $replayFailure = $_.Exception
                        } else {
                            $replayFailure = [InvalidOperationException]::new(
                                "Replay failed ($($replayFailure.Message)); post-replay hash " +
                                "validation also failed ($($_.Exception.Message))."
                            )
                        }
                    }
                }

                $replayRecord = [ordered]@{
                    schema = 'primeforge.optimization08.campaign-replay.v1'
                    original_path = 'out/benchmarks/optimization-07/current-campaign'
                    archive_path = "$runId/campaign"
                    original_absolute_path = $currentCampaign
                    archive_absolute_path = $campaignArchive
                    replay_status = $replayStatus
                    archive_restore_status = $archiveRestoreStatus
                    post_replay_hash_status = $postReplayHashStatus
                    verify_exit_code = $verifyExitCode
                    verify_stdout = 'replay.verify.stdout.txt'
                    verify_stderr = 'replay.verify.stderr.txt'
                    instructions = @(
                        'Move archive_path to original_path.',
                        'Run primeforge verify --result original_path/results.jsonl.',
                        'In finally, move original_path back to archive_path.'
                    )
                    error = if ($null -eq $replayFailure) {
                        $null
                    } else {
                        $replayFailure.Message
                    }
                }
                Write-Json -Path (Join-Path $runDirectory 'campaign-replay.json') `
                    -Value $replayRecord -Depth 6
                if ($null -ne $replayFailure) {
                    throw $replayFailure
                }
                [void]$fullyVerifiedProcesses.Add([int]$item.flint_processes)
                $verifyStatus = 'PASS_RESTAGED_REPLAY'
            }

            $telemetry = @($events | Where-Object event_type -eq 'telemetry')
            if ($telemetry.Count -eq 0) {
                throw "No hardware telemetry was captured for $runId."
            }
            $snapshots = @($telemetry | ForEach-Object { $_.payload.snapshot })
            $cpuTemperatures = [double[]]@($snapshots | ForEach-Object {
                Convert-ToDouble $_.cpu_temperature_celsius.value
            })
            $cpuPowers = [double[]]@($snapshots | ForEach-Object {
                Convert-ToDouble $_.cpu_power_watts.value
            })
            $gpuTemperatures = [double[]]@($snapshots | ForEach-Object {
                Convert-ToDouble $_.gpu_temperature_celsius.value
            })
            $gpuPowers = [double[]]@($snapshots | ForEach-Object {
                Convert-ToDouble $_.gpu_power_watts.value
            })
            $ramAvailable = [double[]]@($snapshots | ForEach-Object {
                Convert-ToDouble $_.ram_available_bytes.value
            })
            $vramFree = [double[]]@($snapshots | ForEach-Object {
                Convert-ToDouble $_.vram_free_mib.value
            })
            $wheaValues = [double[]]@($snapshots | ForEach-Object {
                Convert-ToDouble $_.whea_errors_recent.value
            })
            if (@($snapshots | Where-Object throttling_detected).Count -ne 0 -or
                ($wheaValues | Measure-Object -Maximum).Maximum -ne 0.0) {
                throw "Hardware validity gate failed for $runId."
            }

            $determinismHashes.Add([pscustomobject][ordered]@{
                sequence = [int]$item.sequence
                phase = [string]$item.phase
                round = [int]$item.round
                order = [int]$item.order
                flint_processes = [int]$item.flint_processes
                campaign_id = $expectedCampaignId
                results_jsonl_sha256 = [string]$observedHashes['results.jsonl']
                campaign_checkpoint_json_sha256 =
                    [string]$observedHashes['campaign.checkpoint.json']
                manifest_sha256 = [string]$observedHashes['MANIFEST.sha256']
                coverage_report_json_sha256 =
                    [string]$observedHashes['coverage_report.json']
                search_yaml_sha256 = [string]$observedHashes['search.yaml']
                status = 'PASS'
            })
            $raw.Add([pscustomobject][ordered]@{
                sequence = [int]$item.sequence
                phase = [string]$item.phase
                round = [int]$item.round
                order = [int]$item.order
                flint_processes = [int]$item.flint_processes
                total_ns = [uint64]$keyValues['metrics.total_ns']
                verification_ns = [uint64]$keyValues['metrics.verification_ns']
                proof_ns = [uint64]$keyValues['metrics.proof_ns']
                generation_ns = [uint64]$keyValues['metrics.generation_ns']
                congruence_ns = [uint64]$keyValues['metrics.congruence_ns']
                sieve_ns = [uint64]$keyValues['metrics.sieve_ns']
                packing_ns = [uint64]$keyValues['metrics.packing_ns']
                h2d_ns = [uint64]$keyValues['metrics.h2d_ns']
                kernel_ns = [uint64]$keyValues['metrics.kernel_ns']
                d2h_ns = [uint64]$keyValues['metrics.d2h_ns']
                io_ns = [uint64]$keyValues['metrics.io_ns']
                checkpoint_ns = [uint64]$keyValues['metrics.checkpoint_ns']
                wall_elapsed_ms = [uint64]$timer.ElapsedMilliseconds
                worker_exit_code = if ($null -eq $workerExitCode) {
                    'WATCHDOG_VERIFIED'
                } else {
                    [int]$workerExitCode
                }
                watchdog_exit_code = if ($null -eq $watchdogExitCode) {
                    'OUTPUT_VERIFIED'
                } else {
                    [int]$watchdogExitCode
                }
                verify_status = $verifyStatus
                prp_backend_id = [string]$keyValues['search.prp_backend']
                cpu_temperature_preflight_c =
                    Convert-ToDouble $preflight.cpu_temperature_celsius.value
                cpu_temperature_max_c =
                    ($cpuTemperatures | Measure-Object -Maximum).Maximum
                cpu_power_max_w = ($cpuPowers | Measure-Object -Maximum).Maximum
                gpu_temperature_max_c =
                    ($gpuTemperatures | Measure-Object -Maximum).Maximum
                gpu_power_max_w = ($gpuPowers | Measure-Object -Maximum).Maximum
                ram_available_min_bytes =
                    [uint64](($ramAvailable | Measure-Object -Minimum).Minimum)
                vram_free_min_mib = ($vramFree | Measure-Object -Minimum).Minimum
                whea_max = ($wheaValues | Measure-Object -Maximum).Maximum
                throttling = 'NO'
                results_jsonl_sha256 = [string]$observedHashes['results.jsonl']
                campaign_checkpoint_json_sha256 =
                    [string]$observedHashes['campaign.checkpoint.json']
                manifest_sha256 = [string]$observedHashes['MANIFEST.sha256']
                coverage_report_json_sha256 =
                    [string]$observedHashes['coverage_report.json']
                search_yaml_sha256 = [string]$observedHashes['search.yaml']
                status = 'PASS'
            })

            if ($fullVerify) {
                if ((Test-Path -LiteralPath $currentCampaign) -or
                    -not (Test-Path -LiteralPath $campaignArchive -PathType Container)) {
                    throw "Verified replay archive contract failed for $runId."
                }
            } else {
                # The five validated logical hashes are the retained evidence.
                # Removing duplicate campaign trees keeps this bounded sweep small.
                Remove-Item -LiteralPath $currentCampaign -Recurse -Force
            }
            Write-InvariantCsv -Path (Join-Path $outputRoot 'raw.csv') -Rows @($raw)
            Write-JsonLines -Path (Join-Path $outputRoot 'raw.jsonl') -Rows @($raw)
            Write-InvariantCsv -Path (Join-Path $outputRoot 'determinism-hashes.csv') `
                -Rows @($determinismHashes)
            Write-Host "[$($item.sequence)/$($schedule.Count)] $runId PASS total_ns=$($keyValues['metrics.total_ns']) verification_ns=$($keyValues['metrics.verification_ns'])"
        }
    } finally {
        Pop-Location
    }

    if ($raw.Count -ne $schedule.Count -or
        $determinismHashes.Count -ne $schedule.Count) {
        throw 'Raw rows and determinism hashes do not cover every scheduled run.'
    }
    if ($fullyVerifiedProcesses.Count -ne $processVariants.Count) {
        throw 'Exactly one full verification was not completed for every FLINT variant.'
    }
    $finalExecutableHash =
        (Get-FileHash -LiteralPath $primeforge -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($finalExecutableHash -ne $expectedExecutableHash) {
        throw 'PrimeForge executable changed during the FLINT-process sweep.'
    }

    $measured = @($raw | Where-Object phase -eq 'measure')
    $baselineRows = @($measured | Where-Object flint_processes -eq 1 | Sort-Object round)
    if ($baselineRows.Count -ne $Repetitions) {
        throw 'Measured FLINT p1 baseline does not cover every paired round.'
    }
    $baselineTotals = [double[]]@($baselineRows | ForEach-Object { [double]$_.total_ns })
    $baselineVerification = [double[]]@($baselineRows | ForEach-Object {
        [double]$_.verification_ns
    })
    $baselineMedian = Get-Median $baselineTotals
    $baselineMad = Get-Mad $baselineTotals
    $baselineVerificationMedian = Get-Median $baselineVerification
    $bootstrapRandom = [Random]::new($Seed + 1)
    $summary = [Collections.Generic.List[object]]::new()
    $comparisons = [Collections.Generic.List[object]]::new()

    foreach ($processes in $processVariants) {
        $rows = @($measured | Where-Object flint_processes -eq $processes | Sort-Object round)
        if ($rows.Count -ne $Repetitions) {
            throw "FLINT p$processes measurements do not cover every paired round."
        }
        $totals = [double[]]@($rows | ForEach-Object { [double]$_.total_ns })
        $verification = [double[]]@($rows | ForEach-Object {
            [double]$_.verification_ns
        })
        $median = Get-Median $totals
        $mad = Get-Mad $totals
        $verificationMedian = Get-Median $verification
        $verificationMad = Get-Mad $verification
        $pairedTotalGains = [Collections.Generic.List[double]]::new()
        $pairedVerificationGains = [Collections.Generic.List[double]]::new()
        $wins = 0
        for ($index = 0; $index -lt $rows.Count; ++$index) {
            if ([int]$rows[$index].round -ne [int]$baselineRows[$index].round) {
                throw "Paired round mismatch for FLINT p$processes."
            }
            if ($processes -eq 1) {
                $pairedTotalGains.Add(0.0)
                $pairedVerificationGains.Add(0.0)
            } else {
                $pairedTotalGains.Add(
                    100.0 * ([double]$baselineRows[$index].total_ns -
                        [double]$rows[$index].total_ns) /
                        [double]$baselineRows[$index].total_ns
                )
                $pairedVerificationGains.Add(
                    100.0 * ([double]$baselineRows[$index].verification_ns -
                        [double]$rows[$index].verification_ns) /
                        [double]$baselineRows[$index].verification_ns
                )
                if ([double]$rows[$index].total_ns -lt
                    [double]$baselineRows[$index].total_ns) {
                    ++$wins
                }
            }
        }
        $interval = if ($processes -eq 1) {
            [pscustomobject][ordered]@{ low = 0.0; high = 0.0 }
        } else {
            Get-BootstrapMedianInterval -Values ([double[]]$pairedTotalGains) `
                -Random $bootstrapRandom -TailProbability $baselineFamilyTailProbability
        }
        $gainNs = $baselineMedian - $median
        $ratioOfMediansGainPercent = 100.0 * $gainNs / $baselineMedian
        $pairedMedianTotalGain = Get-Median ([double[]]$pairedTotalGains)
        $pairedMedianVerificationGain = Get-Median ([double[]]$pairedVerificationGains)
        $gate = $processes -ne 1 -and $pairedMedianTotalGain -ge 3.0 -and
            $gainNs -gt 2.0 * [Math]::Max($baselineMad, $mad) -and
            [double]$interval.low -gt 0.0
        $summary.Add([pscustomobject][ordered]@{
            flint_processes = $processes
            repetitions = $rows.Count
            median_total_ns = [uint64]$median
            mad_total_ns = [uint64]$mad
            median_verification_ns = [uint64]$verificationMedian
            mad_verification_ns = [uint64]$verificationMad
            speedup_total_vs_p1 = $baselineMedian / $median
            ratio_of_medians_total_gain_percent_vs_p1 = $ratioOfMediansGainPercent
            paired_median_total_gain_percent_vs_p1 = $pairedMedianTotalGain
            ratio_of_medians_verification_gain_percent_vs_p1 =
                100.0 * ($baselineVerificationMedian - $verificationMedian) /
                $baselineVerificationMedian
            paired_median_verification_gain_percent_vs_p1 =
                $pairedMedianVerificationGain
            gate_vs_p1 = if ($processes -eq 1) {
                'BASELINE'
            } elseif ($gate) {
                'PASS'
            } else {
                'FAIL'
            }
        })
        $comparisons.Add([pscustomobject][ordered]@{
            baseline_flint_processes = 1
            candidate_flint_processes = $processes
            repetitions = $rows.Count
            absolute_median_total_gain_ns = [int64]$gainNs
            ratio_of_medians_total_gain_percent = $ratioOfMediansGainPercent
            absolute_median_verification_gain_ns =
                [int64]($baselineVerificationMedian - $verificationMedian)
            ratio_of_medians_verification_gain_percent =
                100.0 * ($baselineVerificationMedian - $verificationMedian) /
                $baselineVerificationMedian
            paired_total_wins = $wins
            paired_median_total_gain_percent = $pairedMedianTotalGain
            paired_median_verification_gain_percent = $pairedMedianVerificationGain
            bootstrap_family_comparisons = $baselineComparisonCount
            bootstrap_family_confidence_percent = 95.0
            bootstrap_individual_interval_confidence_percent =
                $baselineIndividualConfidencePercent
            bootstrap_family_tail_probability = $baselineFamilyTailProbability
            paired_bootstrap_family_low_percent = [double]$interval.low
            paired_bootstrap_family_high_percent = [double]$interval.high
            delta_of_medians_gt_2_max_mad = if ($processes -eq 1) {
                'BASELINE'
            } elseif ($gainNs -gt 2.0 * [Math]::Max($baselineMad, $mad)) {
                'YES'
            } else {
                'NO'
            }
            retention_gate = if ($processes -eq 1) {
                'BASELINE'
            } elseif ($gate) {
                'PASS'
            } else {
                'FAIL'
            }
        })
    }

    Write-InvariantCsv -Path (Join-Path $outputRoot 'summary.csv') -Rows @($summary)
    Write-Json -Path (Join-Path $outputRoot 'summary.json') -Value ([ordered]@{
        schema = 'primeforge.optimization08.flint-process-summary.v1'
        baseline_family_comparisons = $baselineComparisonCount
        baseline_family_confidence_percent = 95.0
        baseline_individual_interval_confidence_percent =
            $baselineIndividualConfidencePercent
        baseline_family_tail_probability = $baselineFamilyTailProbability
        threshold_statistic = 'paired_median_total_gain_percent'
        ratio_of_medians_role = 'DESCRIPTIVE_ONLY'
        rows = @($summary)
    })
    Write-InvariantCsv -Path (Join-Path $outputRoot 'comparisons.csv') `
        -Rows @($comparisons)
    Write-Json -Path (Join-Path $outputRoot 'comparisons.json') -Value ([ordered]@{
        schema = 'primeforge.optimization08.flint-process-comparisons.v1'
        primary_metric = 'total_ns'
        secondary_metric = 'verification_ns'
        baseline_family_comparisons = $baselineComparisonCount
        baseline_family_confidence_percent = 95.0
        baseline_individual_interval_confidence_percent =
            $baselineIndividualConfidencePercent
        baseline_family_tail_probability = $baselineFamilyTailProbability
        rows = @($comparisons)
    })

    $eligibleProcesses = @($comparisons | Where-Object retention_gate -eq 'PASS' |
        ForEach-Object { [int]$_.candidate_flint_processes })
    $plateauComparisons = [Collections.Generic.List[object]]::new()
    if ($eligibleProcesses.Count -eq 0) {
        $decision = [ordered]@{
            schema = 'primeforge.optimization08.flint-process-decision.v1'
            retained_flint_processes = 1
            decision = 'KEEP_SINGLE_PROCESS'
            primary_metric = 'total_ns'
            secondary_metric = 'verification_ns'
            gate = 'PAIRED_MEDIAN_GAIN_GE_3_PERCENT;DELTA_OF_MEDIANS_GT_2_MAX_MAD;PAIRED_BOOTSTRAP_TWO_SIDED_FAMILY_95_PERCENT_BONFERRONI_3_BASELINE_COMPARISONS_POSITIVE'
            reason = 'NO_PARALLEL_VARIANT_PASSED_THE_PREDECLARED_GATE'
        }
    } else {
        $eligibleSummary = @($summary | Where-Object {
            $eligibleProcesses -contains [int]$_.flint_processes
        } | Sort-Object median_total_ns, flint_processes)
        $fastest = $eligibleSummary[0]
        $fastestRows = @($measured | Where-Object {
            $_.flint_processes -eq $fastest.flint_processes
        } | Sort-Object round)
        # Four declared variants imply six possible pairwise comparisons. Use
        # that full family for the Bonferroni-adjusted two-sided interval.
        $pairCount = $processVariants.Count * ($processVariants.Count - 1) / 2
        $familyTail = 0.025 / [double]$pairCount
        $plateau = [Collections.Generic.List[object]]::new()
        foreach ($candidate in ($eligibleSummary | Sort-Object flint_processes)) {
            if ([int]$candidate.flint_processes -eq [int]$fastest.flint_processes) {
                $row = [pscustomobject][ordered]@{
                    candidate_flint_processes = [int]$candidate.flint_processes
                    fastest_flint_processes = [int]$fastest.flint_processes
                    fastest_paired_wins = $Repetitions
                    fastest_median_gain_percent = 0.0
                    bootstrap_family_tail_probability = $familyTail
                    fastest_bootstrap_low_percent = 0.0
                    fastest_bootstrap_high_percent = 0.0
                    fastest_significantly_faster = 'NO_SAME_VARIANT'
                    on_fastest_plateau = 'YES'
                }
                $plateauComparisons.Add($row)
                $plateau.Add($candidate)
                continue
            }
            $candidateRows = @($measured | Where-Object {
                $_.flint_processes -eq $candidate.flint_processes
            } | Sort-Object round)
            $pairedFastestGains = [Collections.Generic.List[double]]::new()
            $fastestWins = 0
            for ($index = 0; $index -lt $candidateRows.Count; ++$index) {
                if ([int]$candidateRows[$index].round -ne
                    [int]$fastestRows[$index].round) {
                    throw 'Fastest-plateau paired rounds diverged.'
                }
                $candidateTotal = [double]$candidateRows[$index].total_ns
                $fastestTotal = [double]$fastestRows[$index].total_ns
                $pairedFastestGains.Add(
                    100.0 * ($candidateTotal - $fastestTotal) / $candidateTotal
                )
                if ($fastestTotal -lt $candidateTotal) { ++$fastestWins }
            }
            $interval = Get-BootstrapMedianInterval `
                -Values ([double[]]$pairedFastestGains) -Random $bootstrapRandom `
                -TailProbability $familyTail
            $fastestGainNs =
                [double]$candidate.median_total_ns - [double]$fastest.median_total_ns
            $pairedMedianGain = Get-Median ([double[]]$pairedFastestGains)
            $significantlyFaster = $pairedMedianGain -ge 3.0 -and
                $fastestGainNs -gt 2.0 * [Math]::Max(
                    [double]$candidate.mad_total_ns,
                    [double]$fastest.mad_total_ns
                ) -and [double]$interval.low -gt 0.0
            $row = [pscustomobject][ordered]@{
                candidate_flint_processes = [int]$candidate.flint_processes
                fastest_flint_processes = [int]$fastest.flint_processes
                fastest_paired_wins = $fastestWins
                fastest_median_gain_percent = $pairedMedianGain
                bootstrap_family_tail_probability = $familyTail
                fastest_bootstrap_low_percent = [double]$interval.low
                fastest_bootstrap_high_percent = [double]$interval.high
                fastest_significantly_faster = if ($significantlyFaster) { 'YES' } else { 'NO' }
                on_fastest_plateau = if ($significantlyFaster) { 'NO' } else { 'YES' }
            }
            $plateauComparisons.Add($row)
            if (-not $significantlyFaster) { $plateau.Add($candidate) }
        }
        $selected = @($plateau | Sort-Object flint_processes)[0]
        $decision = [ordered]@{
            schema = 'primeforge.optimization08.flint-process-decision.v1'
            retained_flint_processes = [int]$selected.flint_processes
            nominal_fastest_flint_processes = [int]$fastest.flint_processes
            decision = 'RETAIN_PARALLEL_FLINT'
            primary_metric = 'total_ns'
            secondary_metric = 'verification_ns'
            gate = 'PAIRED_MEDIAN_GAIN_GE_3_PERCENT;DELTA_OF_MEDIANS_GT_2_MAX_MAD;PAIRED_BOOTSTRAP_TWO_SIDED_FAMILY_95_PERCENT_BONFERRONI_3_BASELINE_COMPARISONS_POSITIVE'
            plateau_rule = 'SMALLEST_ELIGIBLE_VARIANT_NOT_SIGNIFICANTLY_SLOWER_THAN_FASTEST;BONFERRONI_ALL_6_VARIANT_PAIRS'
            reason = 'SMALLEST_VARIANT_ON_THE_BONFERRONI_FASTEST_PLATEAU'
        }
    }
    if ($plateauComparisons.Count -ne 0) {
        Write-InvariantCsv -Path (Join-Path $outputRoot 'plateau-comparisons.csv') `
            -Rows @($plateauComparisons)
        Write-Json -Path (Join-Path $outputRoot 'plateau-comparisons.json') `
            -Value ([ordered]@{
                schema = 'primeforge.optimization08.flint-process-plateau.v1'
                rows = @($plateauComparisons)
            })
    }
    $allSnapshots = @($raw | Where-Object status -eq 'PASS')
    $decision['baseline_gate_statistics'] = [ordered]@{
        family_comparisons = $baselineComparisonCount
        family_confidence_percent = 95.0
        individual_interval_confidence_percent = $baselineIndividualConfidencePercent
        two_sided_tail_probability_per_side = $baselineFamilyTailProbability
        gain_threshold_statistic = 'paired_median_total_gain_percent'
        ratio_of_medians_role = 'DESCRIPTIVE_ONLY'
    }
    $decision['logical_identity'] = [ordered]@{
        campaign_id = $expectedCampaignId
        hashes = $expectedHashes
        all_runs_identical = 'YES'
    }
    $decision['hardware_validity'] = [ordered]@{
        max_cpu_temperature_c =
            ($allSnapshots.cpu_temperature_max_c | Measure-Object -Maximum).Maximum
        max_cpu_power_w = ($allSnapshots.cpu_power_max_w | Measure-Object -Maximum).Maximum
        max_gpu_temperature_c =
            ($allSnapshots.gpu_temperature_max_c | Measure-Object -Maximum).Maximum
        max_gpu_power_w = ($allSnapshots.gpu_power_max_w | Measure-Object -Maximum).Maximum
        min_ram_available_bytes =
            ($allSnapshots.ram_available_min_bytes | Measure-Object -Minimum).Minimum
        min_vram_free_mib =
            ($allSnapshots.vram_free_min_mib | Measure-Object -Minimum).Minimum
        max_whea_errors = ($allSnapshots.whea_max | Measure-Object -Maximum).Maximum
        throttling = 'NO'
    }
    Write-Json -Path (Join-Path $outputRoot 'decision.json') -Value $decision -Depth 10
    Write-Host "Optimization-08 FLINT-process sweep complete: $($decision.decision), processes=$($decision.retained_flint_processes), output=$outputRoot"
} finally {
    if ($null -ne $sweepLock) { $sweepLock.Dispose() }
}
