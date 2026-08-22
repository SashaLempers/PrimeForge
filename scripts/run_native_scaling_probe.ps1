[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [Parameter(Mandatory)][string]$Executable,
    [Parameter(Mandatory)][string]$InputFile,
    [Parameter(Mandatory)][int]$Digits,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [Parameter(Mandatory)][string]$RunId,
    [string]$HardwareMonitor = 'C:\Users\sashack\source\repos\PrimeForge\out\build\msvc-cuda-release\hardware_monitor.exe',
    [ValidateRange(30, 1800)][int]$MaximumRunSeconds = 1200,
    [ValidateRange(100, 5000)][int]$SampleIntervalMs = 500,
    [ValidateSet('WARMUP', 'MEASURED', 'PROFILED')][string]$RunKind = 'MEASURED',
    [switch]$KernelProfile
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$utf8 = [Text.UTF8Encoding]::new($false)
$invariant = [Globalization.CultureInfo]::InvariantCulture

function Resolve-ProjectPath {
    param([Parameter(Mandatory)][string]$Path)
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $repositoryRoot $Path))
}

function Get-ResultDigest {
    param([Parameter(Mandatory)][string]$Stdout)
    $records = @($Stdout -split "`r?`n" | Where-Object {
        $_.StartsWith("PRIMEFORGE_NATIVE_BATCH_RESULT`t")
    } | Sort-Object { [int](($_ -split "`t")[1]) })
    $canonical = ($records -join "`n") + "`n"
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $hash = $sha.ComputeHash($script:utf8.GetBytes($canonical))
        return [pscustomobject]@{
            count = $records.Count
            sha256 = -join ($hash | ForEach-Object { $_.ToString('x2') })
            records = $records
        }
    } finally { $sha.Dispose() }
}

function Get-TaggedFields {
    param(
        [Parameter(Mandatory)][string]$Stdout,
        [Parameter(Mandatory)][string]$Prefix
    )
    $line = @($Stdout -split "`r?`n" | Where-Object { $_.StartsWith($Prefix) } | Select-Object -Last 1)
    $values = [ordered]@{}
    if ($line.Count -eq 0) { return $values }
    $fields = $line[0] -split "`t"
    for ($index = 1; $index -lt $fields.Count; ++$index) {
        $parts = $fields[$index] -split '=', 2
        if ($parts.Count -eq 2) { $values[$parts[0]] = $parts[1] }
    }
    return $values
}

function Get-Plan {
    param([Parameter(Mandatory)][string]$Stdout)
    $line = @($Stdout -split "`r?`n" | Where-Object {
        $_.StartsWith("PRIMEFORGE_NATIVE_BATCH_PLAN`t")
    } | Select-Object -Last 1)
    if ($line.Count -eq 0) { return 'UNKNOWN' }
    return ($line[0] -split "`t", 2)[1]
}

function Get-PhaseSummary {
    param([Parameter(Mandatory)][string]$Stdout)
    $sums = [ordered]@{}
    foreach ($line in ($Stdout -split "`r?`n")) {
        if (-not $line.StartsWith("PRIMEFORGE_PHASE`t")) { continue }
        $fields = $line -split "`t"
        if ($fields.Count -eq 6) {
            $name = $fields[4]
            $nanoseconds = [uint64]$fields[5]
        } elseif ($fields.Count -eq 7) {
            $name = $fields[5]
            $nanoseconds = [uint64]$fields[6]
        } else { continue }
        if (-not $sums.Contains($name)) { $sums[$name] = [uint64]0 }
        $sums[$name] = [uint64]$sums[$name] + $nanoseconds
    }
    return $sums
}

