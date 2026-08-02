[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$packageRoot = $PSScriptRoot
$primeforge = Join-Path $packageRoot 'primeforge.exe'
$configuration = Join-Path $packageRoot 'search.yaml'
$campaignRoot = Join-Path $packageRoot 'out\campaigns\known-proth-small'
$checkpoint = Join-Path $campaignRoot 'campaign.checkpoint.json'
$results = Join-Path $campaignRoot 'results.jsonl'

$requirements = @(
    @{ Path = 'out\oracles\pari-gp64-2.17.4.exe'; Hash = '518ea54d23832211356c99d1bb58b74a3f0acd354a965543e7bcca9b34030119' },
    @{ Path = 'out\oracles\flint\flint-primality-oracle.exe'; Hash = '5e62bcac0e324d14914979e4f565eab2080da0e215cfff5c97e3fb48368facd4' },
    @{ Path = 'out\oracles\flint\flint-24.dll'; Hash = '00d4d34b091b145885368cb2737871ca98d84aadb52e7ac386fb56ac0016d08b' },
    @{ Path = 'out\oracles\flint\gmp-10.dll'; Hash = '9909aefb265224648bc7055b305c47a7f19319410775a05a91e88081799c0677' },
    @{ Path = 'out\oracles\flint\mpfr-6.dll'; Hash = 'e1852ef40d93f08eb341aa6ba726d529879ccc067867194df1166c2026f35eb2' },
    @{ Path = 'out\oracles\flint\pthreadVC3.dll'; Hash = 'd5348d53b70d994265f776a7b6be73fd86c40ed06442f954aa6624df963cbb02' }
)

if (-not (Test-Path -LiteralPath $primeforge -PathType Leaf)) {
    throw "primeforge.exe is missing from $packageRoot"
}

foreach ($requirement in $requirements) {
    $path = Join-Path $packageRoot $requirement.Path
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required external file is missing: $($requirement.Path). See ORACLES.md."
    }
    $observed = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($observed -ne $requirement.Hash) {
        throw "SHA-256 mismatch for external file: $($requirement.Path)"
    }
}

Push-Location $packageRoot
try {
    if (Test-Path -LiteralPath $checkpoint -PathType Leaf) {
        & $primeforge resume --checkpoint $checkpoint
    } else {
        if (Test-Path -LiteralPath $results -PathType Leaf) {
            throw 'results.jsonl exists without a checkpoint; refusing an ambiguous restart.'
        }
        & $primeforge inspect --config $configuration
        if ($LASTEXITCODE -ne 0) { throw "PrimeForge inspect failed with exit code $LASTEXITCODE" }
        & $primeforge search --config $configuration
    }
    if ($LASTEXITCODE -ne 0) { throw "PrimeForge campaign failed with exit code $LASTEXITCODE" }
    & $primeforge verify --result $results
    if ($LASTEXITCODE -ne 0) { throw "PrimeForge verification failed with exit code $LASTEXITCODE" }
} finally {
    Pop-Location
}
