[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [Parameter(Mandatory = $true)][string]$Executable,
    [string]$CorpusDirectory = 'out\benchmarks\native-digit-scaling\corpus',
    [string]$OutputDirectory = 'out\benchmarks\native-digit-scaling\baseline',
    [int[]]$DigitCounts = @(20000, 40000, 60000, 80000, 100000),
    [ValidateRange(30, 900)][int]$MaximumRunSeconds = 600
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

$executablePath = Resolve-ProjectPath $Executable
$corpusPath = Resolve-ProjectPath $CorpusDirectory
$outputPath = Resolve-ProjectPath $OutputDirectory
if (-not (Test-Path -LiteralPath $executablePath -PathType Leaf)) { throw "Executable missing: $executablePath" }
if (-not (Test-Path -LiteralPath (Join-Path $corpusPath 'manifest.json') -PathType Leaf)) { throw "Corpus manifest missing: $corpusPath" }
[IO.Directory]::CreateDirectory($outputPath) | Out-Null

function Start-GpuSampler {
    param([Parameter(Mandatory)][string]$OutputFile)
    $nvidiaSmi = (Get-Command nvidia-smi.exe -ErrorAction Stop).Source
    return Start-Process -FilePath $nvidiaSmi -ArgumentList @(
        '--query-gpu=timestamp,temperature.gpu,power.draw,memory.used,utilization.gpu,clocks.sm,clocks.mem',
        '--format=csv,noheader,nounits', '--loop-ms=250'
    ) -RedirectStandardOutput $OutputFile -RedirectStandardError ($OutputFile + '.stderr') -WindowStyle Hidden -PassThru
}

function Stop-GpuSampler {
    param([Parameter(Mandatory)][Diagnostics.Process]$Process)
    if (-not $Process.HasExited) {
        Stop-Process -Id $Process.Id -ErrorAction Stop
        $Process.WaitForExit()
    }
}

function Invoke-BoundedBatch {
    param(
        [Parameter(Mandatory)][string]$InputFile,
        [Parameter(Mandatory)][string]$Prefix,
        [switch]$KernelProfile,
        [switch]$SampleGpu
    )

    $stdoutFile = $Prefix + '.stdout.log'
    $stderrFile = $Prefix + '.stderr.log'
    $gpuFile = $Prefix + '.gpu.csv'
    $arguments = "--native-batch `"$($InputFile.Replace('"', '\"'))`" --phase-profile"
    if ($KernelProfile) { $arguments += ' --kernel-profile' }

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $script:executablePath
    $startInfo.Arguments = $arguments
    $startInfo.WorkingDirectory = $script:repositoryRoot
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true

    $sampler = $null
    if ($SampleGpu) { $sampler = Start-GpuSampler -OutputFile $gpuFile }
    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    $clock = [Diagnostics.Stopwatch]::StartNew()
    try {
        if (-not $process.Start()) { throw 'Failed to start native batch.' }
        $stdoutTask = $process.StandardOutput.ReadToEndAsync()
        $stderrTask = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($script:MaximumRunSeconds * 1000)) {
            $process.Kill($true)
            $process.WaitForExit()
            throw "Native batch exceeded the ${MaximumRunSeconds}s safety bound."
        }
        $stdout = $stdoutTask.GetAwaiter().GetResult()
        $stderr = $stderrTask.GetAwaiter().GetResult()
    } finally {
        $clock.Stop()
        if ($null -ne $sampler) { Stop-GpuSampler $sampler }
    }

    [IO.File]::WriteAllText($stdoutFile, $stdout, $script:utf8)
    [IO.File]::WriteAllText($stderrFile, $stderr, $script:utf8)
    return [pscustomobject]@{
        exit_code = $process.ExitCode
        wall_seconds = $clock.Elapsed.TotalSeconds
        stdout = $stdout
        stderr = $stderr
        stdout_file = $stdoutFile
        stderr_file = $stderrFile
        gpu_file = if ($SampleGpu) { $gpuFile } else { $null }
    }
}

function Get-ResultDigest {
    param([Parameter(Mandatory)][string]$Stdout)
    $records = @($Stdout -split "`r?`n" | Where-Object { $_.StartsWith("PRIMEFORGE_NATIVE_BATCH_RESULT`t") } | Sort-Object {
        [int](($_ -split "`t")[1])
    })
    $canonical = ($records -join "`n") + "`n"
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        $hash = $sha.ComputeHash($script:utf8.GetBytes($canonical))
        return [pscustomobject]@{ count = $records.Count; sha256 = -join ($hash | ForEach-Object { $_.ToString('x2') }) }
    } finally { $sha.Dispose() }
}

