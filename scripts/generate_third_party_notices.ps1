# SPDX-License-Identifier: Apache-2.0

[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$manifestPath = Join-Path $repositoryRoot 'licenses\DISTRIBUTION_MANIFEST.tsv'
$outputPath = Join-Path $repositoryRoot 'THIRD_PARTY_NOTICES.txt'

$rows = Import-Csv -LiteralPath $manifestPath -Delimiter "`t"
$redistributedThirdParties = @(
    $rows | Where-Object {
        $_.redistributed -eq 'YES' -and $_.classification -ne 'ORIGINAL'
    }
)

$lines = [System.Collections.Generic.List[string]]::new()
$lines.Add('PrimeForge third-party notices')
$lines.Add('')
$lines.Add('Generated from licenses/DISTRIBUTION_MANIFEST.tsv by scripts/generate_third_party_notices.ps1.')
$lines.Add('')

if ($redistributedThirdParties.Count -eq 0) {
    $lines.Add('No third-party numerical library, runtime component, or external engine is redistributed by this milestone.')
} else {
    foreach ($row in $redistributedThirdParties | Sort-Object component) {
        $lines.Add(('{0}: source={1}; integration={2}; license={3}' -f
            $row.component, $row.source_id, $row.integration, $row.license_file))
    }
}

$lines.Add('')
$lines.Add('Development tools and CI services are not redistributed. Audited sources kept under ignored out/ directories are not part of the distribution.')
$lines.Add('An external-process boundary does not waive or decide third-party license obligations.')
$lines.Add('Exact provenance and decisions are recorded in SOURCES.lock, licenses/DISTRIBUTION_MANIFEST.tsv, and audits/.')
$lines.Add('')

$utf8NoBom = [System.Text.UTF8Encoding]::new($false)
[System.IO.File]::WriteAllText($outputPath, ($lines -join "`n"), $utf8NoBom)

Write-Host "Generated $outputPath"
