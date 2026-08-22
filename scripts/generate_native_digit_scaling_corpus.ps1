[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [string]$OutputDirectory = 'out\benchmarks\native-digit-scaling\corpus',
    [int[]]$DigitCounts = @(20000, 40000, 60000, 80000, 100000),
    [ValidateRange(1, 32)][int]$MaximumBatchSize = 32,
    [string]$BaseCommit = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$utf8 = [Text.UTF8Encoding]::new($false)
$invariant = [Globalization.CultureInfo]::InvariantCulture
$generatorVersion = 'primeforge.native-digit-scaling-corpus.v1'
$domainMin = [uint64]10000001
$domainMax = [uint64]99999999
$log10Two = [Math]::Log10(2.0)

if ([string]::IsNullOrWhiteSpace($BaseCommit)) {
    $BaseCommit = (& git -C $repositoryRoot rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Cannot resolve the corpus base commit.' }
}
if ($BaseCommit -notmatch '^[0-9a-f]{40}$') {
    throw 'BaseCommit must be a full lowercase Git commit hash.'
}

$outputPath = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
    [IO.Path]::GetFullPath($OutputDirectory)
} else {
    [IO.Path]::GetFullPath((Join-Path $repositoryRoot $OutputDirectory))
}
[IO.Directory]::CreateDirectory($outputPath) | Out-Null

function Get-Sha256Bytes {
    param([Parameter(Mandatory)][string]$Text)
    $sha = [Security.Cryptography.SHA256]::Create()
    try { return $sha.ComputeHash($script:utf8.GetBytes($Text)) }
    finally { $sha.Dispose() }
}

function Convert-BigEndianU64 {
    param([Parameter(Mandatory)][byte[]]$Bytes, [Parameter(Mandatory)][int]$Offset)
    $slice = [byte[]]$Bytes[$Offset..($Offset + 7)]
    if ([BitConverter]::IsLittleEndian) { [Array]::Reverse($slice) }
    return [BitConverter]::ToUInt64($slice, 0)
}

function Get-DigitCount {
    param([Parameter(Mandatory)][uint64]$K, [Parameter(Mandatory)][uint32]$Exponent)
    return [int][Math]::Floor([Math]::Log10([double]$K) + [double]$Exponent * $script:log10Two) + 1
}

function Test-OddPrime {
    param([Parameter(Mandatory)][uint32]$Value)
    if ($Value -lt 2) { return $false }
    if ($Value -eq 2) { return $true }
    if (($Value -band 1) -eq 0) { return $false }
    for ([uint32]$divisor = 3; [uint64]$divisor * $divisor -le $Value; $divisor += 2) {
        if ($Value % $divisor -eq 0) { return $false }
    }
    return $true
}

function Get-ModPower {
    param(
        [Parameter(Mandatory)][uint64]$Base,
        [Parameter(Mandatory)][uint32]$Exponent,
        [Parameter(Mandatory)][uint32]$Modulus
    )
    [uint64]$result = 1
    [uint64]$factor = $Base % $Modulus
    [uint32]$remaining = $Exponent
    while ($remaining -ne 0) {
        if (($remaining -band 1) -ne 0) { $result = ($result * $factor) % $Modulus }
        $factor = ($factor * $factor) % $Modulus
        $remaining = $remaining -shr 1
    }
    return [uint32]$result
}

function Get-Jacobi {
    param([Parameter(Mandatory)][uint64]$X, [Parameter(Mandatory)][uint64]$Y)
    [uint64]$m = $X
    [uint64]$n = $Y
    [int]$sign = 1
    while ($m -ne 0) {
        $oddHalves = $false
        while (($m -band 1) -eq 0) {
            $m = $m -shr 1
            $oddHalves = -not $oddHalves
        }
        if ($oddHalves -and ($n % 8 -ne 1) -and ($n % 8 -ne 7)) { $sign = -$sign }
        if ($m -eq 1) { return $sign }
        if (($m % 4 -eq 3) -and ($n % 4 -eq 3)) { $sign = -$sign }
        $temporary = $n
        $n = $m
        $m = $temporary % $n
    }
    return [int]$n
}

function Get-Proth20Witness {
    param([Parameter(Mandatory)][uint64]$K, [Parameter(Mandatory)][uint32]$Exponent)
    for ([uint32]$a = 3; $a -lt 100000; $a += 2) {
        if (-not (Test-OddPrime $a)) { continue }
        [uint32]$pModuloA = [uint32]($K % $a)
        if ($pModuloA -eq 0) { continue }
        $power = Get-ModPower -Base 2 -Exponent $Exponent -Modulus $a
        $pModuloA = [uint32](([uint64]$pModuloA * $power + 1) % $a)
        if ($pModuloA -eq 0) { return [pscustomobject]@{ accepted = $false; value = $a } }
        if ($pModuloA -eq 1) { continue }
        $jacobi = Get-Jacobi -X $pModuloA -Y $a
        if ($jacobi -gt 1) { return [pscustomobject]@{ accepted = $false; value = $jacobi } }
        if ($jacobi -eq -1) { return [pscustomobject]@{ accepted = $true; value = $a } }
    }
    throw "No bounded Proth20 witness found for k=$K n=$Exponent."
}

$entries = [Collections.Generic.List[object]]::new()
foreach ($digits in $DigitCounts) {
    if ($digits -lt 1000 -or $digits -gt 1000000) {
        throw "Digit count outside the bounded scaling domain: $digits"
    }

    $seed = "PrimeForge|$generatorVersion|base=$BaseCommit|digits=$digits|batch=$MaximumBatchSize"
    $digest = Get-Sha256Bytes $seed
    $span = [uint64](2 * ($MaximumBatchSize - 1))
    $oddStartCount = [uint64](($domainMax - $domainMin - $span) / 2 + 1)
    $startSelector = Convert-BigEndianU64 $digest 0
    $kMin = $domainMin + 2 * ($startSelector % $oddStartCount)
    $kMax = $kMin + $span

    $nMin = [int64][Math]::Ceiling((($digits - 1.0) - [Math]::Log10([double]$kMin)) / $log10Two)
    $nMax = [int64][Math]::Ceiling(($digits - [Math]::Log10([double]$kMax)) / $log10Two) - 1
    if ($nMin -lt 32 -or $nMax -ge 100000000 -or $nMin -gt $nMax) {
        throw "No supported exponent keeps the complete $digits-digit batch in band."
    }
    $nCount = [uint64]($nMax - $nMin + 1)
    $n = [uint32]($nMin + [int64]((Convert-BigEndianU64 $digest 8) % $nCount))

    $accepted = [Collections.Generic.List[object]]::new()
    $k = $kMin
    while ($accepted.Count -lt $MaximumBatchSize) {
        if ($k -gt $domainMax) { throw "The deterministic candidate scan exceeded the supported k domain at $digits digits." }
        if ((Get-DigitCount -K $k -Exponent $n) -ne $digits) {
            throw "Exact digit validation failed for digits=$digits k=$k."
        }
        $witness = Get-Proth20Witness -K $k -Exponent $n
        if ($witness.accepted) {
            $accepted.Add([pscustomobject]@{ k = $k; witness = [uint32]$witness.value })
        }
        $k += 2
    }
    $records = @($accepted | ForEach-Object {
        '{0} {1}' -f $_.k.ToString($invariant), $n.ToString($invariant)
    })
    $kMax = [uint64]$accepted[$accepted.Count - 1].k

    $files = [ordered]@{}
    foreach ($batch in @(1, 8, 16, 32)) {
        if ($batch -gt $MaximumBatchSize) { continue }
        $name = 'digits-{0:D6}-b{1}.txt' -f $digits, $batch
        $path = Join-Path $outputPath $name
        [IO.File]::WriteAllText($path, (($records | Select-Object -First $batch) -join "`n") + "`n", $utf8)
        $files["b$batch"] = [ordered]@{
            file = $name
            sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant()
            candidates = $batch
        }
    }

    $entries.Add([pscustomobject][ordered]@{
        requested_digits = $digits
        exact_digits = $digits
        exponent = $n
        k_min = $kMin.ToString($invariant)
        k_max = $kMax.ToString($invariant)
        k_step = 2
        selection = 'FIRST_32_ODD_K_PASSING_PINNED_PROTH20_WITNESS_PREFLIGHT'
        witnesses = @($accepted | ForEach-Object { $_.witness })
        seed = $seed
        seed_sha256 = (-join ($digest | ForEach-Object { $_.ToString('x2') }))
        purpose = 'CLOSED_NON_DISCOVERY_SCALING_BENCHMARK'
        files = $files
    })
}

$manifest = [pscustomobject][ordered]@{
    schema = 'primeforge.native_digit_scaling_corpus.v1'
    generated_utc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    generator_version = $generatorVersion
    base_commit = $BaseCommit
    digit_formula = 'floor(log10(k)+n*log10(2))+1'
    maximum_batch_size = $MaximumBatchSize
    discovery_status = 'NOT_A_DISCOVERY_CAMPAIGN'
    entries = @($entries)
}
$manifestPath = Join-Path $outputPath 'manifest.json'
[IO.File]::WriteAllText($manifestPath, ($manifest | ConvertTo-Json -Depth 10) + "`n", $utf8)

Write-Host "native_scaling.corpus=$outputPath"
Write-Host "native_scaling.manifest=$manifestPath"
Write-Host "native_scaling.manifest_sha256=$((Get-FileHash -Algorithm SHA256 -LiteralPath $manifestPath).Hash.ToLowerInvariant())"
Write-Host "native_scaling.sizes=$($DigitCounts -join ',')"
Write-Host 'native_scaling.status=PASS'
