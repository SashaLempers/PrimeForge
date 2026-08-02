[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [string]$EvidenceDirectory = 'benchmarks\evidence\stage5'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$evidenceRoot = if ([System.IO.Path]::IsPathRooted($EvidenceDirectory)) {
    [System.IO.Path]::GetFullPath($EvidenceDirectory)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $EvidenceDirectory))
}

function Read-StrictJson([string]$Path) {
    $bytes = [System.IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -eq 0) { throw "Empty JSON file: $Path" }
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        throw "JSON must not have a BOM: $Path"
    }
    if ($bytes[$bytes.Length - 1] -eq 0x0A -or $bytes[$bytes.Length - 1] -eq 0x0D) {
        throw "JSON must not have a final newline: $Path"
    }
    $text = [System.Text.Encoding]::UTF8.GetString($bytes)
    return $text | ConvertFrom-Json
}

function Get-LowerMedian([uint64[]]$Values) {
    $sorted = @($Values | Sort-Object)
    if ($sorted.Count -eq 0) { throw 'Cannot summarize an empty sample.' }
    return [uint64]$sorted[[int](($sorted.Count - 1) / 2)]
}

$environment = Read-StrictJson (Join-Path $evidenceRoot 'environment.json')
$compatibility = Read-StrictJson (Join-Path $evidenceRoot 'compatibility.json')
if ($environment.repository_dirty -ne 'NO') { throw 'Evidence was not generated from a clean worktree.' }
if ($environment.power_measurement -eq 'UNKNOWN' -and $environment.wall_energy_millijoules -ne 'UNKNOWN') {
    throw 'Unknown power cannot carry a wall-energy value.'
}
if ($compatibility.compatible -ne 'YES' -or $compatibility.performance_claim -ne 'NONE') {
    throw 'Compatibility evidence is not claim-safe.'
}

foreach ($runId in @('stage5-a', 'stage5-b')) {
    $rawCsvPath = Join-Path $evidenceRoot "$runId-raw.csv"
    $rawJsonPath = Join-Path $evidenceRoot "$runId-raw.json"
    $summaryPath = Join-Path $evidenceRoot "$runId-summary.csv"
    $raw = @(Import-Csv -LiteralPath $rawCsvPath)
    $rawJson = Read-StrictJson $rawJsonPath
    $summary = @(Import-Csv -LiteralPath $summaryPath)
    [void](Read-StrictJson (Join-Path $evidenceRoot "$runId-summary.json"))

    if ($raw.Count -ne $rawJson.measurements.Count) { throw "$runId CSV/JSON row-count mismatch." }
    if (@($raw.candidate_set_sha256 | Sort-Object -Unique).Count -ne 1) { throw "$runId candidate hashes differ." }
    if ($raw[0].candidate_set_sha256 -ne $compatibility.candidate_set_sha256) { throw "$runId candidate hash mismatch." }

    foreach ($row in $raw) {
        $sum = [uint64]$row.transfer_ns + [uint64]$row.kernel_ns + [uint64]$row.proof_ns + [uint64]$row.io_ns
        if ($sum -ne [uint64]$row.total_ns) { throw "$runId phase sum mismatch at order $($row.order_index)." }
        if ($row.valid_for_performance -ne 'NO' -or $row.invalid_reason -ne 'TELEMETRY_UNAVAILABLE') {
            throw "$runId contains an unjustified performance-valid row."
        }
    }

    foreach ($expected in $summary) {
        $rows = @($raw | Where-Object { $_.variant -eq $expected.variant })
        if ($rows.Count -lt 7) { throw "$runId has fewer than seven repetitions for $($expected.variant)." }
        $totals = [uint64[]]@($rows | ForEach-Object { [uint64]$_.total_ns })
        $median = Get-LowerMedian $totals
        $deviations = [uint64[]]@($totals | ForEach-Object { if ($_ -ge $median) { $_ - $median } else { $median - $_ } })
        $mad = Get-LowerMedian $deviations
        $minimum = [uint64]($totals | Measure-Object -Minimum).Minimum
        $maximum = [uint64]($totals | Measure-Object -Maximum).Maximum
        if ([uint64]$expected.median_ns -ne $median -or [uint64]$expected.mad_ns -ne $mad -or
            [uint64]$expected.min_ns -ne $minimum -or [uint64]$expected.max_ns -ne $maximum -or
            [uint64]$expected.confidence_low_ns -ne $minimum -or [uint64]$expected.confidence_high_ns -ne $maximum) {
            throw "$runId summary cannot be recalculated for $($expected.variant)."
        }
    }
}

Write-Host 'PrimeForge benchmark evidence verification PASS: raw data reproduces every summary; claims NONE.'
