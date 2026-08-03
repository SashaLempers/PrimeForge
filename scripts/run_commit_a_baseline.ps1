[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [string]$OutputDirectory = 'out\benchmarks\commit-a',
    [ValidateRange(1, 100)][int]$Warmup = 3,
    [ValidateRange(7, 100)][int]$Repetitions = 7
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$root = Split-Path -Parent $PSScriptRoot
$output = [System.IO.Path]::GetFullPath((Join-Path $root $OutputDirectory))
$rootPrefix = [System.IO.Path]::GetFullPath($root).TrimEnd('\') + '\'
if (-not $output.StartsWith($rootPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Output directory must stay inside the PrimeForge repository: $output"
}
$build = Join-Path $root 'out\build\msvc-cuda-release'
$bench = Join-Path $build 'primeforge-bench.exe'
$primeforge = Join-Path $build 'primeforge.exe'
$profile = Join-Path $root 'benchmarks\profiles\s64_prp_65536.json'
$template = Join-Path $root 'benchmarks\pivot10\known_proth_small.yaml'
foreach ($required in @($bench, $primeforge, $profile, $template)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required commit-A input is absent: $required"
    }
}

New-Item -ItemType Directory -Path $output -Force | Out-Null
$raw = Join-Path $output 'raw'
New-Item -ItemType Directory -Path $raw -Force | Out-Null

function Invoke-CheckedCapture {
    param([string]$Executable, [string[]]$Arguments)
    Write-Host "> $Executable $($Arguments -join ' ')"
    $lines = @(& $Executable @Arguments 2>&1)
    $exit = $LASTEXITCODE
    $lines | ForEach-Object { Write-Host $_ }
    if ($exit -ne 0) { throw "Command failed with exit code ${exit}: $Executable" }
    return $lines
}

foreach ($backend in @('cpu', 'cuda', 'auto')) {
    $backendOutput = Join-Path $raw "prp-$backend"
    if (Test-Path -LiteralPath $backendOutput) {
        Remove-Item -LiteralPath $backendOutput -Recurse -Force
    }
    Invoke-CheckedCapture $bench @(
        'run', '--profile', $profile, '--backend', $backend,
        '--output', $backendOutput, '--warmup', $Warmup,
        '--repetitions', $Repetitions
    ) | Out-Null
}

$campaignRelative = 'out/benchmarks/commit-a/current-campaign'
$campaign = Join-Path $root ($campaignRelative -replace '/', '\')
$config = Join-Path $output 'full-u64-known-160.yaml'
$configText = Get-Content -LiteralPath $template -Raw
$configText = [regex]::Replace(
    $configText,
    '(?m)^  directory:.*$',
    "  directory: `"$campaignRelative`""
)
[System.IO.File]::WriteAllText($config, $configText, [System.Text.UTF8Encoding]::new($false))

$commit = (& git -C $root rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $commit.Length -ne 40) { throw 'Cannot resolve commit SHA.' }
$binarySha = (Get-FileHash -LiteralPath $primeforge -Algorithm SHA256).Hash.ToLowerInvariant()
$profileSha = (Get-FileHash -LiteralPath $config -Algorithm SHA256).Hash.ToLowerInvariant()
$pipelineRaw = Join-Path $raw 'pipeline.jsonl'
[System.IO.File]::WriteAllText($pipelineRaw, '', [System.Text.UTF8Encoding]::new($false))
$generator = [System.Random]::new(20260804)
$expectedByBackend = @{}

for ($round = -$Warmup; $round -lt $Repetitions; $round++) {
    $order = [System.Collections.Generic.List[string]]::new()
    @('cpu', 'cuda', 'auto') | ForEach-Object { $order.Add($_) }
    for ($index = $order.Count - 1; $index -gt 0; $index--) {
        $selected = $generator.Next(0, $index + 1)
        $temporary = $order[$index]
        $order[$index] = $order[$selected]
        $order[$selected] = $temporary
    }
    foreach ($backend in $order) {
        if (Test-Path -LiteralPath $campaign) {
            Remove-Item -LiteralPath $campaign -Recurse -Force
        }
        $lines = Invoke-CheckedCapture $primeforge @(
            'search', '--config', $config, '--prp-backend', $backend,
            '--prp-batch-candidates', '8192'
        )
        $fields = @{}
        foreach ($line in $lines) {
            $text = [string]$line
            $separator = $text.IndexOf('=')
            if ($separator -gt 0) {
                $fields[$text.Substring(0, $separator)] = $text.Substring($separator + 1)
            }
        }
        if ($fields['search.status'] -ne 'PASS') { throw "Pipeline did not pass for $backend" }
        $results = Join-Path $campaign 'results.jsonl'
        $resultSha = (Get-FileHash -LiteralPath $results -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($expectedByBackend.ContainsKey($backend) -and $expectedByBackend[$backend] -ne $resultSha) {
            throw "Pipeline result diverged for backend $backend"
        }
        $expectedByBackend[$backend] = $resultSha
        if ($round -lt 0) { continue }

        $row = [ordered]@{
            backend = $fields['search.prp_backend']
            batch_size = 160
            binary_sha256 = $binarySha
            bits = 64
            checkpoint_ns = [uint64]$fields['metrics.checkpoint_ns']
            commit_sha = $commit
            compiler = 'MSVC-195136252'
            compiler_flags = 'C++23_RELEASE_CUDA_TARGET'
            congruence_ns = [uint64]$fields['metrics.congruence_ns']
            cuda_driver = '610.74'
            cuda_runtime = '13.3'
            d2h_ns = [uint64]$fields['metrics.d2h_ns']
            dataset_sha256 = $profileSha
            generation_ns = [uint64]$fields['metrics.generation_ns']
            gpu_power_mean = 'UNKNOWN'
            gpu_temperature_max = 'UNKNOWN'
            gpu_utilization_mean = 'UNKNOWN'
            h2d_ns = [uint64]$fields['metrics.h2d_ns']
            io_ns = [uint64]$fields['metrics.io_ns']
            kernel_ns = [uint64]$fields['metrics.kernel_ns']
            packing_ns = [uint64]$fields['metrics.packing_ns']
            profile_id = 'FULL_U64_KNOWN_160'
            profile_sha256 = $profileSha
            proof_ns = [uint64]$fields['metrics.proof_ns']
            prp_cpu_ns = [uint64]$fields['metrics.prp_cpu_ns']
            repetition = [uint64]$round
            result_sha256 = $resultSha
            schema = 'primeforge.benchmark.raw.v1'
            sieve_ns = [uint64]$fields['metrics.sieve_ns']
            timestamp_utc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
            total_ns = [uint64]$fields['metrics.total_ns']
            valid_measurement = $true
            verification_ns = [uint64]$fields['metrics.verification_ns']
        }
        [System.IO.File]::AppendAllText(
            $pipelineRaw,
            ($row | ConvertTo-Json -Compress) + "`n",
            [System.Text.UTF8Encoding]::new($false)
        )
    }
}

$summary = Join-Path $output 'summary'
$report = Join-Path $output 'report'
& (Join-Path $root 'benchmarks\scripts\summarize.ps1') `
    -InputPath $raw -OutputPath $summary -BootstrapSamples 10000
& (Join-Path $root 'benchmarks\scripts\report.ps1') `
    -RawPath $raw -OutputPath $report

Write-Host "PrimeForge commit-A baseline: PASS ($Repetitions repetitions per backend; $output)"
