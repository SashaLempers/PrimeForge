[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [string]$OutputDirectory = 'benchmarks\evidence\stage12',
    [string]$Primesieve = 'out\audit_builds\primesieve-release\primesieve.exe',
    [string]$Flint = 'out\oracles\flint\flint-primality-oracle.exe',
    [string]$PariGp = 'out\oracles\pari-gp64-2.17.4.exe'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$root = Split-Path -Parent $PSScriptRoot
function Resolve-ProjectPath([string]$Path) {
    $candidate = if ([IO.Path]::IsPathRooted($Path)) { $Path } else { Join-Path $root $Path }
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { throw "Missing external tool: $candidate" }
    return [IO.Path]::GetFullPath($candidate)
}
$primesieveExe = Resolve-ProjectPath $Primesieve
$flintExe = Resolve-ProjectPath $Flint
$pariExe = Resolve-ProjectPath $PariGp
$output = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
    [IO.Path]::GetFullPath($OutputDirectory)
} else { [IO.Path]::GetFullPath((Join-Path $root $OutputDirectory)) }
if (Test-Path -LiteralPath $output) { throw "Refusing to overwrite existing evidence: $output" }
New-Item -ItemType Directory -Path $output | Out-Null

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$visualStudio = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
& (Join-Path $visualStudio 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation
$cmake = Join-Path $visualStudio 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
& $cmake --preset msvc-release
if ($LASTEXITCODE -ne 0) { throw 'Release configure failed.' }
& $cmake --build --preset msvc-release --parallel
if ($LASTEXITCODE -ne 0) { throw 'Release build failed.' }
$adapter = Join-Path $root 'out\build\msvc-release\primeforge-adapters.exe'
$utf8 = [Text.UTF8Encoding]::new($false)
$rows = [Collections.Generic.List[string]]::new()
$rows.Add("schema_version`tengine`tcase`tinput`texecutable_sha256`tmanual_result`tadapter_status`tadapter_diagnostics`traw_stdout_sha256`tmatch")

function Invoke-AdapterCase {
    param(
        [string]$Engine,
        [string]$Case,
        [string]$InputValue,
        [string]$Executable,
        [string]$ManualResult,
        [string]$ExpectedStatus
    )
    $hash = (Get-FileHash -LiteralPath $Executable -Algorithm SHA256).Hash.ToLowerInvariant()
    $lines = @(& $adapter --engine $Engine --executable $Executable --sha256 $hash --input $InputValue --work-root (Join-Path $output 'work') --job-id "$Engine-$Case" --timeout-ms 10000 --memory-bytes 536870912)
    if ($LASTEXITCODE -ne 0) { throw "Adapter failed: $Engine/$Case" }
    $status = ($lines | Where-Object { $_ -like 'primality_status=*' } | Select-Object -First 1).Substring(17)
    $diagnostics = ($lines | Where-Object { $_ -like 'diagnostics=*' } | Select-Object -First 1).Substring(12)
    $rawPath = ($lines | Where-Object { $_ -like 'raw_stdout=*' } | Select-Object -First 1).Substring(11)
    $rawHash = (Get-FileHash -LiteralPath $rawPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $match = if ($status -eq $ExpectedStatus) { 'YES' } else { 'NO' }
    if ($match -ne 'YES') { throw "Manual/adapter disagreement: $Engine/$Case" }
    $rows.Add("1`t$Engine`t$Case`t$InputValue`t$hash`t$ManualResult`t$status`t$diagnostics`t$rawHash`t$match")
}

$manualPrimeCount = (& $primesieveExe 0 999 --count --quiet --threads=1 | Select-Object -Last 1).Trim()
if ($LASTEXITCODE -ne 0 -or $manualPrimeCount -ne '168') { throw 'Manual primesieve vector failed.' }
Invoke-AdapterCase 'primesieve' 'count-0-999' '0:999' $primesieveExe "COUNT=$manualPrimeCount" 'UNTESTED'

foreach ($case in @(@{ Name='prime-101'; Input='101'; Status='PROVEN_PRIME' }, @{ Name='composite-341'; Input='341'; Status='COMPOSITE' })) {
    $manual = (& $flintExe $case.Input | Select-Object -Last 1).Trim()
    if ($LASTEXITCODE -ne 0 -or $manual -ne $case.Status) { throw "Manual FLINT vector failed: $($case.Name)" }
    Invoke-AdapterCase 'flint' $case.Name $case.Input $flintExe $manual $case.Status
}

foreach ($case in @(@{ Name='prime-101'; Input='101'; Status='PROVEN_PRIME' }, @{ Name='composite-341'; Input='341'; Status='COMPOSITE' })) {
    $manualDirectory = Join-Path $output "manual-pari-$($case.Name)"
    New-Item -ItemType Directory -Path $manualDirectory | Out-Null
    $script = Join-Path $manualDirectory 'request.gp'
    $marker = if ($case.Status -eq 'PROVEN_PRIME') { 'PRIMEFORGE:PROVEN_PRIME' } else { 'PRIMEFORGE:COMPOSITE' }
    [IO.File]::WriteAllText($script, "n=$($case.Input);if(isprime(n),print(`"PRIMEFORGE:PROVEN_PRIME`"),print(`"PRIMEFORGE:COMPOSITE`"));quit()`n", $utf8)
    $manual = (& $pariExe -q $script | Select-Object -Last 1).Trim()
    if ($LASTEXITCODE -ne 0 -or $manual -ne $marker) { throw "Manual PARI vector failed: $($case.Name)" }
    Invoke-AdapterCase 'pari-gp' $case.Name $case.Input $pariExe $manual $case.Status
}

$availability = @(
    "schema_version`tengine`tlocal_execution`tstatus`treason",
    "1`tprimesieve`tYES`tREPRODUCED`tPINNED_BINARY_HASH_AND_MANUAL_MATCH",
    "1`tflint`tYES`tREPRODUCED`tPINNED_BINARY_HASH_AND_MANUAL_MATCH",
    "1`tpari-gp`tYES`tREPRODUCED`tPINNED_BINARY_HASH_AND_MANUAL_MATCH",
    "1`topenpfgw`tNO`tUNAVAILABLE`tNO_AUDITED_LOCAL_BINARY_AND_REDISTRIBUTION_PROHIBITED",
    "1`tgenefer22`tNO`tUNAVAILABLE`tMSYS2_TOOLCHAIN_NOT_INSTALLED",
    "1`tmersenne-prpll`tNO`tUNAVAILABLE`tNO_AUDITED_LOCAL_BINARY",
    "1`tmlucas`tNO`tUNAVAILABLE`tWSL_DISTRIBUTION_NOT_INSTALLED",
    "1`tgmp-ecm`tNO`tDEFERRED`tNO_MEASURED_COST_BENEFIT_JUSTIFICATION"
)
[IO.File]::WriteAllText((Join-Path $output 'manual_comparison.tsv'), (($rows -join "`n") + "`n"), $utf8)
[IO.File]::WriteAllText((Join-Path $output 'availability.tsv'), (($availability -join "`n") + "`n"), $utf8)
$hashLines = @('sha256  file')
foreach ($name in @('manual_comparison.tsv','availability.tsv')) {
    $hashLines += "$((Get-FileHash -LiteralPath (Join-Path $output $name) -Algorithm SHA256).Hash.ToLowerInvariant())  $name"
}
[IO.File]::WriteAllText((Join-Path $output 'SHA256SUMS'), (($hashLines -join "`n") + "`n"), $utf8)
Write-Host 'Stage 12 local adapter validation: PASS (5 manual vectors; 3 installed engines; 5 unavailable/deferred engines explicit).'
