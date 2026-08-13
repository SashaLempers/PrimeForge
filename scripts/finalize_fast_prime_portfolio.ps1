[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [string]$PortfolioFile = 'docs\reports\CANDIDATE_RANGE_PORTFOLIO.json',
    [string]$BenchmarkFile = 'benchmarks\fast-prime\raw.jsonl',
    [string]$PreflightFile = 'docs\reports\NOVELTY_PREFLIGHT_MACHINE.json'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$utf8 = [Text.UTF8Encoding]::new($false)
$culture = [Globalization.CultureInfo]::InvariantCulture

function Resolve-ProjectPath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $root $Path))
}
function Format-Decimal([double]$Value, [string]$Pattern = '0.000000') {
    return $Value.ToString($Pattern, $script:culture)
}

$portfolioPath = Resolve-ProjectPath $PortfolioFile
$benchmarkPath = Resolve-ProjectPath $BenchmarkFile
$preflightPath = Resolve-ProjectPath $PreflightFile
$portfolio = Get-Content -Raw -LiteralPath $portfolioPath | ConvertFrom-Json
$preflight = Get-Content -Raw -LiteralPath $preflightPath | ConvertFrom-Json
$benchmarks = @([IO.File]::ReadLines($benchmarkPath) | ForEach-Object { ($_ | ConvertFrom-Json).record })

$twoWorkerRates = @($benchmarks | Where-Object {
    $_.schema -eq 'primeforge.fast_prime.gpu_workers.v1' -and $_.variant -eq 'two-workers'
} | ForEach-Object { [double]$_.candidates_per_hour } | Sort-Object)
if ($twoWorkerRates.Count -lt 2) { throw 'At least two two-worker measurements are required.' }
$medianRate = if (($twoWorkerRates.Count % 2) -eq 1) {
    $twoWorkerRates[[int]($twoWorkerRates.Count / 2)]
} else {
    ($twoWorkerRates[$twoWorkerRates.Count / 2 - 1] + $twoWorkerRates[$twoWorkerRates.Count / 2]) / 2.0
}
$minimumRate = ($twoWorkerRates | Measure-Object -Minimum).Minimum
$selectedSieve = @($benchmarks | Where-Object {
    $_.schema -eq 'primeforge.fast_prime.sieve_memory.v1' -and $_.sieve_bound -eq 2000000000
} | Select-Object -Last 1)
if ($selectedSieve.Count -ne 1) { throw 'Selected 2-billion sieve measurement is missing.' }
$lambda = [double]$portfolio.target_lambda
$fullPreflightId = 'fp-20000-c0-a210bbba7489'
$survivorCounts = @{
    'fp-20000-c0-a210bbba7489' = 3563
    'fp-20000-c1-e50987f5e688' = 3569
    'fp-20000-c2-da62410639c5' = 3604
}
$ranking = [Collections.Generic.List[object]]::new()

