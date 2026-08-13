[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [string]$PortfolioFile = 'docs\reports\CANDIDATE_RANGE_PORTFOLIO.json',
    [string]$SourceDirectory = 'docs\reports\novelty_sources\2026-08-13-exact-k21952207-n33326'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$utf8 = [Text.UTF8Encoding]::new($false)
$invariant = [Globalization.CultureInfo]::InvariantCulture

function Resolve-ProjectPath {
    param([Parameter(Mandatory)][string]$Path)
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $repositoryRoot $Path))
}

function Get-HtmlRows {
    param([Parameter(Mandatory)][string]$Path)
    $html = [IO.File]::ReadAllText($Path)
    foreach ($rowMatch in [regex]::Matches($html, '<tr\b[^>]*>(.*?)</tr>',
            [Text.RegularExpressions.RegexOptions]'IgnoreCase,Singleline')) {
        $cells = @()
        foreach ($cellMatch in [regex]::Matches($rowMatch.Groups[1].Value,
                '<t[dh]\b[^>]*>(.*?)</t[dh]>',
                [Text.RegularExpressions.RegexOptions]'IgnoreCase,Singleline')) {
            $cell = [regex]::Replace($cellMatch.Groups[1].Value, '<[^>]+>', ' ')
            $cell = [Net.WebUtility]::HtmlDecode($cell)
            $cells += ([regex]::Replace($cell, '\s+', ' ')).Trim()
        }
        if ($cells.Count -gt 0) { ,$cells }
    }
}

function Convert-GroupedUnsigned {
    param([AllowNull()][string]$Text)
    if ($null -eq $Text) { return $null }
    $normalized = [regex]::Replace($Text, '[\s.,\u00a0]', '')
    if ($normalized -notmatch '^[0-9]+$') { return $null }
    try { return [uint64]::Parse($normalized, $invariant) }
    catch { return $null }
}

function Test-Overlap {
    param([uint64]$AMin, [uint64]$AMax, [uint64]$BMin, [uint64]$BMax)
    return $AMin -le $BMax -and $BMin -le $AMax
}

$portfolioPath = Resolve-ProjectPath $PortfolioFile
$sourcePath = Resolve-ProjectPath $SourceDirectory
$portfolio = Get-Content -Raw -LiteralPath $portfolioPath | ConvertFrom-Json
$manifest = Get-Content -Raw -LiteralPath (Join-Path $sourcePath 'source-manifest.json') | ConvertFrom-Json
$requiredSources = @(
    'est-proth', 'fermatsearch-done', 'fermatsearch-running',
    'fermatsearch-merged', 'primegrid-pps', 'primegrid-ppse'
)
$sourceRecords = @($manifest.sources | Where-Object { $_.id -in $requiredSources })
$integrityFailures = [Collections.Generic.List[string]]::new()
foreach ($id in $requiredSources) {
    $records = @($sourceRecords | Where-Object id -eq $id)
    if ($records.Count -ne 1 -or $records[0].capture_result -ne 'CAPTURED' -or
        [int]$records[0].http_status -lt 200 -or [int]$records[0].http_status -ge 300) {
        $integrityFailures.Add("${id}:NOT_CAPTURED")
        continue
    }
    $record = $records[0]
    $file = Join-Path $sourcePath ([string]$record.file)
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        $integrityFailures.Add("${id}:MISSING")
        continue
    }
    $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $file).Hash.ToLowerInvariant()
    $actualBytes = [uint64](Get-Item -LiteralPath $file).Length
    if ($actualHash -ne [string]$record.sha256 -or $actualBytes -ne [uint64]$record.bytes) {
        $integrityFailures.Add("${id}:SIZE_OR_SHA256_MISMATCH")
    }
}
if ($integrityFailures.Count -ne 0) {
    throw "Quick-filter evidence failed closed: $($integrityFailures -join ', ')"
}

$estRows = @(Get-HtmlRows (Join-Path $sourcePath 'est-proth.html'))
$fermatRows = @{
    'fermatsearch-done' = @(Get-HtmlRows (Join-Path $sourcePath 'fermatsearch-done.html'))
    'fermatsearch-running' = @(Get-HtmlRows (Join-Path $sourcePath 'fermatsearch-running.html'))
    'fermatsearch-merged' = @(Get-HtmlRows (Join-Path $sourcePath 'fermatsearch-merged.html'))
}
$primeGridRows = @{
    'primegrid-pps' = @(Get-HtmlRows (Join-Path $sourcePath 'primegrid-pps.html'))
    'primegrid-ppse' = @(Get-HtmlRows (Join-Path $sourcePath 'primegrid-ppse.html'))
}