function Get-KernelProfile {
    param([Parameter(Mandatory)][string]$Stdout)
    $groups = [ordered]@{
        forward_ntt = [ordered]@{ launches = [uint64]0; nanoseconds = [uint64]0 }
        inverse_ntt = [ordered]@{ launches = [uint64]0; nanoseconds = [uint64]0 }
        pointwise_square = [ordered]@{ launches = [uint64]0; nanoseconds = [uint64]0 }
        reduction = [ordered]@{ launches = [uint64]0; nanoseconds = [uint64]0 }
        poly2int = [ordered]@{ launches = [uint64]0; nanoseconds = [uint64]0 }
        other = [ordered]@{ launches = [uint64]0; nanoseconds = [uint64]0 }
    }
    $kernels = [Collections.Generic.List[object]]::new()
    foreach ($line in ($Stdout -split "`r?`n")) {
        if (-not $line.StartsWith("PRIMEFORGE_KERNEL`t")) { continue }
        $fields = $line -split "`t"
        if ($fields.Count -ne 7) { continue }
        $name = $fields[4]
        $launches = [uint64]$fields[5]
        $nanoseconds = [uint64]$fields[6]
        $group = if ($name -match '^(lst_intt|intt)') { 'inverse_ntt' }
            elseif ($name -match '^(sub_ntt|ntt)') { 'forward_ntt' }
            elseif ($name -match '^square') { 'pointwise_square' }
            elseif ($name -match '^poly2int') { 'poly2int' }
            elseif ($name -match '^reduce_') { 'reduction' }
            else { 'other' }
        $groups[$group].launches = [uint64]$groups[$group].launches + $launches
        $groups[$group].nanoseconds = [uint64]$groups[$group].nanoseconds + $nanoseconds
        $kernels.Add([pscustomobject][ordered]@{
            name = $name
            group = $group
            launches = $launches
            nanoseconds = $nanoseconds
            mean_nanoseconds_per_launch = if ($launches -eq 0) { 0.0 } else { [double]$nanoseconds / [double]$launches }
        })
    }
    [uint64]$totalLaunches = 0
    [uint64]$totalNanoseconds = 0
    foreach ($group in $groups.Values) {
        $totalLaunches += [uint64]$group.launches
        $totalNanoseconds += [uint64]$group.nanoseconds
    }
    $groupRows = [ordered]@{}
    foreach ($name in $groups.Keys) {
        $value = $groups[$name]
        $groupRows[$name] = [ordered]@{
            launches = [uint64]$value.launches
            nanoseconds = [uint64]$value.nanoseconds
            seconds = [double]$value.nanoseconds / 1.0e9
            percent_of_profiled_event_time = if ($totalNanoseconds -eq 0) { 'UNKNOWN' } else {
                100.0 * [double]$value.nanoseconds / [double]$totalNanoseconds
            }
        }
    }
    return [pscustomobject][ordered]@{
        total_launches = $totalLaunches
        total_event_nanoseconds = $totalNanoseconds
        duration_distribution = 'AGGREGATE_PER_KERNEL_ONLY'
        groups = $groupRows
        kernels = @($kernels | Sort-Object nanoseconds -Descending)
    }
}

function Get-DetectedDouble {
    param($Field)
    if ($null -eq $Field -or $Field.status -ne 'DETECTED' -or $Field.value -eq 'UNKNOWN') { return $null }
    return [double]::Parse([string]$Field.value, $script:invariant)
}

