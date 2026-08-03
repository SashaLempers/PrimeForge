[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [Parameter(Mandatory = $true)][string]$BaselineTree,
    [Parameter(Mandatory = $true)][string]$BaselineExecutable,
    [Parameter(Mandatory = $true)][string]$CandidateTree,
    [Parameter(Mandatory = $true)][string]$CandidateExecutable,
    [string]$WatchdogExecutable = 'out\build\msvc-cuda-release\benchmark_watchdog.exe',
    [string]$HardwareMonitorExecutable = 'out\build\msvc-cuda-release\hardware_monitor.exe',
    [string]$ProfileRelativePath = 'benchmarks\profiles\full_u64_high_32768.yaml',
    [string]$CampaignRelativePath = 'out\benchmarks\optimization-07\current-campaign',
    [string]$OutputDirectory = 'out\benchmarks\optimization-09\compact-evidence-ab',
    [ValidateRange(15, 15)][int]$Pairs = 15,
    [ValidateRange(1, 1)][int]$WarmupPairs = 1,
    [ValidateRange(1, 32767)][int]$StopAfterCandidates = 16384,
    [int]$Seed = 20260805,
    [switch]$ValidateOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = [IO.Path]::GetFullPath((Split-Path -Parent $PSScriptRoot))
$utf8NoBom = [Text.UTF8Encoding]::new($false)
$invariant = [Globalization.CultureInfo]::InvariantCulture
$expectedPrpBackend = 'primeforge.auto.cpu-cuda.base2-strong-prp-u64.v1[min=512]'

function Resolve-AbsolutePath {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Base
    )
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $Base $Path))
}

function Assert-PathInside {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Role
    )
    $fullPath = [IO.Path]::GetFullPath($Path)
    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $prefix = $fullRoot + [IO.Path]::DirectorySeparatorChar
    if (-not $fullPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Role must remain inside $fullRoot"
    }
}

function Assert-NoReparseAncestor {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Root,
        [Parameter(Mandatory = $true)][string]$Role
    )
    Assert-PathInside $Path $Root $Role
    $fullRoot = [IO.Path]::GetFullPath($Root).TrimEnd('\', '/')
    $current = [IO.Path]::GetFullPath($Path)
    while ($true) {
        if (Test-Path -LiteralPath $current) {
            $item = Get-Item -LiteralPath $current -Force
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "$Role traverses a reparse point: $current"
            }
        }
        if ($current.Equals($fullRoot, [StringComparison]::OrdinalIgnoreCase)) { break }
        $parent = [IO.Path]::GetDirectoryName($current)
        if ([string]::IsNullOrEmpty($parent) -or
            -not ($parent.Equals($fullRoot, [StringComparison]::OrdinalIgnoreCase) -or
                  $parent.StartsWith(
                      $fullRoot + [IO.Path]::DirectorySeparatorChar,
                      [StringComparison]::OrdinalIgnoreCase))) {
            throw "$Role escaped its trusted root while checking reparse points."
        }
        $current = $parent.TrimEnd('\', '/')
    }
}

function Get-SafeCampaignFiles {
    param(
        [Parameter(Mandatory = $true)][string]$Campaign,
        [Parameter(Mandatory = $true)][string]$Tree
    )
    Assert-NoReparseAncestor $Campaign $Tree 'Campaign directory'
    if (-not (Test-Path -LiteralPath $Campaign -PathType Container)) {
        throw "Campaign directory is absent: $Campaign"
    }
    $campaignRoot = [IO.Path]::GetFullPath($Campaign).TrimEnd('\', '/')
    $pending = [Collections.Generic.Stack[string]]::new()
    $files = [Collections.Generic.List[IO.FileInfo]]::new()
    $pending.Push($campaignRoot)
    while ($pending.Count -ne 0) {
        $directory = $pending.Pop()
        foreach ($item in Get-ChildItem -LiteralPath $directory -Force) {
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Campaign contains a reparse point: $($item.FullName)"
            }
            if ($item.PSIsContainer) {
                $pending.Push($item.FullName)
            } else {
                $files.Add([IO.FileInfo]$item)
            }
        }
    }
    foreach ($required in @(
        'results.jsonl', 'campaign.checkpoint.json', 'MANIFEST.sha256',
        'coverage_report.json', 'search.yaml'
    )) {
        $path = [IO.Path]::GetFullPath((Join-Path $campaignRoot $required))
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Required campaign artifact is absent: $required"
        }
    }
    return @($files)
}

function Remove-ValidatedCampaign {
    param(
        [Parameter(Mandatory = $true)][string]$Campaign,
        [Parameter(Mandatory = $true)][string]$Tree
    )
    [void](Get-SafeCampaignFiles $Campaign $Tree)
    Remove-Item -LiteralPath ([IO.Path]::GetFullPath($Campaign)) -Recurse -Force
}

function Move-ValidatedCampaign {
    param(
        [Parameter(Mandatory = $true)][string]$Campaign,
        [Parameter(Mandatory = $true)][string]$Tree,
        [Parameter(Mandatory = $true)][string]$Destination,
        [Parameter(Mandatory = $true)][string]$DestinationRoot
    )
    [void](Get-SafeCampaignFiles $Campaign $Tree)
    Assert-PathInside $Destination $DestinationRoot 'Campaign archive destination'
    if (Test-Path -LiteralPath $Destination) {
        throw "Campaign archive destination already exists: $Destination"
    }
    Move-Item -LiteralPath ([IO.Path]::GetFullPath($Campaign)) -Destination $Destination
}

$baselineTreePath = [IO.Path]::GetFullPath($BaselineTree)
$candidateTreePath = [IO.Path]::GetFullPath($CandidateTree)
$baselineExePath = Resolve-AbsolutePath $BaselineExecutable $baselineTreePath
$candidateExePath = Resolve-AbsolutePath $CandidateExecutable $candidateTreePath
$watchdogPath = Resolve-AbsolutePath $WatchdogExecutable $candidateTreePath
$hardwareMonitorPath = Resolve-AbsolutePath $HardwareMonitorExecutable $candidateTreePath
$outputRoot = Resolve-AbsolutePath $OutputDirectory $repositoryRoot
$optimizationRoot = [IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot 'out\benchmarks\optimization-09')
)
Assert-PathInside $outputRoot $optimizationRoot 'Optimization-09 output'

foreach ($directory in @($baselineTreePath, $candidateTreePath)) {
    if (-not (Test-Path -LiteralPath $directory -PathType Container)) {
        throw "Benchmark tree is absent: $directory"
    }
}
foreach ($file in @($baselineExePath, $candidateExePath, $watchdogPath, $hardwareMonitorPath)) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "Required executable is absent: $file"
    }
}
if (Test-Path -LiteralPath $outputRoot) {
    throw "Output directory already exists: $outputRoot"
}

function Get-GitTreeIdentity {
    param([Parameter(Mandatory = $true)][string]$Tree)
    $commit = (& git -C $Tree rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0 -or $commit -notmatch '^[0-9a-f]{40}$') {
        throw "Cannot resolve Git identity for $Tree"
    }
    $status = @(& git -C $Tree status --porcelain --untracked-files=all)
    if ($LASTEXITCODE -ne 0) { throw "Cannot inspect Git status for $Tree" }
    if ($status.Count -ne 0) { throw "Benchmark tree is dirty: $Tree" }
    return [pscustomobject][ordered]@{ commit = $commit; clean = 'YES' }
}

function Get-PropertyValue {
    param(
        [AllowNull()][object]$Object,
        [Parameter(Mandatory = $true)][string]$Name
    )
    if ($null -eq $Object) { return $null }
    $property = $Object.PSObject.Properties[$Name]
    if ($null -eq $property) { return $null }
    return $property.Value
}

function Convert-ToDouble {
    param([Parameter(Mandatory = $true)][object]$Value)
    if ($Value -is [string]) {
        return [double]::Parse(
            [string]$Value, [Globalization.NumberStyles]::Float, $invariant
        )
    }
    return [Convert]::ToDouble($Value, $invariant)
}

function Write-Json {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][object]$Value,
        [int]$Depth = 12
    )
    [IO.File]::WriteAllText(
        $Path, (($Value | ConvertTo-Json -Depth $Depth -Compress) + "`n"), $utf8NoBom
    )
}

