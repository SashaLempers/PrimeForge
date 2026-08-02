# SPDX-License-Identifier: Apache-2.0

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Collector,
    [Parameter(Mandatory = $true)][string]$SelfTestPath,
    [Parameter(Mandatory = $true)][string]$WorkingDirectory
)

$ErrorActionPreference = 'Stop'
[System.IO.Directory]::CreateDirectory($WorkingDirectory) | Out-Null
$profileA = Join-Path $WorkingDirectory 'profile-a.json'
$profileB = Join-Path $WorkingDirectory 'profile-b.json'
$identityA = Join-Path $WorkingDirectory 'identity-a.json'
$identityB = Join-Path $WorkingDirectory 'identity-b.json'
$profileWithoutNvidia = Join-Path $WorkingDirectory 'profile-without-nvidia.json'

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Collector -OutputPath $profileA -IdentityOutputPath $identityA -SelfTestPath $SelfTestPath | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'First hardware-profile collection failed.' }
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Collector -OutputPath $profileB -IdentityOutputPath $identityB -SelfTestPath $SelfTestPath | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Second hardware-profile collection failed.' }

$profileBytesA = [System.IO.File]::ReadAllBytes($profileA)
$profileBytesB = [System.IO.File]::ReadAllBytes($profileB)
$identityBytesA = [System.IO.File]::ReadAllBytes($identityA)
$identityBytesB = [System.IO.File]::ReadAllBytes($identityB)

$profileHashA = (Get-FileHash -Algorithm SHA256 -LiteralPath $profileA).Hash
$profileHashB = (Get-FileHash -Algorithm SHA256 -LiteralPath $profileB).Hash
$identityHashA = (Get-FileHash -Algorithm SHA256 -LiteralPath $identityA).Hash
$identityHashB = (Get-FileHash -Algorithm SHA256 -LiteralPath $identityB).Hash
if ($profileBytesA.Length -ne $profileBytesB.Length -or $profileHashA -ne $profileHashB) { throw 'Repeated profile bytes differ.' }
if ($identityBytesA.Length -ne $identityBytesB.Length -or $identityHashA -ne $identityHashB) { throw 'Repeated identity bytes differ.' }
if ($profileBytesA.Length -ge 3 -and $profileBytesA[0] -eq 0xef -and $profileBytesA[1] -eq 0xbb -and $profileBytesA[2] -eq 0xbf) { throw 'Profile contains a UTF-8 BOM.' }
if ($profileBytesA.Length -and ($profileBytesA[-1] -eq 10 -or $profileBytesA[-1] -eq 13)) { throw 'Profile has a trailing newline.' }

$profile = Get-Content -Raw -LiteralPath $profileA | ConvertFrom-Json
if ($profile.schema -ne 'primeforge.hardware-profile.v1') { throw 'Unexpected hardware profile schema.' }
if ($profile.profile_id -notmatch '^sha256:[0-9a-f]{64}$') { throw 'Malformed profile id.' }

$sha256 = [System.Security.Cryptography.SHA256]::Create()
try {
    $identityHash = ([BitConverter]::ToString($sha256.ComputeHash($identityBytesA))).Replace('-', '').ToLowerInvariant()
} finally {
    $sha256.Dispose()
}
if ($profile.profile_id -ne "sha256:$identityHash") { throw 'Profile id does not hash the canonical identity bytes.' }

if ($profile.cpu.brand.status -notin @('DETECTED', 'UNKNOWN')) { throw 'CPU brand source classification is invalid.' }
if ($profile.memory.total_physical_bytes.status -ne 'DETECTED') { throw 'Physical memory was not detected.' }
if ($profile.telemetry_capabilities.cpu_temperature.status -ne 'UNKNOWN' -or $profile.telemetry_capabilities.cpu_temperature.value -ne 'UNKNOWN') { throw 'Unavailable CPU temperature must remain UNKNOWN.' }
if ($profile.toolchain.cuda_toolkit_nvcc.status -eq 'UNKNOWN' -and $profile.toolchain.cuda_toolkit_nvcc.value -ne 'UNKNOWN') { throw 'Unknown CUDA toolkit value is inconsistent.' }

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Collector -OutputPath $profileWithoutNvidia -SelfTestPath $SelfTestPath -DisableNvidiaSmi | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'No-NVIDIA hardware-profile collection failed.' }
$withoutNvidia = Get-Content -Raw -LiteralPath $profileWithoutNvidia | ConvertFrom-Json
if (@($withoutNvidia.gpu.nvidia).Count -ne 0) { throw 'Disabled nvidia-smi must produce an empty NVIDIA inventory.' }
if ($withoutNvidia.telemetry_capabilities.gpu_temperature.status -ne 'UNKNOWN') { throw 'Missing nvidia-smi temperature must remain UNKNOWN.' }
if ($withoutNvidia.telemetry_capabilities.gpu_power.status -ne 'UNKNOWN') { throw 'Missing nvidia-smi power must remain UNKNOWN.' }

Write-Output 'primeforge-hardware-profile-tests: PASS'
