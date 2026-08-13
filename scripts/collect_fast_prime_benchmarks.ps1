[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [string]$OutputFile = 'benchmarks\fast-prime\raw.jsonl'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$utf8 = [Text.UTF8Encoding]::new($false)
$records = [Collections.Generic.List[string]]::new()

function Resolve-ProjectPath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $root $Path))
}

function Add-JsonRecord([object]$Record, [string]$Artifact) {
    $envelope = [pscustomobject][ordered]@{
        artifact = $Artifact.Replace('\', '/')
        record = $Record
    }
    $script:records.Add(($envelope | ConvertTo-Json -Depth 12 -Compress))
}

function Add-JsonLines([string]$RelativePath) {
    $path = Resolve-ProjectPath $RelativePath
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing benchmark artifact: $path" }
    foreach ($line in [IO.File]::ReadLines($path)) {
        if (-not [string]::IsNullOrWhiteSpace($line)) { Add-JsonRecord ($line | ConvertFrom-Json) $RelativePath }
    }
}

$jsonLineArtifacts = @(
    'out\fast-prime\sieve-benchmark-20260813T1044Z\raw.jsonl',
    'out\fast-prime\sieve-benchmark-deep-100m-20260813\raw.jsonl',
    'out\fast-prime\sieve-benchmark-deep-1b-20260813\raw.jsonl',
    'out\fast-prime\sieve-benchmark-deep-2b-20260813\raw.jsonl',
    'out\fast-prime\sieve-benchmark-deep-4b-20260813\raw.jsonl',
    'out\fast-prime\sieve-top3-1b-20260813\raw.jsonl',
    'out\fast-prime\sieve-top3-2b-20260813\raw.jsonl',
    'out\fast-prime\gpu-worker-benchmark-20260813-r3\raw.jsonl',
    'out\fast-prime\gpu-worker-benchmark-20260813-higher\raw.jsonl'
)
foreach ($artifact in $jsonLineArtifacts) { Add-JsonLines $artifact }

$probeDirectories = @(
    'probe-campaign-20k-one-b1b',
    'probe-campaign-25k-one-b10m',
    'probe-campaign-50k-one-b10m',
    'probe-campaign-75k-one-b10m',
    'probe-campaign-100k-one',
    'probe-campaign-250k-one'
)
foreach ($name in $probeDirectories) {
    $relativeDirectory = "out\fast-prime\$name"
    $directory = Resolve-ProjectPath $relativeDirectory
    $manifest = Get-Content -Raw -LiteralPath (Join-Path $directory 'campaign.json') | ConvertFrom-Json
    $checkpoint = Get-Content -Raw -LiteralPath (Join-Path $directory 'checkpoint.json') | ConvertFrom-Json
    $stdout = @(Get-ChildItem -LiteralPath (Join-Path $directory 'logs') -Filter '*.stdout.log' -File)
    if ($stdout.Count -ne 1) { throw "$name must contain one probe stdout log." }
    $text = Get-Content -Raw -LiteralPath $stdout[0].FullName
    $match = [regex]::Match($text,
        'Testing ([0-9]+) \* 2\^([0-9]+) \+ 1, ([0-9]+) digits[\s\S]*?time = ([0-9]{2}):([0-9]{2}):([0-9]{2}), RES64 = ([0-9A-F]+)')
    if (-not $match.Success) { throw "Cannot parse Proth20 probe $name." }
    $snapshots = @(
        Get-ChildItem -LiteralPath (Join-Path $directory 'logs') -Filter '*.telemetry.jsonl' -File |
            ForEach-Object { [IO.File]::ReadLines($_.FullName) } |
            ForEach-Object {
                $event = $_ | ConvertFrom-Json
                if ($event.event_type -eq 'telemetry') { $event.payload.snapshot }
            }
    )
    if ($snapshots.Count -eq 0) { throw "No probe telemetry for $name." }
    $record = [pscustomobject][ordered]@{
        schema = 'primeforge.fast_prime.proth20_probe.v1'
        kind = 'PROTH20_FULL_SIZE_PROBE'
        probe = $name
        campaign_id = $manifest.campaign_id
        k = $match.Groups[1].Value
        n = [uint32]$match.Groups[2].Value
        digits = [uint32]$match.Groups[3].Value
        engine_seconds = [uint32]$match.Groups[4].Value * 3600 +
            [uint32]$match.Groups[5].Value * 60 + [uint32]$match.Groups[6].Value
        res64 = $match.Groups[7].Value
        sieve_bound = [uint32]$manifest.sieve_bound
        survivor_count = @([IO.File]::ReadLines((Join-Path $directory 'survivors.txt'))).Count
        max_cpu_temperature_c = [double](($snapshots.cpu_temperature_celsius.value | Measure-Object -Maximum).Maximum)
        max_gpu_temperature_c = [double](($snapshots.gpu_temperature_celsius.value | Measure-Object -Maximum).Maximum)
        max_gpu_power_w = [double](($snapshots.gpu_power_watts.value | Measure-Object -Maximum).Maximum)
        max_gpu_utilization_percent = [double](($snapshots.gpu_utilization_percent.value | Measure-Object -Maximum).Maximum)
        min_ram_available_bytes = [uint64](($snapshots.ram_available_bytes.value | Measure-Object -Minimum).Minimum)
        min_vram_free_mib = [uint64](($snapshots.vram_free_mib.value | Measure-Object -Minimum).Minimum)
        max_recent_whea_errors = [uint64](($snapshots.whea_errors_recent.value | Measure-Object -Maximum).Maximum)
        throttling_detected = @($snapshots | Where-Object throttling_detected).Count -ne 0
        campaign_status = $checkpoint.campaign_status
        proth20_sha256 = $manifest.proth20_sha256
        status = 'PASS'
    }
    Add-JsonRecord $record $relativeDirectory
}

foreach ($artifact in @(
    'out\fast-prime\stop-resume-validation-20260813-r4-final.json',
    'out\fast-prime\pari-timing-20260813.json',
    'out\fast-prime\sieve-2b-memory-probe-final.json'
)) {
    $record = Get-Content -Raw -LiteralPath (Resolve-ProjectPath $artifact) | ConvertFrom-Json
    Add-JsonRecord $record $artifact
}

$outputPath = Resolve-ProjectPath $OutputFile
$outputDirectory = Split-Path -Parent $outputPath
New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
[IO.File]::WriteAllLines($outputPath, $records, $utf8)
Write-Host "fast_prime.collect.records=$($records.Count)"
Write-Host "fast_prime.collect.output=$outputPath"
Write-Host 'fast_prime.collect.status=PASS'