function Get-Plan {
    param([Parameter(Mandatory)][string]$Stdout)
    $line = @($Stdout -split "`r?`n" | Where-Object { $_.StartsWith("PRIMEFORGE_NATIVE_BATCH_PLAN`t") } | Select-Object -Last 1)
    if ($line.Count -eq 0) { return 'UNKNOWN' }
    return ($line[0] -split "`t", 2)[1]
}

function Get-Timing {
    param([Parameter(Mandatory)][string]$Stdout)
    $line = @($Stdout -split "`r?`n" | Where-Object { $_.StartsWith("PRIMEFORGE_NATIVE_BATCH_TIMING`t") } | Select-Object -Last 1)
    $values = [ordered]@{}
    if ($line.Count -eq 0) { return $values }
    $fields = $line[0] -split "`t"
    for ($index = 1; $index -lt $fields.Count; ++$index) {
        $field = $fields[$index]
        $parts = $field -split '=', 2
        if ($parts.Count -eq 2) { $values[$parts[0]] = $parts[1] }
    }
    return $values
}

function Get-PhaseSums {
    param([Parameter(Mandatory)][string]$Stdout)
    $sums = [ordered]@{}
    foreach ($line in ($Stdout -split "`r?`n")) {
        if (-not $line.StartsWith("PRIMEFORGE_PHASE`t")) { continue }
        $fields = $line -split "`t"
        if ($fields.Count -ne 7) { continue }
        $name = $fields[5]
        $value = [uint64]$fields[6]
        if (-not $sums.Contains($name)) { $sums[$name] = [uint64]0 }
        $sums[$name] = [uint64]$sums[$name] + $value
    }
    return $sums
}

function Get-KernelGroups {
    param([Parameter(Mandatory)][string]$Stdout, [Parameter(Mandatory)][double]$ProfileWallMs)
    $groups = [ordered]@{
        forward_ntt = [uint64]0
        pointwise_square = [uint64]0
        inverse_ntt = [uint64]0
        reduction = [uint64]0
        poly2int = [uint64]0
        normalization_other = [uint64]0
    }
    $kernels = [Collections.Generic.List[object]]::new()
    foreach ($line in ($Stdout -split "`r?`n")) {
        if (-not $line.StartsWith("PRIMEFORGE_KERNEL`t")) { continue }
        $fields = $line -split "`t"
        if ($fields.Count -ne 7) { continue }
        $name = $fields[4]
        $count = [uint64]$fields[5]
        $nanoseconds = [uint64]$fields[6]
        $group = if ($name -match '^(lst_intt|intt)') { 'inverse_ntt' }
            elseif ($name -match '^(sub_ntt|ntt)') { 'forward_ntt' }
            elseif ($name -match '^square') { 'pointwise_square' }
            elseif ($name -match '^poly2int') { 'poly2int' }
            elseif ($name -match '^reduce_') { 'reduction' }
            else { 'normalization_other' }
        $groups[$group] = [uint64]$groups[$group] + $nanoseconds
        $kernels.Add([pscustomobject][ordered]@{ name = $name; group = $group; count = $count; nanoseconds = $nanoseconds })
    }
    $total = [uint64]0
    foreach ($value in $groups.Values) { $total += [uint64]$value }
    $summary = [ordered]@{}
    foreach ($name in $groups.Keys) {
        $ns = [uint64]$groups[$name]
        $summary[$name] = [ordered]@{
            milliseconds = ([double]$ns / 1000000.0).ToString('0.000000', $script:invariant)
            percent_of_profiled_gpu_time = if ($total -eq 0) { 'UNKNOWN' } else { (([double]$ns * 100.0 / $total).ToString('0.000000', $script:invariant)) }
            percent_of_profiled_wall = if ($ProfileWallMs -le 0) { 'UNKNOWN' } else { (([double]$ns / 1000000.0 * 100.0 / $ProfileWallMs).ToString('0.000000', $script:invariant)) }
        }
    }
    return [pscustomobject]@{ total_event_nanoseconds = $total; groups = $summary; kernels = @($kernels) }
}

