# SPDX-License-Identifier: Apache-2.0

[CmdletBinding()]
param(
    [ValidateRange(7, 100)]
    [int]$Repetitions = 7,
    [ValidateRange(1, 10)]
    [int]$Warmups = 1,
    [switch]$ValidateOnly,
    [string]$OutputDirectory = '',
    [string]$PrimeForge = 'out/build/msvc-release/primeforge-proth.exe',
    [string]$Proth20 = 'out/oracles/proth20/proth20.exe',
    [string]$Dataset = 'benchmarks/pivot09/proth_u64_reference.tsv'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
function Resolve-RepoFile([string]$Path) {
    $candidate = if ([IO.Path]::IsPathRooted($Path)) { $Path } else { Join-Path $repo $Path }
    return (Resolve-Path -LiteralPath $candidate).Path
}

$primeforgePath = Resolve-RepoFile $PrimeForge
$proth20Path = Resolve-RepoFile $Proth20
$datasetPath = Resolve-RepoFile $Dataset
$monitorPath = Resolve-RepoFile 'out/build/msvc-release/hardware_monitor.exe'
$selftestPath = Resolve-RepoFile 'out/build/msvc-release/primeforge-selftest.exe'

$expectedProth20Hash = '41BBFE6FBCA8976AF9D00C9FC58926BF51FF460D20B9C739B9EF8870FC752C23'
$expectedDatasetHash = '62C3745DFDAB2462E4B013143C65084101C4DA0664D5052B1EA0355AAD60A2AE'
$proth20Hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $proth20Path).Hash
$datasetHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $datasetPath).Hash
if ($proth20Hash -ne $expectedProth20Hash) { throw "proth20 SHA-256 mismatch: $proth20Hash" }
if ($datasetHash -ne $expectedDatasetHash) { throw "dataset SHA-256 mismatch: $datasetHash" }

Push-Location $repo
try {
    $engineChanges = @(git status --porcelain -- CMakeLists.txt include src tests)
    if ($LASTEXITCODE -ne 0) { throw 'git status failed' }
    if ($engineChanges.Count -ne 0) {
        throw 'engine inputs are dirty; commit or revert them before measurement'
    }
    $commit = (git rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $commit.Length -ne 40) { throw 'cannot resolve benchmark commit' }
} finally {
    Pop-Location
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssZ')
    $OutputDirectory = Join-Path $repo "out/benchmarks/pivot09/$stamp"
} elseif (-not [IO.Path]::IsPathRooted($OutputDirectory)) {
    $OutputDirectory = Join-Path $repo $OutputDirectory
}
if (Test-Path -LiteralPath $OutputDirectory) {
    throw "output directory already exists: $OutputDirectory"
}
$output = [IO.Directory]::CreateDirectory($OutputDirectory).FullName
$rawDirectory = [IO.Directory]::CreateDirectory((Join-Path $output 'raw')).FullName
$utf8 = [Text.UTF8Encoding]::new($false)

