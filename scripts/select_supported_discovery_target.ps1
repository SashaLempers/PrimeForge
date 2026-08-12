[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [uint32]$Exponent = 33326,
    [uint64]$CandidateCount = 40001,
    [uint64]$DomainMin = 10000001,
    [uint64]$DomainMax = 99999999,
    [uint32]$Attempt = 1,
    [string]$DateTag = '2026-08-12',
    [string]$BaseCommit = '279a7c76e1bcd1b0f96c60214f534b84c1b00282',
    [string]$OutputFile = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$invariant = [Globalization.CultureInfo]::InvariantCulture
$utf8 = [Text.UTF8Encoding]::new($false)

if ($Exponent -lt 32) { throw 'Exponent must be at least 32.' }
if ($CandidateCount -eq 0) { throw 'CandidateCount must be positive.' }
if ($DomainMin -lt 3 -or $DomainMin -gt $DomainMax -or
    ($DomainMin -band 1) -eq 0 -or ($DomainMax -band 1) -eq 0) {
    throw 'The selection domain must be an ordered odd-k interval beginning at 3 or greater.'
}
if ($DomainMax -gt 99999999) {
    throw 'The selected domain must remain inside the pinned Proth20 documented limit.'
}
if ($CandidateCount - 1 -gt ([uint64]::MaxValue / 2)) {
    throw 'CandidateCount is too large.'
}

$windowSpan = 2 * ($CandidateCount - 1)
if ($windowSpan -gt $DomainMax - $DomainMin) {
    throw 'The requested candidate window does not fit in the selection domain.'
}
$maximumStart = $DomainMax - $windowSpan
$startCount = (($maximumStart - $DomainMin) / 2) + 1

$seed = 'PrimeForge|SashaLempers|{0}|n={1}|attempt={2}|k-domain={3}..{4}|candidate-count={5}|base={6}' -f `
    $DateTag, $Exponent, $Attempt, $DomainMin, $DomainMax, $CandidateCount, $BaseCommit
$hasher = [Security.Cryptography.SHA256]::Create()
try {
    $digest = $hasher.ComputeHash($utf8.GetBytes($seed))
} finally {
    $hasher.Dispose()
}
$seedHash = -join ($digest | ForEach-Object { $_.ToString('x2') })
$prefix = [byte[]]$digest[0..7]
if ([BitConverter]::IsLittleEndian) { [Array]::Reverse($prefix) }
$prefixValue = [BitConverter]::ToUInt64($prefix, 0)
$startIndex = $prefixValue % $startCount
$kMin = $DomainMin + 2 * $startIndex
$kMax = $kMin + $windowSpan

$result = [pscustomobject][ordered]@{
    schema = 'primeforge.discovery.target_selection.v1'
    algorithm = 'u64_be(sha256(seed)[0..7]) modulo odd_start_count'
    seed = $seed
    seed_sha256 = $seedHash
    sha256_prefix_u64_be = $prefixValue.ToString($invariant)
    base_commit = $BaseCommit
    attempt = $Attempt
    exponent = $Exponent
    domain_min = $DomainMin.ToString($invariant)
    domain_max = $DomainMax.ToString($invariant)
    candidate_count = $CandidateCount.ToString($invariant)
    odd_start_count = $startCount.ToString($invariant)
    selected_start_index = $startIndex.ToString($invariant)
    k_min = $kMin.ToString($invariant)
    k_max = $kMax.ToString($invariant)
}
$json = ($result | ConvertTo-Json -Depth 4) + "`n"

if (-not [string]::IsNullOrWhiteSpace($OutputFile)) {
    $path = if ([IO.Path]::IsPathRooted($OutputFile)) {
        [IO.Path]::GetFullPath($OutputFile)
    } else {
        [IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $PSScriptRoot) $OutputFile))
    }
    $parent = Split-Path -Parent $path
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    [IO.File]::WriteAllText($path, $json, $utf8)
}

Write-Output $json.TrimEnd()