function Convert-ToInvariantCsvLines {
    param([Parameter(Mandatory = $true)][object[]]$Rows)
    $normalized = @(
        foreach ($row in $Rows) {
            $properties = [ordered]@{}
            foreach ($property in $row.PSObject.Properties) {
                $value = $property.Value
                if ($value -is [double]) {
                    $value = $value.ToString('R', $invariant)
                } elseif ($value -is [single]) {
                    $value = $value.ToString('R', $invariant)
                } elseif ($value -is [decimal]) {
                    $value = $value.ToString('G29', $invariant)
                }
                $properties[$property.Name] = $value
            }
            [pscustomobject]$properties
        }
    )
    return @($normalized | ConvertTo-Csv -NoTypeInformation)
}

function Write-InvariantCsv {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][object[]]$Rows
    )
    $lines = @(Convert-ToInvariantCsvLines $Rows)
    [IO.File]::WriteAllText($Path, (($lines -join "`n") + "`n"), $utf8NoBom)
}

function Write-JsonLines {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][object[]]$Rows
    )
    $lines = @($Rows | ForEach-Object { $_ | ConvertTo-Json -Depth 12 -Compress })
    [IO.File]::WriteAllText($Path, (($lines -join "`n") + "`n"), $utf8NoBom)
}

function Get-Median {
    param([Parameter(Mandatory = $true)][double[]]$Values)
    if ($Values.Count -eq 0) { throw 'Cannot compute a median of an empty sample.' }
    $ordered = @($Values | Sort-Object)
    $middle = [int][Math]::Floor($ordered.Count / 2)
    if ($ordered.Count % 2 -eq 1) { return [double]$ordered[$middle] }
    return ([double]$ordered[$middle - 1] + [double]$ordered[$middle]) / 2.0
}

function Get-Mad {
    param([Parameter(Mandatory = $true)][double[]]$Values)
    $median = Get-Median $Values
    return Get-Median ([double[]]@($Values | ForEach-Object {
        [Math]::Abs($_ - $median)
    }))
}

function Get-BootstrapMedianInterval {
    param(
        [Parameter(Mandatory = $true)][double[]]$Values,
        [Parameter(Mandatory = $true)][Random]$Random
    )
    if ($Values.Count -eq 0) { throw 'Cannot bootstrap an empty paired sample.' }
    $medians = [double[]]::new(20000)
    for ($sample = 0; $sample -lt $medians.Count; ++$sample) {
        $resample = [double[]]::new($Values.Count)
        for ($index = 0; $index -lt $resample.Count; ++$index) {
            $resample[$index] = $Values[$Random.Next($Values.Count)]
        }
        $medians[$sample] = Get-Median $resample
    }
    [Array]::Sort($medians)
    $lowIndex = [int][Math]::Floor(0.025 * ($medians.Count - 1))
    $highIndex = [int][Math]::Ceiling(0.975 * ($medians.Count - 1))
    return [pscustomobject][ordered]@{
        low = [double]$medians[$lowIndex]
        high = [double]$medians[$highIndex]
    }
}

function Get-KeyValues {
    param([Parameter(Mandatory = $true)][string]$Path)
    $result = @{}
    foreach ($line in Get-Content -LiteralPath $Path) {
        $separator = $line.IndexOf('=')
        if ($separator -gt 0) {
            $result[$line.Substring(0, $separator)] = $line.Substring($separator + 1)
        }
    }
    return $result
}

function Get-BytesSha256 {
    param([Parameter(Mandatory = $true)][AllowEmptyCollection()][byte[]]$Bytes)
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha256.ComputeHash($Bytes))).Replace('-', '').ToLowerInvariant()
    } finally {
        $sha256.Dispose()
    }
}

function Convert-HexToBytes {
    param([Parameter(Mandatory = $true)][string]$Hex)
    if ($Hex.Length % 2 -ne 0 -or $Hex -notmatch '^[0-9a-f]*$') {
        throw 'FLINT journal field is not canonical lowercase hexadecimal.'
    }
    $bytes = [byte[]]::new($Hex.Length / 2)
    for ($index = 0; $index -lt $bytes.Length; ++$index) {
        $bytes[$index] = [Convert]::ToByte($Hex.Substring($index * 2, 2), 16)
    }
    return ,$bytes
}

function Resolve-CampaignArtifact {
    param(
        [Parameter(Mandatory = $true)][string]$Campaign,
        [Parameter(Mandatory = $true)][string]$RelativePath
    )
    if ([IO.Path]::IsPathRooted($RelativePath)) {
        throw "Campaign artifact path is absolute: $RelativePath"
    }
    $path = [IO.Path]::GetFullPath((Join-Path $Campaign $RelativePath))
    Assert-PathInside $path $Campaign 'Campaign artifact'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        throw "Campaign artifact is absent: $RelativePath"
    }
    return $path
}

function Get-EngineProjection {
    param(
        [AllowNull()][object]$Evidence,
        [Parameter(Mandatory = $true)][string]$Campaign,
        [byte[]]$JournalBytes,
        [AllowNull()][string]$ExpectedJobId,
        [AllowNull()][string]$ExpectedInput,
        [ref]$ExpectedJournalOffset
    )
    if ($null -eq $Evidence) { return $null }
    $rawLogPath = Get-PropertyValue $Evidence 'raw_log_path'
    $stdoutBytes = $null
    $stderrBytes = $null
    if ($null -ne $rawLogPath) {
        if ([string]$rawLogPath -ne 'external/flint/evidence.jsonl') {
            throw "Unexpected FLINT journal path: $rawLogPath"
        }
        if ($null -ne (Get-PropertyValue $Evidence 'raw_stdout_path') -or
            $null -ne (Get-PropertyValue $Evidence 'raw_stderr_path')) {
            throw 'FLINT journal evidence must not also reference per-result files.'
        }
        $offsetText = Get-PropertyValue $Evidence 'raw_log_offset'
        $lengthText = Get-PropertyValue $Evidence 'raw_log_length'
        if ($null -eq $offsetText -or $null -eq $lengthText) {
            throw 'FLINT journal reference is incomplete.'
        }
        foreach ($decimal in @([string]$offsetText, [string]$lengthText)) {
            if ($decimal -notmatch '^(0|[1-9][0-9]*)$') {
                throw 'FLINT journal offset or length is not canonical decimal.'
            }
        }
        $offset = [uint64]$offsetText
        $length = [uint64]$lengthText
        if ($length -eq 0 -or $offset -ne [uint64]$ExpectedJournalOffset.Value -or
            $offset -gt [uint64]$JournalBytes.Length -or
            $length -gt [uint64]$JournalBytes.Length - $offset -or
            $length -gt [uint64][int]::MaxValue) {
            throw 'FLINT journal slices are not contiguous and bounded.'
        }
        $slice = [byte[]]::new([int]$length)
        [Buffer]::BlockCopy($JournalBytes, [int]$offset, $slice, 0, [int]$length)
        $line = [Text.Encoding]::ASCII.GetString($slice)
        if (-not $line.EndsWith("`n", [StringComparison]::Ordinal) -or
            $line.Contains("`r")) {
            throw 'FLINT journal slice is not canonical LF JSONL.'
        }
        $journalRecord = $line | ConvertFrom-Json
        $jobBytes = Convert-HexToBytes ([string]$journalRecord.job_id_hex)
        $inputBytes = Convert-HexToBytes ([string]$journalRecord.input_hex)
        $stdoutBytes = Convert-HexToBytes ([string]$journalRecord.stdout_hex)
        $stderrBytes = Convert-HexToBytes ([string]$journalRecord.stderr_hex)
        $jobId = [Text.Encoding]::UTF8.GetString($jobBytes)
        $input = [Text.Encoding]::UTF8.GetString($inputBytes)
        if ($null -ne $ExpectedJobId -and $jobId -ne $ExpectedJobId) {
            throw "FLINT journal job mismatch: expected $ExpectedJobId, got $jobId"
        }
        if ($null -ne $ExpectedInput -and $input -ne $ExpectedInput) {
            throw "FLINT journal input mismatch for $ExpectedJobId"
        }
        $ExpectedJournalOffset.Value = $offset + $length
    } else {
        $stdoutRelative = Get-PropertyValue $Evidence 'raw_stdout_path'
        $stderrRelative = Get-PropertyValue $Evidence 'raw_stderr_path'
        if ($null -eq $stdoutRelative -or $null -eq $stderrRelative) {
            throw 'Path-backed engine evidence is incomplete.'
        }
        $stdoutBytes = [IO.File]::ReadAllBytes(
            (Resolve-CampaignArtifact $Campaign ([string]$stdoutRelative))
        )
        $stderrBytes = [IO.File]::ReadAllBytes(
            (Resolve-CampaignArtifact $Campaign ([string]$stderrRelative))
        )
    }

    $artifactHash = $null
    $artifactPath = Get-PropertyValue $Evidence 'artifact_path'
    if ($null -ne $artifactPath) {
        $artifact = Resolve-CampaignArtifact $Campaign ([string]$artifactPath)
        $artifactHash = (Get-FileHash -LiteralPath $artifact -Algorithm SHA256).Hash.ToLowerInvariant()
        $declaredArtifactHash = Get-PropertyValue $Evidence 'artifact_sha256'
        if ($null -eq $declaredArtifactHash -or
            $artifactHash -ne [string]$declaredArtifactHash) {
            throw "Engine proof artifact hash mismatch: $artifact"
        }
    }
    return [ordered]@{
        artifact_sha256 = $artifactHash
        engine_id = [string](Get-PropertyValue $Evidence 'engine_id')
        executable_sha256 = [string](Get-PropertyValue $Evidence 'executable_sha256')
        stderr_sha256 = Get-BytesSha256 $stderrBytes
        stdout_sha256 = Get-BytesSha256 $stdoutBytes
    }
}

