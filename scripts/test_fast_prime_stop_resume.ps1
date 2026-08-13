[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [string]$CampaignDirectory = 'out\fast-prime\stop-resume-validation',
    [string]$OutputFile = 'out\fast-prime\stop-resume-validation.json',
    [string]$Proth20Executable = 'C:\Users\sashack\source\repos\PrimeForge\out\oracles\proth20-batch\proth20-batch.exe',
    [string]$PariExecutable = 'C:\Users\sashack\source\repos\PrimeForge\out\oracles\pari-gp64-2.17.4.exe'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$utf8 = [Text.UTF8Encoding]::new($false)

function Resolve-ProjectPath([string]$Path) {
    if ([IO.Path]::IsPathRooted($Path)) { return [IO.Path]::GetFullPath($Path) }
    return [IO.Path]::GetFullPath((Join-Path $root $Path))
}

$campaignPath = Resolve-ProjectPath $CampaignDirectory
$outputPath = Resolve-ProjectPath $OutputFile
$prothPath = Resolve-ProjectPath $Proth20Executable
$pariPath = Resolve-ProjectPath $PariExecutable
$runner = Join-Path $root 'scripts\run_prime_discovery.ps1'
foreach ($path in @($prothPath, $pariPath, $runner)) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Required file missing: $path" }
}
if (Test-Path -LiteralPath $campaignPath) { throw "Campaign directory must be new: $campaignPath" }
New-Item -ItemType Directory -Path $campaignPath -Force | Out-Null

$commonArguments = @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $runner,
    '-CampaignDirectory', $campaignPath,
    '-KStart', '75939069', '-KStop', '76077027', '-Exponent', '66411',
    '-SieveBound', '2000000000', '-BatchSize', '5', '-ValidationCount', '5',
    '-Proth20Executable', $prothPath
)
$firstStdout = Join-Path $campaignPath 'controller-stop.stdout.log'
$firstStderr = Join-Path $campaignPath 'controller-stop.stderr.log'
$controller = Start-Process -FilePath 'powershell.exe' -ArgumentList $commonArguments `
    -WorkingDirectory $root -RedirectStandardOutput $firstStdout `
    -RedirectStandardError $firstStderr -PassThru -NoNewWindow
$checkpointPath = Join-Path $campaignPath 'checkpoint.json'
$operatorStop = Join-Path $campaignPath 'work\operator.stop'
$deadline = [DateTime]::UtcNow.AddMinutes(2)
$stopWritten = $false
while (-not $controller.HasExited -and [DateTime]::UtcNow -lt $deadline) {
    $workerLog = Join-Path $campaignPath 'logs\unit-0001-attempt-001.stdout.log'
    if (Test-Path -LiteralPath $workerLog) {
        $completedMarkers = @(Select-String -LiteralPath $workerLog -Pattern '^PRIMEFORGE_BATCH_COMPLETE\t').Count
        $active = @(Get-CimInstance Win32_Process | Where-Object {
            $_.Name -eq 'proth20-batch.exe' -and $_.CommandLine -like "*$campaignPath*"
        })
        if ($completedMarkers -ge 1 -and $active.Count -eq 1) {
            [IO.File]::WriteAllText($operatorStop, "stop`n", $utf8)
            $stopWritten = $true
            break
        }
    }
    Start-Sleep -Milliseconds 100
    $controller.Refresh()
}
if (-not $stopWritten) { throw 'Could not request a stop after a durable completed unit.' }
if (-not $controller.WaitForExit(45000)) {
    throw 'Discovery controller did not complete the cooperative stop in 45 seconds.'
}
if ((Get-Item -LiteralPath $firstStderr).Length -ne 0) { throw 'Stopped controller wrote stderr.' }
$stopped = Get-Content -Raw -LiteralPath $checkpointPath | ConvertFrom-Json
if ([string]$stopped.campaign_status -ne 'IN_PROGRESS' -or
    [int]$stopped.completed_candidates -lt 1 -or [int]$stopped.completed_candidates -ge 5) {
    throw 'The deliberate stop did not preserve a non-terminal durable checkpoint.'
}
$stoppedCheckpointHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $checkpointPath).Hash.ToLowerInvariant()
$manifestPath = Join-Path $campaignPath 'campaign.json'
$manifestHashBeforeResume = (Get-FileHash -Algorithm SHA256 -LiteralPath $manifestPath).Hash.ToLowerInvariant()
$graceful = @(Get-ChildItem -LiteralPath (Join-Path $campaignPath 'logs') -Filter '*.watchdog.log' |
    ForEach-Object { Select-String -LiteralPath $_.FullName -Pattern 'reason=EXTERNAL_GRACEFUL_STOP' })
if ($graceful.Count -eq 0) { throw 'Watchdog did not record the deliberate graceful stop.' }

$resumeStdout = Join-Path $campaignPath 'controller-resume.stdout.log'
$resumeStderr = Join-Path $campaignPath 'controller-resume.stderr.log'
$resumeOutput = @(& powershell.exe @commonArguments 2> $resumeStderr)
$resumeExit = $LASTEXITCODE
[IO.File]::WriteAllLines($resumeStdout, @($resumeOutput | ForEach-Object { [string]$_ }), $utf8)
if ($resumeExit -ne 0 -or (Get-Item -LiteralPath $resumeStderr).Length -ne 0) {
    throw "Resumed discovery controller failed with exit code $resumeExit."
}
$final = Get-Content -Raw -LiteralPath $checkpointPath | ConvertFrom-Json
if ([string]$final.campaign_status -notin @('COMPLETE_NO_PRIME', 'PRIME_FOUND')) {
    throw "Resume did not reach a valid terminal state: $($final.campaign_status)"
}
if ([int]$final.completed_candidates -ne 5) { throw 'Resume did not complete exactly five candidates.' }
if ((Get-FileHash -Algorithm SHA256 -LiteralPath $manifestPath).Hash.ToLowerInvariant() -ne $manifestHashBeforeResume) {
    throw 'Immutable campaign manifest changed across resume.'
}

