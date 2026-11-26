#Requires -Version 7
param([string]$Output)
if (-not $Output) { $Output = Join-Path (Split-Path -Parent $PSScriptRoot) 'artifacts/vdxr-dev' }
& (Join-Path $PSScriptRoot 'build-desktop-dev.ps1') -Output $Output -IncludeVdxr
