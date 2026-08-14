[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [Parameter(Mandatory = $true)][string]$AExecutable,
    [Parameter(Mandatory = $true)][string[]]$AInputs,
    [Parameter(Mandatory = $true)][string]$BExecutable,
    [Parameter(Mandatory = $true)][string[]]$BInputs,
    [Parameter(Mandatory = $true)][string]$OutputDirectory,
    [Parameter(Mandatory = $true)][ValidateRange(1, 1000000)][int]$CandidateCount
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

function Resolve-ExistingFile {
    param([Parameter(Mandatory = $true)][string]$Path)
    $resolved = [System.IO.Path]::GetFullPath($Path)
    if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        throw "Required file is missing: $resolved"
    }
    return $resolved
}

function Invoke-NativeBatch {
    param(
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string]$InputFile,
        [Parameter(Mandatory = $true)][string]$StdoutFile,
        [Parameter(Mandatory = $true)][string]$StderrFile
    )

    $escapedInput = $InputFile.Replace('"', '\"')
    $startInfo = [System.Diagnostics.ProcessStartInfo]::new()
    $startInfo.FileName = $Executable
    $startInfo.Arguments = "--native-batch `"$escapedInput`" --phase-profile"
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true

    $process = [System.Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    if (-not $process.Start()) { throw "Failed to start $Executable" }
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $stdout = $stdoutTask.GetAwaiter().GetResult()
    $stderr = $stderrTask.GetAwaiter().GetResult()
    [System.IO.File]::WriteAllText($StdoutFile, $stdout, [System.Text.UTF8Encoding]::new($false))
    [System.IO.File]::WriteAllText($StderrFile, $stderr, [System.Text.UTF8Encoding]::new($false))
    if ($process.ExitCode -ne 0) {
        throw "Native batch failed with exit code $($process.ExitCode): $Executable"
    }
    return $stdout
}

function Start-GpuSampler {
    param([Parameter(Mandatory = $true)][string]$OutputFile)
    $nvidiaSmi = (Get-Command nvidia-smi.exe -ErrorAction Stop).Source
    return Start-Process -FilePath $nvidiaSmi -ArgumentList @(
        '--query-gpu=timestamp,temperature.gpu,power.draw,memory.used,utilization.gpu,clocks.sm,clocks.mem',
        '--format=csv,noheader,nounits', '--loop-ms=250'
    ) -RedirectStandardOutput $OutputFile -RedirectStandardError ($OutputFile + '.stderr') -NoNewWindow -PassThru
}

function Stop-GpuSampler {
    param([Parameter(Mandatory = $true)][System.Diagnostics.Process]$Process)
    if (-not $Process.HasExited) {
        Stop-Process -Id $Process.Id -ErrorAction Stop
        $Process.WaitForExit()
    }
}

function Get-NormalizedResults {
    param([Parameter(Mandatory = $true)][string[]]$Outputs)
    $records = foreach ($output in $Outputs) {
        foreach ($line in ($output -split "`r?`n")) {
            if ($line.StartsWith("PRIMEFORGE_NATIVE_BATCH_RESULT`t")) {
                $fields = $line -split "`t"
                if ($fields.Count -ne 7) { throw "Malformed result record: $line" }
                $fields[2..6] -join "`t"
            }
        }
    }
    return @($records | Sort-Object { [uint64](($_ -split "`t")[0]) })
}

$aExe = Resolve-ExistingFile $AExecutable
$bExe = Resolve-ExistingFile $BExecutable
$aFiles = @($AInputs | ForEach-Object { Resolve-ExistingFile $_ })
$bFiles = @($BInputs | ForEach-Object { Resolve-ExistingFile $_ })
$outputPath = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($outputPath) | Out-Null

$sequence = @(
    [pscustomobject]@{ Label = '01-A'; Variant = 'A'; Executable = $aExe; Inputs = $aFiles },
    [pscustomobject]@{ Label = '02-B'; Variant = 'B'; Executable = $bExe; Inputs = $bFiles },
    [pscustomobject]@{ Label = '03-B'; Variant = 'B'; Executable = $bExe; Inputs = $bFiles },
    [pscustomobject]@{ Label = '04-A'; Variant = 'A'; Executable = $aExe; Inputs = $aFiles }
)

$runs = @()
$referenceResults = $null
foreach ($entry in $sequence) {
    $gpuFile = Join-Path $outputPath ($entry.Label + '-gpu.csv')
    $sampler = Start-GpuSampler $gpuFile
    $stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        $outputs = @()
        for ($index = 0; $index -lt $entry.Inputs.Count; ++$index) {
            $prefix = '{0}-{1:D2}' -f $entry.Label, $index
            $outputs += Invoke-NativeBatch -Executable $entry.Executable -InputFile $entry.Inputs[$index] `
                -StdoutFile (Join-Path $outputPath ($prefix + '.stdout.log')) `
                -StderrFile (Join-Path $outputPath ($prefix + '.stderr.log'))
        }
    } finally {
        $stopwatch.Stop()
        Stop-GpuSampler $sampler
    }

    $normalized = @(Get-NormalizedResults $outputs)
    if ($normalized.Count -ne $CandidateCount) {
        throw "$($entry.Label) produced $($normalized.Count) records, expected $CandidateCount"
    }
    $gerbiczPasses = @($outputs | ForEach-Object {
        [regex]::Matches($_, 'gerbicz_status=PASS').Count
    } | Measure-Object -Sum).Sum
    if ($gerbiczPasses -ne $entry.Inputs.Count) {
        throw "$($entry.Label) did not report Gerbicz PASS for every native batch"
    }
    $canonical = $normalized -join "`n"
    if ($null -eq $referenceResults) { $referenceResults = $canonical }
    elseif ($canonical -cne $referenceResults) {
        throw "$($entry.Label) mathematical result, witness or RES64 differs from the reference"
    }

    $seconds = $stopwatch.Elapsed.TotalSeconds
    $runs += [pscustomobject]@{
        label = $entry.Label
        variant = $entry.Variant
        seconds = $seconds
        candidates_per_hour = $CandidateCount * 3600.0 / $seconds
        result_count = $normalized.Count
        gerbicz = 'PASS'
    }
}

$aMean = (@($runs | Where-Object variant -eq 'A' | Measure-Object seconds -Average).Average)
$bMean = (@($runs | Where-Object variant -eq 'B' | Measure-Object seconds -Average).Average)
$summary = [ordered]@{
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    order = 'A/B/B/A'
    candidate_count = $CandidateCount
    a_executable = $aExe
    a_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $aExe).Hash
    a_inputs = $aFiles
    b_executable = $bExe
    b_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $bExe).Hash
    b_inputs = $bFiles
    mathematical_results_identical = $true
    witnesses_identical = $true
    res64_identical = $true
    gerbicz = 'PASS'
    a_mean_seconds = $aMean
    b_mean_seconds = $bMean
    a_candidates_per_hour = $CandidateCount * 3600.0 / $aMean
    b_candidates_per_hour = $CandidateCount * 3600.0 / $bMean
    speedup = $aMean / $bMean
    runs = $runs
}

$json = $summary | ConvertTo-Json -Depth 8
[System.IO.File]::WriteAllText((Join-Path $outputPath 'summary.json'), $json + "`n", [System.Text.UTF8Encoding]::new($false))
$json
