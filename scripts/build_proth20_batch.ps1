[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [string]$SourceDirectory = 'out\third_party\proth20-src',
    [string]$OutputDirectory = 'out\oracles\proth20-batch',
    [string]$OpenClLibrary = 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.3\lib\x64\OpenCL.lib'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$expectedRevision = '6771325939a7ceef2c75644c79981c7df4a61882'
$sourceUrl = 'https://github.com/galloty/proth20.git'
$patch = Join-Path $repositoryRoot 'patches\proth20-persistent-batch.patch'

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

if (-not (Test-Path -LiteralPath $patch -PathType Leaf)) {
    throw "Pinned proth20 patch is missing: $patch"
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

& git -C $sourcePath diff --quiet -- src/main.cpp
$sourceChanged = $LASTEXITCODE -ne 0
if (-not $sourceChanged) {
    Invoke-Checked -Executable git -Arguments @('-C', $sourcePath, 'apply', '--check', $patch)
    Invoke-Checked -Executable git -Arguments @('-C', $sourcePath, 'apply', $patch)
} else {
    & git -C $sourcePath apply --reverse --check $patch 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw 'The proth20 source has local changes other than the pinned PrimeForge batch patch.'
    }
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
$executable = Join-Path $outputPath 'proth20-batch.exe'
$object = Join-Path $outputPath 'main.obj'
$main = Join-Path $sourcePath 'src\main.cpp'
$include = '/I' + (Join-Path $sourcePath 'Khronos')

Invoke-Checked -Executable cl.exe -Arguments @(
    '/nologo', '/std:c++17', '/O2', '/EHsc', '/MT', '/DNOMINMAX',
    $include, $main, $openClPath,
    ('/Fe:' + $executable), ('/Fo:' + $object)
)

Copy-Item -LiteralPath (Join-Path $sourcePath 'LICENSE') -Destination (Join-Path $outputPath 'LICENSE-proth20.txt') -Force
$hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $executable).Hash
Write-Host "proth20.batch.revision=$revision"
Write-Host "proth20.batch.executable=$executable"
Write-Host "proth20.batch.sha256=$hash"
Write-Host 'proth20.batch.status=PASS'