function Get-NativeProofProjection {
    param(
        [AllowNull()][object]$Evidence,
        [Parameter(Mandatory = $true)][string]$Campaign
    )
    if ($null -eq $Evidence) { return $null }
    $path = Resolve-CampaignArtifact $Campaign ([string]$Evidence.artifact_path)
    $observedHash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($observedHash -ne [string]$Evidence.artifact_sha256) {
        throw "Native proof hash mismatch: $path"
    }
    $certificate = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
    return [ordered]@{
        format_version = [string]$certificate.format_version
        k = [string]$certificate.k
        n = [string]$certificate.n
        residue = [string]$certificate.residue
        value = [string]$certificate.value
        witness = [string]$certificate.witness
    }
}

function Get-SemanticProjection {
    param([Parameter(Mandatory = $true)][string]$Campaign)
    $resultsPath = Resolve-CampaignArtifact $Campaign 'results.jsonl'
    $journalPath = Join-Path $Campaign 'external\flint\evidence.jsonl'
    $journalBytes = if (Test-Path -LiteralPath $journalPath -PathType Leaf) {
        [IO.File]::ReadAllBytes($journalPath)
    } else {
        [byte[]]::new(0)
    }
    $journalOffset = [uint64]0
    $builder = [Text.StringBuilder]::new()
    $recordCount = 0
    $flintRecordCount = 0
    foreach ($line in [IO.File]::ReadLines($resultsPath)) {
        $record = $line | ConvertFrom-Json
        if ([uint64]$record.flat_index -ne [uint64]$recordCount) {
            throw 'Result ledger is not contiguous by flat_index.'
        }
        $independent = Get-PropertyValue $record 'independent_engine'
        if ($null -ne $independent) { ++$flintRecordCount }
        $primary = Get-PropertyValue $record 'primary_engine'
        $independentProjection = Get-EngineProjection `
            -Evidence $independent -Campaign $Campaign -JournalBytes $journalBytes `
            -ExpectedJobId $(if ($null -eq $independent) { $null } else { "flint-$recordCount" }) `
            -ExpectedInput $(if ($null -eq $independent) { $null } else { [string]$record.value }) `
            -ExpectedJournalOffset ([ref]$journalOffset)
        $unusedPrimaryOffset = [uint64]0
        $primaryProjection = Get-EngineProjection `
            -Evidence $primary -Campaign $Campaign -JournalBytes ([byte[]]::new(0)) `
            -ExpectedJobId $null -ExpectedInput $null `
            -ExpectedJournalOffset ([ref]$unusedPrimaryOffset)
        $projection = [ordered]@{
            classification_method = [string]$record.classification_method
            factor = Get-PropertyValue $record 'factor'
            flat_index = [string]$record.flat_index
            independent_engine = $independentProjection
            k = [string]$record.k
            n = [string]$record.n
            native_proth_certificate = Get-NativeProofProjection `
                (Get-PropertyValue $record 'native_proth_certificate') $Campaign
            novelty_status = [string]$record.novelty_status
            primality_status = [string]$record.primality_status
            primary_engine = $primaryProjection
            prp_status = [string]$record.prp_status
            value = [string]$record.value
            verification_status = [string]$record.verification_status
        }
        [void]$builder.Append(($projection | ConvertTo-Json -Depth 8 -Compress))
        [void]$builder.Append("`n")
        ++$recordCount
    }
    if ($journalBytes.Length -ne 0 -and $journalOffset -ne [uint64]$journalBytes.Length) {
        throw 'FLINT journal has unreferenced or trailing bytes.'
    }
    $projectionBytes = [Text.Encoding]::UTF8.GetBytes($builder.ToString())
    return [pscustomobject][ordered]@{
        schema = 'primeforge.optimization09.semantic-projection.v1'
        sha256 = Get-BytesSha256 $projectionBytes
        records = $recordCount
        flint_records = $flintRecordCount
        representation = if ($journalBytes.Length -eq 0) { 'V2_PATHS' } else { 'V3_JOURNAL' }
    }
}