function Get-TelemetrySummary {
    param([Parameter(Mandatory)][string]$Path)
    $samples = [Collections.Generic.List[object]]::new()
    if (Test-Path -LiteralPath $Path -PathType Leaf) {
        $stream = [IO.FileStream]::new(
            $Path, [IO.FileMode]::Open, [IO.FileAccess]::Read,
            [IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete)
        try {
            $reader = [IO.StreamReader]::new($stream, [Text.Encoding]::UTF8, $true, 4096, $true)
            try { $telemetryText = $reader.ReadToEnd() } finally { $reader.Dispose() }
        } finally { $stream.Dispose() }
        foreach ($line in ($telemetryText -split "`r?`n")) {
            if ([string]::IsNullOrWhiteSpace($line)) { continue }
            try { $samples.Add(($line | ConvertFrom-Json)) } catch { }
        }
    }
    $metrics = [ordered]@{}
    foreach ($specification in @(
        @('cpu_temperature_c', 'cpu_temperature_celsius'),
        @('cpu_power_w', 'cpu_power_watts'),
        @('cpu_frequency_mhz', 'cpu_frequency_mhz'),
        @('gpu_temperature_c', 'gpu_temperature_celsius'),
        @('gpu_power_w', 'gpu_power_watts'),
        @('gpu_utilization_percent', 'gpu_utilization_percent'),
        @('gpu_memory_utilization_percent', 'gpu_memory_utilization_percent'),
        @('gpu_sm_clock_mhz', 'gpu_sm_clock_mhz'),
        @('gpu_memory_clock_mhz', 'gpu_memory_clock_mhz'),
        @('vram_used_mib', 'vram_used_mib'),
        @('ram_used_bytes', 'ram_used_bytes'),
        @('ram_available_bytes', 'ram_available_bytes')
    )) {
        $values = [Collections.Generic.List[double]]::new()
        foreach ($sample in $samples) {
            $value = Get-DetectedDouble $sample.($specification[1])
            if ($null -ne $value) { $values.Add($value) }
        }
        $metrics[$specification[0]] = if ($values.Count -eq 0) { 'UNKNOWN' } else { [ordered]@{
            samples = $values.Count
            minimum = [double](($values | Measure-Object -Minimum).Minimum)
            mean = [double](($values | Measure-Object -Average).Average)
            maximum = [double](($values | Measure-Object -Maximum).Maximum)
        }}
    }
    $whea = @($samples | ForEach-Object {
        if ($_.whea_errors_recent.status -eq 'DETECTED') { [uint64]$_.whea_errors_recent.value }
    })
    return [pscustomobject][ordered]@{
        samples = $samples.Count
        metrics = $metrics
        throttling_samples = @($samples | Where-Object throttling_detected).Count
        throttling_reasons = @($samples | Where-Object throttling_detected | ForEach-Object throttling_reasons | Sort-Object -Unique)
        maximum_recent_whea_errors = if ($whea.Count -eq 0) { 'UNKNOWN' } else { [uint64](($whea | Measure-Object -Maximum).Maximum) }
    }
}

$executablePath = Resolve-ProjectPath $Executable
$inputPath = Resolve-ProjectPath $InputFile
$outputPath = Resolve-ProjectPath $OutputDirectory
$monitorPath = Resolve-ProjectPath $HardwareMonitor
foreach ($path in @($executablePath, $inputPath, $monitorPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required file missing: $path" }
}
[IO.Directory]::CreateDirectory($outputPath) | Out-Null

$inputRows = @([IO.File]::ReadLines($inputPath) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
$batch = $inputRows.Count
if ($batch -lt 1 -or $batch -gt 64) { throw "Unsupported native batch size: $batch" }

$stdoutPath = Join-Path $outputPath ($RunId + '.stdout.log')
$stderrPath = Join-Path $outputPath ($RunId + '.stderr.log')
$telemetryPath = Join-Path $outputPath ($RunId + '.telemetry.jsonl')
$telemetryErrorPath = Join-Path $outputPath ($RunId + '.telemetry.stderr.log')
$processSamplesPath = Join-Path $outputPath ($RunId + '.process.tsv')
$summaryPath = Join-Path $outputPath ($RunId + '.summary.json')

$monitor = Start-Process -FilePath $monitorPath -ArgumentList @(
    '--samples', '0', '--interval-ms', [string]$SampleIntervalMs
) -RedirectStandardOutput $telemetryPath -RedirectStandardError $telemetryErrorPath -WindowStyle Hidden -PassThru

$startInfo = [Diagnostics.ProcessStartInfo]::new()
$startInfo.FileName = $executablePath
$startInfo.ArgumentList.Add('--native-batch')
$startInfo.ArgumentList.Add($inputPath)
$startInfo.ArgumentList.Add('--phase-profile')
if ($KernelProfile) { $startInfo.ArgumentList.Add('--kernel-profile') }
$startInfo.WorkingDirectory = $repositoryRoot
$startInfo.UseShellExecute = $false
$startInfo.CreateNoWindow = $true
$startInfo.RedirectStandardOutput = $true
$startInfo.RedirectStandardError = $true

$process = [Diagnostics.Process]::new()
$process.StartInfo = $startInfo
$processSamples = [Collections.Generic.List[object]]::new()
$clock = [Diagnostics.Stopwatch]::StartNew()
$timedOut = $false
try {
    if (-not $process.Start()) { throw 'Failed to start native batch.' }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    while (-not $process.WaitForExit($SampleIntervalMs)) {
        $process.Refresh()
        $processSamples.Add([pscustomobject][ordered]@{
            elapsed_seconds = $clock.Elapsed.TotalSeconds
            total_cpu_seconds = $process.TotalProcessorTime.TotalSeconds
            user_cpu_seconds = $process.UserProcessorTime.TotalSeconds
            privileged_cpu_seconds = $process.PrivilegedProcessorTime.TotalSeconds
            working_set_bytes = [uint64]$process.WorkingSet64
            private_bytes = [uint64]$process.PrivateMemorySize64
            paged_bytes = [uint64]$process.PagedMemorySize64
            virtual_bytes = [uint64]$process.VirtualMemorySize64
            thread_count = $process.Threads.Count
            handle_count = $process.HandleCount
        })
        if ($clock.Elapsed.TotalSeconds -gt $MaximumRunSeconds) {
            $timedOut = $true
            $process.Kill($true)
            $process.WaitForExit()
            break
        }
    }
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
} finally {
    $clock.Stop()
    if (-not $monitor.HasExited) {
        $monitor.Kill($true)
        $monitor.WaitForExit(10000)
    }
    $monitor.Dispose()
}

[IO.File]::WriteAllText($stdoutPath, $stdout, $utf8)
[IO.File]::WriteAllText($stderrPath, $stderr, $utf8)
$processTsv = @('elapsed_seconds`ttotal_cpu_seconds`tuser_cpu_seconds`tprivileged_cpu_seconds`tworking_set_bytes`tprivate_bytes`tpaged_bytes`tvirtual_bytes`tthread_count`thandle_count')
foreach ($sample in $processSamples) {
    $processTsv += @(
        $sample.elapsed_seconds.ToString('0.000000', $invariant),
        $sample.total_cpu_seconds.ToString('0.000000', $invariant),
        $sample.user_cpu_seconds.ToString('0.000000', $invariant),
        $sample.privileged_cpu_seconds.ToString('0.000000', $invariant),
        $sample.working_set_bytes, $sample.private_bytes, $sample.paged_bytes,
        $sample.virtual_bytes, $sample.thread_count, $sample.handle_count
    ) -join "`t"
}
[IO.File]::WriteAllLines($processSamplesPath, $processTsv, $utf8)

$result = Get-ResultDigest $stdout
$timing = Get-TaggedFields -Stdout $stdout -Prefix "PRIMEFORGE_NATIVE_BATCH_TIMING`t"
$readbackMatches = [regex]::Matches($stdout, "PRIMEFORGE_COUNTER`t[^`r`n]*`tRESULT_READBACK_BYTES`t([0-9]+)")
$readbackBytes = if ($readbackMatches.Count -eq 0) { [uint64]0 } else { [uint64]$readbackMatches[$readbackMatches.Count - 1].Groups[1].Value }
$transformLength = if ($readbackBytes -eq 0) { [uint64]0 } else { [uint64]($readbackBytes / (4 * $batch)) }
$exactBufferBytes = if ($transformLength -eq 0) { [uint64]0 } else {
    [uint64](60 * $transformLength * $batch + 32 * $transformLength + 87296 + 28 * $batch)
}
$cpuSeconds = if ($processSamples.Count -eq 0) { 0.0 } else { [double]$processSamples[$processSamples.Count - 1].total_cpu_seconds }
$summary = [pscustomobject][ordered]@{
    schema = 'primeforge.native_scaling_probe.v1'
    run_id = $RunId
    run_kind = $RunKind
    generated_utc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    purpose = 'BOUNDED_SCALING_ANALYSIS_NOT_DISCOVERY'
    executable = $executablePath
    executable_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $executablePath).Hash.ToLowerInvariant()
    input = $inputPath
    input_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $inputPath).Hash.ToLowerInvariant()
    digits = $Digits
    batch = $batch
    transform_length = $transformLength
    plan = Get-Plan $stdout
    kernel_profile_enabled = [bool]$KernelProfile
    exit_code = $process.ExitCode
    timed_out = $timedOut
    wall_seconds = $clock.Elapsed.TotalSeconds
    seconds_per_candidate = $clock.Elapsed.TotalSeconds / [double]$batch
    candidates_per_hour = 3600.0 * [double]$batch / $clock.Elapsed.TotalSeconds
    result_count = $result.count
    result_sha256 = $result.sha256
    result_records = $result.records
    gerbicz = if ($stdout -match 'gerbicz_status=PASS') { 'PASS' } else { 'FAIL_OR_MISSING' }
    native_timing = $timing
    host_phase_sums_nanoseconds = Get-PhaseSummary $stdout
    kernel_profile = Get-KernelProfile $stdout
    result_readback_bytes = $readbackBytes
    exact_opencl_buffer_bytes_derived = $exactBufferBytes
    process = [ordered]@{
        samples = $processSamples.Count
        total_cpu_seconds = $cpuSeconds
        mean_cpu_core_equivalents = if ($clock.Elapsed.TotalSeconds -eq 0) { 0.0 } else { $cpuSeconds / $clock.Elapsed.TotalSeconds }
        mean_cpu_percent_of_machine = if ($clock.Elapsed.TotalSeconds -eq 0) { 0.0 } else {
            100.0 * $cpuSeconds / $clock.Elapsed.TotalSeconds / [Environment]::ProcessorCount
        }
        peak_working_set_bytes = if ($processSamples.Count -eq 0) { 'UNKNOWN' } else { [uint64](($processSamples.working_set_bytes | Measure-Object -Maximum).Maximum) }
        peak_private_bytes = if ($processSamples.Count -eq 0) { 'UNKNOWN' } else { [uint64](($processSamples.private_bytes | Measure-Object -Maximum).Maximum) }
        peak_threads = if ($processSamples.Count -eq 0) { 'UNKNOWN' } else { [int](($processSamples.thread_count | Measure-Object -Maximum).Maximum) }
    }
    telemetry = Get-TelemetrySummary $telemetryPath
    limitations = @(
        'SM_OCCUPANCY_UNKNOWN_NO_LOW_OVERHEAD_COUNTER_PROVIDER',
        'MEMORY_THROUGHPUT_UNKNOWN_NO_LOW_OVERHEAD_COUNTER_PROVIDER',
        'STALL_REASONS_UNKNOWN_NO_LOW_OVERHEAD_COUNTER_PROVIDER',
        'CPU_GPU_WAIT_SPLIT_UNKNOWN_NOT_EXPOSED_BY_OPENCL_1_2_FAST_QUEUE',
        'KERNEL_DURATION_DISTRIBUTION_AGGREGATE_ONLY'
    )
}
[IO.File]::WriteAllText($summaryPath, ($summary | ConvertTo-Json -Depth 20) + "`n", $utf8)

Write-Host "scaling_probe.run=$RunId"
Write-Host "scaling_probe.kind=$RunKind"
Write-Host "scaling_probe.digits=$Digits"
Write-Host "scaling_probe.batch=$batch"
Write-Host "scaling_probe.transform=$transformLength"
Write-Host "scaling_probe.wall_seconds=$($clock.Elapsed.TotalSeconds.ToString('0.000000', $invariant))"
Write-Host "scaling_probe.candidates_per_hour=$((3600.0 * [double]$batch / $clock.Elapsed.TotalSeconds).ToString('0.000', $invariant))"
Write-Host "scaling_probe.result_sha256=$($result.sha256)"
Write-Host "scaling_probe.gerbicz=$($summary.gerbicz)"
Write-Host "scaling_probe.summary=$summaryPath"
if ($timedOut -or $process.ExitCode -ne 0 -or $result.count -ne $batch -or $summary.gerbicz -ne 'PASS') {
    throw "Scaling probe failed: run=$RunId exit=$($process.ExitCode) results=$($result.count)/$batch gerbicz=$($summary.gerbicz) timeout=$timedOut"
}
Write-Host 'scaling_probe.status=PASS'
