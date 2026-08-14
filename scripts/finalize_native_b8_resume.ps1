[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [Parameter(Mandatory = $true)][string]$ResumeDirectory,
    [Parameter(Mandatory = $true)][string]$SchedulerPath,
    [Parameter(Mandatory = $true)][string]$EnginePath,
    [Parameter(Mandatory = $true)][string]$WatchdogPath,
    [Parameter(Mandatory = $true)][string]$SupervisorScriptPath,
    [Parameter(Mandatory = $true)][string]$StopScriptPath,
    [Parameter(Mandatory = $true)][string]$PreflightMachinePath,
    [Parameter(Mandatory = $true)][string]$ParentCampaignId,
    [Parameter(Mandatory = $true)][string]$ParentCheckpointHash,
    [Parameter(Mandatory = $true)][string]$ParentResultsHash,
    [Parameter(Mandatory = $true)][string]$OldEngineCommit,
    [Parameter(Mandatory = $true)][string]$NewEngineCommit,
    [uint64]$OldSieveBound = 2000000000,
    [uint64]$SelectedSieveBound = 4000000000,
    [uint64]$KMin = 75939069,
    [uint64]$KMax = 76077027,
    [uint32]$Exponent = 66411,
    [ValidateRange(1, 32)][uint32]$BatchSize = 32
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

if (-not ('PrimeForge.NativeB8ManifestAtomicFile' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace PrimeForge {
    public static class NativeB8ManifestAtomicFile {
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

function Get-Sha256 {
    param([Parameter(Mandatory = $true)][string]$Path)
    return (Get-FileHash -Algorithm SHA256 -LiteralPath $Path).Hash.ToLowerInvariant()
}

function Assert-Sha256 {
    param([Parameter(Mandatory = $true)][string]$Value, [Parameter(Mandatory = $true)][string]$Name)
    if ($Value -notmatch '^[0-9a-fA-F]{64}$') {
        throw "$Name must be exactly 64 hexadecimal characters."
    }
}

function Get-StringSha256 {
    param([Parameter(Mandatory = $true)][string]$Value)
    $algorithm = [Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [Text.UTF8Encoding]::new($false).GetBytes($Value)
        return ([BitConverter]::ToString($algorithm.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
    } finally {
        $algorithm.Dispose()
    }
}

function Write-AtomicUtf8 {
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$Content)
    $temporary = $Path + '.new'
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes($Content)
    $stream = [IO.FileStream]::new(
        $temporary,
        [IO.FileMode]::Create,
        [IO.FileAccess]::Write,
        [IO.FileShare]::None,
        4096,
        [IO.FileOptions]::WriteThrough)
    try {
        $stream.Write($bytes, 0, $bytes.Length)
        $stream.Flush($true)
    } finally {
        $stream.Dispose()
    }
    if (-not [PrimeForge.NativeB8ManifestAtomicFile]::MoveFileEx($temporary, $Path, 0x9)) {
        throw [ComponentModel.Win32Exception]::new([Runtime.InteropServices.Marshal]::GetLastWin32Error())
    }
}

foreach ($item in @(
    @{ value = $ParentCheckpointHash; name = 'ParentCheckpointHash' },
    @{ value = $ParentResultsHash; name = 'ParentResultsHash' },
    @{ value = $OldEngineCommit; name = 'OldEngineCommit'; commit = $true },
    @{ value = $NewEngineCommit; name = 'NewEngineCommit'; commit = $true }
)) {
    if ($item.ContainsKey('commit') -and [bool]$item.commit) {
        if ([string]$item.value -notmatch '^[0-9a-fA-F]{40}$') {
            throw "$($item.name) must be exactly 40 hexadecimal characters."
        }
    } else {
        Assert-Sha256 ([string]$item.value) ([string]$item.name)
    }
}
if ($ParentCampaignId -notmatch '^sha256:[0-9a-fA-F]{64}$') {
    throw 'ParentCampaignId must be a sha256: identifier.'
}
if ($KMin -gt $KMax -or ($KMin % 2) -eq 0 -or ($KMax % 2) -eq 0) {
    throw 'The target must be a nonempty odd-k interval.'
}

$resumePath = [IO.Path]::GetFullPath($ResumeDirectory)
if (-not (Test-Path -LiteralPath $resumePath -PathType Container)) {
    throw "Resume directory is missing: $resumePath"
}
$manifestPath = Join-Path $resumePath 'resume-manifest.json'
if (Test-Path -LiteralPath $manifestPath) {
    throw "Refusing to replace an existing resume manifest: $manifestPath"
}

$reconstructionPath = Resolve-ExistingFile (Join-Path $resumePath 'reconstruction.json')
$selectedPath = Resolve-ExistingFile (Join-Path $resumePath 'selected-survivors.txt')
$completedPath = Resolve-ExistingFile (Join-Path $resumePath 'parent-completed.txt')
$queuePath = Resolve-ExistingFile (Join-Path $resumePath 'remaining-queue.txt')
$scheduler = Resolve-ExistingFile $SchedulerPath
$engine = Resolve-ExistingFile $EnginePath
$watchdog = Resolve-ExistingFile $WatchdogPath
$supervisor = Resolve-ExistingFile $SupervisorScriptPath
$stopScript = Resolve-ExistingFile $StopScriptPath
$preflightPath = Resolve-ExistingFile $PreflightMachinePath

$reconstruction = Get-Content -Raw -LiteralPath $reconstructionPath | ConvertFrom-Json
if ($reconstruction.status -ne 'RECONSTRUCTION_PASS' -or
    [uint64]$reconstruction.selected_sieve_bound -ne $SelectedSieveBound -or
    [uint32]$reconstruction.duplicates -ne 0 -or
    [uint32]$reconstruction.invalid_records -ne 0 -or
    [uint32]$reconstruction.results_outside_survivors -ne 0 -or
    [uint32]$reconstruction.proven_primes -ne 0) {
    throw 'Historical reconstruction is not eligible for launch.'
}
if ((Get-Sha256 $selectedPath) -ne [string]$reconstruction.selected_survivor_file_sha256 -or
    (Get-Sha256 $completedPath) -ne [string]$reconstruction.parent_completed_file_sha256 -or
    (Get-Sha256 $queuePath) -ne [string]$reconstruction.remaining_queue_file_sha256) {
    throw 'One or more reconstructed campaign files changed after validation.'
}

$preflight = Get-Content -Raw -LiteralPath $preflightPath | ConvertFrom-Json
if ($preflight.preflight_status -ne 'PREFLIGHT_PASS' -or
    $preflight.coverage_verdict -ne 'PUBLICLY_UNCOVERED_CANDIDATE' -or
    [uint64]$preflight.target.k_min -ne $KMin -or
    [uint64]$preflight.target.k_max -ne $KMax -or
    [uint32]$preflight.target.exponent -ne $Exponent -or
    [uint32]$preflight.numeric_overlap_checks.total_possible_overlaps_or_matches -ne 0) {
    throw 'Final public preflight does not authorize this exact target.'
}

$identity = [ordered]@{
    schema_version = 1
    parent_campaign_id = $ParentCampaignId.ToLowerInvariant()
    parent_checkpoint_hash = $ParentCheckpointHash.ToLowerInvariant()
    parent_results_hash = $ParentResultsHash.ToLowerInvariant()
    target = [ordered]@{ family = 'k*2^n+1'; k_min = [string]$KMin; k_max = [string]$KMax; k_step = 2; n = $Exponent }
    old_sieve_bound = [string]$OldSieveBound
    selected_new_sieve_bound = [string]$SelectedSieveBound
    batch_size = $BatchSize
    old_engine_commit = $OldEngineCommit.ToLowerInvariant()
    new_engine_commit = $NewEngineCommit.ToLowerInvariant()
    engine_binary_sha256 = Get-Sha256 $engine
    scheduler_binary_sha256 = Get-Sha256 $scheduler
    watchdog_binary_sha256 = Get-Sha256 $watchdog
    selected_survivor_file_sha256 = Get-Sha256 $selectedPath
    completed_candidate_set_hash = [string]$reconstruction.completed_candidate_set_sha256
    remaining_candidate_set_hash = [string]$reconstruction.remaining_candidate_set_sha256
    preflight_source_manifest_sha256 = [string]$preflight.source_manifest_sha256
}
$identityJson = $identity | ConvertTo-Json -Depth 8 -Compress
$campaignId = 'sha256:' + (Get-StringSha256 $identityJson)

$manifest = [ordered]@{
    schema_version = 1
    state = 'READY_TO_LAUNCH'
    created_utc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    campaign_id = $campaignId
    parent_campaign_id = $ParentCampaignId.ToLowerInvariant()
    parent_checkpoint_hash = $ParentCheckpointHash.ToLowerInvariant()
    parent_results_hash = $ParentResultsHash.ToLowerInvariant()
    old_engine_commit = $OldEngineCommit.ToLowerInvariant()
    new_engine_commit = $NewEngineCommit.ToLowerInvariant()
    old_sieve_bound = [string]$OldSieveBound
    selected_new_sieve_bound = [string]$SelectedSieveBound
    target = $identity.target
    batch_size = $BatchSize
    survivors_old = [uint32]$reconstruction.survivors_old
    completed_from_parent = [uint32]$reconstruction.completed_valid
    selected_survivors = [uint32]$reconstruction.selected_survivors
    remaining_at_launch = [uint32]$reconstruction.remaining_selected
    completed_candidate_set_hash = [string]$reconstruction.completed_candidate_set_sha256
    remaining_candidate_set_hash = [string]$reconstruction.remaining_candidate_set_sha256
    selected_survivor_file_sha256 = Get-Sha256 $selectedPath
    parent_completed_file_sha256 = Get-Sha256 $completedPath
    remaining_queue_file_sha256 = Get-Sha256 $queuePath
    binaries = [ordered]@{
        engine = [ordered]@{ path = $engine; sha256 = $identity.engine_binary_sha256 }
        scheduler = [ordered]@{ path = $scheduler; sha256 = $identity.scheduler_binary_sha256 }
        watchdog = [ordered]@{ path = $watchdog; sha256 = $identity.watchdog_binary_sha256 }
    }
    scripts = [ordered]@{
        supervisor = [ordered]@{ path = $supervisor; sha256 = Get-Sha256 $supervisor }
        stop = [ordered]@{ path = $stopScript; sha256 = Get-Sha256 $stopScript }
    }
    preflight = [ordered]@{
        status = [string]$preflight.preflight_status
        coverage_verdict = [string]$preflight.coverage_verdict
        generated_utc = [string]$preflight.generated_utc
        machine_file_sha256 = Get-Sha256 $preflightPath
        source_manifest_sha256 = [string]$preflight.source_manifest_sha256
    }
    telemetry = [ordered]@{ file = 'campaign-telemetry.tsv'; soft_limit_bytes = 83886080; hard_limit_bytes = 94371840 }
    identity_sha256_input = $identity
}
Write-AtomicUtf8 $manifestPath (($manifest | ConvertTo-Json -Depth 12 -Compress) + "`n")

Write-Host "native_b8.manifest=$manifestPath"
Write-Host "native_b8.campaign_id=$campaignId"
Write-Host "native_b8.remaining_at_launch=$($reconstruction.remaining_selected)"
Write-Host "native_b8.status=READY_TO_LAUNCH"
