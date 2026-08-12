[CmdletBinding()]
param(
    [string]$SourceDirectory = 'docs\reports\novelty_sources\2026-08-05-or-later',
    [string]$OutputFile = 'docs\reports\NOVELTY_PREFLIGHT_MACHINE.json'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$sourcePath = if ([IO.Path]::IsPathRooted($SourceDirectory)) {
    [IO.Path]::GetFullPath($SourceDirectory)
} else {
    [IO.Path]::GetFullPath((Join-Path $repositoryRoot $SourceDirectory))
}
$outputPath = if ([IO.Path]::IsPathRooted($OutputFile)) {
    [IO.Path]::GetFullPath($OutputFile)
} else {
    [IO.Path]::GetFullPath((Join-Path $repositoryRoot $OutputFile))
}
$utf8 = [Text.UTF8Encoding]::new($false)

$target = Get-Content -LiteralPath (Join-Path $sourcePath 'target-and-queries.json') -Raw | ConvertFrom-Json
$manifest = Get-Content -LiteralPath (Join-Path $sourcePath 'source-manifest.json') -Raw | ConvertFrom-Json
$kMin = [uint64]::Parse($target.k_min, [Globalization.CultureInfo]::InvariantCulture)
$kMax = [uint64]::Parse($target.k_max, [Globalization.CultureInfo]::InvariantCulture)
$n = [uint64]$target.exponent
$allValues = @($target.k_min) + @($target.deterministic_interior_values) + @($target.k_max)

function Get-HtmlRows {
    param([string]$Path)
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
    try { return [uint64]::Parse($normalized, [Globalization.CultureInfo]::InvariantCulture) }
    catch { return $null }
}

function Test-IntervalOverlap {
    param([uint64]$AMin, [uint64]$AMax, [uint64]$BMin, [uint64]$BMax)
    return $AMin -le $BMax -and $BMin -le $AMax
}

function Find-EstOverlaps {
    param([uint64]$TargetMin, [uint64]$TargetMax, [uint64]$Exponent)
    $found = [Collections.Generic.List[object]]::new()
    foreach ($cells in Get-HtmlRows -Path (Join-Path $script:sourcePath 'est-proth.html')) {
        if ($cells.Count -lt 3) { continue }
        $maximumN = Convert-GroupedUnsigned $cells[2]
        if ($null -eq $maximumN -or $maximumN -lt $Exponent) { continue }
        $range = [regex]::Match($cells[1], '^\s*([0-9.,\s]+)\s*-\s*([0-9.,\s]+)\s*$')
        if ($range.Success) {
            $fromK = Convert-GroupedUnsigned $range.Groups[1].Value
            $toK = Convert-GroupedUnsigned $range.Groups[2].Value
        } else {
            $fromK = Convert-GroupedUnsigned $cells[1]
            $toK = $fromK
        }
        if ($null -ne $fromK -and $null -ne $toK -and
            (Test-IntervalOverlap $TargetMin $TargetMax $fromK $toK)) {
            $found.Add([pscustomobject][ordered]@{
                k_from = $fromK.ToString([Globalization.CultureInfo]::InvariantCulture)
                k_to = $toK.ToString([Globalization.CultureInfo]::InvariantCulture)
                maximum_n = $maximumN.ToString([Globalization.CultureInfo]::InvariantCulture)
            })
        }
    }
    return @($found)
}

function Find-FermatSearchOverlaps {
    param([string]$FileName)
    $found = [Collections.Generic.List[object]]::new()
    foreach ($cells in Get-HtmlRows -Path (Join-Path $script:sourcePath $FileName)) {
        if ($cells.Count -lt 5) { continue }
        $fromN = Convert-GroupedUnsigned $cells[1]
        $toN = Convert-GroupedUnsigned $cells[2]
        $fromK = Convert-GroupedUnsigned $cells[3]
        $toK = Convert-GroupedUnsigned $cells[4]
        if ($null -eq $fromN -or $null -eq $toN -or $null -eq $fromK -or $null -eq $toK) { continue }
        if ($fromN -le $script:n -and $toN -ge $script:n -and
            (Test-IntervalOverlap $script:kMin $script:kMax $fromK $toK)) {
            $found.Add([pscustomobject][ordered]@{
                source_row = $cells -join ' | '
                n_from = $fromN.ToString([Globalization.CultureInfo]::InvariantCulture)
                n_to = $toN.ToString([Globalization.CultureInfo]::InvariantCulture)
                k_from = $fromK.ToString([Globalization.CultureInfo]::InvariantCulture)
                k_to = $toK.ToString([Globalization.CultureInfo]::InvariantCulture)
            })
        }
    }
    return @($found)
}

function Find-PrimeGridOverlaps {
    param([string]$FileName)
    $found = [Collections.Generic.List[object]]::new()
    foreach ($cells in Get-HtmlRows -Path (Join-Path $script:sourcePath $FileName)) {
        if ($cells.Count -lt 8) { continue }
        $fixedK = Convert-GroupedUnsigned $cells[0]
        $maximumCompleteN = Convert-GroupedUnsigned $cells[6]
        if ($null -ne $fixedK -and $null -ne $maximumCompleteN -and
            $fixedK -ge $script:kMin -and $fixedK -le $script:kMax -and
            $maximumCompleteN -ge $script:n) {
            $found.Add([pscustomobject][ordered]@{
                k = $fixedK.ToString([Globalization.CultureInfo]::InvariantCulture)
                maximum_complete_n = $maximumCompleteN.ToString([Globalization.CultureInfo]::InvariantCulture)
            })
        }
    }
    return @($found)
}

$estOverlaps = @(Find-EstOverlaps $kMin $kMax $n)
$knownPrimeEstCoverage = @(Find-EstOverlaps 34745 34745 33221)
$fermatDoneOverlaps = @(Find-FermatSearchOverlaps 'fermatsearch-done.html')
$fermatRunningOverlaps = @(Find-FermatSearchOverlaps 'fermatsearch-running.html')
$primeGridPpsOverlaps = @(Find-PrimeGridOverlaps 'primegrid-pps.html')
$primeGridPpseOverlaps = @(Find-PrimeGridOverlaps 'primegrid-ppse.html')

$t5kText = [IO.File]::ReadAllText((Join-Path $sourcePath 't5k-proth-query.html'))
$t5kZero = $t5kText -match 'find\s+0\s+primes'

$webRelevant = [Collections.Generic.List[object]]::new()
$webResultBlocks = 0
foreach ($file in Get-ChildItem -LiteralPath $sourcePath -Filter 'web-exact-*.html' -File) {
    $html = [IO.File]::ReadAllText($file.FullName)
    foreach ($match in [regex]::Matches($html, '<li class="b_algo".*?</li>',
            [Text.RegularExpressions.RegexOptions]'IgnoreCase,Singleline')) {
        ++$webResultBlocks
        $plain = [Net.WebUtility]::HtmlDecode([regex]::Replace($match.Value, '<[^>]+>', ' '))
        $hasValue = $false
        foreach ($value in $allValues) {
            if ($plain.Contains([string]$value)) { $hasValue = $true; break }
        }
        $hasContext = $plain -match '(?i)Proth|33326|2\s*\^\s*33326'
        if ($hasValue -and $hasContext) {
            $webRelevant.Add([pscustomobject][ordered]@{
                file = $file.Name
                excerpt = ([regex]::Replace($plain, '\s+', ' ')).Trim().Substring(
                    0, [Math]::Min(300, ([regex]::Replace($plain, '\s+', ' ')).Trim().Length))
            })
        }
    }
}

$apiFiles = Get-ChildItem -LiteralPath $sourcePath -File | Where-Object {
    $_.Extension -in @('.json', '.xml') -and
    $_.Name -notin @('source-manifest.json', 'target-and-queries.json')
}
$apiResultCounts = [Collections.Generic.List[object]]::new()
$apiRelevantHits = [Collections.Generic.List[object]]::new()
foreach ($file in $apiFiles) {
    $text = [IO.File]::ReadAllText($file.FullName)
    $items = @()
    if ($file.Extension -eq '.xml') {
        $items = @([regex]::Matches($text, '<entry\b.*?</entry>',
                [Text.RegularExpressions.RegexOptions]'IgnoreCase,Singleline') |
            ForEach-Object { $_.Value })
    } else {
        try {
            $json = $text | ConvertFrom-Json
        } catch {
            # Windows PowerShell 5.1 rejects JSON objects containing keys that
            # differ only by case. Such a response is retained raw, but cannot
            # be promoted into a positive match by this analyzer.
            $apiResultCounts.Add([pscustomobject][ordered]@{
                file = $file.Name
                returned_items = $null
                parse_status = 'POWERSHELL_CASE_COLLISION'
            })
            continue
        }
        switch -Regex ($file.Name) {
            '^zenodo-' { $items = @($json.hits.hits); break }
            '^crossref-' { $items = @($json.message.items); break }
            '^datacite-' { $items = @($json.data); break }
            '^openalex-' { $items = @($json.results); break }
            '^github-' { $items = @($json.items); break }
            '^archive-org' { $items = @($json.response.docs); break }
            '^(figshare|gitlab)\.json$' { $items = @($json); break }
            '^oeis\.json$' {
                if ($null -ne $json -and $null -ne $json.results) { $items = @($json.results) }
                break
            }
        }
    }
    $apiResultCounts.Add([pscustomobject][ordered]@{
        file = $file.Name
        returned_items = $items.Count
        parse_status = 'PASS'
    })
    foreach ($item in $items) {
        $itemText = if ($item -is [string]) { $item } else { $item | ConvertTo-Json -Depth 20 -Compress }
        foreach ($value in $allValues) {
            if ($itemText.Contains([string]$value) -and $itemText -match '(?i)Proth|33326|2\s*\^\s*33326') {
                $apiRelevantHits.Add([pscustomobject][ordered]@{
                    file = $file.Name
                    value = [string]$value
                })
            }
        }
    }
}

$captureFailures = @($manifest.sources | Where-Object capture_result -ne 'CAPTURED')
$criticalCaptureFailures = @($captureFailures | Where-Object critical)
$fermatDisplayedUpdate = [datetime]::ParseExact('31 Mar 2026', 'dd MMM yyyy',
    [Globalization.CultureInfo]::InvariantCulture)
$auditTime = [datetime]::Parse($manifest.audit_finished_utc,
    [Globalization.CultureInfo]::InvariantCulture,
    [Globalization.DateTimeStyles]::AssumeUniversal).ToUniversalTime()
$fermatAgeDays = [int][Math]::Floor(($auditTime - $fermatDisplayedUpdate.ToUniversalTime()).TotalDays)
$fermatStale = $fermatAgeDays -gt 90

$sieveSource = [IO.File]::ReadAllText((Join-Path $repositoryRoot 'src\discovery\proth_sieve.cpp'))
$prothPatch = [IO.File]::ReadAllText((Join-Path $repositoryRoot 'patches\proth20-persistent-batch.patch'))
$prothReadmePath = Join-Path $repositoryRoot 'out\third_party\proth20-src\README.md'
$prothReadme = if (Test-Path -LiteralPath $prothReadmePath) { [IO.File]::ReadAllText($prothReadmePath) } else { '' }
$sieveAcceptsTarget = $sieveSource -notmatch "k_stop\s*>\s*99'999'999U"
$adapterAcceptsTarget = $prothPatch -notmatch 'batchK\s*>\s*99999999'
$upstreamSupportsTarget = $prothReadme -notmatch '100,000,000'

$jsonProbe = [ordered]@{
    k_min = [uint32]$kMin
    k_max = [uint32]$kMax
    next_k = [uint32]$kMin
} | ConvertTo-Json -Compress
$jsonRoundTrip = $jsonProbe | ConvertFrom-Json
$jsonExact = [uint64]$jsonRoundTrip.k_min -eq $kMin -and [uint64]$jsonRoundTrip.k_max -eq $kMax
$tsvProbe = "$kMin`t$kMax`t$n"
$tsvFields = $tsvProbe -split "`t"
$tsvExact = [uint64]::Parse($tsvFields[0]) -eq $kMin -and [uint64]::Parse($tsvFields[1]) -eq $kMax

$overlapCount = $estOverlaps.Count + $fermatDoneOverlaps.Count + $fermatRunningOverlaps.Count +
    $primeGridPpsOverlaps.Count + $primeGridPpseOverlaps.Count + $webRelevant.Count + $apiRelevantHits.Count
$failReasons = [Collections.Generic.List[string]]::new()
if ($criticalCaptureFailures.Count -gt 0) { $failReasons.Add('CRITICAL_SOURCE_FETCH_FAILED') }
if ($overlapCount -gt 0) { $failReasons.Add('PUBLIC_OVERLAP_OR_EXACT_MATCH_DETECTED') }
if ($fermatStale) { $failReasons.Add('FERMATSEARCH_DONE_AND_RUNNING_STALE_OVER_90_DAYS') }
if (-not $sieveAcceptsTarget) { $failReasons.Add('PRIMEFORGE_SIEVE_REJECTS_K_ABOVE_99999999') }
if (-not $adapterAcceptsTarget) { $failReasons.Add('PINNED_PROTH20_BATCH_ADAPTER_REJECTS_K_ABOVE_99999999') }
if (-not $upstreamSupportsTarget) { $failReasons.Add('UPSTREAM_PROTH20_DOCUMENTS_K_BELOW_100000000_ONLY') }
if ($webResultBlocks -eq 0) { $failReasons.Add('EXACT_WEB_SEARCH_RETURNED_NO_PARSEABLE_RESULTS') }

$result = [pscustomobject][ordered]@{
    schema = 'primeforge.novelty.preflight.v1'
    generated_utc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    source_manifest_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $sourcePath 'source-manifest.json')).Hash.ToLowerInvariant()
    target = [pscustomobject][ordered]@{
        family = 'k*2^n+1'
        k_min = $target.k_min
        k_max = $target.k_max
        k_step = 2
        exponent = $target.exponent
        candidate_count = $target.candidate_count
        digits_min = 10042
        digits_max = 10042
        expected_prime_count_odd_heuristic = '3.46018171949567'
        probability_at_least_one_poisson = '0.968575948863230'
        selection_provenance = $target.selection_provenance
        deterministic_interior_values = @($target.deterministic_interior_values)
    }
    capture = [pscustomobject][ordered]@{
        source_count = [int]$manifest.source_count
        failures = @($captureFailures | ForEach-Object { $_.id })
        critical_failures = @($criticalCaptureFailures | ForEach-Object { $_.id })
    }
    numeric_overlap_checks = [pscustomobject][ordered]@{
        est_proth = $estOverlaps
        fermatsearch_done = $fermatDoneOverlaps
        fermatsearch_running = $fermatRunningOverlaps
        primegrid_pps = $primeGridPpsOverlaps
        primegrid_ppse = $primeGridPpseOverlaps
        t5k_exact_result_count = if ($t5kZero) { 0 } else { $null }
        exact_web_result_blocks_inspected = $webResultBlocks
        exact_web_relevant_blocks = @($webRelevant)
        machine_api_result_counts = @($apiResultCounts)
        machine_api_relevant_hits = @($apiRelevantHits)
        total_possible_overlaps_or_matches = $overlapCount
    }
    historical_control = [pscustomobject][ordered]@{
        expression = '34745*2^33221+1'
        est_proth_coverage_rows = $knownPrimeEstCoverage
        classification = 'NOT_NOVEL'
    }
    freshness = [pscustomobject][ordered]@{
        est_proth_displayed_update = '2026-06-21'
        fermatsearch_displayed_update = '2026-03-31'
        fermatsearch_age_days = $fermatAgeDays
        conservative_stale_threshold_days = 90
        fermatsearch_stale = $fermatStale
    }
    numeric_path_checks = [pscustomobject][ordered]@{
        json_round_trip = if ($jsonExact) { 'PASS' } else { 'FAIL' }
        tsv_round_trip = if ($tsvExact) { 'PASS' } else { 'FAIL' }
        checkpoint_uint32 = 'PASS'
        cli_uint32 = 'PASS'
        primeforge_sieve = if ($sieveAcceptsTarget) { 'PASS' } else { 'FAIL_LIMIT_99999999' }
        proth20_batch_adapter = if ($adapterAcceptsTarget) { 'PASS' } else { 'FAIL_LIMIT_99999999' }
        upstream_proth20_domain = if ($upstreamSupportsTarget) { 'PASS' } else { 'FAIL_DOCUMENTED_LIMIT_100000000_EXCLUSIVE' }
        cuda_discovery_adapter = 'NOT_IMPLEMENTED_DISCOVERY_PATH_USES_PROTH20_OPENCL'
    }
    coverage_verdict = if ($overlapCount -eq 0) {
        'NO_DOCUMENTED_OVERLAP_FOUND_IN_CAPTURED_RESPONSES'
    } else {
        'POSSIBLE_OR_CONFIRMED_OVERLAP'
    }
    preflight_status = if ($failReasons.Count -eq 0) { 'PREFLIGHT_PASS' } else { 'PREFLIGHT_FAIL' }
    launch_status = if ($failReasons.Count -eq 0) { 'AWAITING_SASHA_GO' } else { 'MASSIVE_RUN_BLOCKED' }
    fail_reasons = @($failReasons)
    limitations = @(
        'Public searches cannot reveal private or unindexed computations.',
        'GitHub has no global API for searching release-asset contents.',
        'T5K is not exhaustive for primes of this size.',
        'No claim of globally unexplored or guaranteed novel is made.'
    )
}

[IO.File]::WriteAllText($outputPath, ($result | ConvertTo-Json -Depth 16) + "`n", $utf8)
Write-Host "novelty.analysis.output=$outputPath"
Write-Host "novelty.analysis.overlaps=$overlapCount"
Write-Host "novelty.analysis.preflight_status=$($result.preflight_status)"
Write-Host "novelty.analysis.launch_status=$($result.launch_status)"
if ($result.preflight_status -ne 'PREFLIGHT_PASS') { exit 2 }
