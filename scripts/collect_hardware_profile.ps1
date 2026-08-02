# SPDX-License-Identifier: Apache-2.0

[CmdletBinding()]
param(
    [string]$OutputPath = 'profiles\hardware_profile.json',
    [string]$IdentityOutputPath = '',
    [string]$SelfTestPath = '',
    [switch]$DisableNvidiaSmi,
    [switch]$DisableCudaToolkit
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repositoryRoot = Split-Path -Parent $PSScriptRoot

function Resolve-RepositoryPath {
    param([Parameter(Mandatory = $true)][string]$Path)
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $Path))
}

function New-Observation {
    param(
        [Parameter(Mandatory = $true)][ValidateSet('DECLARED', 'DETECTED', 'MEASURED', 'UNKNOWN')][string]$Status,
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Value
    )
    $normalized = if ($Status -eq 'UNKNOWN' -or [string]::IsNullOrWhiteSpace($Value)) { 'UNKNOWN' } else { $Value.Trim() }
    return [ordered]@{ source = $Source; status = $(if ($normalized -eq 'UNKNOWN') { 'UNKNOWN' } else { $Status }); value = $normalized }
}

function Escape-JsonString {
    param([Parameter(Mandatory = $true)][AllowEmptyString()][string]$Value)
    $builder = [System.Text.StringBuilder]::new()
    [void]$builder.Append('"')
    foreach ($character in $Value.ToCharArray()) {
        $code = [int]$character
        $escaped = $null
        switch ($character) {
            '"' { $escaped = '\"' }
            '\' { $escaped = '\\' }
            "`b" { $escaped = '\b' }
            "`t" { $escaped = '\t' }
            "`n" { $escaped = '\n' }
            "`f" { $escaped = '\f' }
            "`r" { $escaped = '\r' }
        }
        if ($null -ne $escaped) {
            [void]$builder.Append($escaped)
        } elseif ($code -lt 32) {
            [void]$builder.Append(('\u{0:x4}' -f $code))
        } else {
            [void]$builder.Append($character)
        }
    }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function ConvertTo-CanonicalJson {
    param([Parameter(Mandatory = $true)]$Value)
    if ($Value -is [string]) {
        return Escape-JsonString -Value $Value
    }
    if ($Value -is [System.Collections.IDictionary]) {
        [string[]]$keys = @($Value.Keys | ForEach-Object { [string]$_ })
        [Array]::Sort($keys, [System.StringComparer]::Ordinal)
        $parts = foreach ($key in $keys) {
            (Escape-JsonString -Value $key) + ':' + (ConvertTo-CanonicalJson -Value $Value[$key])
        }
        return '{' + ($parts -join ',') + '}'
    }
    if ($Value -is [System.Collections.IEnumerable]) {
        $parts = foreach ($item in $Value) { ConvertTo-CanonicalJson -Value $item }
        return '[' + ($parts -join ',') + ']'
    }
    throw "Unsupported canonical JSON type: $($Value.GetType().FullName)"
}

function Get-Sha256Hex {
    param([Parameter(Mandatory = $true)][byte[]]$Bytes)
    $sha256 = [System.Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha256.ComputeHash($Bytes))).Replace('-', '').ToLowerInvariant()
    } finally {
        $sha256.Dispose()
    }
}

function Write-Utf8NoBom {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Content
    )
    $parent = Split-Path -Parent $Path
    if ($parent) { [System.IO.Directory]::CreateDirectory($parent) | Out-Null }
    [System.IO.File]::WriteAllText($Path, $Content, [System.Text.UTF8Encoding]::new($false))
}

function Get-CommandVersion {
    param([string]$Command, [string[]]$Arguments)
    $resolved = Get-Command $Command -ErrorAction SilentlyContinue
    if (-not $resolved) { return New-Observation UNKNOWN $Command 'UNKNOWN' }
    try {
        $text = (& $resolved.Source @Arguments 2>$null | Select-Object -First 1).Trim()
        return New-Observation DETECTED $Command $text
    } catch {
        return New-Observation UNKNOWN $Command 'UNKNOWN'
    }
}

