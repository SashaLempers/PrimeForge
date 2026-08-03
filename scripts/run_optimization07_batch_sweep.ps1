[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [string]$OutputDirectory = 'out\benchmarks\optimization-07\batch-sweep-final',
    [ValidateRange(7, 25)][int]$Repetitions = 7,
    [ValidateRange(0, 10)][int]$Warmups = 1,
    [int]$Seed = 20260803
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
    throw 'Optimization-07 retained batch evidence requires a clean Git worktree.'
}
$commit = (& git -C $repositoryRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $commit -notmatch '^[0-9a-f]{40}$') {
    throw 'Cannot resolve the benchmark commit.'
}

$profile = Join-Path $repositoryRoot 'benchmarks\profiles\s64_prp_65536.json'
$benchmark = Join-Path $repositoryRoot 'out\build\msvc-cuda-release\primeforge-bench.exe'
$hardwareMonitor = Join-Path $repositoryRoot 'out\build\msvc-cuda-release\hardware_monitor.exe'
foreach ($required in @($profile, $benchmark, $hardwareMonitor)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required CUDA release benchmark input is absent: $required"
    }
}

$utf8NoBom = [Text.UTF8Encoding]::new($false)
$invariant = [Globalization.CultureInfo]::InvariantCulture
$binarySha256 = (Get-FileHash -LiteralPath $benchmark -Algorithm SHA256).Hash.ToLowerInvariant()
$profileSha256 = (Get-FileHash -LiteralPath $profile -Algorithm SHA256).Hash.ToLowerInvariant()
$referenceResultSha256 = '430f77e57e47e8dbb5b457e8a24ea0688759ad26c02308e3737d6baca776dfce'
$backends = @('cpu', 'cuda', 'auto')
$batchSizes = @(32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384)
$minimumRamAvailableBytes = [uint64]8589934592
$minimumVramFreeMib = 2048.0
$maximumCpuPreflightCelsius = 70.0
$maximumGpuPreflightCelsius = 80.0

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
if ([Math]::Abs((Convert-ToDouble ([double]70.875)) - 70.875) -gt 0.000001 -or
    [Math]::Abs((Convert-ToDouble '70.875') - 70.875) -gt 0.000001) {
    throw 'Invariant floating-point conversion self-test failed.'
}

function Get-MedianUInt64 {
    param([Parameter(Mandatory = $true)][uint64[]]$Values)
    if ($Values.Count -eq 0) { throw 'Cannot compute a median of an empty sample.' }
    $ordered = @($Values | Sort-Object)
    $middle = [int][Math]::Floor($ordered.Count / 2)
    if ($ordered.Count % 2 -eq 1) { return [uint64]$ordered[$middle] }
    return [uint64][Math]::Floor(
        ([double]$ordered[$middle - 1] + [double]$ordered[$middle]) / 2.0
    )
}

function Get-MadUInt64 {
    param([Parameter(Mandatory = $true)][uint64[]]$Values)
    $median = Get-MedianUInt64 $Values
    $deviations = [uint64[]]@($Values | ForEach-Object {
        if ($_ -ge $median) { [uint64]($_ - $median) } else { [uint64]($median - $_) }
    })
    return Get-MedianUInt64 $deviations
}

function Get-MedianDouble {
    param([Parameter(Mandatory = $true)][double[]]$Values)
    if ($Values.Count -eq 0) { throw 'Cannot compute a median of an empty sample.' }
    $ordered = @($Values | Sort-Object)
    $middle = [int][Math]::Floor($ordered.Count / 2)
    if ($ordered.Count % 2 -eq 1) { return [double]$ordered[$middle] }
    return ([double]$ordered[$middle - 1] + [double]$ordered[$middle]) / 2.0
}

function Get-PairedBootstrapInterval {
    param(
        [Parameter(Mandatory = $true)][double[]]$Values,
        [Parameter(Mandatory = $true)][int]$RandomSeed
    )
    if ($Values.Count -lt 2) { throw 'Paired bootstrap requires at least two values.' }
    $random = [Random]::new($RandomSeed)
    $medians = [double[]]::new(20000)
    for ($sample = 0; $sample -lt $medians.Count; ++$sample) {
        $resample = [double[]]::new($Values.Count)
        for ($index = 0; $index -lt $resample.Count; ++$index) {
            $resample[$index] = $Values[$random.Next($Values.Count)]
        }
        $medians[$sample] = Get-MedianDouble $resample
    }
    [Array]::Sort($medians)
    $lowIndex = [int][Math]::Floor(0.025 * ($medians.Count - 1))
    $highIndex = [int][Math]::Ceiling(0.975 * ($medians.Count - 1))
    return [pscustomobject][ordered]@{
        low = [double]$medians[$lowIndex]
        high = [double]$medians[$highIndex]
    }
}

