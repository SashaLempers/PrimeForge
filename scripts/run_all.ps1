[CmdletBinding()]
param(
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$knownBuildDirectories = @(
    (Join-Path $repositoryRoot 'out\build\msvc-debug'),
    (Join-Path $repositoryRoot 'out\build\msvc-release')
)

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

if ($Clean) {
    $resolvedRoot = [System.IO.Path]::GetFullPath($repositoryRoot).TrimEnd('\') + '\'
    foreach ($directory in $knownBuildDirectories) {
        $resolvedDirectory = [System.IO.Path]::GetFullPath($directory)
        if (-not $resolvedDirectory.StartsWith($resolvedRoot, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove path outside repository: $resolvedDirectory"
        }
        if (Test-Path -LiteralPath $resolvedDirectory) {
            Write-Host "Removing known build directory: $resolvedDirectory"
            Remove-Item -LiteralPath $resolvedDirectory -Recurse -Force
        }
    }
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "vswhere was not found at $vswhere"
}

$visualStudioPath = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
if ([string]::IsNullOrWhiteSpace($visualStudioPath)) {
    throw 'No Visual Studio installation with the x64 C++ toolchain was found.'
}

$developerShell = Join-Path $visualStudioPath 'Common7\Tools\Launch-VsDevShell.ps1'
if (-not (Test-Path -LiteralPath $developerShell)) {
    throw "Visual Studio developer shell was not found: $developerShell"
}
& $developerShell -Arch amd64 -HostArch amd64 -SkipAutomaticLocation

$cmake = Join-Path $visualStudioPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = Join-Path $visualStudioPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
if (-not (Test-Path -LiteralPath $cmake) -or -not (Test-Path -LiteralPath $ctest)) {
    throw 'The Visual Studio bundled CMake/CTest executables were not found.'
}

Push-Location $repositoryRoot
try {
    foreach ($preset in @('msvc-debug', 'msvc-release')) {
        Invoke-Checked -Executable $cmake -Arguments @('--preset', $preset)
        Invoke-Checked -Executable $cmake -Arguments @('--build', '--preset', $preset, '--parallel')
        Invoke-Checked -Executable $ctest -Arguments @('--preset', $preset, '--output-on-failure')

        $selftest = Join-Path $repositoryRoot "out\build\$preset\primeforge-selftest.exe"
        if (-not (Test-Path -LiteralPath $selftest)) {
            throw "Self-test executable was not produced: $selftest"
        }
        Invoke-Checked -Executable $selftest
    }
} finally {
    Pop-Location
}

Write-Host 'PrimeForge complete local verification: PASS'
