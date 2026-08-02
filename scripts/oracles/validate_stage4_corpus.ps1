[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [string]$Corpus = 'corpus\v1\cases.tsv',
    [string]$Pari = 'out\oracles\pari-gp64-2.17.4.exe',
    [string]$Flint = 'out\oracles\flint\flint-primality-oracle.exe',
    [string]$Proth20 = 'out\oracles\proth20\proth20.exe',
    [string]$OutputDirectory = 'out\oracles\stage4'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
function Resolve-RepositoryPath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $Path))
}

$corpusPath = Resolve-RepositoryPath $Corpus
$pariPath = Resolve-RepositoryPath $Pari
$flintPath = Resolve-RepositoryPath $Flint
$prothPath = Resolve-RepositoryPath $Proth20
$outputPath = Resolve-RepositoryPath $OutputDirectory

foreach ($required in @($corpusPath, $pariPath, $flintPath, $prothPath)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Required stage-4 input was not found: $required"
    }
}

New-Item -ItemType Directory -Force -Path $outputPath | Out-Null
$cases = Import-Csv -LiteralPath $corpusPath -Delimiter "`t"
if ($cases.Count -eq 0) {
    throw 'The stage-4 corpus is empty.'
}

$gpScriptPath = Join-Path $outputPath 'classify.gp'
$gpLines = New-Object System.Collections.Generic.List[string]
foreach ($case in $cases) {
    $gpLines.Add("print(`"$($case.id)|`",isprime($($case.decimal)))")
}
$gpLines.Add('quit()')
$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
[System.IO.File]::WriteAllLines($gpScriptPath, $gpLines, $utf8NoBom)

$pariOutput = @(& $pariPath -q -f $gpScriptPath)
if ($LASTEXITCODE -ne 0) {
    throw "PARI/GP failed with exit code $LASTEXITCODE"
}
$pariById = @{}
foreach ($line in $pariOutput) {
    $parts = $line.Trim() -split '\|', 2
    if ($parts.Count -ne 2) {
        throw "Unexpected PARI/GP output: $line"
    }
    $case = $cases | Where-Object { $_.id -eq $parts[0] } | Select-Object -First 1
    $pariById[$parts[0]] = if ([uint64]$case.decimal -lt 2) { 'NOT_PRIME' } elseif ($parts[1] -eq '1') { 'PROVEN_PRIME' } else { 'COMPOSITE' }
}

$resultLines = New-Object System.Collections.Generic.List[string]
$resultLines.Add("schema_version`tcase_id`texpected_outcome`tpari_gp_2_17_4`tflint_3_6_0`tagreement")
$disagreements = New-Object System.Collections.Generic.List[string]

foreach ($case in $cases) {
    $flintOutput = (& $flintPath $case.decimal | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) {
        throw "FLINT failed for $($case.id) with exit code $LASTEXITCODE"
    }
    $flintVerdict = if ($flintOutput -in @('PROVEN_PRIME', 'COMPOSITE', 'NOT_PRIME')) { $flintOutput } else { throw "Unexpected FLINT output for $($case.id): $flintOutput" }
    $pariVerdict = $pariById[$case.id]
    $expectedOracleVerdict = if ($case.expected_outcome -eq 'REJECTED_NON_CANDIDATE') { 'NOT_PRIME' } else { $case.expected_outcome }
    $agreement = if ($expectedOracleVerdict -eq $pariVerdict -and $pariVerdict -eq $flintVerdict) { 'AGREE' } else { 'DISAGREE' }
    $resultLines.Add("1`t$($case.id)`t$($case.expected_outcome)`t$pariVerdict`t$flintVerdict`t$agreement")
    if ($agreement -ne 'AGREE') {
        $disagreements.Add("$($case.id): expected=$($case.expected_outcome), PARI=$pariVerdict, FLINT=$flintVerdict")
    }
}

$resultsPath = Join-Path $outputPath 'oracle_results.tsv'
[System.IO.File]::WriteAllLines($resultsPath, $resultLines, $utf8NoBom)

$specialLines = New-Object System.Collections.Generic.List[string]
$specialLines.Add("schema_version`tcase_id`ttool`ttool_version`tverdict`tdevice")
foreach ($case in ($cases | Where-Object { $_.provenance -eq 'PARI_FLINT_PROTH20' })) {
    $raw = (& $prothPath -d 0 -q $case.form | Out-String)
    if ($LASTEXITCODE -ne 0) {
        throw "proth20 failed for $($case.id) with exit code $LASTEXITCODE"
    }
    $verdict = if ($raw -match ' is prime,') { 'PROVEN_PRIME' } elseif ($raw -match ' is composite,') { 'COMPOSITE' } else { throw "No proth20 verdict found for $($case.id)" }
    if ($verdict -ne $case.expected_outcome) {
        $disagreements.Add("$($case.id): expected=$($case.expected_outcome), proth20=$verdict")
    }
    $specialLines.Add("1`t$($case.id)`tPROTH20`t0.9.1`t$verdict`tNVIDIA_GEFORCE_RTX_5080")
    [System.IO.File]::WriteAllText((Join-Path $outputPath "$($case.id)-proth20.txt"), $raw.Replace("`r`n", "`n").TrimEnd() + "`n", $utf8NoBom)
}
[System.IO.File]::WriteAllLines((Join-Path $outputPath 'special_form_results.tsv'), $specialLines, $utf8NoBom)

if ($disagreements.Count -ne 0) {
    $disagreements | ForEach-Object { Write-Error $_ }
    throw "$($disagreements.Count) unexplained corpus disagreement(s)"
}

Write-Host "Stage-4 oracle validation PASS: $($cases.Count) cases, zero disagreements"
Write-Host "Results: $resultsPath"
