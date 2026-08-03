[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [switch]$Clean,
    [string]$ToolkitRoot = 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.3'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $repositoryRoot 'out\build\msvc-cuda-release'

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [string[]]$Arguments = @()
    )

    Write-Host "> $Executable $($Arguments -join ' ')"
    & $Executable @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $Executable"
    }
}

if ($Clean -and (Test-Path -LiteralPath $buildDirectory)) {
    $resolvedRoot = [System.IO.Path]::GetFullPath($repositoryRoot).TrimEnd('\') + '\'
    $resolvedBuild = [System.IO.Path]::GetFullPath($buildDirectory)
    if (-not $resolvedBuild.StartsWith($resolvedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove path outside repository: $resolvedBuild"
    }
    Write-Host "Removing known CUDA build directory: $resolvedBuild"
    Remove-Item -LiteralPath $resolvedBuild -Recurse -Force
}

$nvcc = Join-Path $ToolkitRoot 'bin\nvcc.exe'
$sanitizer = Join-Path $ToolkitRoot 'compute-sanitizer\compute-sanitizer.exe'
foreach ($requiredTool in @($nvcc, $sanitizer)) {
    if (-not (Test-Path -LiteralPath $requiredTool -PathType Leaf)) {
        throw "Pinned CUDA tool was not found: $requiredTool"
    }
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere -PathType Leaf)) {
    throw "vswhere was not found at $vswhere"
}
$visualStudioPath = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if ([string]::IsNullOrWhiteSpace($visualStudioPath)) {
    throw 'No Visual Studio installation with the x64 C++ toolchain was found.'
}

$developerShell = Join-Path $visualStudioPath 'Common7\Tools\Launch-VsDevShell.ps1'
$cmake = Join-Path $visualStudioPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = Join-Path $visualStudioPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
foreach ($requiredTool in @($developerShell, $cmake, $ctest)) {
    if (-not (Test-Path -LiteralPath $requiredTool -PathType Leaf)) {
        throw "Required Visual Studio tool was not found: $requiredTool"
    }
}

& $developerShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation

Push-Location $repositoryRoot
try {
    Invoke-Checked -Executable $cmake -Arguments @(
        '--preset', 'msvc-cuda-release',
        "-DPRIMEFORGE_CUDA_ROOT:PATH=$ToolkitRoot"
    )
    Invoke-Checked -Executable $cmake -Arguments @(
        '--build', '--preset', 'msvc-cuda-release', '--parallel'
    )
    Invoke-Checked -Executable $ctest -Arguments @(
        '--preset', 'msvc-cuda-release', '--output-on-failure'
    )

    $validator = Join-Path $buildDirectory 'primeforge-cuda-validation.exe'
    if (-not (Test-Path -LiteralPath $validator -PathType Leaf)) {
        throw "CUDA validator was not produced: $validator"
    }
    Invoke-Checked -Executable $validator
    Invoke-Checked -Executable $sanitizer -Arguments @(
        '--tool', 'memcheck', '--error-exitcode', '99', $validator
    )
} finally {
    Pop-Location
}

Write-Host 'PrimeForge optional CUDA validation: PASS'
