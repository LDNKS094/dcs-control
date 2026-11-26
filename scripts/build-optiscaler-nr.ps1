#Requires -Version 7
param([Parameter(Mandatory)][string]$DistributionRoot)
$ErrorActionPreference = 'Stop'
$sourceRoot = & python (Join-Path $PSScriptRoot 'prepare-render-source.py') optiscaler-nr
if ($LASTEXITCODE -ne 0) { throw 'Cannot prepare the OptiScaler NR source.' }
$commit = (Get-Content -LiteralPath (Join-Path $PSScriptRoot 'integration-backends.json') -Raw | ConvertFrom-Json).optiScalerNr
& (Join-Path $PSScriptRoot 'msvc.cmd') msbuild (Join-Path $sourceRoot 'OptiScaler.sln') /m:4 /p:Configuration=Release /p:Platform=x64 /p:PostBuildEventUseInBuild=false /verbosity:minimal /nologo
if ($LASTEXITCODE -ne 0) { throw 'OptiScaler NR build failed.' }
& (Join-Path $PSScriptRoot 'prepare-desktop-backend.ps1') -DistributionRoot $DistributionRoot -SourceCommit $commit -SourceRoot $sourceRoot