$rows = @(Import-Csv -Delimiter "`t" -LiteralPath $datasetPath)
if ($rows.Count -ne 16) { throw "expected 16 benchmark cases, found $($rows.Count)" }
$caseIds = [Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
foreach ($row in $rows) {
    if ($row.schema -ne 'primeforge.pivot09.proth-u64.v1' -or
        -not $caseIds.Add($row.case_id) -or
        $row.expected_primality_status -notin @('PROVEN_PRIME', 'COMPOSITE')) {
        throw "invalid benchmark dataset row: $($row.case_id)"
    }
}

function Get-Nanoseconds([Diagnostics.Stopwatch]$Stopwatch) {
    $ticks = [Numerics.BigInteger]$Stopwatch.ElapsedTicks
    $frequency = [Numerics.BigInteger][Diagnostics.Stopwatch]::Frequency
    return [UInt64](($ticks * [Numerics.BigInteger]1000000000) / $frequency)
}

function Get-Median([UInt64[]]$Values) {
    $sorted = @($Values | Sort-Object)
    if ($sorted.Count -eq 0) { throw 'median of empty values' }
    $middle = [int]($sorted.Count / 2)
    if (($sorted.Count % 2) -eq 1) { return [UInt64]$sorted[$middle] }
    return [UInt64](([Numerics.BigInteger]$sorted[$middle - 1] + $sorted[$middle]) / 2)
}

function Write-Tsv([string]$Path, [object[]]$Objects, [string[]]$Columns) {
    $lines = [Collections.Generic.List[string]]::new()
    $lines.Add(($Columns -join "`t"))
    foreach ($object in $Objects) {
        $values = foreach ($column in $Columns) {
            $text = [string]$object.$column
            if ($text.Contains("`t") -or $text.Contains("`r") -or $text.Contains("`n")) {
                throw "TSV value contains a forbidden control character: $column"
            }
            $text
        }
        $lines.Add(($values -join "`t"))
    }
    [IO.File]::WriteAllLines($Path, $lines, $utf8)
}

function Invoke-MonitoredSample([string]$Name) {
    $sample = @(& $monitorPath --once)
    if ($LASTEXITCODE -ne 0 -or $sample.Count -ne 1) { throw 'hardware monitor sample failed' }
    [IO.File]::AppendAllText((Join-Path $output 'telemetry.jsonl'), $sample[0] + "`n", $utf8)
    return $sample[0]
}

function Parse-Verdict([string]$Engine, [string]$Text) {
    if ($Engine -eq 'primeforge') {
        if ($Text -match 'proth\.primality_status=PROVEN_PRIME') { return 'PROVEN_PRIME' }
        if ($Text -match 'proth\.primality_status=COMPOSITE') { return 'COMPOSITE' }
        if ($Text -match 'proth\.primality_status=UNTESTED') { return 'UNTESTED' }
        return 'UNKNOWN'
    }
    if ($Text -match ' is prime,') { return 'PROVEN_PRIME' }
    if ($Text -match ' is composite,' -or $Text -match ' is divisible by ') { return 'COMPOSITE' }
    return 'UNKNOWN'
}

function Invoke-Case(
    [string]$Engine,
    $Row,
    [string]$RunKind,
    [int]$Repetition,
    [int]$Order
) {
    $prefix = '{0}-{1:D2}-{2:D3}-{3}' -f $RunKind, $Repetition, $Order, $Row.case_id
    $stdout = Join-Path $rawDirectory "$prefix-$Engine.stdout.txt"
    $stderr = Join-Path $rawDirectory "$prefix-$Engine.stderr.txt"
    if ($Engine -eq 'primeforge') {
        $file = $primeforgePath
        $arguments = @('--k', $Row.k, '--n', $Row.n, '--max-witness', '255')
    } else {
        $file = $proth20Path
        $arguments = @('-d', '0', '-q', "$($Row.k)*2^$($Row.n)+1")
    }
    $stopwatch = [Diagnostics.Stopwatch]::StartNew()
    $process = Start-Process -FilePath $file -ArgumentList $arguments `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr `
        -WindowStyle Hidden -Wait -PassThru
    $stopwatch.Stop()
    if ($process.ExitCode -ne 0) {
        throw "$Engine failed for $($Row.case_id) with exit code $($process.ExitCode)"
    }
    $text = [IO.File]::ReadAllText($stdout) + [IO.File]::ReadAllText($stderr)
    $verdict = Parse-Verdict $Engine $text
    if ($verdict -ne $Row.expected_primality_status) {
        throw "$Engine disagreement for $($Row.case_id): expected=$($Row.expected_primality_status), actual=$verdict"
    }
    return [PSCustomObject]@{
        schema = 'primeforge.pivot09.raw.v1'
        run_kind = $RunKind
        repetition = $Repetition
        order = $Order
        engine = $Engine
        case_id = $Row.case_id
        k = $Row.k
        n = $Row.n
        expected = $Row.expected_primality_status
        actual = $verdict
        elapsed_nanoseconds = Get-Nanoseconds $stopwatch
        exit_code = $process.ExitCode
        correction_status = 'EXACT_AGREEMENT'
        performance_valid = 'NO'
        performance_claim = 'NONE'
    }
}

$metadata = [ordered]@{
    schema = 'primeforge.pivot09.metadata.v1'
    commit_sha = $commit
    dataset_sha256 = $datasetHash
    primeforge_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $primeforgePath).Hash
    proth20_sha256 = $proth20Hash
    repetitions = $Repetitions
    warmups = $Warmups
    seed = 20260803
    dataset_cases = $rows.Count
    comparison_scope = 'one fresh process per candidate; same inputs and final proof/classification level'
    checkpoint_policy = 'NOT_APPLICABLE_SINGLE_CANDIDATE_INVOCATION'
    cpu_temperature_celsius = 'UNKNOWN'
    cpu_power_watts = 'UNKNOWN'
    energy_joules = 'UNKNOWN'
    performance_valid = 'NO'
    performance_claim = 'NONE'
}
[IO.File]::WriteAllText(
    (Join-Path $output 'metadata.json'),
    ($metadata | ConvertTo-Json -Compress) + "`n",
    $utf8)
[IO.File]::WriteAllLines(
    (Join-Path $output 'selftest.txt'),
    @(& $selftestPath),
    $utf8)

if ($ValidateOnly) {
    [void](Invoke-MonitoredSample 'validation')
    [void](Invoke-Case 'primeforge' $rows[0] 'validation' 0 1)
    [void](Invoke-Case 'proth20' $rows[0] 'validation' 0 2)
    [void](Invoke-Case 'primeforge' $rows[8] 'validation' 0 3)
    [void](Invoke-Case 'proth20' $rows[8] 'validation' 0 4)
    Write-Output "pivot09.validation_output=$output"
    Write-Output 'PrimeForge PIVOT-09 reference comparison validation: PASS'
    return
}

for ($warmup = 1; $warmup -le $Warmups; ++$warmup) {
    $warmRows = @($rows[0], $rows[8])
    $order = 0
    foreach ($engine in @('primeforge', 'proth20')) {
        foreach ($row in $warmRows) {
            ++$order
            [void](Invoke-Case $engine $row 'warmup' $warmup $order)
        }
    }
}

