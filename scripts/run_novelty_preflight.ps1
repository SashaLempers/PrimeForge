[CmdletBinding()]
param(
    [string]$OutputDirectory = 'docs\reports\novelty_sources\2026-08-05-or-later',
    [uint64]$KMin = 1227250535,
    [uint64]$KMax = 1227330535,
    [uint32]$Exponent = 33326,
    [string]$SelectionProvenance = 'EXTERNALLY_SELECTED; the repository contains no reproducible seed-to-range mapping.',
    [string]$SelectionSeed = 'PrimeForge|SashaLempers|2026-08-05|547ee8566eac612756ceff9e50fea4c56c225d96'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$outputPath = if ([System.IO.Path]::IsPathRooted($OutputDirectory)) {
    [System.IO.Path]::GetFullPath($OutputDirectory)
} else {
    [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $OutputDirectory))
}
$reportsRoot = [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot 'docs\reports\novelty_sources'))
if (-not $outputPath.StartsWith($reportsRoot + [System.IO.Path]::DirectorySeparatorChar,
        [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Output directory must remain below $reportsRoot"
}

$curl = (Get-Command curl.exe -ErrorAction Stop).Source
$gh = (Get-Command gh.exe -ErrorAction SilentlyContinue)
$utf8 = [System.Text.UTF8Encoding]::new($false)
$records = [System.Collections.Generic.List[object]]::new()
$startedUtc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')

if (Test-Path -LiteralPath $outputPath) {
    $existing = @(Get-ChildItem -LiteralPath $outputPath -Force)
    if ($existing.Count -ne 0) {
        throw "Evidence directory must be new or empty: $outputPath"
    }
} else {
    New-Item -ItemType Directory -Path $outputPath | Out-Null
}

function Write-Utf8NoBom {
    param([string]$Path, [string]$Content)
    [System.IO.File]::WriteAllText($Path, $Content, $script:utf8)
}

function Get-FileEvidence {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) {
        return [pscustomobject][ordered]@{ bytes = 0; sha256 = $null }
    }
    $item = Get-Item -LiteralPath $Path
    return [pscustomobject][ordered]@{
        bytes = [uint64]$item.Length
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
    }
}

function Invoke-HttpSnapshot {
    param(
        [string]$Id,
        [string]$Url,
        [string]$FileName,
        [string]$Query,
        [bool]$Critical = $false,
        [string]$Method = 'GET',
        [string]$Body = '',
        [string]$ContentType = 'application/x-www-form-urlencoded'
    )

    $rawPath = Join-Path $script:outputPath $FileName
    $arguments = @(
        '--silent', '--show-error', '--location', '--max-time', '90',
        '--connect-timeout', '20', '--retry', '2', '--retry-delay', '1',
        '--user-agent', 'PrimeForge-Novelty-Preflight/1.0',
        '--output', $rawPath, '--write-out', '%{http_code}',
        '--request', $Method
    )
    if ($Body.Length -gt 0) {
        # Windows PowerShell 5.1 can remove embedded JSON quotes when handing a
        # native process a --data-raw argument. A retained request body avoids
        # that ambiguity and makes the exact POST reproducible.
        $requestPath = $rawPath + '.request'
        Write-Utf8NoBom -Path $requestPath -Content $Body
        $arguments += @('--header', "Content-Type: $ContentType", '--data-binary', "@$requestPath")
    }
    $arguments += @('--', $Url)

    $fetchedUtc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    $previousErrorActionPreference = $ErrorActionPreference
    try {
        # Native failures are evidence to record. They must not bypass the
        # critical/non-critical policy through PowerShell's native stderr bridge.
        $ErrorActionPreference = 'Continue'
        $httpText = & $script:curl @arguments
        $curlExit = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    $httpStatus = 0
    if ($httpText -match '([0-9]{3})\s*$') { $httpStatus = [int]$Matches[1] }
    $evidence = Get-FileEvidence -Path $rawPath
    $result = if ($curlExit -ne 0) {
        'FETCH_ERROR'
    } elseif ($httpStatus -lt 200 -or $httpStatus -ge 300) {
        'HTTP_ERROR'
    } elseif ($evidence.bytes -eq 0) {
        'EMPTY_RESPONSE'
    } else {
        'CAPTURED'
    }

    $script:records.Add([pscustomobject][ordered]@{
        id = $Id
        source_type = 'http'
        url = $Url
        query = $Query
        fetched_utc = $fetchedUtc
        http_status = $httpStatus
        command_exit_code = $curlExit
        displayed_update = $null
        bytes = $evidence.bytes
        sha256 = $evidence.sha256
        critical = $Critical
        capture_result = $result
        file = $FileName
        limitation = $null
    })
}

function Invoke-GitHubSnapshot {
    param(
        [string]$Id,
        [string]$Endpoint,
        [string]$Query,
        [string]$FileName
    )

    $rawPath = Join-Path $script:outputPath $FileName
    $fetchedUtc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    if ($null -eq $script:gh) {
        Write-Utf8NoBom -Path $rawPath -Content '{"error":"gh CLI unavailable"}'
        $exitCode = 127
    } else {
        $text = [Collections.Generic.List[string]]::new()
        $exitCode = 1
        for ($attempt = 1; $attempt -le 3; ++$attempt) {
            $previousErrorActionPreference = $ErrorActionPreference
            try {
                # GitHub search occasionally returns 5xx responses. Keep those
                # diagnostics and retry, but never turn this non-critical source
                # into an unhandled PowerShell exception.
                $ErrorActionPreference = 'Continue'
                $attemptText = @(& $script:gh.Source api --method GET $Endpoint `
                    -f "q=$Query" -f 'per_page=100' 2>&1)
                $exitCode = $LASTEXITCODE
            } finally {
                $ErrorActionPreference = $previousErrorActionPreference
            }
            foreach ($line in $attemptText) { $text.Add([string]$line) }
            if ($exitCode -eq 0) { break }
            if ($attempt -lt 3) { Start-Sleep -Seconds $attempt }
        }
        Write-Utf8NoBom -Path $rawPath -Content (($text -join "`n") + "`n")
    }
    $evidence = Get-FileEvidence -Path $rawPath
    $script:records.Add([pscustomobject][ordered]@{
        id = $Id
        source_type = 'github_rest_api'
        url = "https://api.github.com/$Endpoint"
        query = $Query
        fetched_utc = $fetchedUtc
        http_status = if ($exitCode -eq 0) { 200 } else { 0 }
        command_exit_code = $exitCode
        displayed_update = $null
        bytes = $evidence.bytes
        sha256 = $evidence.sha256
        critical = $false
        capture_result = if ($exitCode -eq 0) { 'CAPTURED' } else { 'FETCH_ERROR' }
        file = $FileName
        limitation = 'GitHub search omits private repositories, unindexed large files and deleted history; no global Releases search endpoint exists.'
    })
}

if ($KMin -lt 3 -or $KMin -gt $KMax -or ($KMin -band 1) -eq 0 -or
    ($KMax -band 1) -eq 0 -or (($KMax - $KMin) % 2) -ne 0) {
    throw 'KMin and KMax must be ordered odd integers with an even difference.'
}
if ($KMax -gt [uint32]::MaxValue) {
    throw 'The current discovery transport requires KMax to fit uint32 exactly.'
}
if ($Exponent -lt 32) { throw 'Exponent must be at least 32.' }

$kMin = $KMin
$kMax = $KMax
$n = $Exponent
$candidateCount = (($kMax - $kMin) / 2) + 1
$interior = [System.Collections.Generic.List[uint64]]::new()
for ($sample = 1; $sample -le 16; ++$sample) {
    $index = [uint64][Math]::Floor(($sample * ([double]$candidateCount - 1.0)) / 17.0)
    $interior.Add($kMin + 2 * $index)
}
$allValues = @($kMin) + @($interior) + @($kMax)
$seed = $SelectionSeed
$seedBytes = $utf8.GetBytes($seed)
$sha = [System.Security.Cryptography.SHA256]::Create()
try {
    $seedHash = -join ($sha.ComputeHash($seedBytes) | ForEach-Object { $_.ToString('x2') })
} finally {
    $sha.Dispose()
}

$midpoint = ([double]$kMin + [double]$kMax) / 2.0
$naturalLog = [Math]::Log($midpoint) + [double]$n * [Math]::Log(2.0)
$expectedPrimeCount = [double]$candidateCount * 2.0 / $naturalLog
$probabilityAtLeastOne = 1.0 - [Math]::Exp(-$expectedPrimeCount)
$digitsMin = [int][Math]::Floor([Math]::Log10([double]$kMin) +
    [double]$n * [Math]::Log10(2.0)) + 1
$digitsMax = [int][Math]::Floor([Math]::Log10([double]$kMax) +
    [double]$n * [Math]::Log10(2.0)) + 1

$target = [pscustomobject][ordered]@{
    schema = 'primeforge.novelty.target.v1'
    audit_started_utc = $startedUtc
    family = 'k*2^n+1'
    k_min = $kMin.ToString([Globalization.CultureInfo]::InvariantCulture)
    k_max = $kMax.ToString([Globalization.CultureInfo]::InvariantCulture)
    k_step = 2
    exponent = $n
    candidate_count = $candidateCount.ToString([Globalization.CultureInfo]::InvariantCulture)
    deterministic_interior_values = @($interior | ForEach-Object { $_.ToString([Globalization.CultureInfo]::InvariantCulture) })
    digits_min = $digitsMin
    digits_max = $digitsMax
    expected_prime_count_odd_heuristic = $expectedPrimeCount.ToString('0.000000000000000', [Globalization.CultureInfo]::InvariantCulture)
    probability_at_least_one_poisson = $probabilityAtLeastOne.ToString('0.000000000000000', [Globalization.CultureInfo]::InvariantCulture)
    interior_sampling_algorithm = 'For j=1..16: index=floor(j*(candidate_count-1)/17), k=k_min+2*index.'
    selection_provenance = $SelectionProvenance
    seed = $seed
    seed_sha256 = $seedHash
}
Write-Utf8NoBom -Path (Join-Path $outputPath 'target-and-queries.json') `
    -Content (($target | ConvertTo-Json -Depth 8) + "`n")

# Primary coverage sources. These snapshots are critical to the fail-closed gate.
Invoke-HttpSnapshot -Id 'est-proth' -Url 'https://pzktupel.de/Primetables/Table_EST_Proth.php' `
    -FileName 'est-proth.html' -Query 'Full exhaustive-coverage table; numeric interval comparison' -Critical $true
Invoke-HttpSnapshot -Id 'fermatsearch-done' -Url 'https://www.fermatsearch.org/stat/done.php' `
    -FileName 'fermatsearch-done.html' -Query 'Full completed-work table; numeric interval comparison' -Critical $true
Invoke-HttpSnapshot -Id 'fermatsearch-running' -Url 'https://www.fermatsearch.org/stat/running.php' `
    -FileName 'fermatsearch-running.html' -Query 'Full reserved-work table; numeric interval comparison' -Critical $true
$fermatRangeBody = 'nMin={0}&nMax={0}' -f $n
Invoke-HttpSnapshot -Id 'fermatsearch-live-range' -Url 'https://www.fermatsearch.org/stat/range.php' `
    -FileName 'fermatsearch-live-range.html' `
    -Query "Dynamic current-database range query for n=$n" -Method 'POST' `
    -Body $fermatRangeBody -Critical $true
Invoke-HttpSnapshot -Id 'fermatsearch-merged' -Url 'https://www.fermatsearch.org/stat/merge.php' `
    -FileName 'fermatsearch-merged.html' `
    -Query 'Full merged completed and reserved-work table; numeric interval comparison' -Critical $true
Invoke-HttpSnapshot -Id 'primegrid-home' -Url 'https://www.primegrid.com/' `
    -FileName 'primegrid-home.html' -Query 'Current official project page' -Critical $false
Invoke-HttpSnapshot -Id 'primegrid-subprojects' -Url 'https://www.primegrid.com/server_status_subprojects.php' `
    -FileName 'primegrid-subprojects.html' -Query 'Current official PPS and PPSE status' -Critical $true
Invoke-HttpSnapshot -Id 'primegrid-pps' -Url 'https://www.primegrid.com/stats_pps_llr.php' `
    -FileName 'primegrid-pps.html' -Query 'Full official PPS k table; numeric comparison' -Critical $true
Invoke-HttpSnapshot -Id 'primegrid-ppse' -Url 'https://www.primegrid.com/stats_ppse_llr.php' `
    -FileName 'primegrid-ppse.html' -Query 'Full official PPSE k table; numeric comparison' -Critical $true
Invoke-HttpSnapshot -Id 'proth20-pinned-readme' `
    -Url 'https://raw.githubusercontent.com/galloty/proth20/6771325939a7ceef2c75644c79981c7df4a61882/README.md' `
    -FileName 'proth20-pinned-readme.md' `
    -Query 'Pinned upstream engine domain at revision 6771325939a7ceef2c75644c79981c7df4a61882' `
    -Critical $true

$t5kBody = 'base=2&min_k={0}&max_k={1}&min_n={2}&max_n={2}&plus=on&number=100&search=Start+Search' -f `
    $kMin, $kMax, $n
Invoke-HttpSnapshot -Id 't5k-proth-query' -Url 'https://t5k.org/primes/search_proth.php' `
    -FileName 't5k-proth-query.html' -Query $t5kBody -Method 'POST' -Body $t5kBody -Critical $false

# Exact, deterministic web queries cover both bounds, all 16 interior values, forms and interval spelling.
$webQueries = [System.Collections.Generic.List[string]]::new()
foreach ($value in $allValues) {
    $webQueries.Add(('"{0}" "{1}" Proth' -f $value, $n))
}
$webQueries.Add(('"{0}*2^{1}+1"' -f $kMin, $n))
$webQueries.Add(('"{0}*2^{1}+1"' -f $kMax, $n))
$webQueries.Add(('"{0} × 2^{1} + 1"' -f $kMin, $n))
$webQueries.Add(('"{0}.2^{1}+1"' -f $kMax, $n))
$webQueries.Add(('"{0}..{1}" Proth' -f $kMin, $kMax))
$webQueries.Add(('"{0}" "{1}" Proth' -f `
    $kMin.ToString('N0', [Globalization.CultureInfo]::GetCultureInfo('en-US')), `
    $kMax.ToString('N0', [Globalization.CultureInfo]::GetCultureInfo('en-US'))))
$webQueries.Add(('"{0}" "{1}" Proth' -f `
    $kMin.ToString('N0', [Globalization.CultureInfo]::GetCultureInfo('de-DE')), `
    $kMax.ToString('N0', [Globalization.CultureInfo]::GetCultureInfo('de-DE'))))
$webQueries.Add(('site:github.com/releases "{0}" Proth' -f $kMin))
$webQueries.Add(('site:mersenneforum.org "{0}" Proth' -f $kMin))
$webQueries.Add(('site:prothsearch.com "{0}" Proth' -f $kMin))
$webQueries.Add(('site:rieselprime.de "{0}" Proth' -f $n))
$webQueries.Add(('site:oeis.org "{0}"' -f $kMin))
$webQueries.Add(('site:arxiv.org "{0}"' -f $kMin))
$webQueries.Add(('site:osf.io "{0}"' -f $kMin))
$webQueries.Add(('site:figshare.com "{0}"' -f $kMin))
$webQueries.Add(('site:gitlab.com "{0}" Proth' -f $kMin))
$webQueries.Add(('site:sourceforge.net "{0}" Proth' -f $kMin))
$webQueries.Add(('site:archive.org "{0}" Proth' -f $kMin))

$queryIndex = 0
foreach ($query in $webQueries) {
    ++$queryIndex
    $encoded = [Uri]::EscapeDataString($query)
    Invoke-HttpSnapshot -Id ('web-exact-{0:d2}' -f $queryIndex) `
        -Url "https://www.bing.com/search?q=$encoded&count=50&setlang=en-US" `
        -FileName ('web-exact-{0:d2}.html' -f $queryIndex) -Query $query -Critical $false
}

# Primary scholarly/repository APIs. Four query shapes are retained independently.
$apiQueries = @(
    [string]$kMin,
    [string]$kMax,
    "$n Proth",
    "$kMin $kMax $n Proth"
)
$apiIndex = 0
foreach ($query in $apiQueries) {
    ++$apiIndex
    $encoded = [Uri]::EscapeDataString($query)
    Invoke-HttpSnapshot -Id ('zenodo-{0:d2}' -f $apiIndex) `
        -Url "https://zenodo.org/api/records?q=$encoded&size=25" `
        -FileName ('zenodo-{0:d2}.json' -f $apiIndex) -Query $query -Critical $false
    Invoke-HttpSnapshot -Id ('crossref-{0:d2}' -f $apiIndex) `
        -Url "https://api.crossref.org/works?query=$encoded&rows=100" `
        -FileName ('crossref-{0:d2}.json' -f $apiIndex) -Query $query -Critical $false
    Invoke-HttpSnapshot -Id ('datacite-{0:d2}' -f $apiIndex) `
        -Url "https://api.datacite.org/dois?query=$encoded&page%5Bsize%5D=100" `
        -FileName ('datacite-{0:d2}.json' -f $apiIndex) -Query $query -Critical $false
    Invoke-HttpSnapshot -Id ('openalex-{0:d2}' -f $apiIndex) `
        -Url "https://api.openalex.org/works?search=$encoded&per-page=100" `
        -FileName ('openalex-{0:d2}.json' -f $apiIndex) -Query $query -Critical $false
}

# Other machine-searchable public collections named by the audit specification.
$exactMin = [Uri]::EscapeDataString([string]$kMin)
Invoke-HttpSnapshot -Id 'oeis-api' -Url "https://oeis.org/search?fmt=json&q=$exactMin" `
    -FileName 'oeis.json' -Query ([string]$kMin) -Critical $false
Invoke-HttpSnapshot -Id 'arxiv-api' -Url "https://export.arxiv.org/api/query?search_query=all%3A$exactMin&max_results=100" `
    -FileName 'arxiv.xml' -Query "all:$kMin" -Critical $false
Invoke-HttpSnapshot -Id 'gitlab-api' -Url "https://gitlab.com/api/v4/projects?search=$exactMin&simple=true&per_page=100" `
    -FileName 'gitlab.json' -Query "public project metadata search=$kMin" -Critical $false
Invoke-HttpSnapshot -Id 'archive-api' `
    -Url "https://archive.org/advancedsearch.php?q=%22$exactMin%22&fl%5B%5D=identifier%2Ctitle&rows=100&page=1&output=json" `
    -FileName 'archive-org.json' -Query ('"{0}"' -f $kMin) -Critical $false
$figshareBody = '{"search_for":"' + $kMin + '","page_size":100}'
Invoke-HttpSnapshot -Id 'figshare-api' -Url 'https://api.figshare.com/v2/articles/search' `
    -FileName 'figshare.json' -Query "search_for=$kMin" -Method 'POST' `
    -Body $figshareBody -ContentType 'application/json' -Critical $false

# Authenticated GitHub REST searches. The API has no global release-content search.
$githubQueries = @(
    ('"{0}" "{1}" Proth' -f $kMin, $n),
    ('"{0}" "{1}" Proth' -f $kMax, $n),
    ('"{0}*2^{1}+1"' -f $kMin, $n),
    ('"{0}*2^{1}+1"' -f $kMax, $n)
)
$githubIndex = 0
foreach ($query in $githubQueries) {
    ++$githubIndex
    Invoke-GitHubSnapshot -Id ('github-code-{0:d2}' -f $githubIndex) -Endpoint 'search/code' `
        -Query $query -FileName ('github-code-{0:d2}.json' -f $githubIndex)
    Invoke-GitHubSnapshot -Id ('github-commits-{0:d2}' -f $githubIndex) -Endpoint 'search/commits' `
        -Query $query -FileName ('github-commits-{0:d2}.json' -f $githubIndex)
    Invoke-GitHubSnapshot -Id ('github-issues-{0:d2}' -f $githubIndex) -Endpoint 'search/issues' `
        -Query $query -FileName ('github-issues-{0:d2}.json' -f $githubIndex)
}

# Complete the evidence fields required by the audit even when a source does
# not publish its own update timestamp.
foreach ($record in $records) {
    $record.displayed_update = 'NOT_DISPLAYED'
    if ($null -eq $record.limitation) {
        $record.limitation = 'Public response only; private, deleted, unindexed or unpublished computations are not observable.'
    }
    switch ($record.id) {
        'est-proth' {
            $record.displayed_update = '2026-06-21'
            $record.limitation = 'Aggregated public exhaustive-coverage table; not evidence about private computations.'
        }
        'fermatsearch-done' {
            $record.displayed_update = '2026-03-31'
            $record.limitation = 'Displayed update is stale at capture time; not a complete Proth-primality registry.'
        }
        'fermatsearch-running' {
            $record.displayed_update = '2026-03-31'
            $record.limitation = 'Displayed reservation list is stale at capture time.'
        }
        'fermatsearch-live-range' {
            $record.displayed_update = '2026-03-31'
            $record.limitation = 'Live POST query executed at fetched_utc against the operator database; the shared displayed update remains stale.'
        }
        'fermatsearch-merged' {
            $record.displayed_update = '2026-03-31'
            $record.limitation = 'Merged operator table is current only to its displayed update; used as corroboration, not proof of private work.'
        }
        'primegrid-pps' {
            $record.limitation = 'Current public fixed-k table; does not expose private work or every historical artifact.'
        }
        'primegrid-ppse' {
            $record.limitation = 'Current public fixed-k table; does not expose private work or every historical artifact.'
        }
        't5k-proth-query' {
            $record.limitation = 'Database covers the 5,000 largest known primes plus selected categories; absence is weak evidence.'
        }
        'proth20-pinned-readme' {
            $record.displayed_update = 'PINNED_GIT_REVISION_6771325939A7CEEF2C75644C79981C7DF4A61882'
            $record.limitation = 'Documents the supported engine domain only; it is not coverage evidence.'
        }
    }
    if ($record.id -like 'web-exact-*') {
        $record.limitation = 'Search-engine indexing is incomplete and result ranking is not an exhaustive registry.'
    }
    $record | Add-Member -NotePropertyName semantic_result `
        -NotePropertyValue 'CAPTURED_FOR_DETERMINISTIC_ANALYSIS'
}

$finishedUtc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
$manifest = [pscustomobject][ordered]@{
    schema = 'primeforge.novelty.source_manifest.v1'
    audit_started_utc = $startedUtc
    audit_finished_utc = $finishedUtc
    target_file = 'target-and-queries.json'
    source_count = $records.Count
    sources = @($records)
}
Write-Utf8NoBom -Path (Join-Path $outputPath 'source-manifest.json') `
    -Content (($manifest | ConvertTo-Json -Depth 12) + "`n")

$failedCritical = @($records | Where-Object { $_.critical -and $_.capture_result -ne 'CAPTURED' })
Write-Host "novelty.audit.output=$outputPath"
Write-Host "novelty.audit.sources=$($records.Count)"
Write-Host "novelty.audit.critical_fetch_failures=$($failedCritical.Count)"
Write-Host "novelty.audit.started_utc=$startedUtc"
Write-Host "novelty.audit.finished_utc=$finishedUtc"
if ($failedCritical.Count -gt 0) {
    Write-Host 'novelty.audit.capture_status=FAIL'
    exit 2
}
Write-Host 'novelty.audit.capture_status=PASS'