function Find-CudaNvcc {
    if ($DisableCudaToolkit) { return $null }

    $candidates = [System.Collections.Generic.List[object]]::new()
    $command = Get-Command nvcc.exe -ErrorAction SilentlyContinue
    if ($command) {
        $candidates.Add([pscustomobject]@{ Path = $command.Source; Method = 'PATH'; Root = (Split-Path -Parent (Split-Path -Parent $command.Source)) })
    }
    foreach ($variableName in @('CUDA_PATH', 'CUDA_PATH_V13_3')) {
        $root = [Environment]::GetEnvironmentVariable($variableName)
        if ($root) {
            $candidate = Join-Path $root 'bin\nvcc.exe'
            $candidates.Add([pscustomobject]@{ Path = $candidate; Method = "environment:$variableName"; Root = $root })
        }
    }
    $registryRoot = 'HKLM:\SOFTWARE\NVIDIA Corporation\GPU Computing Toolkit\CUDA'
    if (Test-Path -LiteralPath $registryRoot) {
        foreach ($key in @(Get-ChildItem -LiteralPath $registryRoot | Sort-Object PSChildName -Descending)) {
            $root = Join-Path 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA' $key.PSChildName
            $candidates.Add([pscustomobject]@{ Path = (Join-Path $root 'bin\nvcc.exe'); Method = "registry:$($key.PSChildName)"; Root = $root })
        }
    }
    $installationRoot = 'C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA'
    if (Test-Path -LiteralPath $installationRoot) {
        foreach ($directory in @(Get-ChildItem -LiteralPath $installationRoot -Directory | Sort-Object Name -Descending)) {
            $candidates.Add([pscustomobject]@{ Path = (Join-Path $directory.FullName 'bin\nvcc.exe'); Method = "filesystem:$($directory.Name)"; Root = $directory.FullName })
        }
    }

    $seen = @{}
    foreach ($candidate in $candidates) {
        $fullPath = [System.IO.Path]::GetFullPath($candidate.Path)
        if (-not $seen.ContainsKey($fullPath)) {
            $seen[$fullPath] = $true
            if (Test-Path -LiteralPath $fullPath) {
                return [pscustomobject]@{ Path = $fullPath; Method = $candidate.Method; Root = [System.IO.Path]::GetFullPath($candidate.Root) }
            }
        }
    }
    return $null
}

function Find-MsvcHostCompiler {
    $candidates = [System.Collections.Generic.List[string]]::new()
    $command = Get-Command cl.exe -ErrorAction SilentlyContinue
    if ($command) { $candidates.Add($command.Source) }

    foreach ($cache in @(
        (Join-Path $repositoryRoot 'out\build\msvc-release\CMakeCache.txt'),
        (Join-Path $repositoryRoot 'out\build\msvc-debug\CMakeCache.txt')
    )) {
        if (Test-Path -LiteralPath $cache) {
            $compilerLine = Get-Content -LiteralPath $cache |
                Where-Object { $_ -match '^CMAKE_CXX_COMPILER:[^=]+=' } |
                Select-Object -First 1
            if ($compilerLine) { $candidates.Add($compilerLine.Substring($compilerLine.IndexOf('=') + 1)) }
        }
    }

    $vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path -LiteralPath $vswherePath) {
        $installationPath = (& $vswherePath -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
        if ($installationPath) {
            $toolsRoot = Join-Path $installationPath 'VC\Tools\MSVC'
            if (Test-Path -LiteralPath $toolsRoot) {
                foreach ($toolsVersion in @(Get-ChildItem -LiteralPath $toolsRoot -Directory | Sort-Object Name -Descending)) {
                    $candidates.Add((Join-Path $toolsVersion.FullName 'bin\Hostx64\x64\cl.exe'))
                }
            }
        }
    }

    foreach ($candidate in $candidates) {
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return [System.IO.Path]::GetFullPath($candidate)
        }
    }
    return $null
}

function Get-SelfTestValues {
    param([string]$RequestedPath)
    $candidates = @()
    if ($RequestedPath) { $candidates += (Resolve-RepositoryPath $RequestedPath) }
    $candidates += @(
        (Join-Path $repositoryRoot 'out\build\msvc-release\primeforge-selftest.exe'),
        (Join-Path $repositoryRoot 'out\build\msvc-debug\primeforge-selftest.exe')
    )
    $executable = $candidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
    $values = @{}
    if (-not $executable) { return $values }
    foreach ($line in @(& $executable)) {
        $separator = $line.IndexOf('=')
        if ($separator -gt 0) { $values[$line.Substring(0, $separator)] = $line.Substring($separator + 1) }
    }
    return $values
}

