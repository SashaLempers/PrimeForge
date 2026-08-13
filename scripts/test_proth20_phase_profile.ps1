[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [string]$WorkingDirectory = 'out\tests\proth20-phase-profile-analyzer'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$utf8 = [Text.UTF8Encoding]::new($false)
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$workingPath = if ([IO.Path]::IsPathRooted($WorkingDirectory)) {
    [IO.Path]::GetFullPath($WorkingDirectory)
} else {
    [IO.Path]::GetFullPath((Join-Path $repositoryRoot $WorkingDirectory))
}
$allowedRoot = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'out'))
if (-not $workingPath.StartsWith($allowedRoot + [IO.Path]::DirectorySeparatorChar,
    [StringComparison]::OrdinalIgnoreCase)) {
    throw "Refusing test cleanup outside $allowedRoot"
}
if (Test-Path -LiteralPath $workingPath) { [IO.Directory]::Delete($workingPath, $true) }

try {
    New-Item -ItemType Directory -Path $workingPath -Force | Out-Null
    $runIds = @('A1-one-worker', 'B1-two-workers', 'B2-two-workers', 'A2-one-worker')
    for ($runIndex = 0; $runIndex -lt $runIds.Count; ++$runIndex) {
        $runPath = Join-Path $workingPath $runIds[$runIndex]
        $workerPath = Join-Path $runPath 'worker-01'
        New-Item -ItemType Directory -Path $workerPath -Force | Out-Null
        $lines = [Collections.Generic.List[string]]::new()
        for ($index = 1; $index -le 20; ++$index) {
            $k = 10001 + 2 * $index
            foreach ($phase in @(
                @('WITNESS_SELECTION', 100),
                @('GPMP_CONSTRUCT_TOTAL', 15000),
                @('CHECKPOINT', 100),
                @('A_POW_K', 1000),
                @('MAIN_SQUARING_LOOP', 80000),
                @('FINAL_RESIDUE_TOTAL', 100),
                @('GERBICZ_FINAL_CHECK', 1000),
                @('RESULT_FORMAT_AND_WRITE', 100),
                @('KERNEL_RELEASE', 100),
                @('BUFFER_RELEASE', 100),
                @('PROGRAM_CLEAR', 100),
                @('CANDIDATE_TOTAL', 100000)
            )) {
                $lines.Add("PRIMEFORGE_PHASE`t$index`t$k`t66411`t$($phase[0])`t$($phase[1])")
            }
            $lines.Add("PRIMEFORGE_BATCH_COMPLETE`t$index`t$k`t66411`tCOMPOSITE")
        }
        $lines.Add("PRIMEFORGE_PHASE`t0`t0`t0`tPROCESS_TOTAL`t2000000")
        [IO.File]::WriteAllLines((Join-Path $workerPath 'profile.stdout.log'), $lines, $utf8)
        [IO.File]::WriteAllText((Join-Path $workerPath 'profile.stderr.log'), '', $utf8)
        $temperature = 50 + ($runIndex % 2)
        $telemetry = [pscustomobject][ordered]@{
            cpu_temperature_celsius = [pscustomobject]@{ status='DETECTED'; value='70' }
            gpu_temperature_celsius = [pscustomobject]@{ status='DETECTED'; value=[string]$temperature }
            gpu_power_watts = [pscustomobject]@{ status='DETECTED'; value='200' }
            throttling_detected = $false
            whea_errors_recent = [pscustomobject]@{ status='DETECTED'; value='0' }
        }
        [IO.File]::WriteAllText((Join-Path $runPath 'telemetry.jsonl'),
            ($telemetry | ConvertTo-Json -Compress) + "`n", $utf8)
    }

    $directPath = Join-Path $workingPath 'direct-baseline'
    for ($index = 1; $index -le 3; ++$index) {
        $candidatePath = Join-Path $directPath ("candidate-{0}" -f $index)
        New-Item -ItemType Directory -Path $candidatePath -Force | Out-Null
        $k = 10001 + 2 * $index
        $directLines = @(
            "PRIMEFORGE_PHASE`t1`t$k`t66411`tWITNESS_SELECTION`t100",
            "PRIMEFORGE_PHASE`t1`t$k`t66411`tGPMP_CONSTRUCT_TOTAL`t15000",
            "PRIMEFORGE_PHASE`t1`t$k`t66411`tCHECKPOINT`t100",
            "PRIMEFORGE_PHASE`t1`t$k`t66411`tA_POW_K`t1000",
            "PRIMEFORGE_PHASE`t1`t$k`t66411`tMAIN_SQUARING_LOOP`t80000",
            "PRIMEFORGE_PHASE`t1`t$k`t66411`tFINAL_RESIDUE_TOTAL`t100",
            "PRIMEFORGE_PHASE`t1`t$k`t66411`tGERBICZ_FINAL_CHECK`t1000",
            "PRIMEFORGE_PHASE`t1`t$k`t66411`tRESULT_FORMAT_AND_WRITE`t100",
            "PRIMEFORGE_PHASE`t1`t$k`t66411`tKERNEL_RELEASE`t100",
            "PRIMEFORGE_PHASE`t1`t$k`t66411`tBUFFER_RELEASE`t100",
            "PRIMEFORGE_PHASE`t1`t$k`t66411`tPROGRAM_CLEAR`t100",
            "PRIMEFORGE_PHASE`t1`t$k`t66411`tCANDIDATE_TOTAL`t100000",
            "PRIMEFORGE_BATCH_COMPLETE`t1`t$k`t66411`tCOMPOSITE",
            "PRIMEFORGE_PHASE`t0`t0`t0`tPROCESS_TOTAL`t110000"
        )
        [IO.File]::WriteAllLines((Join-Path $candidatePath 'profile.stdout.log'), $directLines, $utf8)
    }

    & (Join-Path $PSScriptRoot 'analyze_proth20_phase_profile.ps1') `
        -InputDirectory $workingPath -ExpectedCandidates 20
    $summary = Get-Content -Raw -LiteralPath (Join-Path $workingPath 'analysis\summary.json') |
        ConvertFrom-Json
    if ($summary.status -ne 'PASS') { throw 'Synthetic analyzer status was not PASS.' }
    if ([int]$summary.candidate_executions -ne 80) { throw 'Synthetic execution count mismatch.' }
    if ([double]$summary.minimum_coverage_percent -lt 95.0) { throw 'Synthetic coverage gate failed.' }
    if ($summary.p0_100_survivor_gate -ne 'NOT_RUN') { throw 'P0 100-survivor status mismatch.' }
    Write-Host 'proth20.phase-profile-analyzer-test: PASS'
} finally {
    if (Test-Path -LiteralPath $workingPath) { [IO.Directory]::Delete($workingPath, $true) }
}
