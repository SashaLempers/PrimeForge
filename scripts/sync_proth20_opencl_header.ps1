[CmdletBinding()]
# SPDX-License-Identifier: Apache-2.0

param(
    [Parameter(Mandatory = $true)][string]$SourceFile,
    [Parameter(Mandatory = $true)][string]$HeaderFile,
    [Parameter(Mandatory = $true)][ValidatePattern('^[A-Za-z_][A-Za-z0-9_]*$')][string]$VariableName
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$sourcePath = [IO.Path]::GetFullPath($SourceFile)
$headerPath = [IO.Path]::GetFullPath($HeaderFile)
if (-not (Test-Path -LiteralPath $sourcePath -PathType Leaf)) {
    throw "OpenCL source is missing: $sourcePath"
}

$lf = "`n"
$builder = [Text.StringBuilder]::new()
[void]$builder.Append('/*' + $lf)
[void]$builder.Append('Copyright 2020, Yves Gallot' + $lf + $lf)
[void]$builder.Append('proth20 is free source code, under the MIT license (see LICENSE). You can redistribute, use and/or modify it.' + $lf)
[void]$builder.Append('Please give feedback to the authors if improvement is realized. It is distributed in the hope that it will be useful.' + $lf)
[void]$builder.Append('*/' + $lf + $lf)
[void]$builder.Append('#pragma once' + $lf + $lf)
[void]$builder.Append('#include <cstdint>' + $lf + $lf)
[void]$builder.Append('static const char * const ' + $VariableName + ' = \' + $lf)

foreach ($line in [IO.File]::ReadLines($sourcePath)) {
    $escaped = $line.Replace('\', '\\').Replace('"', '\"')
    [void]$builder.Append('"' + $escaped + '\n" \' + $lf)
}
[void]$builder.Append('"";' + $lf)

[IO.Directory]::CreateDirectory((Split-Path -Parent $headerPath)) | Out-Null
[IO.File]::WriteAllText($headerPath, $builder.ToString(), [Text.UTF8Encoding]::new($false))

Write-Host "proth20.opencl_header=$headerPath"
Write-Host "proth20.opencl_header.sha256=$((Get-FileHash -Algorithm SHA256 -LiteralPath $headerPath).Hash.ToLowerInvariant())"
Write-Host 'proth20.opencl_header.status=PASS'