if (-not ('PrimeForge.NativeProcessorGroups' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace PrimeForge {
    public static class NativeProcessorGroups {
        [DllImport("kernel32.dll")]
        public static extern ushort GetActiveProcessorGroupCount();
        [DllImport("kernel32.dll")]
        public static extern uint GetActiveProcessorCount(ushort groupNumber);
    }
}
'@
}

$computer = Get-CimInstance Win32_ComputerSystem
$operatingSystem = Get-CimInstance Win32_OperatingSystem
$processor = Get-CimInstance Win32_Processor | Select-Object -First 1
$bios = Get-CimInstance Win32_BIOS | Select-Object -First 1
$baseboard = Get-CimInstance Win32_BaseBoard | Select-Object -First 1
$selfTest = Get-SelfTestValues -RequestedPath $SelfTestPath

$groups = [PrimeForge.NativeProcessorGroups]::GetActiveProcessorGroupCount()
$groupEntries = @(for ([uint16]$group = 0; $group -lt $groups; $group++) {
    [ordered]@{
        active_logical_processors = New-Observation DETECTED 'GetActiveProcessorCount' ([string][PrimeForge.NativeProcessorGroups]::GetActiveProcessorCount($group))
        group = New-Observation DETECTED 'GetActiveProcessorGroupCount' ([string]$group)
    }
})

$cacheEntries = @(Get-CimInstance Win32_CacheMemory | Sort-Object Level, InstalledSize | ForEach-Object {
    [ordered]@{
        installed_kib = New-Observation DETECTED 'Win32_CacheMemory.InstalledSize' ([string]$_.InstalledSize)
        level_code = New-Observation DETECTED 'Win32_CacheMemory.Level' ([string]$_.Level)
        purpose = New-Observation DETECTED 'Win32_CacheMemory.Purpose' ([string]$_.Purpose)
    }
})

$memoryModules = @(Get-CimInstance Win32_PhysicalMemory | Sort-Object BankLabel, DeviceLocator, PartNumber | ForEach-Object {
    [ordered]@{
        bank_label = New-Observation DETECTED 'Win32_PhysicalMemory.BankLabel' ([string]$_.BankLabel)
        capacity_bytes = New-Observation DETECTED 'Win32_PhysicalMemory.Capacity' ([string]$_.Capacity)
        configured_clock_mhz = New-Observation DETECTED 'Win32_PhysicalMemory.ConfiguredClockSpeed' ([string]$_.ConfiguredClockSpeed)
        data_width_bits = New-Observation DETECTED 'Win32_PhysicalMemory.DataWidth' ([string]$_.DataWidth)
        device_locator = New-Observation DETECTED 'Win32_PhysicalMemory.DeviceLocator' ([string]$_.DeviceLocator)
        manufacturer = New-Observation DETECTED 'Win32_PhysicalMemory.Manufacturer' ([string]$_.Manufacturer)
        part_number = New-Observation DETECTED 'Win32_PhysicalMemory.PartNumber' ([string]$_.PartNumber)
        smbios_memory_type = New-Observation DETECTED 'Win32_PhysicalMemory.SMBIOSMemoryType' ([string]$_.SMBIOSMemoryType)
        speed_mhz = New-Observation DETECTED 'Win32_PhysicalMemory.Speed' ([string]$_.Speed)
        total_width_bits = New-Observation DETECTED 'Win32_PhysicalMemory.TotalWidth' ([string]$_.TotalWidth)
    }
})

$nvidiaSmi = if ($DisableNvidiaSmi) { $null } else { Get-Command nvidia-smi.exe -ErrorAction SilentlyContinue }
$nvidiaRows = @()
if ($nvidiaSmi) {
    $query = @(& $nvidiaSmi.Source '--query-gpu=name,driver_version,memory.total,vbios_version,pci.bus_id,compute_cap' '--format=csv,noheader,nounits' 2>$null)
    foreach ($line in $query) {
        $fields = @($line -split ',' | ForEach-Object { $_.Trim() })
        if ($fields.Count -eq 6) {
            $nvidiaRows += [ordered]@{
                compute_capability = New-Observation DETECTED 'nvidia-smi.compute_cap' $fields[5]
                driver_version = New-Observation DETECTED 'nvidia-smi.driver_version' $fields[1]
                memory_total_mib = New-Observation DETECTED 'nvidia-smi.memory.total' $fields[2]
                name = New-Observation DETECTED 'nvidia-smi.name' $fields[0]
                pci_bus_id = New-Observation DETECTED 'nvidia-smi.pci.bus_id' $fields[4]
                vbios_version = New-Observation DETECTED 'nvidia-smi.vbios_version' $fields[3]
            }
        }
    }
}

$otherAdapters = @(Get-CimInstance Win32_VideoController | Sort-Object Name | Where-Object { $_.Name -notmatch '^NVIDIA ' } | ForEach-Object {
    [ordered]@{
        driver_version = New-Observation DETECTED 'Win32_VideoController.DriverVersion' ([string]$_.DriverVersion)
        name = New-Observation DETECTED 'Win32_VideoController.Name' ([string]$_.Name)
    }
})

$telemetryQuery = @()
if ($nvidiaSmi) {
    $telemetryQuery = @(& $nvidiaSmi.Source '--query-gpu=temperature.gpu,temperature.memory,power.draw,clocks.sm,utilization.gpu,memory.used,memory.free' '--format=csv,noheader,nounits' 2>$null | Select-Object -First 1)
}
$telemetryFields = @(if ($telemetryQuery.Count) { $telemetryQuery[0] -split ',' | ForEach-Object { $_.Trim() } })
function Get-NvidiaTelemetryAvailability {
    param([int]$Index, [string]$Name)
    if ($telemetryFields.Count -le $Index -or $telemetryFields[$Index] -eq 'N/A') {
        return New-Observation UNKNOWN "nvidia-smi.$Name" 'UNKNOWN'
    }
    return New-Observation DETECTED "nvidia-smi.$Name" 'AVAILABLE'
}

$cudaNvcc = Find-CudaNvcc
$cudaDetectionMethod = New-Observation UNKNOWN 'CUDA detector' 'UNKNOWN'
$cudaNvccPath = New-Observation UNKNOWN 'CUDA detector' 'UNKNOWN'
$cudaToolkitRoot = New-Observation UNKNOWN 'CUDA detector' 'UNKNOWN'
$cudaNvccRelease = New-Observation UNKNOWN 'nvcc --version' 'UNKNOWN'
$cudaNvccBuild = New-Observation UNKNOWN 'nvcc --version' 'UNKNOWN'
$cudaRuntimeLibraryPath = New-Observation UNKNOWN 'CUDA toolkit search' 'UNKNOWN'
$cudaRuntimeLibraryVersion = New-Observation UNKNOWN 'cudart file metadata' 'UNKNOWN'
$cudaRuntimeVersion = New-Observation UNKNOWN 'cudaRuntimeGetVersion' 'UNKNOWN'
$cudaRuntimeVersionRaw = New-Observation UNKNOWN 'cudaRuntimeGetVersion' 'UNKNOWN'
$cudaDriverApiVersion = New-Observation UNKNOWN 'cudaDriverGetVersion' 'UNKNOWN'
$cudaDriverApiVersionRaw = New-Observation UNKNOWN 'cudaDriverGetVersion' 'UNKNOWN'
$cudaCompiledRuntimeVersion = New-Observation UNKNOWN 'CUDART_VERSION' 'UNKNOWN'
$cudaCompiledRuntimeVersionRaw = New-Observation UNKNOWN 'CUDART_VERSION' 'UNKNOWN'
$cudaDeviceCount = New-Observation UNKNOWN 'cudaGetDeviceCount' 'UNKNOWN'
$cudaDevices = @()
$cudaProbeStatus = New-Observation UNKNOWN 'cuda_profile_probe' 'UNKNOWN'
$cudaHostCompilerPath = New-Observation UNKNOWN 'MSVC host compiler detector' 'UNKNOWN'
$cudaNvidiaSmiPath = if ($nvidiaSmi) { New-Observation DETECTED 'PATH' $nvidiaSmi.Source } else { New-Observation UNKNOWN 'PATH' 'UNKNOWN' }
$cudaProbeSourcePath = New-Observation DETECTED 'repository path' 'tools/cuda/cuda_profile_probe.cu'
$cudaProbeExecutablePath = New-Observation UNKNOWN 'generated repository path' 'UNKNOWN'

if ($cudaNvcc) {
    $cudaDetectionMethod = New-Observation DETECTED 'CUDA detector' $cudaNvcc.Method
    $cudaNvccPath = New-Observation DETECTED $cudaNvcc.Method $cudaNvcc.Path
    $cudaToolkitRoot = New-Observation DETECTED $cudaNvcc.Method $cudaNvcc.Root
    $nvccText = (& $cudaNvcc.Path --version 2>&1 | Out-String)
    $versionMatch = [regex]::Match($nvccText, 'release\s+([^,\r\n]+),\s+V([^\s\r\n]+)')
    if ($versionMatch.Success) {
        $cudaNvccRelease = New-Observation DETECTED 'nvcc --version' $versionMatch.Groups[1].Value
        $cudaNvccBuild = New-Observation DETECTED 'nvcc --version' $versionMatch.Groups[2].Value
    }

    $runtimeLibrary = Get-ChildItem -LiteralPath $cudaNvcc.Root -Filter 'cudart64_*.dll' -Recurse -ErrorAction SilentlyContinue | Sort-Object FullName | Select-Object -First 1
    if ($runtimeLibrary) {
        $cudaRuntimeLibraryPath = New-Observation DETECTED 'CUDA toolkit filesystem' $runtimeLibrary.FullName
        $cudaRuntimeLibraryVersion = New-Observation DETECTED 'cudart file metadata' $runtimeLibrary.VersionInfo.FileVersion
    }

    $probeSource = Join-Path $repositoryRoot 'tools\cuda\cuda_profile_probe.cu'
    $probeDirectory = Join-Path $repositoryRoot 'out\hardware_probe'
    $probeExecutable = Join-Path $probeDirectory 'cuda_profile_probe.exe'
    $cudaProbeExecutablePath = New-Observation DETECTED 'generated repository path' 'out/hardware_probe/cuda_profile_probe.exe'
    [System.IO.Directory]::CreateDirectory($probeDirectory) | Out-Null
    $mustBuildProbe = -not (Test-Path -LiteralPath $probeExecutable) -or
        (Get-Item -LiteralPath $probeSource).LastWriteTimeUtc -gt (Get-Item -LiteralPath $probeExecutable).LastWriteTimeUtc
    if ($mustBuildProbe) {
        $hostCompiler = Find-MsvcHostCompiler
        if (-not $hostCompiler) {
            throw 'CUDA profile probe compilation requires an installed MSVC host compiler, but cl.exe was not found.'
        }
        $cudaHostCompilerPath = New-Observation DETECTED 'MSVC host compiler detector' $hostCompiler
        $hostCompilerDirectory = Split-Path -Parent $hostCompiler
        $probeBuildOutput = @(& $cudaNvcc.Path '--std=c++17' '-O2' '-ccbin' $hostCompilerDirectory '-o' $probeExecutable $probeSource 2>&1)
        if ($LASTEXITCODE -ne 0) {
            throw "CUDA profile probe compilation failed: $($probeBuildOutput -join [Environment]::NewLine)"
        }
    } else {
        $hostCompiler = Find-MsvcHostCompiler
        if ($hostCompiler) {
            $cudaHostCompilerPath = New-Observation DETECTED 'MSVC host compiler detector' $hostCompiler
        }
    }

    $probeLines = @(& $probeExecutable 2>&1)
    if ($LASTEXITCODE -eq 0) {
        $probeValues = @{}
        foreach ($line in $probeLines) {
            $separator = $line.IndexOf('=')
            if ($separator -gt 0) { $probeValues[$line.Substring(0, $separator)] = $line.Substring($separator + 1) }
        }
        $cudaProbeStatus = New-Observation DETECTED 'cuda_profile_probe' 'PASS'
        $cudaRuntimeVersion = New-Observation DETECTED 'cudaRuntimeGetVersion' ([string]$probeValues['cuda.runtime.version'])
        $cudaRuntimeVersionRaw = New-Observation DETECTED 'cudaRuntimeGetVersion' ([string]$probeValues['cuda.runtime.version_raw'])
        $cudaDriverApiVersion = New-Observation DETECTED 'cudaDriverGetVersion' ([string]$probeValues['cuda.driver_api.version'])
        $cudaDriverApiVersionRaw = New-Observation DETECTED 'cudaDriverGetVersion' ([string]$probeValues['cuda.driver_api.version_raw'])
        $cudaCompiledRuntimeVersion = New-Observation DETECTED 'CUDART_VERSION' ([string]$probeValues['cuda.compiled_runtime.version'])
        $cudaCompiledRuntimeVersionRaw = New-Observation DETECTED 'CUDART_VERSION' ([string]$probeValues['cuda.compiled_runtime.version_raw'])
        $cudaDeviceCount = New-Observation DETECTED 'cudaGetDeviceCount' ([string]$probeValues['cuda.device_count'])
        $deviceCountValue = [int]$probeValues['cuda.device_count']
        $cudaDevices = @(for ($deviceIndex = 0; $deviceIndex -lt $deviceCountValue; $deviceIndex++) {
            $prefix = "cuda.device.$deviceIndex."
            [ordered]@{
                async_engine_count = New-Observation DETECTED 'cudaGetDeviceProperties' ([string]$probeValues[$prefix + 'async_engine_count'])
                can_map_host_memory = New-Observation DETECTED 'cudaGetDeviceProperties' ([string]$probeValues[$prefix + 'can_map_host_memory'])
                compute_capability = New-Observation DETECTED 'cudaGetDeviceProperties' ([string]$probeValues[$prefix + 'compute_capability'])
                concurrent_kernels = New-Observation DETECTED 'cudaGetDeviceProperties' ([string]$probeValues[$prefix + 'concurrent_kernels'])
                concurrent_managed_access = New-Observation DETECTED 'cudaGetDeviceProperties' ([string]$probeValues[$prefix + 'concurrent_managed_access'])
                cooperative_launch = New-Observation DETECTED 'cudaGetDeviceProperties' ([string]$probeValues[$prefix + 'cooperative_launch'])
                direct_managed_memory_access_from_host = New-Observation DETECTED 'cudaGetDeviceProperties' ([string]$probeValues[$prefix + 'direct_managed_memory_access_from_host'])
                host_native_atomic_supported = New-Observation DETECTED 'cudaGetDeviceProperties' ([string]$probeValues[$prefix + 'host_native_atomic_supported'])
                index = New-Observation DETECTED 'cudaGetDeviceProperties' ([string]$deviceIndex)
                l2_cache_bytes = New-Observation DETECTED 'cudaGetDeviceProperties' ([string]$probeValues[$prefix + 'l2_cache_bytes'])
                managed_memory = New-Observation DETECTED 'cudaGetDeviceProperties' ([string]$probeValues[$prefix + 'managed_memory'])
                max_grid_size = New-Observation DETECTED 'cudaGetDeviceProperties' ([string]$probeValues[$prefix + 'max_grid_size'])
                max_threads_dim = New-Observation DETECTED 'cudaGetDeviceProperties' ([string]$probeValues[$prefix + 'max_threads_dim'])
                max_threads_per_block = New-Observation DETECTED 'cudaGetDeviceProperties' ([string]$probeValues[$prefix + 'max_threads_per_block'])
                memory_bus_width_bits = New-Observation DETECTED 'cudaGetDeviceProperties' ([string]$probeValues[$prefix + 'memory_bus_width_bits'])
                memory_clock_khz = New-Observation DETECTED 'cudaGetDeviceProperties' ([string]$probeValues[$prefix + 'memory_clock_khz'])
                memory_pools_supported = New-Observation DETECTED 'cudaGetDeviceProperties' ([string]$probeValues[$prefix + 'memory_pools_supported'])
                multiprocessor_count = New-Observation DETECTED 'cudaGetDeviceProperties' ([string]$probeValues[$prefix + 'multiprocessor_count'])
                name = New-Observation DETECTED 'cudaGetDeviceProperties' ([string]$probeValues[$prefix + 'name'])
                pageable_memory_access = New-Observation DETECTED 'cudaGetDeviceProperties' ([string]$probeValues[$prefix + 'pageable_memory_access'])
                total_global_memory_bytes = New-Observation DETECTED 'cudaGetDeviceProperties' ([string]$probeValues[$prefix + 'total_global_memory_bytes'])
                unified_addressing = New-Observation DETECTED 'cudaGetDeviceProperties' ([string]$probeValues[$prefix + 'unified_addressing'])
                warp_size = New-Observation DETECTED 'cudaGetDeviceProperties' ([string]$probeValues[$prefix + 'warp_size'])
            }
        })
    } else {
        throw "CUDA profile probe execution failed: $($probeLines -join [Environment]::NewLine)"
    }
}

$cudaUmdVersion = New-Observation UNKNOWN 'nvidia-smi summary' 'UNKNOWN'
if ($nvidiaSmi) {
    $smiSummary = (& $nvidiaSmi.Source 2>$null | Out-String)
    $umdMatch = [regex]::Match($smiSummary, 'CUDA UMD Version:\s*([0-9.]+)')
    if ($umdMatch.Success) { $cudaUmdVersion = New-Observation DETECTED 'nvidia-smi CUDA UMD Version' $umdMatch.Groups[1].Value }
}

$visualStudioVersion = New-Observation UNKNOWN 'vswhere' 'UNKNOWN'
$cmakeVersion = Get-CommandVersion 'cmake.exe' @('--version')
$ninjaVersion = Get-CommandVersion 'ninja.exe' @('--version')
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (Test-Path -LiteralPath $vswhere) {
    $installationVersion = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion).Trim()
    $installationPath = (& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath).Trim()
    $visualStudioVersion = New-Observation DETECTED 'vswhere.installationVersion' $installationVersion
    if ($installationPath) {
        $bundledCmake = Join-Path $installationPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
        $bundledNinja = Join-Path $installationPath 'Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe'
        if (Test-Path -LiteralPath $bundledCmake) { $cmakeVersion = New-Observation DETECTED 'Visual Studio bundled cmake' ((& $bundledCmake --version | Select-Object -First 1).Trim()) }
        if (Test-Path -LiteralPath $bundledNinja) { $ninjaVersion = New-Observation DETECTED 'Visual Studio bundled ninja' ((& $bundledNinja --version | Select-Object -First 1).Trim()) }
    }
}

