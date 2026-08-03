[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [Parameter(Mandatory = $true)][string]$InputPath,
    [Parameter(Mandatory = $true)][string]$OutputPath,
    [ValidateRange(100, 100000)][int]$BootstrapSamples = 10000
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Get-NearestRank {
    param([uint64[]]$Values, [double]$Percentile)
    $ordered = @($Values | Sort-Object)
    $rank = [Math]::Max(1, [Math]::Ceiling($Percentile * $ordered.Count))
    return [uint64]$ordered[$rank - 1]
}

function Get-Median {
    param([uint64[]]$Values)
    $ordered = @($Values | Sort-Object)
    if (($ordered.Count % 2) -eq 1) { return [uint64]$ordered[[int]($ordered.Count / 2)] }
    return [uint64](([decimal]$ordered[$ordered.Count / 2 - 1] +
                     [decimal]$ordered[$ordered.Count / 2]) / 2)
}

$inputItem = Get-Item -LiteralPath $InputPath
$files = if ($inputItem.PSIsContainer) {
    @(Get-ChildItem -LiteralPath $inputItem.FullName -Recurse -File -Filter '*.jsonl' | Sort-Object FullName)
} else { @($inputItem) }
$rows = @()
foreach ($file in $files) {
    foreach ($line in Get-Content -LiteralPath $file.FullName -Encoding UTF8) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        $row = $line | ConvertFrom-Json
        if ($row.schema -ne 'primeforge.benchmark.raw.v1') {
            throw "Unsupported raw schema in $($file.FullName)"
        }
        if ($row.valid_measurement -eq $true) { $rows += $row }
    }
}
if ($rows.Count -eq 0) { throw 'No valid benchmark rows found.' }

$random = [System.Random]::new(1347569997)
$summaries = @()
$groups = $rows | Group-Object { "$($_.profile_id)|$($_.backend)|$($_.bits)|$($_.batch_size)" }
foreach ($group in $groups | Sort-Object Name) {
    $values = [uint64[]]@($group.Group | ForEach-Object { [uint64]$_.total_ns })
    $median = Get-Median $values
    $deviations = [uint64[]]@($values | ForEach-Object {
        if ($_ -ge $median) { $_ - $median } else { $median - $_ }
    })
    $mean = [double](($values | Measure-Object -Average).Average)
    $sumSquares = [double]0
    foreach ($value in $values) { $sumSquares += ([double]$value - $mean) * ([double]$value - $mean) }
    $standardDeviation = [Math]::Sqrt($sumSquares / $values.Count)
    $bootstrap = [uint64[]]::new($BootstrapSamples)
    for ($sample = 0; $sample -lt $BootstrapSamples; $sample++) {
        $resample = [uint64[]]::new($values.Count)
        for ($index = 0; $index -lt $values.Count; $index++) {
            $resample[$index] = $values[$random.Next(0, $values.Count)]
        }
        $bootstrap[$sample] = Get-Median $resample
    }
    $parts = $group.Name.Split('|')
    $hashes = @($group.Group.result_sha256 | Sort-Object -Unique)
    if ($hashes.Count -ne 1) { throw "Result divergence in $($group.Name)" }
    $summaries += [pscustomobject][ordered]@{
        profile_id = $parts[0]
        backend = $parts[1]
        bits = [uint64]$parts[2]
        batch_size = [uint64]$parts[3]
        result_sha256 = $hashes[0]
        repetitions = $values.Count
        minimum_ns = [uint64](($values | Measure-Object -Minimum).Minimum)
        maximum_ns = [uint64](($values | Measure-Object -Maximum).Maximum)
        median_ns = $median
        mad_ns = Get-Median $deviations
        p5_ns = Get-NearestRank $values 0.05
        p95_ns = Get-NearestRank $values 0.95
        mean_ns = [Math]::Round($mean, 3)
        coefficient_of_variation = if ($mean -eq 0) { 0 } else { [Math]::Round($standardDeviation / $mean, 6) }
        bootstrap_median_low_ns = Get-NearestRank $bootstrap 0.025
        bootstrap_median_high_ns = Get-NearestRank $bootstrap 0.975
    }
}

New-Item -ItemType Directory -Path $OutputPath -Force | Out-Null
$summaries | Export-Csv -LiteralPath (Join-Path $OutputPath 'summary.csv') -NoTypeInformation -Encoding UTF8
$document = [ordered]@{ schema = 'primeforge.benchmark.summary.v1'; rows = $summaries }
[System.IO.File]::WriteAllText(
    (Join-Path $OutputPath 'summary.json'),
    ($document | ConvertTo-Json -Depth 5) + "`n",
    [System.Text.UTF8Encoding]::new($false)
)
Write-Host "PrimeForge benchmark summary: PASS ($($rows.Count) rows, $(@($groups).Count) groups)"
