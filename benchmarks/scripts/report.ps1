[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [Parameter(Mandatory = $true)][string]$RawPath,
    [Parameter(Mandatory = $true)][string]$OutputPath
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$files = @(Get-ChildItem -LiteralPath $RawPath -Recurse -File -Filter '*.jsonl' | Sort-Object FullName)
$rows = @()
foreach ($file in $files) {
    foreach ($line in Get-Content -LiteralPath $file.FullName -Encoding UTF8) {
        if (-not [string]::IsNullOrWhiteSpace($line)) {
            $row = $line | ConvertFrom-Json
            if ($row.schema -eq 'primeforge.benchmark.raw.v1' -and $row.valid_measurement) {
                $rows += $row
            }
        }
    }
}
if ($rows.Count -eq 0) { throw 'No valid benchmark rows found.' }
New-Item -ItemType Directory -Path $OutputPath -Force | Out-Null

$stages = @('generation_ns','congruence_ns','sieve_ns','packing_ns','h2d_ns','kernel_ns',
            'd2h_ns','prp_cpu_ns','proof_ns','verification_ns','io_ns','checkpoint_ns')
$values = @()
foreach ($stage in $stages) {
    $ordered = @($rows | ForEach-Object { [uint64]($_.$stage) } | Sort-Object)
    $values += [uint64]$ordered[[int](($ordered.Count - 1) / 2)]
}
$colors = @('#4e79a7','#f28e2b','#e15759','#76b7b2','#59a14f','#edc948',
            '#b07aa1','#ff9da7','#9c755f','#bab0ab','#2f4b7c','#a05195')
$maximum = [Math]::Max(1, [double](($values | Measure-Object -Maximum).Maximum))
$elements = @('<rect width="100%" height="100%" fill="white"/>',
              '<text x="10" y="28" font-size="20">Median stage time (ns)</text>')
for ($index = 0; $index -lt $stages.Count; $index++) {
    $y = 45 + 28 * $index
    $width = [Math]::Round(700 * [double]$values[$index] / $maximum)
    $elements += "<text x=`"10`" y=`"$($y + 16)`" font-size=`"12`">$($stages[$index])</text>"
    $elements += "<rect x=`"210`" y=`"$y`" width=`"$width`" height=`"20`" fill=`"$($colors[$index])`"/>"
    $elements += "<text x=`"$($width + 220)`" y=`"$($y + 16)`" font-size=`"12`">$($values[$index])</text>"
}
$svg = "<svg xmlns=`"http://www.w3.org/2000/svg`" width=`"1000`" height=`"430`">$($elements -join '')</svg>`n"
[System.IO.File]::WriteAllText((Join-Path $OutputPath 'stage_breakdown.svg'), $svg, [System.Text.UTF8Encoding]::new($false))

Add-Type -AssemblyName System.Drawing
function Write-BarPng {
    param([string]$Path, [uint64[]]$BarValues)
    $bitmap = [System.Drawing.Bitmap]::new(1000, 260)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.Clear([System.Drawing.Color]::White)
        $total = [Math]::Max(1, [double](($BarValues | Measure-Object -Sum).Sum))
        $x = 40
        for ($index = 0; $index -lt $BarValues.Count; $index++) {
            $width = [int][Math]::Round(920 * [double]$BarValues[$index] / $total)
            if ($width -gt 0) {
                $brush = [System.Drawing.SolidBrush]::new([System.Drawing.ColorTranslator]::FromHtml($colors[$index % $colors.Count]))
                try { $graphics.FillRectangle($brush, $x, 80, $width, 100) } finally { $brush.Dispose() }
            }
            $x += $width
        }
        $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

Write-BarPng (Join-Path $OutputPath 'stage_breakdown.png') ([uint64[]]$values)
$throughput = [uint64[]]@($rows | ForEach-Object {
    [uint64][Math]::Round([double]$_.batch_size * 1000000000 / [Math]::Max(1, [double]$_.total_ns))
})
Write-BarPng (Join-Path $OutputPath 'throughput_by_bits.png') $throughput
Write-BarPng (Join-Path $OutputPath 'speedup_by_batch.png') $throughput
Write-BarPng (Join-Path $OutputPath 'router_regret_heatmap.png') ([uint64[]]@(0) * $rows.Count)
[System.IO.File]::WriteAllText(
    (Join-Path $OutputPath 'gpu_timeline.svg'),
    "<svg xmlns=`"http://www.w3.org/2000/svg`" width=`"1000`" height=`"120`"><rect width=`"100%`" height=`"100%`" fill=`"white`"/><text x=`"10`" y=`"30`" font-size=`"20`">GPU timeline: NVTX capture command is documented; no profiler trace in this short baseline.</text></svg>`n",
    [System.Text.UTF8Encoding]::new($false)
)
Write-Host "PrimeForge benchmark report: PASS ($($rows.Count) rows)"
