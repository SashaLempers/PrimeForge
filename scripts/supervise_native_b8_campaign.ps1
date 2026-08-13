[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [Parameter(Mandatory = $true)][string]$CampaignDirectory,
    [Parameter(Mandatory = $true)][string]$SchedulerPath,
    [Parameter(Mandatory = $true)][string]$QueuePath,
    [Parameter(Mandatory = $true)][string]$ParentCompletedPath,
    [Parameter(Mandatory = $true)][string]$EnginePath,
    [Parameter(Mandatory = $true)][string]$WatchdogPath,
    [Parameter(Mandatory = $true)][string]$CampaignId,
    [Parameter(Mandatory = $true)][string]$ParentCampaignId,
    [Parameter(Mandatory = $true)][string]$EngineCommit,
    [Parameter(Mandatory = $true)][string]$EngineSha256,
    [Parameter(Mandatory = $true)][string]$SurvivorSha256,
    [ValidateRange(0, 31)][int]$Device = 0,
    [switch]$Resume
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not ('PrimeForge.NativeAtomicFile' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace PrimeForge {
    public static class NativeAtomicFile {
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        public static extern bool MoveFileEx(string existingName, string newName, int flags);
    }
}
'@
}

function Resolve-ExistingFile {
    param([Parameter(Mandatory = $true)][string]$Path)
    $resolved = Resolve-Path -LiteralPath $Path -ErrorAction Stop
    if (-not (Test-Path -LiteralPath $resolved.Path -PathType Leaf)) {
        throw "Required file is missing: $Path"
    }
    return $resolved.Path
}

function Write-AtomicUtf8 {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Content
    )
    $temporary = $Path + '.new'
    $encoding = [System.Text.UTF8Encoding]::new($false)
    $bytes = $encoding.GetBytes($Content)
    $stream = [System.IO.FileStream]::new(
        $temporary,
        [System.IO.FileMode]::Create,
        [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::None,
        4096,
        [System.IO.FileOptions]::WriteThrough)
    try {
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush($true)
    } finally {
        $stream.Dispose()
    }
    if (-not [PrimeForge.NativeAtomicFile]::MoveFileEx($temporary, $Path, 0x9)) {
        throw [System.ComponentModel.Win32Exception]::new([Runtime.InteropServices.Marshal]::GetLastWin32Error())
    }
}

function Test-LivePid {
    param([uint64]$ProcessId)
    if ($ProcessId -eq 0) { return $false }
    return $null -ne (Get-Process -Id $ProcessId -ErrorAction SilentlyContinue)
}

function Quote-ProcessArgument {
    param([Parameter(Mandatory = $true)][string]$Value)
    return '"' + $Value.Replace('\', '\').Replace('"', '\"') + '"'
}

$campaignPath = [System.IO.Path]::GetFullPath($CampaignDirectory)
if (-not (Test-Path -LiteralPath $campaignPath -PathType Container)) {
    throw "Campaign directory is missing: $campaignPath"
}
$scheduler = Resolve-ExistingFile $SchedulerPath
$queue = Resolve-ExistingFile $QueuePath
$parentCompleted = Resolve-ExistingFile $ParentCompletedPath
$engine = Resolve-ExistingFile $EnginePath
$watchdog = Resolve-ExistingFile $WatchdogPath

if ((Get-FileHash -Algorithm SHA256 -LiteralPath $engine).Hash.ToLowerInvariant() -ne
    $EngineSha256.ToLowerInvariant()) {
    throw 'Native B8 engine hash mismatch before supervisor launch.'
}

$lockPath = Join-Path $campaignPath 'supervisor.lock.json'
if (Test-Path -LiteralPath $lockPath -PathType Leaf) {
    try {
        $oldLock = Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json
        if (Test-LivePid ([uint64]$oldLock.supervisor_pid)) {
            throw "A native B8 supervisor is already active with PID $($oldLock.supervisor_pid)."
        }
    } catch [System.Management.Automation.RuntimeException] {
        throw
    } catch {
        throw "Existing supervisor lock is malformed: $lockPath"
    }
}

$duplicates = @(Get-CimInstance Win32_Process -ErrorAction Stop | Where-Object {
    $_.Name -ieq ([System.IO.Path]::GetFileName($scheduler)) -and
    $_.CommandLine -and $_.CommandLine.Contains($campaignPath)
})
if ($duplicates.Count -ne 0) {
    throw "A scheduler for this campaign is already active (PID $($duplicates[0].ProcessId))."
}

$operatorStop = Join-Path $campaignPath 'operator.stop'
if (Test-Path -LiteralPath $operatorStop -PathType Leaf) {
    if (-not $Resume) {
        throw 'operator.stop exists; pass -Resume only after confirming a STOPPED checkpoint.'
    }
    $statusPath = Join-Path $campaignPath 'status.json'
    if (-not (Test-Path -LiteralPath $statusPath -PathType Leaf) -or
        (Get-Content -LiteralPath $statusPath -Raw | ConvertFrom-Json).state -ne 'STOPPED') {
        throw 'The stop signal can only be cleared from a confirmed STOPPED state.'
    }
    Remove-Item -LiteralPath $operatorStop -Force
}

$stamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
$stdoutPath = Join-Path $campaignPath "supervisor-$stamp.stdout.log"
$stderrPath = Join-Path $campaignPath "supervisor-$stamp.stderr.log"
$schedulerArguments = @(
    '--campaign-dir', $campaignPath,
    '--queue', $queue,
    '--parent-completed', $parentCompleted,
    '--engine', $engine,
    '--watchdog', $watchdog,
    '--campaign-id', $CampaignId,
    '--parent-campaign-id', $ParentCampaignId,
    '--engine-commit', $EngineCommit,
    '--engine-sha256', $EngineSha256.ToLowerInvariant(),
    '--survivor-sha256', $SurvivorSha256.ToLowerInvariant(),
    '--supervisor-pid', [string]$PID,
    '--device', [string]$Device
)
$argumentLine = ($schedulerArguments | ForEach-Object { Quote-ProcessArgument ([string]$_) }) -join ' '

$schedulerProcess = $null
try {
    $schedulerProcess = Start-Process -FilePath $scheduler -ArgumentList $argumentLine `
        -WorkingDirectory $campaignPath -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath
    $lock = [ordered]@{
        schema_version = 1
        campaign_id = $CampaignId
        state = 'RUNNING'
        started_utc = [DateTime]::UtcNow.ToString('o')
        supervisor_pid = [uint64]$PID
        scheduler_pid = [uint64]$schedulerProcess.Id
        scheduler_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $scheduler).Hash.ToLowerInvariant()
        engine_sha256 = $EngineSha256.ToLowerInvariant()
        stdout = $stdoutPath
        stderr = $stderrPath
    }
    Write-AtomicUtf8 $lockPath (($lock | ConvertTo-Json -Compress) + "`n")
    $schedulerProcess.WaitForExit()
    $schedulerProcess.Refresh()
    $exitCode = [int]$schedulerProcess.ExitCode
    $lock.state = if ($exitCode -eq 0) { 'EXITED' } else { 'ERROR' }
    $lock.ended_utc = [DateTime]::UtcNow.ToString('o')
    $lock.scheduler_exit_code = $exitCode
    Write-AtomicUtf8 $lockPath (($lock | ConvertTo-Json -Compress) + "`n")
    exit $exitCode
} catch {
    $failure = [ordered]@{
        schema_version = 1
        campaign_id = $CampaignId
        state = 'ERROR'
        updated_utc = [DateTime]::UtcNow.ToString('o')
        supervisor_pid = [uint64]$PID
        scheduler_pid = if ($null -eq $schedulerProcess) { 0 } else { [uint64]$schedulerProcess.Id }
        error = $_.Exception.Message
    }
    Write-AtomicUtf8 $lockPath (($failure | ConvertTo-Json -Compress) + "`n")
    throw
}
