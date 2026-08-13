[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [Parameter(Mandatory = $true)][string]$InputLog,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [string]$NsightReport = ''
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$utf8 = [Text.UTF8Encoding]::new($false)
$culture = [Globalization.CultureInfo]::InvariantCulture

function Resolve-ProjectPath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $repositoryRoot $Path))
}

$inputPath = Resolve-ProjectPath $InputLog
$outputPath = Resolve-ProjectPath $OutputDirectory
if (-not (Test-Path -LiteralPath $inputPath -PathType Leaf)) { throw "Missing kernel profile: $inputPath" }
New-Item -ItemType Directory -Path $outputPath -Force | Out-Null

$kernels = [Collections.Generic.List[object]]::new()
$mainWallNanoseconds = $null
$mainSquaringCount = $null
foreach ($line in [IO.File]::ReadLines($inputPath)) {
    if ($line -match '^PRIMEFORGE_KERNEL\t([0-9]+)\t([0-9]+)\t([0-9]+)\t([^\t]+)\t([0-9]+)\t([0-9]+)$') {
        $kernels.Add([pscustomobject][ordered]@{
            candidate_index = [uint64]$Matches[1]
            k = [uint64]$Matches[2]
            n = [uint64]$Matches[3]
            kernel = $Matches[4]
            launches = [uint64]$Matches[5]
            gpu_nanoseconds = [uint64]$Matches[6]
            mean_gpu_nanoseconds = [double]$Matches[6] / [double]$Matches[5]
        })
    } elseif ($line -match '^PRIMEFORGE_PHASE\t[0-9]+\t[0-9]+\t[0-9]+\tMAIN_SQUARING_LOOP\t([0-9]+)$') {
        $mainWallNanoseconds = [uint64]$Matches[1]
    } elseif ($line -match '^PRIMEFORGE_COUNTER\t[0-9]+\t[0-9]+\t[0-9]+\tMAIN_SQUARING_COUNT\t([0-9]+)$') {
        $mainSquaringCount = [uint64]$Matches[1]
    }
}
if ($kernels.Count -eq 0) { throw 'No PRIMEFORGE_KERNEL records were found.' }
if ($null -eq $mainWallNanoseconds -or $null -eq $mainSquaringCount) {
    throw 'Main-loop wall time or squaring count is missing.'
}
$gpuNanoseconds = [uint64](($kernels | Select-Object -ExpandProperty gpu_nanoseconds |
    Measure-Object -Sum).Sum)
$launches = [uint64](($kernels | Select-Object -ExpandProperty launches |
    Measure-Object -Sum).Sum)
$serializedFraction = [double]$gpuNanoseconds / [double]$mainWallNanoseconds
$nsight = [ordered]@{ status='NOT_SUPPLIED' }
if (-not [string]::IsNullOrWhiteSpace($NsightReport)) {
    $nsightPath = Resolve-ProjectPath $NsightReport
    if (-not (Test-Path -LiteralPath $nsightPath -PathType Leaf)) { throw "Missing Nsight report: $nsightPath" }
    $nsight = [ordered]@{
        status = 'CAPTURED_WITH_LIMITATIONS'
        path = $nsightPath
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $nsightPath).Hash.ToLowerInvariant()
        opencl_activity_trace = 'UNAVAILABLE_IN_NSYS_REPORT_SET'
        cpu_sampling = 'UNAVAILABLE_REQUIRES_ADMINISTRATIVE_PRIVILEGES'
        cpu_context_switch_trace = 'UNAVAILABLE_REQUIRES_ADMINISTRATIVE_PRIVILEGES'
        gpu_driver_metrics = 'UNAVAILABLE_ERR_NVGPUCTRPERM'
    }
}
$summary = [pscustomobject][ordered]@{
    schema = 'primeforge.proth20.kernel-profile.summary.v1'
    generated_utc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    input_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $inputPath).Hash.ToLowerInvariant()
    candidate_k = $kernels[0].k
    candidate_n = $kernels[0].n
    quick_probe = $true
    main_squaring_count = $mainSquaringCount
    kernel_types = $kernels.Count
    kernel_launches = $launches
    summed_opencl_event_gpu_nanoseconds = $gpuNanoseconds
    profiled_main_loop_wall_nanoseconds = $mainWallNanoseconds
    serialized_opencl_event_fraction_percent = 100.0 * $serializedFraction
    profiled_host_wait_and_instrumentation_fraction_percent = 100.0 * (1.0 - $serializedFraction)
    interpretation = @(
        'The OpenCL queue is in-order, so summed event durations estimate serialized device work.',
        'Event profiling waits for every kernel and therefore changes wall time.',
        'The residual is host wait plus profiling overhead; it is not a production GPU-idle claim.',
        'The quick probe stops after a bounded main-loop prefix and does not classify primality.'
    )
    nsight = $nsight
    status = 'PASS_WITH_DOCUMENTED_PROFILING_OVERHEAD'
}

$lines = @("kernel`tlaunches`tgpu_nanoseconds`tmean_gpu_nanoseconds")
foreach ($kernel in @($kernels | Sort-Object gpu_nanoseconds -Descending)) {
    $lines += @($kernel.kernel, $kernel.launches, $kernel.gpu_nanoseconds,
        $kernel.mean_gpu_nanoseconds.ToString('0.000', $culture)) -join "`t"
}
[IO.File]::WriteAllLines((Join-Path $outputPath 'kernels.tsv'), $lines, $utf8)
[IO.File]::WriteAllText((Join-Path $outputPath 'kernel-summary.json'),
    ($summary | ConvertTo-Json -Depth 8) + "`n", $utf8)
Write-Host "proth20.kernel_profile.launches=$launches"
Write-Host ('proth20.kernel_profile.opencl_event_fraction_percent=' +
    $summary.serialized_opencl_event_fraction_percent.ToString('0.000000', $culture))
Write-Host "proth20.kernel_profile.output=$outputPath"
Write-Host 'proth20.kernel_profile.status=PASS_WITH_DOCUMENTED_PROFILING_OVERHEAD'
