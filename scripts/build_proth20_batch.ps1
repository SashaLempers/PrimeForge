[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [string]$SourceDirectory = 'out\third_party\proth20-src',
    [string]$OutputDirectory = 'out\oracles\proth20-batch',
    [string]$ExecutableName = 'proth20-batch.exe',
    [string]$OpenClLibrary = 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.3\lib\x64\OpenCL.lib',
    [switch]$BuildQuickKernelProbe
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$expectedRevision = '6771325939a7ceef2c75644c79981c7df4a61882'
$sourceUrl = 'https://github.com/galloty/proth20.git'
$batchPatch = Join-Path $repositoryRoot 'patches\proth20-persistent-batch.patch'
$profilePatch = Join-Path $repositoryRoot 'patches\proth20-phase-profile.patch'
$planCachePatch = Join-Path $repositoryRoot 'patches\proth20-plan-cache.patch'
$invariantPatch = Join-Path $repositoryRoot 'patches\proth20-invariant-context-prototype.patch'
$nativeBatchPatch = Join-Path $repositoryRoot 'patches\proth20-native-batch-prototype.patch'
$nativeProductionPatch = Join-Path $repositoryRoot 'patches\proth20-native-b8-production.patch'
$nativeKernelProfilePatch = Join-Path $repositoryRoot 'patches\proth20-native-b8-kernel-profile.patch'
$nativeB32ProductionPatch = Join-Path $repositoryRoot 'patches\proth20-native-b32-production.patch'

function Resolve-ProjectPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $Path))
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [string[]]$Arguments = @()
    )
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $Executable $($Arguments -join ' ')"
    }
}

$sourcePath = Resolve-ProjectPath $SourceDirectory
$outputPath = Resolve-ProjectPath $OutputDirectory
$openClPath = [System.IO.Path]::GetFullPath($OpenClLibrary)

if (-not (Test-Path -LiteralPath $batchPatch -PathType Leaf)) {
    throw "Pinned proth20 batch patch is missing: $batchPatch"
}

function Test-ReversePatch {
    param(
        [Parameter(Mandatory = $true)][string]$SourcePath,
        [Parameter(Mandatory = $true)][string]$PatchPath,
        [switch]$ZeroContext
    )
    $arguments = @('-C', $SourcePath, 'apply')
    if ($ZeroContext) { $arguments += @('--recount', '--unidiff-zero') }
    $arguments += @('--reverse', '--check', $PatchPath)
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & git @arguments 2>$null
        return $LASTEXITCODE -eq 0
    } finally {
        $ErrorActionPreference = $savedPreference
    }
}
if (-not (Test-Path -LiteralPath $profilePatch -PathType Leaf)) {
    throw "Pinned proth20 profiling patch is missing: $profilePatch"
}
if (-not (Test-Path -LiteralPath $planCachePatch -PathType Leaf)) {
    throw "Pinned proth20 plan-cache patch is missing: $planCachePatch"
}
foreach ($requiredPatch in @($invariantPatch, $nativeBatchPatch, $nativeProductionPatch, $nativeKernelProfilePatch, $nativeB32ProductionPatch)) {
    if (-not (Test-Path -LiteralPath $requiredPatch -PathType Leaf)) {
        throw "Pinned proth20 production patch is missing: $requiredPatch"
    }
}
if (-not (Test-Path -LiteralPath $openClPath -PathType Leaf)) {
    throw "OpenCL import library is missing: $openClPath"
}

if (-not (Test-Path -LiteralPath (Join-Path $sourcePath '.git'))) {
    New-Item -ItemType Directory -Path (Split-Path -Parent $sourcePath) -Force | Out-Null
    Invoke-Checked -Executable git -Arguments @('clone', $sourceUrl, $sourcePath)
    Invoke-Checked -Executable git -Arguments @('-C', $sourcePath, 'checkout', '--detach', $expectedRevision)
}

$revision = (& git -C $sourcePath rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $revision -ne $expectedRevision) {
    throw "proth20 source revision mismatch: expected $expectedRevision, got $revision"
}

& git -C $sourcePath diff --quiet
$sourceChanged = $LASTEXITCODE -ne 0
$profileHeader = Join-Path $sourcePath 'src\primeforge_profile.h'
if (-not $sourceChanged -and -not (Test-Path -LiteralPath $profileHeader -PathType Leaf)) {
    Invoke-Checked -Executable git -Arguments @('-C', $sourcePath, 'apply', '--check', $batchPatch)
    Invoke-Checked -Executable git -Arguments @('-C', $sourcePath, 'apply', $batchPatch)
    Invoke-Checked -Executable git -Arguments @('-C', $sourcePath, 'apply', '--recount', '--unidiff-zero', '--check', $profilePatch)
    Invoke-Checked -Executable git -Arguments @('-C', $sourcePath, 'apply', '--recount', '--unidiff-zero', $profilePatch)
} else {
    $profileApplied = Test-ReversePatch -SourcePath $sourcePath -PatchPath $profilePatch -ZeroContext
    if (-not $profileApplied) {
        $batchApplied = Test-ReversePatch -SourcePath $sourcePath -PatchPath $batchPatch
        if (-not $batchApplied) {
            throw 'The proth20 source has local changes other than the pinned PrimeForge patches.'
        }
        Invoke-Checked -Executable git -Arguments @('-C', $sourcePath, 'apply', '--recount', '--unidiff-zero', '--check', $profilePatch)
        Invoke-Checked -Executable git -Arguments @('-C', $sourcePath, 'apply', '--recount', '--unidiff-zero', $profilePatch)
    }
}

