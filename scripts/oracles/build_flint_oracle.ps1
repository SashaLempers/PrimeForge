[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [string]$InstallRoot = 'out\oracles\vcpkg_installed'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$resolvedInstallRoot = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $InstallRoot))
$tripletRoot = Join-Path $resolvedInstallRoot 'x64-windows'
$source = Join-Path $repositoryRoot 'tools\oracles\flint_primality_oracle.cpp'
$outputDirectory = Join-Path $repositoryRoot 'out\oracles\flint'
$output = Join-Path $outputDirectory 'flint-primality-oracle.exe'

if (-not (Test-Path -LiteralPath (Join-Path $tripletRoot 'include\flint\fmpz.h'))) {
    throw "FLINT headers were not found under $tripletRoot"
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$visualStudioPath = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if ([string]::IsNullOrWhiteSpace($visualStudioPath)) {
    throw 'No Visual Studio installation with the x64 C++ toolchain was found.'
}

$developerShell = Join-Path $visualStudioPath 'Common7\Tools\Launch-VsDevShell.ps1'
& $developerShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation

New-Item -ItemType Directory -Force -Path $outputDirectory | Out-Null

$arguments = @(
    '/nologo', '/std:c++latest', '/EHsc', '/W4', '/WX', '/permissive-', '/utf-8', '/O2',
    '/external:W0', "/external:I$($tripletRoot)\include", $source,
    '/link', "/LIBPATH:$($tripletRoot)\lib", 'flint.lib', 'gmp.lib', 'mpfr.lib',
    "/OUT:$output"
)
& cl.exe @arguments
if ($LASTEXITCODE -ne 0) {
    throw "FLINT oracle compilation failed with exit code $LASTEXITCODE"
}

Copy-Item -LiteralPath (Join-Path $tripletRoot 'bin\flint-24.dll') -Destination $outputDirectory -Force
Copy-Item -LiteralPath (Join-Path $tripletRoot 'bin\gmp-10.dll') -Destination $outputDirectory -Force
Copy-Item -LiteralPath (Join-Path $tripletRoot 'bin\mpfr-6.dll') -Destination $outputDirectory -Force
Copy-Item -LiteralPath (Join-Path $tripletRoot 'bin\pthreadVC3.dll') -Destination $outputDirectory -Force

& $output --version
if ($LASTEXITCODE -ne 0) {
    throw 'The FLINT oracle did not start successfully.'
}

Write-Host "FLINT oracle built at $output"
