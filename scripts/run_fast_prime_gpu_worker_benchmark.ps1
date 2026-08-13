[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [string]$SurvivorsFile = 'out\fast-prime\probe-campaign-20k-one-b1b\survivors.txt',
    [string]$OutputDirectory = 'out\fast-prime\gpu-worker-benchmark',
    [string]$Proth20Executable = 'C:\Users\sashack\source\repos\PrimeForge\out\oracles\proth20-batch\proth20-batch.exe',
    [string]$WatchdogExecutable = 'out\build\msvc-release\benchmark_watchdog.exe',
    [ValidateRange(0, 31)][int]$Device = 0,
    [switch]$TestHigherConcurrency
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$utf8 = [Text.UTF8Encoding]::new($false)
$culture = [Globalization.CultureInfo]::InvariantCulture

function Resolve-ProjectPath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $root $Path))
}

function Read-TelemetrySummary([string[]]$Paths) {
    $snapshots = [Collections.Generic.List[object]]::new()
    foreach ($path in $Paths) {
        foreach ($line in [IO.File]::ReadLines($path)) {
            $event = $line | ConvertFrom-Json
            if ($event.event_type -eq 'telemetry') { $snapshots.Add($event.payload.snapshot) }
        }
    }
    if ($snapshots.Count -eq 0) { throw 'No watchdog telemetry was recorded.' }
    return [pscustomobject][ordered]@{
        samples = $snapshots.Count
        max_cpu_temperature_c = [double](($snapshots.cpu_temperature_celsius.value | Measure-Object -Maximum).Maximum)
        max_gpu_temperature_c = [double](($snapshots.gpu_temperature_celsius.value | Measure-Object -Maximum).Maximum)
        max_gpu_power_w = [double](($snapshots.gpu_power_watts.value | Measure-Object -Maximum).Maximum)
        max_gpu_utilization_percent = [double](($snapshots.gpu_utilization_percent.value | Measure-Object -Maximum).Maximum)
        min_ram_available_bytes = [uint64](($snapshots.ram_available_bytes.value | Measure-Object -Minimum).Minimum)
        min_vram_free_mib = [uint64](($snapshots.vram_free_mib.value | Measure-Object -Minimum).Minimum)
        throttling_detected = @($snapshots | Where-Object throttling_detected).Count -ne 0
        max_recent_whea_errors = [uint64](($snapshots.whea_errors_recent.value | Measure-Object -Maximum).Maximum)
    }
}

