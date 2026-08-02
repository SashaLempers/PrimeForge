[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param()

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$manifestRoot = Join-Path $repositoryRoot 'tools\oracles'
$installRoot = Join-Path $repositoryRoot 'out\oracles\vcpkg_installed'
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$visualStudioPath = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if ([string]::IsNullOrWhiteSpace($visualStudioPath)) {
    throw 'No Visual Studio installation with the x64 C++ toolchain was found.'
}

$vcpkg = Join-Path $visualStudioPath 'VC\vcpkg\vcpkg.exe'
if (-not (Test-Path -LiteralPath $vcpkg)) {
    throw "The Visual Studio vcpkg executable was not found: $vcpkg"
}

& $vcpkg install --x-manifest-root=$manifestRoot --x-install-root=$installRoot --triplet x64-windows
if ($LASTEXITCODE -ne 0) {
    throw "vcpkg failed with exit code $LASTEXITCODE"
}

Write-Host 'Pinned FLINT oracle dependencies installed successfully.'
