[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [string]$CampaignDirectory = 'out\discovery\proth-n33221-k10001-90001',
    [uint32]$KStart = 10001,
    [uint32]$KStop = 90001,
    [uint32]$Exponent = 33221,
    [uint64]$SieveBound = 65521,
    [ValidateRange(1, 64)][int]$SieveThreads = 1,
    [ValidateRange(1, 1000)][int]$BatchSize = 100,
    [ValidateRange(0, 100000000)][int]$ValidationCount = 0,
    [ValidateRange(0, 31)][int]$Device = 0,
    [string]$SieveExecutable = 'out\build\msvc-release\primeforge-discovery-sieve.exe',
    [string]$Proth20Executable = 'out\oracles\proth20-batch\proth20-batch.exe',
    [string]$WatchdogExecutable = 'out\build\msvc-release\benchmark_watchdog.exe',
    [switch]$PrepareOnly
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = Split-Path -Parent $PSScriptRoot
$utf8 = [System.Text.UTF8Encoding]::new($false)

function Resolve-ProjectPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $Path))
}

function Write-AtomicUtf8 {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Content
    )
    $temporary = $Path + '.tmp'
    [System.IO.File]::WriteAllText($temporary, $Content, $script:utf8)
    Move-Item -LiteralPath $temporary -Destination $Path -Force
}

function Get-Sha256Text {
    param([Parameter(Mandatory = $true)][string]$Text)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = $script:utf8.GetBytes($Text)
        return (($sha.ComputeHash($bytes) | ForEach-Object { $_.ToString('x2') }) -join '')
    } finally {
        $sha.Dispose()
    }
}

function Read-CompletedResults {
    param(
        [Parameter(Mandatory = $true)][string]$LogDirectory,
        [Parameter(Mandatory = $true)][hashtable]$AllowedCandidates
    )
    $completed = @{}
    $pattern = '^PRIMEFORGE_BATCH_COMPLETE\t([0-9]+)\t([0-9]+)\t([0-9]+)\t(PROVEN_PRIME|COMPOSITE)$'
    foreach ($log in @(Get-ChildItem -LiteralPath $LogDirectory -Filter 'unit-*-attempt-*.stdout.log' -File | Sort-Object Name)) {
        foreach ($line in [System.IO.File]::ReadLines($log.FullName)) {
            if ($line -notmatch $pattern) { continue }
            $k = [uint32]$Matches[2]
            $n = [uint32]$Matches[3]
            $status = $Matches[4]
            if (-not $AllowedCandidates.ContainsKey($k) -or $AllowedCandidates[$k] -ne $n) {
                throw "Log contains a candidate outside the immutable campaign: $k * 2^$n + 1"
            }
            if ($completed.ContainsKey($k)) {
                if ($completed[$k].status -ne $status) {
                    throw "Conflicting verdicts for k=$k"
                }
                continue
            }
            $completed[$k] = [pscustomobject][ordered]@{
                k = $k
                n = $n
                status = $status
                log = $log.Name
            }
        }
    }
    return $completed
}

function Write-CampaignState {
    param(
        [Parameter(Mandatory = $true)][hashtable]$Completed,
        [Parameter(Mandatory = $true)][object[]]$Candidates,
        [Parameter(Mandatory = $true)][string]$CampaignId,
        [Parameter(Mandatory = $true)][string]$Directory
    )
    $rows = [System.Collections.Generic.List[string]]::new()
    $rows.Add("k`tn`tprimality_status`tsource_log")
    foreach ($candidate in $Candidates) {
        if ($Completed.ContainsKey([uint32]$candidate.k)) {
            $item = $Completed[[uint32]$candidate.k]
            $rows.Add("$($item.k)`t$($item.n)`t$($item.status)`t$($item.log)")
        }
    }
    Write-AtomicUtf8 -Path (Join-Path $Directory 'results.tsv') -Content (($rows -join "`n") + "`n")

    $prime = @($Completed.Values | Where-Object status -eq 'PROVEN_PRIME' | Sort-Object k | Select-Object -First 1)
    $next = @($Candidates | Where-Object { -not $Completed.ContainsKey([uint32]$_.k) } | Select-Object -First 1)
    $campaignStatus = if ($prime.Count -gt 0) {
        'PRIME_FOUND'
    } elseif ($Completed.Count -eq $Candidates.Count) {
        'COMPLETE_NO_PRIME'
    } else {
        'IN_PROGRESS'
    }
    $checkpoint = [pscustomobject][ordered]@{
        schema = 'primeforge.discovery.checkpoint.v1'
        campaign_id = $CampaignId
        campaign_status = $campaignStatus
        total_candidates = $Candidates.Count
        completed_candidates = $Completed.Count
        next_k = if ($next.Count -eq 0) { $null } else { [uint32]$next[0].k }
        proven_prime_k = if ($prime.Count -eq 0) { $null } else { [uint32]$prime[0].k }
        updated_utc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    }
    Write-AtomicUtf8 -Path (Join-Path $Directory 'checkpoint.json') `
        -Content (($checkpoint | ConvertTo-Json -Compress) + "`n")
    return $checkpoint
}