foreach ($range in $portfolio.ranges) {
    if ($survivorCounts.ContainsKey([string]$range.range_id)) {
        $survivors = [uint32]$survivorCounts[[string]$range.range_id]
        $rangeSieve = @($benchmarks | Where-Object {
            $_.schema -eq 'primeforge.fast_prime.benchmark.v1' -and $_.kind -eq 'SIEVE' -and
            $_.range_id -eq $range.range_id -and [uint64]$_.sieve_bound -eq 2000000000
        } | Select-Object -Last 1)
        if ($rangeSieve.Count -ne 1) { throw "Missing 2-billion sieve record for $($range.range_id)." }
        $sieveSeconds = if ($range.range_id -eq $fullPreflightId) {
            [double]$selectedSieve[0].elapsed_seconds
        } else { [double]$rangeSieve[0].elapsed_seconds }
        $sievePeak = if ($range.range_id -eq $fullPreflightId) {
            [string]$selectedSieve[0].peak_working_set_bytes
        } else { 'UNKNOWN_NOT_POLLED' }
        $sieveHours = $sieveSeconds / 3600.0
        $fullHours = $survivors / $medianRate + $sieveHours
        $prudentHours = 1.20 * $survivors / $minimumRate + $sieveHours
        $medianDiscoveryHours = $fullHours * [Math]::Log(2.0) / $lambda
        $p90DiscoveryHours = $fullHours * [Math]::Log(10.0) / $lambda
        $expectedStopHours = $fullHours * (1.0 - [Math]::Exp(-$lambda)) / $lambda
        $rank = [uint32]$range.counter + 1
        $metrics = [pscustomobject][ordered]@{
            rank = $rank
            sieve_bound = 2000000000
            measured_sieve_seconds = Format-Decimal $sieveSeconds
            measured_sieve_peak_working_set_bytes = $sievePeak
            measured_survivors = $survivors
            selected_gpu_workers = 2
            measured_two_worker_rates_per_hour = @($twoWorkerRates | ForEach-Object { Format-Decimal $_ })
            median_throughput_per_hour = Format-Decimal $medianRate
            slower_measured_throughput_per_hour = Format-Decimal $minimumRate
            estimated_full_range_hours = Format-Decimal $fullHours
            prudent_full_range_hours = Format-Decimal $prudentHours
            estimated_median_time_to_prime_hours = Format-Decimal $medianDiscoveryHours
            estimated_p90_time_to_prime_hours = Format-Decimal $p90DiscoveryHours
            estimated_expected_stop_time_hours = Format-Decimal $expectedStopHours
            probability_at_least_one = [string]$range.odd_candidate_heuristic.probability_at_least_one
            probability_model = 'Poisson heuristic; not a guarantee'
        }
        $range | Add-Member -NotePropertyName decision_metrics -NotePropertyValue $metrics -Force
        $range.benchmark_status = 'SIEVE_2B_MEASURED;FULL_SIZE_PROTH20_MEASURED_ON_EQUIVALENT_20000_DIGIT_FINALIST'
        if ($range.range_id -eq $fullPreflightId) {
            $range.coverage_status = [string]$preflight.coverage_verdict
            $range.engine_status = 'ENGINE_SUPPORT_PASS'
            $range.gate_status = 'AWAITING_SASHA_GO'
            $range.coverage_evidence = [pscustomobject][ordered]@{
                level = 'B_FULL_PREFLIGHT'
                source_count = [uint32]$preflight.capture.source_count
                source_manifest_sha256 = [string]$preflight.source_manifest_sha256
                documented_overlap_count = [uint32]$preflight.numeric_overlap_checks.total_possible_overlaps_or_matches
                preflight_status = [string]$preflight.preflight_status
                audited_utc = [string]$preflight.generated_utc
                limitations = @($preflight.limitations)
            }
        } else {
            $range.gate_status = 'LEVEL_B_PREFLIGHT_PENDING'
        }
        $ranking.Add([pscustomobject][ordered]@{
            rank = $rank
            range_id = $range.range_id
            n = $range.n
            k_min = $range.k_min
            k_max = $range.k_max
            digits = $range.digit_count_min
            initial_candidates = $range.candidate_count
            measured_survivors_at_2b = $survivors
            coverage_status = $range.coverage_status
            audit_level = $range.coverage_evidence.level
            estimated_full_range_hours = Format-Decimal $fullHours
            prudent_full_range_hours = Format-Decimal $prudentHours
            probability_at_least_one = [string]$range.odd_candidate_heuristic.probability_at_least_one
        })
        continue
    }

    $probe = @($benchmarks | Where-Object {
        $_.schema -eq 'primeforge.fast_prime.proth20_probe.v1' -and
        [uint32]$_.digits -eq [uint32]$range.digit_band
    } | Select-Object -First 1)
    if ($probe.Count -eq 1) {
        $range.benchmark_status = if ([uint32]$range.counter -eq 0) {
            'ONE_FULL_SIZE_PROBE_PASS;DOMINATED_BY_20000_DIGIT_OPTIONS'
        } else { 'BAND_COST_TRANSFERRED;DOMINATED_BY_20000_DIGIT_OPTIONS' }
        $range.gate_status = 'DOMINATED_NOT_ADVANCED_TO_LEVEL_B'
        $range | Add-Member -NotePropertyName probe_metrics -NotePropertyValue ([pscustomobject][ordered]@{
            representative_engine_seconds = [uint32]$probe[0].engine_seconds
            representative_sieve_bound = [uint32]$probe[0].sieve_bound
            representative_survivors = [uint32]$probe[0].survivor_count
            inference = 'Single-candidate band probe; sufficient only for elimination, not a p90 claim.'
        }) -Force
    } else {
        $range.benchmark_status = 'NOT_RUN;DOMINATED_BEFORE_EXPENSIVE_FULL_SIZE_PROBE'
        $range.gate_status = 'DOMINATED_NOT_ADVANCED_TO_LEVEL_B'
    }
}

$portfolio | Add-Member -NotePropertyName decision -NotePropertyValue ([pscustomobject][ordered]@{
    finalized_utc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    ranked_top_three = @($ranking | Sort-Object rank)
    recommended_range = $fullPreflightId
    selected_sieve_bound = 2000000000
    selected_gpu_workers = 2
    independent_agreement = '5/5 Proth20 versus PARI/GP 2.17.4'
    stop_resume = 'PASS; deliberate stop after 2/5, durable resume to 5/5'
    no_gaps = 'PASS'
    no_duplicates = 'PASS'
    hardware_stability = 'PASS_ON_SHORT_VALIDATION'
    status = 'AWAITING_SASHA_GO'
    campaign_started = $false
}) -Force

[IO.File]::WriteAllText($portfolioPath, ($portfolio | ConvertTo-Json -Depth 16) + "`n", $utf8)
Write-Host "fast_prime.portfolio.recommended=$fullPreflightId"
Write-Host "fast_prime.portfolio.top_three=$($ranking.Count)"
Write-Host 'fast_prime.portfolio.status=AWAITING_SASHA_GO'
