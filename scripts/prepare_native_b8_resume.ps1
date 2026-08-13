[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [Parameter(Mandatory = $true)][string]$HistoricalDirectory,
    [Parameter(Mandatory = $true)][string]$SelectedSurvivorFile,
    [Parameter(Mandatory = $true)][string]$ResumeDirectory,
    [Parameter(Mandatory = $true)][uint64]$SelectedSieveBound
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

function Read-CandidateFile {
    param([Parameter(Mandatory = $true)][string]$Path)
    $seen = [System.Collections.Generic.HashSet[string]]::new([System.StringComparer]::Ordinal)
    $rows = [System.Collections.Generic.List[object]]::new()
    foreach ($line in [System.IO.File]::ReadAllLines($Path)) {
        if ([string]::IsNullOrWhiteSpace($line)) { continue }
        $parts = $line.Trim() -split '\s+'
        if ($parts.Count -ne 2) { throw "Malformed candidate line in ${Path}: $line" }
        $k = [uint32]$parts[0]
        $n = [uint32]$parts[1]
        if ($k -lt 3 -or $k -ge 100000000 -or ($k -band 1) -eq 0 -or $n -lt 32) {
            throw "Invalid candidate in ${Path}: $line"
        }
        $key = "$k`t$n"
        if (-not $seen.Add($key)) { throw "Duplicate candidate in ${Path}: $key" }
        $rows.Add([pscustomobject]@{ k = $k; n = $n; key = $key })
    }
    return $rows.ToArray()
}

function Get-CanonicalSetHash {
    param([Parameter(Mandatory = $true)][object[]]$Rows)
    $ordered = $Rows | Sort-Object -Property @{Expression = 'k'; Ascending = $true}, @{Expression = 'n'; Ascending = $true}
    $content = (($ordered | ForEach-Object { "$($_.k)`t$($_.n)" }) -join "`n")
    if ($ordered.Count -ne 0) { $content += "`n" }
    $bytes = [System.Text.UTF8Encoding]::new($false).GetBytes($content)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return (($sha.ComputeHash($bytes) | ForEach-Object { $_.ToString('x2') }) -join '') }
    finally { $sha.Dispose() }
}

function Write-AtomicUtf8 {
    param([string]$Path, [string]$Content)
    $temporary = $Path + '.new'
    $bytes = [System.Text.UTF8Encoding]::new($false).GetBytes($Content)
    $stream = [System.IO.FileStream]::new(
        $temporary, [System.IO.FileMode]::Create, [System.IO.FileAccess]::Write,
        [System.IO.FileShare]::None, 4096, [System.IO.FileOptions]::WriteThrough)
    try { $stream.Write($bytes, 0, $bytes.Length); $stream.Flush($true) }
    finally { $stream.Dispose() }
    if (-not [PrimeForge.NativeAtomicFile]::MoveFileEx($temporary, $Path, 0x9)) {
        throw [System.ComponentModel.Win32Exception]::new([Runtime.InteropServices.Marshal]::GetLastWin32Error())
    }
}

$history = (Resolve-Path -LiteralPath $HistoricalDirectory -ErrorAction Stop).Path
$selectedPath = (Resolve-Path -LiteralPath $SelectedSurvivorFile -ErrorAction Stop).Path
$resume = [System.IO.Path]::GetFullPath($ResumeDirectory)
if (-not (Test-Path -LiteralPath $resume -PathType Container)) {
    New-Item -ItemType Directory -Path $resume -Force | Out-Null
}