function Get-GpuSummary {
    param([string]$Path)
    if ([string]::IsNullOrWhiteSpace($Path) -or -not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $null }
    $rows = [Collections.Generic.List[object]]::new()
    foreach ($line in (Get-Content -LiteralPath $Path)) {
        $fields = $line -split ',' | ForEach-Object { $_.Trim() }
        if ($fields.Count -lt 7) { continue }
        try {
            $rows.Add([pscustomobject]@{
                temperature_c = [double]::Parse($fields[1], $script:invariant)
                power_w = [double]::Parse($fields[2], $script:invariant)
                vram_mib = [double]::Parse($fields[3], $script:invariant)
                utilization_percent = [double]::Parse($fields[4], $script:invariant)
                sm_clock_mhz = [double]::Parse($fields[5], $script:invariant)
                memory_clock_mhz = [double]::Parse($fields[6], $script:invariant)
            })
        } catch { continue }
    }
    if ($rows.Count -eq 0) { return $null }
    return [pscustomobject][ordered]@{
        samples = $rows.Count
        temperature_max_c = ($rows | Measure-Object temperature_c -Maximum).Maximum
        power_mean_w = ($rows | Measure-Object power_w -Average).Average
        power_max_w = ($rows | Measure-Object power_w -Maximum).Maximum
        vram_max_mib = ($rows | Measure-Object vram_mib -Maximum).Maximum
        utilization_mean_percent = ($rows | Measure-Object utilization_percent -Average).Average
        utilization_max_percent = ($rows | Measure-Object utilization_percent -Maximum).Maximum
        sm_clock_mean_mhz = ($rows | Measure-Object sm_clock_mhz -Average).Average
        memory_clock_mean_mhz = ($rows | Measure-Object memory_clock_mhz -Average).Average
    }
}