function Show-NewLogText {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][ref]$Position,
        [switch]$ErrorStream
    )
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return }
    $stream = [System.IO.File]::Open($Path, 'Open', 'Read', 'ReadWrite')
    try {
        [void]$stream.Seek($Position.Value, 'Begin')
        $reader = [System.IO.StreamReader]::new($stream, $script:utf8, $true, 4096, $true)
        try { $text = $reader.ReadToEnd() } finally { $reader.Dispose() }
        $Position.Value = $stream.Position
        if ($text.Length -gt 0) {
            if ($ErrorStream) { [Console]::Error.Write($text) } else { [Console]::Out.Write($text) }
        }
    } finally {
        $stream.Dispose()
    }
}

$campaignPath = Resolve-ProjectPath $CampaignDirectory
$sievePath = Resolve-ProjectPath $SieveExecutable
$prothPath = Resolve-ProjectPath $Proth20Executable
$watchdogPath = Resolve-ProjectPath $WatchdogExecutable
foreach ($required in @($sievePath, $prothPath, $watchdogPath)) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Required executable is missing: $required"
    }
}
if (($KStart % 2) -eq 0 -or ($KStop % 2) -eq 0 -or $KStart -gt $KStop) {
    throw 'KStart and KStop must be ordered odd values.'
}
if ($Exponent -lt 32 -or $KStop -ge [math]::Pow(2.0, [double]$Exponent)) {
    throw 'The requested range does not satisfy the supported Proth conditions.'
}

$logsPath = Join-Path $campaignPath 'logs'
$workPath = Join-Path $campaignPath 'work'
New-Item -ItemType Directory -Path $campaignPath, $logsPath, $workPath -Force | Out-Null

$proth20Hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $prothPath).Hash.ToLowerInvariant()
$identityText = "primeforge.discovery.proth.v1`nk_start=$KStart`nk_stop=$KStop`nn=$Exponent`nsieve_bound=$SieveBound`nvalidation_count=$ValidationCount`nbatch_size=$BatchSize`nproth20_sha256=$proth20Hash`n"
$campaignId = 'sha256:' + (Get-Sha256Text $identityText)
$manifestPath = Join-Path $campaignPath 'campaign.json'
$manifest = [pscustomobject][ordered]@{
    schema = 'primeforge.discovery.campaign.v1'
    campaign_id = $campaignId
    family = 'k*2^n+1'
    k_start = $KStart
    k_stop = $KStop
    exponent = $Exponent
    sieve_bound = $SieveBound
    validation_count = $ValidationCount
    batch_size = $BatchSize
    proth20_sha256 = $proth20Hash
}
if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
    $existing = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
    foreach ($field in @(
        'campaign_id', 'k_start', 'k_stop', 'exponent', 'sieve_bound',
        'validation_count', 'batch_size', 'proth20_sha256'
    )) {
        if ([string]$existing.$field -ne [string]$manifest.$field) {
            throw "Campaign manifest mismatch for $field; use a different directory."
        }
    }
} else {
    Write-AtomicUtf8 -Path $manifestPath -Content (($manifest | ConvertTo-Json -Compress) + "`n")
}

