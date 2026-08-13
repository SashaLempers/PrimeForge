[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [string]$Proth20Executable = 'out\oracles\proth20-batch\proth20-batch.exe',
    [string]$CandidateFile = 'benchmarks\profiles\proth20_n66411_profile20.txt',
    [string]$ExpectedFile = 'benchmarks\profiles\proth20_n66411_profile20_expected.tsv',
    [string]$OutputFile = '',
    [ValidateRange(2, 10)][int]$CandidateCount = 3
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$utf8 = [Text.UTF8Encoding]::new($false)

function Resolve-ProjectPath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $repositoryRoot $Path))
}

$executable = Resolve-ProjectPath $Proth20Executable
$candidatePath = Resolve-ProjectPath $CandidateFile
$expectedPath = Resolve-ProjectPath $ExpectedFile
foreach ($path in @($executable, $candidatePath, $expectedPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Required plan-cache test input is missing: $path"
    }
}

$candidates = @(Get-Content -LiteralPath $candidatePath |
    Where-Object { $_ -match '^\s*\d+\s+\d+\s*$' } |
    Select-Object -First $CandidateCount)
if ($candidates.Count -ne $CandidateCount) { throw 'Candidate corpus is too short.' }
$expected = @(Import-Csv -LiteralPath $expectedPath -Delimiter "`t" |
    Select-Object -First $CandidateCount)
if ($expected.Count -ne $CandidateCount) { throw 'Expected-result corpus is too short.' }

$temporaryRoot = Join-Path $repositoryRoot 'out\tests\proth20-plan-cache'
New-Item -ItemType Directory -Path $temporaryRoot -Force | Out-Null
$batchPath = Join-Path $temporaryRoot 'candidates.txt'
[IO.File]::WriteAllText($batchPath, (($candidates -join "`n") + "`n"), $utf8)

$output = @(& $executable --batch $batchPath --phase-profile 2>&1 | ForEach-Object { [string]$_ })
$exitCode = $LASTEXITCODE
if ($exitCode -ne 0) { throw "Proth20 plan-cache test exited with code $exitCode." }
$text = $output -join "`n"
if (-not [string]::IsNullOrWhiteSpace($OutputFile)) {
    $outputPath = Resolve-ProjectPath $OutputFile
    $parent = Split-Path -Parent $outputPath
    if (-not [string]::IsNullOrWhiteSpace($parent)) {
        New-Item -ItemType Directory -Path $parent -Force | Out-Null
    }
    [IO.File]::WriteAllText($outputPath, ($text + "`n"), $utf8)
}

$completed = @($output | Where-Object { $_ -match '^PRIMEFORGE_BATCH_COMPLETE\s' })
if ($completed.Count -ne $CandidateCount) {
    throw "Plan-cache test completed $($completed.Count)/$CandidateCount candidates."
}
$cacheCounters = @($output | Where-Object { $_ -match '^PRIMEFORGE_COUNTER\s+\d+\s+\d+\s+\d+\s+PLAN_CACHE_HIT\s+[01]$' })
if ($cacheCounters.Count -ne $CandidateCount -or $cacheCounters[0] -notmatch '\s0$') {
    throw 'The first transform class was not autotuned exactly once.'
}
if (@($cacheCounters | Select-Object -Skip 1 | Where-Object { $_ -notmatch '\s1$' }).Count -ne 0) {
    throw 'A same-class candidate missed the in-process plan cache.'
}
$autotunePhases = @($output | Where-Object { $_ -match '^PRIMEFORGE_PHASE\s+\d+\s+\d+\s+\d+\s+PLAN_AUTOTUNE\s+' })
if ($autotunePhases.Count -ne 1) { throw 'Autotuning did not occur exactly once.' }

foreach ($row in $expected) {
    $escapedPrefix = [Regex]::Escape("$($row.k) * 2^66411 + 1 is composite, a = $($row.witness)")
    $escapedResidue = [Regex]::Escape("RES64 = $($row.res64)")
    if ($text -notmatch ($escapedPrefix + '[^\r\n]*' + $escapedResidue)) {
        throw "Exact expected Proth20 residue is missing for k=$($row.k)."
    }
}

$autotuneNanoseconds = [uint64](($autotunePhases[0] -split '\s+')[-1])
Write-Host "proth20.plan_cache.candidates=$CandidateCount"
Write-Host 'proth20.plan_cache.misses=1'
Write-Host "proth20.plan_cache.hits=$($CandidateCount - 1)"
Write-Host "proth20.plan_cache.autotune_ns=$autotuneNanoseconds"
Write-Host 'proth20.plan_cache.exact_residues=PASS'
Write-Host 'proth20.plan_cache.status=PASS'
