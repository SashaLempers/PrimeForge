[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [Parameter(Mandatory = $true)][string]$CampaignDirectory
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$campaignPath = (Resolve-Path -LiteralPath $CampaignDirectory -ErrorAction Stop).Path
$statusPath = Join-Path $campaignPath 'status.json'
if (-not (Test-Path -LiteralPath $statusPath -PathType Leaf)) {
    throw "Campaign status is missing: $statusPath"
}
$status = Get-Content -LiteralPath $statusPath -Raw | ConvertFrom-Json
if ($status.state -ne 'IN_PROGRESS') {
    throw "Campaign is not IN_PROGRESS (state=$($status.state))."
}
$stopPath = Join-Path $campaignPath 'operator.stop'
$temporary = $stopPath + '.new'
$bytes = [System.Text.UTF8Encoding]::new($false).GetBytes("STOP`n")
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
if ([System.IO.File]::Exists($stopPath)) {
    [System.IO.File]::Delete($temporary)
} else {
    [System.IO.File]::Move($temporary, $stopPath)
}
Write-Host "PrimeForge native B8 stop requested: $stopPath"