function Get-CampaignPhysicalIdentity {
    param(
        [Parameter(Mandatory = $true)][string]$Campaign,
        [Parameter(Mandatory = $true)][string]$Tree
    )
    $files = @(Get-SafeCampaignFiles $Campaign $Tree)
    $campaignRoot = [IO.Path]::GetFullPath($Campaign).TrimEnd('\', '/')
    $fileRows = @(
        foreach ($file in $files) {
            $relative = $file.FullName.Substring($campaignRoot.Length + 1).Replace('\', '/')
            $digest = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            [pscustomobject][ordered]@{
                path = $relative
                bytes = [uint64]$file.Length
                sha256 = $digest
            }
        }
    ) | Sort-Object path
    $required = @(
        'results.jsonl', 'campaign.checkpoint.json', 'MANIFEST.sha256',
        'coverage_report.json', 'search.yaml'
    )
    $hashes = [ordered]@{}
    foreach ($relative in $required) {
        $row = @($fileRows | Where-Object path -eq $relative)
        if ($row.Count -ne 1) { throw "Physical identity is missing $relative" }
        $hashes[$relative] = $row[0].sha256
    }
    $journal = @($fileRows | Where-Object path -eq 'external/flint/evidence.jsonl')
    $hashes['external/flint/evidence.jsonl'] = if ($journal.Count -eq 1) {
        $journal[0].sha256
    } elseif ($journal.Count -eq 0) {
        'ABSENT'
    } else {
        throw 'Physical identity found duplicate FLINT journals.'
    }
    $manifestLines = @([IO.File]::ReadAllLines((Join-Path $Campaign 'MANIFEST.sha256')))
    $identity = [ordered]@{
        files = $fileRows
        file_count = $fileRows.Count
        total_bytes = [uint64](($fileRows | Measure-Object bytes -Sum).Sum)
        manifest_entries = $manifestLines.Count
    }
    $canonical = $identity | ConvertTo-Json -Depth 6 -Compress
    return [pscustomobject][ordered]@{
        token_sha256 = Get-BytesSha256 ([Text.Encoding]::UTF8.GetBytes($canonical))
        file_count = $identity.file_count
        total_bytes = $identity.total_bytes
        manifest_entries = $identity.manifest_entries
        hashes = $hashes
    }
}

function Request-ProcessStop {
    param(
        [Parameter(Mandatory = $true)][Diagnostics.Process]$Process,
        [Parameter(Mandatory = $true)][string]$StopFile,
        [Parameter(Mandatory = $true)][int]$GraceMilliseconds,
        [Parameter(Mandatory = $true)][string]$Role
    )
    if ($Process.HasExited) { $Process.WaitForExit(); return }
    $writeError = $null
    try { [IO.File]::WriteAllText($StopFile, "STOP`n", $utf8NoBom) } catch {
        $writeError = $_.Exception.Message
    }
    if ($null -eq $writeError -and $Process.WaitForExit($GraceMilliseconds)) {
        $Process.WaitForExit()
        return
    }
    try { $Process.Kill() } catch { if (-not $Process.HasExited) { throw } }
    if (-not $Process.WaitForExit(5000)) { throw "$Role did not stop." }
    $Process.WaitForExit()
    if ($null -ne $writeError) { throw "$Role stop request failed: $writeError" }
}

function Wait-ForSafeIdle {
    while ($true) {
        $heavy = @(Get-Process -Name primeforge, primeforge-bench, ninja, cl, nvcc `
            -ErrorAction SilentlyContinue)
        if ($heavy.Count -ne 0) { Start-Sleep -Seconds 2; continue }
        $snapshotText = (& $hardwareMonitorPath --once | Out-String).Trim()
        if ($LASTEXITCODE -ne 0) { throw 'Hardware preflight failed.' }
        $snapshot = $snapshotText | ConvertFrom-Json
        foreach ($metric in @(
            'cpu_temperature_celsius', 'cpu_power_watts', 'gpu_temperature_celsius',
            'gpu_power_watts', 'ram_available_bytes', 'vram_free_mib',
            'whea_errors_recent'
        )) {
            if ($snapshot.$metric.status -ne 'DETECTED') {
                throw "Required hardware metric is unavailable: $metric"
            }
        }
        if (-not [bool]$snapshot.throttling_detected -and
            (Convert-ToDouble $snapshot.whea_errors_recent.value) -eq 0.0 -and
            (Convert-ToDouble $snapshot.cpu_temperature_celsius.value) -le 70.0 -and
            (Convert-ToDouble $snapshot.ram_available_bytes.value) -ge 8589934592.0 -and
            (Convert-ToDouble $snapshot.vram_free_mib.value) -ge 2048.0) {
            return $snapshot
        }
        Start-Sleep -Seconds 5
    }
}

function Convert-ToNativeArgument {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Argument)
    if ($Argument.Length -ne 0 -and $Argument -notmatch '[\s"]') { return $Argument }
    $builder = [Text.StringBuilder]::new()
    [void]$builder.Append('"')
    $backslashes = 0
    foreach ($character in $Argument.ToCharArray()) {
        if ($character -eq '\') {
            ++$backslashes
            continue
        }
        if ($character -eq '"') {
            if ($backslashes -ne 0) {
                [void]$builder.Append((('\' * (2 * $backslashes)) -join ''))
            }
            [void]$builder.Append('\"')
            $backslashes = 0
            continue
        }
        if ($backslashes -ne 0) {
            [void]$builder.Append((('\' * $backslashes) -join ''))
            $backslashes = 0
        }
        [void]$builder.Append($character)
    }
    if ($backslashes -ne 0) {
        [void]$builder.Append((('\' * (2 * $backslashes)) -join ''))
    }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function Join-NativeArguments {
    param([Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$Arguments)
    return (($Arguments | ForEach-Object { Convert-ToNativeArgument $_ }) -join ' ')
}

function Assert-FrozenRunInputs {
    param(
        [Parameter(Mandatory = $true)][object]$Variant,
        [Parameter(Mandatory = $true)][string]$RunId
    )
    foreach ($input in @(
        @($Variant.executable, $Variant.executable_sha256, 'variant executable'),
        @($Variant.profile, $Variant.profile_sha256, 'benchmark profile'),
        @($watchdogPath, $watchdogSha256, 'watchdog executable'),
        @($hardwareMonitorPath, $hardwareMonitorSha256, 'hardware monitor executable')
    )) {
        if (-not (Test-Path -LiteralPath $input[0] -PathType Leaf)) {
            throw "$($input[2]) disappeared before or during $RunId"
        }
        $observed = (Get-FileHash -LiteralPath $input[0] -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($observed -ne $input[1]) {
            throw "$($input[2]) changed before or during $RunId"
        }
    }
}

function Invoke-GuardedCommand {
    param(
        [Parameter(Mandatory = $true)][object]$Variant,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$RunDirectory,
        [Parameter(Mandatory = $true)][string]$RunId
    )
    Assert-FrozenRunInputs $Variant $RunId
    New-Item -ItemType Directory -Path $RunDirectory | Out-Null
    $stdout = Join-Path $RunDirectory 'search.stdout.txt'
    $stderr = Join-Path $RunDirectory 'search.stderr.txt'
    $watchdogStdout = Join-Path $RunDirectory 'watchdog.stdout.txt'
    $watchdogStderr = Join-Path $RunDirectory 'watchdog.stderr.txt'
    $telemetryPath = Join-Path $RunDirectory 'telemetry.jsonl'
    $workerStop = Join-Path $RunDirectory 'worker.stop'
    $watchdogStop = Join-Path $RunDirectory 'watchdog.stop'
    $preflight = Wait-ForSafeIdle
    $watchdogArguments = @(
        '--pid', '', '--stop-file', $workerStop, '--watchdog-stop-file', $watchdogStop,
        '--log', $telemetryPath, '--campaign', $RunId, '--interval-ms', '500',
        '--grace-ms', '60000', '--max-cpu-temp-c', '92', '--max-gpu-temp-c', '88',
        '--require-cpu-temperature', '--require-cpu-power', '--require-gpu-temperature',
        '--require-gpu-power', '--require-ram-available', '--min-ram-available-bytes',
        '8589934592', '--require-vram-free', '--min-vram-free-mib', '2048',
        '--require-whea-status'
    )
    $worker = $null
    $watchdog = $null
    $timer = [Diagnostics.Stopwatch]::new()
    $primaryFailure = $null
    try {
        $timer.Start()
        $worker = Start-Process -FilePath $Variant.executable `
            -ArgumentList (Join-NativeArguments $Arguments) `
            -WorkingDirectory $Variant.tree -RedirectStandardOutput $stdout `
            -RedirectStandardError $stderr -WindowStyle Hidden -PassThru
        $watchdogArguments[1] = [string]$worker.Id
        $watchdog = Start-Process -FilePath $watchdogPath `
            -ArgumentList (Join-NativeArguments $watchdogArguments) `
            -WorkingDirectory $candidateTreePath -RedirectStandardOutput $watchdogStdout `
            -RedirectStandardError $watchdogStderr -WindowStyle Hidden -PassThru
        while (-not $worker.WaitForExit(200)) {
            if ($watchdog.HasExited -and -not $worker.HasExited) {
                $watchdog.WaitForExit()
                throw "Watchdog exited before worker for $RunId"
            }
        }
        $worker.WaitForExit()
        $timer.Stop()
        if (-not $watchdog.WaitForExit(10000)) {
            [IO.File]::WriteAllText($watchdogStop, "STOP`n", $utf8NoBom)
            if (-not $watchdog.WaitForExit(10000)) {
                throw "Watchdog did not stop for $RunId"
            }
        }
        $watchdog.WaitForExit()
        if ($null -ne $worker.ExitCode -and [int]$worker.ExitCode -ne 0) {
            throw "PrimeForge failed for $RunId with exit $($worker.ExitCode)"
        }
        if ($null -ne $watchdog.ExitCode -and [int]$watchdog.ExitCode -ne 0) {
            throw "Watchdog failed for $RunId with exit $($watchdog.ExitCode)"
        }
    } catch {
        $primaryFailure = $_.Exception
        throw
    } finally {
        if ($timer.IsRunning) { $timer.Stop() }
        $cleanup = [Collections.Generic.List[string]]::new()
        if ($null -ne $worker) {
            try { Request-ProcessStop $worker $workerStop 60000 'PrimeForge worker' } catch {
                $cleanup.Add($_.Exception.Message)
            }
        }
        if ($null -ne $watchdog) {
            try { Request-ProcessStop $watchdog $watchdogStop 10000 'PrimeForge watchdog' } catch {
                $cleanup.Add($_.Exception.Message)
            }
        }
        if ($cleanup.Count -ne 0 -and $null -eq $primaryFailure) {
            throw "Per-run cleanup failed: $($cleanup -join '; ')"
        }
    }
    if ((Get-Item -LiteralPath $stderr).Length -ne 0L -or
        (Get-Item -LiteralPath $watchdogStderr).Length -ne 0L) {
        throw "Successful guarded run wrote stderr: $RunId"
    }
    Assert-FrozenRunInputs $Variant $RunId
    $events = @(Get-Content -LiteralPath $telemetryPath | ForEach-Object {
        $_ | ConvertFrom-Json
    })
    $exitEvent = @($events | Where-Object event_type -eq 'worker_exited') |
        Select-Object -Last 1
    if ($null -eq $exitEvent -or [string]$exitEvent.payload.exit_code -ne '0' -or
        @($events | Where-Object event_type -in @('graceful_stop_requested', 'forced_stop')).Count -ne 0 -or
        -not (Select-String -LiteralPath $watchdogStdout -SimpleMatch `
            'benchmark_watchdog.decision=WORKER_EXITED reason=NONE' -Quiet)) {
        throw "Watchdog completion contract failed: $RunId"
    }
    $snapshots = @($events | Where-Object event_type -eq 'telemetry' |
        ForEach-Object { $_.payload.snapshot })
    if ($snapshots.Count -eq 0) { throw "No telemetry captured: $RunId" }
    foreach ($snapshot in $snapshots) {
        foreach ($metric in @(
            'cpu_temperature_celsius', 'cpu_power_watts', 'gpu_temperature_celsius',
            'gpu_power_watts', 'ram_available_bytes', 'vram_free_mib',
            'whea_errors_recent'
        )) {
            if ($snapshot.$metric.status -ne 'DETECTED') {
                throw "Telemetry metric became unavailable in ${RunId}: $metric"
            }
        }
    }
    $whea = [double[]]@($snapshots | ForEach-Object {
        Convert-ToDouble $_.whea_errors_recent.value
    })
    $cpuTemperatureMax = ($snapshots.cpu_temperature_celsius.value |
        ForEach-Object { Convert-ToDouble $_ } | Measure-Object -Maximum).Maximum
    $gpuTemperatureMax = ($snapshots.gpu_temperature_celsius.value |
        ForEach-Object { Convert-ToDouble $_ } | Measure-Object -Maximum).Maximum
    $ramAvailableMin = ($snapshots.ram_available_bytes.value |
        ForEach-Object { Convert-ToDouble $_ } | Measure-Object -Minimum).Minimum
    $vramFreeMin = ($snapshots.vram_free_mib.value |
        ForEach-Object { Convert-ToDouble $_ } | Measure-Object -Minimum).Minimum
    if (@($snapshots | Where-Object throttling_detected).Count -ne 0 -or
        ($whea | Measure-Object -Maximum).Maximum -ne 0.0 -or
        $cpuTemperatureMax -gt 92.0 -or $gpuTemperatureMax -gt 88.0 -or
        $ramAvailableMin -lt 8589934592.0 -or $vramFreeMin -lt 2048.0) {
        throw "Hardware validity gate failed: $RunId"
    }
    return [pscustomobject][ordered]@{
        stdout = $stdout
        key_values = Get-KeyValues $stdout
        wall_elapsed_ms = [uint64]$timer.ElapsedMilliseconds
        cpu_temperature_preflight_c = Convert-ToDouble $preflight.cpu_temperature_celsius.value
        cpu_temperature_max_c = $cpuTemperatureMax
        cpu_power_max_w = ($snapshots.cpu_power_watts.value |
            ForEach-Object { Convert-ToDouble $_ } | Measure-Object -Maximum).Maximum
        gpu_temperature_max_c = $gpuTemperatureMax
        gpu_power_max_w = ($snapshots.gpu_power_watts.value |
            ForEach-Object { Convert-ToDouble $_ } | Measure-Object -Maximum).Maximum
        ram_available_min_bytes = [uint64]$ramAvailableMin
        vram_free_min_mib = $vramFreeMin
        whea_max = ($whea | Measure-Object -Maximum).Maximum
    }
}

function Invoke-FullVerify {
    param(
        [Parameter(Mandatory = $true)][object]$Variant,
        [Parameter(Mandatory = $true)][string]$Campaign,
        [Parameter(Mandatory = $true)][string]$Directory,
        [Parameter(Mandatory = $true)][string]$RunId
    )
    $verifyDirectory = Join-Path $Directory 'full-verify'
    $guarded = Invoke-GuardedCommand $Variant @(
        'verify', '--result', (Join-Path $Campaign 'results.jsonl')
    ) $verifyDirectory ($RunId + '-full-verify')
    if ($guarded.key_values['verify.status'] -ne 'PASS') {
        throw "Full campaign verification failed for $($Variant.name)"
    }
}

function Shuffle-Items {
    param(
        [Parameter(Mandatory = $true)][object[]]$Items,
        [Parameter(Mandatory = $true)][Random]$Random
    )
    $copy = @($Items)
    for ($index = $copy.Count - 1; $index -gt 0; --$index) {
        $swap = $Random.Next($index + 1)
        $temporary = $copy[$index]
        $copy[$index] = $copy[$swap]
        $copy[$swap] = $temporary
    }
    return $copy
}

function New-O09Schedule {
    param(
        [Parameter(Mandatory = $true)][int]$MeasuredPairs,
        [Parameter(Mandatory = $true)][int]$NumberOfWarmupPairs,
        [Parameter(Mandatory = $true)][int]$ScheduleSeed
    )
    $random = [Random]::new($ScheduleSeed)
    $schedule = [Collections.Generic.List[object]]::new()
    $sequence = 0
    for ($warmup = 1; $warmup -le $NumberOfWarmupPairs; ++$warmup) {
        $direction = if ($random.Next(2) -eq 0) { @('baseline', 'candidate') } else {
            @('candidate', 'baseline')
        }
        for ($order = 0; $order -lt 2; ++$order) {
            ++$sequence
            $schedule.Add([pscustomobject][ordered]@{
                sequence = $sequence; phase = 'warmup'; pair = $warmup
                order = $order + 1; variant = $direction[$order]
            })
        }
    }
    $directions = @()
    for ($index = 0; $index -lt 8; ++$index) {
        $directions += ,@('baseline', 'candidate')
    }
    for ($index = 0; $index -lt 7; ++$index) {
        $directions += ,@('candidate', 'baseline')
    }
    if ($MeasuredPairs -ne $directions.Count) {
        throw 'The O09 evidence protocol requires exactly 15 measured pairs.'
    }
    $directions = @(Shuffle-Items $directions $random)
    for ($pair = 1; $pair -le $MeasuredPairs; ++$pair) {
        for ($order = 0; $order -lt 2; ++$order) {
            ++$sequence
            $schedule.Add([pscustomobject][ordered]@{
                sequence = $sequence; phase = 'measure'; pair = $pair
                order = $order + 1; variant = $directions[$pair - 1][$order]
            })
        }
    }
    return @($schedule)
}

function Assert-O09Schedule {
    param(
        [Parameter(Mandatory = $true)][object[]]$Schedule,
        [Parameter(Mandatory = $true)][int]$MeasuredPairs,
        [Parameter(Mandatory = $true)][int]$NumberOfWarmupPairs
    )
    if ($Schedule.Count -ne 2 * ($MeasuredPairs + $NumberOfWarmupPairs)) {
        throw 'O09 schedule row count is invalid.'
    }
    for ($index = 0; $index -lt $Schedule.Count; ++$index) {
        if ([int]$Schedule[$index].sequence -ne $index + 1) {
            throw 'O09 schedule sequence is not contiguous.'
        }
    }
    foreach ($phase in @(
        @('warmup', $NumberOfWarmupPairs), @('measure', $MeasuredPairs)
    )) {
        for ($pair = 1; $pair -le [int]$phase[1]; ++$pair) {
            $rows = @($Schedule | Where-Object {
                $_.phase -eq $phase[0] -and [int]$_.pair -eq $pair
            } | Sort-Object order)
            if ($rows.Count -ne 2 -or [int]$rows[0].order -ne 1 -or
                [int]$rows[1].order -ne 2 -or
                (@($rows.variant | Sort-Object) -join ',') -ne 'baseline,candidate') {
                throw "O09 schedule pair is malformed: $($phase[0])/$pair"
            }
        }
    }
    $measure = @($Schedule | Where-Object phase -eq 'measure')
    $ab = @($measure | Where-Object {
        [int]$_.order -eq 1 -and $_.variant -eq 'baseline'
    }).Count
    $ba = @($measure | Where-Object {
        [int]$_.order -eq 1 -and $_.variant -eq 'candidate'
    }).Count
    if ($ab -ne 8 -or $ba -ne 7) {
        throw "O09 measured order balance is invalid: AB=$ab;BA=$ba"
    }
}

if ([Math]::Abs((Convert-ToDouble '70.875') - 70.875) -gt 0.000001) {
    throw 'Invariant numeric conversion self-test failed.'
}
$emptyDigest = Get-BytesSha256 ([byte[]]::new(0))
if ($emptyDigest -ne 'e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855' -or
    (Get-Median ([double[]]@(1.0, 5.0, 3.0))) -ne 3.0 -or
    (Get-Mad ([double[]]@(1.0, 2.0, 3.0))) -ne 1.0 -or
    (Convert-ToNativeArgument '') -ne '""' -or
    (Convert-ToNativeArgument 'alpha beta') -ne '"alpha beta"') {
    throw 'O09 helper self-test failed.'
}
$schedule = @(New-O09Schedule $Pairs $WarmupPairs $Seed)
Assert-O09Schedule $schedule $Pairs $WarmupPairs
if ($ValidateOnly) {
    Write-Host 'validation.status=PASS'
    Write-Host "validation.schedule_rows=$($schedule.Count)"
    Write-Host 'validation.warmup_pairs=1'
    Write-Host 'validation.measured_pairs=15'
    Write-Host 'validation.order_balance=AB:8,BA:7'
    Write-Host 'validation.external_inputs=NOT_EXECUTED'
    return
}

if ($baselineTreePath.Equals($candidateTreePath, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Baseline and candidate must be separate Git trees.'
}
Assert-NoReparseAncestor $baselineExePath $baselineTreePath 'Baseline executable'
Assert-NoReparseAncestor $candidateExePath $candidateTreePath 'Candidate executable'
Assert-NoReparseAncestor $watchdogPath $candidateTreePath 'Watchdog executable'
Assert-NoReparseAncestor $hardwareMonitorPath $candidateTreePath 'Hardware monitor executable'
Assert-NoReparseAncestor $outputRoot $repositoryRoot 'Optimization-09 output'

$baselineGit = Get-GitTreeIdentity $baselineTreePath
$candidateGit = Get-GitTreeIdentity $candidateTreePath
$variants = @{
    baseline = [pscustomobject][ordered]@{
        name = 'baseline'
        format = 'v2'
        tree = $baselineTreePath
        executable = $baselineExePath
        executable_sha256 = (Get-FileHash $baselineExePath -Algorithm SHA256).Hash.ToLowerInvariant()
        commit = $baselineGit.commit
        profile = Resolve-AbsolutePath $ProfileRelativePath $baselineTreePath
        campaign = Resolve-AbsolutePath $CampaignRelativePath $baselineTreePath
    }
    candidate = [pscustomobject][ordered]@{
        name = 'candidate'
        format = 'v3'
        tree = $candidateTreePath
        executable = $candidateExePath
        executable_sha256 = (Get-FileHash $candidateExePath -Algorithm SHA256).Hash.ToLowerInvariant()
        commit = $candidateGit.commit
        profile = Resolve-AbsolutePath $ProfileRelativePath $candidateTreePath
        campaign = Resolve-AbsolutePath $CampaignRelativePath $candidateTreePath
    }
}
foreach ($name in @('baseline', 'candidate')) {
    $variant = $variants[$name]
    foreach ($path in @($variant.profile, $variant.executable)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Variant input absent: $path"
        }
    }
    Assert-NoReparseAncestor $variant.profile $variant.tree "$name profile"
    Assert-NoReparseAncestor $variant.campaign $variant.tree "$name campaign"
    if (Test-Path -LiteralPath $variant.campaign) {
        throw "Archive existing fixed campaign before O09: $($variant.campaign)"
    }
}
$baselineProfileHash = (Get-FileHash $variants.baseline.profile -Algorithm SHA256).Hash.ToLowerInvariant()
$candidateProfileHash = (Get-FileHash $variants.candidate.profile -Algorithm SHA256).Hash.ToLowerInvariant()
if ($baselineProfileHash -ne $candidateProfileHash) {
    throw 'Baseline and candidate profile bytes differ.'
}
$variants.baseline | Add-Member -NotePropertyName profile_sha256 -NotePropertyValue $baselineProfileHash
$variants.candidate | Add-Member -NotePropertyName profile_sha256 -NotePropertyValue $candidateProfileHash
$watchdogSha256 = (Get-FileHash $watchdogPath -Algorithm SHA256).Hash.ToLowerInvariant()
$hardwareMonitorSha256 = (Get-FileHash $hardwareMonitorPath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($baselineGit.commit -eq $candidateGit.commit) {
    throw 'Baseline and candidate Git commits are identical.'
}
if ($variants.baseline.executable_sha256 -eq $variants.candidate.executable_sha256) {
    throw 'Baseline and candidate executable bytes are identical.'
}

New-Item -ItemType Directory -Path $optimizationRoot -Force | Out-Null
$lockPath = Join-Path $optimizationRoot '.compact-evidence-ab.lock'
$sweepLock = $null
try {
    try {
        $sweepLock = [IO.File]::Open(
            $lockPath, [IO.FileMode]::OpenOrCreate, [IO.FileAccess]::ReadWrite,
            [IO.FileShare]::None
        )
    } catch {
        throw "Another O09 A/B run owns the lock: $lockPath"
    }
    if (Test-Path -LiteralPath $outputRoot) { throw "Output appeared: $outputRoot" }
    New-Item -ItemType Directory -Path $outputRoot | Out-Null

    Write-InvariantCsv (Join-Path $outputRoot 'schedule.csv') @($schedule)

    $environment = [ordered]@{
        schema = 'primeforge.optimization09.compact-evidence-environment.v1'
        captured_utc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ss.fffZ')
        seed = $Seed
        warmup_pairs = $WarmupPairs
        measured_pairs = $Pairs
        measured_order_balance = 'AB=8;BA=7'
        profile_relative_path = $ProfileRelativePath
        profile_sha256 = $baselineProfileHash
        campaign_relative_path = $CampaignRelativePath
        baseline = $variants.baseline
        candidate = $variants.candidate
        watchdog_sha256 = $watchdogSha256
        hardware_monitor_sha256 = $hardwareMonitorSha256
        script_sha256 = (Get-FileHash $PSCommandPath -Algorithm SHA256).Hash.ToLowerInvariant()
        command = 'primeforge search --config <profile> --prp-backend auto --prp-batch-candidates 8192 --proof-workers 4'
        primary_metric = 'metrics.total_ns'
        secondary_metrics = @('metrics.verification_ns', 'metrics.io_ns',
            'metrics.checkpoint_ns', 'metrics.proof_ns')
        gate = 'PAIRED_MEDIAN_GAIN_GE_3_PERCENT;DELTA_OF_MEDIANS_GT_2_MAX_MAD;PAIRED_BOOTSTRAP_TWO_SIDED_95_PERCENT_LOWER_GT_ZERO;SEMANTIC_IDENTITY;PHYSICAL_DETERMINISM_PER_VARIANT;STOP_RESUME_IDENTITY'
        cpu_temperature_stop_celsius = 92
        minimum_ram_available_bytes = 8589934592
        minimum_vram_free_mib = 2048
    }
    Write-Json (Join-Path $outputRoot 'environment.json') $environment

    $raw = [Collections.Generic.List[object]]::new()
    $physicalRows = [Collections.Generic.List[object]]::new()
    $semanticRows = [Collections.Generic.List[object]]::new()
    $references = @{}
    $expectedSemanticHash = $null

    foreach ($item in $schedule) {
        $variant = $variants[[string]$item.variant]
        if (Test-Path -LiteralPath $variant.campaign) {
            throw "Fixed campaign path exists before run $($item.sequence)"
        }
        $currentExeHash = (Get-FileHash $variant.executable -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($currentExeHash -ne $variant.executable_sha256) {
            throw "Variant executable changed before run $($item.sequence)"
        }
        $runId = '{0:D2}-{1}-pair{2:D2}-o{3}-{4}' -f [int]$item.sequence,
            [string]$item.phase, [int]$item.pair, [int]$item.order, [string]$item.variant
        $runDirectory = Join-Path $outputRoot $runId
        $arguments = @(
            'search', '--config', $ProfileRelativePath, '--prp-backend', 'auto',
            '--prp-batch-candidates', '8192', '--proof-workers', '4',
            '--stop-file', (Join-Path $runDirectory 'worker.stop')
        )
        $guarded = Invoke-GuardedCommand $variant $arguments $runDirectory $runId
        $keyValues = $guarded.key_values
        if ($keyValues['search.status'] -ne 'PASS' -or
            $keyValues['search.commit_sha'] -ne $variant.commit -or
            $keyValues['search.prp_backend'] -ne $expectedPrpBackend -or
            $keyValues['search.proof_workers'] -ne '4' -or
            $keyValues['search.candidates'] -ne '32768' -or
            $keyValues['search.proven_primes'] -ne '1506' -or
            $keyValues['search.composites'] -ne '31262') {
            throw "Search contract failed: $runId"
        }
        $physical = Get-CampaignPhysicalIdentity $variant.campaign $variant.tree
        $semantic = Get-SemanticProjection $variant.campaign
        $expectedRepresentation = if ($variant.format -eq 'v2') { 'V2_PATHS' } else {
            'V3_JOURNAL'
        }
        if ($semantic.records -ne 32768 -or $semantic.flint_records -ne 1506) {
            throw "Semantic projection coverage failed: $runId"
        }
        if ($semantic.representation -ne $expectedRepresentation) {
            throw "Evidence representation mismatch for $runId"
        }
        if ($null -eq $expectedSemanticHash) {
            $expectedSemanticHash = $semantic.sha256
        } elseif ($semantic.sha256 -ne $expectedSemanticHash) {
            throw "Cross-version semantic projection mismatch: $runId"
        }

        $fullVerify = -not $references.ContainsKey([string]$item.variant)
        if ($fullVerify) {
            Invoke-FullVerify $variant $variant.campaign $runDirectory $runId
            $postVerify = Get-CampaignPhysicalIdentity $variant.campaign $variant.tree
            if ($postVerify.token_sha256 -ne $physical.token_sha256) {
                throw "Verifier mutated campaign: $runId"
            }
            $references[[string]$item.variant] = [pscustomobject]@{
                physical = $physical
                semantic = $semantic
            }
            Move-ValidatedCampaign $variant.campaign $variant.tree `
                (Join-Path $runDirectory 'campaign') $outputRoot
            $verifyStatus = 'PASS_FULL_AND_ARCHIVED'
        } else {
            $reference = $references[[string]$item.variant]
            if ($physical.token_sha256 -ne $reference.physical.token_sha256) {
                throw "Physical determinism failed within $($item.variant): $runId"
            }
            Remove-ValidatedCampaign $variant.campaign $variant.tree
            $verifyStatus = 'REFERENCE_PHYSICAL_AND_SEMANTIC_IDENTITY'
        }

        $physicalRows.Add([pscustomobject][ordered]@{
            sequence = [int]$item.sequence; phase = [string]$item.phase
            pair = [int]$item.pair; variant = [string]$item.variant
            physical_token_sha256 = $physical.token_sha256
            results_sha256 = $physical.hashes['results.jsonl']
            checkpoint_sha256 = $physical.hashes['campaign.checkpoint.json']
            manifest_sha256 = $physical.hashes['MANIFEST.sha256']
            coverage_sha256 = $physical.hashes['coverage_report.json']
            search_yaml_sha256 = $physical.hashes['search.yaml']
            flint_journal_sha256 = $physical.hashes['external/flint/evidence.jsonl']
            file_count = $physical.file_count; total_bytes = $physical.total_bytes
            manifest_entries = $physical.manifest_entries; status = 'PASS'
        })
        $semanticRows.Add([pscustomobject][ordered]@{
            sequence = [int]$item.sequence; phase = [string]$item.phase
            pair = [int]$item.pair; variant = [string]$item.variant
            representation = $semantic.representation
            semantic_projection_sha256 = $semantic.sha256
            records = $semantic.records; flint_records = $semantic.flint_records
            status = 'PASS'
        })
        $raw.Add([pscustomobject][ordered]@{
            sequence = [int]$item.sequence; phase = [string]$item.phase
            pair = [int]$item.pair; order = [int]$item.order
            variant = [string]$item.variant
            total_ns = [uint64]$keyValues['metrics.total_ns']
            verification_ns = [uint64]$keyValues['metrics.verification_ns']
            io_ns = [uint64]$keyValues['metrics.io_ns']
            checkpoint_ns = [uint64]$keyValues['metrics.checkpoint_ns']
            proof_ns = [uint64]$keyValues['metrics.proof_ns']
            wall_elapsed_ms = $guarded.wall_elapsed_ms
            campaign_id = [string]$keyValues['search.campaign_id']
            build_commit = [string]$keyValues['search.commit_sha']
            verify_status = $verifyStatus
            semantic_projection_sha256 = $semantic.sha256
            physical_token_sha256 = $physical.token_sha256
            file_count = $physical.file_count; total_bytes = $physical.total_bytes
            manifest_entries = $physical.manifest_entries
            cpu_temperature_preflight_c = $guarded.cpu_temperature_preflight_c
            cpu_temperature_max_c = $guarded.cpu_temperature_max_c
            cpu_power_max_w = $guarded.cpu_power_max_w
            gpu_temperature_max_c = $guarded.gpu_temperature_max_c
            gpu_power_max_w = $guarded.gpu_power_max_w
            ram_available_min_bytes = $guarded.ram_available_min_bytes
            vram_free_min_mib = $guarded.vram_free_min_mib
            whea_max = $guarded.whea_max
            throttling = 'NO'; status = 'PASS'
        })
        Write-InvariantCsv (Join-Path $outputRoot 'raw.csv') @($raw)
        Write-JsonLines (Join-Path $outputRoot 'raw.jsonl') @($raw)
        Write-InvariantCsv (Join-Path $outputRoot 'physical-hashes.csv') @($physicalRows)
        Write-InvariantCsv (Join-Path $outputRoot 'semantic-projections.csv') @($semanticRows)
        Write-Host "[$($item.sequence)/$($schedule.Count)] $runId PASS total_ns=$($keyValues['metrics.total_ns'])"
    }

    if ($references.Count -ne 2 -or $raw.Count -ne 2 * ($WarmupPairs + $Pairs)) {
        throw 'O09 schedule or full-reference coverage is incomplete.'
    }

    $recoveryRows = [Collections.Generic.List[object]]::new()
    foreach ($name in @('baseline', 'candidate')) {
        $variant = $variants[$name]
        $recoveryRoot = Join-Path $outputRoot ("recovery-" + $name)
        New-Item -ItemType Directory -Path $recoveryRoot | Out-Null
        $stopRun = Join-Path $recoveryRoot 'stop'
        $stopArguments = @(
            'search', '--config', $ProfileRelativePath, '--stop-after',
            [string]$StopAfterCandidates, '--prp-backend', 'auto',
            '--prp-batch-candidates', '8192', '--proof-workers', '4',
            '--stop-file', (Join-Path $stopRun 'worker.stop')
        )
        $stopped = Invoke-GuardedCommand $variant $stopArguments $stopRun ("recovery-$name-stop")
        if ($stopped.key_values['search.status'] -ne 'STOPPED' -or
            $stopped.key_values['search.commit_sha'] -ne $variant.commit -or
            $stopped.key_values['search.prp_backend'] -ne $expectedPrpBackend -or
            -not (Test-Path -LiteralPath (Join-Path $variant.campaign 'campaign.checkpoint.json') -PathType Leaf)) {
            throw "Clean stop gate failed for $name"
        }
        $resumeRun = Join-Path $recoveryRoot 'resume'
        $resumeArguments = @(
            'resume', '--checkpoint', (Join-Path $variant.campaign 'campaign.checkpoint.json'),
            '--prp-backend', 'auto', '--prp-batch-candidates', '8192',
            '--proof-workers', '4', '--stop-file', (Join-Path $resumeRun 'worker.stop')
        )
        $resumed = Invoke-GuardedCommand $variant $resumeArguments $resumeRun ("recovery-$name-resume")
        if ($resumed.key_values['search.status'] -ne 'PASS' -or
            $resumed.key_values['search.commit_sha'] -ne $variant.commit -or
            $resumed.key_values['search.prp_backend'] -ne $expectedPrpBackend) {
            throw "Resume did not complete for $name"
        }
        $physical = Get-CampaignPhysicalIdentity $variant.campaign $variant.tree
        $semantic = Get-SemanticProjection $variant.campaign
        $reference = $references[$name]
        if ($physical.token_sha256 -ne $reference.physical.token_sha256 -or
            $semantic.sha256 -ne $reference.semantic.sha256 -or
            $semantic.sha256 -ne $expectedSemanticHash) {
            throw "Stop/resume identity failed for $name"
        }
        Invoke-FullVerify $variant $variant.campaign $resumeRun ("recovery-$name")
        Move-ValidatedCampaign $variant.campaign $variant.tree `
            (Join-Path $recoveryRoot 'campaign') $outputRoot
        $recoveryRows.Add([pscustomobject][ordered]@{
            variant = $name; stop_after_candidates = $StopAfterCandidates
            stopped_status = 'PASS'; resumed_status = 'PASS'; verify_status = 'PASS'
            physical_identity_vs_uninterrupted = 'IDENTICAL'
            semantic_identity_vs_uninterrupted = 'IDENTICAL'
            physical_token_sha256 = $physical.token_sha256
            semantic_projection_sha256 = $semantic.sha256
        })
    }
    Write-Json (Join-Path $outputRoot 'recovery.json') ([ordered]@{
        schema = 'primeforge.optimization09.recovery.v1'; rows = @($recoveryRows)
    })

    $measured = @($raw | Where-Object phase -eq 'measure')
    $baselineRows = @($measured | Where-Object variant -eq 'baseline' | Sort-Object pair)
    $candidateRows = @($measured | Where-Object variant -eq 'candidate' | Sort-Object pair)
    if ($baselineRows.Count -ne $Pairs -or $candidateRows.Count -ne $Pairs) {
        throw 'Measured A/B rows do not cover all pairs.'
    }
    $pairedGains = [Collections.Generic.List[double]]::new()
    $candidateWins = 0
    for ($index = 0; $index -lt $Pairs; ++$index) {
        if ([int]$baselineRows[$index].pair -ne [int]$candidateRows[$index].pair) {
            throw 'A/B pair alignment failed.'
        }
        $baselineTotal = [double]$baselineRows[$index].total_ns
        $candidateTotal = [double]$candidateRows[$index].total_ns
        $pairedGains.Add(100.0 * ($baselineTotal - $candidateTotal) / $baselineTotal)
        if ($candidateTotal -lt $baselineTotal) { ++$candidateWins }
    }
    $baselineTotals = [double[]]@($baselineRows | ForEach-Object { [double]$_.total_ns })
    $candidateTotals = [double[]]@($candidateRows | ForEach-Object { [double]$_.total_ns })
    $baselineMedian = Get-Median $baselineTotals
    $candidateMedian = Get-Median $candidateTotals
    $baselineMad = Get-Mad $baselineTotals
    $candidateMad = Get-Mad $candidateTotals
    $gainNs = $baselineMedian - $candidateMedian
    $pairedMedianGain = Get-Median ([double[]]$pairedGains)
    $interval = Get-BootstrapMedianInterval ([double[]]$pairedGains) ([Random]::new($Seed + 1))
    $gainGate = $pairedMedianGain -ge 3.0
    $madGate = $gainNs -gt 2.0 * [Math]::Max($baselineMad, $candidateMad)
    $bootstrapGate = [double]$interval.low -gt 0.0
    $retained = $gainGate -and $madGate -and $bootstrapGate

    $summaryRows = @(
        foreach ($name in @('baseline', 'candidate')) {
            $rows = if ($name -eq 'baseline') { $baselineRows } else { $candidateRows }
            [pscustomobject][ordered]@{
                variant = $name; repetitions = $rows.Count
                median_total_ns = [uint64](Get-Median ([double[]]$rows.total_ns))
                mad_total_ns = [uint64](Get-Mad ([double[]]$rows.total_ns))
                median_verification_ns = [uint64](Get-Median ([double[]]$rows.verification_ns))
                median_io_ns = [uint64](Get-Median ([double[]]$rows.io_ns))
                median_checkpoint_ns = [uint64](Get-Median ([double[]]$rows.checkpoint_ns))
                median_proof_ns = [uint64](Get-Median ([double[]]$rows.proof_ns))
                semantic_projection_sha256 = $expectedSemanticHash
            }
        }
    )
    Write-InvariantCsv (Join-Path $outputRoot 'summary.csv') $summaryRows
    Write-Json (Join-Path $outputRoot 'summary.json') ([ordered]@{
        schema = 'primeforge.optimization09.compact-evidence-summary.v1'; rows = $summaryRows
    })
    $comparison = [pscustomobject][ordered]@{
        baseline = 'v2-path-backed-evidence'; candidate = 'v3-compact-journal'
        pairs = $Pairs; candidate_wins = $candidateWins
        absolute_median_total_gain_ns = [int64]$gainNs
        ratio_of_medians_total_gain_percent = 100.0 * $gainNs / $baselineMedian
        paired_median_total_gain_percent = $pairedMedianGain
        paired_bootstrap_95_low_percent = [double]$interval.low
        paired_bootstrap_95_high_percent = [double]$interval.high
        paired_gain_ge_3_percent = if ($gainGate) { 'YES' } else { 'NO' }
        delta_of_medians_gt_2_max_mad = if ($madGate) { 'YES' } else { 'NO' }
        paired_bootstrap_95_lower_gt_zero = if ($bootstrapGate) { 'YES' } else { 'NO' }
        semantic_identity = 'PASS'; physical_determinism_per_variant = 'PASS'
        stop_resume_identity = 'PASS'
        retention_gate = if ($retained) { 'PASS' } else { 'FAIL' }
    }
    Write-InvariantCsv (Join-Path $outputRoot 'comparison.csv') @($comparison)
    Write-Json (Join-Path $outputRoot 'comparison.json') ([ordered]@{
        schema = 'primeforge.optimization09.compact-evidence-comparison.v1'
        row = $comparison
    })
    $decision = [ordered]@{
        schema = 'primeforge.optimization09.compact-evidence-decision.v1'
        decision = if ($retained) { 'RETAIN_COMPACT_EVIDENCE_V3' } else { 'KEEP_PATH_BACKED_V2' }
        primary_metric = 'total_ns'
        gate = 'PAIRED_MEDIAN_GAIN_GE_3_PERCENT;DELTA_OF_MEDIANS_GT_2_MAX_MAD;PAIRED_BOOTSTRAP_TWO_SIDED_95_PERCENT_LOWER_GT_ZERO;SEMANTIC_IDENTITY;PHYSICAL_DETERMINISM_PER_VARIANT;STOP_RESUME_IDENTITY'
        comparison = $comparison
        semantic_projection_sha256 = $expectedSemanticHash
        hardware_validity = [ordered]@{
            max_cpu_temperature_c = ($raw.cpu_temperature_max_c | Measure-Object -Maximum).Maximum
            max_cpu_power_w = ($raw.cpu_power_max_w | Measure-Object -Maximum).Maximum
            max_gpu_temperature_c = ($raw.gpu_temperature_max_c | Measure-Object -Maximum).Maximum
            max_gpu_power_w = ($raw.gpu_power_max_w | Measure-Object -Maximum).Maximum
            min_ram_available_bytes = ($raw.ram_available_min_bytes | Measure-Object -Minimum).Minimum
            min_vram_free_mib = ($raw.vram_free_min_mib | Measure-Object -Minimum).Minimum
            max_whea_errors = ($raw.whea_max | Measure-Object -Maximum).Maximum
            throttling = 'NO'
        }
    }
    Write-Json (Join-Path $outputRoot 'decision.json') $decision

    $checksumLines = @(
        Get-ChildItem -LiteralPath $outputRoot -File | Sort-Object Name | ForEach-Object {
            $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            "$hash  $($_.Name)"
        }
    )
    [IO.File]::WriteAllText(
        (Join-Path $outputRoot 'SHA256SUMS'), (($checksumLines -join "`n") + "`n"),
        $utf8NoBom
    )
    Write-Host "Optimization-09 A/B complete: $($decision.decision), output=$outputRoot"
} finally {
    if ($null -ne $sweepLock) { $sweepLock.Dispose() }
}