$msvcBinaryVersion = New-Observation UNKNOWN 'CMake CXX compiler binary' 'UNKNOWN'
$compilerCache = @(
    (Join-Path $repositoryRoot 'out\build\msvc-release\CMakeCache.txt'),
    (Join-Path $repositoryRoot 'out\build\msvc-debug\CMakeCache.txt')
) | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if ($compilerCache) {
    $compilerLine = Get-Content -LiteralPath $compilerCache | Where-Object { $_ -match '^CMAKE_CXX_COMPILER:[^=]+=' } | Select-Object -First 1
    if ($compilerLine) {
        $compilerPath = $compilerLine.Substring($compilerLine.IndexOf('=') + 1)
        if (Test-Path -LiteralPath $compilerPath) {
            $msvcBinaryVersion = New-Observation DETECTED 'cl.exe FileVersion' ((Get-Item -LiteralPath $compilerPath).VersionInfo.FileVersion)
        }
    }
}
$releaseDate = if ($bios.ReleaseDate) { $bios.ReleaseDate.ToUniversalTime().ToString('yyyy-MM-dd', [Globalization.CultureInfo]::InvariantCulture) } else { 'UNKNOWN' }

$identity = [ordered]@{
    baseboard = [ordered]@{
        manufacturer = New-Observation DETECTED 'Win32_BaseBoard.Manufacturer' ([string]$baseboard.Manufacturer)
        product = New-Observation DETECTED 'Win32_BaseBoard.Product' ([string]$baseboard.Product)
        version = New-Observation DETECTED 'Win32_BaseBoard.Version' ([string]$baseboard.Version)
    }
    bios = [ordered]@{
        manufacturer = New-Observation DETECTED 'Win32_BIOS.Manufacturer' ([string]$bios.Manufacturer)
        release_date_utc = New-Observation $(if ($releaseDate -eq 'UNKNOWN') { 'UNKNOWN' } else { 'DETECTED' }) 'Win32_BIOS.ReleaseDate' $releaseDate
        version = New-Observation DETECTED 'Win32_BIOS.SMBIOSBIOSVersion' ([string]$bios.SMBIOSBIOSVersion)
    }
    cpu = [ordered]@{
        brand = New-Observation DETECTED 'CPUID/selftest' ([string]$selfTest['cpu.brand'])
        cache_inventory = $cacheEntries
        features = [ordered]@{
            avx = New-Observation DETECTED 'CPUID+selftest OS state' ([string]$selfTest['cpu.avx'])
            avx2 = New-Observation DETECTED 'CPUID+selftest OS state' ([string]$selfTest['cpu.avx2'])
            avx512f = New-Observation DETECTED 'CPUID+selftest OS state' ([string]$selfTest['cpu.avx512f'])
            bmi2 = New-Observation DETECTED 'CPUID/selftest' ([string]$selfTest['cpu.bmi2'])
            sse2 = New-Observation DETECTED 'CPUID/selftest' ([string]$selfTest['cpu.sse2'])
        }
        logical_processors = New-Observation DETECTED 'Win32_Processor.NumberOfLogicalProcessors' ([string]$processor.NumberOfLogicalProcessors)
        manufacturer = New-Observation DETECTED 'Win32_Processor.Manufacturer' ([string]$processor.Manufacturer)
        max_clock_mhz_firmware = New-Observation DETECTED 'Win32_Processor.MaxClockSpeed' ([string]$processor.MaxClockSpeed)
        physical_cores = New-Observation DETECTED 'Win32_Processor.NumberOfCores' ([string]$processor.NumberOfCores)
        processor_groups = $groupEntries
        socket = New-Observation DETECTED 'Win32_Processor.SocketDesignation' ([string]$processor.SocketDesignation)
    }
    cuda = [ordered]@{
        compiled_runtime_version = $cudaCompiledRuntimeVersion
        compiled_runtime_version_raw = $cudaCompiledRuntimeVersionRaw
        detection_method = $cudaDetectionMethod
        devices = $cudaDevices
        driver_api_version = $cudaDriverApiVersion
        driver_api_version_raw = $cudaDriverApiVersionRaw
        host_compiler_path = $cudaHostCompilerPath
        nvidia_umd_version = $cudaUmdVersion
        nvidia_smi_path = $cudaNvidiaSmiPath
        nvcc_build = $cudaNvccBuild
        nvcc_path = $cudaNvccPath
        nvcc_release = $cudaNvccRelease
        probe_status = $cudaProbeStatus
        probe_executable_path = $cudaProbeExecutablePath
        probe_source_path = $cudaProbeSourcePath
        runtime_library_file_version = $cudaRuntimeLibraryVersion
        runtime_library_path = $cudaRuntimeLibraryPath
        runtime_version = $cudaRuntimeVersion
        runtime_version_raw = $cudaRuntimeVersionRaw
        toolkit_root = $cudaToolkitRoot
    }
    gpu = [ordered]@{
        nvidia = $nvidiaRows
        other_adapters = $otherAdapters
    }
    machine = [ordered]@{
        manufacturer = New-Observation DETECTED 'Win32_ComputerSystem.Manufacturer' ([string]$computer.Manufacturer)
        model = New-Observation DETECTED 'Win32_ComputerSystem.Model' ([string]$computer.Model)
    }
    memory = [ordered]@{
        module_capacity_sum_bytes = New-Observation DETECTED 'sum(Win32_PhysicalMemory.Capacity)' ([string](($memoryModules | ForEach-Object { [uint64]$_.capacity_bytes.value } | Measure-Object -Sum).Sum))
        modules = $memoryModules
        timings = New-Observation UNKNOWN 'SMBIOS/WMI' 'UNKNOWN'
        total_physical_bytes = New-Observation DETECTED 'Win32_ComputerSystem.TotalPhysicalMemory' ([string]$computer.TotalPhysicalMemory)
    }
    operating_system = [ordered]@{
        architecture = New-Observation DETECTED 'Win32_OperatingSystem.OSArchitecture' ([string]$operatingSystem.OSArchitecture)
        build = New-Observation DETECTED 'Win32_OperatingSystem.BuildNumber' ([string]$operatingSystem.BuildNumber)
        caption = New-Observation DETECTED 'Win32_OperatingSystem.Caption' ([string]$operatingSystem.Caption)
        version = New-Observation DETECTED 'Win32_OperatingSystem.Version' ([string]$operatingSystem.Version)
    }
    schema = 'primeforge.hardware-profile.v1'
    telemetry_capabilities = [ordered]@{
        cpu_effective_frequency = New-Observation UNKNOWN 'no validated provider integrated' 'UNKNOWN'
        cpu_power = New-Observation UNKNOWN 'no validated provider integrated' 'UNKNOWN'
        cpu_temperature = New-Observation UNKNOWN 'no validated provider integrated' 'UNKNOWN'
        gpu_core_frequency = Get-NvidiaTelemetryAvailability 3 'clocks.sm'
        gpu_hotspot_temperature = New-Observation UNKNOWN 'nvidia-smi query unavailable' 'UNKNOWN'
        gpu_memory_temperature = Get-NvidiaTelemetryAvailability 1 'temperature.memory'
        gpu_power = Get-NvidiaTelemetryAvailability 2 'power.draw'
        gpu_temperature = Get-NvidiaTelemetryAvailability 0 'temperature.gpu'
        gpu_utilization = Get-NvidiaTelemetryAvailability 4 'utilization.gpu'
        hardware_errors = New-Observation DETECTED 'Windows WHEA event log can be queried' 'AVAILABLE'
        ram_available = New-Observation DETECTED 'Win32_OperatingSystem.FreePhysicalMemory' 'AVAILABLE'
        vram_available = Get-NvidiaTelemetryAvailability 6 'memory.free'
    }
    toolchain = [ordered]@{
        cmake = $cmakeVersion
        cuda_toolkit_nvcc = $cudaNvccBuild
        git = Get-CommandVersion 'git.exe' @('--version')
        msvc_binary_version = $msvcBinaryVersion
        msvc_full_version = New-Observation DETECTED 'primeforge-selftest._MSC_FULL_VER' ([string]$selfTest['compiler._MSC_FULL_VER'])
        ninja = $ninjaVersion
        visual_studio = $visualStudioVersion
    }
}

$identityJson = ConvertTo-CanonicalJson -Value $identity
$identityBytes = [System.Text.UTF8Encoding]::new($false).GetBytes($identityJson)
$profileId = 'sha256:' + (Get-Sha256Hex -Bytes $identityBytes)

$profile = [ordered]@{}
foreach ($key in $identity.Keys) { $profile[$key] = $identity[$key] }
$profile['profile_id'] = $profileId
$profileJson = ConvertTo-CanonicalJson -Value $profile

$resolvedOutput = Resolve-RepositoryPath $OutputPath
Write-Utf8NoBom -Path $resolvedOutput -Content $profileJson
if ($IdentityOutputPath) {
    Write-Utf8NoBom -Path (Resolve-RepositoryPath $IdentityOutputPath) -Content $identityJson
}

Write-Output "hardware_profile.path=$resolvedOutput"
Write-Output "hardware_profile.id=$profileId"
Write-Output 'hardware_profile.status=PASS'
