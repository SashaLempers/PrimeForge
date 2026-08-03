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
    [int]$Seed = 20260805
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
    param([Parameter(Mandatory = $true)][byte[]]$Bytes)
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
        $offsetText = Get-PropertyValue $Evidence 'raw_log_offset'
        $lengthText = Get-PropertyValue $Evidence 'raw_log_length'
        if ($null -eq $offsetText -or $null -eq $lengthText) {
            throw 'FLINT journal reference is incomplete.'
        }
        $offset = [uint64]$offsetText
        $length = [uint64]$lengthText
        if ($length -eq 0 -or $offset -ne [uint64]$ExpectedJournalOffset.Value -or
            $offset -gt [uint64]$JournalBytes.Length -or
            $length -gt [uint64]$JournalBytes.Length - $offset) {
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
    param([Parameter(Mandatory = $true)][string]$Campaign)
    $required = @(
        'results.jsonl', 'campaign.checkpoint.json', 'MANIFEST.sha256',
        'coverage_report.json', 'search.yaml'
    )
    $hashes = [ordered]@{}
    foreach ($relative in $required) {
        $path = Resolve-CampaignArtifact $Campaign $relative
        $hashes[$relative] =
            (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
    }
    $journal = Join-Path $Campaign 'external\flint\evidence.jsonl'
    $hashes['external/flint/evidence.jsonl'] = if (Test-Path -LiteralPath $journal -PathType Leaf) {
        (Get-FileHash -LiteralPath $journal -Algorithm SHA256).Hash.ToLowerInvariant()
    } else {
        'ABSENT'
    }
    $files = @(Get-ChildItem -LiteralPath $Campaign -Recurse -File)
    $manifestLines = @([IO.File]::ReadAllLines((Join-Path $Campaign 'MANIFEST.sha256')))
    $identity = [ordered]@{
        file_count = $files.Count
        total_bytes = [uint64](($files | Measure-Object Length -Sum).Sum)
        manifest_entries = $manifestLines.Count
        hashes = $hashes
    }
    $canonical = $identity | ConvertTo-Json -Depth 5 -Compress
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

function Invoke-GuardedCommand {
    param(
        [Parameter(Mandatory = $true)][object]$Variant,
        [Parameter(Mandatory = $true)][string[]]$Arguments,
        [Parameter(Mandatory = $true)][string]$RunDirectory,
        [Parameter(Mandatory = $true)][string]$RunId
    )
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
        $worker = Start-Process -FilePath $Variant.executable -ArgumentList $Arguments `
            -WorkingDirectory $Variant.tree -RedirectStandardOutput $stdout `
            -RedirectStandardError $stderr -WindowStyle Hidden -PassThru
        $watchdogArguments[1] = [string]$worker.Id
        $watchdog = Start-Process -FilePath $watchdogPath -ArgumentList $watchdogArguments `
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
    $whea = [double[]]@($snapshots | ForEach-Object {
        Convert-ToDouble $_.whea_errors_recent.value
    })
    if (@($snapshots | Where-Object throttling_detected).Count -ne 0 -or
        ($whea | Measure-Object -Maximum).Maximum -ne 0.0) {
        throw "Hardware validity gate failed: $RunId"
    }
    return [pscustomobject][ordered]@{
        stdout = $stdout
        key_values = Get-KeyValues $stdout
        wall_elapsed_ms = [uint64]$timer.ElapsedMilliseconds
        cpu_temperature_preflight_c = Convert-ToDouble $preflight.cpu_temperature_celsius.value
        cpu_temperature_max_c = ($snapshots.cpu_temperature_celsius.value |
            ForEach-Object { Convert-ToDouble $_ } | Measure-Object -Maximum).Maximum
        cpu_power_max_w = ($snapshots.cpu_power_watts.value |
            ForEach-Object { Convert-ToDouble $_ } | Measure-Object -Maximum).Maximum
        gpu_temperature_max_c = ($snapshots.gpu_temperature_celsius.value |
            ForEach-Object { Convert-ToDouble $_ } | Measure-Object -Maximum).Maximum
        gpu_power_max_w = ($snapshots.gpu_power_watts.value |
            ForEach-Object { Convert-ToDouble $_ } | Measure-Object -Maximum).Maximum
        ram_available_min_bytes = [uint64](($snapshots.ram_available_bytes.value |
            ForEach-Object { Convert-ToDouble $_ } | Measure-Object -Minimum).Minimum)
        vram_free_min_mib = ($snapshots.vram_free_mib.value |
            ForEach-Object { Convert-ToDouble $_ } | Measure-Object -Minimum).Minimum
        whea_max = ($whea | Measure-Object -Maximum).Maximum
    }
}

function Invoke-FullVerify {
    param(
        [Parameter(Mandatory = $true)][object]$Variant,
        [Parameter(Mandatory = $true)][string]$Campaign,
        [Parameter(Mandatory = $true)][string]$Directory
    )
    $stdout = Join-Path $Directory 'verify.stdout.txt'
    $stderr = Join-Path $Directory 'verify.stderr.txt'
    & $Variant.executable verify --result (Join-Path $Campaign 'results.jsonl') `
        1> $stdout 2> $stderr
    if ($LASTEXITCODE -ne 0 -or (Get-Item -LiteralPath $stderr).Length -ne 0L -or
        -not (Select-String -LiteralPath $stdout -SimpleMatch 'verify.status=PASS' -Quiet)) {
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

if ([Math]::Abs((Convert-ToDouble '70.875') - 70.875) -gt 0.000001) {
    throw 'Invariant numeric conversion self-test failed.'
}

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
    Assert-PathInside $variant.campaign $variant.tree "$name campaign"
    if (Test-Path -LiteralPath $variant.campaign) {
        throw "Archive existing fixed campaign before O09: $($variant.campaign)"
    }
}
$baselineProfileHash = (Get-FileHash $variants.baseline.profile -Algorithm SHA256).Hash.ToLowerInvariant()
$candidateProfileHash = (Get-FileHash $variants.candidate.profile -Algorithm SHA256).Hash.ToLowerInvariant()
if ($baselineProfileHash -ne $candidateProfileHash) {
    throw 'Baseline and candidate profile bytes differ.'
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

    $random = [Random]::new($Seed)
    $schedule = [Collections.Generic.List[object]]::new()
    $sequence = 0
    for ($warmup = 1; $warmup -le $WarmupPairs; ++$warmup) {
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
    for ($index = 0; $index -lt 8; ++$index) { $directions += ,@('baseline', 'candidate') }
    for ($index = 0; $index -lt 7; ++$index) { $directions += ,@('candidate', 'baseline') }
    $directions = @(Shuffle-Items $directions $random)
    for ($pair = 1; $pair -le $Pairs; ++$pair) {
        for ($order = 0; $order -lt 2; ++$order) {
            ++$sequence
            $schedule.Add([pscustomobject][ordered]@{
                sequence = $sequence; phase = 'measure'; pair = $pair
                order = $order + 1; variant = $directions[$pair - 1][$order]
            })
        }
    }
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
        watchdog_sha256 = (Get-FileHash $watchdogPath -Algorithm SHA256).Hash.ToLowerInvariant()
        hardware_monitor_sha256 = (Get-FileHash $hardwareMonitorPath -Algorithm SHA256).Hash.ToLowerInvariant()
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
            $keyValues['search.prp_backend'] -ne $expectedPrpBackend -or
            $keyValues['search.proof_workers'] -ne '4' -or
            $keyValues['search.candidates'] -ne '32768' -or
            $keyValues['search.proven_primes'] -ne '1506' -or
            $keyValues['search.composites'] -ne '31262') {
            throw "Search contract failed: $runId"
        }
        $physical = Get-CampaignPhysicalIdentity $variant.campaign
        $semantic = Get-SemanticProjection $variant.campaign
        if ($semantic.records -ne 32768 -or $semantic.flint_records -ne 1506) {
            throw "Semantic projection coverage failed: $runId"
        }
        if ($null -eq $expectedSemanticHash) {
            $expectedSemanticHash = $semantic.sha256
        } elseif ($semantic.sha256 -ne $expectedSemanticHash) {
            throw "Cross-version semantic projection mismatch: $runId"
        }

        $fullVerify = -not $references.ContainsKey([string]$item.variant)
        if ($fullVerify) {
            Invoke-FullVerify $variant $variant.campaign $runDirectory
            $postVerify = Get-CampaignPhysicalIdentity $variant.campaign
            if ($postVerify.token_sha256 -ne $physical.token_sha256) {
                throw "Verifier mutated campaign: $runId"
            }
            $references[[string]$item.variant] = [pscustomobject]@{
                physical = $physical
                semantic = $semantic
            }
            Move-Item -LiteralPath $variant.campaign -Destination (Join-Path $runDirectory 'campaign')
            $verifyStatus = 'PASS_FULL_AND_ARCHIVED'
        } else {
            $reference = $references[[string]$item.variant]
            if ($physical.token_sha256 -ne $reference.physical.token_sha256) {
                throw "Physical determinism failed within $($item.variant): $runId"
            }
            Remove-Item -LiteralPath $variant.campaign -Recurse -Force
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
        if ($resumed.key_values['search.status'] -ne 'PASS') {
            throw "Resume did not complete for $name"
        }
        $physical = Get-CampaignPhysicalIdentity $variant.campaign
        $semantic = Get-SemanticProjection $variant.campaign
        $reference = $references[$name]
        if ($physical.token_sha256 -ne $reference.physical.token_sha256 -or
            $semantic.sha256 -ne $reference.semantic.sha256 -or
            $semantic.sha256 -ne $expectedSemanticHash) {
            throw "Stop/resume identity failed for $name"
        }
        Invoke-FullVerify $variant $variant.campaign $resumeRun
        Move-Item -LiteralPath $variant.campaign -Destination (Join-Path $recoveryRoot 'campaign')
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