$artifactManifest = Join-Path $history 'artifacts.sha256'
$artifactLines = [System.IO.File]::ReadAllLines($artifactManifest)
$artifactCount = 0
foreach ($line in $artifactLines) {
    if ([string]::IsNullOrWhiteSpace($line)) { continue }
    if ($line -notmatch '^([0-9a-fA-F]{64})  (.+)$') { throw "Malformed historical artifact manifest line: $line" }
    $artifactPath = Join-Path $history ($Matches[2] -replace '/', '\')
    if (-not (Test-Path -LiteralPath $artifactPath -PathType Leaf)) { throw "Historical artifact is missing: $artifactPath" }
    $actual = (Get-FileHash -Algorithm SHA256 -LiteralPath $artifactPath).Hash
    if ($actual -ine $Matches[1]) { throw "Historical artifact hash mismatch: $artifactPath" }
    ++$artifactCount
}

$oldRows = @(
    Read-CandidateFile (Join-Path $history 'worker-01\survivors.txt')
    Read-CandidateFile (Join-Path $history 'worker-02\survivors.txt')
)
$oldMap = [System.Collections.Generic.Dictionary[string, object]]::new([System.StringComparer]::Ordinal)
foreach ($row in $oldRows) {
    if ($oldMap.ContainsKey($row.key)) { throw "Duplicate in historical survivor partitions: $($row.key)" }
    $oldMap.Add($row.key, $row)
}

$completedMap = [System.Collections.Generic.Dictionary[string, object]]::new([System.StringComparer]::Ordinal)
$invalidRecords = 0
$outside = 0
$provenPrimes = 0
foreach ($worker in @('worker-01', 'worker-02')) {
    $resultPath = Join-Path $history "$worker\results.tsv"
    $proofText = [System.IO.File]::ReadAllText((Join-Path $history "$worker\presults.txt"))
    foreach ($result in Import-Csv -Delimiter "`t" -LiteralPath $resultPath) {
        try {
            $k = [uint32]$result.k
            $n = [uint32]$result.n
            $key = "$k`t$n"
            if ($result.primality_status -notin @('COMPOSITE', 'PROVEN_PRIME')) { ++$invalidRecords; continue }
            if (-not $oldMap.ContainsKey($key)) { ++$outside; continue }
            if ($completedMap.ContainsKey($key)) { throw "Duplicate historical durable result: $key" }
            $sourceLog = Join-Path $history "$worker\logs\$($result.source_log)"
            if (-not (Test-Path -LiteralPath $sourceLog -PathType Leaf)) { ++$invalidRecords; continue }
            $logText = [System.IO.File]::ReadAllText($sourceLog)
            $statusWord = if ($result.primality_status -eq 'PROVEN_PRIME') { 'is prime' } else { 'is composite' }
            $marker = [Regex]::Escape("$k * 2^$n + 1 $statusWord")
            if ($logText -notmatch $marker -or $proofText -notmatch $marker) { ++$invalidRecords; continue }
            if ($result.primality_status -eq 'PROVEN_PRIME') { ++$provenPrimes }
            $completedMap.Add($key, [pscustomobject]@{ k = $k; n = $n; key = $key })
        } catch {
            if ($_.Exception.Message -like 'Duplicate historical durable result:*') { throw }
            ++$invalidRecords
        }
    }
}

$selected = @(Read-CandidateFile $selectedPath)
foreach ($row in $selected) {
    if (-not $oldMap.ContainsKey($row.key)) {
        throw "Selected sieve output is not a subset of the historical 2G survivor set: $($row.key)"
    }
}
$remaining = @($selected | Where-Object { -not $completedMap.ContainsKey($_.key) })
$completed = @($completedMap.Values | Sort-Object -Property @{Expression = 'k'; Ascending = $true}, @{Expression = 'n'; Ascending = $true})

if ($invalidRecords -ne 0 -or $outside -ne 0 -or $provenPrimes -ne 0 -or
    $completed.Count + ($oldMap.Count - $completed.Count) -ne $oldMap.Count) {
    throw "Historical reconstruction failed: invalid=$invalidRecords outside=$outside primes=$provenPrimes"
}

$selectedContent = (($selected | ForEach-Object { "$($_.k) $($_.n)" }) -join "`n") + "`n"
$completedContent = (($completed | ForEach-Object { "$($_.k) $($_.n)" }) -join "`n") + "`n"
$remainingContent = (($remaining | ForEach-Object { "$($_.k) $($_.n)" }) -join "`n") + "`n"
$selectedOutput = Join-Path $resume 'selected-survivors.txt'
$completedOutput = Join-Path $resume 'parent-completed.txt'
$remainingOutput = Join-Path $resume 'remaining-queue.txt'
Write-AtomicUtf8 $selectedOutput $selectedContent
Write-AtomicUtf8 $completedOutput $completedContent
Write-AtomicUtf8 $remainingOutput $remainingContent

$report = [ordered]@{
    schema_version = 1
    historical_directory = $history
    historical_artifacts_verified = $artifactCount
    selected_sieve_bound = [string]$SelectedSieveBound
    survivors_old = $oldMap.Count
    completed_valid = $completed.Count
    remaining_old = $oldMap.Count - $completed.Count
    duplicates = 0
    invalid_records = $invalidRecords
    results_outside_survivors = $outside
    proven_primes = $provenPrimes
    selected_survivors = $selected.Count
    remaining_selected = $remaining.Count
    survivors_old_set_sha256 = Get-CanonicalSetHash $oldRows
    completed_candidate_set_sha256 = Get-CanonicalSetHash $completed
    selected_survivor_set_sha256 = Get-CanonicalSetHash $selected
    remaining_candidate_set_sha256 = Get-CanonicalSetHash $remaining
    selected_survivor_file_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $selectedOutput).Hash.ToLowerInvariant()
    parent_completed_file_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $completedOutput).Hash.ToLowerInvariant()
    remaining_queue_file_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $remainingOutput).Hash.ToLowerInvariant()
    status = 'RECONSTRUCTION_PASS'
}
Write-AtomicUtf8 (Join-Path $resume 'reconstruction.json') (($report | ConvertTo-Json -Compress) + "`n")
$report | ConvertTo-Json