$planCacheApplied = Test-ReversePatch `
    -SourcePath $sourcePath -PatchPath $planCachePatch -ZeroContext
if (-not $planCacheApplied) {
    Invoke-Checked -Executable git -Arguments @(
        '-C', $sourcePath, 'apply', '--recount', '--unidiff-zero', '--check', $planCachePatch)
    Invoke-Checked -Executable git -Arguments @(
        '-C', $sourcePath, 'apply', '--recount', '--unidiff-zero', $planCachePatch)
}

foreach ($productionPatch in @($invariantPatch, $nativeBatchPatch, $nativeProductionPatch)) {
    $alreadyApplied = Test-ReversePatch -SourcePath $sourcePath -PatchPath $productionPatch
    if (-not $alreadyApplied) {
        Invoke-Checked -Executable git -Arguments @('-C', $sourcePath, 'apply', '--check', $productionPatch)
        Invoke-Checked -Executable git -Arguments @('-C', $sourcePath, 'apply', $productionPatch)
    }
}

$nativeKernelProfileApplied = Test-ReversePatch `
    -SourcePath $sourcePath -PatchPath $nativeKernelProfilePatch
if (-not $nativeKernelProfileApplied) {
    Invoke-Checked -Executable git -Arguments @('-C', $sourcePath, 'apply', '--check', $nativeKernelProfilePatch)
    Invoke-Checked -Executable git -Arguments @('-C', $sourcePath, 'apply', $nativeKernelProfilePatch)
}

$nativeB32ProductionApplied = Test-ReversePatch `
    -SourcePath $sourcePath -PatchPath $nativeB32ProductionPatch
if (-not $nativeB32ProductionApplied) {
    Invoke-Checked -Executable git -Arguments @('-C', $sourcePath, 'apply', '--check', $nativeB32ProductionPatch)
    Invoke-Checked -Executable git -Arguments @('-C', $sourcePath, 'apply', $nativeB32ProductionPatch)
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw "vswhere was not found: $vswhere"
}
$visualStudioPath = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if ([string]::IsNullOrWhiteSpace($visualStudioPath)) {
    throw 'No Visual Studio installation with the x64 C++ toolchain was found.'
}

$developerShell = Join-Path $visualStudioPath 'Common7\Tools\Launch-VsDevShell.ps1'
& $developerShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation

New-Item -ItemType Directory -Path $outputPath -Force | Out-Null
$executable = Join-Path $outputPath $ExecutableName
$object = Join-Path $outputPath 'main.obj'
$main = Join-Path $sourcePath 'src\main.cpp'
$include = '/I' + (Join-Path $sourcePath 'Khronos')

Invoke-Checked -Executable cl.exe -Arguments @(
    '/nologo', '/std:c++17', '/O2', '/EHsc', '/MT', '/DNOMINMAX',
    $include, $main, $openClPath,
    ('/Fe:' + $executable), ('/Fo:' + $object)
)

if ($BuildQuickKernelProbe) {
    $probeExecutable = Join-Path $outputPath 'proth20-kernel-probe.exe'
    $probeObject = Join-Path $outputPath 'main-kernel-probe.obj'
    Invoke-Checked -Executable cl.exe -Arguments @(
        '/nologo', '/std:c++17', '/O2', '/EHsc', '/MT', '/DNOMINMAX', '/Dquick_bench',
        $include, $main, $openClPath,
        ('/Fe:' + $probeExecutable), ('/Fo:' + $probeObject)
    )
    $probeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $probeExecutable).Hash
    Write-Host "proth20.kernel_probe.executable=$probeExecutable"
    Write-Host "proth20.kernel_probe.sha256=$probeHash"
}

Copy-Item -LiteralPath (Join-Path $sourcePath 'LICENSE') -Destination (Join-Path $outputPath 'LICENSE-proth20.txt') -Force
$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $executable).Hash
Write-Host "proth20.batch.revision=$revision"
Write-Host "proth20.batch.executable=$executable"
Write-Host "proth20.batch.sha256=$hash"
Write-Host 'proth20.batch.status=PASS'
