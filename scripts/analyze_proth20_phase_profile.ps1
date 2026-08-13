[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [Parameter(Mandatory = $true)][string]$InputDirectory,
    [string]$OutputDirectory = '',
    [string]$ExpectedResultsFile = '',
    [ValidateRange(20, 100)][int]$ExpectedCandidates = 20,
    [ValidateRange(90.0, 100.0)][double]$MinimumCoveragePercent = 95.0,
    [ValidateRange(0.0, 10.0)][double]$MaximumStartTemperatureSpreadC = 3.0
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

function Get-Percentile([double[]]$Values, [double]$Fraction) {
    if ($Values.Count -eq 0) { throw 'Cannot summarize an empty value set.' }
    $ordered = @($Values | Sort-Object)
    $index = [Math]::Ceiling($Fraction * $ordered.Count) - 1
    if ($index -lt 0) { $index = 0 }
    return [double]$ordered[$index]
}

function Get-PhaseValue([object[]]$Records, [string]$Name) {
    $matches = @($Records | Where-Object phase -eq $Name)
    if ($matches.Count -ne 1) {
        throw "Expected exactly one $Name phase, found $($matches.Count)."
    }
    return [uint64]$matches[0].nanoseconds
}

function Get-DetectedNumber($Observation) {
    if ($null -eq $Observation -or $Observation.status -ne 'DETECTED') { return $null }
    return [double]::Parse([string]$Observation.value, $culture)
}

$inputPath = Resolve-ProjectPath $InputDirectory
if (-not (Test-Path -LiteralPath $inputPath -PathType Container)) {
    throw "Profile input directory is missing: $inputPath"
}
$outputPath = if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    Join-Path $inputPath 'analysis'
} else {
    Resolve-ProjectPath $OutputDirectory
}
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null

