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
$profileWithoutCuda = Join-Path $WorkingDirectory 'profile-without-cuda.json'
$profileWithoutLConnect = Join-Path $WorkingDirectory 'profile-without-lconnect.json'

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Collector -OutputPath $profileA -IdentityOutputPath $identityA -SelfTestPath $SelfTestPath | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'First hardware-profile collection failed.' }
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Collector -OutputPath $profileB -IdentityOutputPath $identityB -SelfTestPath $SelfTestPath | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Second hardware-profile collection failed.' }

$profileBytesA = [System.IO.File]::ReadAllBytes($profileA)
$profileBytesB = [System.IO.File]::ReadAllBytes($profileB)
$identityBytesA = [System.IO.File]::ReadAllBytes($identityA)
$identityBytesB = [System.IO.File]::ReadAllBytes($identityB)

function Get-Sha256Hex {
    param([Parameter(Mandatory = $true)][byte[]]$Bytes)
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($algorithm.ComputeHash($Bytes))).Replace('-', '')
    } finally {
        $algorithm.Dispose()
    }
}

$profileHashA = Get-Sha256Hex $profileBytesA
$profileHashB = Get-Sha256Hex $profileBytesB
$identityHashA = Get-Sha256Hex $identityBytesA
$identityHashB = Get-Sha256Hex $identityBytesB
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
foreach ($field in @(
    $profile.telemetry_capabilities.cpu_temperature,
    $profile.telemetry_capabilities.cpu_power,
    $profile.telemetry_capabilities.cpu_effective_frequency
)) {
    if ($field.status -notin @('DETECTED', 'UNKNOWN')) { throw 'CPU telemetry availability classification is invalid.' }
    if ($field.status -eq 'DETECTED' -and $field.value -ne 'AVAILABLE') { throw 'Detected CPU telemetry must be marked AVAILABLE.' }
    if ($field.status -eq 'UNKNOWN' -and $field.value -ne 'UNKNOWN') { throw 'Unavailable CPU telemetry must remain UNKNOWN.' }
}
if ($profile.toolchain.cuda_toolkit_nvcc.status -eq 'UNKNOWN' -and $profile.toolchain.cuda_toolkit_nvcc.value -ne 'UNKNOWN') { throw 'Unknown CUDA toolkit value is inconsistent.' }
if ($profile.cuda.nvcc_path.status -eq 'DETECTED') {
    foreach ($field in @(
        $profile.cuda.nvcc_release,
        $profile.cuda.nvcc_build,
        $profile.cuda.toolkit_root,
        $profile.cuda.host_compiler_path,
        $profile.cuda.runtime_version,
        $profile.cuda.driver_api_version,
        $profile.cuda.compiled_runtime_version,
        $profile.cuda.probe_status
    )) {
        if ($field.status -ne 'DETECTED' -or $field.value -eq 'UNKNOWN') {
            throw 'Detected nvcc requires complete toolkit and runtime probe metadata.'
        }
    }
    if ([int]$profile.cuda.devices.Count -lt 1) { throw 'Detected local CUDA runtime must report at least one CUDA device.' }
    foreach ($device in @($profile.cuda.devices)) {
        if ($device.compute_capability.status -ne 'DETECTED' -or $device.compute_capability.value -notmatch '^\d+\.\d+$') {
            throw 'CUDA device compute capability is missing or malformed.'
        }
        if ($device.name.status -ne 'DETECTED' -or $device.name.value -eq 'UNKNOWN') { throw 'CUDA device name is missing.' }
    }
}

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Collector -OutputPath $profileWithoutNvidia -SelfTestPath $SelfTestPath -DisableNvidiaSmi | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'No-NVIDIA hardware-profile collection failed.' }
$withoutNvidia = Get-Content -Raw -LiteralPath $profileWithoutNvidia | ConvertFrom-Json
if (@($withoutNvidia.gpu.nvidia).Count -ne 0) { throw 'Disabled nvidia-smi must produce an empty NVIDIA inventory.' }
if ($withoutNvidia.telemetry_capabilities.gpu_temperature.status -ne 'UNKNOWN') { throw 'Missing nvidia-smi temperature must remain UNKNOWN.' }
if ($withoutNvidia.telemetry_capabilities.gpu_power.status -ne 'UNKNOWN') { throw 'Missing nvidia-smi power must remain UNKNOWN.' }

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Collector -OutputPath $profileWithoutCuda -SelfTestPath $SelfTestPath -DisableCudaToolkit | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'No-CUDA-toolkit hardware-profile collection failed.' }
$withoutCuda = Get-Content -Raw -LiteralPath $profileWithoutCuda | ConvertFrom-Json
if ($withoutCuda.cuda.nvcc_path.status -ne 'UNKNOWN') { throw 'Disabled CUDA toolkit must leave nvcc UNKNOWN.' }
if ($withoutCuda.cuda.probe_status.status -ne 'UNKNOWN') { throw 'Disabled CUDA toolkit must leave the runtime probe UNKNOWN.' }
if (@($withoutCuda.cuda.devices).Count -ne 0) { throw 'Disabled CUDA toolkit must produce an empty CUDA device inventory.' }

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $Collector -OutputPath $profileWithoutLConnect -SelfTestPath $SelfTestPath -DisableLConnectTelemetry | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'No-L-Connect hardware-profile collection failed.' }
$withoutLConnect = Get-Content -Raw -LiteralPath $profileWithoutLConnect | ConvertFrom-Json
foreach ($field in @(
    $withoutLConnect.telemetry_capabilities.cpu_temperature,
    $withoutLConnect.telemetry_capabilities.cpu_power,
    $withoutLConnect.telemetry_capabilities.cpu_effective_frequency
)) {
    if ($field.status -ne 'UNKNOWN' -or $field.value -ne 'UNKNOWN') {
        throw 'Disabled L-Connect telemetry must leave CPU telemetry UNKNOWN.'
    }
}

Write-Output 'primeforge-hardware-profile-tests: PASS'
