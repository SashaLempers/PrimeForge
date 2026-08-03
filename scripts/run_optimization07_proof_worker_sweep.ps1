[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [string]$OutputDirectory = 'out\benchmarks\optimization-07\proof-worker-sweep',
    [ValidateRange(7, 25)][int]$Repetitions = 7,
    [ValidateRange(1, 10)][int]$Warmups = 1,
    [int]$Seed = 20260803,
    [switch]$VerifyEachRun
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$outputRoot = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
    [IO.Path]::GetFullPath($OutputDirectory)
} else {
    [IO.Path]::GetFullPath((Join-Path $repositoryRoot $OutputDirectory))
}
$optimizationRoot = [IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'out\benchmarks\optimization-07')
).TrimEnd('\') + '\'
if (-not $outputRoot.StartsWith($optimizationRoot, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Output directory must remain inside $optimizationRoot"
}
if (Test-Path -LiteralPath $outputRoot) {
    throw "Output directory already exists: $outputRoot"
}

$gitStatus = @(& git -C $repositoryRoot status --porcelain --untracked-files=all)
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect the Git worktree.' }
if ($gitStatus.Count -ne 0) {
    throw 'Optimization-07 retained proof-worker evidence requires a clean Git worktree.'
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
$workers = @(1, 2, 4, 8, 16)
$utf8NoBom = [Text.UTF8Encoding]::new($false)
$invariant = [Globalization.CultureInfo]::InvariantCulture

function Convert-ToDouble {
    param([Parameter(Mandatory = $true)][object]$Value)
    return [double]::Parse([string]$Value, $invariant)
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
    return Get-Median @($Values | ForEach-Object { [Math]::Abs($_ - $median) })
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

    # This is the exact Process object created for this run. Never match or
    # terminate processes by executable name because they may belong to users.
    try {
        $Process.Kill()
    } catch {
        if (-not $Process.HasExited) {
            throw
        }
    }
    if (-not $Process.WaitForExit(5000)) {
        throw "$Role process $($Process.Id) did not exit after its bounded shutdown."
    }
    $Process.WaitForExit()
    if ($null -ne $stopRequestError) {
        throw "$Role stop request could not be written before forced shutdown: $stopRequestError"
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

New-Item -ItemType Directory -Path $outputRoot | Out-Null
$random = [Random]::new($Seed)
$schedule = [Collections.Generic.List[object]]::new()
$sequence = 0
for ($warmup = 1; $warmup -le $Warmups; ++$warmup) {
    foreach ($proofWorkers in $workers) {
        ++$sequence
        $schedule.Add([pscustomobject][ordered]@{
            sequence = $sequence
            phase = 'warmup'
            round = $warmup
            order = $proofWorkers
            workers = $proofWorkers
        })
    }
}
for ($round = 1; $round -le $Repetitions; ++$round) {
    $order = 0
    foreach ($proofWorkers in (Shuffle-Values $workers $random)) {
        ++$sequence
        ++$order
        $schedule.Add([pscustomobject][ordered]@{
            sequence = $sequence
            phase = 'measure'
            round = $round
            order = $order
            workers = $proofWorkers
        })
    }
}
$schedule | Export-Csv -LiteralPath (Join-Path $outputRoot 'schedule.csv') -NoTypeInformation

$commit = (& git -C $repositoryRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve benchmark commit.' }
$expectedExecutableHash =
    (Get-FileHash -LiteralPath $primeforge -Algorithm SHA256).Hash.ToLowerInvariant()
$environment = [ordered]@{
    schema_version = 1
    captured_utc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ss.fffZ')
    commit = $commit
    repository_dirty = 'NO'
    seed = $Seed
    repetitions = $Repetitions
    warmups = $Warmups
    candidate_profile = 'benchmarks/profiles/full_u64_high_32768.yaml'
    candidate_profile_sha256 = (Get-FileHash -LiteralPath $profile -Algorithm SHA256).Hash.ToLowerInvariant()
    executable_sha256 = $expectedExecutableHash
    prp_backend = 'auto'
    prp_batch_candidates = 8192
    proof_worker_variants = $workers
    cpu_temperature_stop_celsius = 92
    minimum_ram_available_bytes = 8589934592
    minimum_vram_free_mib = 2048
    full_verification = if ($VerifyEachRun) { 'EVERY_RUN' } else { 'FIRST_RUN_PER_VARIANT' }
}
[IO.File]::WriteAllText(
    (Join-Path $outputRoot 'environment.json'),
    ($environment | ConvertTo-Json -Depth 4 -Compress),
    $utf8NoBom
)

$raw = [Collections.Generic.List[object]]::new()
$determinismHashes = [Collections.Generic.List[object]]::new()
$fullyVerifiedWorkers = [Collections.Generic.HashSet[int]]::new()
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
        $runId = '{0:D2}-{1}-r{2:D2}-o{3:D2}-w{4:D2}' -f `
            [int]$item.sequence, $item.phase, [int]$item.round, [int]$item.order, [int]$item.workers
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
            '--proof-workers', [string]$item.workers, '--stop-file', $workerStop
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
        $timer = [Diagnostics.Stopwatch]::new()
        $primaryFailure = $null
        try {
            $timer.Start()
            $worker = Start-Process -FilePath $primeforge -ArgumentList $arguments `
                -WorkingDirectory $repositoryRoot -RedirectStandardOutput $searchStdout `
                -RedirectStandardError $searchStderr -WindowStyle Hidden -PassThru

            # The worker must exist before its PID can be bound to the watchdog.
            $watchdogArguments[1] = [string]$worker.Id
            $watchdogProcess = Start-Process -FilePath $watchdog -ArgumentList $watchdogArguments `
                -WorkingDirectory $repositoryRoot -RedirectStandardOutput $watchdogStdout `
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

            # A healthy watchdog normally observes worker exit on its own. Give
            # it a bounded interval, then request a clean stop for this run only.
            if (-not $watchdogProcess.WaitForExit(10000)) {
                [IO.File]::WriteAllText($operatorStop, "STOP`n", $utf8NoBom)
                if (-not $watchdogProcess.WaitForExit(10000)) {
                    throw "Watchdog did not stop after worker exit for $runId."
                }
            }
            $watchdogProcess.WaitForExit()
            if ($worker.ExitCode -ne 0) {
                throw "PrimeForge worker failed for $runId with code $($worker.ExitCode)."
            }
            if ($watchdogProcess.ExitCode -ne 0) {
                throw "Watchdog failed for $runId with code $($watchdogProcess.ExitCode)."
            }
        } catch {
            $primaryFailure = $_.Exception
            throw
        } finally {
            if ($timer.IsRunning) {
                $timer.Stop()
            }
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

        $events = @(Get-Content -LiteralPath $telemetryPath | ForEach-Object { $_ | ConvertFrom-Json })
        $exitEvent = @($events | Where-Object event_type -eq 'worker_exited') | Select-Object -Last 1
        if ($null -eq $exitEvent -or [string]$exitEvent.payload.exit_code -ne '0') {
            throw "Worker exit was not clean for $runId."
        }
        if (@($events | Where-Object event_type -in @('graceful_stop_requested', 'forced_stop')).Count -ne 0) {
            throw "Watchdog rejected run $runId."
        }
        $keyValues = Get-KeyValues $searchStdout
        if ($keyValues['search.status'] -ne 'PASS' -or
            $keyValues['search.commit_sha'] -ne $commit -or
            $keyValues['search.campaign_id'] -ne $expectedCampaignId -or
            $keyValues['search.proof_workers'] -ne [string]$item.workers -or
            $keyValues['search.candidates'] -ne '32768' -or
            $keyValues['search.proven_primes'] -ne '1506' -or
            $keyValues['search.composites'] -ne '31262') {
            throw "Search output contract failed for $runId."
        }
        $observedHashes = [ordered]@{}
        foreach ($entry in $expectedHashes.GetEnumerator()) {
            $path = Join-Path $currentCampaign $entry.Key
            $observed = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
            if ($observed -ne $entry.Value) {
                throw "Deterministic hash mismatch for $($entry.Key) in $runId."
            }
            $observedHashes[$entry.Key] = $observed
        }

        $fullVerify = $VerifyEachRun -or -not $fullyVerifiedWorkers.Contains([int]$item.workers)
        $verifyStatus = 'REFERENCE_HASH_IDENTITY'
        if ($fullVerify) {
            $verifyStdout = Join-Path $runDirectory 'verify.stdout.txt'
            $verifyStderr = Join-Path $runDirectory 'verify.stderr.txt'
            & $primeforge verify --result (Join-Path $currentCampaign 'results.jsonl') `
                1> $verifyStdout 2> $verifyStderr
            if ($LASTEXITCODE -ne 0 -or
                -not (Select-String -LiteralPath $verifyStdout -SimpleMatch 'verify.status=PASS' -Quiet)) {
                throw "Independent campaign verification failed for $runId."
            }
            [void]$fullyVerifiedWorkers.Add([int]$item.workers)
            $verifyStatus = 'PASS'
        }

        $telemetry = @($events | Where-Object event_type -eq 'telemetry')
        if ($telemetry.Count -eq 0) { throw "No hardware telemetry was captured for $runId." }
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

        $campaignDestination = Join-Path $runDirectory 'campaign'
        Move-Item -LiteralPath $currentCampaign -Destination $campaignDestination
        $determinismHashes.Add([pscustomobject][ordered]@{
            sequence = [int]$item.sequence
            phase = [string]$item.phase
            round = [int]$item.round
            order = [int]$item.order
            workers = [int]$item.workers
            results_jsonl_sha256 = [string]$observedHashes['results.jsonl']
            campaign_checkpoint_json_sha256 = [string]$observedHashes['campaign.checkpoint.json']
            manifest_sha256 = [string]$observedHashes['MANIFEST.sha256']
            coverage_report_json_sha256 = [string]$observedHashes['coverage_report.json']
            search_yaml_sha256 = [string]$observedHashes['search.yaml']
            status = 'PASS'
        })
        $raw.Add([pscustomobject][ordered]@{
            sequence = [int]$item.sequence
            phase = [string]$item.phase
            round = [int]$item.round
            order = [int]$item.order
            workers = [int]$item.workers
            total_ns = [uint64]$keyValues['metrics.total_ns']
            proof_ns = [uint64]$keyValues['metrics.proof_ns']
            generation_ns = [uint64]$keyValues['metrics.generation_ns']
            congruence_ns = [uint64]$keyValues['metrics.congruence_ns']
            sieve_ns = [uint64]$keyValues['metrics.sieve_ns']
            packing_ns = [uint64]$keyValues['metrics.packing_ns']
            h2d_ns = [uint64]$keyValues['metrics.h2d_ns']
            kernel_ns = [uint64]$keyValues['metrics.kernel_ns']
            d2h_ns = [uint64]$keyValues['metrics.d2h_ns']
            verification_ns = [uint64]$keyValues['metrics.verification_ns']
            io_ns = [uint64]$keyValues['metrics.io_ns']
            checkpoint_ns = [uint64]$keyValues['metrics.checkpoint_ns']
            wall_elapsed_ms = [uint64]$timer.ElapsedMilliseconds
            verify_status = $verifyStatus
            cpu_temperature_max_c = ($cpuTemperatures | Measure-Object -Maximum).Maximum
            cpu_power_max_w = ($cpuPowers | Measure-Object -Maximum).Maximum
            gpu_temperature_max_c = ($gpuTemperatures | Measure-Object -Maximum).Maximum
            gpu_power_max_w = ($gpuPowers | Measure-Object -Maximum).Maximum
            ram_available_min_bytes = [uint64](($ramAvailable | Measure-Object -Minimum).Minimum)
            vram_free_min_mib = (($vramFree | Measure-Object -Minimum).Minimum)
            whea_max = (($wheaValues | Measure-Object -Maximum).Maximum)
            throttling = 'NO'
            results_jsonl_sha256 = [string]$observedHashes['results.jsonl']
            campaign_checkpoint_json_sha256 = [string]$observedHashes['campaign.checkpoint.json']
            manifest_sha256 = [string]$observedHashes['MANIFEST.sha256']
            coverage_report_json_sha256 = [string]$observedHashes['coverage_report.json']
            search_yaml_sha256 = [string]$observedHashes['search.yaml']
            status = 'PASS'
        })
        $rawCsvLines = @($raw | ConvertTo-Csv -NoTypeInformation)
        [IO.File]::WriteAllText(
            (Join-Path $outputRoot 'raw.csv'), (($rawCsvLines -join "`n") + "`n"), $utf8NoBom
        )
        $rawJsonLines = @($raw | ForEach-Object { $_ | ConvertTo-Json -Compress })
        [IO.File]::WriteAllText(
            (Join-Path $outputRoot 'raw.jsonl'), (($rawJsonLines -join "`n") + "`n"), $utf8NoBom
        )
        $hashCsvLines = @($determinismHashes | ConvertTo-Csv -NoTypeInformation)
        [IO.File]::WriteAllText(
            (Join-Path $outputRoot 'determinism-hashes.csv'),
            (($hashCsvLines -join "`n") + "`n"),
            $utf8NoBom
        )
        Write-Host "[$($item.sequence)/$($schedule.Count)] $runId PASS total_ns=$($keyValues['metrics.total_ns']) proof_ns=$($keyValues['metrics.proof_ns'])"
    }
} finally {
    Pop-Location
}
if ($raw.Count -ne $schedule.Count -or $determinismHashes.Count -ne $schedule.Count) {
    throw 'Proof-worker raw rows and determinism hashes do not cover every scheduled run.'
}
$finalExecutableHash =
    (Get-FileHash -LiteralPath $primeforge -Algorithm SHA256).Hash.ToLowerInvariant()
if ($finalExecutableHash -ne $expectedExecutableHash) {
    throw 'PrimeForge executable changed during the proof-worker sweep.'
}

$measured = @($raw | Where-Object phase -eq 'measure')
$baselineRows = @($measured | Where-Object workers -eq 1)
$baselineTotal = [double[]]@($baselineRows | ForEach-Object { [double]$_.total_ns })
$baselineMedian = Get-Median $baselineTotal
$baselineMad = Get-Mad $baselineTotal
$summary = [Collections.Generic.List[object]]::new()
$bootstrapRandom = [Random]::new($Seed + 1)
foreach ($proofWorkers in $workers) {
    $rows = @($measured | Where-Object workers -eq $proofWorkers | Sort-Object round)
    $totals = [double[]]@($rows | ForEach-Object { [double]$_.total_ns })
    $proofs = [double[]]@($rows | ForEach-Object { [double]$_.proof_ns })
    $median = Get-Median $totals
    $mad = Get-Mad $totals
    $pairedGains = [Collections.Generic.List[double]]::new()
    $wins = 0
    if ($proofWorkers -ne 1) {
        foreach ($row in $rows) {
            $baseline = $baselineRows | Where-Object round -eq $row.round | Select-Object -First 1
            if ($null -eq $baseline) { throw "Missing paired baseline for round $($row.round)." }
            $gain = 100.0 * ([double]$baseline.total_ns - [double]$row.total_ns) / [double]$baseline.total_ns
            $pairedGains.Add($gain)
            if ([double]$row.total_ns -lt [double]$baseline.total_ns) { ++$wins }
        }
    } else {
        foreach ($unused in $rows) { $pairedGains.Add(0.0) }
    }
    $bootstrap = [Collections.Generic.List[double]]::new()
    for ($sample = 0; $sample -lt 20000; ++$sample) {
        $resample = [double[]]::new($pairedGains.Count)
        for ($index = 0; $index -lt $resample.Count; ++$index) {
            $resample[$index] = $pairedGains[$bootstrapRandom.Next($pairedGains.Count)]
        }
        $bootstrap.Add((Get-Median $resample))
    }
    $bootstrapOrdered = @($bootstrap | Sort-Object)
    $lowIndex = [int][Math]::Floor(0.025 * ($bootstrapOrdered.Count - 1))
    $highIndex = [int][Math]::Ceiling(0.975 * ($bootstrapOrdered.Count - 1))
    $gainNs = $baselineMedian - $median
    $retentionGate = $proofWorkers -ne 1 -and $rows.Count -eq $Repetitions -and
        (100.0 * $gainNs / $baselineMedian) -ge 3.0 -and
        $gainNs -gt 2.0 * [Math]::Max($baselineMad, $mad) -and
        $wins -eq $Repetitions -and [double]$bootstrapOrdered[$lowIndex] -gt 0.0
    $summary.Add([pscustomobject][ordered]@{
        workers = $proofWorkers
        repetitions = $rows.Count
        median_total_ns = [uint64]$median
        mad_total_ns = [uint64]$mad
        median_proof_ns = [uint64](Get-Median $proofs)
        speedup_vs_w1 = $baselineMedian / $median
        median_total_gain_percent = 100.0 * $gainNs / $baselineMedian
        paired_wins = $wins
        paired_median_gain_percent = Get-Median ([double[]]$pairedGains)
        paired_bootstrap_low_percent = [double]$bootstrapOrdered[$lowIndex]
        paired_bootstrap_high_percent = [double]$bootstrapOrdered[$highIndex]
        retention_gate = if ($retentionGate) { 'PASS' } else { 'FAIL' }
    })
}
$summary | Export-Csv -LiteralPath (Join-Path $outputRoot 'summary.csv') -NoTypeInformation
$eligible = @($summary | Where-Object retention_gate -eq 'PASS' | Sort-Object median_total_ns, workers)
$decision = if ($eligible.Count -eq 0) {
    [ordered]@{
        schema_version = 1
        retained_workers = 1
        decision = 'KEEP_SERIAL'
        reason = 'NO_PARALLEL_VARIANT_PASSED_THE_PREDECLARED_GATE'
    }
} else {
    $fastest = $eligible[0]
    $fastestRows = @($measured | Where-Object workers -eq $fastest.workers | Sort-Object round)
    $plateauComparisons = [Collections.Generic.List[object]]::new()
    $noisePlateau = @($eligible | Where-Object {
        $candidate = $_
        if ([int]$candidate.workers -eq [int]$fastest.workers) {
            $plateauComparisons.Add([pscustomobject][ordered]@{
                candidate_workers = [int]$candidate.workers
                fastest_workers = [int]$fastest.workers
                fastest_paired_wins = $Repetitions
                fastest_median_gain_percent = 0.0
                fastest_bootstrap_low_percent = 0.0
                fastest_bootstrap_high_percent = 0.0
                fastest_significantly_faster = 'NO_SAME_VARIANT'
            })
            return $true
        }

        $candidateRows = @(
            $measured | Where-Object workers -eq $candidate.workers | Sort-Object round
        )
        $pairedFastestGains = [Collections.Generic.List[double]]::new()
        $fastestWins = 0
        for ($index = 0; $index -lt $candidateRows.Count; ++$index) {
            if ($candidateRows[$index].round -ne $fastestRows[$index].round) {
                throw 'Fastest-plateau paired rounds diverged.'
            }
            $candidateTotal = [double]$candidateRows[$index].total_ns
            $fastestTotal = [double]$fastestRows[$index].total_ns
            $pairedFastestGains.Add(
                100.0 * ($candidateTotal - $fastestTotal) / $candidateTotal
            )
            if ($fastestTotal -lt $candidateTotal) { ++$fastestWins }
        }

        $plateauBootstrap = [Collections.Generic.List[double]]::new()
        for ($sample = 0; $sample -lt 20000; ++$sample) {
            $resample = [double[]]::new($pairedFastestGains.Count)
            for ($index = 0; $index -lt $resample.Count; ++$index) {
                $resample[$index] =
                    $pairedFastestGains[$bootstrapRandom.Next($pairedFastestGains.Count)]
            }
            $plateauBootstrap.Add((Get-Median $resample))
        }
        $orderedPlateauBootstrap = @($plateauBootstrap | Sort-Object)
        $plateauLow = [double]$orderedPlateauBootstrap[
            [int][Math]::Floor(0.025 * ($orderedPlateauBootstrap.Count - 1))
        ]
        $plateauHigh = [double]$orderedPlateauBootstrap[
            [int][Math]::Ceiling(0.975 * ($orderedPlateauBootstrap.Count - 1))
        ]
        $fastestGainNs =
            [double]$candidate.median_total_ns - [double]$fastest.median_total_ns
        $fastestSignificantlyFaster =
            (100.0 * $fastestGainNs / [double]$candidate.median_total_ns) -ge 3.0 -and
            $fastestGainNs -gt 2.0 * [Math]::Max(
                [double]$candidate.mad_total_ns, [double]$fastest.mad_total_ns
            ) -and
            $fastestWins -eq $Repetitions -and $plateauLow -gt 0.0
        $plateauComparisons.Add([pscustomobject][ordered]@{
            candidate_workers = [int]$candidate.workers
            fastest_workers = [int]$fastest.workers
            fastest_paired_wins = $fastestWins
            fastest_median_gain_percent = Get-Median ([double[]]$pairedFastestGains)
            fastest_bootstrap_low_percent = $plateauLow
            fastest_bootstrap_high_percent = $plateauHigh
            fastest_significantly_faster =
                if ($fastestSignificantlyFaster) { 'YES' } else { 'NO' }
        })
        return -not $fastestSignificantlyFaster
    } | Sort-Object workers)
    $plateauComparisons | Export-Csv `
        -LiteralPath (Join-Path $outputRoot 'plateau-comparisons.csv') -NoTypeInformation
    $selected = $noisePlateau[0]
    [ordered]@{
        schema_version = 1
        retained_workers = [int]$selected.workers
        decision = 'RETAIN_PARALLEL'
        reason = 'SMALLEST_VARIANT_NOT_SIGNIFICANTLY_SLOWER_THAN_FASTEST_BY_PAIRED_GATE'
    }
}
[IO.File]::WriteAllText(
    (Join-Path $outputRoot 'decision.json'),
    ($decision | ConvertTo-Json -Compress),
    $utf8NoBom
)
Write-Host "Optimization-07 proof-worker sweep complete: $($decision.decision), workers=$($decision.retained_workers), output=$outputRoot"
