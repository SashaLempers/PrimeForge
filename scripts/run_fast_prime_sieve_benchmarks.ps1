[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [string]$PortfolioFile = 'docs\reports\CANDIDATE_RANGE_PORTFOLIO.json',
    [string]$OutputDirectory = 'out\fast-prime\sieve-benchmark',
    [string]$SieveExecutable = 'out\build\msvc-release\primeforge-discovery-sieve.exe',
    [uint32[]]$Bounds = @(1000000, 10000000),
    [uint32[]]$DigitBands = @(),
    [uint32[]]$Counters = @(0)
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$utf8 = [Text.UTF8Encoding]::new($false)
$invariant = [Globalization.CultureInfo]::InvariantCulture

function Resolve-ProjectPath {
    param([Parameter(Mandatory)][string]$Path)
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $repositoryRoot $Path))
}

$portfolioPath = Resolve-ProjectPath $PortfolioFile
$outputPath = Resolve-ProjectPath $OutputDirectory
$sieve = Resolve-ProjectPath $SieveExecutable
$monitor = Join-Path $repositoryRoot 'out\build\msvc-release\hardware_monitor.exe'
foreach ($file in @($portfolioPath, $sieve, $monitor)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) { throw "Required file missing: $file" }
}
if (Test-Path -LiteralPath $outputPath) {
    if (@(Get-ChildItem -LiteralPath $outputPath -Force).Count -ne 0) {
        throw "Benchmark output directory must be new or empty: $outputPath"
    }
} else {
    New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
}

$portfolio = Get-Content -Raw -LiteralPath $portfolioPath | ConvertFrom-Json
$allCounterZero = @($portfolio.ranges | Where-Object counter -eq 0)
if ($allCounterZero.Count -lt 5) { throw 'Portfolio does not contain the required counter-zero bands.' }
$selected = @($portfolio.ranges | Where-Object { $Counters -contains [uint32]$_.counter } |
    Sort-Object digit_band, counter)
if ($DigitBands.Count -ne 0) {
    $selected = @($selected | Where-Object { $DigitBands -contains [uint32]$_.digit_band })
}
if ($selected.Count -eq 0) { throw 'No portfolio range matches the requested digit bands.' }

$before = @(& $monitor --once)
if ($LASTEXITCODE -ne 0 -or $before.Count -ne 1) { throw 'Pre-benchmark hardware snapshot failed.' }
[IO.File]::WriteAllText((Join-Path $outputPath 'hardware-before.json'), $before[0] + "`n", $utf8)

# Warm the executable, allocator and filesystem without recording a performance claim.
$warmRange = $selected[0]
$warmOutput = Join-Path $outputPath 'warmup-survivors.txt'
& $sieve --k-start $warmRange.k_min --k-stop $warmRange.k_max --n $warmRange.n `
    --sieve-bound $Bounds[0] --output $warmOutput | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Sieve warmup failed.' }

$records = [Collections.Generic.List[string]]::new()
foreach ($range in $selected) {
    foreach ($bound in $Bounds) {
        $stem = "$($range.range_id)-b$bound"
        $survivors = Join-Path $outputPath ($stem + '-survivors.txt')
        $stdout = Join-Path $outputPath ($stem + '.stdout.log')
        $stderr = Join-Path $outputPath ($stem + '.stderr.log')
        $arguments = @(
            '--k-start', [string]$range.k_min,
            '--k-stop', [string]$range.k_max,
            '--n', [string]$range.n,
            '--sieve-bound', [string]$bound,
            '--output', $survivors
        )
        $timer = [Diagnostics.Stopwatch]::StartNew()
        $stdoutLines = @(& $sieve @arguments 2> $stderr)
        $exitCode = $LASTEXITCODE
        $timer.Stop()
        [IO.File]::WriteAllLines($stdout, @($stdoutLines | ForEach-Object { [string]$_ }), $utf8)
        if ($exitCode -ne 0) {
            throw "Sieve benchmark failed for $($range.range_id), bound $bound."
        }
        $values = @{}
        foreach ($line in [IO.File]::ReadLines($stdout)) {
            if ($line -match '^([^=]+)=(.*)$') { $values[$Matches[1]] = $Matches[2] }
        }
        foreach ($required in @(
            'discovery.sieve.candidates', 'discovery.sieve.eliminated',
            'discovery.sieve.survivors', 'discovery.sieve.primes_applied',
            'discovery.sieve.sha256', 'discovery.sieve.status'
        )) {
            if (-not $values.ContainsKey($required)) { throw "Missing sieve metric $required" }
        }
        if ($values['discovery.sieve.status'] -ne 'PASS') { throw 'Sieve did not report PASS.' }

        $record = [pscustomobject][ordered]@{
            schema = 'primeforge.fast_prime.benchmark.v1'
            recorded_utc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
            kind = 'SIEVE'
            range_id = $range.range_id
            digit_band = [int]$range.digit_band
            n = [uint32]$range.n
            k_min = [string]$range.k_min
            k_max = [string]$range.k_max
            candidate_count = [string]$values['discovery.sieve.candidates']
            sieve_bound = [uint32]$bound
            elapsed_seconds = $timer.Elapsed.TotalSeconds.ToString('0.000000000', $invariant)
            eliminated = [string]$values['discovery.sieve.eliminated']
            survivors = [string]$values['discovery.sieve.survivors']
            primes_applied = [string]$values['discovery.sieve.primes_applied']
            peak_working_set_bytes = 'UNKNOWN_SHORT_PROCESS'
            output_sha256 = [string]$values['discovery.sieve.sha256']
            stdout_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $stdout).Hash.ToLowerInvariant()
            stderr_bytes = [uint64](Get-Item -LiteralPath $stderr).Length
            executable_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $sieve).Hash.ToLowerInvariant()
            command = "$sieve $($arguments -join ' ')"
            status = 'PASS'
        }
        $records.Add(($record | ConvertTo-Json -Depth 6 -Compress))
        Write-Host "fast_prime.sieve.range=$($range.range_id) bound=$bound elapsed_s=$($record.elapsed_seconds) survivors=$($record.survivors)"
    }
}

$after = @(& $monitor --once)
if ($LASTEXITCODE -ne 0 -or $after.Count -ne 1) { throw 'Post-benchmark hardware snapshot failed.' }
[IO.File]::WriteAllText((Join-Path $outputPath 'hardware-after.json'), $after[0] + "`n", $utf8)
$rawPath = Join-Path $outputPath 'raw.jsonl'
[IO.File]::WriteAllLines($rawPath, $records, $utf8)

Write-Host "fast_prime.sieve.records=$($records.Count)"
Write-Host "fast_prime.sieve.raw=$rawPath"
Write-Host 'fast_prime.sieve.status=PASS'