function Shuffle-Combinations {
    param(
        [Parameter(Mandatory = $true)][object[]]$Values,
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

function Write-CsvUtf8NoBom {
    param(
        [Parameter(Mandatory = $true)][object[]]$Rows,
        [Parameter(Mandatory = $true)][string]$Path
    )
    if ($Rows.Count -eq 0) { throw "Cannot write an empty CSV: $Path" }
    $lines = @($Rows | ConvertTo-Csv -NoTypeInformation)
    [IO.File]::WriteAllText($Path, (($lines -join "`n") + "`n"), $utf8NoBom)
}

function Assert-DetectedMetric {
    param(
        [Parameter(Mandatory = $true)][object]$Snapshot,
        [Parameter(Mandatory = $true)][string]$Name
    )
    $property = $Snapshot.PSObject.Properties[$Name]
    if ($null -eq $property -or $null -eq $property.Value -or
        [string]$property.Value.status -ne 'DETECTED') {
        throw "Required preflight metric is unavailable: $Name"
    }
}

function Wait-ForSafePreflight {
    $deadline = [DateTime]::UtcNow.AddMinutes(15)
    while ([DateTime]::UtcNow -lt $deadline) {
        $heavy = @(Get-Process -Name primeforge, primeforge-bench, ninja, cl, nvcc `
            -ErrorAction SilentlyContinue)
        if ($heavy.Count -ne 0) {
            Start-Sleep -Seconds 2
            continue
        }

        $snapshotText = (& $hardwareMonitor --once | Out-String).Trim()
        if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($snapshotText)) {
            throw 'Hardware preflight command failed.'
        }
        $snapshot = $snapshotText | ConvertFrom-Json
        foreach ($metric in @(
            'cpu_temperature_celsius', 'cpu_power_watts',
            'gpu_temperature_celsius', 'gpu_power_watts',
            'ram_available_bytes', 'vram_free_mib', 'whea_errors_recent'
        )) {
            Assert-DetectedMetric -Snapshot $snapshot -Name $metric
        }

        $cpuTemperature = Convert-ToDouble $snapshot.cpu_temperature_celsius.value
        $gpuTemperature = Convert-ToDouble $snapshot.gpu_temperature_celsius.value
        $ramAvailable = Convert-ToDouble $snapshot.ram_available_bytes.value
        $vramFree = Convert-ToDouble $snapshot.vram_free_mib.value
        $whea = Convert-ToDouble $snapshot.whea_errors_recent.value
        if (-not [bool]$snapshot.throttling_detected -and $whea -eq 0.0 -and
            $cpuTemperature -le $maximumCpuPreflightCelsius -and
            $gpuTemperature -le $maximumGpuPreflightCelsius -and
            $ramAvailable -ge [double]$minimumRamAvailableBytes -and
            $vramFree -ge $minimumVramFreeMib) {
            return $snapshot
        }
        Start-Sleep -Seconds 5
    }
    throw 'Safe benchmark preflight was not reached within 15 minutes.'
}

function Assert-BackendMetrics {
    param(
        [Parameter(Mandatory = $true)][object]$Row,
        [Parameter(Mandatory = $true)][string]$RequestedBackend,
        [Parameter(Mandatory = $true)][int]$BatchSize
    )
    $cpuNanoseconds = [uint64]$Row.prp_cpu_ns
    $h2dNanoseconds = [uint64]$Row.h2d_ns
    $kernelNanoseconds = [uint64]$Row.kernel_ns
    $d2hNanoseconds = [uint64]$Row.d2h_ns
    $hasCpuMetrics = $cpuNanoseconds -gt 0
    $hasCudaMetrics = $h2dNanoseconds -gt 0 -and $kernelNanoseconds -gt 0 -and
        $d2hNanoseconds -gt 0
    $noCudaMetrics = $h2dNanoseconds -eq 0 -and $kernelNanoseconds -eq 0 -and
        $d2hNanoseconds -eq 0

    if ($RequestedBackend -eq 'cpu') {
        if (-not ([string]$Row.backend).StartsWith('primeforge.cpu.',
                [StringComparison]::Ordinal) -or -not $hasCpuMetrics -or -not $noCudaMetrics) {
            throw "CPU-only metric contract failed for batch size $BatchSize."
        }
        return 'CPU_ONLY'
    }
    if ($RequestedBackend -eq 'cuda') {
        if (-not ([string]$Row.backend).StartsWith('primeforge.cuda.',
                [StringComparison]::Ordinal) -or $cpuNanoseconds -ne 0 -or -not $hasCudaMetrics) {
            throw "CUDA-only metric contract failed for batch size $BatchSize."
        }
        return 'CUDA_ONLY'
    }

    if (-not ([string]$Row.backend).StartsWith('primeforge.auto.',
            [StringComparison]::Ordinal)) {
        throw "Auto-router backend identity is invalid for batch size $BatchSize."
    }
    if ($BatchSize -lt 512) {
        if (-not $hasCpuMetrics -or -not $noCudaMetrics) {
            throw "Auto-router must expose CPU metrics only below 512 candidates (batch $BatchSize)."
        }
        return 'CPU_ONLY'
    }
    if ($cpuNanoseconds -ne 0 -or -not $hasCudaMetrics) {
        throw "Auto-router must expose CUDA metrics only at or above 512 candidates (batch $BatchSize)."
    }
    return 'CUDA_ONLY'
}

$combinations = [Collections.Generic.List[object]]::new()
foreach ($requestedBackend in $backends) {
    foreach ($batchSize in $batchSizes) {
        $combinations.Add([pscustomobject][ordered]@{
            requested_backend = $requestedBackend
            batch_size = $batchSize
        })
    }
}
$random = [Random]::new($Seed)
$schedule = [Collections.Generic.List[object]]::new()
$sequence = 0
for ($warmupRound = 1; $warmupRound -le $Warmups; ++$warmupRound) {
    $order = 0
    foreach ($combination in (Shuffle-Combinations @($combinations) $random)) {
        ++$sequence
        ++$order
        $schedule.Add([pscustomobject][ordered]@{
            sequence = $sequence
            phase = 'warmup'
            round = $warmupRound
            order = $order
            requested_backend = [string]$combination.requested_backend
            batch_size = [int]$combination.batch_size
        })
    }
}
for ($measurementRound = 1; $measurementRound -le $Repetitions; ++$measurementRound) {
    $order = 0
    foreach ($combination in (Shuffle-Combinations @($combinations) $random)) {
        ++$sequence
        ++$order
        $schedule.Add([pscustomobject][ordered]@{
            sequence = $sequence
            phase = 'measure'
            round = $measurementRound
            order = $order
            requested_backend = [string]$combination.requested_backend
            batch_size = [int]$combination.batch_size
        })
    }
}

$initialPreflight = Wait-ForSafePreflight
New-Item -ItemType Directory -Path $outputRoot | Out-Null
Write-CsvUtf8NoBom -Rows @($schedule) -Path (Join-Path $outputRoot 'schedule.csv')
$allRows = [Collections.Generic.List[object]]::new()
$combinationRows = @{}
$routingByCombination = @{}
$preflightEvidence = [Collections.Generic.List[object]]::new()
$datasetSha256 = $null
$observedResultSha256 = $null
$preflightRoundIndex = 0

foreach ($combination in $combinations) {
    $key = '{0}|{1}' -f $combination.requested_backend, $combination.batch_size
    $combinationRows[$key] = [Collections.Generic.List[object]]::new()
}

Push-Location $repositoryRoot
try {
    foreach ($item in $schedule) {
        if ([int]$item.order -eq 1) {
            ++$preflightRoundIndex
            $snapshot = if ($preflightRoundIndex -eq 1) {
                $initialPreflight
            } else {
                Wait-ForSafePreflight
            }
            $preflightEvidence.Add([pscustomobject][ordered]@{
                phase = [string]$item.phase
                round = [int]$item.round
                snapshot = $snapshot
            })
        }

        $combinationName = '{0}-batch-{1}' -f $item.requested_backend, $item.batch_size
        $combinationDirectory = Join-Path $outputRoot $combinationName
        $runsDirectory = Join-Path $combinationDirectory 'runs'
        if (-not (Test-Path -LiteralPath $runsDirectory)) {
            New-Item -ItemType Directory -Path $runsDirectory | Out-Null
        }
        $runName = '{0:D3}-{1}-r{2:D2}-o{3:D2}' -f `
            [int]$item.sequence, [string]$item.phase, [int]$item.round, [int]$item.order
        $runDirectory = Join-Path $runsDirectory $runName
        New-Item -ItemType Directory -Path $runDirectory | Out-Null
        $stdoutPath = Join-Path $runDirectory 'console.stdout.txt'
        $stderrPath = Join-Path $runDirectory 'console.stderr.txt'
        $arguments = @(
            'run', '--profile', ('"{0}"' -f $profile),
            '--backend', [string]$item.requested_backend,
            '--output', ('"{0}"' -f $runDirectory),
            # The direct CUDA backend initializes eagerly in its constructor,
            # while the automatic router initializes CUDA on its first
            # accelerated call. One untimed call per process makes their
            # steady-state timing boundaries identical.
            '--warmup', '1', '--repetitions', '1',
            '--batch-size', [string]$item.batch_size
        )
        $process = Start-Process -FilePath $benchmark -ArgumentList $arguments `
            -WorkingDirectory $repositoryRoot -RedirectStandardOutput $stdoutPath `
            -RedirectStandardError $stderrPath -WindowStyle Hidden -Wait -PassThru
        if ($process.ExitCode -ne 0) {
            throw "primeforge-bench failed for $runName with exit code $($process.ExitCode)."
        }
        if (-not (Select-String -LiteralPath $stdoutPath -SimpleMatch `
                'benchmark.status=PASS' -Quiet)) {
            throw "Benchmark PASS marker is absent for $runName."
        }

        $rawCsvPath = Join-Path $runDirectory 'raw.csv'
        $rawJsonlPath = Join-Path $runDirectory 'raw.jsonl'
        $csvRows = @(Import-Csv -LiteralPath $rawCsvPath)
        $jsonLines = @([IO.File]::ReadAllLines($rawJsonlPath) | Where-Object {
            -not [string]::IsNullOrWhiteSpace($_)
        })
        if ($csvRows.Count -ne 1 -or $jsonLines.Count -ne 1) {
            throw "$runName must contain exactly one CSV and JSONL row."
        }
        $row = $jsonLines[0] | ConvertFrom-Json
        $csvRow = $csvRows[0]
        if ([string]$row.schema -ne 'primeforge.benchmark.raw.v1' -or
            -not [bool]$row.valid_measurement -or [int]$row.repetition -ne 0 -or
            [int]$row.candidate_count -ne 65536 -or
            [int]$row.batch_size -ne [int]$item.batch_size -or
            [string]$row.commit_sha -ne $commit -or
            [string]$row.binary_sha256 -ne $binarySha256 -or
            [string]$row.profile_id -ne 'S64_PRP_65536' -or
            [string]$row.profile_sha256 -ne $profileSha256) {
            throw "Benchmark identity contract failed for $runName."
        }
        if ([int]$csvRow.candidate_count -ne 65536 -or
            [int]$csvRow.batch_size -ne [int]$item.batch_size -or
            [string]$csvRow.commit_sha -ne $commit -or
            [string]$csvRow.binary_sha256 -ne $binarySha256 -or
            [string]$csvRow.result_sha256 -ne [string]$row.result_sha256) {
            throw "CSV/JSONL identity diverged for $runName."
        }
        if ([string]$row.result_sha256 -ne $referenceResultSha256) {
            throw "Reference verdict hash mismatch for $runName."
        }
        if ($null -eq $observedResultSha256) {
            $observedResultSha256 = [string]$row.result_sha256
            $datasetSha256 = [string]$row.dataset_sha256
        }
        if ([string]$row.result_sha256 -ne $observedResultSha256 -or
            [string]$row.dataset_sha256 -ne $datasetSha256) {
            throw "Cross-run determinism failed for $runName."
        }

        $routingMode = Assert-BackendMetrics -Row $row `
            -RequestedBackend ([string]$item.requested_backend) `
            -BatchSize ([int]$item.batch_size)
        $key = '{0}|{1}' -f $item.requested_backend, $item.batch_size
        if ($routingByCombination.ContainsKey($key) -and
            [string]$routingByCombination[$key] -ne $routingMode) {
            throw "Routing changed between rounds for $combinationName."
        }
        $routingByCombination[$key] = $routingMode

        $row | Add-Member -NotePropertyName requested_backend `
            -NotePropertyValue ([string]$item.requested_backend)
        $row | Add-Member -NotePropertyName schedule_phase `
            -NotePropertyValue ([string]$item.phase)
        $row | Add-Member -NotePropertyName schedule_round `
            -NotePropertyValue ([int]$item.round)
        $row | Add-Member -NotePropertyName schedule_order `
            -NotePropertyValue ([int]$item.order)
        $row | Add-Member -NotePropertyName schedule_sequence `
            -NotePropertyValue ([int]$item.sequence)
        if ([string]$item.phase -eq 'measure') {
            $combinationRows[$key].Add($row)
            $allRows.Add($row)
        }
        Write-Host "[$($item.sequence)/$($schedule.Count)] $runName PASS route=$routingMode"
    }
} finally {
    Pop-Location
}

$combinationEvidence = [Collections.Generic.List[object]]::new()
foreach ($combination in $combinations) {
    $key = '{0}|{1}' -f $combination.requested_backend, $combination.batch_size
    $rows = @($combinationRows[$key] | Sort-Object schedule_round)
    if ($rows.Count -ne $Repetitions) {
        throw "$key must contain exactly $Repetitions measured rows."
    }
    $expectedRounds = @($rows | ForEach-Object { [int]$_.schedule_round } | Sort-Object)
    for ($roundIndex = 0; $roundIndex -lt $expectedRounds.Count; ++$roundIndex) {
        if ($expectedRounds[$roundIndex] -ne ($roundIndex + 1)) {
            throw "Measured round coverage is incomplete for $key."
        }
    }
    $combinationDirectory = Join-Path $outputRoot (
        '{0}-batch-{1}' -f $combination.requested_backend, $combination.batch_size
    )
    Write-CsvUtf8NoBom -Rows $rows -Path (Join-Path $combinationDirectory 'raw.csv')
    $jsonLines = @($rows | ForEach-Object { $_ | ConvertTo-Json -Compress })
    [IO.File]::WriteAllText(
        (Join-Path $combinationDirectory 'raw.jsonl'),
        (($jsonLines -join "`n") + "`n"),
        $utf8NoBom
    )
    $combinationEvidence.Add([pscustomobject][ordered]@{
        requested_backend = [string]$combination.requested_backend
        observed_backend = [string]$rows[0].backend
        routing_mode = [string]$routingByCombination[$key]
        batch_size = [int]$combination.batch_size
        rows = $rows
    })
}

if ($combinationEvidence.Count -ne 30 -or $allRows.Count -ne (30 * $Repetitions)) {
    throw 'Consolidated Optimization-07 evidence has an invalid cardinality.'
}
$allRowsArray = @($allRows | Sort-Object schedule_round, schedule_order)
Write-CsvUtf8NoBom -Rows $allRowsArray -Path (Join-Path $outputRoot 'raw.csv')
$allJsonLines = @($allRowsArray | ForEach-Object { $_ | ConvertTo-Json -Compress })
[IO.File]::WriteAllText(
    (Join-Path $outputRoot 'raw.jsonl'), (($allJsonLines -join "`n") + "`n"), $utf8NoBom
)

$summaryRows = [Collections.Generic.List[object]]::new()
foreach ($combination in $combinationEvidence) {
    $rows = @($combination.rows)
    $totals = [uint64[]]@($rows | ForEach-Object { [uint64]$_.total_ns })
    $h2d = [uint64[]]@($rows | ForEach-Object { [uint64]$_.h2d_ns })
    $kernels = [uint64[]]@($rows | ForEach-Object { [uint64]$_.kernel_ns })
    $d2h = [uint64[]]@($rows | ForEach-Object { [uint64]$_.d2h_ns })
    $cpu = [uint64[]]@($rows | ForEach-Object { [uint64]$_.prp_cpu_ns })
    $summaryRows.Add([pscustomobject][ordered]@{
        requested_backend = [string]$combination.requested_backend
        observed_backend = [string]$combination.observed_backend
        routing_mode = [string]$combination.routing_mode
        batch_size = [int]$combination.batch_size
        candidate_count = 65536
        repetitions = $Repetitions
        minimum_total_ns = [uint64](($totals | Measure-Object -Minimum).Minimum)
        median_total_ns = Get-MedianUInt64 $totals
        mad_total_ns = Get-MadUInt64 $totals
        maximum_total_ns = [uint64](($totals | Measure-Object -Maximum).Maximum)
        median_h2d_ns = Get-MedianUInt64 $h2d
        median_kernel_ns = Get-MedianUInt64 $kernels
        median_d2h_ns = Get-MedianUInt64 $d2h
        median_prp_cpu_ns = Get-MedianUInt64 $cpu
        result_sha256 = $observedResultSha256
        commit_sha = $commit
        binary_sha256 = $binarySha256
        validation = 'PASS'
    })
}
$summaryArray = @($summaryRows)
Write-CsvUtf8NoBom -Rows $summaryArray -Path (Join-Path $outputRoot 'summary.csv')
$summaryDocument = [ordered]@{
    schema = 'primeforge.optimization07.batch-summary.v1'
    rows = $summaryArray
}
[IO.File]::WriteAllText(
    (Join-Path $outputRoot 'summary.json'),
    (($summaryDocument | ConvertTo-Json -Depth 6 -Compress) + "`n"),
    $utf8NoBom
)

function Get-SummaryRow {
    param(
        [Parameter(Mandatory = $true)][string]$RequestedBackend,
        [Parameter(Mandatory = $true)][int]$BatchSize
    )
    $row = $summaryArray | Where-Object {
        $_.requested_backend -eq $RequestedBackend -and $_.batch_size -eq $BatchSize
    } | Select-Object -First 1
    if ($null -eq $row) { throw "Missing summary row for $RequestedBackend/$BatchSize." }
    return $row
}

function Get-CombinationMeasuredRows {
    param(
        [Parameter(Mandatory = $true)][string]$RequestedBackend,
        [Parameter(Mandatory = $true)][int]$BatchSize
    )
    $key = '{0}|{1}' -f $RequestedBackend, $BatchSize
    return @($combinationRows[$key] | Sort-Object schedule_round)
}

function Get-PairedDelta {
    param(
        [Parameter(Mandatory = $true)][object[]]$LeftRows,
        [Parameter(Mandatory = $true)][object[]]$RightRows
    )
    if ($LeftRows.Count -ne $Repetitions -or $RightRows.Count -ne $Repetitions) {
        throw 'Paired comparison has incomplete round coverage.'
    }
    $deltas = [double[]]::new($Repetitions)
    for ($index = 0; $index -lt $Repetitions; ++$index) {
        if ([int]$LeftRows[$index].schedule_round -ne [int]$RightRows[$index].schedule_round) {
            throw 'Paired comparison round identities diverged.'
        }
        $deltas[$index] = [double]$LeftRows[$index].total_ns -
            [double]$RightRows[$index].total_ns
    }
    return $deltas
}

function Get-BatchSelection {
    param(
        [Parameter(Mandatory = $true)][string]$RequestedBackend,
        [Parameter(Mandatory = $true)][int]$SeedOffset
    )
    $backendSummaries = @($summaryArray | Where-Object requested_backend -eq $RequestedBackend)
    $nominal = @($backendSummaries | Sort-Object `
        @{ Expression = { [uint64]$_.median_total_ns }; Ascending = $true },
        @{ Expression = { [int]$_.batch_size }; Ascending = $true })[0]
    $nominalRows = Get-CombinationMeasuredRows $RequestedBackend ([int]$nominal.batch_size)
    $plateau = [Collections.Generic.List[int]]::new()
    $plateau.Add([int]$nominal.batch_size)
    $allComparisonsSignificant = $true
    foreach ($candidate in $backendSummaries) {
        if ([int]$candidate.batch_size -eq [int]$nominal.batch_size) { continue }
        # Positive delta means the nominal batch is faster than this candidate.
        $candidateRows = Get-CombinationMeasuredRows $RequestedBackend ([int]$candidate.batch_size)
        $deltas = Get-PairedDelta $candidateRows $nominalRows
        $interval = Get-PairedBootstrapInterval $deltas `
            ($Seed + $SeedOffset + [int]$candidate.batch_size)
        $noiseThreshold = 2.0 * [Math]::Max(
            [double]$candidate.mad_total_ns, [double]$nominal.mad_total_ns
        )
        $significantlyFaster = (Get-MedianDouble $deltas) -gt $noiseThreshold -and
            [double]$interval.low -gt 0.0
        if (-not $significantlyFaster) {
            $allComparisonsSignificant = $false
            $plateau.Add([int]$candidate.batch_size)
        }
    }
    $selected = @($plateau | Sort-Object)[0]
    return [pscustomobject][ordered]@{
        requested_backend = $RequestedBackend
        nominal_fastest_batch_size = [int]$nominal.batch_size
        nominal_fastest_median_total_ns = [uint64]$nominal.median_total_ns
        selection_status = if ($allComparisonsSignificant) {
            'PROVEN_UNIQUE_FASTEST'
        } else {
            'AMBIGUOUS_NOISE_PLATEAU'
        }
        conservative_selected_batch_size = [int]$selected
        noise_plateau_batch_sizes = @($plateau | Sort-Object)
    }
}

$comparisonRows = [Collections.Generic.List[object]]::new()
$cpuCudaComparisons = [Collections.Generic.List[object]]::new()
$autoRegrets = [Collections.Generic.List[object]]::new()
foreach ($batchSize in $batchSizes) {
    $cpuSummary = Get-SummaryRow 'cpu' $batchSize
    $cudaSummary = Get-SummaryRow 'cuda' $batchSize
    # Positive CPU-minus-CUDA delta means CUDA is faster.
    $cpuCudaDeltas = Get-PairedDelta `
        (Get-CombinationMeasuredRows 'cpu' $batchSize) `
        (Get-CombinationMeasuredRows 'cuda' $batchSize)
    $cpuCudaInterval = Get-PairedBootstrapInterval $cpuCudaDeltas ($Seed + $batchSize)
    $cpuCudaMedian = Get-MedianDouble $cpuCudaDeltas
    $cpuCudaNoise = 2.0 * [Math]::Max(
        [double]$cpuSummary.mad_total_ns, [double]$cudaSummary.mad_total_ns
    )
    $cpuCudaVerdict = if ($cpuCudaMedian -gt $cpuCudaNoise -and
        [double]$cpuCudaInterval.low -gt 0.0) {
        'CUDA_FASTER'
    } elseif ($cpuCudaMedian -lt (-1.0 * $cpuCudaNoise) -and
        [double]$cpuCudaInterval.high -lt 0.0) {
        'CPU_FASTER'
    } else {
        'AMBIGUOUS_NOISE_PLATEAU'
    }
    $cpuCudaItem = [pscustomobject][ordered]@{
        batch_size = $batchSize
        median_cpu_minus_cuda_ns = [double]$cpuCudaMedian
        noise_threshold_ns = [double]$cpuCudaNoise
        bootstrap_low_ns = [double]$cpuCudaInterval.low
        bootstrap_high_ns = [double]$cpuCudaInterval.high
        verdict = $cpuCudaVerdict
    }
    $cpuCudaComparisons.Add($cpuCudaItem)
    $comparisonRows.Add([pscustomobject][ordered]@{
        comparison_type = 'CPU_MINUS_CUDA'
        batch_size = $batchSize
        reference_backend = 'cuda'
        median_delta_ns = [double]$cpuCudaMedian
        noise_threshold_ns = [double]$cpuCudaNoise
        bootstrap_low_ns = [double]$cpuCudaInterval.low
        bootstrap_high_ns = [double]$cpuCudaInterval.high
        verdict = $cpuCudaVerdict
    })

    $directBackend = if ([uint64]$cpuSummary.median_total_ns -le
        [uint64]$cudaSummary.median_total_ns) { 'cpu' } else { 'cuda' }
    $directSummary = if ($directBackend -eq 'cpu') { $cpuSummary } else { $cudaSummary }
    # Positive auto-minus-direct delta is routing regret.
    $autoDeltas = Get-PairedDelta `
        (Get-CombinationMeasuredRows 'auto' $batchSize) `
        (Get-CombinationMeasuredRows $directBackend $batchSize)
    $autoInterval = Get-PairedBootstrapInterval $autoDeltas ($Seed + 100000 + $batchSize)
    $autoMedian = Get-MedianDouble $autoDeltas
    $autoSummary = Get-SummaryRow 'auto' $batchSize
    $autoNoise = 2.0 * [Math]::Max(
        [double]$autoSummary.mad_total_ns, [double]$directSummary.mad_total_ns
    )
    $regretStatus = if ($autoMedian -gt $autoNoise -and
        [double]$autoInterval.low -gt 0.0) {
        'REGRET_EXCEEDS_NOISE'
    } else {
        'NO_SIGNIFICANT_REGRET'
    }
    $regretItem = [pscustomobject][ordered]@{
        batch_size = $batchSize
        minimum_direct_backend = $directBackend
        median_auto_regret_ns = [double]$autoMedian
        median_auto_regret_percent = if ([double]$directSummary.median_total_ns -eq 0.0) {
            0.0
        } else {
            100.0 * $autoMedian / [double]$directSummary.median_total_ns
        }
        noise_threshold_ns = [double]$autoNoise
        bootstrap_low_ns = [double]$autoInterval.low
        bootstrap_high_ns = [double]$autoInterval.high
        verdict = $regretStatus
    }
    $autoRegrets.Add($regretItem)
    $comparisonRows.Add([pscustomobject][ordered]@{
        comparison_type = 'AUTO_MINUS_MIN_DIRECT'
        batch_size = $batchSize
        reference_backend = $directBackend
        median_delta_ns = [double]$autoMedian
        noise_threshold_ns = [double]$autoNoise
        bootstrap_low_ns = [double]$autoInterval.low
        bootstrap_high_ns = [double]$autoInterval.high
        verdict = $regretStatus
    })
}
Write-CsvUtf8NoBom -Rows @($comparisonRows) -Path (Join-Path $outputRoot 'comparisons.csv')
$comparisonDocument = [ordered]@{
    schema = 'primeforge.optimization07.batch-comparisons.v1'
    cpu_cuda = @($cpuCudaComparisons)
    auto_regret = @($autoRegrets)
}
[IO.File]::WriteAllText(
    (Join-Path $outputRoot 'comparisons.json'),
    (($comparisonDocument | ConvertTo-Json -Depth 6 -Compress) + "`n"),
    $utf8NoBom
)

$autoCudaRows = @($summaryArray | Where-Object {
    $_.requested_backend -eq 'auto' -and $_.routing_mode -eq 'CUDA_ONLY'
} | Sort-Object batch_size)
$implementationRoutingPass = $autoCudaRows.Count -ne 0 -and
    [int]$autoCudaRows[0].batch_size -eq 512
$significantCuda = @($cpuCudaComparisons | Where-Object verdict -eq 'CUDA_FASTER' |
    Sort-Object batch_size)
$measuredCrossover = if ($significantCuda.Count -eq 0) {
    'NONE'
} else {
    [string]$significantCuda[0].batch_size
}
$expectedPatternPass = @($cpuCudaComparisons | Where-Object {
    ($_.batch_size -lt 512 -and $_.verdict -ne 'CPU_FASTER') -or
    ($_.batch_size -ge 512 -and $_.verdict -ne 'CUDA_FASTER')
}).Count -eq 0
$regretPass = @($autoRegrets | Where-Object verdict -eq 'REGRET_EXCEEDS_NOISE').Count -eq 0
$thresholdStatus = if (-not $implementationRoutingPass) {
    'FAIL_IMPLEMENTATION_ROUTING'
} elseif ($measuredCrossover -ne '512') {
    'FAIL_MEASURED_CROSSOVER_DIFFERS'
} elseif (-not $expectedPatternPass) {
    'AMBIGUOUS_NOISE_PLATEAU'
} elseif (-not $regretPass) {
    'FAIL_AUTO_REGRET_EXCEEDS_NOISE'
} else {
    'SUPPORTED'
}

$cpuSelection = Get-BatchSelection 'cpu' 200000
$cudaSelection = Get-BatchSelection 'cuda' 300000
$autoSelection = Get-BatchSelection 'auto' 400000
$decision = [ordered]@{
    schema = 'primeforge.optimization07.batch-decision.v1'
    profile_id = 'S64_PRP_65536'
    candidate_count = 65536
    repetitions = $Repetitions
    randomized_block_seed = $Seed
    comparison_metric = 'paired_median_total_ns'
    significance_rule = 'PAIRED_BOOTSTRAP_95_PERCENT_AND_DELTA_GT_2_MAX_MAD'
    conservative_tie_break = 'SMALLEST_BATCH_ON_AMBIGUOUS_NOISE_PLATEAU'
    result_sha256 = $observedResultSha256
    implementation_routing_validation = if ($implementationRoutingPass) { 'PASS' } else { 'FAIL' }
    implementation_auto_threshold_candidates = if ($implementationRoutingPass) { 512 } else { 'NONE' }
    measured_cpu_cuda_crossover_candidates = $measuredCrossover
    threshold_optimality_status = $thresholdStatus
    retained_auto_threshold_candidates = if ($thresholdStatus -eq 'SUPPORTED') { 512 } else { 'NONE' }
    auto_regret_validation = if ($regretPass) { 'PASS' } else { 'FAIL' }
    cpu_batch_selection = $cpuSelection
    cuda_batch_selection = $cudaSelection
    auto_batch_selection = $autoSelection
    status = if ($thresholdStatus -eq 'SUPPORTED') { 'PASS' } else { $thresholdStatus }
}
[IO.File]::WriteAllText(
    (Join-Path $outputRoot 'decision.json'),
    (($decision | ConvertTo-Json -Depth 8 -Compress) + "`n"),
    $utf8NoBom
)

$environment = [ordered]@{
    schema = 'primeforge.optimization07.batch-environment.v1'
    captured_utc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ss.fffZ')
    commit_sha = $commit
    repository_dirty = 'NO'
    binary_path = 'out/build/msvc-cuda-release/primeforge-bench.exe'
    binary_sha256 = $binarySha256
    hardware_monitor_path = 'out/build/msvc-cuda-release/hardware_monitor.exe'
    profile_path = 'benchmarks/profiles/s64_prp_65536.json'
    profile_sha256 = $profileSha256
    dataset_sha256 = $datasetSha256
    result_sha256 = $observedResultSha256
    requested_backends = $backends
    batch_sizes = $batchSizes
    repetitions = $Repetitions
    warmups = $Warmups
    per_process_backend_warmups = 1
    randomized_block_seed = $Seed
    schedule_protocol = 'ONE_SCHEDULED_ROW_AND_ONE_UNTIMED_BACKEND_CALL_PER_PROCESS;30_RANDOMIZED_COMBINATIONS_PER_BLOCK'
    measured_row_count = 30 * $Repetitions
    safety_thresholds = [ordered]@{
        maximum_cpu_preflight_celsius = $maximumCpuPreflightCelsius
        maximum_gpu_preflight_celsius = $maximumGpuPreflightCelsius
        minimum_ram_available_bytes = $minimumRamAvailableBytes
        minimum_vram_free_mib = $minimumVramFreeMib
        require_no_throttling = $true
        require_zero_recent_whea_errors = $true
    }
    preflight_snapshots = @($preflightEvidence)
}
[IO.File]::WriteAllText(
    (Join-Path $outputRoot 'environment.json'),
    (($environment | ConvertTo-Json -Depth 12 -Compress) + "`n"),
    $utf8NoBom
)

Write-Host "Optimization-07 batch sweep complete: $thresholdStatus; output=$outputRoot"
Write-Host "Implementation threshold=512; measured crossover=$measuredCrossover; retained threshold=$($decision.retained_auto_threshold_candidates)"