function Invoke-WorkerGroup([string]$Name, [object[]]$Groups) {
    $variantPath = Join-Path $outputPath $Name
    New-Item -ItemType Directory -Path $variantPath -Force | Out-Null
    $workers = [Collections.Generic.List[object]]::new()
    $watchdogs = [Collections.Generic.List[object]]::new()
    $telemetryPaths = [Collections.Generic.List[string]]::new()
    $timer = [Diagnostics.Stopwatch]::StartNew()
    for ($index = 0; $index -lt $Groups.Count; ++$index) {
        $workerPath = Join-Path $variantPath ('worker-{0:d2}' -f ($index + 1))
        New-Item -ItemType Directory -Path $workerPath -Force | Out-Null
        $batchPath = Join-Path $workerPath 'batch.txt'
        $batchText = (($Groups[$index].candidates | ForEach-Object { "$(($_.k)) $(($_.n))" }) -join "`n") + "`n"
        [IO.File]::WriteAllText($batchPath, $batchText, $utf8)
        $stdout = Join-Path $workerPath 'stdout.log'
        $stderr = Join-Path $workerPath 'stderr.log'
        $workerStop = Join-Path $workerPath 'worker.stop'
        $operatorStop = Join-Path $workerPath 'operator.stop'
        $telemetry = Join-Path $workerPath 'telemetry.jsonl'
        $watchdogOut = Join-Path $workerPath 'watchdog.log'
        $watchdogErr = Join-Path $workerPath 'watchdog-error.log'
        $worker = Start-Process -FilePath $prothPath -ArgumentList @(
            '--device', $Device, '--batch', ('"' + $batchPath + '"'), '--stop-on-prime',
            '--stop-file', ('"' + $workerStop + '"')
        ) -WorkingDirectory $workerPath -RedirectStandardOutput $stdout `
            -RedirectStandardError $stderr -PassThru -NoNewWindow
        $watchdog = Start-Process -FilePath $watchdogPath -ArgumentList @(
            '--pid', $worker.Id, '--stop-file', ('"' + $workerStop + '"'),
            '--watchdog-stop-file', ('"' + $operatorStop + '"'),
            '--log', ('"' + $telemetry + '"'), '--campaign', "fast-prime-$Name",
            '--interval-ms', 1000, '--grace-ms', 30000,
            '--max-cpu-temp-c', 92, '--max-gpu-temp-c', 88,
            '--require-cpu-temperature', '--require-cpu-power',
            '--require-gpu-temperature', '--require-gpu-power',
            '--require-ram-available', '--min-ram-available-bytes', 8589934592,
            '--require-vram-free', '--min-vram-free-mib', 2048,
            '--require-whea-status'
        ) -RedirectStandardOutput $watchdogOut -RedirectStandardError $watchdogErr `
            -PassThru -NoNewWindow
        $workers.Add([pscustomobject]@{ process=$worker; stdout=$stdout; stderr=$stderr })
        $watchdogs.Add([pscustomobject]@{ process=$watchdog; stdout=$watchdogOut })
        $telemetryPaths.Add($telemetry)
    }
    foreach ($worker in $workers) {
        $worker.process.WaitForExit()
        $worker.process.Refresh()
        if ($null -ne $worker.process.ExitCode -and $worker.process.ExitCode -ne 0) {
            throw "$Name worker exited with $($worker.process.ExitCode)."
        }
        if ((Get-Item -LiteralPath $worker.stderr).Length -ne 0) { throw "$Name worker wrote stderr." }
    }
    $timer.Stop()
    foreach ($watchdog in $watchdogs) {
        if (-not $watchdog.process.WaitForExit(5000)) {
            Stop-Process -Id $watchdog.process.Id -Force -ErrorAction SilentlyContinue
            throw "$Name watchdog did not observe worker exit."
        }
        $watchdog.process.Refresh()
        if ($null -ne $watchdog.process.ExitCode -and $watchdog.process.ExitCode -ne 0) {
            throw "$Name watchdog exited with $($watchdog.process.ExitCode)."
        }
        $decision = @(Get-Content -LiteralPath $watchdog.stdout | Where-Object {
            $_ -like 'benchmark_watchdog.decision=*'
        } | Select-Object -Last 1)
        if ($decision.Count -ne 1 -or $decision[0] -ne 'benchmark_watchdog.decision=WORKER_EXITED reason=NONE') {
            throw "$Name watchdog decision was not a normal worker exit: $decision"
        }
    }
    foreach ($telemetryPath in $telemetryPaths) {
        $exitEvents = @([IO.File]::ReadLines($telemetryPath) | ForEach-Object {
            $event = $_ | ConvertFrom-Json
            if ($event.event_type -eq 'worker_exited') { $event }
        })
        if ($exitEvents.Count -ne 1 -or [string]$exitEvents[0].payload.exit_code -ne '0') {
            throw "$Name watchdog did not record a zero worker exit code."
        }
    }
    $verdicts = @{}
    foreach ($worker in $workers) {
        foreach ($line in [IO.File]::ReadLines($worker.stdout)) {
            if ($line -match '^PRIMEFORGE_BATCH_COMPLETE\t[0-9]+\t([0-9]+)\t([0-9]+)\t(PROVEN_PRIME|COMPOSITE)$') {
                $key = "$($Matches[1])/$($Matches[2])"
                if ($verdicts.ContainsKey($key)) { throw "$Name produced duplicate verdict $key." }
                $verdicts[$key] = $Matches[3]
            }
        }
    }
    $expected = @($Groups | ForEach-Object candidates).Count
    if ($verdicts.Count -ne $expected) { throw "$Name completed $($verdicts.Count)/$expected candidates." }
    $telemetry = Read-TelemetrySummary -Paths $telemetryPaths.ToArray()
    return [pscustomobject][ordered]@{
        schema = 'primeforge.fast_prime.gpu_workers.v1'
        recorded_utc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
        variant = $Name
        worker_count = $Groups.Count
        candidate_count = $expected
        elapsed_seconds = $timer.Elapsed.TotalSeconds
        candidates_per_hour = 3600.0 * $expected / $timer.Elapsed.TotalSeconds
        verdicts = $verdicts
        telemetry = $telemetry
        status = 'PASS'
    }
}

