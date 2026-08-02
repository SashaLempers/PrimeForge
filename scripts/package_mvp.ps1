[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [string]$Version = '0.1.0-mvp',
    [string]$Executable = 'out\build\msvc-release\primeforge.exe',
    [string]$OutputDirectory = 'out\release'
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if ($Version -notmatch '^[0-9A-Za-z][0-9A-Za-z.-]*$') {
    throw 'Version may contain only ASCII letters, digits, dots and hyphens.'
}

$repositoryRoot = Split-Path -Parent $PSScriptRoot
function Resolve-RepositoryPath([string]$Path) {
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $Path))
}

$resolvedExecutable = Resolve-RepositoryPath $Executable
$releaseRoot = Resolve-RepositoryPath $OutputDirectory
$allowedReleaseRoot = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot 'out\release'))
$allowedPrefix = $allowedReleaseRoot.TrimEnd('\') + '\'
if ($releaseRoot -ne $allowedReleaseRoot -and
    -not $releaseRoot.StartsWith($allowedPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Package output must remain under $allowedReleaseRoot"
}
if (-not (Test-Path -LiteralPath $resolvedExecutable -PathType Leaf)) {
    throw "Release executable not found: $resolvedExecutable"
}

$packageName = "PrimeForge-$Version-windows-x64"
$packageRoot = Join-Path $releaseRoot $packageName
$archivePath = Join-Path $releaseRoot ($packageName + '.zip')
$archiveHashPath = $archivePath + '.sha256'

New-Item -ItemType Directory -Force -Path $releaseRoot | Out-Null
foreach ($knownTarget in @($packageRoot, $archivePath, $archiveHashPath)) {
    $resolvedTarget = [System.IO.Path]::GetFullPath($knownTarget)
    if (-not $resolvedTarget.StartsWith($allowedPrefix, [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to replace target outside the release root: $resolvedTarget"
    }
    if (Test-Path -LiteralPath $resolvedTarget) {
        Remove-Item -LiteralPath $resolvedTarget -Recurse -Force
    }
}

New-Item -ItemType Directory -Path (Join-Path $packageRoot 'docs') -Force | Out-Null
New-Item -ItemType Directory -Path (Join-Path $packageRoot 'licenses') -Force | Out-Null

$copies = @(
    @{ Source = $resolvedExecutable; Destination = 'primeforge.exe' },
    @{ Source = (Join-Path $repositoryRoot 'examples\mvp\search.yaml'); Destination = 'search.yaml' },
    @{ Source = (Join-Path $repositoryRoot 'packaging\README.md'); Destination = 'README.md' },
    @{ Source = (Join-Path $repositoryRoot 'packaging\ORACLES.md'); Destination = 'ORACLES.md' },
    @{ Source = (Join-Path $repositoryRoot 'packaging\run_known_campaign.ps1'); Destination = 'run_known_campaign.ps1' },
    @{ Source = (Join-Path $repositoryRoot 'LICENSE'); Destination = 'LICENSE' },
    @{ Source = (Join-Path $repositoryRoot 'NOTICE'); Destination = 'NOTICE' },
    @{ Source = (Join-Path $repositoryRoot 'LICENSING.md'); Destination = 'LICENSING.md' },
    @{ Source = (Join-Path $repositoryRoot 'THIRD_PARTY_NOTICES.txt'); Destination = 'THIRD_PARTY_NOTICES.txt' },
    @{ Source = (Join-Path $repositoryRoot 'docs\mvp\SEARCH_CONFIG.md'); Destination = 'docs\SEARCH_CONFIG.md' },
    @{ Source = (Join-Path $repositoryRoot 'docs\mvp\RESULTS.md'); Destination = 'docs\RESULTS.md' },
    @{ Source = (Join-Path $repositoryRoot 'docs\mvp\RECOVERY_AND_VERIFICATION.md'); Destination = 'docs\RECOVERY_AND_VERIFICATION.md' },
    @{ Source = (Join-Path $repositoryRoot 'licenses\DISTRIBUTION_MANIFEST.tsv'); Destination = 'licenses\DISTRIBUTION_MANIFEST.tsv' }
)
foreach ($copy in $copies) {
    Copy-Item -LiteralPath $copy.Source -Destination (Join-Path $packageRoot $copy.Destination)
}

$commit = (& git -C $repositoryRoot rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or $commit -notmatch '^[0-9a-f]{40}$') {
    throw 'Unable to record the source commit.'
}
$executableHash = (Get-FileHash -LiteralPath $resolvedExecutable -Algorithm SHA256).Hash.ToLowerInvariant()
$buildInfo = '{' +
    '"commit":"' + $commit + '",' +
    '"executable_sha256":"' + $executableHash + '",' +
    '"package_schema":"primeforge.package.v1",' +
    '"version":"' + $Version + '"}'
$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText((Join-Path $packageRoot 'BUILD_INFO.json'), $buildInfo, $utf8NoBom)

$manifestEntries = @(Get-ChildItem -LiteralPath $packageRoot -Recurse -File | ForEach-Object {
    $relative = $_.FullName.Substring($packageRoot.Length).TrimStart('\', '/').Replace('\', '/')
    $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    "$hash  $relative"
} | Sort-Object)
[System.IO.File]::WriteAllText(
    (Join-Path $packageRoot 'PACKAGE_MANIFEST.sha256'),
    (($manifestEntries -join "`n") + "`n"),
    $utf8NoBom)

& (Join-Path $PSScriptRoot 'verify_mvp_package.ps1') -PackagePath $packageRoot
if ($LASTEXITCODE -ne 0) { throw 'Unpacked package verification failed.' }

Compress-Archive -Path (Join-Path $packageRoot '*') -DestinationPath $archivePath -CompressionLevel Optimal
& (Join-Path $PSScriptRoot 'verify_mvp_package.ps1') -PackagePath $archivePath
if ($LASTEXITCODE -ne 0) { throw 'ZIP package verification failed.' }

$archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
[System.IO.File]::WriteAllText(
    $archiveHashPath,
    ($archiveHash + '  ' + [System.IO.Path]::GetFileName($archivePath) + "`n"),
    $utf8NoBom)

Write-Output "package.directory=$packageRoot"
Write-Output "package.archive=$archivePath"
Write-Output "package.archive_sha256=$archiveHash"
Write-Output "package.archive_hash_file=$archiveHashPath"
Write-Output 'package.status=PASS'
