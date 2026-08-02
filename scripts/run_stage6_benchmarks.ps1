[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [string]$OutputDirectory = 'benchmarks\raw\stage6',
    [ValidateRange(7, 99)][int]$Repetitions = 7,
    [ValidateRange(1, 64)][int]$Threads = 4
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot
$primeforge = Join-Path $root 'out\build\msvc-release\primeforge-sieve.exe'
$primesieve = Join-Path $root 'out\audit_builds\primesieve-release\primesieve.exe'
foreach ($tool in @($primeforge, $primesieve)) {
    if (-not (Test-Path -LiteralPath $tool -PathType Leaf)) {
        throw "Required benchmark executable is absent: $tool"
    }
}

$output = if ([System.IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory
} else {
    Join-Path $root $OutputDirectory
}
New-Item -ItemType Directory -Path $output -Force | Out-Null
$rawPath = Join-Path $output 'raw.tsv'
$environmentPath = Join-Path $output 'environment.tsv'

$variants = @(
    @{ Name = 'primeforge-default-1t'; Tool = 'primeforge'; Threads = 1; Extra = @() },
    @{ Name = 'primeforge-default-nt'; Tool = 'primeforge'; Threads = $Threads; Extra = @() },
    @{ Name = 'primeforge-wheel-off'; Tool = 'primeforge'; Threads = $Threads; Extra = @('--wheel30', 'no') },
    @{ Name = 'primeforge-byte'; Tool = 'primeforge'; Threads = $Threads; Extra = @('--bit-packed', 'no') },
    @{ Name = 'primeforge-bucket-off'; Tool = 'primeforge'; Threads = $Threads; Extra = @('--bucket', 'no') },
    @{ Name = 'primeforge-prefetch'; Tool = 'primeforge'; Threads = $Threads; Extra = @('--prefetch', 'yes') },
    @{ Name = 'primeforge-bucket-aos'; Tool = 'primeforge'; Threads = $Threads; Extra = @('--bucket-layout', 'aos') },
    @{ Name = 'primesieve-1t'; Tool = 'primesieve'; Threads = 1; Extra = @() },
    @{ Name = 'primesieve-nt'; Tool = 'primesieve'; Threads = $Threads; Extra = @() }
)
$ranges = @(
    @{ Name = 'zero-10m'; Begin = [uint64]0; End = [uint64]10000000 },
    @{ Name = '1e9-10m'; Begin = [uint64]1000000000; End = [uint64]1010000000 },
    @{ Name = '1e12-10m'; Begin = [uint64]1000000000000; End = [uint64]1000010000000 }
)

$commit = (& git -C $root rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve Git commit.' }
$dirty = if (@(& git -C $root status --porcelain).Count -eq 0) { 'NO' } else { 'YES' }
$environmentLines = @(
    "key`tvalue",
    "schema_version`t1",
    "commit`t$commit",
    "dirty`t$dirty",
    "repetitions`t$Repetitions",
    "thread_parameter`t$Threads",
    "clock`tSystem.Diagnostics.Stopwatch",
    "timed_region`tchild process start through exit; executable startup included",
    "temperature_celsius`tUNKNOWN",
    "frequency_hz`tUNKNOWN",
    "electrical_power_watts`tUNKNOWN",
    "wall_energy_joules`tUNKNOWN",
    "ambient_temperature_celsius`tUNKNOWN",
    "performance_claim`tNONE"
)
[System.IO.File]::WriteAllLines($environmentPath, $environmentLines, [System.Text.UTF8Encoding]::new($false))

$rawLines = [System.Collections.Generic.List[string]]::new()
$rawLines.Add("schema_version`trange`tvariant`trepetition`torder`tbegin`tend`tcount`telapsed_nanoseconds`tperformance_valid")
$random = [System.Random]::new(20260802)
foreach ($range in $ranges) {
    foreach ($variant in $variants) {
        if ($variant.Tool -eq 'primeforge') {
            & $primeforge --begin "$($range.Begin)" --end "$($range.End)" --threads "$($variant.Threads)" @($variant.Extra) | Out-Null
        } else {
            & $primesieve "$($range.Begin)" "$($range.End - 1)" --count --quiet "--threads=$($variant.Threads)" | Out-Null
        }
        if ($LASTEXITCODE -ne 0) { throw "Warmup failed: $($variant.Name)" }
    }
    for ($repetition = 1; $repetition -le $Repetitions; ++$repetition) {
        $order = @($variants | Sort-Object { $random.Next() })
        for ($orderIndex = 0; $orderIndex -lt $order.Count; ++$orderIndex) {
            $variant = $order[$orderIndex]
            $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
            if ($variant.Tool -eq 'primeforge') {
                $lines = @(& $primeforge --begin "$($range.Begin)" --end "$($range.End)" --threads "$($variant.Threads)" @($variant.Extra))
                $countLine = @($lines | Where-Object { $_ -match '^count=([0-9]+)$' })
                if ($countLine.Count -ne 1) { throw "Missing count: $($variant.Name)" }
                $count = [uint64]($countLine[0].Substring(6))
            } else {
                $lines = @(& $primesieve "$($range.Begin)" "$($range.End - 1)" --count --quiet "--threads=$($variant.Threads)")
                $count = [uint64]($lines[-1].Trim())
            }
            $stopwatch.Stop()
            if ($LASTEXITCODE -ne 0) { throw "Benchmark failed: $($variant.Name)" }
            $nanoseconds = [uint64][Math]::Round($stopwatch.ElapsedTicks * (1000000000.0 / [System.Diagnostics.Stopwatch]::Frequency))
            $rawLines.Add("1`t$($range.Name)`t$($variant.Name)`t$repetition`t$($orderIndex + 1)`t$($range.Begin)`t$($range.End)`t$count`t$nanoseconds`tNO")
        }
    }
}
[System.IO.File]::WriteAllLines($rawPath, $rawLines, [System.Text.UTF8Encoding]::new($false))
$records = @(Import-Csv -LiteralPath $rawPath -Delimiter "`t")
$summaryLines = [System.Collections.Generic.List[string]]::new()
$summaryLines.Add("schema_version`trange`tvariant`tsamples`tcount`tminimum_nanoseconds`tmedian_nanoseconds`tmaximum_nanoseconds`tmedian_absolute_deviation_nanoseconds`tperformance_valid`tperformance_claim")
foreach ($range in $ranges) {
    $rangeRecords = @($records | Where-Object { $_.range -eq $range.Name })
    $rangeCounts = @($rangeRecords | ForEach-Object { $_.count } | Sort-Object -Unique)
    if ($rangeCounts.Count -ne 1) {
        throw "Count disagreement in benchmark range $($range.Name): $($rangeCounts -join ',')"
    }
    foreach ($variant in $variants) {
        $group = @($rangeRecords | Where-Object { $_.variant -eq $variant.Name })
        if ($group.Count -ne $Repetitions) {
            throw "Incomplete sample group: $($range.Name) / $($variant.Name)"
        }
        $durations = @([uint64[]]$group.elapsed_nanoseconds | Sort-Object)
        $median = $durations[[int][Math]::Floor($durations.Count / 2)]
        $deviations = @($durations | ForEach-Object {
            if ($_ -ge $median) { [uint64]($_ - $median) } else { [uint64]($median - $_) }
        } | Sort-Object)
        $mad = $deviations[[int][Math]::Floor($deviations.Count / 2)]
        $summaryLines.Add("1`t$($range.Name)`t$($variant.Name)`t$($group.Count)`t$($rangeCounts[0])`t$($durations[0])`t$median`t$($durations[-1])`t$mad`tNO`tNONE")
    }
}
$summaryPath = Join-Path $output 'summary.tsv'
[System.IO.File]::WriteAllLines($summaryPath, $summaryLines, [System.Text.UTF8Encoding]::new($false))
Write-Host "Stage 6 benchmark collection: PASS ($($records.Count) samples; counts agree; performance claim NONE; output $output)"