$survivorsPath = Join-Path $campaignPath 'survivors.txt'
if (-not (Test-Path -LiteralPath $survivorsPath -PathType Leaf)) {
    Write-Host '[PrimeForge] Criblage exact de la plage...'
    $sieveOutput = @(& $sievePath --k-start $KStart --k-stop $KStop --n $Exponent `
        --sieve-bound $SieveBound --threads $SieveThreads --output $survivorsPath 2>&1)
    $sieveExit = $LASTEXITCODE
    $sieveText = ($sieveOutput | ForEach-Object { $_.ToString() }) -join "`n"
    Write-AtomicUtf8 -Path (Join-Path $logsPath 'sieve.log') -Content ($sieveText + "`n")
    $sieveOutput | ForEach-Object { Write-Host $_ }
    if ($sieveExit -ne 0) { throw "Discovery sieve failed with exit code $sieveExit" }
}

$candidates = [System.Collections.Generic.List[object]]::new()
$seen = @{}
foreach ($line in [System.IO.File]::ReadLines($survivorsPath)) {
    if ($line -notmatch '^([0-9]+) ([0-9]+)$') { throw "Malformed survivor line: $line" }
    $k = [uint32]$Matches[1]
    $n = [uint32]$Matches[2]
    if ($k -lt $KStart -or $k -gt $KStop -or ($k % 2) -eq 0 -or $n -ne $Exponent -or $seen.ContainsKey($k)) {
        throw "Invalid or duplicate survivor: $line"
    }
    $seen[$k] = $n
    $candidates.Add([pscustomobject][ordered]@{ k = $k; n = $n })
}
if ($ValidationCount -gt 0 -and $ValidationCount -lt $candidates.Count) {
    $selected = @($candidates | Select-Object -First $ValidationCount)
} else {
    $selected = @($candidates)
}
$allowed = @{}
foreach ($candidate in $selected) { $allowed[[uint32]$candidate.k] = [uint32]$candidate.n }

$completed = Read-CompletedResults -LogDirectory $logsPath -AllowedCandidates $allowed
$checkpoint = Write-CampaignState -Completed $completed -Candidates $selected `
    -CampaignId $campaignId -Directory $campaignPath
Write-Host "[PrimeForge] Campagne : $campaignId"
Write-Host "[PrimeForge] Survivants retenus : $($selected.Count); deja traites : $($completed.Count)"
Write-Host "[PrimeForge] Dossier : $campaignPath"
if ($PrepareOnly) {
    Write-Host '[PrimeForge] Preparation terminee; aucun test Proth lance.'
    return
}
if ($checkpoint.campaign_status -ne 'IN_PROGRESS') {
    Write-Host "[PrimeForge] Etat terminal deja atteint : $($checkpoint.campaign_status)"
    return
}

$unitCount = [int][math]::Ceiling($selected.Count / [double]$BatchSize)
for ($unitIndex = 0; $unitIndex -lt $unitCount; ++$unitIndex) {
    $offset = $unitIndex * $BatchSize
    $last = [math]::Min($selected.Count - 1, $offset + $BatchSize - 1)
    $unit = @($selected[$offset..$last])
    $remaining = @($unit | Where-Object { -not $completed.ContainsKey([uint32]$_.k) })
    if ($remaining.Count -eq 0) { continue }

    $unitName = 'unit-{0:d4}' -f ($unitIndex + 1)
    $attempt = @(Get-ChildItem -LiteralPath $logsPath -Filter "$unitName-attempt-*.stdout.log" -File).Count + 1
    $attemptName = '{0}-attempt-{1:d3}' -f $unitName, $attempt
    $batchPath = Join-Path $workPath ($unitName + '.pending.txt')
    $batchText = (($remaining | ForEach-Object { "$($_.k) $($_.n)" }) -join "`n") + "`n"
    Write-AtomicUtf8 -Path $batchPath -Content $batchText

    $stdoutPath = Join-Path $logsPath ($attemptName + '.stdout.log')
    $stderrPath = Join-Path $logsPath ($attemptName + '.stderr.log')
    $watchdogStdout = Join-Path $logsPath ($attemptName + '.watchdog.log')
    $watchdogStderr = Join-Path $logsPath ($attemptName + '.watchdog-error.log')
    $telemetryLog = Join-Path $logsPath ($attemptName + '.telemetry.jsonl')
    $workerStop = Join-Path $workPath 'worker.stop'
    $operatorStop = Join-Path $workPath 'operator.stop'
    Remove-Item -LiteralPath $workerStop, $operatorStop -Force -ErrorAction SilentlyContinue

    Write-Host "[PrimeForge] $unitName : $($remaining.Count) candidat(s), tentative $attempt"
    $worker = Start-Process -FilePath $prothPath -ArgumentList @(
        '--device', $Device, '--batch', ('"' + $batchPath + '"'), '--stop-on-prime',
        '--stop-file', ('"' + $workerStop + '"')
    ) -WorkingDirectory $campaignPath -RedirectStandardOutput $stdoutPath `
        -RedirectStandardError $stderrPath -PassThru -NoNewWindow
    $watchdog = Start-Process -FilePath $watchdogPath -ArgumentList @(
        '--pid', $worker.Id,
        '--stop-file', ('"' + $workerStop + '"'),
        '--watchdog-stop-file', ('"' + $operatorStop + '"'),
        '--log', ('"' + $telemetryLog + '"'),
        '--campaign', $campaignId,
        '--interval-ms', 1000,
        '--grace-ms', 30000,
        '--max-cpu-temp-c', 92,
        '--max-gpu-temp-c', 88,
        '--require-cpu-temperature', '--require-cpu-power',
        '--require-gpu-temperature', '--require-gpu-power',
        '--require-ram-available', '--min-ram-available-bytes', 8589934592,
        '--require-vram-free', '--min-vram-free-mib', 2048,
        '--require-whea-status'
    ) -RedirectStandardOutput $watchdogStdout -RedirectStandardError $watchdogStderr -PassThru -NoNewWindow

    $stdoutPosition = [int64]0
    $stderrPosition = [int64]0
    try {
        while (-not $worker.HasExited) {
            Show-NewLogText -Path $stdoutPath -Position ([ref]$stdoutPosition)
            Show-NewLogText -Path $stderrPath -Position ([ref]$stderrPosition) -ErrorStream
            if ($watchdog.HasExited -and $watchdog.ExitCode -ne 0) {
                throw "Watchdog failed with exit code $($watchdog.ExitCode)"
            }
            Start-Sleep -Milliseconds 500
            $worker.Refresh()
            $watchdog.Refresh()
        }
    } finally {
        if (-not $worker.HasExited) {
            [System.IO.File]::WriteAllText($operatorStop, "stop`n", $utf8)
            if (-not $worker.WaitForExit(35000)) {
                Stop-Process -Id $worker.Id -Force -ErrorAction SilentlyContinue
            }
        }
    }
    $worker.WaitForExit()
    Show-NewLogText -Path $stdoutPath -Position ([ref]$stdoutPosition)
    Show-NewLogText -Path $stderrPath -Position ([ref]$stderrPosition) -ErrorStream
    if (-not $watchdog.WaitForExit(5000)) {
        Stop-Process -Id $watchdog.Id -Force -ErrorAction SilentlyContinue
        throw 'Watchdog did not observe the worker exit.'
    }

    $completed = Read-CompletedResults -LogDirectory $logsPath -AllowedCandidates $allowed
    $checkpoint = Write-CampaignState -Completed $completed -Candidates $selected `
        -CampaignId $campaignId -Directory $campaignPath
    Write-Host "[PrimeForge] Progression : $($completed.Count)/$($selected.Count); etat=$($checkpoint.campaign_status)"
    $cooperativeStop = Select-String -LiteralPath $stdoutPath `
        -Pattern '^PRIMEFORGE_BATCH_STOPPED$' -Quiet
    $watchdogDecision = @(Get-Content -LiteralPath $watchdogStdout |
        Where-Object { $_ -like 'benchmark_watchdog.decision=*' } | Select-Object -Last 1)
    if ($watchdogDecision.Count -ne 1 -or
        $watchdogDecision[0] -notin @(
            'benchmark_watchdog.decision=WORKER_EXITED reason=NONE',
            'benchmark_watchdog.decision=WORKER_EXITED reason=EXTERNAL_GRACEFUL_STOP'
        )) {
        throw "Watchdog did not record a normal worker exit; checkpoint preserved. Decision: $watchdogDecision"
    }
    $workerEvents = @([System.IO.File]::ReadLines($telemetryLog) | ForEach-Object {
        $event = $_ | ConvertFrom-Json
        if ($event.event_type -eq 'worker_exited') { $event }
    })
    if ($workerEvents.Count -ne 1 -or [string]($workerEvents[0].payload.exit_code) -ne '0') {
        throw 'The watchdog recorded a nonzero or missing proth20 exit code; checkpoint preserved.'
    }
    $completedInAttempt = @($remaining | Where-Object {
        $completed.ContainsKey([uint32]$_.k)
    }).Count
    if ($checkpoint.campaign_status -ne 'PRIME_FOUND' -and
        $completedInAttempt -ne $remaining.Count) {
        if ($cooperativeStop) {
            Write-Host '[PrimeForge] Arret cooperatif confirme; checkpoint preserve.'
            return
        }
        throw 'proth20 exited before completing its deterministic unit; checkpoint preserved.'
    }
    if ($checkpoint.campaign_status -ne 'IN_PROGRESS') { break }
}

$completed = Read-CompletedResults -LogDirectory $logsPath -AllowedCandidates $allowed
$checkpoint = Write-CampaignState -Completed $completed -Candidates $selected `
    -CampaignId $campaignId -Directory $campaignPath
Write-Host "[PrimeForge] Etat final : $($checkpoint.campaign_status)"
Write-Host "[PrimeForge] Resultats : $(Join-Path $campaignPath 'results.tsv')"
Write-Host "[PrimeForge] Checkpoint : $(Join-Path $campaignPath 'checkpoint.json')"
