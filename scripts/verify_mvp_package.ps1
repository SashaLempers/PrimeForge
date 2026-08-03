[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [Parameter(Mandatory = $true)]
    [string]$PackagePath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$resolvedPackage = [System.IO.Path]::GetFullPath($PackagePath)
$temporaryRoot = $null

try {
    if ([System.IO.Path]::GetExtension($resolvedPackage) -ieq '.zip') {
        $temporaryRoot = Join-Path ([System.IO.Path]::GetTempPath()) ('primeforge-package-' + [guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $temporaryRoot | Out-Null
        Expand-Archive -LiteralPath $resolvedPackage -DestinationPath $temporaryRoot
        $root = $temporaryRoot
    } elseif (Test-Path -LiteralPath $resolvedPackage -PathType Container) {
        $root = $resolvedPackage
    } else {
        throw "Package path is neither a directory nor a ZIP archive: $resolvedPackage"
    }

    $expectedFiles = @(
        'BUILD_INFO.json',
        'LICENSE',
        'LICENSING.md',
        'NOTICE',
        'ORACLES.md',
        'PACKAGE_MANIFEST.sha256',
        'README.md',
        'THIRD_PARTY_NOTICES.txt',
        'docs/RECOVERY_AND_VERIFICATION.md',
        'docs/RESULTS.md',
        'docs/SEARCH_CONFIG.md',
        'licenses/DISTRIBUTION_MANIFEST.tsv',
        'primeforge.exe',
        'primeforge-launcher.exe',
        'run_known_campaign.ps1',
        'search.yaml'
    )

    $observedFiles = @(Get-ChildItem -LiteralPath $root -Recurse -File | ForEach-Object {
        $_.FullName.Substring($root.Length).TrimStart('\', '/').Replace('\', '/')
    } | Sort-Object)
    $expectedSorted = @($expectedFiles | Sort-Object)
    if (($observedFiles -join "`n") -ne ($expectedSorted -join "`n")) {
        throw "Package inventory differs from the closed MVP allowlist.`nObserved:`n$($observedFiles -join "`n")"
    }

    $binaries = @(Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object {
        $_.Extension -in @('.exe', '.dll', '.lib', '.pdb')
    })
    $binaryNames = @($binaries.Name | Sort-Object)
    if ($binaries.Count -ne 2 -or
        ($binaryNames -join "`n") -ne "primeforge.exe`nprimeforge-launcher.exe") {
        throw 'The package must contain only primeforge.exe and primeforge-launcher.exe.'
    }

    $licenseHash = (Get-FileHash -LiteralPath (Join-Path $root 'LICENSE') -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($licenseHash -ne '04d223ddc28f864ff52adf675d7c02b359af9bd54f9cc074d4491185786f1a6f') {
        throw 'The Apache-2.0 license hash does not match the governed PrimeForge license.'
    }

    $manifestPath = Join-Path $root 'PACKAGE_MANIFEST.sha256'
    $manifestLines = @(Get-Content -LiteralPath $manifestPath -Encoding UTF8)
    $manifestPaths = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
    foreach ($line in $manifestLines) {
        if ($line -notmatch '^([0-9a-f]{64})  (.+)$') {
            throw "Malformed package manifest line: $line"
        }
        $expectedHash = $Matches[1]
        $relative = $Matches[2]
        if ($relative -eq 'PACKAGE_MANIFEST.sha256' -or -not $manifestPaths.Add($relative)) {
            throw "Invalid or duplicate package manifest path: $relative"
        }
        $filePath = Join-Path $root $relative.Replace('/', '\')
        if (-not (Test-Path -LiteralPath $filePath -PathType Leaf)) {
            throw "Package manifest file is missing: $relative"
        }
        $observedHash = (Get-FileHash -LiteralPath $filePath -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($observedHash -ne $expectedHash) {
            throw "Package manifest hash mismatch: $relative"
        }
    }
    $expectedManifestPaths = @($expectedSorted | Where-Object { $_ -ne 'PACKAGE_MANIFEST.sha256' })
    $actualManifestPaths = @($manifestPaths | Sort-Object)
    if (($actualManifestPaths -join "`n") -ne ($expectedManifestPaths -join "`n")) {
        throw 'Package manifest does not cover the exact package inventory.'
    }

    $executable = Join-Path $root 'primeforge.exe'
    Push-Location $root
    try {
        $selftest = @(& $executable selftest 2>&1)
        if ($LASTEXITCODE -ne 0 -or ($selftest -join "`n") -notmatch 'mvp.status=PASS') {
            throw "Packaged PrimeForge self-test failed.`n$($selftest -join "`n")"
        }
        $inspection = @(& $executable inspect --config (Join-Path $root 'search.yaml') 2>&1)
        $unavailableEngines = @($inspection | Where-Object { $_ -match 'engine\..+\.availability=UNAVAILABLE' })
        if ($LASTEXITCODE -ne 0 -or
            ($inspection -join "`n") -notmatch 'inspect.status=PASS' -or
            $unavailableEngines.Count -ne 2) {
            throw "Packaged PrimeForge inspection failed or found bundled engines.`n$($inspection -join "`n")"
        }
    } finally {
        Pop-Location
    }

    $manifestHash = (Get-FileHash -LiteralPath $manifestPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Output "package.files=$($observedFiles.Count)"
    Write-Output "package.manifest_sha256=$manifestHash"
    Write-Output 'package.external_binaries=0'
    Write-Output 'package.status=PASS'
} finally {
    if ($null -ne $temporaryRoot -and (Test-Path -LiteralPath $temporaryRoot)) {
        Remove-Item -LiteralPath $temporaryRoot -Recurse -Force
    }
}