$survivorsPath = Resolve-ProjectPath $SurvivorsFile
$outputPath = Resolve-ProjectPath $OutputDirectory
$prothPath = Resolve-ProjectPath $Proth20Executable
$watchdogPath = Resolve-ProjectPath $WatchdogExecutable
foreach ($path in @($survivorsPath, $prothPath, $watchdogPath)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required file missing: $path" }
}
if (Test-Path -LiteralPath $outputPath) {
    if (@(Get-ChildItem -LiteralPath $outputPath -Force).Count -ne 0) {
        throw "Output directory must be new or empty: $outputPath"
    }
} else { New-Item -ItemType Directory -Path $outputPath -Force | Out-Null }

$candidates = @([IO.File]::ReadLines($survivorsPath) | Select-Object -First 5 | ForEach-Object {
    if ($_ -notmatch '^([0-9]+) ([0-9]+)$') { throw "Malformed survivor: $_" }
    [pscustomobject]@{ k=[uint32]$Matches[1]; n=[uint32]$Matches[2] }
})
if ($candidates.Count -ne 5) { throw 'At least five survivors are required.' }

$warmup = Invoke-WorkerGroup -Name 'warmup-excluded' -Groups @(
    [pscustomobject]@{ candidates=@($candidates[0]) }
)
$single = Invoke-WorkerGroup -Name 'one-worker' -Groups @(
    [pscustomobject]@{ candidates=@($candidates[1..4]) }
)
$dual = Invoke-WorkerGroup -Name 'two-workers' -Groups @(
    [pscustomobject]@{ candidates=@($candidates[1..2]) },
    [pscustomobject]@{ candidates=@($candidates[3..4]) }
)
foreach ($key in $single.verdicts.Keys) {
    if (-not $dual.verdicts.ContainsKey($key) -or $dual.verdicts[$key] -ne $single.verdicts[$key]) {
        throw "One/two-worker classification mismatch for $key."
    }
}
$records = [Collections.Generic.List[string]]::new()
foreach ($record in @($warmup, $single, $dual)) {
    $records.Add(($record | ConvertTo-Json -Depth 8 -Compress))
}
$three = $null
$four = $null
if ($TestHigherConcurrency) {
    $three = Invoke-WorkerGroup -Name 'three-workers' -Groups @(
        [pscustomobject]@{ candidates=@($candidates[1..2]) },
        [pscustomobject]@{ candidates=@($candidates[3]) },
        [pscustomobject]@{ candidates=@($candidates[4]) }
    )
    foreach ($key in $single.verdicts.Keys) {
        if (-not $three.verdicts.ContainsKey($key) -or $three.verdicts[$key] -ne $single.verdicts[$key]) {
            throw "One/three-worker classification mismatch for $key."
        }
    }
    $records.Add(($three | ConvertTo-Json -Depth 8 -Compress))
    if ($three.candidates_per_hour -gt $dual.candidates_per_hour) {
        $four = Invoke-WorkerGroup -Name 'four-workers' -Groups @(
            [pscustomobject]@{ candidates=@($candidates[1]) },
            [pscustomobject]@{ candidates=@($candidates[2]) },
            [pscustomobject]@{ candidates=@($candidates[3]) },
            [pscustomobject]@{ candidates=@($candidates[4]) }
        )
        foreach ($key in $single.verdicts.Keys) {
            if (-not $four.verdicts.ContainsKey($key) -or $four.verdicts[$key] -ne $single.verdicts[$key]) {
                throw "One/four-worker classification mismatch for $key."
            }
        }
        $records.Add(($four | ConvertTo-Json -Depth 8 -Compress))
    }
}
$raw = Join-Path $outputPath 'raw.jsonl'
[IO.File]::WriteAllLines($raw, $records, $utf8)
Write-Host ('fast_prime.gpu.one_worker_per_hour=' + $single.candidates_per_hour.ToString('0.000', $culture))
Write-Host ('fast_prime.gpu.two_workers_per_hour=' + $dual.candidates_per_hour.ToString('0.000', $culture))
Write-Host ('fast_prime.gpu.speedup=' + ($dual.candidates_per_hour / $single.candidates_per_hour).ToString('0.000', $culture))
if ($null -ne $three) {
    Write-Host ('fast_prime.gpu.three_workers_per_hour=' + $three.candidates_per_hour.ToString('0.000', $culture))
}
if ($null -ne $four) {
    Write-Host ('fast_prime.gpu.four_workers_per_hour=' + $four.candidates_per_hour.ToString('0.000', $culture))
}
Write-Host "fast_prime.gpu.raw=$raw"
Write-Host 'fast_prime.gpu.status=PASS'
