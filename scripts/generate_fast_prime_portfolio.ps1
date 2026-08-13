[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [string]$OutputFile = 'docs\reports\CANDIDATE_RANGE_PORTFOLIO.json',
    [string]$DateTag = '2026-08-13',
    [string]$BaseCommit = '',
    [ValidateRange(1, 16)][int]$CountersPerBand = 3,
    [ValidateRange(0.5, 0.999999)][double]$TargetProbability = 0.95,
    [uint64]$DomainMin = 10000001,
    [uint64]$DomainMax = 99999999
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$utf8 = [Text.UTF8Encoding]::new($false)
$invariant = [Globalization.CultureInfo]::InvariantCulture
$generatorVersion = 'primeforge.fast-prime-range.v1'
# The five mandated bands are retained. Four shorter bands are added because
# the first full-size measurements can make every mandated option exceed the
# preferred 24--72 hour window while a larger-than-current discovery remains
# realistic below 100,000 digits.
$bands = @(20000, 25000, 50000, 75000, 100000, 250000, 500000, 750000, 1000000)
$log10Two = [Math]::Log10(2.0)
$lnTen = [Math]::Log(10.0)

if ([string]::IsNullOrWhiteSpace($BaseCommit)) {
    $BaseCommit = (& git -C $repositoryRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve the portfolio base commit.' }
}
if ($BaseCommit -notmatch '^[0-9a-f]{40}$') {
    throw 'BaseCommit must be a full lowercase Git commit hash.'
}
if ($DomainMin -lt 3 -or $DomainMin -gt $DomainMax -or
    ($DomainMin -band 1) -eq 0 -or ($DomainMax -band 1) -eq 0 -or
    $DomainMax -ge 100000000) {
    throw 'The k domain must be an ordered odd interval inside the pinned Proth20 domain.'
}

function Get-Sha256Bytes {
    param([Parameter(Mandatory)][string]$Text)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return $sha.ComputeHash($script:utf8.GetBytes($Text)) }
    finally { $sha.Dispose() }
}

function Convert-BytesToHex {
    param([Parameter(Mandatory)][byte[]]$Bytes)
    return -join ($Bytes | ForEach-Object { $_.ToString('x2') })
}

function Convert-BigEndianU64 {
    param([Parameter(Mandatory)][byte[]]$Bytes, [Parameter(Mandatory)][int]$Offset)
    $slice = [byte[]]$Bytes[$Offset..($Offset + 7)]
    if ([BitConverter]::IsLittleEndian) { [Array]::Reverse($slice) }
    return [BitConverter]::ToUInt64($slice, 0)
}

function Get-DigitCount {
    param([Parameter(Mandatory)][uint64]$K, [Parameter(Mandatory)][uint32]$Exponent)
    $logValue = [Math]::Log10([double]$K) + [double]$Exponent * $script:log10Two
    $nearestInteger = [Math]::Round($logValue)
    if ([Math]::Abs($logValue - $nearestInteger) -lt 1.0e-9) {
        throw "Digit boundary is too close for a reliable binary64 decision: k=$K n=$Exponent"
    }
    return [int][Math]::Floor($logValue) + 1
}

$lambdaTarget = -[Math]::Log(1.0 - $TargetProbability)
$ranges = [Collections.Generic.List[object]]::new()

foreach ($digitBand in $bands) {
    # The top of the decimal band gives a conservative candidate count for
    # lambda = 2*C/ln(N), because every selected N has fewer than digitBand+1 digits.
    $candidateCount = [uint64][Math]::Ceiling(
        $lambdaTarget * ([double]$digitBand * $lnTen) / 2.0)
    $windowSpan = 2 * ($candidateCount - 1)
    if ($windowSpan -gt $DomainMax - $DomainMin) {
        throw "The $digitBand-digit window does not fit in the configured k domain."
    }
    $oddStartCount = (($DomainMax - $DomainMin - $windowSpan) / 2) + 1

    for ($counter = 0; $counter -lt $CountersPerBand; ++$counter) {
        $seed = 'PrimeForge|SashaLempers|FAST_PUBLICLY_UNCOVERED_PRIME|{0}|{1}|{2}|digits={3}|counter={4}|k-domain={5}..{6}|candidate-count={7}' -f `
            $DateTag, $BaseCommit, $generatorVersion, $digitBand, $counter,
            $DomainMin, $DomainMax, $candidateCount
        $digest = Get-Sha256Bytes $seed
        $seedHash = Convert-BytesToHex $digest
        $startSelector = Convert-BigEndianU64 $digest 0
        $nSelector = Convert-BigEndianU64 $digest 8
        $startIndex = $startSelector % $oddStartCount
        $kMin = $DomainMin + 2 * $startIndex
        $kMax = $kMin + $windowSpan

        # A valid n must put both ends of the complete k window in the exact
        # decimal band. The upper inequality is strict, hence ceil(x)-1.
        $nMin = [int64][Math]::Ceiling(
            (($digitBand - 1.0) - [Math]::Log10([double]$kMin)) / $log10Two)
        $nMax = [int64][Math]::Ceiling(
            ($digitBand - [Math]::Log10([double]$kMax)) / $log10Two) - 1
        if ($nMin -lt 32 -or $nMax -ge 100000000 -or $nMin -gt $nMax) {
            throw "No supported exponent keeps the complete $digitBand-digit window in band."
        }
        $nCount = [uint64]($nMax - $nMin + 1)
        $exponent = [uint32]($nMin + [int64]($nSelector % $nCount))
        $digitsMin = Get-DigitCount $kMin $exponent
        $digitsMax = Get-DigitCount $kMax $exponent
        if ($digitsMin -ne $digitBand -or $digitsMax -ne $digitBand) {
            throw "Exact digit validation failed for band $digitBand counter $counter."
        }

        $midpoint = ([double]$kMin + [double]$kMax) / 2.0
        $lnMidpointN = [Math]::Log($midpoint) + [double]$exponent * [Math]::Log(2.0)
        $lambda = [double]$candidateCount * 2.0 / $lnMidpointN
        $probability = 1.0 - [Math]::Exp(-$lambda)
        $canonical = @(
            "generator_version=$generatorVersion",
            "base_commit=$BaseCommit",
            "date_tag=$DateTag",
            "digit_band=$digitBand",
            "counter=$counter",
            "n=$exponent",
            "k_min=$kMin",
            "k_max=$kMax",
            'k_step=2',
            "candidate_count=$candidateCount",
            "seed_sha256=$seedHash"
        ) -join "`n"
        $generationHash = Convert-BytesToHex (Get-Sha256Bytes ($canonical + "`n"))

        $ranges.Add([pscustomobject][ordered]@{
            range_id = "fp-${digitBand}-c${counter}-$($generationHash.Substring(0, 12))"
            generator_version = $generatorVersion
            seed = $seed
            seed_sha256 = $seedHash
            counter = $counter
            base_commit = $BaseCommit
            digit_band = $digitBand
            n = $exponent
            k_min = $kMin.ToString($invariant)
            k_max = $kMax.ToString($invariant)
            k_step = 2
            candidate_count = $candidateCount.ToString($invariant)
            digit_count_min = $digitsMin
            digit_count_max = $digitsMax
            generation_hash = $generationHash
            odd_candidate_heuristic = [pscustomobject][ordered]@{
                model = 'lambda=2*candidate_count/ln(k_mid*2^n); P=1-exp(-lambda)'
                lambda = $lambda.ToString('0.000000000000000', $invariant)
                probability_at_least_one = $probability.ToString('0.000000000000000', $invariant)
                guarantee = 'HEURISTIC_NOT_GUARANTEED'
            }
            coverage_status = 'NOT_CHECKED'
            coverage_reasons = @()
            engine_status = 'PROTH20_DOCUMENTED_DOMAIN'
            benchmark_status = 'NOT_MEASURED'
            gate_status = 'GENERATED'
        })
    }
}

$document = [pscustomobject][ordered]@{
    schema = 'primeforge.fast_prime.portfolio.v1'
    generated_utc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    generator_version = $generatorVersion
    base_commit = $BaseCommit
    date_tag = $DateTag
    domain = [pscustomobject][ordered]@{
        k_min = $DomainMin.ToString($invariant)
        k_max = $DomainMax.ToString($invariant)
        k_parity = 'ODD'
        proth20_documented_k_limit_exclusive = '100000000'
        proth20_documented_n_limit_exclusive = '100000000'
    }
    target_probability = $TargetProbability.ToString('0.000000', $invariant)
    target_lambda = $lambdaTarget.ToString('0.000000000000000', $invariant)
    exact_digit_formula = 'floor(log10(k)+n*log10(2))+1'
    ranges = @($ranges)
}
$json = ($document | ConvertTo-Json -Depth 10) + "`n"

if (-not [string]::IsNullOrWhiteSpace($OutputFile)) {
    $path = if ([IO.Path]::IsPathRooted($OutputFile)) {
        [IO.Path]::GetFullPath($OutputFile)
    } else {
        [IO.Path]::GetFullPath((Join-Path $repositoryRoot $OutputFile))
    }
    $parent = Split-Path -Parent $path
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    [IO.File]::WriteAllText($path, $json, $utf8)
    Write-Host "fast_prime.portfolio.output=$path"
    Write-Host "fast_prime.portfolio.sha256=$((Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant())"
}
Write-Host "fast_prime.portfolio.ranges=$($ranges.Count)"
Write-Host 'fast_prime.portfolio.status=PASS'
