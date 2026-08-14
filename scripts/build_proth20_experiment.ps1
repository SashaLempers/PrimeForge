[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [Parameter(Mandatory = $true)][string]$SourceDirectory,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [string]$ExecutableName = 'proth20-experiment.exe',
    [string]$OpenClLibrary = 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.3\lib\x64\OpenCL.lib'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = Split-Path -Parent $PSScriptRoot
function Resolve-ProjectPath {
    param([Parameter(Mandatory)][string]$Path)
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $repositoryRoot $Path))
}

$sourcePath = Resolve-ProjectPath $SourceDirectory
$outputPath = Resolve-ProjectPath $OutputDirectory
$openClPath = [IO.Path]::GetFullPath($OpenClLibrary)
foreach ($required in @((Join-Path $sourcePath 'src\main.cpp'), (Join-Path $sourcePath 'Khronos'), $openClPath)) {
    if (-not (Test-Path -LiteralPath $required)) { throw "Required experiment input is missing: $required" }
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$visualStudioPath = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if ([string]::IsNullOrWhiteSpace($visualStudioPath)) { throw 'No Visual Studio C++ toolchain found.' }
& (Join-Path $visualStudioPath 'Common7\Tools\Launch-VsDevShell.ps1') -Arch amd64 -HostArch amd64 -SkipAutomaticLocation

[IO.Directory]::CreateDirectory($outputPath) | Out-Null
$executable = Join-Path $outputPath $ExecutableName
$object = Join-Path $outputPath 'main.obj'
& cl.exe /nologo /std:c++17 /O2 /EHsc /MT /DNOMINMAX `
    ('/I' + (Join-Path $sourcePath 'Khronos')) (Join-Path $sourcePath 'src\main.cpp') $openClPath `
    ('/Fe:' + $executable) ('/Fo:' + $object)
if ($LASTEXITCODE -ne 0) { throw "MSVC experiment build failed with exit code $LASTEXITCODE." }

Write-Host "proth20.experiment.executable=$executable"
Write-Host "proth20.experiment.sha256=$((Get-FileHash -Algorithm SHA256 -LiteralPath $executable).Hash.ToLowerInvariant())"
Write-Host 'proth20.experiment.status=PASS'