$passCount = 0
$rejectCount = 0
foreach ($range in $portfolio.ranges) {
    $targetMin = [uint64]::Parse([string]$range.k_min, $invariant)
    $targetMax = [uint64]::Parse([string]$range.k_max, $invariant)
    $n = [uint64]$range.n
    $hits = [Collections.Generic.List[object]]::new()

    foreach ($cells in $estRows) {
        if ($cells.Count -lt 3) { continue }
        $maximumN = Convert-GroupedUnsigned $cells[2]
        if ($null -eq $maximumN -or $maximumN -lt $n) { continue }
        $match = [regex]::Match($cells[1], '^\s*([0-9.,\s]+)\s*-\s*([0-9.,\s]+)\s*$')
        if ($match.Success) {
            $fromK = Convert-GroupedUnsigned $match.Groups[1].Value
            $toK = Convert-GroupedUnsigned $match.Groups[2].Value
        } else {
            $fromK = Convert-GroupedUnsigned $cells[1]
            $toK = $fromK
        }
        if ($null -ne $fromK -and $null -ne $toK -and
            (Test-Overlap $targetMin $targetMax $fromK $toK)) {
            $hits.Add([pscustomobject][ordered]@{
                source = 'est-proth'
                n_min = '0'
                n_max = $maximumN.ToString($invariant)
                k_min = $fromK.ToString($invariant)
                k_max = $toK.ToString($invariant)
                source_row = $cells -join ' | '
            })
        }
    }

    foreach ($sourceId in $fermatRows.Keys) {
        foreach ($cells in $fermatRows[$sourceId]) {
            if ($cells.Count -lt 5) { continue }
            $fromN = Convert-GroupedUnsigned $cells[1]
            $toN = Convert-GroupedUnsigned $cells[2]
            $fromK = Convert-GroupedUnsigned $cells[3]
            $toK = Convert-GroupedUnsigned $cells[4]
            if ($null -eq $fromN -or $null -eq $toN -or
                $null -eq $fromK -or $null -eq $toK) { continue }
            if ($fromN -le $n -and $toN -ge $n -and
                (Test-Overlap $targetMin $targetMax $fromK $toK)) {
                $hits.Add([pscustomobject][ordered]@{
                    source = $sourceId
                    n_min = $fromN.ToString($invariant)
                    n_max = $toN.ToString($invariant)
                    k_min = $fromK.ToString($invariant)
                    k_max = $toK.ToString($invariant)
                    source_row = $cells -join ' | '
                })
            }
        }
    }

    foreach ($sourceId in $primeGridRows.Keys) {
        foreach ($cells in $primeGridRows[$sourceId]) {
            if ($cells.Count -lt 8) { continue }
            $fixedK = Convert-GroupedUnsigned $cells[0]
            $maximumN = Convert-GroupedUnsigned $cells[6]
            if ($null -ne $fixedK -and $null -ne $maximumN -and
                $fixedK -ge $targetMin -and $fixedK -le $targetMax -and $maximumN -ge $n) {
                $hits.Add([pscustomobject][ordered]@{
                    source = $sourceId
                    n_min = '0'
                    n_max = $maximumN.ToString($invariant)
                    k_min = $fixedK.ToString($invariant)
                    k_max = $fixedK.ToString($invariant)
                    source_row = $cells -join ' | '
                })
            }
        }
    }

    $range.coverage_reasons = @($hits)
    $range | Add-Member -NotePropertyName coverage_evidence -NotePropertyValue ([pscustomobject][ordered]@{
        level = 'A_QUICK_FILTER'
        checked_source_ids = $requiredSources
        documented_overlap_count = $hits.Count
        limitations = @(
            'This is a structural quick filter, not the full novelty preflight.',
            'Private, deleted, offline and unindexed computations cannot be observed.',
            'ProthSearch, T5K exact queries and broad exact searches remain pending for finalists.'
        )
    }) -Force
    if ($hits.Count -eq 0) {
        $range.coverage_status = 'QUICK_FILTER_PASS'
        $range.gate_status = 'LEVEL_B_PREFLIGHT_PENDING'
        ++$passCount
    } else {
        $range.coverage_status = 'QUICK_FILTER_REJECTED_DOCUMENTED_OVERLAP'
        $range.gate_status = 'REJECTED'
        ++$rejectCount
    }
}

$quickCoverage = [pscustomobject][ordered]@{
    schema = 'primeforge.fast_prime.quick_coverage.v1'
    evaluated_utc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    evidence_directory = $SourceDirectory.Replace('\', '/')
    evidence_manifest_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $sourcePath 'source-manifest.json')).Hash.ToLowerInvariant()
    sources = @($sourceRecords | ForEach-Object {
        [pscustomobject][ordered]@{
            id = $_.id
            url = $_.url
            fetched_utc = $_.fetched_utc
            displayed_update = $_.displayed_update
            http_status = $_.http_status
            bytes = $_.bytes
            sha256 = $_.sha256
        }
    })
    pass_count = $passCount
    rejected_count = $rejectCount
    status = 'QUICK_FILTER_COMPLETE'
}
$portfolio | Add-Member -NotePropertyName quick_coverage -NotePropertyValue $quickCoverage -Force
$json = ($portfolio | ConvertTo-Json -Depth 14) + "`n"
[IO.File]::WriteAllText($portfolioPath, $json, $utf8)

Write-Host "fast_prime.quick_filter.pass=$passCount"
Write-Host "fast_prime.quick_filter.rejected=$rejectCount"
Write-Host "fast_prime.quick_filter.output=$portfolioPath"
Write-Host 'fast_prime.quick_filter.status=PASS'