$cases = [Collections.Generic.List[object]]::new()
foreach ($digits in $DigitCounts) {
    $batch = 32
    $input = Join-Path $corpusPath ('digits-{0:D6}-b{1}.txt' -f $digits, $batch)
    if (-not (Test-Path -LiteralPath $input -PathType Leaf)) { throw "Corpus input missing: $input" }
    $casePath = Join-Path $outputPath ('digits-{0:D6}-b{1}' -f $digits, $batch)
    [IO.Directory]::CreateDirectory($casePath) | Out-Null

    Write-Host "native_scaling.case.start=digits:$digits,batch:$batch"
    $warmup = Invoke-BoundedBatch -InputFile $input -Prefix (Join-Path $casePath '01-warmup')
    if ($warmup.exit_code -ne 0) { throw "B32 warm-up failed at $digits digits: $($warmup.stderr)" }
    $wall = Invoke-BoundedBatch -InputFile $input -Prefix (Join-Path $casePath '02-measured') -SampleGpu
    if ($wall.exit_code -ne 0) { throw "B32 measured run failed at $digits digits: $($wall.stderr)" }
    $profile = Invoke-BoundedBatch -InputFile $input -Prefix (Join-Path $casePath '03-profiled') -KernelProfile -SampleGpu
    if ($profile.exit_code -ne 0) { throw "B32 profiled run failed at $digits digits: $($profile.stderr)" }

    $warmupResult = Get-ResultDigest $warmup.stdout
    $wallResult = Get-ResultDigest $wall.stdout
    $profileResult = Get-ResultDigest $profile.stdout
    if ($wallResult.count -ne $batch -or $warmupResult.sha256 -cne $wallResult.sha256 -or $wallResult.sha256 -cne $profileResult.sha256) {
        throw "Mathematical results differ at $digits digits."
    }
    foreach ($run in @($warmup, $wall, $profile)) {
        if ($run.stdout -notmatch 'gerbicz_status=PASS') { throw "Gerbicz did not pass at $digits digits." }
    }

    $timing = Get-Timing $wall.stdout
    $phases = Get-PhaseSums $wall.stdout
    $kernelGroups = Get-KernelGroups -Stdout $profile.stdout -ProfileWallMs ($profile.wall_seconds * 1000.0)
    $readbackMatch = [regex]::Matches($wall.stdout, "PRIMEFORGE_COUNTER`t[^`r`n]*`tRESULT_READBACK_BYTES`t([0-9]+)") | Select-Object -Last 1
    $readbackBytes = if ($null -eq $readbackMatch) { [uint64]0 } else { [uint64]$readbackMatch.Groups[1].Value }
    $transformLength = if ($readbackBytes -eq 0) { 0 } else { [uint64]($readbackBytes / (4 * $batch)) }
    $exactBufferBytes = if ($transformLength -eq 0) { 0 } else { [uint64](60 * $transformLength * $batch + 32 * $transformLength + 87296 + 28 * $batch) }

    $case = [pscustomobject][ordered]@{
        digits = $digits
        batch = $batch
        input = $input
        input_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $input).Hash.ToLowerInvariant()
        transform_length = $transformLength
        plan_warmup = Get-Plan $warmup.stdout
        plan_measured = Get-Plan $wall.stdout
        plan_profiled = Get-Plan $profile.stdout
        warmup_wall_seconds = $warmup.wall_seconds
        measured_wall_seconds = $wall.wall_seconds
        profiled_wall_seconds = $profile.wall_seconds
        measured_candidates_per_hour = $batch * 3600.0 / $wall.wall_seconds
        result_count = $wallResult.count
        result_sha256 = $wallResult.sha256
        classifications_witnesses_res64_identical = $true
        gerbicz = 'PASS'
        timing = $timing
        phase_sums_nanoseconds = $phases
        kernel_profile = $kernelGroups
        gpu_measured = Get-GpuSummary $wall.gpu_file
        gpu_profiled = Get-GpuSummary $profile.gpu_file
        exact_opencl_buffer_bytes_derived = $exactBufferBytes
        synchronization_time = 'UNKNOWN_NOT_EXPOSED_BY_OPENCL_1_2_FAST_QUEUE'
        register_usage = 'UNKNOWN_NVIDIA_OPENCL_DOES_NOT_EXPOSE_REGISTERS'
    }
    $cases.Add($case)
    [IO.File]::WriteAllText((Join-Path $casePath 'summary.json'), ($case | ConvertTo-Json -Depth 12) + "`n", $utf8)
    Write-Host ("native_scaling.case.pass=digits:{0},transform:{1},wall_s:{2:F6},cph:{3:F3}" -f $digits, $transformLength, $wall.wall_seconds, ($batch * 3600.0 / $wall.wall_seconds))
}

$summary = [pscustomobject][ordered]@{
    schema = 'primeforge.native_digit_scaling.v1'
    generated_utc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    executable = $executablePath
    executable_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $executablePath).Hash.ToLowerInvariant()
    corpus_manifest = Join-Path $corpusPath 'manifest.json'
    corpus_manifest_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $corpusPath 'manifest.json')).Hash.ToLowerInvariant()
    purpose = 'BOUNDED_SCALING_ANALYSIS_NOT_DISCOVERY'
    cases = @($cases)
}
$summaryPath = Join-Path $outputPath 'summary.json'
[IO.File]::WriteAllText($summaryPath, ($summary | ConvertTo-Json -Depth 14) + "`n", $utf8)
Write-Host "native_scaling.summary=$summaryPath"
Write-Host "native_scaling.summary_sha256=$((Get-FileHash -Algorithm SHA256 -LiteralPath $summaryPath).Hash.ToLowerInvariant())"
Write-Host 'native_scaling.status=PASS'