$results = [Collections.Generic.List[object]]::new()
for ($repetition = 1; $repetition -le $Repetitions; ++$repetition) {
    [void](Invoke-MonitoredSample "before-$repetition")
    $items = [Collections.Generic.List[object]]::new()
    foreach ($engine in @('primeforge', 'proth20')) {
        foreach ($row in $rows) {
            $items.Add([PSCustomObject]@{ engine = $engine; row = $row })
        }
    }
    $random = [Random]::new(20260803 + $repetition)
    for ($index = $items.Count - 1; $index -gt 0; --$index) {
        $swap = $random.Next($index + 1)
        $temporary = $items[$index]
        $items[$index] = $items[$swap]
        $items[$swap] = $temporary
    }
    for ($index = 0; $index -lt $items.Count; ++$index) {
        $item = $items[$index]
        $results.Add((Invoke-Case $item.engine $item.row 'measured' $repetition ($index + 1)))
    }
    [void](Invoke-MonitoredSample "after-$repetition")
}

$rawPath = Join-Path $output 'raw.tsv'
Write-Tsv $rawPath $results.ToArray() @(
    'schema', 'run_kind', 'repetition', 'order', 'engine', 'case_id', 'k', 'n',
    'expected', 'actual', 'elapsed_nanoseconds', 'exit_code', 'correction_status',
    'performance_valid', 'performance_claim')

$aggregates = [Collections.Generic.List[object]]::new()
foreach ($engine in @('primeforge', 'proth20')) {
    for ($repetition = 1; $repetition -le $Repetitions; ++$repetition) {
        $selected = @($results | Where-Object {
            $_.engine -eq $engine -and $_.repetition -eq $repetition
        })
        $total = [Numerics.BigInteger]0
        foreach ($result in $selected) { $total += $result.elapsed_nanoseconds }
        $aggregates.Add([PSCustomObject]@{
            schema = 'primeforge.pivot09.aggregate.v1'
            engine = $engine
            repetition = $repetition
            cases = $selected.Count
            total_nanoseconds = [UInt64]$total
            correction_status = 'EXACT_AGREEMENT'
            performance_valid = 'NO'
            performance_claim = 'NONE'
        })
    }
}
$aggregatePath = Join-Path $output 'aggregate.tsv'
Write-Tsv $aggregatePath $aggregates.ToArray() @(
    'schema', 'engine', 'repetition', 'cases', 'total_nanoseconds',
    'correction_status', 'performance_valid', 'performance_claim')

$summary = [Collections.Generic.List[object]]::new()
foreach ($engine in @('primeforge', 'proth20')) {
    [UInt64[]]$values = @($aggregates | Where-Object engine -eq $engine |
        ForEach-Object { [UInt64]$_.total_nanoseconds })
    $median = Get-Median $values
    [UInt64[]]$deviations = @($values | ForEach-Object {
        if ($_ -ge $median) { [UInt64]($_ - $median) } else { [UInt64]($median - $_) }
    })
    $summary.Add([PSCustomObject]@{
        schema = 'primeforge.pivot09.summary.v1'
        engine = $engine
        repetitions = $values.Count
        cases_per_repetition = $rows.Count
        median_total_nanoseconds = $median
        median_absolute_deviation_nanoseconds = (Get-Median $deviations)
        correction_status = 'EXACT_AGREEMENT'
        performance_valid = 'NO'
        performance_claim = 'NONE'
    })
}
$summaryPath = Join-Path $output 'summary.tsv'
Write-Tsv $summaryPath $summary.ToArray() @(
    'schema', 'engine', 'repetitions', 'cases_per_repetition',
    'median_total_nanoseconds', 'median_absolute_deviation_nanoseconds',
    'correction_status', 'performance_valid', 'performance_claim')

$manifestLines = [Collections.Generic.List[string]]::new()
$manifestLines.Add("schema`trelative_path`tsha256")
foreach ($file in Get-ChildItem -LiteralPath $output -Recurse -File | Sort-Object FullName) {
    if ($file.Name -eq 'manifest.tsv') { continue }
    $relative = $file.FullName.Substring($output.Length).TrimStart([char[]]@('\', '/')).Replace('\', '/')
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $file.FullName).Hash
    $manifestLines.Add("primeforge.pivot09.manifest.v1`t$relative`t$hash")
}
[IO.File]::WriteAllLines((Join-Path $output 'manifest.tsv'), $manifestLines, $utf8)

Write-Output "pivot09.output=$output"
Write-Output "pivot09.commit=$commit"
Write-Output "pivot09.cases=$($rows.Count)"
Write-Output "pivot09.repetitions=$Repetitions"
Write-Output "pivot09.exact_agreements=$($results.Count)"
foreach ($row in $summary) {
    Write-Output "pivot09.$($row.engine).median_total_nanoseconds=$($row.median_total_nanoseconds)"
    Write-Output "pivot09.$($row.engine).mad_nanoseconds=$($row.median_absolute_deviation_nanoseconds)"
}
Write-Output 'pivot09.performance_valid=NO'
Write-Output 'pivot09.performance_claim=NONE'
Write-Output 'PrimeForge PIVOT-09 reference comparison: PASS'