$resultRows = @(Import-Csv -Delimiter "`t" -LiteralPath (Join-Path $campaignPath 'results.tsv'))
if ($resultRows.Count -ne 5) { throw 'Results table does not contain exactly five completed candidates.' }
$keys = @($resultRows | ForEach-Object { "$($_.k)/$($_.n)" })
if (@($keys | Select-Object -Unique).Count -ne 5) { throw 'Results table contains a duplicate candidate.' }
$bases = @{}
foreach ($log in Get-ChildItem -LiteralPath (Join-Path $campaignPath 'logs') -Filter '*.stdout.log') {
    foreach ($line in [IO.File]::ReadLines($log.FullName)) {
        if ($line -match '^([0-9]+) \* 2\^([0-9]+) \+ 1 is (prime|composite), a = ([0-9]+),') {
            $bases["$($Matches[1])/$($Matches[2])"] = [uint32]$Matches[4]
        }
    }
}
if ($bases.Count -ne 5) { throw 'Could not extract five Proth20 witnesses.' }

$gpLines = [Collections.Generic.List[string]]::new()
foreach ($row in $resultRows) {
    $key = "$($row.k)/$($row.n)"
    $base = $bases[$key]
    $gpLines.Add(('N={0}*2^{1}+1; a={2}; j=kronecker(a,N); r=lift(Mod(a,N)^((N-1)/2)); print("PRIMEFORGE_PARI\t{0}\t{1}\t",j,"\t",if(j==-1 && r==N-1,"PROVEN_PRIME","COMPOSITE"));' -f $row.k, $row.n, $base))
}
$gpLines.Add('quit;')
$gpProgram = Join-Path $campaignPath 'independent-verification.gp'
[IO.File]::WriteAllLines($gpProgram, $gpLines, $utf8)
$pariStdout = Join-Path $campaignPath 'independent-verification.stdout.log'
$pariStderr = Join-Path $campaignPath 'independent-verification.stderr.log'
$pariTimer = [Diagnostics.Stopwatch]::StartNew()
$pariLines = @(& $pariPath -q $gpProgram 2> $pariStderr)
$pariExit = $LASTEXITCODE
$pariTimer.Stop()
[IO.File]::WriteAllLines($pariStdout, @($pariLines | ForEach-Object { [string]$_ }), $utf8)
if ($pariExit -ne 0 -or (Get-Item -LiteralPath $pariStderr).Length -ne 0) {
    throw "PARI/GP independent verification failed with exit code $pariExit."
}
$pariVerdicts = @{}
foreach ($line in $pariLines) {
    if ([string]$line -match '^PRIMEFORGE_PARI\t([0-9]+)\t([0-9]+)\t(-?[0-9]+)\t(PROVEN_PRIME|COMPOSITE)$') {
        if ($Matches[3] -ne '-1') { throw "PARI witness has Jacobi symbol $($Matches[3])." }
        $pariVerdicts["$($Matches[1])/$($Matches[2])"] = $Matches[4]
    }
}
if ($pariVerdicts.Count -ne 5) { throw 'PARI/GP did not report five verdicts.' }
foreach ($row in $resultRows) {
    $key = "$($row.k)/$($row.n)"
    if (-not $pariVerdicts.ContainsKey($key) -or $pariVerdicts[$key] -ne $row.primality_status) {
        throw "Independent verification disagrees for $key."
    }
}

$result = [pscustomobject][ordered]@{
    schema = 'primeforge.fast_prime.stop_resume.v1'
    recorded_utc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    campaign_id = $final.campaign_id
    k_start = 75939069
    k_stop = 76077027
    exponent = 66411
    sieve_bound = 2000000000
    interrupted_completed_candidates = [int]$stopped.completed_candidates
    stopped_checkpoint_sha256 = $stoppedCheckpointHash
    manifest_sha256 = $manifestHashBeforeResume
    final_status = [string]$final.campaign_status
    final_completed_candidates = [int]$final.completed_candidates
    unique_results = $keys.Count
    gaps = 0
    duplicates = 0
    proth20_pari_agreements = $pariVerdicts.Count
    independent_engine = 'PARI/GP 2.17.4 modular arithmetic and Kronecker symbol'
    independent_elapsed_seconds = $pariTimer.Elapsed.TotalSeconds
    independent_seconds_per_candidate = $pariTimer.Elapsed.TotalSeconds / $pariVerdicts.Count
    graceful_stop_observed = $true
    resume_pass = $true
    status = 'PASS'
}
[IO.File]::WriteAllText($outputPath, ($result | ConvertTo-Json -Depth 6) + "`n", $utf8)
Write-Host "fast_prime.stop_resume.interrupted_completed=$($result.interrupted_completed_candidates)"
Write-Host "fast_prime.stop_resume.final_completed=$($result.final_completed_candidates)"
Write-Host "fast_prime.stop_resume.independent_agreements=$($result.proth20_pari_agreements)"
Write-Host "fast_prime.stop_resume.output=$outputPath"
Write-Host 'fast_prime.stop_resume.status=PASS'
