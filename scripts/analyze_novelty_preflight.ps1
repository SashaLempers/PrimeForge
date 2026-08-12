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
$manifestIntegrityFailures = [Collections.Generic.List[string]]::new()
foreach ($source in $manifest.sources) {
    $sourceFile = Join-Path $sourcePath ([string]$source.file)
    if (-not (Test-Path -LiteralPath $sourceFile -PathType Leaf)) {
        $manifestIntegrityFailures.Add("$($source.id):MISSING")
        continue
    }
    $actualBytes = [uint64](Get-Item -LiteralPath $sourceFile).Length
    $actualHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $sourceFile).Hash.ToLowerInvariant()
    if ($actualBytes -ne [uint64]$source.bytes -or $actualHash -ne [string]$source.sha256) {
        $manifestIntegrityFailures.Add("$($source.id):SIZE_OR_SHA256_MISMATCH")
    }
}

function Get-OptionalProperty {
    param(
        [Parameter(Mandatory)]$Object,
        [Parameter(Mandatory)][string]$Name,
        $Fallback
    )
    if ($Object.PSObject.Properties.Name -contains $Name) {
        return $Object.$Name
    }
    return $Fallback
}

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
$fermatLiveOverlaps = @(if (Test-Path -LiteralPath (Join-Path $sourcePath 'fermatsearch-live-range.html')) {
    Find-FermatSearchOverlaps 'fermatsearch-live-range.html'
})
$fermatMergedOverlaps = @(if (Test-Path -LiteralPath (Join-Path $sourcePath 'fermatsearch-merged.html')) {
    Find-FermatSearchOverlaps 'fermatsearch-merged.html'
})
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
        $hasContext = $plain -match ('(?i)Proth|{0}|2\s*\^\s*{0}' -f $n)
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
    $parseStatus = 'PASS'
    if ($file.Extension -eq '.xml') {
        $items = @([regex]::Matches($text, '<entry\b.*?</entry>',
                [Text.RegularExpressions.RegexOptions]'IgnoreCase,Singleline') |
            ForEach-Object { $_.Value })
    } else {
        try {
            $json = $text | ConvertFrom-Json
        } catch {
            # Windows PowerShell 5.1 rejects JSON objects containing keys that
            # differ only by case. JavaScriptSerializer retains case-sensitive
            # dictionary keys and lets the exact result array remain auditable.
            try {
                Add-Type -AssemblyName System.Web.Extensions
                $serializer = [System.Web.Script.Serialization.JavaScriptSerializer]::new()
                $serializer.MaxJsonLength = [int]::MaxValue
                $caseSensitiveJson = $serializer.DeserializeObject($text)
                if ($file.Name -like 'openalex-*.json') {
                    $items = @($caseSensitiveJson['results'])
                } else {
                    throw 'No case-sensitive fallback mapping exists for this response.'
                }
                $parseStatus = 'PASS_CASE_SENSITIVE_FALLBACK'
            } catch {
                $apiResultCounts.Add([pscustomobject][ordered]@{
                    file = $file.Name
                    returned_items = $null
                    parse_status = 'FAIL_JSON_PARSE'
                })
                continue
            }
        }
        if ($parseStatus -eq 'PASS') {
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
    }
    $apiResultCounts.Add([pscustomobject][ordered]@{
        file = $file.Name
        returned_items = $items.Count
        parse_status = $parseStatus
    })
    foreach ($item in $items) {
        $itemText = if ($item -is [string]) { $item } else { $item | ConvertTo-Json -Depth 20 -Compress }
        foreach ($value in $allValues) {
            if ($itemText.Contains([string]$value) -and
                $itemText -match ('(?i)Proth|{0}|2\s*\^\s*{0}' -f $n)) {
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
$fermatDoneRecord = @($manifest.sources | Where-Object id -eq 'fermatsearch-done')
$fermatDisplayedUpdateText = if ($fermatDoneRecord.Count -eq 1 -and
    $fermatDoneRecord[0].PSObject.Properties.Name -contains 'displayed_update') {
    [string]$fermatDoneRecord[0].displayed_update
} else { '2026-03-31' }
$fermatDisplayedUpdate = [datetime]::ParseExact($fermatDisplayedUpdateText, 'yyyy-MM-dd',
    [Globalization.CultureInfo]::InvariantCulture)
$auditTime = [datetime]::Parse($manifest.audit_finished_utc,
    [Globalization.CultureInfo]::InvariantCulture,
    [Globalization.DateTimeStyles]::AssumeUniversal).ToUniversalTime()
$fermatAgeDays = [int][Math]::Floor(($auditTime - $fermatDisplayedUpdate.ToUniversalTime()).TotalDays)
$fermatStale = $fermatAgeDays -gt 90
$fermatLiveRecord = @($manifest.sources | Where-Object id -eq 'fermatsearch-live-range')
$fermatMergedRecord = @($manifest.sources | Where-Object id -eq 'fermatsearch-merged')
$fermatLiveText = if (Test-Path -LiteralPath (Join-Path $sourcePath 'fermatsearch-live-range.html')) {
    [Net.WebUtility]::HtmlDecode([regex]::Replace(
        [IO.File]::ReadAllText((Join-Path $sourcePath 'fermatsearch-live-range.html')), '<[^>]+>', ' '))
} else { '' }
$fermatLiveEchoesExponent = $fermatLiveText -match
    ('(?i)example\s+of\s+research\s+for\s+N\s*=\s*{0}\s+to\s+{0}' -f $n)
$fermatLiveQueryConfirmed = $fermatLiveRecord.Count -eq 1 -and
    $fermatLiveRecord[0].capture_result -eq 'CAPTURED' -and $fermatLiveEchoesExponent
$fermatMergedCaptured = $fermatMergedRecord.Count -eq 1 -and
    $fermatMergedRecord[0].capture_result -eq 'CAPTURED'
$fermatStaleCorroborated = $fermatStale -and $fermatLiveQueryConfirmed -and
    $fermatMergedCaptured -and $fermatLiveOverlaps.Count -eq 0 -and
    $fermatMergedOverlaps.Count -eq 0

$sieveSource = [IO.File]::ReadAllText((Join-Path $repositoryRoot 'src\discovery\proth_sieve.cpp'))
$prothPatch = [IO.File]::ReadAllText((Join-Path $repositoryRoot 'patches\proth20-persistent-batch.patch'))
$prothReadmePath = Join-Path $repositoryRoot 'out\third_party\proth20-src\README.md'
$prothReadme = if (Test-Path -LiteralPath $prothReadmePath) { [IO.File]::ReadAllText($prothReadmePath) } else { '' }
$sieveHasPinnedLimit = $sieveSource -match "k_stop\s*>\s*99'999'999U"
$adapterHasPinnedLimit = $prothPatch -match 'batchK\s*>\s*99999999'
$upstreamDocumentsPinnedLimit = $prothReadme -match '100,000,000'
$sieveAcceptsTarget = $sieveHasPinnedLimit -and $kMax -le 99999999
$adapterAcceptsTarget = $adapterHasPinnedLimit -and $kMax -le 99999999
$upstreamSupportsTarget = $upstreamDocumentsPinnedLimit -and $kMax -lt 100000000

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
$candidateCountFallback = (($kMax - $kMin) / 2) + 1
$midpoint = ([double]$kMin + [double]$kMax) / 2.0
$naturalLog = [Math]::Log($midpoint) + [double]$n * [Math]::Log(2.0)
$expectedPrimeCountFallback = [double]$candidateCountFallback * 2.0 / $naturalLog
$digitsMinFallback = [int][Math]::Floor([Math]::Log10([double]$kMin) +
    [double]$n * [Math]::Log10(2.0)) + 1
$digitsMaxFallback = [int][Math]::Floor([Math]::Log10([double]$kMax) +
    [double]$n * [Math]::Log10(2.0)) + 1
$digitsMin = [int](Get-OptionalProperty $target 'digits_min' $digitsMinFallback)
$digitsMax = [int](Get-OptionalProperty $target 'digits_max' $digitsMaxFallback)
$expectedPrimeCount = [string](Get-OptionalProperty $target 'expected_prime_count_odd_heuristic' `
    $expectedPrimeCountFallback.ToString('0.000000000000000', [Globalization.CultureInfo]::InvariantCulture))
$probabilityAtLeastOne = [string](Get-OptionalProperty $target 'probability_at_least_one_poisson' `
    (1.0 - [Math]::Exp(-$expectedPrimeCountFallback)).ToString('0.000000000000000',
        [Globalization.CultureInfo]::InvariantCulture))

$overlapCount = $estOverlaps.Count + $fermatDoneOverlaps.Count + $fermatRunningOverlaps.Count +
    $fermatLiveOverlaps.Count + $fermatMergedOverlaps.Count +
    $primeGridPpsOverlaps.Count + $primeGridPpseOverlaps.Count + $webRelevant.Count + $apiRelevantHits.Count
$failReasons = [Collections.Generic.List[string]]::new()
if ($manifestIntegrityFailures.Count -gt 0) { $failReasons.Add('EVIDENCE_MANIFEST_INTEGRITY_FAILED') }
if ($criticalCaptureFailures.Count -gt 0) { $failReasons.Add('CRITICAL_SOURCE_FETCH_FAILED') }
if ($overlapCount -gt 0) { $failReasons.Add('PUBLIC_OVERLAP_OR_EXACT_MATCH_DETECTED') }
if ($fermatStale -and -not $fermatStaleCorroborated) {
    $failReasons.Add('FERMATSEARCH_STALE_WITHOUT_LIVE_DYNAMIC_CORROBORATION')
}
if (-not $fermatLiveQueryConfirmed) { $failReasons.Add('FERMATSEARCH_LIVE_RANGE_QUERY_AMBIGUOUS') }
if (-not $jsonExact -or -not $tsvExact -or $kMax -gt [uint32]::MaxValue) {
    $failReasons.Add('NUMERIC_TRANSPORT_ROUND_TRIP_FAILED')
}
if (-not $sieveAcceptsTarget) { $failReasons.Add('PRIMEFORGE_SIEVE_REJECTS_K_ABOVE_99999999') }
if (-not $adapterAcceptsTarget) { $failReasons.Add('PINNED_PROTH20_BATCH_ADAPTER_REJECTS_K_ABOVE_99999999') }
if (-not $upstreamSupportsTarget) { $failReasons.Add('UPSTREAM_PROTH20_DOCUMENTS_K_BELOW_100000000_ONLY') }
if ($webResultBlocks -eq 0) { $failReasons.Add('EXACT_WEB_SEARCH_RETURNED_NO_PARSEABLE_RESULTS') }
if (@($apiResultCounts | Where-Object parse_status -like 'FAIL_*').Count -gt 0) {
    $failReasons.Add('MACHINE_API_RESPONSE_AMBIGUOUS')
}

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
        digits_min = $digitsMin
        digits_max = $digitsMax
        expected_prime_count_odd_heuristic = $expectedPrimeCount
        probability_at_least_one_poisson = $probabilityAtLeastOne
        selection_provenance = $target.selection_provenance
        selection_seed = Get-OptionalProperty $target 'seed' ''
        selection_seed_sha256 = Get-OptionalProperty $target 'seed_sha256' ''
        deterministic_interior_values = @($target.deterministic_interior_values)
    }
    capture = [pscustomobject][ordered]@{
        source_count = [int]$manifest.source_count
        failures = @($captureFailures | ForEach-Object { $_.id })
        critical_failures = @($criticalCaptureFailures | ForEach-Object { $_.id })
        manifest_integrity_failures = @($manifestIntegrityFailures)
    }
    numeric_overlap_checks = [pscustomobject][ordered]@{
        est_proth = $estOverlaps
        fermatsearch_done = $fermatDoneOverlaps
        fermatsearch_running = $fermatRunningOverlaps
        fermatsearch_live_range = $fermatLiveOverlaps
        fermatsearch_merged = $fermatMergedOverlaps
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
        fermatsearch_displayed_update = $fermatDisplayedUpdate.ToString('yyyy-MM-dd')
        fermatsearch_age_days = $fermatAgeDays
        conservative_stale_threshold_days = 90
        fermatsearch_stale = $fermatStale
        live_range_query_echoed_exponent = $fermatLiveEchoesExponent
        live_range_query_confirmed = $fermatLiveQueryConfirmed
        merged_table_captured = $fermatMergedCaptured
        stale_page_corroborated = $fermatStaleCorroborated
    }
    numeric_path_checks = [pscustomobject][ordered]@{
        json_round_trip = if ($jsonExact) { 'PASS' } else { 'FAIL' }
        tsv_round_trip = if ($tsvExact) { 'PASS' } else { 'FAIL' }
        checkpoint_uint32 = if ($kMax -le [uint32]::MaxValue) { 'PASS' } else { 'FAIL' }
        cli_uint32 = if ($kMax -le [uint32]::MaxValue) { 'PASS' } else { 'FAIL' }
        primeforge_sieve = if ($sieveAcceptsTarget) { 'PASS' } else { 'FAIL_LIMIT_99999999' }
        proth20_batch_adapter = if ($adapterAcceptsTarget) { 'PASS' } else { 'FAIL_LIMIT_99999999' }
        upstream_proth20_domain = if ($upstreamSupportsTarget) { 'PASS' } else { 'FAIL_DOCUMENTED_LIMIT_100000000_EXCLUSIVE' }
        cuda_discovery_adapter = 'NOT_IMPLEMENTED_DISCOVERY_PATH_USES_PROTH20_OPENCL'
    }
    coverage_verdict = if ($overlapCount -eq 0 -and $failReasons.Count -eq 0) {
        'PUBLICLY_UNCOVERED_CANDIDATE'
    } elseif ($overlapCount -eq 0) {
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
        'The FermatSearch live range endpoint and merged table share the same operator database; their agreement corroborates but does not erase the displayed-date limitation.',
        'No claim of globally unexplored or guaranteed novel is made.'
    )
}

[IO.File]::WriteAllText($outputPath, ($result | ConvertTo-Json -Depth 16) + "`n", $utf8)
Write-Host "novelty.analysis.output=$outputPath"
Write-Host "novelty.analysis.overlaps=$overlapCount"
Write-Host "novelty.analysis.preflight_status=$($result.preflight_status)"
Write-Host "novelty.analysis.launch_status=$($result.launch_status)"
if ($result.preflight_status -ne 'PREFLIGHT_PASS') { exit 2 }