$gateRunIds = @('A1-one-worker', 'B1-two-workers', 'B2-two-workers', 'A2-one-worker')
$includedRunIds = @($gateRunIds + 'direct-baseline')
$phaseRecords = [Collections.Generic.List[object]]::new()
$verdictRecords = [Collections.Generic.List[object]]::new()
$resultDetails = [Collections.Generic.List[object]]::new()
$sequence = 0L
$stdoutFiles = @(Get-ChildItem -LiteralPath $inputPath -Recurse -Filter '*.stdout.log' -File |
    Where-Object {
        $relative = $_.FullName.Substring($inputPath.Length).TrimStart([char[]]@('\', '/'))
        ($relative -split '[\\/]')[0] -in $includedRunIds
    } | Sort-Object FullName)
if ($stdoutFiles.Count -eq 0) { throw "No *.stdout.log file was found under $inputPath" }

foreach ($file in $stdoutFiles) {
    $relative = $file.FullName.Substring($inputPath.Length).TrimStart([char[]]@('\', '/'))
    $parts = @($relative -split '[\\/]')
    $runId = $parts[0]
    $workerId = if ($parts.Count -gt 1) { $parts[$parts.Count - 2] } else { 'worker-01' }
    foreach ($line in ([IO.File]::ReadAllText($file.FullName) -split '[\r\n]+')) {
        $normalized = $line.Trim()
        if ($normalized -match '^PRIMEFORGE_PHASE\t([0-9]+)\t([0-9]+)\t([0-9]+)\t([A-Z0-9_]+)\t([0-9]+)$') {
            ++$sequence
            $phaseRecords.Add([pscustomobject][ordered]@{
                run = $runId
                worker = $workerId
                source = $relative.Replace('\', '/')
                candidate_index = [uint64]$Matches[1]
                k = [uint64]$Matches[2]
                n = [uint64]$Matches[3]
                phase = $Matches[4]
                nanoseconds = [uint64]$Matches[5]
                sequence = $sequence
            })
        } elseif ($normalized -match '^PRIMEFORGE_BATCH_COMPLETE\t[0-9]+\t([0-9]+)\t([0-9]+)\t(PROVEN_PRIME|COMPOSITE)$') {
            $verdictRecords.Add([pscustomobject][ordered]@{
                run = $runId
                k = [uint64]$Matches[1]
                n = [uint64]$Matches[2]
                verdict = $Matches[3]
            })
        } elseif ($normalized -match '^([0-9]+) \* 2\^([0-9]+) \+ 1 is (prime|composite), a = ([0-9]+), .*RES64 = ([0-9A-F]+)$') {
            $status = if ($Matches[3] -eq 'prime') { 'PROVEN_PRIME' } else { 'COMPOSITE' }
            $verdictRecords.Add([pscustomobject][ordered]@{
                run = $runId
                k = [uint64]$Matches[1]
                n = [uint64]$Matches[2]
                verdict = $status
            })
            $resultDetails.Add([pscustomobject][ordered]@{
                run = $runId
                k = [uint64]$Matches[1]
                n = [uint64]$Matches[2]
                verdict = $status
                witness = [uint64]$Matches[4]
                res64 = $Matches[5]
            })
        }
    }
}

$candidateRows = [Collections.Generic.List[object]]::new()
$candidateGroups = @($phaseRecords | Where-Object { $_.k -ne 0 -and $_.phase -eq 'CANDIDATE_TOTAL' } |
    Group-Object run, k, n)
foreach ($totalGroup in $candidateGroups) {
    if ($totalGroup.Count -ne 1) { throw "Duplicate CANDIDATE_TOTAL record: $($totalGroup.Name)" }
    $totalRecord = $totalGroup.Group[0]
    $records = @($phaseRecords | Where-Object {
        $_.run -eq $totalRecord.run -and $_.k -eq $totalRecord.k -and $_.n -eq $totalRecord.n
    } | Sort-Object sequence)
    $resultRecord = @($records | Where-Object phase -eq 'RESULT_FORMAT_AND_WRITE')
    if ($resultRecord.Count -ne 1) { throw "Missing result phase for $($totalGroup.Name)" }
    $cleanup = @($records | Where-Object {
        $_.sequence -gt $resultRecord[0].sequence -and
        $_.phase -in @('KERNEL_RELEASE', 'BUFFER_RELEASE', 'PROGRAM_CLEAR')
    })
    if ($cleanup.Count -ne 3) { throw "Expected three final cleanup phases for $($totalGroup.Name)" }

    $explained = [uint64]0
    foreach ($phase in @(
        'WITNESS_SELECTION', 'GPMP_CONSTRUCT_TOTAL', 'CHECKPOINT', 'A_POW_K',
        'MAIN_SQUARING_LOOP', 'FINAL_RESIDUE_TOTAL', 'GERBICZ_FINAL_CHECK',
        'RESULT_FORMAT_AND_WRITE'
    )) {
        $explained += Get-PhaseValue -Records $records -Name $phase
    }
    $explained += [uint64](($cleanup.nanoseconds | Measure-Object -Sum).Sum)
    $total = [uint64]$totalRecord.nanoseconds
    $coverage = 100.0 * [double]$explained / [double]$total
    $verdict = @($verdictRecords | Where-Object {
        $_.run -eq $totalRecord.run -and $_.k -eq $totalRecord.k -and $_.n -eq $totalRecord.n
    } | Select-Object -ExpandProperty verdict -Unique)
    if ($verdict.Count -ne 1) { throw "Missing or ambiguous verdict for $($totalGroup.Name)" }
    $candidateRows.Add([pscustomobject][ordered]@{
        run = $totalRecord.run
        worker = $totalRecord.worker
        k = $totalRecord.k
        n = $totalRecord.n
        verdict = $verdict[0]
        total_ns = $total
        explained_ns = $explained
        unexplained_ns = [int64]$total - [int64]$explained
        coverage_percent = $coverage
        gpmp_construct_ns = Get-PhaseValue -Records $records -Name 'GPMP_CONSTRUCT_TOTAL'
        main_squaring_ns = Get-PhaseValue -Records $records -Name 'MAIN_SQUARING_LOOP'
    })
}

$availableRuns = @($candidateRows.run | Sort-Object -Unique)
$missingRuns = @($gateRunIds | Where-Object { $_ -notin $availableRuns })
if ($missingRuns.Count -ne 0) { throw "Missing AB/BA runs: $($missingRuns -join ', ')" }

$candidateSetReference = @($candidateRows | Where-Object run -eq $gateRunIds[0] |
    Sort-Object k, n | ForEach-Object { "$($_.k)/$($_.n)" })
$candidateSetGate = $candidateSetReference.Count -eq $ExpectedCandidates
$runCounts = [ordered]@{}
foreach ($runId in $gateRunIds) {
    $runRows = @($candidateRows | Where-Object run -eq $runId)
    $runCounts[$runId] = $runRows.Count
    $set = @($runRows | Sort-Object k, n | ForEach-Object { "$($_.k)/$($_.n)" })
    if (($set -join "`n") -ne ($candidateSetReference -join "`n")) { $candidateSetGate = $false }
}

$verdictAgreement = $true
foreach ($key in $candidateSetReference) {
    $tokens = $key -split '/'
    $statuses = @($candidateRows | Where-Object {
        $_.run -in $gateRunIds -and $_.k -eq [uint64]$tokens[0] -and $_.n -eq [uint64]$tokens[1]
    } | Select-Object -ExpandProperty verdict -Unique)
    if ($statuses.Count -ne 1) { $verdictAgreement = $false }
}

$exactResultGate = 'NOT_CHECKED'
$exactComparisons = [Collections.Generic.List[object]]::new()
if (-not [string]::IsNullOrWhiteSpace($ExpectedResultsFile)) {
    $expectedPath = Resolve-ProjectPath $ExpectedResultsFile
    if (-not (Test-Path -LiteralPath $expectedPath -PathType Leaf)) {
        throw "Expected-results file is missing: $expectedPath"
    }
    $expected = @(Import-Csv -Delimiter "`t" -LiteralPath $expectedPath)
    $exactResultGate = 'PASS'
    foreach ($runId in $gateRunIds) {
        foreach ($row in $expected) {
            $actual = @($resultDetails | Where-Object {
                $_.run -eq $runId -and $_.k -eq [uint64]$row.k -and $_.n -eq [uint64]$row.n
            })
            $match = $actual.Count -eq 1 -and
                $actual[0].verdict -eq $row.primality_status -and
                $actual[0].witness -eq [uint64]$row.witness -and
                $actual[0].res64 -eq $row.res64
            if (-not $match) { $exactResultGate = 'FAIL' }
            $exactComparisons.Add([pscustomobject][ordered]@{
                run = $runId
                k = [uint64]$row.k
                n = [uint64]$row.n
                expected_status = $row.primality_status
                actual_status = if ($actual.Count -eq 1) { $actual[0].verdict } else { 'MISSING_OR_DUPLICATE' }
                expected_witness = [uint64]$row.witness
                actual_witness = if ($actual.Count -eq 1) { $actual[0].witness } else { 'UNKNOWN' }
                expected_res64 = $row.res64
                actual_res64 = if ($actual.Count -eq 1) { $actual[0].res64 } else { 'UNKNOWN' }
                status = if ($match) { 'PASS' } else { 'FAIL' }
            })
        }
    }
}

$gateRows = @($candidateRows | Where-Object run -in $gateRunIds)
$coverageValues = @($gateRows | ForEach-Object { [double]$_.coverage_percent })
$minimumCoverage = [double](($coverageValues | Measure-Object -Minimum).Minimum)
$meanCoverage = [double](($coverageValues | Measure-Object -Average).Average)
$coverageGate = $minimumCoverage -ge $MinimumCoveragePercent

$telemetryRows = [Collections.Generic.List[object]]::new()
foreach ($runId in $gateRunIds) {
    $telemetryPath = Join-Path (Join-Path $inputPath $runId) 'telemetry.jsonl'
    if (-not (Test-Path -LiteralPath $telemetryPath -PathType Leaf)) {
        throw "Missing telemetry for ${runId}: $telemetryPath"
    }
    $samples = @([IO.File]::ReadLines($telemetryPath) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } |
        ForEach-Object { $_ | ConvertFrom-Json })
    if ($samples.Count -eq 0) { throw "Empty telemetry for $runId" }
    $gpuTemperatures = @($samples | ForEach-Object { Get-DetectedNumber $_.gpu_temperature_celsius } |
        Where-Object { $null -ne $_ })
    $cpuTemperatures = @($samples | ForEach-Object { Get-DetectedNumber $_.cpu_temperature_celsius } |
        Where-Object { $null -ne $_ })
    $gpuPower = @($samples | ForEach-Object { Get-DetectedNumber $_.gpu_power_watts } |
        Where-Object { $null -ne $_ })
    if ($gpuTemperatures.Count -eq 0) { throw "GPU temperature remained UNKNOWN for $runId" }
    $telemetryRows.Add([pscustomobject][ordered]@{
        run = $runId
        samples = $samples.Count
        start_gpu_temperature_c = [double]$gpuTemperatures[0]
        max_gpu_temperature_c = [double](($gpuTemperatures | Measure-Object -Maximum).Maximum)
        max_cpu_temperature_c = if ($cpuTemperatures.Count) {
            [double](($cpuTemperatures | Measure-Object -Maximum).Maximum)
        } else { 'UNKNOWN' }
        max_gpu_power_w = if ($gpuPower.Count) {
            [double](($gpuPower | Measure-Object -Maximum).Maximum)
        } else { 'UNKNOWN' }
        throttling_samples = @($samples | Where-Object throttling_detected).Count
        max_whea_errors = [uint64](($samples.whea_errors_recent.value | Where-Object { $_ -ne 'UNKNOWN' } |
            ForEach-Object { [uint64]$_ } | Measure-Object -Maximum).Maximum)
    })
}
$startTemperatures = @($telemetryRows | ForEach-Object { [double]$_.start_gpu_temperature_c })
$startTemperatureSpread = [double](($startTemperatures | Measure-Object -Maximum).Maximum) -
    [double](($startTemperatures | Measure-Object -Minimum).Minimum)
$temperatureGate = $startTemperatureSpread -le $MaximumStartTemperatureSpreadC
$hardwareGate = @($telemetryRows | Where-Object {
    $_.throttling_samples -ne 0 -or $_.max_whea_errors -ne 0 -or
    ($_.max_cpu_temperature_c -ne 'UNKNOWN' -and [double]$_.max_cpu_temperature_c -gt 92.0)
}).Count -eq 0

$runPerformance = [Collections.Generic.List[object]]::new()
foreach ($runId in $gateRunIds) {
    $runRows = @($gateRows | Where-Object run -eq $runId)
    $workerCount = if ($runId -like 'B*') { 2 } else { 1 }
    $sumSeconds = [double](($runRows.total_ns | Measure-Object -Sum).Sum) / 1.0e9
    $meanSeconds = [double](($runRows.total_ns | Measure-Object -Average).Average) / 1.0e9
    $runPerformance.Add([pscustomobject][ordered]@{
        run = $runId
        workers = $workerCount
        candidates = $runRows.Count
        summed_candidate_seconds = $sumSeconds
        mean_candidate_seconds = $meanSeconds
        effective_candidates_per_hour = 3600.0 * $workerCount * $runRows.Count / $sumSeconds
    })
}
$directRows = @($candidateRows | Where-Object run -eq 'direct-baseline')
$directProcessTotals = @($phaseRecords | Where-Object {
    $_.run -eq 'direct-baseline' -and $_.phase -eq 'PROCESS_TOTAL'
})
$a1ProcessTotals = @($phaseRecords | Where-Object {
    $_.run -eq 'A1-one-worker' -and $_.phase -eq 'PROCESS_TOTAL'
})
if ($directRows.Count -eq 0 -or $directProcessTotals.Count -ne $directRows.Count -or
    $a1ProcessTotals.Count -ne 1) {
    throw 'Direct/batch end-to-end baseline records are incomplete.'
}
$directPerCandidateNanoseconds = [double](($directProcessTotals.nanoseconds | Measure-Object -Sum).Sum) /
    [double]$directRows.Count
$batchPerCandidateNanoseconds = [double]$a1ProcessTotals[0].nanoseconds / [double]$ExpectedCandidates
$directVsBatch = [pscustomobject][ordered]@{
    direct_processes = $directRows.Count
    direct_mean_end_to_end_ns_per_candidate = $directPerCandidateNanoseconds
    persistent_batch_candidates = $ExpectedCandidates
    persistent_batch_mean_end_to_end_ns_per_candidate = $batchPerCandidateNanoseconds
    persistent_batch_speedup = $directPerCandidateNanoseconds / $batchPerCandidateNanoseconds
    interpretation = 'Includes process, platform and OpenCL context startup; direct uses one process per candidate.'
}

$phaseRows = [Collections.Generic.List[object]]::new()
foreach ($group in @($phaseRecords | Where-Object { $_.run -in $gateRunIds -and $_.k -ne 0 } |
    Group-Object phase | Sort-Object Name)) {
    $values = @($group.Group | ForEach-Object { [double]$_.nanoseconds })
    $phaseRows.Add([pscustomobject][ordered]@{
        phase = $group.Name
        samples = $values.Count
        total_ns = [uint64](($values | Measure-Object -Sum).Sum)
        mean_ns = [double](($values | Measure-Object -Average).Average)
        median_ns = Get-Percentile -Values $values -Fraction 0.5
        p95_ns = Get-Percentile -Values $values -Fraction 0.95
    })
}

$constructMean = [double](($gateRows.gpmp_construct_ns | Measure-Object -Average).Average)
$mainMean = [double](($gateRows.main_squaring_ns | Measure-Object -Average).Average)
$candidateMean = [double](($gateRows.total_ns | Measure-Object -Average).Average)
$jalonAStatus = if ($candidateSetGate -and $verdictAgreement -and $coverageGate -and
    $exactResultGate -ne 'FAIL' -and
    $temperatureGate -and $hardwareGate) { 'PASS' } else { 'FAIL' }
$summary = [pscustomobject][ordered]@{
    schema = 'primeforge.proth20.phase-profile.summary.v1'
    generated_utc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    candidate_cohort_size = $ExpectedCandidates
    candidate_executions = $gateRows.Count
    schedule = 'A1_ONE_WORKER;B1_TWO_WORKERS;B2_TWO_WORKERS;A2_ONE_WORKER'
    run_candidate_counts = $runCounts
    candidate_sets_identical = $candidateSetGate
    verdicts_identical = $verdictAgreement
    exact_witness_and_res64_gate = $exactResultGate
    minimum_coverage_percent = $minimumCoverage
    mean_coverage_percent = $meanCoverage
    required_coverage_percent = $MinimumCoveragePercent
    mean_candidate_ns = $candidateMean
    mean_gpmp_construct_ns = $constructMean
    mean_main_squaring_ns = $mainMean
    mean_gpmp_construct_percent = 100.0 * $constructMean / $candidateMean
    mean_main_squaring_percent = 100.0 * $mainMean / $candidateMean
    run_performance = $runPerformance
    direct_vs_persistent_batch = $directVsBatch
    start_gpu_temperature_spread_c = $startTemperatureSpread
    maximum_start_temperature_spread_c = $MaximumStartTemperatureSpreadC
    hardware_gate = if ($hardwareGate) { 'PASS' } else { 'FAIL' }
    p0_100_survivor_gate = if ($ExpectedCandidates -eq 100) { 'PASS' } else { 'NOT_RUN' }
    jalon_a_20_to_100_candidate_gate = if ($ExpectedCandidates -ge 20 -and $ExpectedCandidates -le 100) {
        'PASS'
    } else { 'FAIL' }
    status = $jalonAStatus
}

$candidateTsv = @("run`tworker`tk`tn`tverdict`ttotal_ns`texplained_ns`tunexplained_ns`tcoverage_percent`tgpmp_construct_ns`tmain_squaring_ns")
foreach ($row in @($candidateRows | Sort-Object run, k, n)) {
    $candidateTsv += @($row.run, $row.worker, $row.k, $row.n, $row.verdict, $row.total_ns,
        $row.explained_ns, $row.unexplained_ns, $row.coverage_percent.ToString('0.000000', $culture),
        $row.gpmp_construct_ns, $row.main_squaring_ns) -join "`t"
}
[IO.File]::WriteAllLines((Join-Path $outputPath 'candidates.tsv'), $candidateTsv, $utf8)

$phaseTsv = @("phase`tsamples`ttotal_ns`tmean_ns`tmedian_ns`tp95_ns")
foreach ($row in $phaseRows) {
    $phaseTsv += @($row.phase, $row.samples, $row.total_ns,
        $row.mean_ns.ToString('0.000', $culture), $row.median_ns.ToString('0.000', $culture),
        $row.p95_ns.ToString('0.000', $culture)) -join "`t"
}
[IO.File]::WriteAllLines((Join-Path $outputPath 'phases.tsv'), $phaseTsv, $utf8)

$telemetryTsv = @("run`tsamples`tstart_gpu_temperature_c`tmax_gpu_temperature_c`tmax_cpu_temperature_c`tmax_gpu_power_w`tthrottling_samples`tmax_whea_errors")
foreach ($row in $telemetryRows) {
    $telemetryTsv += @($row.run, $row.samples, $row.start_gpu_temperature_c,
        $row.max_gpu_temperature_c, $row.max_cpu_temperature_c, $row.max_gpu_power_w,
        $row.throttling_samples, $row.max_whea_errors) -join "`t"
}
[IO.File]::WriteAllLines((Join-Path $outputPath 'telemetry.tsv'), $telemetryTsv, $utf8)

$performanceTsv = @("run`tworkers`tcandidates`tsummed_candidate_seconds`tmean_candidate_seconds`teffective_candidates_per_hour")
foreach ($row in $runPerformance) {
    $performanceTsv += @($row.run, $row.workers, $row.candidates,
        $row.summed_candidate_seconds.ToString('0.000000', $culture),
        $row.mean_candidate_seconds.ToString('0.000000', $culture),
        $row.effective_candidates_per_hour.ToString('0.000', $culture)) -join "`t"
}
[IO.File]::WriteAllLines((Join-Path $outputPath 'run-performance.tsv'), $performanceTsv, $utf8)
if ($exactComparisons.Count -ne 0) {
    $comparisonTsv = @("run`tk`tn`texpected_status`tactual_status`texpected_witness`tactual_witness`texpected_res64`tactual_res64`tstatus")
    foreach ($row in $exactComparisons) {
        $comparisonTsv += @($row.run, $row.k, $row.n, $row.expected_status, $row.actual_status,
            $row.expected_witness, $row.actual_witness, $row.expected_res64, $row.actual_res64,
            $row.status) -join "`t"
    }
    [IO.File]::WriteAllLines((Join-Path $outputPath 'exact-result-comparison.tsv'), $comparisonTsv, $utf8)
}
[IO.File]::WriteAllText((Join-Path $outputPath 'summary.json'),
    ($summary | ConvertTo-Json -Depth 8) + "`n", $utf8)

$topPhases = @(
    [pscustomobject]@{ name='gpmp construction'; value=$constructMean; color='#d97706' },
    [pscustomobject]@{ name='main squaring'; value=$mainMean; color='#2563eb' },
    [pscustomobject]@{ name='other explained'; value=[Math]::Max(0.0, $candidateMean - $constructMean - $mainMean); color='#059669' }
)
$svg = [Text.StringBuilder]::new()
[void]$svg.AppendLine('<svg xmlns="http://www.w3.org/2000/svg" width="1100" height="220" viewBox="0 0 1100 220">')
[void]$svg.AppendLine('<rect width="1100" height="220" fill="#ffffff"/>')
[void]$svg.AppendLine('<text x="30" y="34" font-family="Segoe UI, sans-serif" font-size="22" fill="#111827">Mean Proth20 candidate wall-time decomposition</text>')
$x = 30.0
$barWidth = 1040.0
foreach ($phase in $topPhases) {
    $width = $barWidth * [double]$phase.value / $candidateMean
    [void]$svg.AppendLine(('<rect x="{0}" y="58" width="{1}" height="46" fill="{2}"/>' -f
        $x.ToString('0.000', $culture), $width.ToString('0.000', $culture), $phase.color))
    $x += $width
}
$legendX = 30
foreach ($phase in $topPhases) {
    $percent = 100.0 * [double]$phase.value / $candidateMean
    [void]$svg.AppendLine(('<rect x="{0}" y="132" width="18" height="18" fill="{1}"/>' -f $legendX, $phase.color))
    [void]$svg.AppendLine(('<text x="{0}" y="147" font-family="Segoe UI, sans-serif" font-size="15" fill="#111827">{1}: {2}%</text>' -f
        ($legendX + 26), $phase.name, $percent.ToString('0.00', $culture)))
    $legendX += 335
}
[void]$svg.AppendLine(('<text x="30" y="190" font-family="Segoe UI, sans-serif" font-size="14" fill="#4b5563">Coverage: {0}% mean; {1}% minimum. Nested phases are excluded from the top-level sum.</text>' -f
    $meanCoverage.ToString('0.000', $culture), $minimumCoverage.ToString('0.000', $culture)))
[void]$svg.AppendLine('</svg>')
[IO.File]::WriteAllText((Join-Path $outputPath 'timeline.svg'), $svg.ToString(), $utf8)

Write-Host ('proth20.profile.cohort=' + $ExpectedCandidates)
Write-Host ('proth20.profile.executions=' + $gateRows.Count)
Write-Host ('proth20.profile.coverage.minimum_percent=' + $minimumCoverage.ToString('0.000000', $culture))
Write-Host ('proth20.profile.coverage.mean_percent=' + $meanCoverage.ToString('0.000000', $culture))
Write-Host ('proth20.profile.gpmp_construct.mean_percent=' + $summary.mean_gpmp_construct_percent.ToString('0.000000', $culture))
Write-Host ('proth20.profile.main_squaring.mean_percent=' + $summary.mean_main_squaring_percent.ToString('0.000000', $culture))
Write-Host "proth20.profile.output=$outputPath"
Write-Host "proth20.profile.status=$jalonAStatus"
if ($jalonAStatus -ne 'PASS') { throw 'The Proth20 phase-profile gate failed.' }
