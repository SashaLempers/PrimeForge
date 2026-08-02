[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [string]$PrimeForgeSieve = 'out\build\msvc-release\primeforge-sieve.exe',
    [string]$Primesieve = 'out\audit_builds\primesieve-release\primesieve.exe',
    [string]$FlintOracle = 'out\oracles\flint\flint-primality-oracle.exe'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot

function Resolve-ProjectTool {
    param([Parameter(Mandatory = $true)][string]$Path)
    $candidate = if ([System.IO.Path]::IsPathRooted($Path)) { $Path } else { Join-Path $root $Path }
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) {
        throw "Required local tool is absent: $candidate"
    }
    return [System.IO.Path]::GetFullPath($candidate)
}

function Invoke-Tool {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )
    $lines = @(& $Executable @Arguments)
    if ($LASTEXITCODE -ne 0) {
        throw "Tool failed with exit code ${LASTEXITCODE}: $Executable"
    }
    return $lines
}

$primeforgeExe = Resolve-ProjectTool $PrimeForgeSieve
$primesieveExe = Resolve-ProjectTool $Primesieve
$flintExe = Resolve-ProjectTool $FlintOracle

$intervals = @(
    @{ Begin = [uint64]0; End = [uint64]1000 },
    @{ Begin = [uint64]999983; End = [uint64]1001000 },
    @{ Begin = [uint64]1000000000; End = [uint64]1000100000 },
    @{ Begin = [uint64]1000000000000; End = [uint64]1000000100000 }
)

foreach ($interval in $intervals) {
    $begin = $interval.Begin
    $end = $interval.End
    $primeforgeOutput = @(Invoke-Tool $primeforgeExe @('--begin', "$begin", '--end', "$end", '--threads', '4'))
    $countLine = @($primeforgeOutput | Where-Object { $_ -match '^count=([0-9]+)$' })
    if ($countLine.Count -ne 1) {
        throw "PrimeForge returned no unique count for [$begin,$end)"
    }
    $primeforgeCount = [uint64]($countLine[0].Substring(6))
    $closedEnd = $end - 1
    $primesieveOutput = @(Invoke-Tool $primesieveExe @("$begin", "$closedEnd", '--count', '--quiet', '--threads=4'))
    $primesieveCount = [uint64]($primesieveOutput[-1].Trim())
    if ($primeforgeCount -ne $primesieveCount) {
        throw "primesieve disagreement for [$begin,$end): PrimeForge=$primeforgeCount primesieve=$primesieveCount"
    }
    Write-Host "AGREE interval=[$begin,$end) count=$primeforgeCount"
}

$casesPath = Join-Path $root 'corpus\v1\cases.tsv'
$cases = Import-Csv -LiteralPath $casesPath -Delimiter "`t"
foreach ($case in $cases) {
    $value = $case.decimal
    $primeforgeOutput = @(Invoke-Tool $primeforgeExe @('--is-prime', $value))
    $primeforgePrime = $primeforgeOutput[-1].Trim() -eq 'is_prime=YES'
    $flintOutput = @(Invoke-Tool $flintExe @($value))
    $flintPrime = $flintOutput[-1].Trim() -eq 'PROVEN_PRIME'
    if ($primeforgePrime -ne $flintPrime) {
        throw "FLINT disagreement for corpus case $($case.id) value=$value"
    }
}

Write-Host "Stage 6 external oracle validation: PASS ($($intervals.Count) primesieve intervals; $($cases.Count) FLINT corpus values)"
